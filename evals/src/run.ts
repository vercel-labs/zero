import { Sandbox, type NetworkPolicy } from "@vercel/sandbox";
import { execFile, spawn, spawnSync } from "node:child_process";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { cp, mkdir, rm, writeFile } from "node:fs/promises";
import { dirname, join, relative, resolve } from "node:path";
import { performance } from "node:perf_hooks";
import { fileURLToPath } from "node:url";
import {
  evalCases,
  evalSuiteIds,
  findEvalCase,
  findEvalSuite,
  type EvalCase,
  type EvalRunCheck,
  type EvalServerCheck,
} from "./cases.js";
import {
  extractZeroSource,
  finalSourceResponseFailures,
  sourcePatternFailures,
} from "./source.js";

interface RunOptions {
  caseId: string | null;
  suiteId: string | null;
  dryRun: boolean;
  fixture: boolean;
  json: boolean;
  keepAlive: boolean;
  maxTurns: number;
  models: string[];
  outDir: string;
  sandboxRuntime: string;
  sandboxTimeoutMs: number;
  sandboxTimeoutMsExplicit: boolean;
  sandboxVcpus: number;
  commandTimeoutMs: number;
}

interface CommandResult {
  code: number;
  stdout: string;
  stderr: string;
}

interface EvalRunResult {
  name: string;
  args: string[];
  cwd: string | null;
  expectedStdout: string;
  expectedStderr: string;
  actualStdout: string;
  actualStderr: string;
  command: CommandResult | null;
}

interface ValidationResult {
  check: CommandResult;
  build: CommandResult | null;
  runs: EvalRunResult[];
  remoteSourcePath: string | null;
  sourceText: string;
}

interface AgentToolCall {
  id: string;
  name: string;
  input: unknown;
}

interface AgentToolResult {
  toolUseId: string;
  name: string;
  output: unknown;
}

interface AgentStep {
  turn: number;
  id: string;
  type: string;
  text: string;
  toolCalls: AgentToolCall[];
  toolResults: AgentToolResult[];
  usage: unknown;
}

interface AgentMetrics {
  turnCount: number;
  toolCallCount: number;
  zeroCliCallCount: number;
  zeroSkillLoadCount: number;
  zeroCheckCallCount: number;
  zeroRunCallCount: number;
}

interface AgentRun {
  responseText: string;
  steps: AgentStep[];
  metrics: AgentMetrics | null;
  error: string | null;
  rawOutputPath?: string;
  stderrPath?: string;
}

interface SandboxContext {
  sandbox: Sandbox;
  sandboxId: string;
  projectDir: string;
}

interface SandboxCredentials {
  token?: string;
  teamId?: string;
  projectId?: string;
}

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const zero = join(repoRoot, "bin", "zero");
const AI_GATEWAY_HOST = "ai-gateway.vercel.sh";
const AI_GATEWAY_URL = `https://${AI_GATEWAY_HOST}`;
const DEFAULT_PROJECT_DIR = "/vercel/sandbox/zero-lang";
const PROMPT_PATH = "/tmp/zero-eval-prompt.txt";
const REMOTE_EVAL_ROOT = "/tmp/zero-evals";
const REMOTE_RESULTS_ROOT = `${REMOTE_EVAL_ROOT}/results`;
const REMOTE_WORKSPACE_ROOT = `${REMOTE_EVAL_ROOT}/workspaces`;
const DEFAULT_SANDBOX_TIMEOUT_MS = 30 * 60 * 1000;
const DEFAULT_COMMAND_TIMEOUT_MS = 20 * 60 * 1000;
const DEFAULT_LOCAL_COMMAND_TIMEOUT_MS = 120_000;
const DEFAULT_SANDBOX_SETUP_TIMEOUT_MS = 5 * 60 * 1000;
const DEFAULT_MAX_TURNS = 30;
const DEFAULT_MODELS = [
  "anthropic/claude-opus-4.7",
  "anthropic/claude-sonnet-4.6",
];
const LOCAL_ARCHIVE_EXCLUDE_DIRS = new Set([
  ".claude",
  ".cursor",
  ".vercel",
  ".zero",
]);

const systemPrompt = [
  "You are an agent evaluating a Zero programming task.",
  "Work inside the repository checkout prepared by the evaluator.",
  "Use the local ./bin/zero compiler as your source of Zero-specific guidance and verification.",
  "First run ./bin/zero skills get zero --full.",
  "Load any additional skills recommended by that skill before writing code.",
  "Use the prepared repository root as the candidate package root unless the task explicitly asks for a subdirectory.",
  "Do not inspect examples unless the skills and compiler diagnostics are insufficient.",
  "Use the graph-first workflow from the loaded skills. If you write a source projection for the final answer, import it to a graph before check/run.",
  "Use ./bin/zero check --json to inspect diagnostics, then ./bin/zero run to verify stdout and stderr.",
  "Follow the final response shape requested by the task prompt.",
].join("\n");

async function main() {
  loadDotEnvFiles([".env", ".env.local"]);
  const options = parseArgs(process.argv.slice(2));
  const selectedCases = selectCases(options.caseId, options.suiteId);
  applyDefaultSandboxTimeout(options, selectedCases.length);

  if (options.dryRun) {
    printJsonOrText(options.json, {
      models: options.models,
      gatewayURL: AI_GATEWAY_URL,
      maxTurns: options.maxTurns,
      commandTimeoutMs: options.commandTimeoutMs,
      sandboxTimeoutMs: options.sandboxTimeoutMs,
      mode: options.fixture ? "fixture" : "sandbox",
      cases: selectedCases.map(
        ({
          id,
          title,
          kind,
          suites,
          prompt,
          expectedStdout,
          expectedStderr,
          maxValidationDurationMs,
          runChecks,
          serverChecks,
          requiredSourcePatterns,
        }) => ({
          id,
          title,
          kind: kind ?? "source",
          suites: suites ?? [],
          prompt,
          expectedStdout,
          expectedStderr: expectedStderr ?? "",
          maxValidationDurationMs: maxValidationDurationMs ?? null,
          runCheckCount:
            serverChecks && serverChecks.length > 0
              ? 0
              : normalizedRunChecks({
                  expectedStdout,
                  expectedStderr,
                  runChecks,
                }).length,
          serverCheckCount: serverChecks?.length ?? 0,
          requiredSourcePatternCount: requiredSourcePatterns.length,
        }),
      ),
    });
    return;
  }

  await mkdir(options.outDir, { recursive: true });

  let sandboxContext: SandboxContext | null = null;
  try {
    if (!options.fixture) {
      sandboxContext = await createSandboxContext(options);
    }

    const results = [];
    for (const model of options.models) {
      for (const evalCase of selectedCases) {
        results.push(await runCase(evalCase, model, options, sandboxContext));
      }
    }

    const summary = {
      ok: results.every((result) => result.passed),
      models: options.models,
      outDir: options.outDir,
      sandboxId: sandboxContext?.sandboxId ?? null,
      sandboxKeptAlive: Boolean(sandboxContext && options.keepAlive),
      passed: results.filter((result) => result.passed).length,
      failed: results.filter((result) => !result.passed).length,
      results,
    };

    await writeFile(
      join(options.outDir, "summary.json"),
      `${JSON.stringify(summary, null, 2)}\n`,
    );
    printJsonOrText(options.json, summary);
    if (!summary.ok) process.exitCode = 1;
  } finally {
    if (sandboxContext && !options.keepAlive) {
      await sandboxContext.sandbox.stop({ blocking: true }).catch(() => {});
    } else if (sandboxContext) {
      process.stderr.write(
        `Leaving Vercel Sandbox running: ${sandboxContext.sandboxId}\n`,
      );
    }
  }
}

async function createSandboxContext(
  options: RunOptions,
): Promise<SandboxContext> {
  ensureSandboxAuthEnv();
  const gatewayCredential = resolveGatewayCredential();
  const archivePath = createSourceArchive(options.outDir);
  const sandboxCredentials = resolveSandboxCredentials();

  process.stderr.write("Creating Vercel Sandbox for Zero evals...\n");
  const sandbox = await Sandbox.create({
    ...sandboxCredentials,
    runtime: options.sandboxRuntime,
    timeout: options.sandboxTimeoutMs,
    resources: { vcpus: options.sandboxVcpus },
    networkPolicy: buildAIGatewayNetworkPolicy(gatewayCredential),
    env: buildClaudeGatewayEnv(),
  });
  const sandboxId = sandbox.sandboxId;
  const projectDir =
    process.env.ZERO_EVAL_SANDBOX_PROJECT_DIR?.trim() || DEFAULT_PROJECT_DIR;

  try {
    process.stderr.write(`Sandbox ready: ${sandboxId}\n`);
    await sandbox.writeFiles([
      {
        path: "/tmp/zero-lang-source.tar.gz",
        content: readFileSync(archivePath),
      },
    ]);
    await runSandboxCommandChecked(
      sandbox,
      {
        cmd: "bash",
        args: ["-lc", buildSandboxSetupScript(projectDir)],
      },
      "sandbox setup",
    );
    return { sandbox, sandboxId, projectDir };
  } catch (error) {
    await sandbox.stop({ blocking: true }).catch(() => {});
    throw error;
  }
}

async function runCase(
  evalCase: EvalCase,
  model: string,
  options: RunOptions,
  sandboxContext: SandboxContext | null,
) {
  const started = performance.now();
  const caseDir = join(options.outDir, modelPathSegment(model), evalCase.id);
  await rm(caseDir, { force: true, recursive: true });
  await mkdir(caseDir, { recursive: true });
  const sandboxProjectDir = sandboxContext
    ? await createSandboxRunWorkspace(sandboxContext, evalCase, model)
    : null;
  const sandboxCandidatePackageDir =
    sandboxContext && sandboxProjectDir && isPackageEvalCase(evalCase)
      ? await createSandboxCandidatePackage(
          sandboxContext,
          sandboxProjectDir,
          evalCase,
        )
      : null;

  const agentRun: AgentRun =
    options.fixture || !sandboxContext || !sandboxProjectDir
      ? {
          responseText: evalCase.fixtureSource ?? "READY\n",
          steps: [],
          metrics: null,
          error: null,
        }
      : await runSandboxAgentCase(
          evalCase,
          model,
          options,
          sandboxContext,
          sandboxProjectDir,
          caseDir,
        );

  const responsePath = join(caseDir, "response.md");
  const stepsPath = options.fixture ? null : join(caseDir, "steps.json");
  const sourcePath = join(caseDir, "candidate.0");
  const source = isPackageEvalCase(evalCase)
    ? ""
    : extractZeroSource(agentRun.responseText);

  await writeFile(responsePath, agentRun.responseText);
  if (stepsPath) {
    await writeFile(stepsPath, `${JSON.stringify(agentRun.steps, null, 2)}\n`);
  }
  if (!isPackageEvalCase(evalCase)) {
    await writeFile(sourcePath, source);
  }

  const validationStarted = performance.now();
  const validation = await validateEvalCase({
    caseDir,
    evalCase,
    model,
    sandboxCandidatePackageDir,
    sandboxContext,
    sandboxProjectDir,
    source,
    sourcePath,
  });
  const validationDurationMs = Math.round(performance.now() - validationStarted);
  const { check, build, runs, remoteSourcePath, sourceText } = validation;
  const run = runs[0]?.command ?? null;

  let error: string | null =
    agentRun.error ?? (check.code === 0 ? null : "zero check failed");
  const patternFailures = sourcePatternFailures(
    sourceText,
    evalCase.requiredSourcePatterns,
  );
  const responseFormatFailures = isPackageEvalCase(evalCase)
    ? finalPackageResponseFailures(agentRun.responseText)
    : finalSourceResponseFailures(agentRun.responseText, source);
  const agentRequirementFailures = options.fixture
    ? []
    : getAgentRequirementFailures(agentRun.metrics, options.maxTurns);
  const actualStdout = run?.stdout ?? "";
  const actualStderr = run?.stderr ?? "";
  const expectedStderr = expectedStderrForEvalCase(evalCase);
  const runFailures = runResultFailures(runs);
  const budgetFailures = validationBudgetFailures(
    evalCase,
    validationDurationMs,
  );
  const negativeFixtureResults =
    options.fixture && !isPackageEvalCase(evalCase)
      ? await evaluateNegativeFixtures(evalCase, caseDir)
      : [];
  const negativeFixtureFailures = negativeFixtureResults
    .filter((negativeResult) => !negativeResult.rejected)
    .map(
      (negativeResult) =>
        `${negativeResult.label}: not rejected by any gate (case does not discriminate)`,
    );
  const passed =
    agentRun.error === null &&
    check.code === 0 &&
    (build?.code ?? 0) === 0 &&
    runFailures.length === 0 &&
    budgetFailures.length === 0 &&
    patternFailures.length === 0 &&
    responseFormatFailures.length === 0 &&
    agentRequirementFailures.length === 0 &&
    negativeFixtureFailures.length === 0;

  if (!passed && !error) {
    error =
      negativeFixtureFailures.length > 0
        ? negativeFixtureFailures.join("; ")
        : failureReason({
      run,
      build,
      runFailures,
      budgetFailures,
      actualStdout,
      actualStderr,
      expectedStdout: evalCase.expectedStdout,
      expectedStderr,
      patternFailures,
      responseFormatFailures,
      agentRequirementFailures,
    });
  }

  const result = {
    id: evalCase.id,
    title: evalCase.title,
    passed,
    model,
    mode: options.fixture ? "fixture" : "sandbox",
    kind: evalCase.kind ?? "source",
    durationMs: Math.round(performance.now() - started),
    validationDurationMs,
    sourcePath: isPackageEvalCase(evalCase) ? null : sourcePath,
    remoteSourcePath,
    remoteProjectDir: sandboxProjectDir,
    remoteCandidatePackageDir: sandboxCandidatePackageDir,
    responsePath,
    stepsPath,
    rawOutputPath: agentRun.rawOutputPath ?? null,
    stderrPath: agentRun.stderrPath ?? null,
    agentError: agentRun.error,
    agent: agentRun.metrics,
    check,
    build,
    run,
    runChecks: runs,
    runFailures,
    budgetFailures,
    expectedStdout: evalCase.expectedStdout,
    expectedStderr,
    actualStdout,
    actualStderr,
    sourcePatternFailures: patternFailures,
    responseFormatFailures,
    agentRequirementFailures,
    negativeFixtures: negativeFixtureResults,
    error,
  };

  await writeFile(
    join(caseDir, "result.json"),
    `${JSON.stringify(result, null, 2)}\n`,
  );
  return result;
}

async function runSandboxAgentCase(
  evalCase: EvalCase,
  model: string,
  options: RunOptions,
  context: SandboxContext,
  projectDir: string,
  caseDir: string,
): Promise<AgentRun> {
  const prompt = buildClaudePrompt(evalCase, options, projectDir);
  await context.sandbox.writeFiles([
    {
      path: PROMPT_PATH,
      content: prompt,
      mode: 0o600,
    },
  ]);

  const claudeArgs = [
    "--model",
    shellQuote(claudeModelName(model)),
    "--output-format",
    "stream-json",
    "--verbose",
    "--dangerously-skip-permissions",
  ];
  const commandTimeoutSeconds = Math.max(
    1,
    Math.ceil(options.commandTimeoutMs / 1000),
  );
  const script = [
    "set -euo pipefail",
    `cd ${shellQuote(projectDir)}`,
    `export ANTHROPIC_BASE_URL=${shellQuote(AI_GATEWAY_URL)}`,
    "export ANTHROPIC_AUTH_TOKEN=placeholder",
    "export ANTHROPIC_API_KEY=",
    `export BASH_DEFAULT_TIMEOUT_MS=${shellQuote(String(Math.min(options.commandTimeoutMs, 600_000)))}`,
    `export BASH_MAX_TIMEOUT_MS=${shellQuote(String(options.commandTimeoutMs))}`,
    `ZERO_EVAL_PROMPT="$(cat ${shellQuote(PROMPT_PATH)})"`,
    `timeout --foreground ${commandTimeoutSeconds}s claude ${claudeArgs.join(" ")} -p "$ZERO_EVAL_PROMPT"`,
  ].join("\n");

  const output = await runSandboxCommand(
    context.sandbox,
    {
      cmd: "bash",
      args: ["-lc", script],
      cwd: projectDir,
    },
    "claude eval",
  );
  const rawOutputPath = join(caseDir, "claude-stream.jsonl");
  const stderrPath = join(caseDir, "claude-stderr.txt");
  await writeFile(rawOutputPath, output.stdout);
  await writeFile(stderrPath, output.stderr);

  const parsed = parseClaudeStream(output.stdout);
  const commandError =
    output.code === 0
      ? null
      : `Claude Code failed with exit code ${output.code}\n${truncate(output.stderr || output.stdout, 4_000)}`;
  const resultError =
    commandError ??
    (parsed.responseText.trim()
      ? null
      : "Claude Code stream did not include a final result");
  return { ...parsed, error: resultError, rawOutputPath, stderrPath };
}

async function createSandboxRunWorkspace(
  context: SandboxContext,
  evalCase: EvalCase,
  model: string,
) {
  const workspaceDir = `${REMOTE_WORKSPACE_ROOT}/${modelPathSegment(model)}/${modelPathSegment(evalCase.id)}`;
  const script = [
    "set -euo pipefail",
    `rm -rf ${shellQuote(workspaceDir)}`,
    `mkdir -p ${shellQuote(workspaceDir)}`,
    `cp -a ${shellQuote(`${context.projectDir}/.`)} ${shellQuote(workspaceDir)}`,
  ].join("\n");
  await runSandboxCommandChecked(
    context.sandbox,
    {
      cmd: "bash",
      args: ["-lc", script],
    },
    "prepare eval workspace",
  );
  return workspaceDir;
}

async function createSandboxCandidatePackage(
  context: SandboxContext,
  workspaceDir: string,
  evalCase: EvalCase,
) {
  const candidateDir = `${workspaceDir}/.zero/eval-candidates/${modelPathSegment(
    evalCase.id,
  )}`;
  await runSandboxCommandChecked(
    context.sandbox,
    { cmd: "mkdir", args: ["-p", candidateDir] },
    "prepare candidate package",
  );
  return candidateDir;
}

function buildClaudePrompt(
  evalCase: EvalCase,
  options: RunOptions,
  projectDir: string,
) {
  const candidatePackageDir = isPackageEvalCase(evalCase)
    ? `${projectDir}/.zero/eval-candidates/${modelPathSegment(evalCase.id)}`
    : null;
  return [
    systemPrompt,
    "",
    "Task:",
    evalCase.prompt,
    "",
    `Repository root: ${projectDir}`,
    candidatePackageDir ? `Candidate package root: ${candidatePackageDir}` : "",
    `You have at most ${options.maxTurns} agent turns before the evaluator marks the run failed.`,
    "Use shell commands from the repository root. Prefer ./bin/zero, not any global zero binary.",
    ...finalAnswerInstructions(evalCase),
  ].filter(Boolean).join("\n");
}

function finalAnswerInstructions(evalCase: EvalCase) {
  if (isPackageEvalCase(evalCase)) {
    return [
      "Create and validate the Zero package in the candidate package root.",
      "After ./bin/zero check and the expected ./bin/zero run commands pass, answer exactly READY.",
      "Do not paste source, include a success sentence, explanation, or Markdown fence.",
    ];
  }
  return [
    "After graph import, ./bin/zero check, and ./bin/zero run confirm the expected output, return code only.",
    "Do not include a success sentence, explanation, or Markdown fence.",
  ];
}

async function validateEvalCase(input: {
  caseDir: string;
  evalCase: EvalCase;
  model: string;
  sandboxCandidatePackageDir: string | null;
  sandboxContext: SandboxContext | null;
  sandboxProjectDir: string | null;
  source: string;
  sourcePath: string;
}): Promise<ValidationResult> {
  if (isPackageEvalCase(input.evalCase)) {
    if (input.sandboxContext && input.sandboxCandidatePackageDir) {
      return validatePackageInSandbox(
        input.sandboxContext,
        input.evalCase,
        input.model,
        input.sandboxProjectDir ?? input.sandboxContext.projectDir,
        input.sandboxCandidatePackageDir,
      );
    }
    return validateFixturePackageLocally(input.evalCase, input.caseDir);
  }

  if (input.sandboxContext) {
    return validateSourceInSandbox(
      input.sandboxContext,
      input.evalCase,
      input.model,
      input.source,
      input.sandboxProjectDir ?? input.sandboxContext.projectDir,
    );
  }
  return validateSourceLocally(input.evalCase, input.sourcePath, input.source);
}

async function validateSourceLocally(
  evalCase: EvalCase,
  sourcePath: string,
  source: string,
): Promise<ValidationResult> {
  const graphPath = join(dirname(sourcePath), "candidate.graph");
  const imported = await runLocalCommand(zero, [
    "import",
    "--format",
    "binary",
    "--out",
    graphPath,
    sourcePath,
  ]);
  if (imported.code !== 0) {
    return {
      check: imported,
      build: null,
      runs: [],
      remoteSourcePath: null,
      sourceText: source,
    };
  }

  const check = await runLocalCommand(zero, ["check", "--json", graphPath]);
  let build: CommandResult | null = null;
  let runs: EvalRunResult[] = [];
  if (check.code === 0) {
    if (hasServerChecks(evalCase)) {
      runs = await runServerCandidateLocally(
        evalCase,
        graphPath,
        dirname(sourcePath),
      );
    } else {
      const built = await buildCandidateLocally(
        evalCase,
        graphPath,
        dirname(sourcePath),
      );
      build = built.build;
      runs = built.runs.length > 0 ? built.runs : emptyRunResults(evalCase);
    }
  }
  return { check, build, runs, remoteSourcePath: null, sourceText: source };
}

interface NegativeFixtureResult {
  label: string;
  expectGate: string;
  rejected: boolean;
  gatesFailed: string[];
  caughtByExpectedGate: boolean;
}

// In --fixture mode, run each negative fixture through the same local
// validation path as the golden and assert it is REJECTED by at least one
// gate. A negative that passes every gate means the case's gates are too weak
// to discriminate a wrong answer (a silent non-discriminating eval).
async function evaluateNegativeFixtures(
  evalCase: EvalCase,
  caseDir: string,
): Promise<NegativeFixtureResult[]> {
  const negatives = evalCase.negativeFixtures ?? [];
  const results: NegativeFixtureResult[] = [];
  for (const [index, negative] of negatives.entries()) {
    const dir = join(caseDir, `negative-${index}`);
    await mkdir(dir, { recursive: true });
    const sourcePath = join(dir, "candidate.0");
    const source = extractZeroSource(negative.source);
    await writeFile(sourcePath, source);
    const validation = await validateSourceLocally(evalCase, sourcePath, source);
    const gatesFailed: string[] = [];
    if (validation.check.code !== 0) gatesFailed.push("check");
    if ((validation.build?.code ?? 0) !== 0) gatesFailed.push("build");
    if (
      sourcePatternFailures(source, evalCase.requiredSourcePatterns).length > 0
    ) {
      gatesFailed.push("pattern");
    }
    if (runResultFailures(validation.runs).length > 0) gatesFailed.push("run");
    // "stdout" mismatches surface through the run-result comparison.
    const expectedGate = negative.expectGate === "stdout" ? "run" : negative.expectGate;
    results.push({
      label: negative.label,
      expectGate: negative.expectGate,
      rejected: gatesFailed.length > 0,
      gatesFailed,
      caughtByExpectedGate: gatesFailed.includes(expectedGate),
    });
  }
  return results;
}

async function validateSourceInSandbox(
  context: SandboxContext,
  evalCase: EvalCase,
  model: string,
  source: string,
  projectDir: string,
): Promise<ValidationResult> {
  const remoteCaseDir = `${REMOTE_RESULTS_ROOT}/${modelPathSegment(model)}/${
    evalCase.id
  }`;
  const remoteSourcePath = `${remoteCaseDir}/candidate.0`;
  const remoteGraphPath = `${remoteCaseDir}/candidate.graph`;
  await runSandboxCommandChecked(
    context.sandbox,
    { cmd: "mkdir", args: ["-p", remoteCaseDir] },
    "create eval case directory",
  );
  await context.sandbox.writeFiles([
    {
      path: remoteSourcePath,
      content: source,
    },
  ]);

  const imported = await runSandboxCommand(
    context.sandbox,
    {
      cmd: "bash",
      args: [
        "-lc",
        `./bin/zero import --format binary --out ${shellQuote(remoteGraphPath)} ${shellQuote(remoteSourcePath)}`,
      ],
      cwd: projectDir,
    },
    "zero import",
  );
  if (imported.code !== 0) {
    return {
      check: imported,
      build: null,
      runs: [],
      remoteSourcePath,
      sourceText: source,
    };
  }

  const check = await runSandboxCommand(
    context.sandbox,
    {
      cmd: "bash",
      args: ["-lc", `./bin/zero check --json ${shellQuote(remoteGraphPath)}`],
      cwd: projectDir,
    },
    "zero check",
  );
  let build: CommandResult | null = null;
  let runs: EvalRunResult[] = [];
  if (check.code === 0) {
    if (hasServerChecks(evalCase)) {
      runs = await runServerCandidateInSandbox(
        context,
        evalCase,
        projectDir,
        remoteGraphPath,
        remoteCaseDir,
      );
    } else {
      const built = await buildCandidateInSandbox(
        context,
        evalCase,
        projectDir,
        remoteGraphPath,
        remoteCaseDir,
      );
      build = built.build;
      runs = built.runs.length > 0 ? built.runs : emptyRunResults(evalCase);
    }
  }

  return { check, build, runs, remoteSourcePath, sourceText: source };
}

async function validateFixturePackageLocally(
  evalCase: EvalCase,
  caseDir: string,
): Promise<ValidationResult> {
  if (!evalCase.fixtureProjectDir) {
    const check = {
      code: 1,
      stdout: "",
      stderr: "package eval case is missing fixtureProjectDir",
    };
    return { check, build: null, runs: [], remoteSourcePath: null, sourceText: "" };
  }
  const packageDir = join(caseDir, "fixture-package");
  await cp(resolve(repoRoot, evalCase.fixtureProjectDir), packageDir, {
    recursive: true,
  });
  const check = await runLocalCommand(zero, ["check", "--json", packageDir]);
  const sourceText = await packageSourceTextLocally(packageDir);
  let build: CommandResult | null = null;
  let runs: EvalRunResult[] = [];
  if (check.code === 0) {
    if (hasServerChecks(evalCase)) {
      runs = await runServerCandidateLocally(evalCase, packageDir, caseDir);
    } else {
      const built = await buildCandidateLocally(evalCase, packageDir, caseDir);
      build = built.build;
      runs = built.runs.length > 0 ? built.runs : emptyRunResults(evalCase);
    }
  }
  return { check, build, runs, remoteSourcePath: null, sourceText };
}

async function validatePackageInSandbox(
  context: SandboxContext,
  evalCase: EvalCase,
  model: string,
  workspaceDir: string,
  packageDir: string,
): Promise<ValidationResult> {
  const remoteCaseDir = `${REMOTE_RESULTS_ROOT}/${modelPathSegment(model)}/${
    evalCase.id
  }`;
  await runSandboxCommandChecked(
    context.sandbox,
    { cmd: "mkdir", args: ["-p", remoteCaseDir] },
    "create eval case directory",
  );
  const check = await runSandboxCommand(
    context.sandbox,
    {
      cmd: "./bin/zero",
      args: ["check", "--json", packageDir],
      cwd: workspaceDir,
    },
    "zero check",
  );
  const sourceText = await packageSourceTextInSandbox(
    context,
    workspaceDir,
    packageDir,
  );
  let build: CommandResult | null = null;
  let runs: EvalRunResult[] = [];
  if (check.code === 0) {
    if (hasServerChecks(evalCase)) {
      runs = await runServerCandidateInSandbox(
        context,
        evalCase,
        workspaceDir,
        packageDir,
        remoteCaseDir,
      );
    } else {
      const built = await buildCandidateInSandbox(
        context,
        evalCase,
        workspaceDir,
        packageDir,
        remoteCaseDir,
      );
      build = built.build;
      runs = built.runs.length > 0 ? built.runs : emptyRunResults(evalCase);
    }
  }
  return {
    check,
    build,
    runs: runs.length > 0 ? runs : emptyRunResults(evalCase),
    remoteSourcePath: null,
    sourceText,
  };
}

async function buildCandidateLocally(
  evalCase: EvalCase,
  inputPath: string,
  outDir: string,
): Promise<{ build: CommandResult; runs: EvalRunResult[] }> {
  const programPath = join(outDir, "program");
  const runDir = join(outDir, "run");
  await mkdir(runDir, { recursive: true });
  const build = await runLocalCommand(
    zero,
    ["build", "--out", programPath, inputPath],
    DEFAULT_LOCAL_COMMAND_TIMEOUT_MS,
  );
  if (build.code !== 0) return { build, runs: [] };

  const results: EvalRunResult[] = [];
  const checks = normalizedRunChecks(evalCase);
  for (const check of checks) {
    const args = check.args ?? [];
    const command = await runLocalCommand(
      programPath,
      args,
      DEFAULT_LOCAL_COMMAND_TIMEOUT_MS,
      runDir,
    );
    results.push(toEvalRunResult(check, command, runDir));
  }
  return { build, runs: results };
}

async function buildCandidateInSandbox(
  context: SandboxContext,
  evalCase: EvalCase,
  cwd: string,
  inputPath: string,
  remoteCaseDir: string,
): Promise<{ build: CommandResult; runs: EvalRunResult[] }> {
  const programPath = `${remoteCaseDir}/program`;
  const runDir = `${remoteCaseDir}/run`;
  const build = await runSandboxCommand(
    context.sandbox,
    {
      cmd: "./bin/zero",
      args: ["build", "--out", programPath, inputPath],
      cwd,
    },
    "zero build",
  );
  if (build.code !== 0) return { build, runs: [] };
  await runSandboxCommandChecked(
    context.sandbox,
    { cmd: "mkdir", args: ["-p", runDir] },
    "prepare candidate run directory",
  );

  const results: EvalRunResult[] = [];
  const checks = normalizedRunChecks(evalCase);
  for (const check of checks) {
    const args = check.args ?? [];
    const command = await runSandboxCommand(
      context.sandbox,
      {
        cmd: programPath,
        args,
        cwd: runDir,
      },
      "candidate run",
    );
    results.push(toEvalRunResult(check, command, runDir));
  }
  return { build, runs: results };
}

async function runServerCandidateLocally(
  evalCase: EvalCase,
  inputPath: string,
  outDir: string,
): Promise<EvalRunResult[]> {
  const runDir = join(outDir, "server-run");
  await mkdir(runDir, { recursive: true });

  const server = spawn(zero, ["run", inputPath], {
    cwd: runDir,
    stdio: ["ignore", "pipe", "pipe"],
  });
  let serverStdout = "";
  let serverStderr = "";
  server.stdout.setEncoding("utf8");
  server.stderr.setEncoding("utf8");
  server.stdout.on("data", (chunk) => {
    serverStdout += String(chunk);
  });
  server.stderr.on("data", (chunk) => {
    serverStderr += String(chunk);
  });

  try {
    const url = await waitForLocalServerUrl(
      server,
      () => serverStdout,
      () => serverStderr,
    );
    if (!url) {
      const command = {
        code: 1,
        stdout: serverStdout,
        stderr: [
          "server did not print a loopback listening URL",
          serverStderr.trim(),
        ].filter(Boolean).join("\n"),
      };
      return normalizedServerChecks(evalCase).map((check) =>
        toServerEvalRunResult(check, command, runDir),
      );
    }
    const results: EvalRunResult[] = [];
    for (const check of normalizedServerChecks(evalCase)) {
      const command = await runLocalCommand(
        "curl",
        ["-sS", "-i", `${url}${check.path}`],
        DEFAULT_LOCAL_COMMAND_TIMEOUT_MS,
        runDir,
      );
      results.push(toServerEvalRunResult(check, command, runDir));
    }
    return results;
  } finally {
    await stopLocalServer(server);
  }
}

async function runServerCandidateInSandbox(
  context: SandboxContext,
  evalCase: EvalCase,
  workspaceDir: string,
  inputPath: string,
  remoteCaseDir: string,
): Promise<EvalRunResult[]> {
  const runDir = `${remoteCaseDir}/server-run`;
  await runSandboxCommandChecked(
    context.sandbox,
    { cmd: "mkdir", args: ["-p", runDir] },
    "prepare server run directory",
  );

  const checks = normalizedServerChecks(evalCase);
  const command = await runSandboxCommand(
    context.sandbox,
    {
      cmd: "bash",
      args: [
        "-lc",
        buildSandboxServerChecksScript(
          `${workspaceDir}/bin/zero`,
          inputPath,
          runDir,
          checks,
        ),
      ],
      cwd: runDir,
    },
    "server curl checks",
  );
  return parseSandboxServerCheckResults(checks, command, runDir);
}

function buildSandboxServerChecksScript(
  zeroPath: string,
  inputPath: string,
  runDir: string,
  checks: EvalServerCheck[],
) {
  const logPath = `${runDir}/server.log`;
  const curlLines = checks.flatMap((check, index) => {
    const stdoutPath = `${runDir}/server-check-${index}.stdout`;
    const stderrPath = `${runDir}/server-check-${index}.stderr`;
    return [
      `check_code=0`,
      `curl -sS -i "$url"${shellQuote(check.path)} > ${shellQuote(stdoutPath)} 2> ${shellQuote(stderrPath)} || check_code=$?`,
      `printf '\\n__ZERO_EVAL_SERVER_CHECK_${index}_CODE__%s\\n' "$check_code"`,
      `printf '__ZERO_EVAL_SERVER_CHECK_${index}_STDOUT_BEGIN__\\n'`,
      `cat ${shellQuote(stdoutPath)} || true`,
      `printf '\\n__ZERO_EVAL_SERVER_CHECK_${index}_STDOUT_END__\\n'`,
      `printf '__ZERO_EVAL_SERVER_CHECK_${index}_STDERR_BEGIN__\\n'`,
      `cat ${shellQuote(stderrPath)} || true`,
      `printf '\\n__ZERO_EVAL_SERVER_CHECK_${index}_STDERR_END__\\n'`,
    ];
  });
  return [
    "set -euo pipefail",
    `${shellQuote(zeroPath)} run ${shellQuote(inputPath)} > ${shellQuote(logPath)} 2>&1 &`,
    "server_pid=$!",
    "cleanup() { kill \"$server_pid\" >/dev/null 2>&1 || true; wait \"$server_pid\" >/dev/null 2>&1 || true; }",
    "trap cleanup EXIT",
    "url=\"\"",
    "for _ in $(seq 1 300); do",
    `  url="$(grep -Eo 'http://127[.]0[.]0[.]1:[0-9]+' ${shellQuote(logPath)} | tail -n 1 || true)"`,
    "  if [ -n \"$url\" ]; then break; fi",
    "  if ! kill -0 \"$server_pid\" >/dev/null 2>&1; then",
    "    echo 'server exited before listening' >&2",
    `    cat ${shellQuote(logPath)} >&2 || true`,
    "    exit 1",
    "  fi",
    "  sleep 0.05",
    "done",
    "if [ -z \"$url\" ]; then",
    "  echo 'server did not print a loopback listening URL' >&2",
    `  cat ${shellQuote(logPath)} >&2 || true`,
    "  exit 1",
    "fi",
    ...curlLines,
  ].join("\n");
}

function parseSandboxServerCheckResults(
  checks: EvalServerCheck[],
  command: CommandResult,
  cwd: string,
) {
  if (command.code !== 0) {
    return checks.map((check) => toServerEvalRunResult(check, command, cwd));
  }
  return checks.map((check, index) => {
    const codeText = extractBetween(
      command.stdout,
      `__ZERO_EVAL_SERVER_CHECK_${index}_CODE__`,
      "\n",
    ).trim();
    const code = Number.parseInt(codeText, 10);
    const stdout = extractBetween(
      command.stdout,
      `__ZERO_EVAL_SERVER_CHECK_${index}_STDOUT_BEGIN__\n`,
      `\n__ZERO_EVAL_SERVER_CHECK_${index}_STDOUT_END__`,
    );
    const stderr = extractBetween(
      command.stdout,
      `__ZERO_EVAL_SERVER_CHECK_${index}_STDERR_BEGIN__\n`,
      `\n__ZERO_EVAL_SERVER_CHECK_${index}_STDERR_END__`,
    );
    return toServerEvalRunResult(
      check,
      {
        code: Number.isFinite(code) ? code : 1,
        stdout,
        stderr,
      },
      cwd,
    );
  });
}

function extractBetween(value: string, start: string, end: string) {
  const startIndex = value.indexOf(start);
  if (startIndex === -1) return "";
  const contentStart = startIndex + start.length;
  const endIndex = value.indexOf(end, contentStart);
  return endIndex === -1 ? value.slice(contentStart) : value.slice(contentStart, endIndex);
}

async function waitForLocalServerUrl(
  server: ReturnType<typeof spawn>,
  stdout: () => string,
  stderr: () => string,
) {
  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    const url = extractLoopbackUrl(`${stdout()}\n${stderr()}`);
    if (url) return url;
    if (server.exitCode !== null) {
      if (!stderr()) return null;
      return null;
    }
    await delay(50);
  }
  return extractLoopbackUrl(`${stdout()}\n${stderr()}`);
}

function extractLoopbackUrl(output: string) {
  return /http:\/\/127[.]0[.]0[.]1:[0-9]+/.exec(output)?.[0] ?? null;
}

async function stopLocalServer(server: ReturnType<typeof spawn>) {
  if (server.exitCode !== null) return;
  server.kill("SIGTERM");
  await Promise.race([
    new Promise<void>((resolveStop) => {
      server.once("exit", () => resolveStop());
    }),
    delay(1_000).then(() => {
      if (server.exitCode === null) server.kill("SIGKILL");
    }),
  ]);
}

function toEvalRunResult(
  check: EvalRunCheck,
  command: CommandResult | null,
  cwd: string | null = null,
): EvalRunResult {
  return {
    name: check.name,
    args: check.args ?? [],
    cwd,
    expectedStdout: check.expectedStdout,
    expectedStderr: check.expectedStderr ?? "",
    actualStdout: command?.stdout ?? "",
    actualStderr: command?.stderr ?? "",
    command,
  };
}

function toServerEvalRunResult(
  check: EvalServerCheck,
  command: CommandResult | null,
  cwd: string | null = null,
): EvalRunResult {
  return {
    name: check.name,
    args: [check.path],
    cwd,
    expectedStdout: check.expectedStdout,
    expectedStderr: check.expectedStderr ?? "",
    actualStdout: command?.stdout ?? "",
    actualStderr: command?.stderr ?? "",
    command,
  };
}

function normalizedRunChecks(evalCase: {
  expectedStdout: string;
  expectedStderr?: string;
  runArgs?: string[];
  runChecks?: EvalRunCheck[];
}): EvalRunCheck[] {
  if (evalCase.runChecks && evalCase.runChecks.length > 0) {
    return evalCase.runChecks.map((check) => ({
      ...check,
      args: check.args ?? [],
      expectedStderr: check.expectedStderr ?? "",
    }));
  }
  return [
    {
      name: "default",
      args: evalCase.runArgs ?? [],
      expectedStdout: evalCase.expectedStdout,
      expectedStderr: evalCase.expectedStderr ?? "",
    },
  ];
}

function normalizedServerChecks(evalCase: {
  serverChecks?: EvalServerCheck[];
}): EvalServerCheck[] {
  return (evalCase.serverChecks ?? []).map((check) => ({
    ...check,
    expectedStderr: check.expectedStderr ?? "",
  }));
}

function hasServerChecks(evalCase: { serverChecks?: EvalServerCheck[] }) {
  return Boolean(evalCase.serverChecks && evalCase.serverChecks.length > 0);
}

function expectedStderrForEvalCase(evalCase: EvalCase) {
  if (hasServerChecks(evalCase)) {
    return normalizedServerChecks(evalCase)[0]?.expectedStderr ?? "";
  }
  return normalizedRunChecks(evalCase)[0]?.expectedStderr ?? "";
}

function runResultFailures(runs: EvalRunResult[]): string[] {
  if (runs.length === 0) return ["zero run did not execute"];
  const failures: string[] = [];
  for (const run of runs) {
    if (!run.command) {
      failures.push(`${run.name}: zero run did not execute`);
      continue;
    }
    if (run.command.code !== 0) {
      failures.push(`${run.name}: zero run exited ${run.command.code}`);
      continue;
    }
    if (run.actualStdout !== run.expectedStdout) {
      failures.push(`${run.name}: stdout did not match expected output`);
    }
    if (run.actualStderr !== run.expectedStderr) {
      failures.push(`${run.name}: stderr did not match expected output`);
    }
  }
  return failures;
}

function emptyRunResults(evalCase: EvalCase): EvalRunResult[] {
  if (hasServerChecks(evalCase)) {
    return normalizedServerChecks(evalCase).map((check) =>
      toServerEvalRunResult(check, null),
    );
  }
  return normalizedRunChecks(evalCase).map((check) =>
    toEvalRunResult(check, null),
  );
}

async function packageSourceTextLocally(packageDir: string) {
  const manifestText = manifestSummary(packageDir);
  const graphText = existsSync(join(packageDir, "zero.graph")) ? "zero.graph\n" : "";
  const view = await runLocalCommand(zero, ["view", packageDir]);
  return `${manifestText}${graphText}${view.stdout}${view.stderr}`;
}

async function packageSourceTextInSandbox(
  context: SandboxContext,
  cwd: string,
  packageDir: string,
) {
  const manifest = await runSandboxCommand(
    context.sandbox,
    {
      cmd: "bash",
      args: [
        "-lc",
        [
          `test -f ${shellQuote(`${packageDir}/zero.toml`)} && echo zero.toml || true`,
          `test -f ${shellQuote(`${packageDir}/zero.json`)} && echo zero.json || true`,
          `test -f ${shellQuote(`${packageDir}/zero.graph`)} && echo zero.graph || true`,
        ].join("\n"),
      ],
      cwd,
    },
    "package manifest summary",
  );
  const view = await runSandboxCommand(
    context.sandbox,
    {
      cmd: "./bin/zero",
      args: ["view", packageDir],
      cwd,
    },
    "zero view",
  );
  return `${manifest.stdout}${manifest.stderr}${view.stdout}${view.stderr}`;
}

function manifestSummary(packageDir: string) {
  const parts = [];
  if (existsSync(join(packageDir, "zero.toml"))) parts.push("zero.toml");
  if (existsSync(join(packageDir, "zero.json"))) parts.push("zero.json");
  return parts.length > 0 ? `${parts.join("\n")}\n` : "";
}

function isPackageEvalCase(evalCase: EvalCase) {
  return evalCase.kind === "package";
}

function finalPackageResponseFailures(responseText: string) {
  return responseText.trim() === "READY"
    ? []
    : ["final response for package eval must be exactly READY"];
}

async function runSandboxCommand(
  sandbox: Sandbox,
  params: {
    cmd: string;
    args?: string[];
    cwd?: string;
    env?: Record<string, string>;
  },
  _label: string,
): Promise<CommandResult> {
  const result = await sandbox.runCommand(params);
  const [stdout, stderr] = await Promise.all([result.stdout(), result.stderr()]);
  return { code: result.exitCode, stdout, stderr };
}

async function runSandboxCommandChecked(
  sandbox: Sandbox,
  params: {
    cmd: string;
    args?: string[];
    cwd?: string;
    env?: Record<string, string>;
  },
  label: string,
) {
  process.stderr.write(`${label}...\n`);
  const result = await runSandboxCommand(sandbox, params, label);
  if (result.stdout) process.stderr.write(result.stdout);
  if (result.stderr) process.stderr.write(result.stderr);
  if (result.code !== 0) {
    throw new Error(
      `${label} failed with exit code ${result.code}\n${truncate(result.stderr || result.stdout, 4_000)}`,
    );
  }
  return result;
}

function parseClaudeStream(output: string): AgentRun {
  const steps: AgentStep[] = [];
  let responseText = "";
  let latestAssistantText = "";
  let toolResultCount = 0;

  for (const line of output.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) continue;

    let event: unknown;
    try {
      event = JSON.parse(trimmed);
    } catch {
      continue;
    }
    if (!isRecord(event)) continue;

    const eventType = typeof event.type === "string" ? event.type : "unknown";
    if (eventType === "assistant") {
      const message = isRecord(event.message) ? event.message : {};
      const content = Array.isArray(message.content) ? message.content : [];
      const toolCalls: AgentToolCall[] = [];
      const textParts: string[] = [];
      for (const block of content) {
        if (!isRecord(block)) continue;
        if (block.type === "text" && typeof block.text === "string") {
          textParts.push(block.text);
        } else if (block.type === "tool_use") {
          toolCalls.push({
            id: typeof block.id === "string" ? block.id : "",
            name: typeof block.name === "string" ? block.name : "tool",
            input: "input" in block ? block.input : null,
          });
        }
      }
      latestAssistantText = textParts.join("");
      steps.push({
        turn: steps.length + 1,
        id: typeof message.id === "string" ? message.id : "",
        type: eventType,
        text: truncate(latestAssistantText, 4_000),
        toolCalls,
        toolResults: [],
        usage: "usage" in message ? message.usage : null,
      });
      continue;
    }

    if (eventType === "user") {
      const message = isRecord(event.message) ? event.message : {};
      const content = Array.isArray(message.content) ? message.content : [];
      const toolResults: AgentToolResult[] = [];
      for (const block of content) {
        if (!isRecord(block) || block.type !== "tool_result") continue;
        toolResultCount += 1;
        toolResults.push({
          toolUseId:
            typeof block.tool_use_id === "string"
              ? block.tool_use_id
              : `tool-result-${toolResultCount}`,
          name: "tool_result",
          output: summarizeToolResult(block),
        });
      }
      if (toolResults.length > 0) {
        const latest = steps.at(-1);
        if (latest) {
          latest.toolResults.push(...toolResults);
        } else {
          steps.push({
            turn: steps.length + 1,
            id: "",
            type: eventType,
            text: "",
            toolCalls: [],
            toolResults,
            usage: null,
          });
        }
      }
      continue;
    }

    if (eventType === "result") {
      responseText =
        typeof event.result === "string" ? event.result : latestAssistantText;
    }
  }

  if (!responseText) responseText = latestAssistantText;
  return {
    responseText,
    steps,
    metrics: measureAgent(steps),
    error: null,
  };
}

function summarizeToolResult(block: Record<string, unknown>) {
  const value = block.content;
  if (typeof value !== "string") return value ?? null;
  return truncate(value, 2_000);
}

function createSourceArchive(outDir: string) {
  const archivePath = join(outDir, "source.tar.gz");
  const fileListPath = join(outDir, "source-files.txt");
  writeFileSync(fileListPath, buildSourceArchiveFileList(outDir));
  const metadataArgs = process.platform === "darwin" ? ["--no-xattrs"] : [];
  const result = spawnSync(
    "tar",
    [...metadataArgs, "--null", "-T", fileListPath, "-czf", archivePath],
    {
      cwd: repoRoot,
      encoding: "utf8",
      env: { ...process.env, COPYFILE_DISABLE: "1" },
    },
  );
  if (result.status !== 0) {
    throw new Error(
      `failed to create eval source archive\n${result.stderr.trim()}`,
    );
  }
  return archivePath;
}

function buildSourceArchiveFileList(outDir: string) {
  const result = spawnSync(
    "git",
    ["ls-files", "-z", "--cached", "--others", "--exclude-standard"],
    {
      cwd: repoRoot,
      encoding: "utf8",
    },
  );
  if (result.status !== 0) {
    throw new Error(
      `failed to list eval source files\n${result.stderr.trim()}`,
    );
  }

  const outDirPath = repoRelativePath(outDir);
  const entries = result.stdout
    .split("\0")
    .filter(Boolean)
    .filter((entry) => existsSync(join(repoRoot, entry)))
    .filter((entry) => !isLocalArchiveConfigPath(entry))
    .filter((entry) => !outDirPath || !isPathAtOrBelow(entry, outDirPath));

  if (entries.length === 0) {
    throw new Error(
      "failed to create eval source archive: no source files found",
    );
  }

  return `${entries.join("\0")}\0`;
}

function isLocalArchiveConfigPath(path: string) {
  const [firstSegment] = path.split("/", 1);
  if (LOCAL_ARCHIVE_EXCLUDE_DIRS.has(firstSegment)) return true;
  if (path === ".env") return true;
  if (path.startsWith(".env.") && path !== ".env.example") return true;
  return false;
}

function repoRelativePath(path: string) {
  const relativePath = relative(repoRoot, resolve(path)).replaceAll("\\", "/");
  if (
    !relativePath ||
    relativePath === ".." ||
    relativePath.startsWith("../") ||
    relativePath.startsWith("/")
  ) {
    return null;
  }
  return relativePath;
}

function isPathAtOrBelow(path: string, parent: string) {
  return path === parent || path.startsWith(`${parent}/`);
}

function buildSandboxSetupScript(projectDir: string) {
  return [
    "set -euo pipefail",
    "as_root() { if command -v sudo >/dev/null 2>&1; then sudo \"$@\"; else \"$@\"; fi; }",
    "install_build_tools() {",
    "  if command -v make >/dev/null 2>&1 && { command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1; }; then return; fi",
    "  if command -v apt-get >/dev/null 2>&1; then",
    "    as_root apt-get update",
    "    as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential ca-certificates git gzip make npm tar",
    "  elif command -v apk >/dev/null 2>&1; then",
    "    as_root apk add --no-cache build-base ca-certificates git gzip make npm tar",
    "  elif command -v dnf >/dev/null 2>&1; then",
    "    as_root dnf install -y ca-certificates gcc git glibc-devel gzip make npm tar",
    "  elif command -v yum >/dev/null 2>&1; then",
    "    as_root yum install -y ca-certificates gcc git glibc-devel gzip make npm tar",
    "  else",
    "    echo 'no supported package manager found to install make and a C compiler' >&2",
    "    exit 127",
    "  fi",
    "}",
    "install_build_tools",
    `rm -rf ${shellQuote(projectDir)}`,
    `mkdir -p ${shellQuote(projectDir)}`,
    `tar -xzf /tmp/zero-lang-source.tar.gz -C ${shellQuote(projectDir)}`,
    `cd ${shellQuote(projectDir)}`,
    "make -C native/zero-c",
    "command -v claude >/dev/null 2>&1 || npm install -g @anthropic-ai/claude-code",
    "claude --version",
    "./bin/zero --version",
  ].join("\n");
}

function buildAIGatewayNetworkPolicy(gatewayCredential: string): NetworkPolicy {
  return {
    allow: {
      "*": [],
      [AI_GATEWAY_HOST]: [
        {
          transform: [
            {
              headers: {
                authorization: `Bearer ${gatewayCredential}`,
              },
            },
          ],
        },
      ],
    },
  };
}

function buildClaudeGatewayEnv() {
  return {
    ANTHROPIC_BASE_URL: AI_GATEWAY_URL,
    // Claude Code expects Anthropic-shaped auth, but the sandbox policy injects
    // the real AI Gateway bearer token on outbound requests.
    ANTHROPIC_AUTH_TOKEN: "placeholder",
    ANTHROPIC_API_KEY: "",
  };
}

function resolveGatewayCredential() {
  const credential =
    process.env.AI_GATEWAY_API_KEY || process.env.VERCEL_OIDC_TOKEN;

  if (!credential) {
    throw new Error(
      [
        "Missing AI Gateway credential.",
        "Set AI_GATEWAY_API_KEY or run `vercel env pull` to provide VERCEL_OIDC_TOKEN.",
      ].join(" "),
    );
  }

  return credential;
}

function resolveSandboxCredentials(): SandboxCredentials {
  const token = process.env.VERCEL_TOKEN;
  const teamId = process.env.VERCEL_TEAM_ID;
  const projectId = process.env.VERCEL_PROJECT_ID;

  if (token && teamId && projectId) {
    return { token, teamId, projectId };
  }

  return {};
}

function ensureSandboxAuthEnv() {
  const credentials = resolveSandboxCredentials();
  if (credentials.token && credentials.teamId && credentials.projectId) return;
  if (process.env.VERCEL_OIDC_TOKEN?.trim()) return;
  throw new Error(
    [
      "Missing Vercel Sandbox credentials.",
      "Set VERCEL_OIDC_TOKEN, or set VERCEL_TOKEN, VERCEL_TEAM_ID, and VERCEL_PROJECT_ID.",
    ].join(" "),
  );
}

function describeError(error: unknown) {
  if (!(error instanceof Error)) return String(error);
  const parts = [error.message];
  if ("json" in error && error.json) {
    parts.push(sanitizeErrorDetail(JSON.stringify(error.json)));
  }
  if ("text" in error && error.text) {
    parts.push(sanitizeErrorDetail(String(error.text)));
  }
  return parts.filter(Boolean).join("\n");
}

function sanitizeErrorDetail(value: string) {
  let sanitized = value;
  for (const secret of [
    process.env.AI_GATEWAY_API_KEY,
    process.env.VERCEL_OIDC_TOKEN,
    process.env.VERCEL_TOKEN,
  ]) {
    if (secret) sanitized = sanitized.replaceAll(secret, "<redacted>");
  }
  return sanitized;
}

function loadDotEnvFiles(paths: string[]) {
  for (const path of paths) {
    const fullPath = join(repoRoot, path);
    if (!existsSync(fullPath)) continue;
    const lines = readFileSync(fullPath, "utf8").split(/\r?\n/);
    for (const line of lines) {
      const trimmed = line.trim();
      if (trimmed === "" || trimmed.startsWith("#")) continue;
      const match = /^(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)=(.*)$/.exec(
        trimmed,
      );
      if (!match || process.env[match[1]] !== undefined) continue;
      process.env[match[1]] = parseDotEnvValue(match[2]);
    }
  }
}

function parseDotEnvValue(value: string) {
  const trimmed = value.trim();
  const quote = trimmed[0];
  if ((quote === "\"" || quote === "'") && trimmed.endsWith(quote)) {
    const inner = trimmed.slice(1, -1);
    return quote === "\""
      ? inner
          .replace(/\\n/g, "\n")
          .replace(/\\r/g, "\r")
          .replace(/\\t/g, "\t")
          .replace(/\\"/g, "\"")
          .replace(/\\\\/g, "\\")
      : inner;
  }
  const commentStart = trimmed.search(/\s#/);
  return commentStart === -1 ? trimmed : trimmed.slice(0, commentStart).trimEnd();
}

function shellQuote(value: string) {
  if (/^[A-Za-z0-9_/:=.,@%+-]+$/.test(value)) return value;
  return `'${value.replace(/'/g, "'\\''")}'`;
}

function claudeModelName(value: string) {
  return value.startsWith("anthropic/") ? value.slice("anthropic/".length) : value;
}

function parseArgs(args: string[]): RunOptions {
  let caseId: string | null = null;
  let suiteId: string | null = null;
  let dryRun = false;
  let fixture = false;
  let json = false;
  let keepAlive = false;
  let maxTurns = DEFAULT_MAX_TURNS;
  let models = defaultModels();
  const requestedModels: string[] = [];
  let outDir = join(repoRoot, ".zero", "evals", "runs", timestamp());
  let sandboxRuntime = process.env.ZERO_EVAL_SANDBOX_RUNTIME ?? "node24";
  let sandboxTimeoutMsExplicit =
    process.env.ZERO_EVAL_SANDBOX_TIMEOUT_MS !== undefined &&
    process.env.ZERO_EVAL_SANDBOX_TIMEOUT_MS !== "";
  let sandboxTimeoutMs = parsePositiveIntOrDefault(
    process.env.ZERO_EVAL_SANDBOX_TIMEOUT_MS,
    DEFAULT_SANDBOX_TIMEOUT_MS,
    "ZERO_EVAL_SANDBOX_TIMEOUT_MS",
  );
  let sandboxVcpus = parsePositiveIntOrDefault(
    process.env.ZERO_EVAL_SANDBOX_VCPUS,
    4,
    "ZERO_EVAL_SANDBOX_VCPUS",
  );
  let commandTimeoutMs = parsePositiveIntOrDefault(
    process.env.ZERO_EVAL_COMMAND_TIMEOUT_MS,
    DEFAULT_COMMAND_TIMEOUT_MS,
    "ZERO_EVAL_COMMAND_TIMEOUT_MS",
  );

  for (let i = 0; i < args.length; i += 1) {
    const arg = args[i];
    if (arg === "--") {
      continue;
    } else if (arg === "--case") {
      caseId = requiredValue(args, ++i, "--case");
    } else if (arg === "--suite") {
      suiteId = requiredValue(args, ++i, "--suite");
    } else if (arg === "--model") {
      requestedModels.push(
        ...parseModelList(requiredValue(args, ++i, "--model"), "--model"),
      );
    } else if (arg === "--models") {
      requestedModels.push(
        ...parseModelList(requiredValue(args, ++i, "--models"), "--models"),
      );
    } else if (arg === "--max-turns") {
      maxTurns = parsePositiveInt(
        requiredValue(args, ++i, "--max-turns"),
        "--max-turns",
      );
    } else if (arg === "--out") {
      outDir = resolve(requiredValue(args, ++i, "--out"));
    } else if (arg === "--sandbox-runtime") {
      sandboxRuntime = requiredValue(args, ++i, "--sandbox-runtime");
    } else if (arg === "--sandbox-timeout-ms") {
      sandboxTimeoutMsExplicit = true;
      sandboxTimeoutMs = parsePositiveInt(
        requiredValue(args, ++i, "--sandbox-timeout-ms"),
        "--sandbox-timeout-ms",
      );
    } else if (arg === "--sandbox-vcpus") {
      sandboxVcpus = parsePositiveInt(
        requiredValue(args, ++i, "--sandbox-vcpus"),
        "--sandbox-vcpus",
      );
    } else if (arg === "--command-timeout-ms") {
      commandTimeoutMs = parsePositiveInt(
        requiredValue(args, ++i, "--command-timeout-ms"),
        "--command-timeout-ms",
      );
    } else if (arg === "--dry-run") {
      dryRun = true;
    } else if (arg === "--fixture") {
      fixture = true;
    } else if (arg === "--json") {
      json = true;
    } else if (arg === "--keep-alive") {
      keepAlive = true;
    } else if (arg === "--help" || arg === "-h") {
      printHelp();
      process.exit(0);
    } else {
      throw new Error(`Unknown evals flag: ${arg}`);
    }
  }

  if (requestedModels.length > 0) {
    models = uniqueOrdered(requestedModels);
  }

  return {
    caseId,
    suiteId,
    dryRun,
    fixture,
    json,
    keepAlive,
    maxTurns,
    models,
    outDir,
    sandboxRuntime,
    sandboxTimeoutMs,
    sandboxTimeoutMsExplicit,
    sandboxVcpus,
    commandTimeoutMs,
  };
}

function applyDefaultSandboxTimeout(options: RunOptions, caseCount: number) {
  if (options.fixture || options.sandboxTimeoutMsExplicit) return;
  const runCount = Math.max(1, options.models.length * caseCount);
  const requiredTimeoutMs =
    DEFAULT_SANDBOX_SETUP_TIMEOUT_MS + runCount * options.commandTimeoutMs;
  options.sandboxTimeoutMs = Math.max(
    options.sandboxTimeoutMs,
    requiredTimeoutMs,
  );
}

function defaultModels() {
  if (process.env.ZERO_EVAL_MODELS) {
    return parseModelList(process.env.ZERO_EVAL_MODELS, "ZERO_EVAL_MODELS");
  }
  if (process.env.ZERO_EVAL_MODEL) {
    return parseModelList(process.env.ZERO_EVAL_MODEL, "ZERO_EVAL_MODEL");
  }
  return DEFAULT_MODELS;
}

function parseModelList(value: string, label: string): string[] {
  const models = value
    .split(",")
    .map((model) => model.trim())
    .filter(Boolean);
  if (models.length === 0) {
    throw new Error(`${label} requires at least one model id`);
  }
  return uniqueOrdered(models);
}

function uniqueOrdered(values: string[]) {
  return values.filter((value, index) => values.indexOf(value) === index);
}

function selectCases(caseId: string | null, suiteId: string | null) {
  if (caseId && suiteId) {
    throw new Error("Use either --case or --suite, not both");
  }
  if (suiteId) {
    const cases = findEvalSuite(suiteId);
    if (cases.length === 0) {
      throw new Error(
        `Unknown eval suite '${suiteId}'. Available suites: ${evalSuiteIds().join(", ")}`,
      );
    }
    return cases;
  }
  if (!caseId) return evalCases;
  const evalCase = findEvalCase(caseId);
  if (!evalCase) {
    const ids = evalCases.map((item) => item.id).join(", ");
    throw new Error(`Unknown eval case '${caseId}'. Available cases: ${ids}`);
  }
  return [evalCase];
}

function requiredValue(args: string[], index: number, flag: string): string {
  const value = args[index];
  if (!value || value.startsWith("--")) {
    throw new Error(`${flag} requires a value`);
  }
  return value;
}

function parsePositiveInt(value: string, flag: string): number {
  const parsed = Number.parseInt(value, 10);
  if (!Number.isSafeInteger(parsed) || parsed <= 0) {
    throw new Error(`${flag} requires a positive integer`);
  }
  return parsed;
}

function parsePositiveIntOrDefault(
  value: string | undefined,
  fallback: number,
  label: string,
) {
  if (value === undefined || value === "") return fallback;
  return parsePositiveInt(value, label);
}

function delay(ms: number) {
  return new Promise<void>((resolveDelay) => setTimeout(resolveDelay, ms));
}

async function runLocalCommand(
  command: string,
  args: string[],
  timeoutMs = DEFAULT_LOCAL_COMMAND_TIMEOUT_MS,
  cwd = repoRoot,
): Promise<CommandResult> {
  return new Promise((resolveCommand) => {
    execFile(
      command,
      args,
      { cwd, encoding: "utf8", timeout: timeoutMs },
      (error, stdout, stderr) => {
        const code =
          typeof error?.code === "number" ? error.code : error ? 1 : 0;
        resolveCommand({ code, stdout, stderr });
      },
    );
  });
}

function failureReason(input: {
  run: CommandResult | null;
  build: CommandResult | null;
  runFailures: string[];
  budgetFailures: string[];
  actualStdout: string;
  actualStderr: string;
  expectedStdout: string;
  expectedStderr: string;
  patternFailures: string[];
  responseFormatFailures: string[];
  agentRequirementFailures: string[];
}) {
  if (input.build && input.build.code !== 0) {
    return `zero build exited ${input.build.code}`;
  }
  if (!input.run) return "zero run did not execute";
  if (input.runFailures.length > 0) {
    return input.runFailures[0];
  }
  if (input.budgetFailures.length > 0) {
    return input.budgetFailures[0];
  }
  if (input.patternFailures.length > 0) {
    return "source did not match required patterns";
  }
  if (input.responseFormatFailures.length > 0) {
    return "final response included prose or Markdown";
  }
  if (input.agentRequirementFailures.length > 0) {
    return "agent did not satisfy the required Zero CLI workflow";
  }
  return "unknown failure";
}

function validationBudgetFailures(
  evalCase: EvalCase,
  validationDurationMs: number,
): string[] {
  const max = evalCase.maxValidationDurationMs;
  if (!max || validationDurationMs <= max) return [];
  return [
    `validation took ${validationDurationMs}ms, exceeding budget ${max}ms`,
  ];
}

function getAgentRequirementFailures(
  metrics: AgentMetrics | null,
  maxTurns: number,
): string[] {
  if (!metrics) return ["missing agent metrics"];
  const failures = [];
  if (metrics.turnCount > maxTurns) {
    failures.push(
      `agent used ${metrics.turnCount} turns, exceeding max ${maxTurns}`,
    );
  }
  if (metrics.zeroCliCallCount === 0) failures.push("zero CLI was not called");
  if (metrics.zeroSkillLoadCount === 0) {
    failures.push("zero skill was not loaded");
  }
  if (metrics.zeroCheckCallCount === 0) failures.push("zero check was not called");
  if (metrics.zeroRunCallCount === 0) failures.push("zero run was not called");
  return failures;
}

function measureAgent(steps: AgentStep[]): AgentMetrics {
  const toolCalls = steps.flatMap((step) => step.toolCalls);
  const commands = toolCalls
    .map((call) => toolCommand(call))
    .filter((command): command is string => Boolean(command));
  const zeroCommands = commands.filter(isZeroCliCommand);
  const assistantTurnIds = new Set(
    steps
      .filter(
        (step) =>
          step.type === "assistant" &&
          (step.text.trim() !== "" || step.toolCalls.length > 0),
      )
      .map((step) => step.id || `assistant-step-${step.turn}`),
  );
  return {
    turnCount: assistantTurnIds.size,
    toolCallCount: toolCalls.length,
    zeroCliCallCount: zeroCommands.length,
    zeroSkillLoadCount: zeroCommands.filter(isZeroSkillLoadCommand).length,
    zeroCheckCallCount: zeroCommands.filter((command) =>
      /\b(?:\.\/)?bin\/zero\s+check\b/.test(command),
    ).length,
    zeroRunCallCount: zeroCommands.filter((command) =>
      /\b(?:\.\/)?bin\/zero\s+run\b/.test(command),
    ).length,
  };
}

function toolCommand(call: AgentToolCall): string | null {
  if (!isRecord(call.input)) return null;
  const command = call.input.command;
  return typeof command === "string" ? command : null;
}

function isZeroCliCommand(command: string) {
  return /\b(?:\.\/)?bin\/zero\b/.test(command);
}

function isZeroSkillLoadCommand(command: string) {
  return (
    /\b(?:\.\/)?bin\/zero\s+skills\s+get\s+zero\b/.test(command) &&
    /--full\b/.test(command)
  );
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return Boolean(value && typeof value === "object");
}

function truncate(text: string, limit: number): string {
  if (text.length <= limit) return text;
  return `${text.slice(0, limit)}\n...[truncated ${text.length - limit} chars]`;
}

function timestamp(): string {
  return new Date().toISOString().replace(/[:.]/g, "-");
}

function modelPathSegment(model: string) {
  const segment = model
    .replace(/\//g, "__")
    .replace(/[^A-Za-z0-9._-]+/g, "-")
    .replace(/^-+|-+$/g, "");
  return segment || "model";
}

function printJsonOrText(json: boolean, value: unknown) {
  if (json) {
    console.log(JSON.stringify(value, null, 2));
    return;
  }

  if (isSummary(value)) {
    for (const result of value.results) {
      const steps = result.agent
        ? `, ${result.agent.turnCount} turns, ${result.agent.toolCallCount} tools`
        : "";
      const status = result.passed ? "PASS" : "FAIL";
      console.log(
        `${status} ${result.model} ${result.id} ${result.mode} (${result.durationMs} ms${steps})`,
      );
      if (!result.passed && result.error) console.log(`  ${result.error}`);
      if (result.agentRequirementFailures.length > 0) {
        console.log(`  ${result.agentRequirementFailures.join("; ")}`);
      }
      if (result.responseFormatFailures.length > 0) {
        console.log(`  ${result.responseFormatFailures.join("; ")}`);
      }
      if (result.budgetFailures.length > 0) {
        console.log(`  ${result.budgetFailures.join("; ")}`);
      }
    }
    if (value.sandboxId) {
      console.log(
        `sandbox: ${value.sandboxId}${value.sandboxKeptAlive ? " (kept alive)" : ""}`,
      );
    }
    console.log(`results: ${value.outDir}`);
    return;
  }

  console.log(JSON.stringify(value, null, 2));
}

function isSummary(value: unknown): value is {
  outDir: string;
  sandboxId: string | null;
  sandboxKeptAlive: boolean;
  results: Array<{
    id: string;
    model: string;
    mode: string;
    passed: boolean;
    durationMs: number;
    error: string | null;
    agent: AgentMetrics | null;
    agentRequirementFailures: string[];
    responseFormatFailures: string[];
    budgetFailures: string[];
  }>;
} {
  return Boolean(
    value &&
      typeof value === "object" &&
      "results" in value &&
      Array.isArray(value.results),
  );
}

function printHelp() {
  console.log(`Usage: pnpm evals -- [options]

Options:
  --case <id>              Run one case, e.g. hello-world
  --suite <id>             Run one suite, e.g. agent-scale
  --model <id>             AI Gateway model id. May be repeated; overrides defaults
  --models <ids>           Comma-separated AI Gateway model ids; overrides defaults
  --max-turns <n>          Maximum Claude turns before failing scoring (default: ${DEFAULT_MAX_TURNS})
  --out <dir>              Output directory (default: .zero/evals/runs/<timestamp>)
  --sandbox-runtime <id>   Vercel Sandbox runtime (default: ZERO_EVAL_SANDBOX_RUNTIME or node24)
  --sandbox-timeout-ms <n> Sandbox timeout (default: ZERO_EVAL_SANDBOX_TIMEOUT_MS or enough for selected runs)
  --sandbox-vcpus <n>      Sandbox vCPU count (default: ZERO_EVAL_SANDBOX_VCPUS or 4)
  --command-timeout-ms <n> Claude command timeout (default: ZERO_EVAL_COMMAND_TIMEOUT_MS or 1200000)
  --keep-alive             Leave the Vercel Sandbox running after the eval
  --dry-run                Print selected cases without creating a sandbox
  --fixture                Run checked-in fixture answers locally instead of calling Claude
  --json                   Print JSON output

Live eval credentials:
  Vercel Sandbox: VERCEL_OIDC_TOKEN, or VERCEL_TOKEN + VERCEL_TEAM_ID + VERCEL_PROJECT_ID
  AI Gateway: AI_GATEWAY_API_KEY or VERCEL_OIDC_TOKEN

Default models:
  ${DEFAULT_MODELS.join("\n  ")}

Suites:
  ${evalSuiteIds().join("\n  ")}

Model environment:
  ZERO_EVAL_MODELS=<comma-separated ids>
  ZERO_EVAL_MODEL=<single id, kept for compatibility>
`);
}

main().catch((error: unknown) => {
  console.error(describeError(error));
  process.exit(1);
});
