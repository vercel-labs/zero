import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { access, mkdtemp, mkdir, readdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, relative, sep } from "node:path";
import { describe, it } from "node:test";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const root = process.cwd();
const zero = join(root, "bin", "zero");
const nativeZero = join(root, ".zero", "bin", "zero");
const supportedTargets = [
  "darwin-arm64",
  "darwin-x64",
  "linux-musl-x64",
  "linux-musl-arm64",
  "linux-x64",
  "linux-arm64",
  "win32-x64.exe",
  "win32-arm64.exe",
];
const artifactSummaryPattern = /\((\d+ B|\d+\.\d KiB|\d+\.\d MiB|\d+\.\d GiB), (\d+ ms|\d+\.\d s)\)/;
const runnableDirectTarget =
  process.platform === "darwin" && process.arch === "arm64" ? "darwin-arm64" :
  process.platform === "linux" && process.arch === "x64" ? "linux-musl-x64" :
  null;

function runnableBuildArgs(input: string, out: string) {
  if (runnableDirectTarget) return ["build", "--emit", "exe", "--target", runnableDirectTarget, input, "--out", out];
  throw new Error("no runnable direct target for this host");
}

function runZero(args: string[], options: { cwd?: string; env?: NodeJS.ProcessEnv } = {}) {
  return execFileAsync(zero, args, { cwd: options.cwd ?? root, env: options.env ?? process.env });
}

function runNativeZero(args: string[], options: { cwd?: string; env?: NodeJS.ProcessEnv } = {}) {
  return execFileAsync(nativeZero, args, { cwd: options.cwd ?? root, env: options.env ?? process.env });
}

async function collectSkillMdFiles(dir: string): Promise<string[]> {
  const ignoredDirs = new Set([".git", ".next", ".zero", "node_modules"]);
  let entries;
  try {
    entries = await readdir(dir, { withFileTypes: true });
  } catch {
    return [];
  }

  const files: string[] = [];
  for (const entry of entries) {
    if (entry.isDirectory()) {
      if (ignoredDirs.has(entry.name)) continue;
      files.push(...await collectSkillMdFiles(join(dir, entry.name)));
    } else if (entry.isFile() && entry.name === "SKILL.md") {
      files.push(relative(root, join(dir, entry.name)).split(sep).join("/"));
    }
  }
  return files.sort();
}

describe("native zero CLI", () => {
  it("prints a terse plain version with the build hash", async () => {
    const versionPattern = /^zero 0\.3\.4 \(build (?:[0-9a-f]{7,40}|unknown)\)\n$/;
    assert.match((await runZero(["--version"])).stdout, versionPattern);
    assert.match((await runNativeZero(["--version"])).stdout, versionPattern);
  });

  it("checks directly and rejects removed legacy build flags", async () => {
    const check = await runZero(["check", "examples/hello.graph"]);
    assert.match(check.stdout, /ok/);

    const removedEmit = await runZero(["build", "--json", "--emit", "c", "examples/hello.graph"]).catch((error) => error);
    assert.notEqual(removedEmit.code, 0);
    assert.equal(JSON.parse(removedEmit.stdout).diagnostics[0].code, "BLD003");

    const removedFlag = await runZero(["build", "--json", "--legacy-backend", "examples/hello.graph"]).catch((error) => error);
    assert.notEqual(removedFlag.code, 0);
    assert.equal(JSON.parse(removedFlag.stdout).diagnostics[0].code, "BLD003");
  });

  it("builds and runs an executable", async () => {
    const cwd = await mkdtemp(join(tmpdir(), "zero-cli-"));
    const exe = join(cwd, "add");

    try {
      const build = await runZero(runnableBuildArgs("examples/add.graph", exe));
      assert.ok(build.stdout.includes(`${exe} (`));
      assert.match(build.stdout, artifactSummaryPattern);
      const run = await execFileAsync(exe, []);
      assert.match(run.stdout, /math works/);
      await assert.rejects(access(`${exe}.c`));

      const runExe = join(cwd, "add-run");
      const zeroRun = await runZero(["run", "--out", runExe, "examples/add.graph"]);
      assert.match(zeroRun.stdout, /math works/);
      assert.equal(zeroRun.stderr, "");
      await assert.rejects(access(`${runExe}.c`));

      const zeroRunWithArgs = await runZero(["run", "--out", join(cwd, "add-run-args"), "examples/add.graph", "--", "--emit", "c"]);
      assert.match(zeroRunWithArgs.stdout, /math works/);

      const exitCodeRun = await runZero(["run", "--out", join(cwd, "return42"), "examples/direct-exe-return.graph"]).catch((error) => error);
      assert.equal(exitCodeRun.code, 42);
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("checks and builds a manifest project", async () => {
    const cwd = await mkdtemp(join(tmpdir(), "zero-project-"));
    const project = join(cwd, "demo");
    const src = join(project, "src");
    const exe = join(cwd, "demo-exe");

    try {
      await mkdir(src, { recursive: true });
      await writeFile(
        join(project, "zero.json"),
        `{
  "package": { "name": "demo", "version": "0.1.0" },
  "targets": { "cli": { "kind": "exe", "main": "src/main.0" } }
}
`,
      );
      await writeFile(
        join(src, "main.0"),
        `pub fn main(world: World) -> Void raises {
    check world.out.write("hello from demo\\n")
}
`,
      );
      await runZero(["import", project]);

      assert.match((await runZero(["check", project])).stdout, /ok/);
      await runZero(runnableBuildArgs(project, exe));
      assert.match((await execFileAsync(exe, [])).stdout, /hello from demo/);
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("reports graph, size, objects, and targets", async () => {
    const graph = JSON.parse((await runZero(["inspect", "--json", "examples/point.graph"])).stdout);
    assert.equal(graph.shapes[0].name, "Point");
    assert.equal(graph.functions.some((item: { name: string }) => item.name === "main"), true);

    const size = JSON.parse((await runZero(["size", "--json", "examples/hello.graph"])).stdout);
    assert.equal(size.schemaVersion, 1);
    assert.equal(size.generatedCBytes, 0);

    const objOut = join(tmpdir(), `zero-target-${Date.now()}.o`);
    try {
      const objBuild = await runZero(["build", "--emit", "obj", "--target", "linux-musl-arm64", "examples/direct-exe-return.graph", "--out", objOut]);
      assert.ok(objBuild.stdout.includes(`${objOut} (`));
      assert.match(objBuild.stdout, artifactSummaryPattern);
      const bytes = await readFile(objOut);
      assert.equal(bytes[0], 0x7f);
      assert.equal(bytes[1], 0x45);
    } finally {
      await rm(objOut, { force: true });
    }

    const targets = JSON.parse((await runZero(["targets"])).stdout);
    assert.deepEqual(
      targets.targets.map((target: { name: string }) => target.name),
      supportedTargets,
    );
    assert.equal(targets.targets.find((target: { name: string }) => target.name === "linux-musl-x64")?.compilerTarget, "x86_64-linux-musl");
    assert.equal(targets.targets.find((target: { name: string }) => target.name === "win32-x64.exe")?.exeSuffix, ".exe");
  });

  it("lists and retrieves bundled skills", async () => {
    assert.deepEqual(await collectSkillMdFiles(root), ["skills/zero/SKILL.md"]);

    const list = JSON.parse((await runZero(["skills", "list", "--json"])).stdout);
    assert.equal(list.success, true);
    const skillNames = new Set(list.data.map((skill: { name: string }) => skill.name));
    for (const name of [
      "zero",
      "agent",
      "builds",
      "diagnostics",
      "language",
      "packages",
      "stdlib",
      "testing",
    ]) {
      assert.equal(skillNames.has(name), true);
    }

    const zeroSkill = JSON.parse((await runZero(["skills", "get", "zero", "--full", "--json"])).stdout);
    assert.equal(zeroSkill.success, true);
    assert.match(zeroSkill.data[0].content, /# Zero/);
    assert.match(zeroSkill.data[0].content, /zero skills get zero --full/);
    assert.equal(zeroSkill.data[0].files, undefined);

    const languageSkill = JSON.parse((await runZero(["skills", "get", "language", "--json"])).stdout);
    assert.equal(languageSkill.success, true);
    assert.match(languageSkill.data[0].content, /# zerolang Language/);
    assert.match(languageSkill.data[0].content, /pub fn main/);

    const diagnosticSkill = JSON.parse((await runZero(["skills", "get", "diagnostics", "--json"])).stdout);
    assert.equal(diagnosticSkill.success, true);
    assert.match(diagnosticSkill.data[0].content, /fixSafety/);

    const missing = await runZero(["skills", "get", "missing", "--json"]).catch((error) => error);
    assert.notEqual(missing.code, 0);
    assert.equal(JSON.parse(missing.stdout).success, false);

    const removedPath = await runZero(["skills", "path", "zero", "--json"]).catch((error) => error);
    assert.notEqual(removedPath.code, 0);
    assert.match(JSON.parse(removedPath.stdout).error, /Unknown skills subcommand: path/);

    const badSkillsFlag = await runZero(["skills", "-x"]).catch((error) => error);
    assert.notEqual(badSkillsFlag.code, 0);
    assert.match(badSkillsFlag.stderr, /Unknown skills flag: -x/);

    const badListFlag = await runZero(["skills", "list", "--unknown", "--json"]).catch((error) => error);
    assert.notEqual(badListFlag.code, 0);
    assert.match(JSON.parse(badListFlag.stdout).error, /Unknown skills flag: --unknown/);

    const badGetFlag = await runZero(["skills", "get", "language", "--unknown", "--json"]).catch((error) => error);
    assert.notEqual(badGetFlag.code, 0);
    assert.match(JSON.parse(badGetFlag.stdout).error, /Unknown skills flag: --unknown/);

    const nativeList = JSON.parse((await runNativeZero(["skills", "list", "--json"])).stdout);
    assert.equal(nativeList.success, true);
    assert.equal(nativeList.data.some((skill: { name: string }) => skill.name === "language"), true);

    const nativeLanguageSkill = JSON.parse((await runNativeZero(["skills", "get", "language", "--json"])).stdout);
    assert.equal(nativeLanguageSkill.success, true);
    assert.match(nativeLanguageSkill.data[0].content, /# zerolang Language/);
  });

  it("handles target-specific executable names", async () => {
    const cwd = await mkdtemp(join(tmpdir(), "zero-target-"));
    const out = join(cwd, "hello-windows");

    try {
      const result = await runZero(["build", "--emit", "exe", "--target", "win32-x64.exe", "examples/hello.graph", "--out", out], {
        env: { ...process.env, ZERO_CC: "/usr/bin/false" },
      });
      assert.match(result.stdout, /hello-windows\.exe/);
      const bytes = await readFile(`${out}.exe`);
      assert.equal(bytes[0], 0x4d);
      assert.equal(bytes[1], 0x5a);
      await assert.rejects(access(`${out}.exe.c`));
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("rejects unsupported target triples", async () => {
    const result = await runZero(["check", "--target", "mips-unknown-none", "examples/hello.graph"]).catch((error) => error);
    assert.notEqual(result.code, 0);
    assert.match(result.stderr, /unknown target 'mips-unknown-none'/);
  });

  it("builds hosted Linux hello through the direct executable backend", async () => {
    const cwd = await mkdtemp(join(tmpdir(), "zero-cross-"));
    const out = join(cwd, "hello-linux-musl");

    try {
      const build = await runZero(["build", "--emit", "exe", "--target", "linux-musl-x64", "examples/hello.graph", "--out", out]);
      assert.match(build.stdout, /hello-linux-musl/);
      assert.ok((await readFile(out)).includes(Buffer.from("hello from zero\n")));
    } finally {
      await rm(cwd, { force: true, recursive: true });
    }
  });

  it("emits native diagnostic codes", async () => {
    const out = join(tmpdir(), `zero-backend-blocked-${Date.now()}`);
    const result = await runZero(["build", "--json", "--target", "linux-musl-x64", "conformance/common/fail/unsupported-target-feature.graph", "--out", out]).catch((error) => error);
    assert.notEqual(result.code, 0);
    const diagnostic = JSON.parse(result.stdout).diagnostics[0];
    assert.equal(diagnostic.code, "BLD004");
    assert.match(diagnostic.help, /use zero check/);
  });

  it("covers fix-plan repair safety metadata and agent-facing TYP009 contract", async () => {
    const plan = JSON.parse((await runZero(["fix", "--plan", "--json", "examples/agent-repair-demo/broken.0"])).stdout);
    assert.equal(plan.ok, false);
    assert.equal(plan.mode, "plan");
    assert.equal(plan.appliesEdits, false);
    assert.equal(plan.schemaVersion, 1);

    // Verify diagnostics array has the expected error
    assert.equal(plan.diagnostics.length, 1);
    assert.equal(plan.diagnostics[0].code, "TYP009");
    assert.equal(plan.diagnostics[0].fixSafety, "behavior-preserving");
    assert.equal(plan.diagnostics[0].repair.id, "make-binding-mutable");

    // Verify fixes array preserves repair metadata
    assert.equal(plan.fixes.length, 1);
    assert.equal(plan.fixes[0].id, "make-binding-mutable");
    assert.equal(plan.fixes[0].diagnosticCode, "TYP009");
    assert.equal(plan.fixes[0].safety, "behavior-preserving");
    assert.equal(plan.fixes[0].appliesEdits, false);

    // Verify the plan remains non-applying
    assert.equal(plan.appliesEdits, false);
  });

  it("covers target capability facts in zero targets", async () => {
    const targets = JSON.parse((await runZero(["targets"])).stdout);
    assert.equal(targets.schemaVersion, 1);
    assert.ok(targets.host.length > 0);

    // Find the host target
    const hostTarget = targets.targets.find((t: { hosted: boolean }) => t.hosted);
    assert.ok(hostTarget, "host target should exist");
    assert.equal(hostTarget.hosted, true);

    // Host target should have process/runtime capabilities
    assert.ok(hostTarget.capabilities.includes("proc"), "host target should have proc capability");
    assert.ok(hostTarget.capabilities.includes("fs"), "host target should have fs capability");
    assert.ok(hostTarget.capabilities.includes("net"), "host target should have net capability");

    // Host target should have hosted process/runtime surface facts
    const hostCapabilityNames = hostTarget.capabilityFacts.map((f: { name: string }) => f.name);
    assert.ok(hostCapabilityNames.includes("proc"), "host target capabilityFacts should include proc");
    assert.ok(hostCapabilityNames.includes("fs"), "host target capabilityFacts should include fs");

    // Unavailable capabilities should appear with available: false
    const webFact = hostTarget.capabilityFacts.find((f: { name: string }) => f.name === "web");
    if (webFact) {
      assert.equal(webFact.available, false, "web capability should be unavailable on host target");
    }

    // Find a non-host target (e.g., wasm32-wasi)
    const wasmTarget = targets.targets.find((t: { name: string }) => t.name === "wasm32-wasi");
    if (wasmTarget) {
      assert.equal(wasmTarget.hosted, false, "wasm32-wasi should not be hosted");
      // Non-host targets should not have proc capability
      assert.ok(!wasmTarget.capabilities.includes("proc"), "wasm should not have proc");
    }
  });
});
