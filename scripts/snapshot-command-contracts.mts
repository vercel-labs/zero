#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import { execFileSync, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { chmodSync, cpSync, existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, statSync, symlinkSync, utimesSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { createAggregateAssert, finishAggregateAssert } from "./aggregate-assert.mjs";

const assert = createAggregateAssert();

if (process.env.ZERO_NATIVE_TEST_SANDBOX !== "1" && process.env.ZERO_NATIVE_TEST_ALLOW_LOCAL !== "1") {
  console.error("command contract snapshots emit native test artifacts; run `pnpm run command-contracts` for Vercel Sandbox execution or set ZERO_NATIVE_TEST_ALLOW_LOCAL=1 to opt into local artifacts.");
  process.exit(1);
}

const outDir = ".zero/command-contracts";
const execMaxBuffer = 64 * 1024 * 1024;
const zeroBin = process.env.ZERO_BIN || (existsSync(".zero/bin/zero") ? resolve(".zero/bin/zero") : resolve("bin/zero"));
mkdirSync(outDir, { recursive: true });

function graphSidecarPath(sourcePath: string) {
  return `${sourcePath.slice(0, -2)}.graph`;
}

function zero(args, options: { allowFailure?: boolean; env?: Record<string, string> } = {}) {
  try {
    const stdout = execFileSync(zeroBin, args, { encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"], env: options.env ? { ...process.env, ...options.env } : process.env });
    return { code: 0, stdout };
  } catch (error) {
    if (!options.allowFailure) throw error;
    return {
      code: error.status ?? 1,
      stdout: error.stdout?.toString() ?? "",
      stderr: error.stderr?.toString() ?? "",
    };
  }
}

function zeroWithStderr(args, options: { cwd?: string } = {}) {
  const result = spawnSync(zeroBin, args, { encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"], cwd: options.cwd });
  if (result.error) throw result.error;
  return { code: result.status ?? 1, stdout: result.stdout ?? "", stderr: result.stderr ?? "" };
}

function zeroWithInput(args, input: string, options: { allowFailure?: boolean } = {}) {
  const result = spawnSync(zeroBin, args, { encoding: "utf8", maxBuffer: execMaxBuffer, input, stdio: ["pipe", "pipe", "pipe"] });
  if (result.error) throw result.error;
  const code = result.status ?? 1;
  if (code !== 0 && !options.allowFailure) {
    throw new Error(`zero ${args.join(" ")} exited ${code}: ${result.stderr ?? ""}`);
  }
  return { code, stdout: result.stdout ?? "", stderr: result.stderr ?? "" };
}

function zeroRaw(args, options: { allowFailure?: boolean; env?: Record<string, string> } = {}) {
  try {
    const stdout = execFileSync(zeroBin, args, { encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"], env: options.env ? { ...process.env, ...options.env } : process.env });
    return { code: 0, stdout };
  } catch (error) {
    if (!options.allowFailure) throw error;
    return {
      code: error.status ?? 1,
      stdout: error.stdout?.toString() ?? "",
      stderr: error.stderr?.toString() ?? "",
    };
  }
}

function assertPatchOkOutput(stdout: string, expectedSaved?: string) {
  assert.match(stdout, /^program graph patch ok\n/);
  if (expectedSaved) {
    assert.match(stdout, new RegExp(`saved: ${expectedSaved.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\n`));
  }
  assert.match(stdout, /graphHash: graph:[0-9a-f]{16}\n/);
  assert.match(stdout, /applied: \d+ ops?, \d+ nodes? touched\n/);
}

function json(args, options = {}) {
  const result = zero(args, options);
  return { ...result, body: JSON.parse(result.stdout) };
}

function jsonRaw(args, options = {}) {
  const result = zeroRaw(args, options);
  return { ...result, body: JSON.parse(result.stdout) };
}

function projectionSidecarGraphPath(sourcePath: string) {
  assert(sourcePath.endsWith(".0"), `projection path must end in .0: ${sourcePath}`);
  return graphSidecarPath(sourcePath);
}

function importProjectionSidecar(sourcePath: string) {
  const graphPath = projectionSidecarGraphPath(sourcePath);
  assert.equal(zero(["import", "--format", "binary", "--out", graphPath, sourcePath]).stdout, "");
  return graphPath;
}

function assertProjectionCheckOk(sourcePath: string) {
  const graphPath = importProjectionSidecar(sourcePath);
  assert.equal(zero(["check", graphPath]).stdout, "ok\n");
}

function writeProjectionFile(sourcePath: string, contents: string) {
  rmSync(projectionSidecarGraphPath(sourcePath), { force: true });
  writeFileSync(sourcePath, contents);
}

function repositoryGraphVerifyScript(root, options: { allowFailure?: boolean; target?: string } = {}) {
  const args = [
    "--experimental-strip-types",
    "--disable-warning=ExperimentalWarning",
    "scripts/repository-graph-verify-projection.mts",
    "--root",
    root,
  ];
  if (options.target) args.push("--target", options.target);
  try {
    const stdout = execFileSync(
      process.execPath,
      args,
      { encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"] },
    );
    return { code: 0, stdout, stderr: "" };
  } catch (error) {
    if (!options.allowFailure) throw error;
    return {
      code: error.status ?? 1,
      stdout: error.stdout?.toString() ?? "",
      stderr: error.stderr?.toString() ?? "",
    };
  }
}

function sha256File(path) {
  return createHash("sha256").update(readFileSync(path)).digest("hex");
}

function assertSourceGraph(body, artifact, moduleIdentity, lowering = "typed-program-graph-mir", canonicalSource = true, sourceProjectionState = undefined) {
  assert.equal(body.graph.artifact, artifact);
  assert.equal(body.graph.canonicalSource, canonicalSource);
  assert.equal(body.graph.moduleIdentity, moduleIdentity);
  assert.match(body.graph.graphHash, /^graph:[0-9a-f]{16}$/);
  assert.equal(body.graph.lowering, lowering);
  if (sourceProjectionState !== undefined) assert.equal(body.graph.sourceProjectionState, sourceProjectionState);
}

const programGraphParseTreeKeys = new Map();

function assertProgramGraphCompilerInput(body, artifact) {
  assert(body.compilerCaches.every((cache) => cache.sourceKind === "program-graph" && cache.graphHash === body.graph.graphHash));
  assert(body.compilerCaches.every((cache) => cache.parserArtifactsInKey === false));
  const caches = new Map<string, any>();
  for (const cache of body.compilerCaches) caches.set(cache.name, cache);
  const assertCacheInputs = (name, includes, excludes = []) => {
    const cache = caches.get(name);
    assert(cache, `missing compiler cache ${name}`);
    for (const key of includes) assert(cache.graphKeyInputs.includes(key), `${name} cache key inputs should include ${key}`);
    for (const key of excludes) assert(!cache.graphKeyInputs.includes(key), `${name} cache key inputs should not include ${key}`);
  };
  assertCacheInputs("parseTree", ["graphHash", "stdlibGraph", "nodeHashes", "importPaths", "compilerVersion", "packageDependencies"], ["sourceFiles", "targetFacts", "profile"]);
  assertCacheInputs("interface", ["graphHash", "modulePaths", "symbolFacts", "importGraph"], ["targetFacts", "profile", "compilerVersion", "packageDependencies"]);
  assertCacheInputs("checkedBody", ["graphHash", "stdlibGraph", "importPaths", "targetFacts", "compilerVersion", "packageDependencies"], ["sourceFiles", "profile"]);
  assertCacheInputs("specialization", ["graphHash", "stdlibGraph", "importPaths", "targetFacts", "profile", "compilerVersion", "packageDependencies"], ["sourceFiles"]);
  if (body.graph.lowering === "mapped-final-mir") {
    assertCacheInputs("mappedFinalMir", ["graphHash", "stdlibGraph", "importPaths", "targetFacts", "compilerVersion", "packageDependencies", "emitKind", "backend"], ["sourceFiles", "profile"]);
    const mappedMirCache = caches.get("mappedFinalMir");
    assert.match(mappedMirCache.path, /\.zero\/cache\/native\/mir-[0-9a-f]+\.zmir$/);
    assert.equal(mappedMirCache.memoryMapped, true);
    assert.equal(mappedMirCache.borrowedStorage, true);
    assert.equal(mappedMirCache.byteLength > 0, true);
    assert.equal(typeof mappedMirCache.codegenImmediate, "boolean");
    assert.equal(typeof mappedMirCache.programReconstructed, "boolean");
    assert.equal(body.incrementalInvalidation.graphInput.mappedFinalMir.path, mappedMirCache.path);
    assert.equal(body.incrementalInvalidation.graphInput.mappedFinalMir.memoryMapped, true);
    assert.equal(body.incrementalInvalidation.graphInput.mappedFinalMir.borrowedStorage, true);
    assert.equal(body.incrementalInvalidation.graphInput.mappedFinalMir.codegenImmediate, mappedMirCache.codegenImmediate);
    assert.equal(body.incrementalInvalidation.graphInput.mappedFinalMir.programReconstructed, mappedMirCache.programReconstructed);
  }
  assertCacheInputs("emittedObject", ["graphHash", "stdlibGraph", "importPaths", "targetFacts", "profile", "compilerVersion", "packageDependencies"], ["sourceFiles"]);
  const parseTreeCache = caches.get("parseTree");
  assert.equal(parseTreeCache.invalidatesOn, "ProgramGraph input");
  if (programGraphParseTreeKeys.has(artifact)) assert.equal(parseTreeCache.key, programGraphParseTreeKeys.get(artifact));
  else programGraphParseTreeKeys.set(artifact, parseTreeCache.key);
  assert.equal(body.incrementalInvalidation.sourceKind, "program-graph");
  assert.equal(body.incrementalInvalidation.graphInput.artifact, artifact);
  assert.equal(body.incrementalInvalidation.graphInput.graphHash, body.graph.graphHash);
  assert.equal(body.incrementalInvalidation.graphInput.parserArtifactsInKey, false);
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("graphHash"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("nodeHashes"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("typeFacts"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("symbolFacts"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("modulePaths"));
  assert(body.incrementalInvalidation.graphInput.keyedBy.includes("importPaths"));
  assert.equal(body.incrementalInvalidation.changedInputs.graphArtifact, artifact);
  assert.equal(body.incrementalInvalidation.interfaceFingerprints.sourceKind, "program-graph");
  assert.equal(body.incrementalInvalidation.interfaceFingerprints.graphHash, body.graph.graphHash);
}

function assertRepositoryGraphNativeCheck(body, sourceProjectionState = "clean", options: { graphHirToMirUsed?: boolean } = {}) {
  const graphHirToMirUsed = options.graphHirToMirUsed === false ? false : true;
  assert.equal(body.graphCompiler.input, "repository-graph-store");
  assert.equal(body.graphCompiler.graphStoreLoaded, true);
  assert.equal(body.graphCompiler.sourceProjectionRequiredForCompilerInput, false);
  assert.equal(body.graphCompiler.sourceProjectionState, sourceProjectionState);
  assert.equal(body.graphCompiler.graphNativeCheckerUsed, true);
  assert.equal(body.graphCompiler.graphHirToMirUsed, graphHirToMirUsed);
  assert.equal(body.graphCompiler.unsupportedGraphFacts.count, 0);
  assert.equal(body.graphCompiler.resolution.ok, true);
  assert.equal(body.graphCompiler.resolution.state, "resolved-graph-facts");
  assert.equal(body.graphCompiler.checking.ok, true);
  assert.equal(body.graphCompiler.checking.state, "checked-graph-readiness-facts");
  assert.equal(body.graphCompiler.checking.scope, "resolution-package-target-and-graph-mir-readiness");
  assert.equal(body.graphCompiler.checking.semanticDiagnosticsEnforced, false);
  assert.equal(body.graphCompiler.checking.authority, "ProgramGraphStore");
  assert.equal(body.graphCompiler.checking.sourceTextAuthority, false);
  assert.equal(body.graphCompiler.semanticFacts.state, "typed-facts");
  assert.equal(body.graphCompiler.semanticFacts.ok, true);
  const targetReady = body.targetReadiness?.ok === true;
  const compilerInputReady = targetReady && graphHirToMirUsed;
  assert.equal(body.graphCompiler.defaultReadiness.compilerInputReady, compilerInputReady);
  assert.equal(body.graphCompiler.defaultReadiness.claim, compilerInputReady ? "ready-for-repository-graph-input" : "blocked");
  assert.equal(body.graphCompiler.defaultReadiness.sourceFreeCompile, compilerInputReady);
  assert.equal(body.graphCompiler.defaultReadiness.sourceProjectionRequired, false);
  assert.equal(body.graphCompiler.defaultReadiness.sourceProjectionState, sourceProjectionState);
  assert.equal(body.graphCompiler.defaultReadiness.graphMir.used, graphHirToMirUsed);
  assert.equal(Object.hasOwn(body.graphCompiler.defaultReadiness, "fallback"), false);
  assert.equal(body.graphCompiler.defaultReadiness.performance.validationInLoad, true);
  assert.equal(body.graphCompiler.defaultReadiness.cacheInvalidation.parserArtifactsInKey, false);
  assert(body.graphCompiler.defaultReadiness.cacheInvalidation.keyedBy.includes("nodeHashes"));
  assert(body.graphCompiler.defaultReadiness.cacheInvalidation.keyedBy.includes("symbolFacts"));
  assert(body.graphCompiler.defaultReadiness.cacheInvalidation.keyedBy.includes("modulePaths"));
  assert(body.graphCompiler.defaultReadiness.cacheInvalidation.keyedBy.includes("importPaths"));
  assert.equal(body.graphCompiler.defaultReadiness.targetReadinessOk, targetReady);
  assert.equal(body.compileTime.deterministic, true);
  assert.equal(body.targetReadiness.languageOk, true);
  assert.equal(body.safetyFacts.schemaVersion, 1);
}

const graphHashPrime = 1099511628211n;
const graphHashMask = (1n << 64n) - 1n;

function graphHashText(hash, text) {
  for (const byte of Buffer.from(text ?? "")) {
    hash ^= BigInt(byte);
    hash = (hash * graphHashPrime) & graphHashMask;
  }
  hash ^= 0xffn;
  return (hash * graphHashPrime) & graphHashMask;
}

function graphHashU64(hash, value) {
  let item = BigInt(value);
  for (let i = 0n; i < 8n; i++) {
    hash ^= (item >> (i * 8n)) & 0xffn;
    hash = (hash * graphHashPrime) & graphHashMask;
  }
  return hash;
}

function graphQuotedField(line, field) {
  const match = line.match(new RegExp(`(?:^| )${field}:"((?:\\\\.|[^"])*)"`));
  return match ? JSON.parse(`"${match[1]}"`) : "";
}

function graphBareField(line, field, fallback = "") {
  const match = line.match(new RegExp(`(?:^| )${field}:([^ ]+)`));
  return match ? match[1] : fallback;
}

function graphTopLevelQuoted(line, field) {
  const match = line.match(new RegExp(`^${field} "([^"]*)"$`));
  return match ? match[1] : "";
}

function graphNodeHandle(line) {
  const match = line.match(/^node (#[A-Za-z0-9_.-]+)/);
  return match ? match[1] : "";
}

function graphNodeKind(line) {
  const match = line.match(/^node #[A-Za-z0-9_.-]+ ([A-Za-z_][A-Za-z0-9_-]*)/);
  return match ? match[1] : "";
}

function graphStoredModuleIdentity(text) {
  if (!text) return "module:main";
  if (text.startsWith("module:") || text.startsWith("package:")) return text;
  return `module:${text}`;
}

function graphEdgeRecord(line) {
  const match = line.match(/^edge (#[A-Za-z0-9_.-]+) ([A-Za-z_][A-Za-z0-9_-]*) ([^ ]+)/);
  return {
    line,
    from: match?.[1] ?? "",
    kind: match?.[2] ?? "",
    to: match?.[3] ?? "",
    target: graphBareField(line, "target", "node"),
    order: graphBareField(line, "order", "0"),
  };
}

function graphDomainId(domain, hash) {
  return `${domain}:${hash.toString(16).padStart(16, "0")}`;
}

function graphNodeDeclaresSymbol(node) {
  if (!node?.name) return false;
  return [
    "Module",
    "Const",
    "CImport",
    "TypeAlias",
    "Shape",
    "Interface",
    "Enum",
    "Choice",
    "Function",
    "Param",
    "Field",
    "EnumCase",
    "ChoiceCase",
    "Let",
    "ErrorVariant",
  ].includes(node.kind);
}

function graphNodeHash(line) {
  let hash = 1469598103934665603n;
  hash = graphHashText(hash, graphNodeKind(line));
  hash = graphHashText(hash, graphQuotedField(line, "name"));
  hash = graphHashText(hash, graphQuotedField(line, "type"));
  hash = graphHashText(hash, graphQuotedField(line, "value"));
  hash = graphHashU64(hash, graphBareField(line, "public", "false") === "true" ? 1n : 0n);
  hash = graphHashU64(hash, graphBareField(line, "mutable", "false") === "true" ? 1n : 0n);
  hash = graphHashU64(hash, graphBareField(line, "static", "false") === "true" ? 1n : 0n);
  hash = graphHashU64(hash, graphBareField(line, "fallible", "false") === "true" ? 1n : 0n);
  hash = graphHashU64(hash, graphBareField(line, "exportC", "false") === "true" ? 1n : 0n);
  return graphDomainId("nodehash", hash);
}

function graphSymbolComponent(text) {
  let out = "";
  for (const ch of text ?? "") out += /^[A-Za-z0-9._@/-]$/.test(ch) ? ch : "_";
  return out || "main";
}

function graphIdentityName(identity) {
  if (identity?.startsWith("module:")) return identity.slice("module:".length);
  if (identity?.startsWith("package:")) return identity.slice("package:".length);
  return identity || "main";
}

function graphModuleScopeName(moduleIdentity, moduleNode) {
  const moduleName = moduleNode?.name || graphIdentityName(moduleIdentity);
  if (moduleIdentity?.startsWith("package:")) return `${graphSymbolComponent(graphIdentityName(moduleIdentity))}/${graphSymbolComponent(moduleName)}`;
  return graphSymbolComponent(moduleName);
}

function graphImportBindingName(node) {
  if (node.value) return node.value;
  const parts = (node.name ?? "").split(".");
  return parts.at(-1) || node.name || "";
}

function graphSymbolName(node) {
  return node.kind === "Import" ? graphImportBindingName(node) : (node.name ?? "");
}

const graphOwnerKinds = new Set([
  "alias",
  "arg",
  "arm",
  "body",
  "cImport",
  "case",
  "choice",
  "const",
  "constraint",
  "declaredType",
  "default",
  "effect",
  "else",
  "enum",
  "error",
  "expr",
  "field",
  "function",
  "guard",
  "import",
  "interface",
  "left",
  "method",
  "param",
  "rangeEnd",
  "returnType",
  "right",
  "shape",
  "statement",
  "target",
  "then",
  "type",
  "typeArg",
  "typeParam",
  "value",
  "variant",
]);

function graphOwnerEdgeForNode(edges, nodeId) {
  return edges.find((edge) => edge.target === "node" && edge.to === nodeId && graphOwnerKinds.has(edge.kind)) ?? null;
}

function graphSymbolOwner(nodesById, edges, node) {
  let current = node?.id ?? "";
  for (let depth = 0; current && depth < nodesById.size; depth++) {
    const ownerEdge = graphOwnerEdgeForNode(edges, current);
    if (!ownerEdge) return null;
    const owner = nodesById.get(ownerEdge.from);
    if (!owner) return null;
    if (owner.kind === "Module" || graphNodeDeclaresSymbol(owner)) return owner;
    current = owner.id;
  }
  return null;
}

function graphNodeSymbolNamespace(node, ownerEdge) {
  switch (node.kind) {
    case "Module": return "module";
    case "Import": return "import";
    case "CImport": return "c-import";
    case "Const": return "value";
    case "Function": return ownerEdge?.kind === "method" ? "method" : "value";
    case "TypeAlias":
    case "Shape":
    case "Interface":
    case "Enum":
    case "Choice":
      return "type";
    case "Param": return "param";
    case "Field": return "field";
    case "EnumCase":
    case "ChoiceCase":
    case "ErrorVariant":
      return "variant";
    case "Let": return "local";
    default: return "";
  }
}

function graphLocalSymbolSuffix(node) {
  if (node.kind !== "Let") return "";
  const suffix = node.id.split(".").at(-1) || node.id;
  return suffix ? `@${graphSymbolComponent(suffix)}` : "";
}

function graphNodeSymbolIdFromGraph(moduleIdentity, nodesById, edges, node, depth = 0) {
  if (!graphNodeDeclaresSymbol(node) || depth > nodesById.size) return "";
  const name = graphSymbolName(node);
  if (!name) return "";
  if (node.kind === "Module") return `symbol:${graphModuleScopeName(moduleIdentity, node)}::module`;
  const ownerEdge = graphOwnerEdgeForNode(edges, node.id);
  const owner = graphSymbolOwner(nodesById, edges, node);
  const namespace = graphNodeSymbolNamespace(node, ownerEdge);
  if (!namespace) return "";
  if (owner?.kind === "Module") {
    return `symbol:${graphModuleScopeName(moduleIdentity, owner)}::${graphSymbolComponent(namespace)}.${graphSymbolComponent(name)}${graphLocalSymbolSuffix(node)}`;
  }
  if (owner) {
    const ownerSymbol = graphNodeSymbolIdFromGraph(moduleIdentity, nodesById, edges, owner, depth + 1);
    if (ownerSymbol) return `${ownerSymbol}/${graphSymbolComponent(namespace)}.${graphSymbolComponent(name)}${graphLocalSymbolSuffix(node)}`;
  }
  return `symbol:${graphSymbolComponent(graphIdentityName(moduleIdentity))}::${graphSymbolComponent(namespace)}.${graphSymbolComponent(name)}${graphLocalSymbolSuffix(node)}`;
}

function graphNodeTypeId(line) {
  const type = graphQuotedField(line, "type");
  return type ? graphDomainId("type", graphHashText(1469598103934665603n, type)) : "";
}

function graphNodeEffectId(line) {
  const kind = graphNodeKind(line);
  const name = graphQuotedField(line, "name");
  return kind === "EffectRef" && name ? graphDomainId("effect", graphHashText(1469598103934665603n, name)) : "";
}

function recomputeGraphHash(text) {
  const lines = text.trimEnd().split("\n");
  const moduleIdentity = graphStoredModuleIdentity(graphTopLevelQuoted(lines.find((line) => line.startsWith("module ")) ?? "", "module"));
  const nodeRecords = lines.filter((item) => item.startsWith("node ")).map((line) => ({
    line,
    id: graphNodeHandle(line),
    kind: graphNodeKind(line),
    name: graphQuotedField(line, "name"),
    type: graphQuotedField(line, "type"),
    value: graphQuotedField(line, "value"),
  }));
  const edgeRecords = lines.filter((item) => item.startsWith("edge ")).map(graphEdgeRecord);
  const nodesById = new Map(nodeRecords.map((node) => [node.id, node]));
  let hash = 1469598103934665603n;
  hash = graphHashU64(hash, 1n);
  hash = graphHashText(hash, moduleIdentity);
  for (const node of nodeRecords) {
    const line = node.line;
    hash = graphHashText(hash, node.id);
    hash = graphHashText(hash, graphQuotedField(line, "nodeHash") || graphNodeHash(line));
    hash = graphHashText(hash, graphQuotedField(line, "symbolId") || graphNodeSymbolIdFromGraph(moduleIdentity, nodesById, edgeRecords, node));
    hash = graphHashText(hash, graphQuotedField(line, "typeId") || graphNodeTypeId(line));
    hash = graphHashText(hash, graphQuotedField(line, "effectId") || graphNodeEffectId(line));
  }
  for (const edge of edgeRecords) {
    hash = graphHashText(hash, edge.from);
    hash = graphHashText(hash, edge.to);
    hash = graphHashText(hash, edge.kind);
    hash = graphHashText(hash, edge.target);
    hash = graphHashU64(hash, edge.order);
  }
  return `graph:${hash.toString(16).padStart(16, "0")}`;
}

function repositoryGraphPayload(storeText) {
  const marker = "\ngraph\n";
  const start = storeText.indexOf(marker);
  assert.notEqual(start, -1);
  return storeText.slice(start + marker.length);
}

function repositoryGraphModuleIdentity(storeText, graphText) {
  const stored = storeText.match(/^moduleIdentity "([^"]+)"$/m)?.[1];
  if (stored) return stored;
  const moduleLine = graphText.split("\n").find((line) => line.startsWith("module ")) ?? "";
  return graphStoredModuleIdentity(graphTopLevelQuoted(moduleLine, "module"));
}

function graphNodeHasSourceMap(line) {
  return graphQuotedField(line, "path") !== "" ||
    Number.parseInt(graphBareField(line, "line", "0"), 10) > 0 ||
    Number.parseInt(graphBareField(line, "column", "0"), 10) > 0;
}

function graphNodeIsDeclaration(kind) {
  return [
    "Import",
    "CImport",
    "Const",
    "TypeAlias",
    "Shape",
    "Interface",
    "Enum",
    "Choice",
    "Function",
    "Param",
    "Field",
    "EnumCase",
    "ChoiceCase",
    "Let",
    "ErrorVariant",
  ].includes(kind);
}

function graphNodeIsScope(kind) {
  return [
    "Module",
    "Function",
    "Block",
    "Shape",
    "Interface",
    "Enum",
    "Choice",
  ].includes(kind);
}

function graphTypeIsCapability(type) {
  return ["World", "Fs", "File", "Net", "HttpClient"].includes(type);
}

function graphTypeIsOwnership(type) {
  return type.includes("owned<") || type.includes("MutSpan<") || type.includes("mutref<");
}

function graphTypeIsResource(type) {
  return ["World", "Fs", "File"].includes(type) || type.includes("owned<File>");
}

function repositoryGraphCompilerTableCounts(storeText) {
  const graphText = repositoryGraphPayload(storeText);
  const moduleIdentity = repositoryGraphModuleIdentity(storeText, graphText);
  const nodeLines = graphText.split("\n").filter((line) => line.startsWith("node "));
  const edgeLines = graphText.split("\n").filter((line) => line.startsWith("edge "));
  const projection = storeText.match(/^projection path:/gm)?.length ?? 0;
  const counts = {
    schema: 1,
    package: moduleIdentity.startsWith("package:") ? 1 : 0,
    module: 0,
    declaration: 0,
    scope: 0,
    import: 0,
    symbol: 0,
    type: 0,
    effect: 0,
    capability: 0,
    ownership: 0,
    resource: 0,
    node: nodeLines.length,
    edge: edgeLines.length,
    projection,
    sourceMap: 0,
  };
  for (const line of nodeLines) {
    const kind = graphNodeKind(line);
    const type = graphQuotedField(line, "type");
    const fallible = graphBareField(line, "fallible", "false") === "true";
    const mutable = graphBareField(line, "mutable", "false") === "true";
    const node = {
      id: graphNodeHandle(line),
      kind,
      name: graphQuotedField(line, "name"),
      type,
      value: graphQuotedField(line, "value"),
    };
    if (kind === "Module") counts.module++;
    if (graphNodeIsDeclaration(kind)) counts.declaration++;
    if (graphNodeIsScope(kind)) counts.scope++;
    if (kind === "Import" || kind === "CImport") counts.import++;
    if (graphQuotedField(line, "symbolId") || graphNodeDeclaresSymbol(node)) counts.symbol++;
    if (graphQuotedField(line, "typeId") || type) counts.type++;
    if (graphQuotedField(line, "effectId") || kind === "EffectRef" || fallible) counts.effect++;
    if (fallible || graphTypeIsCapability(type)) counts.capability++;
    if (mutable || graphTypeIsOwnership(type)) counts.ownership++;
    if (graphTypeIsResource(type)) counts.resource++;
    if (graphNodeHasSourceMap(line)) counts.sourceMap++;
  }
  return counts;
}

function repositoryGraphCompilerTablesLine(storeText) {
  const counts = repositoryGraphCompilerTableCounts(storeText);
  return `compilerTables schema:${counts.schema} package:${counts.package} module:${counts.module} declaration:${counts.declaration} scope:${counts.scope} import:${counts.import} symbol:${counts.symbol} type:${counts.type} effect:${counts.effect} capability:${counts.capability} ownership:${counts.ownership} resource:${counts.resource} node:${counts.node} edge:${counts.edge} projection:${counts.projection} sourceMap:${counts.sourceMap}`;
}

function refreshRepositoryGraphCompilerMetadata(storeText) {
  const compilerStoreLine = 'compilerStore schemaVersion:1 shape:"compiler-oriented-tables" semanticOk:true semanticValidity:"shape-valid" sourceProjectionRequired:false sourceMapRequired:false storageInterface:"ProgramGraphStore"';
  const compilerHashInputsLine = 'compilerHashInputs graphHashExcludes:"source-path,line,column,projection-text" nodeHashExcludes:"source-path,line,column,projection-text" idCollisionFallbackMayUse:"source-path,line,column"';
  return storeText
    .replace(/^compilerStore .*$/m, compilerStoreLine)
    .replace(/^compilerTables .*$/m, repositoryGraphCompilerTablesLine(storeText))
    .replace(/^compilerHashInputs .*$/m, compilerHashInputsLine);
}

function repeatBuildHash(args, firstPath, repeatOut, repeatPath = repeatOut) {
  const repeatArgs = [...args];
  const outIndex = repeatArgs.indexOf("--out");
  assert(outIndex >= 0, "repeat build args should include --out");
  repeatArgs[outIndex + 1] = repeatOut;
  rmSync(repeatPath, { force: true, recursive: true });
  rmSync(`${repeatOut}.exe`, { force: true });
  const repeatReport = json(repeatArgs).body;
  assert.equal(repeatReport.generatedCBytes, 0);
  assert.equal(repeatReport.cBridgeFallback ?? false, false);
  assert.equal(sha256File(repeatPath), sha256File(firstPath));
  return repeatReport;
}

function hasAarch64Instruction(bytes, expected) {
  for (let offset = 0; offset + 4 <= bytes.length; offset++) {
    if (bytes.readUInt32LE(offset) === expected) return true;
  }
  return false;
}

function hasAarch64CondBranch(bytes, cond) {
  for (let offset = 0; offset + 4 <= bytes.length; offset++) {
    const instruction = bytes.readUInt32LE(offset);
    if ((instruction & 0xff000010) === 0x54000000 && (instruction & 0xf) === cond) return true;
  }
  return false;
}

function hasAarch64VoidReturnEpilogue(bytes) {
  for (let offset = 0; offset + 12 <= bytes.length; offset++) {
    if (bytes.readUInt32LE(offset) !== 0x52800000) continue;
    let cursor = offset + 4;
    const maybeAddSp = bytes.readUInt32LE(cursor);
    if (((maybeAddSp & 0xffc003ff) >>> 0) === 0x910003ff) cursor += 4;
    if (cursor + 8 <= bytes.length &&
        bytes.readUInt32LE(cursor) === 0xa8c17bfd &&
        bytes.readUInt32LE(cursor + 4) === 0xd65f03c0) return true;
  }
  return false;
}

function assertMachOLoadCommand(bytes, expectedCommand, expectedSize) {
  const ncmds = bytes.readUInt32LE(16);
  for (let offset = 32, i = 0; i < ncmds; i++) {
    const cmd = bytes.readUInt32LE(offset);
    const cmdsize = bytes.readUInt32LE(offset + 4);
    assert(cmdsize >= 8);
    assert(offset + cmdsize <= bytes.length);
    if (cmd === expectedCommand) {
      if (expectedSize !== undefined) assert.equal(cmdsize, expectedSize);
      return bytes.subarray(offset, offset + cmdsize);
    }
    offset += cmdsize;
  }
  assert.fail(`missing Mach-O load command 0x${expectedCommand.toString(16)}`);
}

function elfPackedErrorBytes(code, flagged = false) {
  // Raises functions pack (code << 32); world-main envelopes fold the error
  // flag into the same movabs as (code << 32) | 1, since a separate 32-bit
  // flag mov would zero-extend over the code.
  const bytes = Buffer.alloc(10);
  bytes[0] = 0x48;
  bytes[1] = 0xb8;
  bytes.writeBigUInt64LE((BigInt(code) << 32n) | (flagged ? 1n : 0n), 2);
  return bytes;
}

function hasAnsiControlBytes(text) {
  return /\x1b\[[0-9;?]*[ -/]*[@-~]|\x1b\][^\x07]*(?:\x07|\x1b\\)/.test(text);
}

function removeInlineTests(path) {
  if (!existsSync(path)) return;
  const source = readFileSync(path, "utf8");
  const testStart = source.indexOf("\n\ntest ");
  if (testStart >= 0) writeFileSync(path, `${source.slice(0, testStart)}\n`);
}

function tomlQuote(value: unknown) {
  return JSON.stringify(String(value));
}

function tomlArray(values: unknown[]) {
  return `[${values.map(tomlQuote).join(", ")}]`;
}

function appendManifestFields(lines: string[], object: Record<string, unknown>, order: string[]) {
  for (const key of order) {
    if (!(key in object)) continue;
    const value = object[key];
    if (Array.isArray(value)) lines.push(`${key} = ${tomlArray(value)}`);
    else if (typeof value === "boolean") lines.push(`${key} = ${value ? "true" : "false"}`);
    else lines.push(`${key} = ${tomlQuote(value)}`);
  }
}

function manifestToml(manifest: Record<string, any>) {
  const lines = ["[package]"];
  appendManifestFields(lines, manifest.package ?? {}, ["name", "version", "license"]);
  for (const [targetName, target] of Object.entries(manifest.targets ?? {})) {
    lines.push("", `[targets.${targetName}]`);
    appendManifestFields(lines, target as Record<string, unknown>, ["kind", "main", "graph", "defaultTarget", "devTarget", "releaseProfile"]);
  }
  if (manifest.repositoryGraph) {
    lines.push("", "[repositoryGraph]");
    appendManifestFields(lines, manifest.repositoryGraph, ["compilerInput"]);
  }
  for (const sectionName of ["deps", "dependencies"]) {
    if (!manifest[sectionName]) continue;
    lines.push("", `[${sectionName}]`);
    for (const [name, value] of Object.entries(manifest[sectionName])) {
      if (typeof value === "string") lines.push(`${name} = ${tomlQuote(value)}`);
    }
    for (const [name, value] of Object.entries(manifest[sectionName])) {
      if (typeof value === "string") continue;
      lines.push("", `[${sectionName}.${name}]`);
      appendManifestFields(lines, value as Record<string, unknown>, ["path", "version", "targets", "target"]);
    }
  }
  for (const [name, lib] of Object.entries(manifest.c?.libs ?? {})) {
    lines.push("", `[c.libs.${name}]`);
    appendManifestFields(lines, lib as Record<string, unknown>, ["headers", "include", "lib", "link", "mode", "pkg_config", "pkgConfig"]);
  }
  return `${lines.join("\n").replace(/\n{3,}/g, "\n\n")}\n`;
}

function writeZeroTomlSync(root: string, manifest: Record<string, any>) {
  writeFileSync(join(root, "zero.toml"), manifestToml(manifest));
}

function parseSimpleTomlManifest(text: string) {
  const manifest: Record<string, any> = {};
  let section: string[] = [];
  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#")) continue;
    const sectionMatch = line.match(/^\[([A-Za-z0-9_.-]+)\]$/);
    if (sectionMatch) {
      section = sectionMatch[1].split(".");
      let cursor = manifest;
      for (const part of section) cursor = cursor[part] ??= {};
      continue;
    }
    const valueMatch = line.match(/^([A-Za-z0-9_.-]+)\s*=\s*"([^"]*)"$/);
    if (!valueMatch) continue;
    let cursor = manifest;
    for (const part of section) cursor = cursor[part] ??= {};
    cursor[valueMatch[1]] = valueMatch[2];
  }
  return manifest;
}

function readGeneratedManifest(root: string) {
  const tomlPath = join(root, "zero.toml");
  if (existsSync(tomlPath)) return parseSimpleTomlManifest(readFileSync(tomlPath, "utf8"));
  return JSON.parse(readFileSync(join(root, "zero.json"), "utf8"));
}

function assertTemplateManifest(kind, manifest, readme) {
  assert.equal(manifest.package.version, "0.1.0");
  assert.equal(manifest.targets.cli.defaultTarget, "linux-musl-x64");
  assert.equal(manifest.targets.cli.devTarget, "host");
  assert.equal(manifest.targets.cli.releaseProfile, "release-small");
  if (manifest.docs) {
    assert.equal(manifest.docs.readme, "README.md");
    assert.deepEqual(manifest.docs.examples, [manifest.targets.cli.main]);
  }
  assert.match(readme, /zero check/);
  assert.match(readme, /zero test/);
  assert.match(readme, /zero dev --json/);
  if (kind === "lib") {
    assert.equal(manifest.targets.cli.main, "src/lib.0");
    assert.match(readme, /zero doc --json/);
  } else {
    assert.equal(manifest.targets.cli.main, "src/main.0");
    assert.match(readme, /zero run/);
    assert.match(readme, /zero build --target linux-musl-x64/);
  }
}

function assertDevReport(report, kind) {
  assert.equal(report.schemaVersion, 1);
  assert.equal(report.ok, true);
  assert.equal(report.mode, "watch-plan");
  assert.equal(report.generatedCBytes, 0);
  assert.equal(report.cBridgeFallback, false);
  assert.equal(report.watch.planOnly, true);
  assert.equal(report.watch.manifest, "zero.toml");
  assert.deepEqual(report.watch.rerun, ["check", "test", "examples"]);
  assert(Array.isArray(report.watch.packageLocks));
  assert(Array.isArray(report.watch.generatedBindingInputs));
  assert.equal(report.watch.restartOnSuccess, kind !== "lib");
  assert.equal(report.restart.runnableCli, kind !== "lib");
  assert.equal(report.trace.enabled, true);
  assert.equal(report.trace.requested, false);
  assert.equal(report.trace.phaseTiming, true);
  assert.equal(report.trace.cacheFacts, true);
  assert.equal(report.trace.diagnosticsPassthrough, true);
  assert.equal(report.partialDiagnostics.stable, true);
  assert.equal(report.partialDiagnostics.whileCodegenPending, true);
  assert.equal(report.interfaceFingerprints.algorithm, "fnv1a64-zero-interface-v1");
  assert.match(report.interfaceFingerprints.targetFactsHash, /^[0-9a-f]{16}$/);
  assert(report.interfaceFingerprints.modules.length >= 1);
  assert.equal(report.incrementalInvalidation.partialDiagnosticsStable, true);
  assert.match(String(report.incrementalInvalidation.cacheHits), /^\d+$/);
  assert.match(String(report.incrementalInvalidation.cacheMisses), /^\d+$/);
  assert(report.actions.some((action) => action.kind === "check"));
  assert(report.actions.some((action) => action.kind === "test"));
  assert(report.actions.some((action) => action.kind === "examples"));
  assert(report.actions.some((action) => action.kind === "restart" && action.enabled === (kind !== "lib")));
}

function assertReleaseTargetContract(report, expected) {
  const contract = report.releaseTargetContract;
  assert(contract, "release target contract should be present");
  assert.equal(contract.schemaVersion, 1);
  assert.equal(contract.target, expected.target);
  assert.equal(contract.emit, expected.emit);
  assert.equal(contract.artifactKind, expected.artifactKind);
  assert.equal(contract.objectFormat, expected.objectFormat);
  assert.equal(contract.linkerFlavor, expected.linkerFlavor);
  assert.equal(contract.libc.targetMode, expected.targetLibcMode);
  assert.equal(contract.libc.artifactMode, "none");
  assert.equal(contract.generatedCBytes, 0);
  assert.equal(contract.cBridgeFallback, false);
  assert.equal(contract.fallbackPolicy, "explicit-direct-never-c-bridge");
  assert.equal(contract.readiness.status, "supported");
  assert.equal(contract.readiness.directArtifact, true);
  assert.equal(contract.sysroot.requiredByArtifact, false);
  assert.equal(contract.determinism.repeatBuildHash, "checked-by-command-contracts");
  assert(contract.capabilityFacts.some((capability) => capability.name === "memory" && capability.available === true));
}

function directExeEmitterForTarget(target: string) {
  if (target.startsWith("linux") && target.includes("arm64")) return "zero-elf-aarch64-exe";
  if (target.startsWith("linux")) return "zero-elf64-exe";
  if (target === "darwin-x64") return "zero-macho-x64-exe";
  if (target.startsWith("darwin")) return "zero-macho64-exe";
  if (target === "win32-arm64.exe") return "zero-coff-aarch64-exe";
  if (target.startsWith("win32")) return "zero-coff-x64-exe";
  return "direct";
}

const generatedCBytesBeforeReadOnlyCommands = json(["size", "--json", "examples/memory-package"]).body.generatedCBytes;

assert.match(zero(["--version"]).stdout, /^zero 0\.3\.4 \(build (?:[0-9a-f]{7,40}|unknown)\)\n$/);

const version = json(["--version", "--json"]).body;
assert.equal(version.schemaVersion, 1);
assert.equal(version.version, "0.3.4");
assert.match(version.commit, /^(?:[0-9a-f]{7,40}|unknown)$/);
assert.equal(version.backend, "zero-c");
assert.equal(typeof version.host, "string");
assert(version.targets.includes("darwin-arm64"));
assert(version.targets.includes("linux-musl-x64"));
assert(version.targets.includes("win32-x64.exe"));
assert.equal(typeof version.targetCompiler.available, "boolean");

const doctor = json(["doctor", "--json"]).body;
assert.equal(doctor.schemaVersion, 1);
assert(["ok", "warning", "error"].includes(doctor.status));
assert(doctor.checks.some((check) => check.name === "native-c-compiler"));
assert(doctor.checks.some((check) => check.name === "target-c-compiler"));
assert(doctor.checks.some((check) => check.name === "llvm-toolchain"));
assert(["ready", "tool-missing", "unsupported-target"].includes(doctor.llvmToolchain.status));
assert.equal(typeof doctor.llvmToolchain.compiler, "string");
assert.equal(doctor.llvmToolchain.backendLifecycle.stability, "experimental");
assert.equal(doctor.llvmToolchain.backendLifecycle.defaultEligible, false);
assert.equal(doctor.llvmToolchain.backendLifecycle.releaseEligible, false);
assert(doctor.checks.some((check) => check.name === "cross-executable-builds" && /non-host executable builds|target-capable C compiler available/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "path" && /PATH/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "host-target" && /host target/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "target-sdk-sysroot" && /sysroot|target-capable C compiler/.test(check.message)));
assert(doctor.checks.some((check) => check.name === "docs-examples"));

for (const [command, expected] of [
  [["--help"], /zero init --template cli hello/],
  [["check", "--help"], /Usage: zero check/],
  [["build", "--help"], /Usage: zero build/],
  [["run", "--help"], /Usage: zero run/],
  [["test", "--help"], /Usage: zero test/],
  [["fmt", "--help"], /Usage: zero fmt/],
  [["init", "--help"], /--template cli\|lib\|package/],
  [["skills", "--help"], /Usage: zero skills/],
  [["targets", "--help"], /Usage: zero targets/],
  [["tokens", "--help"], /Usage: zero tokens/],
  [["parse", "--help"], /Usage: zero parse/],
  [["query", "--help"], /Usage: zero query \[--json\] \[--fn <name>\] \[--find <text>\] \[--refs <name>\] \[--calls <name>\] \[--node <id>\] \[--depth <n>\] \[--full\] \[--handles\] \[--no-help\] \[graph-input\|name\]/],
  [["inspect", "--help"], /Usage: zero init \[--template cli\|lib\|package\] \[project-path\]; zero query\|view\|diff\|dump\|inspect\|validate\|source-map\|roundtrip \[--json\] \[graph-input\]/],
  [["diff", "--help"], /Usage: zero diff \[--fn <name>\] \[graph-input\]/],
  [["size", "--help"], /Usage: zero size/],
  [["explain", "--help"], /Usage: zero explain/],
  [["fix", "--help"], /Usage: zero fix/],
] as Array<[string[], RegExp]>) {
  assert.match(zero(command).stdout, expected);
}
const removedNewCommand = zero(["new", "--help"], { allowFailure: true });
assert.notEqual(removedNewCommand.code, 0);
assert.match(removedNewCommand.stdout, /zero init --template cli hello/);

const tomlInitRoot = join(outDir, "init-toml-manifest");
rmSync(tomlInitRoot, { recursive: true, force: true });
const tomlInit = json(["init", "--json", "--manifest", "toml", tomlInitRoot]).body;
assert.equal(tomlInit.ok, true);
assert(tomlInit.writes.includes(join(tomlInitRoot, "zero.toml")));
assert(tomlInit.writes.includes(join(tomlInitRoot, "zero.graph")));
assert(existsSync(join(tomlInitRoot, "zero.toml")));
assert(!existsSync(join(tomlInitRoot, "zero.json")));
assert.match(readFileSync(join(tomlInitRoot, "zero.toml"), "utf8"), /\[package\]/);
const defaultInitRoot = join(outDir, "init-default-manifest");
rmSync(defaultInitRoot, { recursive: true, force: true });
const defaultInit = json(["init", "--json", defaultInitRoot]).body;
assert.equal(defaultInit.ok, true);
assert(defaultInit.writes.includes(join(defaultInitRoot, "zero.toml")));
assert(!existsSync(join(defaultInitRoot, "zero.json")));

for (const [code, goodExample, stalePattern] of [
  ["STD003", "var bytes: [4]u8 = [0, 0, 0, 0]", /\bmut bytes\b|std\.mem\.fill bytes/],
  ["TYP026", "alias Bytes = Span<u8>", /\btype [A-Z][A-Za-z0-9_]* =/],
  ["PUB001", "pub const answer: i32 = 42", /pub const answer (42|i32 42)/],
  ["IFC002", "fn describe(self: ref<Self>) -> String", /type Person\n|fn describe (String|i32)|ret self/],
  ["IFC003", "fn describe(self: ref<Self>) -> String", /fn describe (String|i32)|ret self/],
  ["IFC004", "fn describe(self: ref<Self>) -> String", /fn describe (String|i32)|ret self/],
  ["IFC005", "fn describe(self: ref<Self>) -> String", /fn describe (String|i32)|ret self/],
  ["STC001", "type Buf<static N: usize> {", /type Buf<static N: usize>\n|len usize/],
  ["RCV002", "var vec: FixedVec<u8, 4>", /\bmut vec\b|vec\.push 1|use `mut`/],
] as Array<[string, string, RegExp]>) {
  const explanation = json(["explain", "--json", code]).body;
  assert.equal(explanation.code, code);
  assert(explanation.examples.good.includes(goodExample), `${code} explain good example should use canonical source`);
  assert.doesNotMatch(
    `${explanation.repair.summary}\n${explanation.examples.bad}\n${explanation.examples.good}`,
    stalePattern,
    `${code} explain examples should use canonical source syntax`,
  );
}

// Every diagnostic code the compiler can emit must resolve in `zero explain`.
const compilerSourceDir = "native/zero-c/src";
const emittableDiagnosticCodes = new Set<string>();
for (const entry of readdirSync(compilerSourceDir)) {
  if (!entry.endsWith(".c")) continue;
  const source = readFileSync(join(compilerSourceDir, entry), "utf8");
  for (const match of source.matchAll(/"([A-Z]{3,4}[0-9]{3})"/g)) emittableDiagnosticCodes.add(match[1]);
}
assert(emittableDiagnosticCodes.size > 100, "expected to discover the compiler diagnostic code catalog");
for (const code of [...emittableDiagnosticCodes].sort()) {
  const explained = json(["explain", "--json", code], { allowFailure: true });
  assert.equal(explained.code, 0, `zero explain ${code} must have an explain entry`);
  assert.equal(explained.body.code, code, `zero explain ${code} must explain the requested code`);
}

const graphHelp = zero(["inspect", "--help"]).stdout;
assert.match(graphHelp, /zero dump\|validate\|roundtrip \[--json\] \[--format text\|binary\] --out <program-graph-artifact> \[graph-input\]; zero import \[--json\] \[--format text\|binary\] --out <program-graph-artifact> \[project\|zero\.toml\|zero\.json\|file\.0\]/);
assert.match(graphHelp, /zero view \[--json\] \[--fn <name> \[--around <text>\|--handles\]\] \[--outline <module-or-file>\] \[--out <file\.0>\] \[graph-input\]/);
assert.match(graphHelp, /zero diff \[--fn <name>\] \[graph-input\]/);
assert.match(graphHelp, /zero source-map \[--json\] \[graph-input\]/);
assert.match(graphHelp, /zero query \[--json\] \[--fn <name>\] \[--find <text>\] \[--refs <name>\] \[--calls <name>\] \[--node <id>\] \[--depth <n>\] \[--full\] \[--handles\] \[--no-help\] \[graph-input\|name\]/);
assert.match(graphHelp, /zero reconcile \[--json\] <base-graph-input> --source <edited-file\.0\|project\|zero\.toml\|zero\.json>/);
assert.match(graphHelp, /zero status\|verify-projection \[--json\] \[project\|zero\.toml\|zero\.json\|file\.0\|zero\.graph\]/);
assert.match(graphHelp, /zero import \[--json\] \[--format text\|binary\] \[project\|zero\.toml\|zero\.json\|file\.0\]/);
assert.match(graphHelp, /zero export \[--json\] \[project\|zero\.toml\|zero\.json\|file\.0\]/);
assert.match(graphHelp, /zero merge --base <base-zero\.graph> --left <left-zero\.graph> --right <right-zero\.graph> \[--json\] \[project\|zero\.toml\|zero\.json\|file\.0\]/);
assert.match(graphHelp, /zero size \[--json\] \[--target <target>\] \[--out <artifact>\] \[graph-input\]/);
assert.match(graphHelp, /Patch usage: zero patch \[--json\] \[--check-only\|--dry-run\] \[--format text\|binary\] \[--out <program-graph-artifact>\] \[graph-input\] \(<patch-file>\|--op <operation>\|--replace-fn <name> --body-file <file\|->\|--replace-in-fn <name> --old <text> --new <text>\|--rewrite <pattern> --to <template> \[--fn <name>\] \[--apply\]\)/);
assert.match(graphHelp, /Patch operation help: zero patch --op help/);
assert.match(graphHelp, /binary is the default zero\.graph encoding/);
assert.match(graphHelp, /--format text writes readable repository stores when explicitly requested/);
assert.match(graphHelp, /--format binary opts explicit graph artifact outputs into binary storage/);
assert.doesNotMatch(graphHelp, /setMain[A-Za-z]+Cli/);
assert.match(graphHelp, /replaceFunctionBody main \.\.\. end/);
assert.match(graphHelp, /replaceBlockBody #block_id \.\.\. end/);
assert.match(graphHelp, /addLetBinary fn="add" name="sum" type="i32" operator="\+"/);
assert.match(graphHelp, /addReturnExpr fn="maybe" expr="null"/);
assert.match(graphHelp, /appendStmt fn="main" stmt="check std\.http\.listen\(world, 3000_u16\)"/);
assert.match(graphHelp, /addTestBody name="api add"/);
assert.match(graphHelp, /renameTest name="api add" value="api add route"/);
assert.match(graphHelp, /deleteTest name="api add"/);
assert.match(graphHelp, /upsertFunction handle/);
assert.match(graphHelp, /zero build \[--json\] \[--emit exe\|obj\|llvm-ir\].*\[graph-input\]/);
assert.match(graphHelp, /zero run \[--target <host-target>\].*\[graph-input\] \[-- args\.\.\.\]/);
assert.match(graphHelp, /zero test \[--json\] \[--filter <name>\] \[--target <target>\] \[graph-input\]/);
assert.doesNotMatch(graphHelp, /zero graph (check|patch|size|build|run|test)/);
assert.doesNotMatch(graphHelp, /zero sync/);
const runHelp = zero(["run", "--help"]).stdout;
assert.match(runHelp, /Usage: zero run \[--backend direct\|llvm\|<direct-emitter>\]/);
assert.match(runHelp, /LLVM is explicit and requires clang/);
const rootHelp = zero(["--help"]).stdout;
assert.match(rootHelp, /zero run \[--backend direct\|llvm\|<direct-emitter>\].*\[graph-input\] \[-- args\.\.\.\]/);
assert.match(rootHelp, /zero build \[--json\] \[--emit exe\|obj\|llvm-ir\].*\[graph-input\]/);
assert.match(rootHelp, /zero test \[graph-input\]/);
assert.match(rootHelp, /zero check \[--json\] \[--target <target>\] \[--emit exe\|obj\|llvm-ir\] \[graph-input\]/);
assert.match(rootHelp, /zero fix --plan --json \[graph-input\]/);
assert.match(rootHelp, /zero patch \[--json\] \[--check-only\|--dry-run\] \[--format text\|binary\] \[--out <program-graph-artifact>\] \[graph-input\] \(<patch-file>\|--op <operation>\|--replace-fn <name> --body-file <file\|->\|--replace-in-fn <name> --old <text> --new <text>\|--rewrite <pattern> --to <template> \[--fn <name>\] \[--apply\]\)/);
assert.match(rootHelp, /zero dump\|validate\|roundtrip \[--json\] \[--format text\|binary\] \[--out <program-graph-artifact>\] \[graph-input\]/);
assert.match(rootHelp, /zero view \[--json\] \[--fn <name> \[--around <text>\|--handles\]\] \[--outline <module-or-file>\] \[--out <file\.0>\] \[graph-input\]/);
assert.match(rootHelp, /zero diff \[--fn <name>\] \[graph-input\]/);
assert.match(rootHelp, /zero source-map \[--json\] \[graph-input\]/);
assert.match(rootHelp, /zero query \[--json\] \[--fn <name>\] \[--find <text>\] \[--refs <name>\] \[--calls <name>\] \[--node <id>\] \[--depth <n>\] \[--full\] \[--handles\] \[--no-help\] \[graph-input\|name\]/);
assert.match(rootHelp, /zero reconcile \[--json\] <base-graph-input> --source <edited-file\.0\|project\|zero\.toml\|zero\.json>/);
assert.match(rootHelp, /zero status\|verify-projection \[--json\] \[project\|zero\.toml\|zero\.json\|file\.0\|zero\.graph\]/);
assert.match(rootHelp, /zero import \[--json\] \[--format text\|binary\] \[--out <program-graph-artifact>\] \[project\|zero\.toml\|zero\.json\|file\.0\]/);
assert.match(rootHelp, /zero export \[--json\] \[project\|zero\.toml\|zero\.json\|file\.0\]/);
assert.match(rootHelp, /zero merge --base <base-zero\.graph> --left <left-zero\.graph> --right <right-zero\.graph> \[--json\] \[--format text\|binary\] <project\|zero\.toml\|zero\.json\|file\.0>/);
assert.doesNotMatch(rootHelp, /zero graph (check|patch|size|build|run|test)/);
assert.doesNotMatch(rootHelp, /zero sync/);
const graphPatchHelp = zero(["patch", "--op", "help"]).stdout;
assert.match(graphPatchHelp, /program graph patch operations/);
assert.match(graphPatchHelp, /accepted by zero patch --op, --patch-text, and zero-program-graph-patch v1 files/);
assert.match(graphPatchHelp, /insert node="#id" kind="Literal"/);
assert.match(graphPatchHelp, /insertEdge from="#from" to="#to"/);
assert.match(graphPatchHelp, /replace node="#id" expect="nodehash:abc123"/);
assert.doesNotMatch(graphPatchHelp, /setMain[A-Za-z]+Cli/);
assert.match(graphPatchHelp, /replaceFunctionBody main/);
assert.match(graphPatchHelp, /replaceBlockBody #block_id/);
assert.match(graphPatchHelp, /use --replace-fn with --body-file/);
assert.match(graphPatchHelp, /--body-file - reads the body rows from stdin, so a heredoc does the whole edit in one call/);
assert.match(graphPatchHelp, /zero patch \. --replace-fn greet --body-file - <<'EOF'/);
assert.match(graphPatchHelp, /Alternative: write the rows to a file and pass its path/);
assert.match(graphPatchHelp, /zero patch \. --replace-fn greet --body-file \/tmp\/greet\.body/);
assert(graphPatchHelp.indexOf("--body-file - <<'EOF'") < graphPatchHelp.indexOf("/tmp/greet.body"), "patch help should show the stdin heredoc form before the temp-file form");
assert.match(graphPatchHelp, /--replace-in-fn/);
assert.match(graphPatchHelp, /zero patch \. --replace-in-fn greet --old 'return 1' --new 'return 2'/);
assert.match(graphPatchHelp, /--old must match the body text zero view --fn <name> prints exactly once/);
assert.match(graphPatchHelp, /advanced: node-level ops \(see zero view --fn <name> --handles\)/);
assert.match(graphPatchHelp, /replaceExpr node="#id" with="left \+ 1"/);
assert.match(graphPatchHelp, /--patch-text - reads a complete patch from stdin/);
const patchHelpInFnIdx = graphPatchHelp.indexOf("--replace-in-fn greet");
const patchHelpReplaceFnIdx = graphPatchHelp.indexOf("--replace-fn greet --body-file - <<'EOF'");
const patchHelpGrammarIdx = graphPatchHelp.indexOf("zero-program-graph-patch v1 files");
const patchHelpAdvancedIdx = graphPatchHelp.indexOf("advanced: node-level ops");
assert(patchHelpInFnIdx >= 0 && patchHelpInFnIdx < patchHelpReplaceFnIdx, "patch help leads with --replace-in-fn");
assert(patchHelpReplaceFnIdx < patchHelpGrammarIdx, "the --replace-fn heredoc precedes the op grammar reference");
assert(patchHelpGrammarIdx < patchHelpAdvancedIdx, "the op grammar precedes the advanced node-level section");
assert(graphPatchHelp.indexOf('set node="#id"') > patchHelpAdvancedIdx, "node-handle ops sit under the advanced header");
assert(graphPatchHelp.indexOf("addMain") < patchHelpAdvancedIdx, "authoring ops print before node-level ops");
assert.match(graphPatchHelp, /addLetLiteral fn="main" name="count" type="u32" value="0"/);
assert.match(graphPatchHelp, /addReturnValue fn="identity" value="input" type="i32"/);
assert.match(graphPatchHelp, /addReturnExpr fn="maybe" expr="null"/);
assert.match(graphPatchHelp, /appendStmt fn="main" stmt="check std\.http\.listen\(world, 3000_u16\)"/);
assert.match(graphPatchHelp, /addTestBody name="api add"/);
assert.match(graphPatchHelp, /renameTest name="api add" value="api add route"/);
assert.match(graphPatchHelp, /deleteTest name="api add"/);
assert.match(graphPatchHelp, /upsertFunction handle/);
const graphPatchHelpJson = json(["patch", "--op", "help", "--json"]).body;
assert.equal(graphPatchHelpJson.ok, true);
assert.equal(graphPatchHelpJson.command, "zero patch");
assert.equal(graphPatchHelpJson.operations.includes("insert node=\"#id\" kind=\"Literal\" parent=\"#parent\" edge=\"arg\" order=\"0\" type=\"String\" value=\"text\""), true);
assert.equal(graphPatchHelpJson.operations.includes("insertEdge from=\"#from\" to=\"#to\" edge=\"arg\" target=\"node\" order=\"0\""), true);
assert.equal(graphPatchHelpJson.operations.includes("replace node=\"#id\" expect=\"nodehash:abc123\" kind=\"Literal\" type=\"String\" value=\"text\""), true);
assert.equal(graphPatchHelpJson.operations.some((op) => op.includes("setMain")), false);
assert.equal(graphPatchHelpJson.operations.some((op) => op.startsWith("replaceFunctionBody main\n")), true);
assert.equal(graphPatchHelpJson.operations.some((op) => op.startsWith("replaceBlockBody #block_id\n")), true);
assert.equal(graphPatchHelpJson.operations.includes("addLetBinary fn=\"add\" name=\"sum\" type=\"i32\" operator=\"+\" left=\"left\" right=\"right\""), true);
assert.equal(graphPatchHelpJson.operations.includes("addCheckWriteValue fn=\"main\" value=\"message\" type=\"String\""), true);
assert.equal(graphPatchHelpJson.operations.includes("addReturnExpr fn=\"maybe\" expr=\"null\""), true);
assert.equal(graphPatchHelpJson.operations.includes("appendStmt fn=\"main\" stmt=\"check std.http.listen(world, 3000_u16)\""), true);
assert.equal(graphPatchHelpJson.operations.some((op) => op.startsWith("addTestBody name=\"api add\"\n")), true);
assert.equal(graphPatchHelpJson.operations.includes("renameTest name=\"api add\" value=\"api add route\""), true);
assert.equal(graphPatchHelpJson.operations.includes("deleteTest name=\"api add\""), true);
assert.equal(graphPatchHelpJson.operations.some((op) => op.startsWith("upsertFunction handle\n")), true);
const retiredGraphCheckJson = json(["graph", "check", "--json", "examples/hello.0"], { allowFailure: true });
assert.equal(retiredGraphCheckJson.code, 1);
assert.equal(retiredGraphCheckJson.body.diagnostics[0].message, "zero graph check is not supported");
assert.equal(retiredGraphCheckJson.body.diagnostics[0].expected, "zero check [graph-input]");
const retiredGraphPatchJson = json(["graph", "patch", "--json", "--op", "help"], { allowFailure: true });
assert.equal(retiredGraphPatchJson.code, 1);
assert.equal(retiredGraphPatchJson.body.diagnostics[0].message, "zero graph patch is not supported");
assert.match(retiredGraphPatchJson.body.diagnostics[0].expected, /zero patch/);
const retiredGraphDumpJson = json(["graph", "dump", "--json", "examples/hello.0"], { allowFailure: true });
assert.equal(retiredGraphDumpJson.code, 1);
assert.equal(retiredGraphDumpJson.body.diagnostics[0].message, "zero graph dump is not supported");
assert.equal(retiredGraphDumpJson.body.diagnostics[0].expected, "zero dump [graph-input]");
const retiredGraphDefaultJson = json(["graph", "--json", "examples/hello.0"], { allowFailure: true });
assert.equal(retiredGraphDefaultJson.code, 1);
assert.equal(retiredGraphDefaultJson.body.diagnostics[0].message, "zero graph is not supported");
assert.equal(retiredGraphDefaultJson.body.diagnostics[0].expected, "zero inspect [graph-input]");
const retiredGraphHelpJson = json(["graph", "--help", "--json"], { allowFailure: true });
assert.equal(retiredGraphHelpJson.code, 1);
assert.equal(retiredGraphHelpJson.body.diagnostics[0].message, "zero graph is not supported");
assert.equal(retiredGraphHelpJson.body.diagnostics[0].expected, "zero inspect [graph-input]");

const graphDump = zero(["dump", "examples/hello.0"]).stdout;
const graphDumpAgain = zero(["dump", "examples/hello.0"]).stdout;
assert.equal(graphDumpAgain, graphDump);
assert.equal(zero(["dump", "examples/hello.0"]).stdout, graphDump);
assert.match(graphDump, /^zero-graph v1\n/);
assert.match(graphDump, /origin source-text/);
assert.match(graphDump, /module "hello"/);
assert.match(graphDump, /hash "graph:[0-9a-f]{16}"/);
assert.doesNotMatch(graphDump, /idStrategy/);
assert.doesNotMatch(graphDump, /canonicalSource/);
assert.doesNotMatch(graphDump, /moduleIdentity/);
assert.doesNotMatch(graphDump, /graphHash/);
assert.doesNotMatch(graphDump, /counts nodes=/);
assert.doesNotMatch(graphDump, /validation "shape-valid" ok/);
assert.doesNotMatch(graphDump, / nodeHash:/);
assert.match(graphDump, / path:"hello\.0"/);
assert.match(graphDump, / line:1 column:1/);
assert.match(graphDump, / line:2 column:27/);
assert.doesNotMatch(graphDump, / public:false/);
assert.doesNotMatch(graphDump, / target:node order:0/);

const rootQueryHello = json(["query", "--json", "--fn", "main", "examples/hello.0"]).body;
assert.equal(rootQueryHello.ok, true);
assert.equal(rootQueryHello.query.function, "main");
assert.equal(rootQueryHello.functions.some((fun: any) => fun.name === "main"), true);
assert.equal(zero(["view", "examples/hello.0"]).stdout, readFileSync("examples/hello.0", "utf8"));

const queryScopeRoot = join("/tmp", `zero-query-scope-${process.pid}`);
rmSync(queryScopeRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", queryScopeRoot]).body.ok, true);
mkdirSync(join(queryScopeRoot, "src"), { recursive: true });
writeFileSync(
  join(queryScopeRoot, "src", "main.0"),
  'fn dealsTotal(amount: usize) -> usize {\n    return amount + 1\n}\n\npub fn main(world: World) -> Void raises {\n    let total: usize = dealsTotal(41)\n    if total == 42 {\n        check world.out.write("query scope\\n")\n    }\n}\n',
);
assert.equal(json(["import", "--json", join(queryScopeRoot, "src", "main.0")]).body.ok, true);
const queryScopeFind = json(["query", "--json", "--find", "dealsTotal", queryScopeRoot]).body;
assert.equal(queryScopeFind.ok, true);
const queryScopeFunctionId = queryScopeFind.matches.find((match: any) => match.kind === "Function")?.id;
assert(queryScopeFunctionId);
const queryScopeWholeModule = zero(["query", "--json", queryScopeRoot]);
const queryScopeNode = json(["query", "--json", "--node", queryScopeFunctionId, queryScopeRoot]);
assert.equal(queryScopeNode.body.ok, true);
assert.equal(queryScopeNode.body.query.scope, "node");
assert.equal(queryScopeNode.body.query.depth, 1);
assert.equal(queryScopeNode.body.node.selected.id, queryScopeFunctionId);
assert.equal(queryScopeNode.body.node.span.path, "src/main.0");
assert(queryScopeNode.body.node.parents.length >= 1);
assert(queryScopeNode.body.node.children.length >= 1);
assert.equal(queryScopeNode.body.node.children.every((edge: any) => edge.children === undefined), true);
assert.match(queryScopeNode.body.hint, /--depth <n>.*--full.*zero view --fn <name>/);
assert(queryScopeNode.stdout.length < 4096, `node-scoped query output should stay within a few KB, got ${queryScopeNode.stdout.length}`);
assert(queryScopeNode.stdout.length < queryScopeWholeModule.stdout.length, "node-scoped query output should be smaller than the whole-module report");
const queryScopeDepthTwo = json(["query", "--json", "--node", queryScopeFunctionId, "--depth", "2", queryScopeRoot]).body;
assert.equal(queryScopeDepthTwo.query.depth, 2);
assert.equal(queryScopeDepthTwo.node.children.some((edge: any) => Array.isArray(edge.children)), true);
const queryScopeFullModule = json(["query", "--json", "--node", queryScopeFunctionId, "--full", queryScopeRoot]).body;
assert.equal(queryScopeFullModule.ok, true);
assert(queryScopeFullModule.counts.nodes > 0);
assert.equal(queryScopeFullModule.node.selected.id, queryScopeFunctionId);
assert(Array.isArray(queryScopeFullModule.functions));
const queryScopeBadDepth = json(["query", "--json", "--node", queryScopeFunctionId, "--depth", "0", queryScopeRoot], { allowFailure: true });
assert.notEqual(queryScopeBadDepth.code, 0);
assert.match(queryScopeBadDepth.body.diagnostics[0].message, /invalid query depth '0'/);
const queryBareName = JSON.parse(execFileSync(zeroBin, ["query", "--json", "dealsTotal"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer }));
assert.equal(queryBareName.ok, true);
assert.equal(queryBareName.query.find, "dealsTotal");
assert.equal(queryBareName.argument.value, "dealsTotal");
assert.equal(queryBareName.argument.resolvedAs, "find");
assert.match(queryBareName.argument.hint, /zero view --fn <name>/);
assert.equal(queryBareName.matches.some((match: any) => match.kind === "Function" && match.name === "dealsTotal"), true);
const queryBareNameText = execFileSync(zeroBin, ["query", "dealsTotal"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer });
assert.match(queryBareNameText, /argument: dealsTotal is not an existing path; treated as --find dealsTotal/);
assert.match(queryBareNameText, /zero view --fn <name> prints one function's source/);
assert.doesNotMatch(queryBareNameText, /modules:/, "scoped query text output must not repeat the module list");
assert.doesNotMatch(queryBareNameText, /patch help:/, "scoped query text output must not repeat the patch footer");
const queryOverviewText = execFileSync(zeroBin, ["query"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer });
assert.match(queryOverviewText, /modules:/);
assert.match(queryOverviewText, /main path:src\/main\.0 functions:2 \(entry\)/);
assert.match(queryOverviewText, /functions \(module main\):/);
assert.match(queryOverviewText, /dealsTotal\(amount: usize\) -> usize #decl_/);
assert.doesNotMatch(queryOverviewText, /stmt\[0\]/, "overview hides stmt handle inventories unless --handles is set");
assert.doesNotMatch(queryOverviewText, /resolution:/, "overview omits resolution facts");
assert.match(queryOverviewText, /tips: zero query --fn <name> \| --find <text> \| --calls <name> \| --refs <name>/);
assert.match(queryOverviewText, /zero view --fn <name>.*zero view --outline/);
assert(queryOverviewText.length < 3072, `bare query overview should stay compact, got ${queryOverviewText.length}`);
const queryFnText = execFileSync(zeroBin, ["query", "--fn", "dealsTotal"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer });
assert.match(queryFnText, /query: fn:dealsTotal/);
assert.match(queryFnText, /dealsTotal\(amount: usize\) -> usize #decl_/);
assert.doesNotMatch(queryFnText, /stmt\[0\]/);
assert.match(queryFnText, /add --handles/);
const queryFnHandlesText = execFileSync(zeroBin, ["query", "--fn", "dealsTotal", "--handles"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer });
assert.match(queryFnHandlesText, /param\[0\] amount: usize #param_/);
assert.match(queryFnHandlesText, /stmt\[0\] Return #stmt_/);
assert.match(queryFnHandlesText, /patch help:/);
const queryFnHandlesNoHelpText = execFileSync(zeroBin, ["query", "--fn", "dealsTotal", "--handles", "--no-help"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer });
assert.match(queryFnHandlesNoHelpText, /param\[0\] amount: usize #param_/);
assert.match(queryFnHandlesNoHelpText, /stmt\[0\] Return #stmt_/);
assert.doesNotMatch(queryFnHandlesNoHelpText, /patch help:/, "--handles --no-help keeps handles without the operation footer");
assert(queryFnText.length < queryFnHandlesText.length, "default --fn output must be smaller than the --handles report");
const queryMissingPathInput = zero(["query", "no-such-dir/no-such-input"], { allowFailure: true });
assert.notEqual(queryMissingPathInput.code, 0, "path-shaped query arguments must keep file input semantics");
const viewFunction = zero(["view", "--fn", "dealsTotal", queryScopeRoot]).stdout;
assert.match(viewFunction, /^fn dealsTotal\(amount: usize\) -> usize \{\n/);
assert.match(viewFunction, /return amount \+ 1/);
assert.doesNotMatch(viewFunction, /fn main/);
const viewFunctionMiss = zero(["view", "--fn", "dealsTotle", queryScopeRoot], { allowFailure: true });
assert.notEqual(viewFunctionMiss.code, 0);
const viewFunctionMissOutput = `${viewFunctionMiss.stdout}${viewFunctionMiss.stderr}`;
assert.match(viewFunctionMissOutput, /function 'dealsTotle' not found in graph view/);
assert.match(viewFunctionMissOutput, /close matches: dealsTotal/);
assert.match(viewFunctionMissOutput, /zero query --find dealsTotle/);
const viewOutline = zero(["view", "--outline", "main", queryScopeRoot]).stdout;
assert.match(viewOutline, /module main path:src\/main\.0/);
assert.match(viewOutline, /fn dealsTotal\(amount: usize\) -> usize/);
assert.match(viewOutline, /pub fn main\(world: World\) -> Void raises/);
assert.doesNotMatch(viewOutline, /return amount/, "outline must not print function bodies");
assert.equal(zero(["view", "--outline", ".", queryScopeRoot]).stdout, viewOutline);
const viewOutlineMiss = zero(["view", "--outline", "nosuch", queryScopeRoot], { allowFailure: true });
assert.notEqual(viewOutlineMiss.code, 0);
assert.match(`${viewOutlineMiss.stdout}${viewOutlineMiss.stderr}`, /no module matches --outline nosuch/);
assert.match(`${viewOutlineMiss.stdout}${viewOutlineMiss.stderr}`, /modules in this graph: main/);
const viewAround = zero(["view", "--fn", "main", "--around", "total == 42", queryScopeRoot]).stdout;
assert.match(viewAround, /pub fn main\(world: World\) -> Void raises \{/);
assert.match(viewAround, /if total == 42 \{/);
assert.match(viewAround, /query scope/);
assert.match(viewAround, /\n    \.\.\.\n/, "around output elides lines outside the enclosing block");
assert.doesNotMatch(viewAround, /let total/, "around output keeps only the enclosing block");
const viewAroundNoFn = zero(["view", "--around", "total", queryScopeRoot], { allowFailure: true });
assert.notEqual(viewAroundNoFn.code, 0);
assert.match(`${viewAroundNoFn.stdout}${viewAroundNoFn.stderr}`, /--around requires --fn <name>/);
const viewAroundMiss = zero(["view", "--fn", "main", "--around", "zzz-not-here", queryScopeRoot], { allowFailure: true });
assert.notEqual(viewAroundMiss.code, 0);
assert.match(`${viewAroundMiss.stdout}${viewAroundMiss.stderr}`, /text 'zzz-not-here' not found in function 'main'/);
const diffFunction = zero(["diff", "--fn", "dealsTotal", queryScopeRoot]).stdout;
assert.equal(diffFunction, viewFunction);
const diffFunctionMiss = zero(["diff", "--fn", "dealsTotle", queryScopeRoot], { allowFailure: true });
assert.notEqual(diffFunctionMiss.code, 0);
assert.match(`${diffFunctionMiss.stdout}${diffFunctionMiss.stderr}`, /close matches: dealsTotal/);
const diffHelp = zero(["diff", "--help"]).stdout;
assert.match(diffHelp, /Usage: zero diff \[--fn <name>\] \[graph-input\]/);
assert.match(diffHelp, /zero diff --fn handleLine \./);
assert.doesNotMatch(diffHelp, /Patch usage:/);
const queryHelp = zero(["query", "--help"]).stdout;
assert.match(queryHelp, /Usage: zero query .*\[--node <id>\] \[--depth <n>\] \[--full\] \[--handles\] \[--no-help\] \[graph-input\|name\]/);
assert.match(queryHelp, /--handles adds stmt and param patch handles/);
assert.match(queryHelp, /--no-help suppresses the long patch-operation footer/);
assert.match(queryHelp, /zero query userTotals/);
assert.match(queryHelp, /zero query --json --node '#decl_12ab34cd' --depth 2/);
assert.match(queryHelp, /zero view --fn <name>/);
const reconcileHelp = zero(["reconcile", "--help"]).stdout;
assert.match(reconcileHelp, /Usage: zero reconcile \[--json\] <base-graph-input> --source/);
assert.match(reconcileHelp, /zero reconcile zero\.graph --source src\/main\.0/);
assert.match(reconcileHelp, /zero reconcile --json \. --source src\/main\.0/);
assert.match(reconcileHelp, /zero reconcile --json baseline\.graph --source \./);
const viewHelp = zero(["view", "--help"]).stdout;
assert.match(viewHelp, /Usage: zero view \[--json\] \[--fn <name> \[--around <text>\|--handles\]\] \[--outline <module-or-file>\] \[--out <file\.0>\] \[graph-input\]/);
assert.match(viewHelp, /zero view --fn handleLine \./);
assert.match(viewHelp, /--outline <module-or-file> prints function signatures/);
assert.match(viewHelp, /--around <text> prints only the enclosing block/);
assert.match(viewHelp, /close matches/);
// --- think-in-graph addressing: zero view --fn <name> --handles ---
const viewPlainMain = zero(["view", "--fn", "main", queryScopeRoot]).stdout;
const viewHandlesMain = zero(["view", "--fn", "main", "--handles", queryScopeRoot]).stdout;
assert.equal(viewHandlesMain.replace(/  \/\/ [^\n]*$/gm, ""), viewPlainMain, "--handles must only add margin comments");
assert.match(viewHandlesMain, /let total: usize = dealsTotal\(41\)  \/\/ #\S+\n/, "every statement line gains a trailing handle comment");
assert.match(viewHandlesMain, /if total == 42 \{  \/\/ #\S+ #\S+\n/, "compound headers show the stmt handle and the clause block handle");
assert(viewHandlesMain.length <= viewPlainMain.length * 1.3, `--handles annotation overhead must stay under 30%, got ${Math.round((viewHandlesMain.length / viewPlainMain.length - 1) * 1000) / 10}%`);
const viewHandlesNoFn = zero(["view", "--handles", queryScopeRoot], { allowFailure: true });
assert.notEqual(viewHandlesNoFn.code, 0);
assert.match(`${viewHandlesNoFn.stdout}${viewHandlesNoFn.stderr}`, /--handles requires --fn <name>/);

// refs and find rows gain the enclosing statement handle with --handles
const refsHandlesText = execFileSync(zeroBin, ["query", "--refs", "dealsTotal", "--handles"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer });
assert.match(refsHandlesText, /call #expr_\S+ fn:main stmt:#stmt_\S+/, "--refs --handles rows show the enclosing statement handle");
const refsPlainText = execFileSync(zeroBin, ["query", "--refs", "dealsTotal"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer });
assert.doesNotMatch(refsPlainText, / stmt:#/, "--refs without --handles stays unchanged");
const refsHandlesJson = json(["query", "--json", "--refs", "dealsTotal", queryScopeRoot]).body;
assert(refsHandlesJson.references.some((ref: any) => typeof ref.stmt === "string" && ref.stmt.startsWith("#stmt_")), "refs JSON rows carry the enclosing statement handle");
const findHandlesText = execFileSync(zeroBin, ["query", "--find", "dealsTotal", "--handles"], { cwd: queryScopeRoot, encoding: "utf8", maxBuffer: execMaxBuffer });
assert.match(findHandlesText, /Identifier #expr_\S+ name:dealsTotal stmt:#stmt_\S+/, "--find --handles match rows show the enclosing statement handle");

// the printed handles are the identifiers zero patch accepts: set-by-handle round trip
const letHandle = viewHandlesMain.match(/let total[^\n]*  \/\/ (#\S+)$/m)?.[1];
assert(letHandle, "let statement line carries a handle");
const letNode = json(["query", "--json", "--node", letHandle, "--depth", "2", queryScopeRoot]).body;
assert.equal(letNode.node.selected.kind, "Let", "short handles resolve in zero query --node");
const letCall = letNode.node.children.find((edge: any) => edge.kind === "expr");
const letLiteralId = letCall?.children?.find((edge: any) => edge.kind === "arg")?.node?.id;
assert(letLiteralId, "literal argument node is reachable from the statement handle");
const setByHandle = zero(["patch", queryScopeRoot, "--op", `set node="${letLiteralId}" field="value" expect="41" value="43"`]);
assert.equal(setByHandle.code, 0);
const viewAfterSet = zero(["view", "--fn", "main", "--handles", queryScopeRoot]).stdout;
assert.match(viewAfterSet, /let total: usize = dealsTotal\(43\)  \/\/ #\S+\n/, "set-by-handle edit shows up in the handles view");

// clause block handles drive replaceBlockBody directly from the view output
const ifClause = viewAfterSet.match(/if total == 42 \{  \/\/ (#\S+) (#\S+)$/m);
assert(ifClause, "if header carries stmt and clause block handles");
const clausePatch = zero(["patch", queryScopeRoot, "--patch-text", `zero-program-graph-patch v1\nreplaceBlockBody ${ifClause[2]}\n  check world.out.write "clause by handle\\n"\nend\n`]);
assert.equal(clausePatch.code, 0);
assert.match(zero(["view", "--fn", "main", queryScopeRoot]).stdout, /clause by handle/, "replaceBlockBody accepts the clause handle printed by --handles");

// replaceExpr: one-line micro edits by handle (value, condition, subexpression)
const viewDealsHandles = zero(["view", "--fn", "dealsTotal", "--handles", queryScopeRoot]).stdout;
const returnHandle = viewDealsHandles.match(/return amount \+ 1  \/\/ (#\S+)$/m)?.[1];
assert(returnHandle, "return statement carries a handle");
const exprBadText = zero(["patch", queryScopeRoot, "--op", `replaceExpr node="${returnHandle}" with="amount ) 2"`], { allowFailure: true });
assert.notEqual(exprBadText.code, 0);
assert.match(exprBadText.stderr, /replaceExpr text did not parse as a Zero expression/);
const exprEdit = zero(["patch", queryScopeRoot, "--op", `replaceExpr node="${returnHandle}" with="amount * 2 + 1"`]);
assert.equal(exprEdit.code, 0);
assert.match(zero(["view", "--fn", "dealsTotal", queryScopeRoot]).stdout, /return amount \* 2 \+ 1/, "replaceExpr on a statement handle replaces its expression");

// micro-op batch: three --op edits, one patch, one revalidation pass
const viewMainBatch = zero(["view", "--fn", "main", "--handles", queryScopeRoot]).stdout;
const batchLetHandle = viewMainBatch.match(/let total[^\n]*  \/\/ (#\S+)$/m)?.[1];
const batchIfHandle = viewMainBatch.match(/if total == 42 \{  \/\/ (#\S+) #\S+$/m)?.[1];
assert(batchLetHandle && batchIfHandle, "main handles for batch edit");
const batchPatch = zero(["patch", queryScopeRoot,
  "--op", `replaceExpr node="${batchLetHandle}" with="dealsTotal(20) + 1"`,
  "--op", `replaceExpr node="${batchIfHandle}" with="total != 0"`,
  "--op", `set node="${batchLetHandle}" field="mutable" value="true"`,
]);
assert.equal(batchPatch.code, 0);
assert.match(batchPatch.stdout, /applied: 3 ops/, "batched --op edits apply as one patch");
const viewMainAfterBatch = zero(["view", "--fn", "main", queryScopeRoot]).stdout;
assert.match(viewMainAfterBatch, /var total: usize = dealsTotal\(20\) \+ 1/);
assert.match(viewMainAfterBatch, /if total != 0 \{/);

// structural rewrite by example: zero patch --rewrite '<pattern>' --to '<template>'
const rewriteRoot = join("/tmp", `zero-rewrite-scope-${process.pid}`);
rmSync(rewriteRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", rewriteRoot]).body.ok, true);
mkdirSync(join(rewriteRoot, "src"), { recursive: true });
writeFileSync(
  join(rewriteRoot, "src", "main.0"),
  'fn calc(a: usize) -> usize {\n    let x: usize = a + 1\n    let y: usize = a + 1\n    return x + y\n}\n\nfn calc2(b: usize) -> usize {\n    return b + 1\n}\n\npub fn main(world: World) -> Void raises {\n    let t: usize = calc(1) + calc2(2)\n    if t == t {\n        check world.out.write("rewrite scope\\n")\n    }\n}\n',
);
assert.equal(json(["import", "--json", join(rewriteRoot, "src", "main.0")]).body.ok, true);

// dry run is the default: each site lists file, fn, handle, and rendered before/after
const rewriteDry = zero(["patch", rewriteRoot, "--rewrite", "$A + 1", "--to", "$A + 2"]);
assert.equal(rewriteDry.code, 0);
assert.match(rewriteDry.stdout, /rewrite: \$A \+ 1 -> \$A \+ 2/);
assert.match(rewriteDry.stdout, /matches: 3/, "package-wide rewrite matches all three sites");
assert.match(rewriteDry.stdout, /src\/main\.0 fn:calc #\S+/);
assert.match(rewriteDry.stdout, /- a \+ 1/);
assert.match(rewriteDry.stdout, /\+ a \+ 2/);
assert.match(rewriteDry.stdout, /- b \+ 1/);
assert.match(rewriteDry.stdout, /dry run: pass --apply/);
assert.equal(zero(["view", "--fn", "calc", rewriteRoot]).stdout.includes("a + 2"), false, "dry run leaves the store untouched");

// --fn scopes matching to one function
const rewriteScoped = zero(["patch", rewriteRoot, "--rewrite", "$A + 1", "--to", "$A + 2", "--fn", "calc"]);
assert.match(rewriteScoped.stdout, /matches: 2/, "--fn calc scopes the rewrite to two sites");
assert.doesNotMatch(rewriteScoped.stdout, /fn:calc2/);

// the same metavariable twice must match equal subtrees
const rewriteEquality = zero(["patch", rewriteRoot, "--rewrite", "$A == $A", "--to", "true"]);
assert.match(rewriteEquality.stdout, /matches: 1/, "$A == $A matches t == t and nothing else");
assert.match(rewriteEquality.stdout, /- t == t/);

// JSON dry run carries the match list
const rewriteJson = json(["patch", "--json", rewriteRoot, "--rewrite", "$A + 1", "--to", "$A + 2"]).body;
assert.equal(rewriteJson.ok, true);
assert.equal(rewriteJson.matchCount, 3);
assert.equal(rewriteJson.matches.length, 3);
assert.equal(rewriteJson.matches[0].before, "a + 1");
assert.equal(rewriteJson.matches[0].after, "a + 2");
assert.equal(rewriteJson.rewrite.apply, false);

// revalidation failure rolls the whole batch back
const rewriteRollback = zero(["patch", rewriteRoot, "--rewrite", "$A + 1", "--to", "$A + nosuchconst", "--apply"], { allowFailure: true });
assert.notEqual(rewriteRollback.code, 0);
assert.match(`${rewriteRollback.stdout}${rewriteRollback.stderr}`, /revalidation/);
assert.equal(zero(["view", "--fn", "calc", rewriteRoot]).stdout.includes("nosuchconst"), false, "failed rewrite leaves the store untouched");

// metavar capture reuse on both sides, applied in one batch
const rewriteApply = zero(["patch", rewriteRoot, "--rewrite", "$A + 1", "--to", "$A + 1 + ($A - $A)", "--apply"]);
assert.equal(rewriteApply.code, 0);
assert.match(rewriteApply.stdout, /matches: 3/);
assert.match(rewriteApply.stdout, /applied: 3 ops/, "apply rewrites every site in one batch with one revalidation");
const rewriteCalcView = zero(["view", "--fn", "calc", rewriteRoot]).stdout;
assert.match(rewriteCalcView, /let x: usize = a \+ 1 \+ \(a - a\)/, "applied rewrite matches the dry-run rendering");
assert.match(rewriteCalcView, /let y: usize = a \+ 1 \+ \(a - a\)/);
assert.match(zero(["view", "--fn", "calc2", rewriteRoot]).stdout, /return b \+ 1 \+ \(b - b\)/);

// rewrite needs both halves and a real function for --fn
const rewriteMissingTo = zero(["patch", rewriteRoot, "--rewrite", "$A + 1"], { allowFailure: true });
assert.notEqual(rewriteMissingTo.code, 0);
assert.match(rewriteMissingTo.stderr, /graph rewrite needs both --rewrite and --to/);
const rewriteBadFn = zero(["patch", rewriteRoot, "--rewrite", "$A + 1", "--to", "$A", "--fn", "nosuchfn"], { allowFailure: true });
assert.notEqual(rewriteBadFn.code, 0);
assert.match(rewriteBadFn.stderr, /rewrite --fn function 'nosuchfn' was not found/);
const rewriteUnboundVar = zero(["patch", rewriteRoot, "--rewrite", "$A + 1", "--to", "$B + 1"], { allowFailure: true });
assert.notEqual(rewriteUnboundVar.code, 0);
assert.match(rewriteUnboundVar.stderr, /template uses \$B which the pattern does not bind/);
rmSync(rewriteRoot, { force: true, recursive: true });

// GPH004 with a near-miss handle names the nearest existing handle
const nearMiss = `${letHandle.slice(0, -1)}${letHandle.endsWith("z") ? "y" : "z"}`;
const nearMissPatch = zero(["patch", queryScopeRoot, "--op", `set node="${nearMiss}" field="value" value="9"`], { allowFailure: true });
assert.notEqual(nearMissPatch.code, 0);
assert.match(nearMissPatch.stderr, /patch node was not found; nearest: #/, "GPH004 suggests the nearest handle");

rmSync(queryScopeRoot, { force: true, recursive: true });
const repoGraphStatus = json(["status", "--json", "."]).body;
assert.equal(repoGraphStatus.ok, true);
assert.equal(repoGraphStatus.mode, "status");
assert.equal(repoGraphStatus.writes, false);
assert.equal(repoGraphStatus.repositoryGraph.storePath, "./zero.graph");
assert.equal(repoGraphStatus.repositoryGraph.storePresent, false);
assert.equal(repoGraphStatus.repositoryGraph.storeValid, false);
assert.equal(repoGraphStatus.repositoryGraph.enabled, true);
assert.equal(repoGraphStatus.repositoryGraph.projectionState, "store-missing");
assert.equal(repoGraphStatus.repositoryGraph.semanticValidity, "unavailable");
assert.equal(repoGraphStatus.repositoryGraph.projectionValidity, "unavailable");
assert.equal(repoGraphStatus.contract.artifact, "zero.graph");
assert.equal(repoGraphStatus.contract.compilerInput, "manifest package plus checked-in zero.graph repository store");
assert.equal(repoGraphStatus.contract.commands.status.writes, false);
assert.equal(repoGraphStatus.contract.commands.verifyProjection.writes, false);
assert.equal(repoGraphStatus.contract.commands.verifyProjection.available, false);
assert.equal(repoGraphStatus.contract.commands.import.writes, true);
assert.equal(repoGraphStatus.contract.commands.import.available, true);
assert.equal(repoGraphStatus.contract.commands.export.writes, true);
assert.equal(repoGraphStatus.contract.commands.export.available, false);
assert.equal(repoGraphStatus.contract.commands.merge.writes, true);
assert.equal(repoGraphStatus.contract.commands.merge.available, true);
assert.deepEqual(repoGraphStatus.contract.commands.merge.requires, ["--base", "--left", "--right"]);
assert.equal(repoGraphStatus.storage.encoding, "single-file-binary");
assert.equal(repoGraphStatus.storage.defaultEncoding, "binary");
assert.equal(repoGraphStatus.storage.interface, "ProgramGraphStore");
assert.equal(repoGraphStatus.storage.evolvable, true);
assert.equal(repoGraphStatus.scale.nodes, 0);
assert.equal(repoGraphStatus.scale.edges, 0);
assert.deepEqual(repoGraphStatus.repairCommands, []);
const invalidRepoGraphCompilerInputRoot = join("/tmp", `zero-repo-graph-invalid-compiler-input-${process.pid}`);
rmSync(invalidRepoGraphCompilerInputRoot, { force: true, recursive: true });
mkdirSync(invalidRepoGraphCompilerInputRoot, { recursive: true });
writeFileSync(join(invalidRepoGraphCompilerInputRoot, "zero.toml"), `[package]
name = "invalid-repo-graph-compiler-input"
version = "0.1.0"

[targets.cli]
kind = "exe"
main = "main.0"

[repositoryGraph]
compilerInput = "true"
`);
const invalidRepoGraphCompilerInputStatus = json(["status", "--json", invalidRepoGraphCompilerInputRoot], { allowFailure: true });
assert.notEqual(invalidRepoGraphCompilerInputStatus.code, 0);
assert.equal(invalidRepoGraphCompilerInputStatus.body.ok, false);
assert.equal(invalidRepoGraphCompilerInputStatus.body.repositoryGraph.compilerInput, "invalid");
assert.equal(invalidRepoGraphCompilerInputStatus.body.diagnostics[0].code, "BLD002");
assert.equal(invalidRepoGraphCompilerInputStatus.body.diagnostics[0].path, join(invalidRepoGraphCompilerInputRoot, "zero.toml"));
assert.equal(invalidRepoGraphCompilerInputStatus.body.diagnostics[0].message, "repositoryGraph.compilerInput must be a boolean");
const standaloneRepoGraphRoot = join("/tmp", `zero-repo-graph-contract-${process.pid}`);
const standaloneRepoGraphSource = join(standaloneRepoGraphRoot, "standalone.0");
const standaloneRepoGraphStore = join(standaloneRepoGraphRoot, "zero.graph");
rmSync(standaloneRepoGraphRoot, { force: true, recursive: true });
mkdirSync(standaloneRepoGraphRoot, { recursive: true });
const standaloneRepoGraphComment = "// repository graph projection comment\n";
writeFileSync(standaloneRepoGraphSource, standaloneRepoGraphComment + readFileSync("examples/hello.0", "utf8"));
const standaloneRepoGraphStatus = json(["status", "--json", resolve(standaloneRepoGraphSource)]).body;
assert.equal(standaloneRepoGraphStatus.repositoryGraph.root, resolve(standaloneRepoGraphRoot));
assert.equal(standaloneRepoGraphStatus.repositoryGraph.storePath, standaloneRepoGraphStore);
const nestedRelativeRepoGraphStatus = JSON.parse(execFileSync(zeroBin, ["status", "--json", "main.0"], { cwd: "examples/systems-package/src", encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"] }));
assert.equal(nestedRelativeRepoGraphStatus.repositoryGraph.root, "..");
assert.equal(nestedRelativeRepoGraphStatus.repositoryGraph.storePath, "../zero.graph");
writeFileSync(standaloneRepoGraphStore, "");
const standaloneRepoGraphInvalidStatus = json(["status", "--json", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(standaloneRepoGraphInvalidStatus.code, 0);
assert.equal(standaloneRepoGraphInvalidStatus.body.repositoryGraph.storePresent, true);
assert.equal(standaloneRepoGraphInvalidStatus.body.repositoryGraph.storeValid, false);
assert.equal(standaloneRepoGraphInvalidStatus.body.repositoryGraph.projectionState, "store-invalid");
assert.equal(standaloneRepoGraphInvalidStatus.body.diagnostics[0].code, "RGP003");
rmSync(standaloneRepoGraphStore, { force: true });
const repoGraphImport = json(["import", "--format", "text", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(repoGraphImport.code, 0);
assert.equal(repoGraphImport.body.ok, true);
assert.equal(repoGraphImport.body.mode, "import");
assert.equal(repoGraphImport.body.writes, true);
assert.equal(repoGraphImport.body.repositoryGraph.storePresent, true);
assert.equal(repoGraphImport.body.repositoryGraph.storeValid, true);
assert.equal(repoGraphImport.body.repositoryGraph.enabled, true);
assert.equal(repoGraphImport.body.repositoryGraph.projectionState, "clean");
assert.equal(repoGraphImport.body.repositoryGraph.semanticValidity, "shape-valid");
assert.equal(repoGraphImport.body.repositoryGraph.projectionValidity, "clean");
assert.equal(repoGraphImport.body.compilerStore.shape, "compiler-oriented-tables");
assert.equal(repoGraphImport.body.compilerStore.sourceFreeInspection, true);
assert.equal(repoGraphImport.body.compilerStore.sourceProjectionRequiredForSemanticFacts, false);
assert.equal(repoGraphImport.body.compilerStore.semanticValidity.state, "shape-valid");
assert.equal(repoGraphImport.body.compilerStore.projectionValidity.state, "clean");
assert.equal(repoGraphImport.body.compilerStore.tables.node, repoGraphImport.body.store.nodes);
assert.deepEqual(repoGraphImport.body.compilerStore.hashInputs.graphHashExcludes, ["sourcePath", "line", "column", "projectionText"]);
assert(repoGraphImport.body.repositoryGraph.possibleProjectionStates.includes("source-stale"));
assert(repoGraphImport.body.repositoryGraph.possibleProjectionStates.includes("conflict"));
assert.equal(repoGraphImport.body.contract.commands.export.available, true);
assert.equal(repoGraphImport.body.changedPaths[0], standaloneRepoGraphStore);
assert(existsSync(standaloneRepoGraphStore));
const storeText = readFileSync(standaloneRepoGraphStore, "utf8");
assert.match(storeText, /^zero-repository-graph v1\n/);
assert.match(storeText, /^sourceProjection "\.0"$/m);
assert.match(storeText, /^compilerStore schemaVersion:1 shape:"compiler-oriented-tables"/m);
assert.match(storeText, /^compilerTables schema:1 package:/m);
assert.match(storeText, /^compilerHashInputs graphHashExcludes:"source-path,line,column,projection-text"/m);
assert.match(storeText, /^source path:"standalone\.0"$/m);
assert.match(storeText, /^projection path:"standalone\.0" text:"\/\/ repository graph projection comment\\n/m);
assert.match(storeText, /^nodeHash node:"#/m);
assert.match(storeText, /^graph\nzero-graph v1\n/m);
assert(!storeText.includes(standaloneRepoGraphRoot));
const firstStoreHash = sha256File(standaloneRepoGraphStore);
writeFileSync(standaloneRepoGraphStore, storeText.replace('sourceProjection ".0"\n', 'sourceProjection ".0"\n\n'));
const standaloneRepoGraphNonCanonicalStatus = json(["status", "--json", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(standaloneRepoGraphNonCanonicalStatus.code, 0);
assert.equal(standaloneRepoGraphNonCanonicalStatus.body.repositoryGraph.storePresent, true);
assert.equal(standaloneRepoGraphNonCanonicalStatus.body.repositoryGraph.storeValid, false);
assert.equal(standaloneRepoGraphNonCanonicalStatus.body.diagnostics[0].code, "RGP003");
assert.match(standaloneRepoGraphNonCanonicalStatus.body.diagnostics[0].actual, /byte-stable/);
writeFileSync(standaloneRepoGraphStore, storeText);
const standaloneRepoGraphStatusWithStore = json(["status", "--json", resolve(standaloneRepoGraphSource)]).body;
assert.equal(standaloneRepoGraphStatusWithStore.repositoryGraph.storePresent, true);
assert.equal(standaloneRepoGraphStatusWithStore.repositoryGraph.storeValid, true);
assert.equal(standaloneRepoGraphStatusWithStore.repositoryGraph.enabled, true);
assert.equal(standaloneRepoGraphStatusWithStore.repositoryGraph.projectionState, "clean");
assert.equal(standaloneRepoGraphStatusWithStore.repositoryGraph.semanticValidity, "shape-valid");
assert.equal(standaloneRepoGraphStatusWithStore.repositoryGraph.projectionValidity, "clean");
assert.equal(standaloneRepoGraphStatusWithStore.store.format, "zero-repository-graph");
assert.equal(standaloneRepoGraphStatusWithStore.store.encoding, "text");
assert.equal(standaloneRepoGraphStatusWithStore.store.graphHash, repoGraphImport.body.store.graphHash);
assert.equal(standaloneRepoGraphStatusWithStore.compilerStore.tables.sourceMap, standaloneRepoGraphStatusWithStore.store.nodes);
const binaryRepoGraphRoot = join("/tmp", `zero-repo-graph-binary-contract-${process.pid}`);
rmSync(binaryRepoGraphRoot, { force: true, recursive: true });
mkdirSync(binaryRepoGraphRoot, { recursive: true });
assert.match(zero(["init", binaryRepoGraphRoot]).stdout, /graph project init ok/);
let binaryRepoGraphStore = readFileSync(join(binaryRepoGraphRoot, "zero.graph"));
assert.equal(binaryRepoGraphStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");
assertPatchOkOutput(zero(["patch", binaryRepoGraphRoot, "--op", "addMain", "--op", 'addCheckWrite fn="main" text="hello binary\\n"']).stdout, join(binaryRepoGraphRoot, "zero.graph"));
binaryRepoGraphStore = readFileSync(join(binaryRepoGraphRoot, "zero.graph"));
assert.equal(binaryRepoGraphStore.subarray(0, 8).toString("binary"), "ZRGBIN1\0");
const binaryRepoGraphStatus = json(["status", "--json", binaryRepoGraphRoot]).body;
assert.equal(binaryRepoGraphStatus.repositoryGraph.storeValid, true);
assert.equal(binaryRepoGraphStatus.store.format, "zero-repository-graph");
assert.equal(binaryRepoGraphStatus.store.encoding, "binary");
assert.equal(binaryRepoGraphStatus.storage.encoding, "single-file-binary");
assert.equal(binaryRepoGraphStatus.storage.defaultEncoding, "binary");
assert.equal(binaryRepoGraphStatus.storage.binaryAvailable, true);
assert.equal(zero(["check", binaryRepoGraphRoot]).stdout, "ok\n");
assert.equal(zero(["run", binaryRepoGraphRoot]).stdout, "hello binary\n");
assert.match(zero(["export", binaryRepoGraphRoot]).stdout, /repository graph export ok/);
assert.equal(zero(["verify-projection", binaryRepoGraphRoot]).stdout, "repository graph verify-projection ok\n");
const invalidBinaryFormatSync = zero(["import", "--format", "pickle", binaryRepoGraphRoot], { allowFailure: true });
assert.notEqual(invalidBinaryFormatSync.code, 0);
assert.match(invalidBinaryFormatSync.stderr, /repository graph store format is not supported/);
const standaloneRepoGraphVerifyWithStore = json(["verify-projection", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(standaloneRepoGraphVerifyWithStore.code, 0);
assert.equal(standaloneRepoGraphVerifyWithStore.body.ok, true);
assert.equal(standaloneRepoGraphVerifyWithStore.body.writes, false);
const repositoryGraphVerifyCleanScript = repositoryGraphVerifyScript(standaloneRepoGraphRoot);
assert.equal(repositoryGraphVerifyCleanScript.code, 0);
assert.match(repositoryGraphVerifyCleanScript.stdout, /repository graph verify-projection ok \(1 store\)/);
const repositoryGraphVerifyNoStoresRoot = join("/tmp", `zero-repo-graph-no-stores-${process.pid}`);
rmSync(repositoryGraphVerifyNoStoresRoot, { force: true, recursive: true });
mkdirSync(repositoryGraphVerifyNoStoresRoot, { recursive: true });
const repositoryGraphVerifyNoStoresScript = repositoryGraphVerifyScript(repositoryGraphVerifyNoStoresRoot);
assert.equal(repositoryGraphVerifyNoStoresScript.code, 0);
assert.match(repositoryGraphVerifyNoStoresScript.stdout, /repository graph verify-projection ok \(0 stores\)/);
const repositoryGraphVerifyRedirectRoot = join("/tmp", `zero-repo-graph-redirect-${process.pid}`);
const repositoryGraphVerifyRedirectGood = join(repositoryGraphVerifyRedirectRoot, "good");
const repositoryGraphVerifyRedirectBad = join(repositoryGraphVerifyRedirectRoot, "bad");
rmSync(repositoryGraphVerifyRedirectRoot, { force: true, recursive: true });
mkdirSync(repositoryGraphVerifyRedirectGood, { recursive: true });
mkdirSync(repositoryGraphVerifyRedirectBad, { recursive: true });
writeFileSync(join(repositoryGraphVerifyRedirectGood, "main.0"), readFileSync("examples/hello.0", "utf8"));
json(["import", "--format", "text", "--json", join(repositoryGraphVerifyRedirectGood, "main.0")]);
writeFileSync(join(repositoryGraphVerifyRedirectBad, "zero.graph"), 'invalid repository graph store\nprojection path:"../good/main.0" text:""\n');
const repositoryGraphVerifyRedirectScript = repositoryGraphVerifyScript(repositoryGraphVerifyRedirectRoot, { allowFailure: true });
assert.notEqual(repositoryGraphVerifyRedirectScript.code, 0);
assert.match(repositoryGraphVerifyRedirectScript.stderr, /repository graph verify-projection failed/);
assert.match(repositoryGraphVerifyRedirectScript.stderr, /zero-repo-graph-redirect-[0-9]+\/bad/);
const standaloneRepoGraphVerifyRelative = JSON.parse(execFileSync(zeroBin, ["verify-projection", "--json", "standalone.0"], { cwd: standaloneRepoGraphRoot, encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"] }));
assert.equal(standaloneRepoGraphVerifyRelative.ok, true);
const repoGraphImportAgain = json(["import", "--format", "text", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(repoGraphImportAgain.code, 0);
assert.equal(sha256File(standaloneRepoGraphStore), firstStoreHash);
writeFileSync(standaloneRepoGraphSource, readFileSync(standaloneRepoGraphSource, "utf8").replace("hello from zero", "hello from graph projection"));
const standaloneRepoGraphProjectionStale = json(["status", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(standaloneRepoGraphProjectionStale.body.repositoryGraph.projectionState, "source-stale");
assert.equal(standaloneRepoGraphProjectionStale.body.repositoryGraph.projectionValidity, "stale");
const standaloneRepoGraphExport = json(["export", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(standaloneRepoGraphExport.code, 0);
assert.equal(standaloneRepoGraphExport.body.mode, "export");
assert.equal(standaloneRepoGraphExport.body.writes, true);
assert.deepEqual(standaloneRepoGraphExport.body.changedPaths, [standaloneRepoGraphSource]);
const restoredRepoGraphSource = readFileSync(standaloneRepoGraphSource, "utf8");
assert.match(restoredRepoGraphSource, /hello from zero/);
assert.equal(restoredRepoGraphSource.startsWith(standaloneRepoGraphComment), true);
const standaloneRepoGraphExportAgain = json(["export", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(standaloneRepoGraphExportAgain.code, 0);
assert.deepEqual(standaloneRepoGraphExportAgain.body.changedPaths, []);
const standaloneRepoGraphVerifyAfterProjection = json(["verify-projection", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(standaloneRepoGraphVerifyAfterProjection.body.ok, true);
const standaloneRepoGraphSourceMapAfterProjection = json(["source-map", "--json", resolve(standaloneRepoGraphSource)]).body;
assert.equal(standaloneRepoGraphSourceMapAfterProjection.ok, true);
assert(standaloneRepoGraphSourceMapAfterProjection.files.some((file: any) => file.path.endsWith("standalone.0") && file.nodeCount > 0));
assert(standaloneRepoGraphSourceMapAfterProjection.mappings.some((mapping: any) => mapping.sourceAvailable === false && mapping.sourceRange?.path.endsWith("standalone.0")));
const invalidProjectionText = readFileSync(standaloneRepoGraphSource, "utf8").replace("hello from zero", "projection row changed graph");
writeFileSync(standaloneRepoGraphStore, storeText.replace(/^projection path:"standalone\.0" text:.*$/m, `projection path:"standalone.0" text:${JSON.stringify(invalidProjectionText)}`));
const invalidProjectionStatus = json(["status", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(invalidProjectionStatus.body.repositoryGraph.projectionState, "conflict");
const invalidProjectionVerify = json(["verify-projection", "--json", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(invalidProjectionVerify.code, 0);
assert.equal(invalidProjectionVerify.body.diagnostics[0].code, "RGP006");
assert.deepEqual(invalidProjectionVerify.body.repairCommands, [`zero import ${resolve(standaloneRepoGraphSource)}`, `zero export ${resolve(standaloneRepoGraphSource)}`]);
const invalidProjectionVerifyText = zero(["verify-projection", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(invalidProjectionVerifyText.code, 0);
assert.match(invalidProjectionVerifyText.stderr, new RegExp(`repair: zero import ${resolve(standaloneRepoGraphSource).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
assert.match(invalidProjectionVerifyText.stderr, new RegExp(`repair: zero export ${resolve(standaloneRepoGraphSource).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
const invalidProjectionSync = json(["export", "--json", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(invalidProjectionSync.code, 0);
assert.equal(invalidProjectionSync.body.diagnostics[0].code, "RGP004");
assert(!readFileSync(standaloneRepoGraphSource, "utf8").includes("projection row changed graph"));
writeFileSync(standaloneRepoGraphStore, storeText);
const graphMarker = "\ngraph\n";
const standaloneRepoGraphGraphStart = storeText.indexOf(graphMarker);
assert.notEqual(standaloneRepoGraphGraphStart, -1);
const standaloneRepoGraphBaseArtifact = join(standaloneRepoGraphRoot, "base.program-graph");
const standaloneRepoGraphPatchedArtifact = join(standaloneRepoGraphRoot, "graph-only-drift.program-graph");
const standaloneRepoGraphBaseGraphText = storeText.slice(standaloneRepoGraphGraphStart + graphMarker.length);
writeFileSync(standaloneRepoGraphBaseArtifact, standaloneRepoGraphBaseGraphText);
const standaloneRepoGraphModule = standaloneRepoGraphBaseGraphText.match(/^node (#[^ ]+) Module /m)?.[1];
assert(standaloneRepoGraphModule);
const voidTypeId = graphDomainId("type", graphHashText(1469598103934665603n, "Void"));
const graphOnlyDriftPatch = `insertEdge from="${standaloneRepoGraphModule}" to="${voidTypeId}" edge="projectionDrift" target="type" order="0"`;
const graphOnlyDriftPatchResult = json(["patch", "--json", "--out", standaloneRepoGraphPatchedArtifact, standaloneRepoGraphBaseArtifact, "--op", graphOnlyDriftPatch]);
assert.equal(graphOnlyDriftPatchResult.body.ok, true);
const standaloneRepoGraphPatchedGraphText = readFileSync(standaloneRepoGraphPatchedArtifact, "utf8");
const standaloneRepoGraphPatchedHash = standaloneRepoGraphPatchedGraphText.match(/^hash "([^"]+)"$/m)?.[1];
assert(standaloneRepoGraphPatchedHash);
const graphOnlyDriftStoreText = refreshRepositoryGraphCompilerMetadata(storeText
  .slice(0, standaloneRepoGraphGraphStart + graphMarker.length)
  .replace(/^graphHash "[^"]+"$/m, `graphHash "${standaloneRepoGraphPatchedHash}"`)
  .replace(/^moduleHash "[^"]+"$/m, `moduleHash "${standaloneRepoGraphPatchedHash}"`) +
  standaloneRepoGraphPatchedGraphText);
writeFileSync(standaloneRepoGraphStore, graphOnlyDriftStoreText);
const graphOnlyDriftStatus = json(["status", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(graphOnlyDriftStatus.body.repositoryGraph.projectionState, "conflict");
const graphOnlyDriftSync = json(["export", "--json", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(graphOnlyDriftSync.code, 0);
assert.equal(graphOnlyDriftSync.body.diagnostics[0].code, "RGP004");
writeFileSync(standaloneRepoGraphStore, storeText);
const typeErasedBaseLiteralLine = standaloneRepoGraphBaseGraphText.match(/^node (#[^ ]+) Literal [^\n]*value:"hello from zero[^\n]*$/m);
assert(typeErasedBaseLiteralLine);
assert(typeErasedBaseLiteralLine[0].includes(' type:"String"'));
const typeErasedLiteralText = typeErasedBaseLiteralLine[0].replace(' type:"String"', "");
const typeErasedGraphText = standaloneRepoGraphBaseGraphText.replace(typeErasedBaseLiteralLine[0], typeErasedLiteralText);
assert.notEqual(typeErasedGraphText, standaloneRepoGraphBaseGraphText);
const typeErasedLiteralLine = typeErasedGraphText.match(/^node (#[^ ]+) Literal [^\n]*value:"hello from zero\\n"[^\n]*$/m);
assert(typeErasedLiteralLine);
const typeErasedGraphHash = recomputeGraphHash(typeErasedGraphText);
const typeErasedStoredGraphText = typeErasedGraphText.replace(/^hash "graph:[0-9a-f]{16}"$/m, `hash "${typeErasedGraphHash}"`);
const typeErasedStoreText = refreshRepositoryGraphCompilerMetadata(storeText
  .slice(0, standaloneRepoGraphGraphStart + graphMarker.length)
  .replace(/^graphHash "[^"]+"$/m, `graphHash "${typeErasedGraphHash}"`)
  .replace(/^moduleHash "[^"]+"$/m, `moduleHash "${typeErasedGraphHash}"`)
  .replace(
    new RegExp(`^nodeHash node:${JSON.stringify(typeErasedLiteralLine[1])} hash:"[^"]+"$`, "m"),
    `nodeHash node:${JSON.stringify(typeErasedLiteralLine[1])} hash:${JSON.stringify(graphNodeHash(typeErasedLiteralLine[0]))}`,
  ) +
  typeErasedStoredGraphText);
writeFileSync(standaloneRepoGraphStore, typeErasedStoreText);
const typeErasedProjectionStatus = json(["status", "--json", resolve(standaloneRepoGraphSource)]);
assert.equal(typeErasedProjectionStatus.body.repositoryGraph.projectionState, "conflict");
const typeErasedProjectionSync = json(["export", "--json", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(typeErasedProjectionSync.code, 0);
assert.equal(typeErasedProjectionSync.body.diagnostics[0].code, "RGP004");
writeFileSync(standaloneRepoGraphStore, storeText);
const relativePackageRoot = join("/tmp", `zero-repo-graph-relative-package-${process.pid}`);
const relativePackageSource = join(relativePackageRoot, "src", "main.0");
const relativePackageStore = join(relativePackageRoot, "zero.graph");
rmSync(relativePackageRoot, { force: true, recursive: true });
mkdirSync(join(relativePackageRoot, "src"), { recursive: true });
writeFileSync(join(relativePackageRoot, "zero.toml"), readFileSync("examples/systems-package/zero.toml", "utf8"));
writeFileSync(relativePackageSource, readFileSync("examples/hello.0", "utf8"));
json(["import", "--format", "text", "--json", relativePackageRoot]);
const relativePackageStoreText = readFileSync(relativePackageStore, "utf8");
assert.match(relativePackageStoreText, /^source path:"src\/main\.0"$/m);
assert(!relativePackageStoreText.includes(relativePackageRoot));
const relativePackageStoreHash = sha256File(relativePackageStore);
const relativePackageVerifyDot = JSON.parse(execFileSync(zeroBin, ["verify-projection", "--json", "."], { cwd: relativePackageRoot, encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"] }));
assert.equal(relativePackageVerifyDot.ok, true);
const relativePackageSyncDot = JSON.parse(execFileSync(zeroBin, ["import", "--format", "text", "--json", "."], { cwd: relativePackageRoot, encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"] }));
assert.equal(relativePackageSyncDot.ok, true);
assert.equal(sha256File(relativePackageStore), relativePackageStoreHash);
const relativePackageVerifyAbsolute = json(["verify-projection", "--json", relativePackageRoot]);
assert.equal(relativePackageVerifyAbsolute.body.ok, true);
const relativePackageManifest = join(relativePackageRoot, "zero.toml");
writeFileSync(relativePackageManifest, manifestToml({ package: { name: "systems-package-renamed", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "src/main.0" } } }));
const relativePackageManifestStatus = json(["status", "--json", relativePackageRoot]);
assert.equal(relativePackageManifestStatus.body.repositoryGraph.projectionState, "conflict");
const relativePackageManifestVerify = json(["verify-projection", "--json", relativePackageRoot], { allowFailure: true });
assert.notEqual(relativePackageManifestVerify.code, 0);
assert.equal(relativePackageManifestVerify.body.repositoryGraph.projectionState, "conflict");
assert.equal(relativePackageManifestVerify.body.diagnostics[0].code, "RGP007");
const relativePackageManifestExport = json(["export", "--json", relativePackageRoot], { allowFailure: true });
assert.notEqual(relativePackageManifestExport.code, 0);
assert.equal(relativePackageManifestExport.body.repositoryGraph.projectionState, "conflict");
assert.equal(relativePackageManifestExport.body.diagnostics[0].code, "RGP007");
writeFileSync(relativePackageManifest, manifestToml({ package: { name: "systems-package", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "src/missing.0" } } }));
const relativePackageBrokenManifestStatus = json(["status", "--json", relativePackageRoot]);
assert.equal(relativePackageBrokenManifestStatus.code, 0);
assert.equal(relativePackageBrokenManifestStatus.body.ok, true);
assert.equal(relativePackageBrokenManifestStatus.body.mode, "status");
assert.equal(relativePackageBrokenManifestStatus.body.repositoryGraph.projectionState, "conflict");
assert.deepEqual(relativePackageBrokenManifestStatus.body.diagnostics, []);
const relativePackageBrokenManifestVerify = json(["verify-projection", "--json", relativePackageRoot], { allowFailure: true });
assert.notEqual(relativePackageBrokenManifestVerify.code, 0);
const packageRefreshRoot = join("/tmp", `zero-repo-graph-package-refresh-${process.pid}`);
const packageRefreshSource = join(packageRefreshRoot, "src", "main.0");
const packageRefreshStore = join(packageRefreshRoot, "zero.graph");
rmSync(packageRefreshRoot, { force: true, recursive: true });
const packageRefreshInit = json(["init", "--json", packageRefreshRoot]);
assert.equal(packageRefreshInit.body.ok, true);
const packageRefreshName = readFileSync(join(packageRefreshRoot, "zero.toml"), "utf8").match(/^name = "([^"]+)"$/m)?.[1];
assert(packageRefreshName);
mkdirSync(join(packageRefreshRoot, "src"), { recursive: true });
writeFileSync(packageRefreshSource, 'pub fn main(world: World) -> Void raises {\n    check world.out.write("package refresh one\\n")\n}\n');
const packageRefreshImport = json(["import", "--json", packageRefreshSource]);
assert.equal(packageRefreshImport.code, 0);
assert.equal(packageRefreshImport.body.ok, true);
const packageRefreshQuery = json(["query", "--json", "--fn", "main", packageRefreshRoot]);
assert.equal(packageRefreshQuery.body.moduleIdentity, `package:${packageRefreshName}@0.1.0`);
const packageRefreshMainId = packageRefreshQuery.body.functions[0].id;
assert(packageRefreshMainId);
assert.equal(zero(["run", packageRefreshRoot]).stdout, "package refresh one\n");
writeFileSync(packageRefreshSource, 'pub fn main(world: World) -> Void raises {\n    check world.out.write("package refresh two\\n")\n}\n');
const packageRefreshReimport = json(["import", "--json", packageRefreshSource]);
assert.equal(packageRefreshReimport.body.ok, true);
assert.equal(zero(["run", packageRefreshRoot]).stdout, "package refresh two\n");
assert.equal(json(["query", "--json", "--fn", "main", packageRefreshRoot]).body.functions[0].id, packageRefreshMainId);
writeFileSync(packageRefreshSource, 'pub fn main(world: World) -> Void raises {\n    check world.out.write("package refresh three\\n")\n}\n');
const packageRefreshRootReimport = json(["import", "--json", packageRefreshRoot]);
assert.equal(packageRefreshRootReimport.body.ok, true);
assert.equal(zero(["run", packageRefreshRoot]).stdout, "package refresh three\n");
const packageRefreshStoreHash = sha256File(packageRefreshStore);
const packageRefreshOutRejected = json(["import", "--json", "--format", "binary", "--out", packageRefreshStore, packageRefreshSource], { allowFailure: true });
assert.notEqual(packageRefreshOutRejected.code, 0);
assert.equal(packageRefreshOutRejected.body.diagnostics[0].code, "RGP002");
assert.deepEqual(packageRefreshOutRejected.body.repairCommands, [`zero import ${packageRefreshSource}`]);
assert.equal(sha256File(packageRefreshStore), packageRefreshStoreHash);
const packageRefreshManifestText = readFileSync(join(packageRefreshRoot, "zero.toml"), "utf8");
writeFileSync(join(packageRefreshRoot, "zero.toml"), packageRefreshManifestText.replace('version = "0.1.0"', 'version = "0.2.0"'));
const packageRefreshBumpImport = json(["import", "--json", packageRefreshRoot]);
assert.equal(packageRefreshBumpImport.body.ok, true);
assert.equal(json(["query", "--json", packageRefreshRoot]).body.moduleIdentity, `package:${packageRefreshName}@0.2.0`);
assert.equal(zero(["run", packageRefreshRoot]).stdout, "package refresh three\n");
writeFileSync(join(packageRefreshRoot, "zero.toml"), packageRefreshManifestText);
assert.equal(json(["import", "--json", packageRefreshRoot]).body.ok, true);
const packageRefreshBareRoot = join("/tmp", `zero-repo-graph-package-refresh-bare-${process.pid}`);
rmSync(packageRefreshBareRoot, { force: true, recursive: true });
mkdirSync(packageRefreshBareRoot, { recursive: true });
writeFileSync(join(packageRefreshBareRoot, "main.0"), readFileSync(packageRefreshSource, "utf8"));
assert.equal(json(["import", "--json", "--format", "binary", join(packageRefreshBareRoot, "main.0")]).body.ok, true);
writeFileSync(packageRefreshStore, readFileSync(join(packageRefreshBareRoot, "zero.graph")));
const packageRefreshPoisonedCheck = json(["check", "--json", packageRefreshRoot], { allowFailure: true });
assert.notEqual(packageRefreshPoisonedCheck.code, 0);
assert.equal(packageRefreshPoisonedCheck.body.diagnostics[0].code, "RGP007");
assert.equal(packageRefreshPoisonedCheck.body.diagnostics[0].actual, "module:main");
assert.match(packageRefreshPoisonedCheck.body.diagnostics[0].help, /run zero import to refresh zero\.graph/);
assert.deepEqual(packageRefreshPoisonedCheck.body.repairCommands, [`zero import ${packageRefreshRoot}`]);
const packageRefreshHealImport = json(["import", "--json", packageRefreshRoot]);
assert.equal(packageRefreshHealImport.body.ok, true);
assert.equal(zero(["run", packageRefreshRoot]).stdout, "package refresh three\n");
const packageForeignRoot = join("/tmp", `zero-repo-graph-package-foreign-${process.pid}`);
rmSync(packageForeignRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", packageForeignRoot]).body.ok, true);
mkdirSync(join(packageForeignRoot, "src"), { recursive: true });
writeFileSync(join(packageForeignRoot, "src", "main.0"), readFileSync(packageRefreshSource, "utf8"));
assert.equal(json(["import", "--json", packageForeignRoot]).body.ok, true);
writeFileSync(packageRefreshStore, readFileSync(join(packageForeignRoot, "zero.graph")));
const packageForeignStoreHash = sha256File(packageRefreshStore);
const packageForeignImport = json(["import", "--json", packageRefreshRoot], { allowFailure: true });
assert.notEqual(packageForeignImport.code, 0);
assert.equal(packageForeignImport.body.diagnostics[0].code, "RGP007");
assert.equal(packageForeignImport.body.diagnostics[0].message, "repository graph source identity has a different module identity");
assert.equal(sha256File(packageRefreshStore), packageForeignStoreHash);
const packageForeignCheck = json(["check", "--json", packageRefreshRoot], { allowFailure: true });
assert.notEqual(packageForeignCheck.code, 0);
assert.equal(packageForeignCheck.body.diagnostics[0].code, "RGP007");
rmSync(packageRefreshStore, { force: true });
assert.equal(json(["import", "--json", packageRefreshRoot]).body.ok, true);
assert.equal(zero(["run", packageRefreshRoot]).stdout, "package refresh three\n");
const staleRunRoot = join("/tmp", `zero-repo-graph-stale-run-${process.pid}`);
const staleRunSource = join(staleRunRoot, "src", "main.0");
const staleRunStore = join(staleRunRoot, "zero.graph");
rmSync(staleRunRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", staleRunRoot]).body.ok, true);
mkdirSync(join(staleRunRoot, "src"), { recursive: true });
writeFileSync(staleRunSource, 'pub fn main(world: World) -> Void raises {\n    check world.out.write("stale run one\\n")\n}\n');
assert.equal(json(["import", "--json", staleRunRoot]).body.ok, true);
assert.equal(zero(["run", staleRunRoot]).stdout, "stale run one\n");
writeFileSync(staleRunSource, 'pub fn main(world: World) -> Void raises {\n    check world.out.write("stale run two\\n")\n}\n');
assert.equal(json(["status", "--json", staleRunRoot]).body.repositoryGraph.projectionState, "source-stale");
const staleRunStoreHashBefore = sha256File(staleRunStore);
assert.equal(zero(["run", staleRunRoot]).stdout, "stale run two\n");
assert.notEqual(sha256File(staleRunStore), staleRunStoreHashBefore);
assert.equal(json(["status", "--json", staleRunRoot]).body.repositoryGraph.projectionState, "clean");
writeFileSync(staleRunSource, 'pub fn main(world: World) -> Void raises {\n    check world.out.write("stale run three\\n")\n}\n');
assert.equal(zero(["check", staleRunRoot]).stdout, "ok\n");
assert.equal(json(["status", "--json", staleRunRoot]).body.repositoryGraph.projectionState, "clean");
assert.equal(zero(["run", staleRunRoot]).stdout, "stale run three\n");
const staleRunGoodSource = readFileSync(staleRunSource, "utf8");
writeFileSync(staleRunSource, `${staleRunGoodSource}fn broken( {\n`);
const staleRunBrokenCheck = zero(["check", staleRunRoot], { allowFailure: true });
assert.notEqual(staleRunBrokenCheck.code, 0);
const staleRunBrokenRun = zero(["run", staleRunRoot], { allowFailure: true });
assert.notEqual(staleRunBrokenRun.code, 0);
writeFileSync(staleRunSource, staleRunGoodSource);
assert.equal(zero(["run", staleRunRoot]).stdout, "stale run three\n");
writeFileSync(staleRunSource, 'pub fn main(world: World) -> Void raises {\n    check world.out.write("stale run four\\n")\n}\n');
const staleRunStrictHash = sha256File(staleRunStore);
const staleRunStrict = json(["build", "--json", staleRunRoot], { allowFailure: true, env: { ZERO_STALE: "fail" } });
assert.notEqual(staleRunStrict.code, 0);
assert.equal(staleRunStrict.body.diagnostics[0].code, "RGP008");
assert.deepEqual(staleRunStrict.body.repairCommands, [`zero import ${staleRunRoot}`]);
assert.equal(sha256File(staleRunStore), staleRunStrictHash);
const staleRunStrictRun = zero(["run", staleRunRoot], { allowFailure: true, env: { ZERO_STALE: "fail" } });
assert.notEqual(staleRunStrictRun.code, 0);
assert.equal(zero(["run", staleRunRoot]).stdout, "stale run four\n");
assert.equal(zero(["run", staleRunSource]).stdout, "stale run four\n");
assert.equal(zero(["check", staleRunSource]).stdout, "ok\n");
writeFileSync(staleRunSource, 'pub fn main(world: World) -> Void raises {\n    check world.out.write("stale run five\\n")\n}\n');
assert.equal(zero(["run", staleRunSource]).stdout, "stale run five\n");
assert.equal(json(["status", "--json", staleRunRoot]).body.repositoryGraph.projectionState, "clean");

const cachedArgvRoot = join("/tmp", `zero-repo-graph-cached-argv-${process.pid}`);
const cachedArgvSource = join(cachedArgvRoot, "src", "main.0");
rmSync(cachedArgvRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", cachedArgvRoot]).body.ok, true);
mkdirSync(join(cachedArgvRoot, "src"), { recursive: true });
writeFileSync(cachedArgvSource, 'pub fn main(world: World) -> Void raises {\n    let self: Maybe<String> = std.args.get(0)\n    if self.has {\n        check world.out.write(self.value)\n        check world.out.write("\\n")\n    }\n}\n');
assert.equal(json(["import", "--json", cachedArgvRoot]).body.ok, true);
assert.match(zero(["run", cachedArgvRoot]).stdout, /\.run-/);
const cachedArgvSecondRun = zero(["run", cachedArgvRoot]).stdout.trim();
assert.match(cachedArgvSecondRun, /\.run-/);
assert.doesNotMatch(cachedArgvSecondRun, /linked-exe-/);

const procFdRoot = join("/tmp", `zero-proc-child-fd-inherit-${process.pid}`);
const procFdSource = join(procFdRoot, "src", "main.0");
rmSync(procFdRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", procFdRoot]).body.ok, true);
mkdirSync(join(procFdRoot, "src"), { recursive: true });
writeFileSync(procFdSource, `pub fn main(world: World) -> Void raises {
    let first: ProcChild = std.proc.spawnChild("sh -c 'cat >/dev/null; printf first-done'")
    let second: ProcChild = std.proc.spawnChild("sleep 5")
    let closed: Bool = std.proc.closeStdin(first)
    let status: ProcStatus = std.proc.wait(first)
    var output: [64]u8 = [0_u8; 64]
    let read: Maybe<usize> = std.proc.readStdout(first, output)
    if closed && std.proc.succeeded(status) && read.has {
        let bytes: Span<u8> = std.io.written(output, read.value)
        check world.out.write(bytes)
        check world.out.write("\\n")
    }
    let killed: Bool = std.proc.kill(second)
    let second_status: ProcStatus = std.proc.wait(second)
    let closed_first: Bool = std.proc.close(first)
    let closed_second: Bool = std.proc.close(second)
}
`);
assert.equal(json(["import", "--json", procFdRoot]).body.ok, true);
const procFdRun = spawnSync(zeroBin, ["run", procFdRoot], { encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"], timeout: 1800 });
assert.equal(procFdRun.error, undefined);
assert.equal(procFdRun.status, 0);
assert.equal(procFdRun.stdout, "first-done\n");

const staleMultiRoot = join("/tmp", `zero-repo-graph-stale-multi-${process.pid}`);
const staleMultiStore = join(staleMultiRoot, "zero.graph");
const staleMultiHelper = join(staleMultiRoot, "src", "math.0");
rmSync(staleMultiRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", staleMultiRoot]).body.ok, true);
mkdirSync(join(staleMultiRoot, "src"), { recursive: true });
writeFileSync(
  join(staleMultiRoot, "src", "main.0"),
  'use math\n\npub fn main(world: World) -> Void raises {\n    if add_one(41) == 42 {\n        check world.out.write("stale multi one\\n")\n    }\n    if add_one(41) == 43 {\n        check world.out.write("stale multi two\\n")\n    }\n}\n',
);
writeFileSync(staleMultiHelper, "pub fn add_one(value: i32) -> i32 {\n    return value + 1\n}\n");
assert.equal(json(["import", "--json", staleMultiRoot]).body.ok, true);
assert.equal(zero(["run", staleMultiRoot]).stdout, "stale multi one\n");
const staleMultiOverview = zero(["query", staleMultiRoot]).stdout;
assert.match(staleMultiOverview, /math path:src\/math\.0 functions:1\n/);
assert.match(staleMultiOverview, /main path:src\/main\.0 functions:1 \(entry\)/);
assert.match(staleMultiOverview, /functions \(module main\):/);
assert.doesNotMatch(staleMultiOverview, /add_one\(value: i32\)/, "the bare overview lists only entry-module signatures");
writeFileSync(staleMultiHelper, "pub fn add_one(value: i32) -> i32 {\n    return value + 2\n}\n");
const staleMultiCheck = zeroWithStderr(["check"], { cwd: staleMultiRoot });
assert.equal(staleMultiCheck.code, 0);
assert.equal(staleMultiCheck.stdout, "ok\n");
assert.match(staleMultiCheck.stderr, /note: zero check refreshed zero\.graph from the edited package source projection; tip: zero patch --replace-fn <fn> --body-file - edits the graph directly and skips this reconcile/);
assert.equal((staleMultiCheck.stderr.match(/\n/g) ?? []).length, 1, "graph refresh note plus tip should stay on one stderr line");
assert.equal(json(["status", "--json", staleMultiRoot]).body.repositoryGraph.projectionState, "clean");
assert.equal(zero(["run", staleMultiRoot]).stdout, "stale multi two\n");
// Context-aware refresh tips: a signature-changing source edit teaches the
// declaration-level patch ops at the moment of fallback, a const initializer
// edit teaches setConst, and the refreshed (clean) state stays quiet.
const refreshTipRoot = join("/tmp", `zero-repo-graph-refresh-tip-${process.pid}`);
const refreshTipMain = join(refreshTipRoot, "src", "main.0");
const refreshTipMath = join(refreshTipRoot, "src", "math.0");
rmSync(refreshTipRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", refreshTipRoot]).body.ok, true);
mkdirSync(join(refreshTipRoot, "src"), { recursive: true });
writeFileSync(refreshTipMain, 'use math\n\nconst limit: i32 = 42\n\npub fn main(world: World) -> Void raises {\n    if add_one(41) == limit {\n        check world.out.write("tip one\\n")\n    }\n}\n');
writeFileSync(refreshTipMath, "pub fn add_one(value: i32) -> i32 {\n    return value + 1\n}\n");
assert.equal(json(["import", "--json", refreshTipRoot]).body.ok, true);
writeFileSync(refreshTipMath, "pub fn add_one(value: i32, bias: i32) -> i32 {\n    return value + bias\n}\n");
writeFileSync(refreshTipMain, 'use math\n\nconst limit: i32 = 42\n\npub fn main(world: World) -> Void raises {\n    if add_one(41, 1) == limit {\n        check world.out.write("tip one\\n")\n    }\n}\n');
const refreshTipSignature = zeroWithStderr(["check", refreshTipRoot]);
assert.equal(refreshTipSignature.code, 0);
assert.match(refreshTipSignature.stderr, /note: zero check refreshed zero\.graph from the edited package source projection; tip: zero patch --op 'addParamTo fn=\.\.\. name=\.\.\. type=\.\.\. default=\.\.\.' threads a new parameter through every call site in one step; setReturnType changes return types/);
assert.equal((refreshTipSignature.stderr.match(/\n/g) ?? []).length, 1, "signature refresh note plus tip should stay on one stderr line");
const refreshTipQuiet = zeroWithStderr(["check", refreshTipRoot]);
assert.equal(refreshTipQuiet.code, 0);
assert.doesNotMatch(refreshTipQuiet.stderr, /refreshed zero\.graph/, "the refresh note prints once per stale state");
writeFileSync(refreshTipMain, 'use math\n\nconst limit: i32 = 43\n\npub fn main(world: World) -> Void raises {\n    if add_one(41, 1) == limit {\n        check world.out.write("tip one\\n")\n    }\n}\n');
const refreshTipConst = zeroWithStderr(["check", refreshTipRoot]);
assert.equal(refreshTipConst.code, 0);
assert.match(refreshTipConst.stderr, /note: zero check refreshed zero\.graph from the edited package source projection; tip: zero patch --op 'setConst name=\.\.\. value=\.\.\.' replaces a top-level const initializer directly and skips this reconcile/);
assert.equal(json(["status", "--json", refreshTipRoot]).body.repositoryGraph.projectionState, "clean");
writeFileSync(staleMultiHelper, "pub fn add_one(value: i32) -> i32 {\n    return value + 1\n}\n");
const staleMultiStoreCheck = zeroWithStderr(["check", staleMultiStore]);
assert.equal(staleMultiStoreCheck.code, 0);
assert.equal(staleMultiStoreCheck.stdout, "ok\n");
assert.match(staleMultiStoreCheck.stderr, /note: zero check refreshed zero\.graph from the edited package source projection/);
assert.equal(json(["status", "--json", staleMultiRoot]).body.repositoryGraph.projectionState, "clean");
writeFileSync(staleMultiHelper, "pub fn add_one(value: i32) -> i32 {\n    return value + 2\n}\n");
const staleMultiBuild = zeroWithStderr(["build", staleMultiStore]);
assert.equal(staleMultiBuild.code, 0);
assert.match(staleMultiBuild.stderr, /note: zero build refreshed zero\.graph from the edited package source projection/);
assert.equal(zero(["run", staleMultiRoot]).stdout, "stale multi two\n");
writeFileSync(staleMultiHelper, "pub fn add_one(value: i32) -> i32 {\n    return value + 1\n}\n");
const staleMultiView = zeroWithStderr(["view", "--fn", "add_one", staleMultiRoot]);
assert.equal(staleMultiView.code, 0);
assert.match(staleMultiView.stdout, /return value \+ 1/);
assert.match(staleMultiView.stderr, /note: zero view refreshed zero\.graph from the edited package source projection/);
assert.equal(json(["status", "--json", staleMultiRoot]).body.repositoryGraph.projectionState, "clean");
const staleMultiPatch = json(["patch", "--json", staleMultiRoot, "--op", 'addLetLiteral fn="main" name="patched" type="u32" value="7"']);
assert.equal(staleMultiPatch.body.ok, true);
const staleMultiGraphNewerBuild = zeroWithStderr(["build", staleMultiRoot]);
assert.equal(staleMultiGraphNewerBuild.code, 0);
assert.match(staleMultiGraphNewerBuild.stderr, /note: zero build is using zero\.graph, which is newer than the \.0 source projection/);
// the store-newer note is once-per-state: a second command on the same store
// stays quiet until the store or source state changes
const staleMultiGraphNewerRepeat = zeroWithStderr(["check", staleMultiRoot]);
assert.equal(staleMultiGraphNewerRepeat.code, 0);
assert.doesNotMatch(staleMultiGraphNewerRepeat.stderr, /is using zero\.graph/, "two consecutive commands print the store-newer note once");
writeFileSync(staleMultiHelper, "pub fn add_one(value: i32) -> i32 {\n    return value + 2\n}\n");
const staleMultiDiverged = json(["check", "--json", staleMultiRoot], { allowFailure: true });
assert.notEqual(staleMultiDiverged.code, 0);
assert.equal(staleMultiDiverged.body.diagnostics[0].code, "RGP006");
assert.match(staleMultiDiverged.body.diagnostics[0].message, /have diverged/);
assert.match(staleMultiDiverged.body.repairCommands.join("\n"), /zero import/);
assert.match(staleMultiDiverged.body.repairCommands.join("\n"), /zero export/);
const staleMultiDivergedBuild = json(["build", "--json", staleMultiRoot], { allowFailure: true });
assert.notEqual(staleMultiDivergedBuild.code, 0);
assert.equal(staleMultiDivergedBuild.body.diagnostics[0].code, "RGP006");
assert.equal(json(["import", "--json", staleMultiRoot]).body.ok, true);
assert.equal(zero(["run", staleMultiRoot]).stdout, "stale multi two\n");
assert.equal(json(["status", "--json", staleMultiRoot]).body.repositoryGraph.projectionState, "clean");
// Content-based store/source sync classification: a freshly staged workspace
// with an edited source projection classifies source-newer and runs the
// edited behavior with the store mtime newest, oldest, or tied, because the
// store records a hash of the source projection at every write and mtimes
// never decide the state.
const stagedSyncBase = join("/tmp", `zero-staged-sync-base-${process.pid}`);
rmSync(stagedSyncBase, { force: true, recursive: true });
mkdirSync(join(stagedSyncBase, "src"), { recursive: true });
writeFileSync(join(stagedSyncBase, "zero.toml"), '[package]\nname = "staged-sync"\nversion = "0.1.0"\n\n[targets.cli]\nkind = "exe"\nmain = "src/main.0"\n');
writeFileSync(join(stagedSyncBase, "src", "main.0"), 'pub fn main(world: World) -> Void raises {\n    check world.out.write("staged one\\n")\n}\n');
assert.equal(json(["import", "--json", stagedSyncBase]).body.ok, true);
assert.equal(zero(["run", stagedSyncBase]).stdout, "staged one\n");
const stagedSyncEpoch = new Date("2026-01-01T00:00:00Z");
for (const order of ["store-newest", "store-oldest", "tied"]) {
  const stagedRoot = join("/tmp", `zero-staged-sync-${order}-${process.pid}`);
  rmSync(stagedRoot, { force: true, recursive: true });
  cpSync(stagedSyncBase, stagedRoot, { recursive: true });
  writeFileSync(join(stagedRoot, "src", "main.0"), 'pub fn main(world: World) -> Void raises {\n    check world.out.write("staged two\\n")\n}\n');
  const stagedNow = new Date();
  const storeTime = order === "store-newest" ? stagedNow : stagedSyncEpoch;
  const sourceTime = order === "store-oldest" ? stagedNow : stagedSyncEpoch;
  utimesSync(join(stagedRoot, "zero.graph"), storeTime, storeTime);
  utimesSync(join(stagedRoot, "src", "main.0"), sourceTime, sourceTime);
  const stagedRun = zeroWithStderr(["run", stagedRoot]);
  assert.equal(stagedRun.code, 0, `staged ${order} run exits 0`);
  assert.equal(stagedRun.stdout, "staged two\n", `staged ${order} runs the edited behavior`);
  assert.match(stagedRun.stderr, /refreshed zero\.graph from the edited package source projection/, `staged ${order} classifies source-newer`);
  rmSync(stagedRoot, { force: true, recursive: true });
}
// A patched store stays authoritative by content even when its mtime is the
// oldest file in the workspace.
const stagedPatch = json(["patch", "--json", stagedSyncBase, "--op", 'addCheckWrite fn="main" text="staged patched\\n"']);
assert.equal(stagedPatch.body.ok, true);
utimesSync(join(stagedSyncBase, "zero.graph"), stagedSyncEpoch, stagedSyncEpoch);
const stagedStoreNewer = zeroWithStderr(["run", stagedSyncBase]);
assert.equal(stagedStoreNewer.code, 0);
assert.equal(stagedStoreNewer.stdout, "staged one\nstaged patched\n");
assert.match(stagedStoreNewer.stderr, /is using zero\.graph, which is newer than the \.0 source projection/);
// zero export refreshes the recorded source projection hash so a source edit
// after the export classifies source-newer instead of diverged.
const stagedExport = json(["export", "--json", stagedSyncBase]);
assert.equal(stagedExport.body.ok, true);
assert.equal(stagedExport.body.changedPaths.includes(join(stagedSyncBase, "zero.graph")), true);
writeFileSync(join(stagedSyncBase, "src", "main.0"), 'pub fn main(world: World) -> Void raises {\n    check world.out.write("staged three\\n")\n}\n');
utimesSync(join(stagedSyncBase, "src", "main.0"), stagedSyncEpoch, stagedSyncEpoch);
const stagedAfterExport = zeroWithStderr(["run", stagedSyncBase]);
assert.equal(stagedAfterExport.code, 0);
assert.equal(stagedAfterExport.stdout, "staged three\n");
assert.match(stagedAfterExport.stderr, /refreshed zero\.graph from the edited package source projection/);
// Editing the source while the store holds unexported patches is a genuine
// divergence regardless of mtimes.
assert.equal(json(["patch", "--json", stagedSyncBase, "--op", 'addCheckWrite fn="main" text="staged patched again\\n"']).body.ok, true);
writeFileSync(join(stagedSyncBase, "src", "main.0"), 'pub fn main(world: World) -> Void raises {\n    check world.out.write("staged four\\n")\n}\n');
const stagedDiverged = json(["check", "--json", stagedSyncBase], { allowFailure: true });
assert.notEqual(stagedDiverged.code, 0);
assert.equal(stagedDiverged.body.diagnostics[0].code, "RGP006");
rmSync(stagedSyncBase, { force: true, recursive: true });
// Stores written before the recorded source projection hash existed degrade
// to one reconciling source refresh instead of an error wall, and the
// refresh records the hash.
const legacySyncRoot = join("/tmp", `zero-legacy-sync-${process.pid}`);
rmSync(legacySyncRoot, { force: true, recursive: true });
mkdirSync(join(legacySyncRoot, "src"), { recursive: true });
writeFileSync(join(legacySyncRoot, "zero.toml"), '[package]\nname = "legacy-sync"\nversion = "0.1.0"\n\n[targets.cli]\nkind = "exe"\nmain = "src/main.0"\n');
writeFileSync(join(legacySyncRoot, "src", "main.0"), 'pub fn main(world: World) -> Void raises {\n    check world.out.write("legacy one\\n")\n}\n');
assert.equal(json(["import", "--json", "--format", "text", legacySyncRoot]).body.ok, true);
const legacyStorePath = join(legacySyncRoot, "zero.graph");
const legacyStoreText = readFileSync(legacyStorePath, "utf8");
assert.match(legacyStoreText, /\nsourceHash "src:[0-9a-f]{16}"\n/);
writeFileSync(legacyStorePath, legacyStoreText.replace(/\nsourceHash "[^"]*"/, ""));
writeFileSync(join(legacySyncRoot, "src", "main.0"), 'pub fn main(world: World) -> Void raises {\n    check world.out.write("legacy two\\n")\n}\n');
utimesSync(join(legacySyncRoot, "src", "main.0"), stagedSyncEpoch, stagedSyncEpoch);
const legacyRun = zeroWithStderr(["run", legacySyncRoot]);
assert.equal(legacyRun.code, 0);
assert.equal(legacyRun.stdout, "legacy two\n");
assert.match(legacyRun.stderr, /refreshed zero\.graph from the edited package source projection/);
assert.match(readFileSync(legacyStorePath, "utf8"), /\nsourceHash "src:[0-9a-f]{16}"\n/);
rmSync(legacySyncRoot, { force: true, recursive: true });
// One diagnostic oracle for the sync verbs: zero check, zero import, and
// zero patch revalidation accept the same code (Span<T> helpers through
// std.mem.prefix, integer-literal returns in plain helpers, and ref<shape>
// parameters), revalidation reports every introduced diagnostic in one
// shot, and pre-existing diagnostics print as notes instead of walls.
const oracleAlignRoot = join("/tmp", `zero-oracle-align-${process.pid}`);
rmSync(oracleAlignRoot, { force: true, recursive: true });
mkdirSync(join(oracleAlignRoot, "src"), { recursive: true });
writeFileSync(join(oracleAlignRoot, "zero.toml"), '[package]\nname = "oracle-align"\nversion = "0.1.0"\n\n[targets.cli]\nkind = "exe"\nmain = "src/main.0"\n');
const oracleAlignSource = [
  "pub type Pair {",
  "    left: u64,",
  "    right: u64,",
  "}",
  "",
  "fn headBytes(bytes: Span<u8>, len: usize) -> Span<u8> {",
  "    return std.mem.prefix(bytes, len)",
  "}",
  "",
  "fn limit() -> usize {",
  "    return 5",
  "}",
  "",
  "pub fn pairLeft(p: ref<Pair>) -> u64 {",
  "    return p.left",
  "}",
  "",
  "pub fn main(world: World) -> Void raises {",
  '    let text: String = "alignment"',
  "    let head: Span<u8> = headBytes(text, limit())",
  "    if std.mem.len(head) == 5 {",
  '        check world.out.write("aligned\\n")',
  "    }",
  "}",
  "",
].join("\n");
writeFileSync(join(oracleAlignRoot, "src", "main.0"), oracleAlignSource);
assert.equal(json(["import", "--json", oracleAlignRoot]).body.ok, true);
assert.equal(zero(["check", oracleAlignRoot]).stdout, "ok\n");
assert.equal(zero(["run", oracleAlignRoot]).stdout, "aligned\n");
const oracleAlignPatch = json(["patch", "--json", oracleAlignRoot, "--op", 'addLetLiteral fn="main" name="probe" type="u32" value="1"']);
assert.equal(oracleAlignPatch.body.ok, true);
// a deliberate bad payload still fails with the diagnostic list and leaves
// the store untouched
const oracleHashBefore = json(["status", "--json", oracleAlignRoot]).body.store.graphHash;
const oracleBadPatch = json(["patch", "--json", oracleAlignRoot, "--op", 'addReturnValue fn="limit" value="no_such_binding" type="usize"'], { allowFailure: true });
assert.notEqual(oracleBadPatch.code, 0);
assert.equal(oracleBadPatch.body.ok, false);
assert.equal(oracleBadPatch.body.diagnostics[0].code, "NAM003");
assert.equal(json(["status", "--json", oracleAlignRoot]).body.store.graphHash, oracleHashBefore);
// an import that introduces two diagnostics reports both in one shot
assert.equal(json(["export", "--json", oracleAlignRoot]).body.ok, true);
writeFileSync(join(oracleAlignRoot, "src", "main.0"), oracleAlignSource.replace('    if std.mem.len(head) == 5 {', "    check world.out.write(missing_one)\n    check world.out.write(missing_two)\n    if std.mem.len(head) == 5 {"));
const oracleMultiImport = json(["import", "--json", oracleAlignRoot], { allowFailure: true });
assert.notEqual(oracleMultiImport.code, 0);
assert.equal(oracleMultiImport.body.diagnostics.length, 2);
assert.equal(oracleMultiImport.body.diagnostics[0].code, "NAM003");
assert.equal(oracleMultiImport.body.diagnostics[1].code, "NAM003");
rmSync(oracleAlignRoot, { force: true, recursive: true });
// pre-existing diagnostics report as notes, not walls: the TAR002 a
// capability-less target reports in zero check predates the patch, so a
// valid op still applies and the note points back at zero check
const oraclePreRoot = join("/tmp", `zero-oracle-preexisting-${process.pid}`);
rmSync(oraclePreRoot, { force: true, recursive: true });
mkdirSync(join(oraclePreRoot, "src"), { recursive: true });
writeFileSync(join(oraclePreRoot, "zero.toml"), '[package]\nname = "oracle-preexisting"\nversion = "0.1.0"\n\n[targets.cli]\nkind = "exe"\nmain = "src/main.0"\n');
writeFileSync(join(oraclePreRoot, "src", "main.0"), 'pub fn main(world: World) -> Void raises {\n    let a1: Maybe<String> = std.args.get(1)\n    if a1.has {\n        check world.out.write(a1.value)\n    }\n    check world.out.write("done\\n")\n}\n');
assert.equal(json(["import", "--json", oraclePreRoot]).body.ok, true);
// win32-x64.exe is a direct-backend cross target on every CI host, so the
// Args-capability gap is host-independent (linux-x64 resolves to the hosted
// backend on linux runners and reports nothing).
const oraclePreCheck = json(["check", "--json", "--target", "win32-x64.exe", oraclePreRoot], { allowFailure: true });
assert.notEqual(oraclePreCheck.code, 0);
assert.equal(oraclePreCheck.body.diagnostics[0].code, "TAR002");
const oraclePrePatch = zeroWithStderr(["patch", "--target", "win32-x64.exe", oraclePreRoot, "--op", 'addLetLiteral fn="main" name="probe" type="u32" value="1"']);
assert.equal(oraclePrePatch.code, 0);
assert.match(oraclePrePatch.stdout, /program graph patch ok/);
assert.match(oraclePrePatch.stderr, /pre-existing diagnostic predates this patch and did not block it/);
assert.match(oraclePrePatch.stderr, /TAR002: target does not provide required Args capability/);
// the pre-existing note is once-per-state as well: the next patch against the
// same unchanged diagnostic set stays quiet
const oraclePrePatchRepeat = zeroWithStderr(["patch", "--target", "win32-x64.exe", oraclePreRoot, "--op", 'addLetLiteral fn="main" name="probe2" type="u32" value="2"']);
assert.equal(oraclePrePatchRepeat.code, 0);
assert.match(oraclePrePatchRepeat.stdout, /program graph patch ok/);
assert.doesNotMatch(oraclePrePatchRepeat.stderr, /pre-existing diagnostic/, "the pre-existing diagnostics note prints once per diagnostic state");
rmSync(oraclePreRoot, { force: true, recursive: true });
// a store whose recorded projection already fails the compiler typecheck
// (checker drift, stores written by older compilers) does not wall the
// import of unchanged files: the failure prints as a note and the refresh
// proceeds, so one pre-existing diagnostic never costs a whack-a-mole round
const oracleDriftRoot = join("/tmp", `zero-oracle-drift-${process.pid}`);
rmSync(oracleDriftRoot, { force: true, recursive: true });
mkdirSync(join(oracleDriftRoot, "src"), { recursive: true });
writeFileSync(join(oracleDriftRoot, "zero.toml"), '[package]\nname = "oracle-drift"\nversion = "0.1.0"\n\n[targets.cli]\nkind = "exe"\nmain = "src/main.0"\n');
writeFileSync(join(oracleDriftRoot, "src", "main.0"), 'fn want(v: u32) -> u32 {\n    return v\n}\n\npub fn main(world: World) -> Void raises {\n    let r: u32 = want(1_u32)\n    if r == 1 {\n        check world.out.write("drift ok\\n")\n    }\n}\n');
assert.equal(json(["import", "--json", "--format", "text", oracleDriftRoot]).body.ok, true);
const oracleDriftStorePath = join(oracleDriftRoot, "zero.graph");
writeFileSync(oracleDriftStorePath, readFileSync(oracleDriftStorePath, "utf8").replace("want(1_u32)", 'want(\\"text\\")'));
writeFileSync(join(oracleDriftRoot, "src", "main.0"), readFileSync(join(oracleDriftRoot, "src", "main.0"), "utf8").replace("want(1_u32)", 'want("text")'));
const oracleDriftImport = zeroWithStderr(["import", oracleDriftRoot]);
assert.equal(oracleDriftImport.code, 0);
assert.match(oracleDriftImport.stdout, /repository graph import ok/);
assert.match(oracleDriftImport.stderr, /note: a pre-existing diagnostic predates this import and did not block it \(the file is unchanged since the last sync\)/);
assert.equal(json(["status", "--json", oracleDriftRoot]).body.repositoryGraph.projectionState, "clean");
assert.equal(zero(["check", oracleDriftRoot]).stdout, "ok\n");
rmSync(oracleDriftRoot, { force: true, recursive: true });
const reconcileScaleRoot = join("/tmp", `zero-reconcile-scale-${process.pid}`);
const reconcileScaleSource = join(reconcileScaleRoot, "src", "main.0");
rmSync(reconcileScaleRoot, { force: true, recursive: true });
assert.equal(json(["init", "--json", reconcileScaleRoot]).body.ok, true);
mkdirSync(join(reconcileScaleRoot, "src"), { recursive: true });
const reconcileScaleFunctions: string[] = [];
for (let i = 0; i < 240; i++) {
  reconcileScaleFunctions.push([
    `fn helper${i}(x: usize) -> usize {`,
    "    var a: usize = x",
    ...Array.from({ length: 8 }, (_, j) => `    a = a + ${i * 8 + j + 1}_usize`),
    "    return a",
    "}",
  ].join("\n"));
}
const reconcileScaleMain = 'pub fn main(world: World) -> Void raises {\n    check world.out.write("reconcile scale\\n")\n}';
writeFileSync(reconcileScaleSource, `${reconcileScaleFunctions.join("\n\n")}\n\n${reconcileScaleMain}\n`);
assert.equal(json(["import", "--json", reconcileScaleRoot]).body.ok, true);
const reconcileScaleNeedle = "fn helper120(x: usize) -> usize {\n    var a: usize = x";
const reconcileScaleText = readFileSync(reconcileScaleSource, "utf8");
assert(reconcileScaleText.includes(reconcileScaleNeedle));
writeFileSync(reconcileScaleSource, reconcileScaleText.replace(reconcileScaleNeedle, `${reconcileScaleNeedle}\n    var inserted: usize = a + 7001_usize\n    inserted = inserted + 7003_usize\n    a = a + inserted`));
const reconcileScaleStarted = Date.now();
let reconcileScaleStdout = "";
try {
  reconcileScaleStdout = execFileSync(zeroBin, ["reconcile", "--json", reconcileScaleRoot, "--source", reconcileScaleSource], { encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"], timeout: 120000 });
} catch (error) {
  assert(false, `zero reconcile on a multi-statement insertion into a large package did not complete: ${error.signal ?? error.status}`);
}
const reconcileScaleElapsed = Date.now() - reconcileScaleStarted;
assert(reconcileScaleElapsed < 60000, `zero reconcile on a large package took ${reconcileScaleElapsed}ms`);
if (reconcileScaleStdout) {
  const reconcileScale = JSON.parse(reconcileScaleStdout);
  assert.equal(reconcileScale.ok, true);
  assert.equal(reconcileScale.identity.moduleIdentityChanged, false);
  assert.equal(reconcileScale.identity.ambiguous, 0);
  assert(reconcileScale.identity.inserted > 0);
  assert(reconcileScale.identity.unchanged > 0);
}
assert.equal(zero(["run", reconcileScaleRoot]).stdout, "reconcile scale\n");
assert.equal(json(["status", "--json", reconcileScaleRoot]).body.repositoryGraph.projectionState, "clean");
const postCheckWriteFailureRoot = join("/tmp", `zero-repo-graph-post-check-write-${process.pid}`);
const postCheckWriteFailureHelper = join(postCheckWriteFailureRoot, "src", "helper.0");
rmSync(postCheckWriteFailureRoot, { force: true, recursive: true });
mkdirSync(join(postCheckWriteFailureRoot, "src"), { recursive: true });
writeFileSync(join(postCheckWriteFailureRoot, "zero.toml"), readFileSync("conformance/packages/test-app/zero.toml", "utf8"));
writeFileSync(join(postCheckWriteFailureRoot, "src", "main.0"), readFileSync("conformance/packages/test-app/src/main.0", "utf8"));
writeFileSync(postCheckWriteFailureHelper, readFileSync("conformance/packages/test-app/src/helper.0", "utf8"));
json(["import", "--format", "text", "--json", postCheckWriteFailureRoot]);
const postCheckWriteFailureOriginalHelper = readFileSync(postCheckWriteFailureHelper, "utf8");
writeFileSync(postCheckWriteFailureHelper, postCheckWriteFailureOriginalHelper.replace("return value + value", "return value + value + 0"));
writeZeroTomlSync(postCheckWriteFailureRoot, { package: { name: "test-app", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "src/missing.0" } } });
const postCheckWriteFailure = json(["export", "--json", postCheckWriteFailureRoot], { allowFailure: true });
assert.notEqual(postCheckWriteFailure.code, 0);
assert.equal(postCheckWriteFailure.body.diagnostics[0].code, "RGP006");
assert.equal(postCheckWriteFailure.body.writes, true);
assert.deepEqual(postCheckWriteFailure.body.changedPaths, [postCheckWriteFailureHelper]);
assert.equal(readFileSync(postCheckWriteFailureHelper, "utf8"), postCheckWriteFailureOriginalHelper);
const targetProjectionRoot = join("/tmp", `zero-repo-graph-target-projection-${process.pid}`);
const targetProjectionSource = join(targetProjectionRoot, "main.0");
rmSync(targetProjectionRoot, { force: true, recursive: true });
mkdirSync(targetProjectionRoot, { recursive: true });
writeFileSync(targetProjectionSource, readFileSync("conformance/native/pass/meta-typed-target-type.0", "utf8"));
const targetProjectionSync = json(["import", "--format", "text", "--json", "--target", "linux-musl-x64", targetProjectionSource]);
assert.equal(targetProjectionSync.code, 0);
assert.equal(targetProjectionSync.body.repositoryGraph.projectionState, "clean");
const targetProjectionStatus = json(["status", "--json", "--target", "linux-musl-x64", targetProjectionSource]);
assert.equal(targetProjectionStatus.code, 0);
assert.equal(targetProjectionStatus.body.repositoryGraph.projectionState, "clean");
const targetProjectionVerify = json(["verify-projection", "--json", "--target", "linux-musl-x64", targetProjectionSource]);
assert.equal(targetProjectionVerify.code, 0);
assert.equal(targetProjectionVerify.body.ok, true);
const targetProjectionVerifyScript = repositoryGraphVerifyScript(targetProjectionRoot, { target: "linux-musl-x64" });
assert.equal(targetProjectionVerifyScript.code, 0);
assert.match(targetProjectionVerifyScript.stdout, /repository graph verify-projection ok \(1 store\)/);
const targetProjectionExport = json(["export", "--json", "--target", "linux-musl-x64", targetProjectionSource]);
assert.equal(targetProjectionExport.code, 0);
assert.equal(targetProjectionExport.body.repositoryGraph.projectionState, "clean");
assert.deepEqual(targetProjectionExport.body.changedPaths, []);
const typeInvalidRepoGraphRoot = join("/tmp", `zero-repo-graph-type-invalid-${process.pid}`);
const typeInvalidRepoGraphSource = join(typeInvalidRepoGraphRoot, "main.0");
const typeInvalidRepoGraphStore = join(typeInvalidRepoGraphRoot, "zero.graph");
rmSync(typeInvalidRepoGraphRoot, { force: true, recursive: true });
mkdirSync(typeInvalidRepoGraphRoot, { recursive: true });
writeFileSync(typeInvalidRepoGraphSource, "pub fn main() -> i32 {\n    return true\n}\n");
const typeInvalidRepoGraphSync = json(["import", "--format", "text", "--json", typeInvalidRepoGraphSource], { allowFailure: true });
assert.notEqual(typeInvalidRepoGraphSync.code, 0);
assert.equal(typeInvalidRepoGraphSync.body.diagnostics[0].code, "TYP003");
assert(!existsSync(typeInvalidRepoGraphStore));
const stdProjectionRoot = join("/tmp", `zero-repo-graph-std-projection-${process.pid}`);
const stdProjectionSource = join(stdProjectionRoot, "std-path-io.0");
const stdProjectionStore = join(stdProjectionRoot, "zero.graph");
rmSync(stdProjectionRoot, { force: true, recursive: true });
mkdirSync(stdProjectionRoot, { recursive: true });
writeFileSync(stdProjectionSource, readFileSync("examples/std-path-io.0", "utf8"));
const stdProjectionSync = json(["import", "--format", "text", "--json", stdProjectionSource]);
assert.equal(stdProjectionSync.code, 0);
assert.equal(stdProjectionSync.body.repositoryGraph.projectionState, "clean");
const stdProjectionStoreText = readFileSync(stdProjectionStore, "utf8");
assert.match(stdProjectionStoreText, /^source path:"std-path-io\.0"$/m);
assert.doesNotMatch(stdProjectionStoreText, /^source path:"std\//m);
assert.match(stdProjectionStoreText, /^projection path:"std-path-io\.0" text:/m);
assert.doesNotMatch(stdProjectionStoreText, /^projection path:"std\//m);
const stdProjectionVerify = json(["verify-projection", "--json", stdProjectionSource]);
assert.equal(stdProjectionVerify.body.ok, true);
const embeddedStdMainRoot = join("/tmp", `zero-repo-graph-embedded-std-main-${process.pid}`);
const embeddedStdMainStore = join(embeddedStdMainRoot, "zero.graph");
rmSync(embeddedStdMainRoot, { force: true, recursive: true });
mkdirSync(join(embeddedStdMainRoot, "std"), { recursive: true });
writeZeroTomlSync(embeddedStdMainRoot, { package: { name: "embedded-std-main", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "std/path.0" } } });
writeFileSync(join(embeddedStdMainRoot, "std", "path.0"), readFileSync("std/path.0", "utf8"));
const embeddedStdMainSync = json(["import", "--format", "text", "--json", embeddedStdMainRoot]);
assert.equal(embeddedStdMainSync.code, 0);
assert.equal(embeddedStdMainSync.body.ok, true);
assert.equal(embeddedStdMainSync.body.repositoryGraph.projectionState, "clean");
const embeddedStdMainStoreText = readFileSync(embeddedStdMainStore, "utf8");
assert.match(embeddedStdMainStoreText, /^projection path:"std\/path\.0" text:/m);
const embeddedStdMainVerify = json(["verify-projection", "--json", embeddedStdMainRoot]);
assert.equal(embeddedStdMainVerify.body.ok, true);
const embeddedStdRelativeRoot = join("/tmp", `zero-repo-graph-embedded-std-relative-${process.pid}`);
const embeddedStdRelativeSource = join(embeddedStdRelativeRoot, "std", "path.0");
rmSync(embeddedStdRelativeRoot, { force: true, recursive: true });
mkdirSync(join(embeddedStdRelativeRoot, "std"), { recursive: true });
writeFileSync(embeddedStdRelativeSource, readFileSync("std/path.0", "utf8"));
writeFileSync(join(embeddedStdRelativeRoot, "std", "str.0"), readFileSync("std/str.0", "utf8"));
const embeddedStdRelativeSync = json(["import", "--format", "text", "--json", embeddedStdRelativeSource]);
assert.equal(embeddedStdRelativeSync.code, 0);
assert.equal(embeddedStdRelativeSync.body.repositoryGraph.projectionState, "clean");
const embeddedStdRelativeVerify = json(["verify-projection", "--json", embeddedStdRelativeSource]);
assert.equal(embeddedStdRelativeVerify.body.ok, true);
writeFileSync(stdProjectionStore, refreshRepositoryGraphCompilerMetadata(stdProjectionStoreText.replace(/^nodeHash /m, `projection path:"std/path.0" text:${JSON.stringify("// embedded std projection should be rejected\\n")}\nnodeHash `)));
const embeddedStdProjectionStatus = json(["status", "--json", stdProjectionSource], { allowFailure: true });
assert.notEqual(embeddedStdProjectionStatus.code, 0);
assert.equal(embeddedStdProjectionStatus.body.diagnostics[0].code, "RGP003");
assert.match(embeddedStdProjectionStatus.body.diagnostics[0].actual, /unknown source path/);
const partialProjectionRoot = join("/tmp", `zero-repo-graph-partial-projection-${process.pid}`);
const partialProjectionStore = join(partialProjectionRoot, "zero.graph");
rmSync(partialProjectionRoot, { force: true, recursive: true });
mkdirSync(join(partialProjectionRoot, "src"), { recursive: true });
writeFileSync(join(partialProjectionRoot, "zero.toml"), readFileSync("conformance/packages/test-app/zero.toml", "utf8"));
writeFileSync(join(partialProjectionRoot, "src", "main.0"), readFileSync("conformance/packages/test-app/src/main.0", "utf8"));
writeFileSync(join(partialProjectionRoot, "src", "helper.0"), readFileSync("conformance/packages/test-app/src/helper.0", "utf8"));
json(["import", "--format", "text", "--json", partialProjectionRoot]);
const partialProjectionStoreText = readFileSync(partialProjectionStore, "utf8");
assert.match(partialProjectionStoreText, /^source path:"src\/helper\.0"$/m);
assert.match(partialProjectionStoreText, /^projection path:"src\/helper\.0" text:/m);
const swappedProjectionStoreText = partialProjectionStoreText.replace(
  /^(projection path:"src\/helper\.0" text:[^\n]*\n)(projection path:"src\/main\.0" text:[^\n]*\n)/m,
  "$2$1",
);
writeFileSync(partialProjectionStore, swappedProjectionStoreText);
const swappedProjectionStatus = json(["status", "--json", partialProjectionRoot], { allowFailure: true });
assert.notEqual(swappedProjectionStatus.code, 0);
assert.equal(swappedProjectionStatus.body.diagnostics[0].code, "RGP003");
assert.match(swappedProjectionStatus.body.diagnostics[0].actual, /byte-stable/);
writeFileSync(partialProjectionStore, partialProjectionStoreText);
writeFileSync(partialProjectionStore, refreshRepositoryGraphCompilerMetadata(partialProjectionStoreText.replace(/^projection path:"src\/helper\.0" text:[^\n]*\n/m, "")));
const partialProjectionStatus = json(["status", "--json", partialProjectionRoot], { allowFailure: true });
assert.notEqual(partialProjectionStatus.code, 0);
assert.equal(partialProjectionStatus.body.diagnostics[0].code, "RGP003");
assert.match(partialProjectionStatus.body.diagnostics[0].actual, /projection table is missing a source path/);
const partialProjectionWriteRoot = join("/tmp", `zero-repo-graph-partial-write-${process.pid}`);
const partialProjectionWriteMain = join(partialProjectionWriteRoot, "src", "main.0");
const partialProjectionWriteHelper = join(partialProjectionWriteRoot, "src", "helper.0");
rmSync(partialProjectionWriteRoot, { force: true, recursive: true });
mkdirSync(join(partialProjectionWriteRoot, "src"), { recursive: true });
writeFileSync(join(partialProjectionWriteRoot, "zero.toml"), readFileSync("conformance/packages/test-app/zero.toml", "utf8"));
writeFileSync(partialProjectionWriteMain, readFileSync("conformance/packages/test-app/src/main.0", "utf8"));
writeFileSync(partialProjectionWriteHelper, readFileSync("conformance/packages/test-app/src/helper.0", "utf8"));
json(["import", "--format", "text", "--json", partialProjectionWriteRoot]);
const partialProjectionOriginalHelper = readFileSync(partialProjectionWriteHelper, "utf8");
const partialProjectionDriftedHelper = partialProjectionOriginalHelper.replace("return value + value", "return value + value + 0");
writeFileSync(partialProjectionWriteHelper, partialProjectionDriftedHelper);
writeFileSync(partialProjectionWriteMain, readFileSync(partialProjectionWriteMain, "utf8").replace("package tests", "package tests drift"));
rmSync(partialProjectionWriteMain, { force: true });
mkdirSync(partialProjectionWriteMain);
const partialProjectionWriteFailure = json(["export", "--json", partialProjectionWriteRoot], { allowFailure: true });
assert.notEqual(partialProjectionWriteFailure.code, 0);
assert.equal(partialProjectionWriteFailure.body.diagnostics[0].code, "RGP004");
assert.equal(partialProjectionWriteFailure.body.writes, false);
assert.equal(readFileSync(partialProjectionWriteHelper, "utf8"), partialProjectionDriftedHelper);
const unsafeProjectionRoot = join("/tmp", `zero-repo-graph-unsafe-projection-${process.pid}`);
const unsafeProjectionSource = join(unsafeProjectionRoot, "outside.0");
const unsafeProjectionArtifact = join(unsafeProjectionRoot, "outside.program-graph");
const unsafeProjectionStoreRoot = join(unsafeProjectionRoot, "repo");
const unsafeProjectionStore = join(unsafeProjectionStoreRoot, "zero.graph");
const unsafeProjectionText = "// should not be written outside the repository graph root\n";
rmSync(unsafeProjectionRoot, { force: true, recursive: true });
mkdirSync(unsafeProjectionStoreRoot, { recursive: true });
writeFileSync(unsafeProjectionSource, readFileSync("examples/hello.0", "utf8"));
assert.equal(zero(["import", "--out", unsafeProjectionArtifact, unsafeProjectionSource]).stdout, "");
const unsafeProjectionGraph = zero(["dump", unsafeProjectionArtifact]).stdout;
const unsafeProjectionGraphJson = json(["dump", "--json", unsafeProjectionArtifact]).body;
const unsafeProjectionQuote = JSON.stringify;
const unsafeProjectionStoreLines = [
  "zero-repository-graph v1",
  `sourceProjection ${unsafeProjectionQuote(".0")}`,
  `moduleIdentity ${unsafeProjectionQuote(unsafeProjectionGraphJson.moduleIdentity)}`,
  `graphHash ${unsafeProjectionQuote(unsafeProjectionGraphJson.graphHash)}`,
  `moduleHash ${unsafeProjectionQuote(unsafeProjectionGraphJson.graphHash)}`,
  `source path:${unsafeProjectionQuote(unsafeProjectionSource)}`,
  `projection path:${unsafeProjectionQuote(unsafeProjectionSource)} text:${unsafeProjectionQuote(unsafeProjectionText)}`,
  ...unsafeProjectionGraphJson.nodes.map((node) => `nodeHash node:${unsafeProjectionQuote(node.id)} hash:${unsafeProjectionQuote(node.nodeHash)}`),
];
writeFileSync(unsafeProjectionStore, `${unsafeProjectionStoreLines.join("\n")}\n\ngraph\n${unsafeProjectionGraph}`);
const unsafeProjectionStatus = json(["status", "--json", unsafeProjectionStoreRoot], { allowFailure: true });
assert.notEqual(unsafeProjectionStatus.code, 0);
assert.equal(unsafeProjectionStatus.body.diagnostics[0].code, "RGP003");
assert.match(unsafeProjectionStatus.body.diagnostics[0].actual, /invalid repository graph store/);
const unsafeProjectionSync = json(["export", "--json", unsafeProjectionStoreRoot], { allowFailure: true });
assert.notEqual(unsafeProjectionSync.code, 0);
assert.notEqual(readFileSync(unsafeProjectionSource, "utf8"), unsafeProjectionText);
if (process.platform !== "win32") {
  const symlinkProjectionParent = join("/tmp", `zero-repo-graph-symlink-projection-${process.pid}`);
  const symlinkProjectionRoot = join(symlinkProjectionParent, "repo");
  const symlinkProjectionOutside = join(symlinkProjectionParent, "outside");
  const symlinkProjectionRootSourceDir = join(symlinkProjectionRoot, "src");
  const symlinkProjectionRootSource = join(symlinkProjectionRootSourceDir, "main.0");
  const symlinkProjectionOutsideSource = join(symlinkProjectionOutside, "main.0");
  rmSync(symlinkProjectionParent, { force: true, recursive: true });
  mkdirSync(symlinkProjectionRoot, { recursive: true });
  mkdirSync(symlinkProjectionRootSourceDir, { recursive: true });
  mkdirSync(symlinkProjectionOutside, { recursive: true });
  writeZeroTomlSync(symlinkProjectionRoot, { package: { name: "symlink-projection", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "src/main.0" } } });
  writeFileSync(symlinkProjectionRootSource, readFileSync("examples/hello.0", "utf8"));
  json(["import", "--format", "text", "--json", symlinkProjectionRoot]);
  rmSync(symlinkProjectionRootSourceDir, { force: true, recursive: true });
  symlinkSync(symlinkProjectionOutside, symlinkProjectionRootSourceDir, "dir");
  writeFileSync(symlinkProjectionOutsideSource, readFileSync("examples/hello.0", "utf8"));
  const symlinkProjectionDriftedSource = readFileSync(symlinkProjectionOutsideSource, "utf8").replace("hello from zero", "changed outside projection root");
  writeFileSync(symlinkProjectionOutsideSource, symlinkProjectionDriftedSource);
  const symlinkProjectionSync = json(["export", "--json", symlinkProjectionRoot], { allowFailure: true });
  assert.notEqual(symlinkProjectionSync.code, 0);
  assert.equal(symlinkProjectionSync.body.diagnostics[0].code, "RGP004");
  assert.equal(symlinkProjectionSync.body.writes, false);
  assert.equal(readFileSync(symlinkProjectionOutsideSource, "utf8"), symlinkProjectionDriftedSource);
}
const repoGraphArtifactInputRoot = join("/tmp", `zero-repo-graph-artifact-input-${process.pid}`);
const repoGraphArtifactInputPath = join(repoGraphArtifactInputRoot, "hello.program-graph");
rmSync(repoGraphArtifactInputRoot, { force: true, recursive: true });
mkdirSync(repoGraphArtifactInputRoot, { recursive: true });
zero(["dump", "--out", repoGraphArtifactInputPath, "examples/hello.0"]);
const repoGraphStatusArtifactNoStore = json(["status", "--json", repoGraphArtifactInputPath], { allowFailure: true });
assert.notEqual(repoGraphStatusArtifactNoStore.code, 0);
assert.equal(repoGraphStatusArtifactNoStore.body.diagnostics[0].message, "expected Zero source file or package");
const repoGraphVerifyArtifactNoStore = json(["verify-projection", "--json", repoGraphArtifactInputPath], { allowFailure: true });
assert.notEqual(repoGraphVerifyArtifactNoStore.code, 0);
assert.equal(repoGraphVerifyArtifactNoStore.body.diagnostics[0].message, "expected Zero source file or package");
assert(!existsSync(join(repoGraphArtifactInputRoot, "zero.graph")));
const repoGraphSyncFromArtifact = json(["import", "--format", "text", "--json", repoGraphArtifactInputPath], { allowFailure: true });
assert.notEqual(repoGraphSyncFromArtifact.code, 0);
assert.equal(repoGraphSyncFromArtifact.body.diagnostics[0].message, "expected Zero source file or package");
assert(!existsSync(join(repoGraphArtifactInputRoot, "zero.graph")));
writeFileSync(join(repoGraphArtifactInputRoot, "zero.graph"), readFileSync(standaloneRepoGraphStore));
const repoGraphVerifyArtifactWithStore = json(["verify-projection", "--json", repoGraphArtifactInputPath], { allowFailure: true });
assert.notEqual(repoGraphVerifyArtifactWithStore.code, 0);
assert.equal(repoGraphVerifyArtifactWithStore.body.diagnostics[0].message, "expected Zero source file or package");
const copiedRepoGraphRootA = join("/tmp", `zero-repo-graph-copied-a-${process.pid}`);
const copiedRepoGraphRootB = join("/tmp", `zero-repo-graph-copied-b-${process.pid}`);
rmSync(copiedRepoGraphRootA, { force: true, recursive: true });
rmSync(copiedRepoGraphRootB, { force: true, recursive: true });
mkdirSync(copiedRepoGraphRootA, { recursive: true });
mkdirSync(copiedRepoGraphRootB, { recursive: true });
const copiedRepoGraphSourceText = `pub fn main(world: World) -> Void raises {
    let total: i32 = 41 + 1
    if total == 42 {
        check world.out.write("copied graph source ok\\n")
    }
}
`;
writeFileSync(join(copiedRepoGraphRootA, "main.0"), copiedRepoGraphSourceText);
writeFileSync(join(copiedRepoGraphRootB, "main.0"), copiedRepoGraphSourceText);
const copiedRepoGraphSourceA = join(copiedRepoGraphRootA, "main.0");
const copiedRepoGraphSourceB = join(copiedRepoGraphRootB, "main.0");
json(["import", "--format", "text", "--json", copiedRepoGraphSourceA]);
writeFileSync(join(copiedRepoGraphRootB, "zero.graph"), readFileSync(join(copiedRepoGraphRootA, "zero.graph")));
const copiedRepoGraphVerify = json(["verify-projection", "--json", copiedRepoGraphSourceB]);
assert.equal(copiedRepoGraphVerify.code, 0);
assert.equal(copiedRepoGraphVerify.body.ok, true);
writeFileSync(copiedRepoGraphSourceB, readFileSync(copiedRepoGraphSourceB, "utf8").replace("copied graph source ok", "checker copied graph store"));
const copiedRepoGraphVerifyDrift = json(["verify-projection", "--json", copiedRepoGraphSourceB], { allowFailure: true });
assert.notEqual(copiedRepoGraphVerifyDrift.code, 0);
assert.equal(copiedRepoGraphVerifyDrift.body.diagnostics[0].code, "RGP006");
const identityRepoGraphRoot = join("/tmp", `zero-repo-graph-identity-${process.pid}`);
const identityRepoGraphSource = join(identityRepoGraphRoot, "main.0");
const identityRepoGraphStore = join(identityRepoGraphRoot, "zero.graph");
rmSync(identityRepoGraphRoot, { force: true, recursive: true });
mkdirSync(identityRepoGraphRoot, { recursive: true });
writeFileSync(
  identityRepoGraphSource,
  `use std.mem

pub fn main(world: World) -> Void raises {
    let text: String = "zero"
    if std.mem.len(text) == 4 {
        check world.out.write("identity ok\\n")
    }
}
`,
);
json(["import", "--format", "text", "--json", identityRepoGraphSource]);
const identityRepoGraphStoreText = readFileSync(identityRepoGraphStore, "utf8");
const identityRepoGraphMainId = identityRepoGraphStoreText.match(/^node (#[^ ]+) Function name:"main"/m)?.[1];
const identityRepoGraphMemImportId = identityRepoGraphStoreText.match(/^node (#[^ ]+) Import name:"std\.mem"/m)?.[1];
const identityRepoGraphLiteralId = identityRepoGraphStoreText.match(/^node (#[^ ]+) Literal [^\n]*value:"identity ok\\n"/m)?.[1];
assert(identityRepoGraphMainId);
assert(identityRepoGraphMemImportId);
assert(identityRepoGraphLiteralId);
writeFileSync(
  identityRepoGraphSource,
  `// human source edit
use std.str
use std.mem

pub fn main(world: World) -> Void raises {
    let label: String = "zero"
    let count: usize = std.mem.len(label)
    if count == 4 {
        // projection comment
        check world.out.write("identity updated\\n")
    }
}
`,
);
const identityRepoGraphSyncEdited = json(["import", "--format", "text", "--json", identityRepoGraphSource]);
assert.equal(identityRepoGraphSyncEdited.code, 0);
assert.equal(identityRepoGraphSyncEdited.body.repositoryGraph.projectionState, "clean");
const identityRepoGraphEditedStoreText = readFileSync(identityRepoGraphStore, "utf8");
assert(identityRepoGraphEditedStoreText.includes(`node ${identityRepoGraphMainId} Function name:"main"`));
assert(identityRepoGraphEditedStoreText.includes(`node ${identityRepoGraphMemImportId} Import name:"std.mem"`));
assert(identityRepoGraphEditedStoreText.includes(`node ${identityRepoGraphLiteralId} Literal type:"String" value:"identity updated\\n"`));
assert.match(identityRepoGraphEditedStoreText, /^node #[^ ]+ Import name:"std\.str"/m);
assert.match(identityRepoGraphEditedStoreText, /^projection path:"main\.0" text:"\/\/ human source edit\\n/m);
assert(identityRepoGraphEditedStoreText.includes("// projection comment"));
const identityRepoGraphSourceAfterSync = readFileSync(identityRepoGraphSource, "utf8");
const identityRepoGraphSyncProjection = json(["export", "--json", identityRepoGraphSource]);
assert.equal(identityRepoGraphSyncProjection.code, 0);
assert.deepEqual(identityRepoGraphSyncProjection.body.changedPaths, []);
assert.equal(readFileSync(identityRepoGraphSource, "utf8"), identityRepoGraphSourceAfterSync);
const deleteRepoGraphRoot = join("/tmp", `zero-repo-graph-delete-${process.pid}`);
const deleteRepoGraphSource = join(deleteRepoGraphRoot, "main.0");
const deleteRepoGraphStore = join(deleteRepoGraphRoot, "zero.graph");
rmSync(deleteRepoGraphRoot, { force: true, recursive: true });
mkdirSync(deleteRepoGraphRoot, { recursive: true });
const deleteRepoGraphOriginal = `fn alpha() -> i32 {
    return 1
}

fn beta() -> i32 {
    return 2
}

fn gamma() -> i32 {
    return 3
}

pub fn main(world: World) -> Void raises {
    check world.out.write("delete ok\\n")
}
`;
writeFileSync(deleteRepoGraphSource, deleteRepoGraphOriginal);
json(["import", "--format", "text", "--json", deleteRepoGraphSource]);
const deleteRepoGraphStoreBefore = readFileSync(deleteRepoGraphStore, "utf8");
const deleteRepoGraphAlphaId = deleteRepoGraphStoreBefore.match(/^node (#[^ ]+) Function name:"alpha"/m)?.[1];
const deleteRepoGraphBetaId = deleteRepoGraphStoreBefore.match(/^node (#[^ ]+) Function name:"beta"/m)?.[1];
const deleteRepoGraphGammaId = deleteRepoGraphStoreBefore.match(/^node (#[^ ]+) Function name:"gamma"/m)?.[1];
assert(deleteRepoGraphAlphaId);
assert(deleteRepoGraphBetaId);
assert(deleteRepoGraphGammaId);
writeFileSync(
  deleteRepoGraphSource,
  deleteRepoGraphOriginal.replace(
    `\nfn beta() -> i32 {
    return 2
}
`,
    "",
  ),
);
const deleteRepoGraphSync = json(["import", "--format", "text", "--json", deleteRepoGraphSource]);
assert.equal(deleteRepoGraphSync.code, 0);
assert.equal(deleteRepoGraphSync.body.repositoryGraph.projectionState, "clean");
const deleteRepoGraphStoreAfter = readFileSync(deleteRepoGraphStore, "utf8");
assert(deleteRepoGraphStoreAfter.includes(`node ${deleteRepoGraphAlphaId} Function name:"alpha"`));
assert(deleteRepoGraphStoreAfter.includes(`node ${deleteRepoGraphGammaId} Function name:"gamma"`));
assert(!deleteRepoGraphStoreAfter.includes(`node ${deleteRepoGraphBetaId} Function name:"beta"`));
const renameRepoGraphRoot = join("/tmp", `zero-repo-graph-rename-${process.pid}`);
const renameRepoGraphSource = join(renameRepoGraphRoot, "main.0");
const renameRepoGraphStore = join(renameRepoGraphRoot, "zero.graph");
rmSync(renameRepoGraphRoot, { force: true, recursive: true });
mkdirSync(renameRepoGraphRoot, { recursive: true });
const renameRepoGraphOriginal = `fn alpha() -> i32 {
    return 1
}

fn beta() -> i32 {
    return 2
}

pub fn main(world: World) -> Void raises {
    check world.out.write("rename ok\\n")
}
`;
writeFileSync(renameRepoGraphSource, renameRepoGraphOriginal);
json(["import", "--format", "text", "--json", renameRepoGraphSource]);
const renameRepoGraphStoreBefore = readFileSync(renameRepoGraphStore, "utf8");
const renameRepoGraphBetaId = renameRepoGraphStoreBefore.match(/^node (#[^ ]+) Function name:"beta"/m)?.[1];
assert(renameRepoGraphBetaId);
writeFileSync(renameRepoGraphSource, renameRepoGraphOriginal.replace("fn beta()", "fn gamma()"));
const renameRepoGraphSync = json(["import", "--format", "text", "--json", renameRepoGraphSource]);
assert.equal(renameRepoGraphSync.code, 0);
assert.equal(renameRepoGraphSync.body.repositoryGraph.projectionState, "clean");
assert(readFileSync(renameRepoGraphStore, "utf8").includes(`node ${renameRepoGraphBetaId} Function name:"gamma"`));
const moduleIdentityRepoGraphRoot = join("/tmp", `zero-repo-graph-module-identity-${process.pid}`);
const moduleIdentityRepoGraphSourceOne = join(moduleIdentityRepoGraphRoot, "one.0");
const moduleIdentityRepoGraphSourceTwo = join(moduleIdentityRepoGraphRoot, "two.0");
const moduleIdentityRepoGraphStore = join(moduleIdentityRepoGraphRoot, "zero.graph");
rmSync(moduleIdentityRepoGraphRoot, { force: true, recursive: true });
mkdirSync(moduleIdentityRepoGraphRoot, { recursive: true });
writeFileSync(
  moduleIdentityRepoGraphSourceOne,
  `pub fn main(world: World) -> Void raises {
    check world.out.write("module one\\n")
}
`,
);
json(["import", "--format", "text", "--json", moduleIdentityRepoGraphSourceOne]);
const moduleIdentityRepoGraphStoreBefore = readFileSync(moduleIdentityRepoGraphStore, "utf8");
renameSync(moduleIdentityRepoGraphSourceOne, moduleIdentityRepoGraphSourceTwo);
const moduleIdentityRepoGraphSync = json(["import", "--format", "text", "--json", moduleIdentityRepoGraphSourceTwo], { allowFailure: true });
assert.notEqual(moduleIdentityRepoGraphSync.code, 0);
assert.equal(moduleIdentityRepoGraphSync.body.diagnostics[0].code, "RGP007");
assert.equal(moduleIdentityRepoGraphSync.body.diagnostics[0].message, "repository graph source identity has a different module identity");
assert.equal(moduleIdentityRepoGraphSync.body.diagnostics[0].expected, "module:one");
assert.equal(moduleIdentityRepoGraphSync.body.diagnostics[0].actual, "module:two");
assert.match(moduleIdentityRepoGraphSync.body.diagnostics[0].help, /module rename/);
assert.equal(readFileSync(moduleIdentityRepoGraphStore, "utf8"), moduleIdentityRepoGraphStoreBefore);
const ambiguousRepoGraphRoot = join("/tmp", `zero-repo-graph-ambiguous-${process.pid}`);
const ambiguousRepoGraphSource = join(ambiguousRepoGraphRoot, "main.0");
const ambiguousRepoGraphStore = join(ambiguousRepoGraphRoot, "zero.graph");
rmSync(ambiguousRepoGraphRoot, { force: true, recursive: true });
mkdirSync(ambiguousRepoGraphRoot, { recursive: true });
writeFileSync(
  ambiguousRepoGraphSource,
  `fn add(a: i32, b: i32) -> i32 {
    return a + b
}

pub fn main(world: World) -> Void raises {
    let total: i32 = add(1, 2)
    if total == 3 {
        check world.out.write("ambiguous ok\\n")
    }
}
`,
);
json(["import", "--format", "text", "--json", ambiguousRepoGraphSource]);
const ambiguousRepoGraphStoreBefore = readFileSync(ambiguousRepoGraphStore, "utf8");
writeFileSync(ambiguousRepoGraphSource, readFileSync(ambiguousRepoGraphSource, "utf8").replace("add(1, 2)", "add(2, 1)"));
// The argument swap ties structurally, but it is a single-file edit whose
// function set still matches by name and signature, so import accepts the
// rewrite wholesale with regenerated node identities instead of RGP007.
const ambiguousRepoGraphSync = zeroWithStderr(["import", "--format", "text", "--json", ambiguousRepoGraphSource]);
assert.equal(ambiguousRepoGraphSync.code, 0);
assert.match(ambiguousRepoGraphSync.stderr, /note: file-scope rewrite accepted; node identities regenerated for main\.0/);
assert.equal(JSON.parse(ambiguousRepoGraphSync.stdout).repositoryGraph.projectionState, "clean");
assert.notEqual(readFileSync(ambiguousRepoGraphStore, "utf8"), ambiguousRepoGraphStoreBefore);
const rotatedRepoGraphRoot = join("/tmp", `zero-repo-graph-rotated-${process.pid}`);
const rotatedRepoGraphSource = join(rotatedRepoGraphRoot, "main.0");
const rotatedRepoGraphStore = join(rotatedRepoGraphRoot, "zero.graph");
rmSync(rotatedRepoGraphRoot, { force: true, recursive: true });
mkdirSync(rotatedRepoGraphRoot, { recursive: true });
writeFileSync(
  rotatedRepoGraphSource,
  `fn sum3(a: i32, b: i32, c: i32) -> i32 {
    return a + b + c
}

pub fn main(world: World) -> Void raises {
    let total: i32 = sum3(1, 2, 3)
    if total == 6 {
        check world.out.write("rotated ok\\n")
    }
}
`,
);
json(["import", "--format", "text", "--json", rotatedRepoGraphSource]);
const rotatedRepoGraphStoreBefore = readFileSync(rotatedRepoGraphStore, "utf8");
writeFileSync(rotatedRepoGraphSource, readFileSync(rotatedRepoGraphSource, "utf8").replace("sum3(1, 2, 3)", "sum3(2, 3, 1)"));
// Rotated identical arguments tie structurally; the single-file rewrite is
// accepted wholesale with regenerated identities instead of RGP007.
const rotatedRepoGraphSync = zeroWithStderr(["import", "--format", "text", "--json", rotatedRepoGraphSource]);
assert.equal(rotatedRepoGraphSync.code, 0);
assert.match(rotatedRepoGraphSync.stderr, /note: file-scope rewrite accepted; node identities regenerated for main\.0/);
assert.equal(JSON.parse(rotatedRepoGraphSync.stdout).repositoryGraph.projectionState, "clean");
assert.notEqual(readFileSync(rotatedRepoGraphStore, "utf8"), rotatedRepoGraphStoreBefore);
const ambiguousSiblingRepoGraphRoot = join("/tmp", `zero-repo-graph-ambiguous-sibling-${process.pid}`);
const ambiguousSiblingRepoGraphSource = join(ambiguousSiblingRepoGraphRoot, "main.0");
const ambiguousSiblingRepoGraphStore = join(ambiguousSiblingRepoGraphRoot, "zero.graph");
rmSync(ambiguousSiblingRepoGraphRoot, { force: true, recursive: true });
mkdirSync(ambiguousSiblingRepoGraphRoot, { recursive: true });
const ambiguousSiblingRepoGraphOriginal = `fn helper() -> i32 {
    return 1
}

pub fn main(world: World) -> Void raises {
    check world.out.write("sibling ok\\n")
}
`;
writeFileSync(ambiguousSiblingRepoGraphSource, ambiguousSiblingRepoGraphOriginal);
json(["import", "--format", "text", "--json", ambiguousSiblingRepoGraphSource]);
const ambiguousSiblingRepoGraphStoreBefore = readFileSync(ambiguousSiblingRepoGraphStore, "utf8");
const ambiguousSiblingRepoGraphHelperId = ambiguousSiblingRepoGraphStoreBefore.match(/^node (#[^ ]+) Function name:"helper"/m)?.[1];
assert(ambiguousSiblingRepoGraphHelperId);
writeFileSync(
  ambiguousSiblingRepoGraphSource,
  ambiguousSiblingRepoGraphOriginal.replace(
    "\npub fn main",
    "\nfn appendedHelper() -> i32 {\n    return 2\n}\n\npub fn main",
  ),
);
const ambiguousSiblingRepoGraphSync = json(["import", "--format", "text", "--json", ambiguousSiblingRepoGraphSource], { allowFailure: true });
assert.equal(ambiguousSiblingRepoGraphSync.code, 0);
assert.equal(ambiguousSiblingRepoGraphSync.body.repositoryGraph.projectionState, "clean");
const ambiguousSiblingRepoGraphStoreAfter = readFileSync(ambiguousSiblingRepoGraphStore, "utf8");
assert(ambiguousSiblingRepoGraphStoreAfter.includes(`node ${ambiguousSiblingRepoGraphHelperId} Function name:"helper"`));
const ambiguousSiblingRepoGraphAppendedId = ambiguousSiblingRepoGraphStoreAfter.match(/^node (#[^ ]+) Function name:"appendedHelper"/m)?.[1];
assert(ambiguousSiblingRepoGraphAppendedId);
assert.notEqual(ambiguousSiblingRepoGraphAppendedId, ambiguousSiblingRepoGraphHelperId);
const insertRunRepoGraphRoot = join("/tmp", `zero-repo-graph-insert-run-${process.pid}`);
const insertRunRepoGraphSource = join(insertRunRepoGraphRoot, "main.0");
const insertRunRepoGraphStore = join(insertRunRepoGraphRoot, "zero.graph");
rmSync(insertRunRepoGraphRoot, { force: true, recursive: true });
mkdirSync(insertRunRepoGraphRoot, { recursive: true });
const insertRunRepoGraphOriginal = `fn fill(out: MutSpan<u8>, n: usize) -> usize {
    var i: usize = 0
    var p: usize = 0
    while i < n {
        p = p + 1_usize
        i = i + 1_usize
    }
    return p
}

pub fn main(world: World) -> Void raises {
    check world.out.write("insert run ok\\n")
}
`;
writeFileSync(insertRunRepoGraphSource, insertRunRepoGraphOriginal);
json(["import", "--format", "text", "--json", insertRunRepoGraphSource]);
const insertRunRepoGraphStoreBefore = readFileSync(insertRunRepoGraphStore, "utf8");
const insertRunRepoGraphWhileId = insertRunRepoGraphStoreBefore.match(/^node (#[^ ]+) While/m)?.[1];
const insertRunRepoGraphReturnId = insertRunRepoGraphStoreBefore.match(/^node (#[^ ]+) Return path:"main\.0" line:8/m)?.[1];
assert(insertRunRepoGraphWhileId);
assert(insertRunRepoGraphReturnId);
assert(!insertRunRepoGraphWhileId.includes("-"));
writeFileSync(
  insertRunRepoGraphSource,
  insertRunRepoGraphOriginal.replace(
    "    while i < n {",
    "    var padded: usize = 0\n    while padded < n {\n        padded = padded + 1_usize\n    }\n    var spill: usize = padded\n    while i < n {",
  ),
);
const insertRunRepoGraphSync = json(["import", "--format", "text", "--json", insertRunRepoGraphSource]);
assert.equal(insertRunRepoGraphSync.code, 0);
assert.equal(insertRunRepoGraphSync.body.repositoryGraph.projectionState, "clean");
const insertRunRepoGraphStoreAfter = readFileSync(insertRunRepoGraphStore, "utf8");
assert(insertRunRepoGraphStoreAfter.includes(`node ${insertRunRepoGraphWhileId} While`));
assert(insertRunRepoGraphStoreAfter.includes(`node ${insertRunRepoGraphReturnId} Return`));
const insertRunRepoGraphWhileIds = [...insertRunRepoGraphStoreAfter.matchAll(/^node (#[^ ]+) While/gm)].map((m) => m[1]);
assert.equal(insertRunRepoGraphWhileIds.length, 2);
assert(insertRunRepoGraphWhileIds.includes(insertRunRepoGraphWhileId));
const ambiguousRunRepoGraphRoot = join("/tmp", `zero-repo-graph-ambiguous-run-${process.pid}`);
const ambiguousRunRepoGraphSource = join(ambiguousRunRepoGraphRoot, "main.0");
const ambiguousRunRepoGraphStore = join(ambiguousRunRepoGraphRoot, "zero.graph");
rmSync(ambiguousRunRepoGraphRoot, { force: true, recursive: true });
mkdirSync(ambiguousRunRepoGraphRoot, { recursive: true });
writeFileSync(ambiguousRunRepoGraphSource, insertRunRepoGraphOriginal);
json(["import", "--format", "text", "--json", ambiguousRunRepoGraphSource]);
const ambiguousRunRepoGraphStoreBefore = readFileSync(ambiguousRunRepoGraphStore, "utf8");
const ambiguousRunRepoGraphWhileId = ambiguousRunRepoGraphStoreBefore.match(/^node (#[^ ]+) While/m)?.[1];
assert(ambiguousRunRepoGraphWhileId);
writeFileSync(
  ambiguousRunRepoGraphSource,
  insertRunRepoGraphOriginal.replace(
    "    while i < n {",
    "    var padded: usize = 0\n    while padded < n {\n        padded = padded + 1_usize\n    }\n    while i <= n {",
  ),
);
// An inserted sibling plus an edit to the original statement used to fail RGP007;
// import now disambiguates by structural similarity and keeps the original handle
// on the statement that shares its body.
const ambiguousRunRepoGraphSync = zeroWithStderr(["import", "--format", "text", "--json", ambiguousRunRepoGraphSource]);
assert.equal(ambiguousRunRepoGraphSync.code, 0);
assert.match(ambiguousRunRepoGraphSync.stderr, /note: import matched 1 edited node to existing graph identities by structure/);
const ambiguousRunRepoGraphStoreAfter = readFileSync(ambiguousRunRepoGraphStore, "utf8");
const ambiguousRunRepoGraphResolvedLine = ambiguousRunRepoGraphStoreAfter
  .split("\n")
  .find((line) => line.startsWith(`node ${ambiguousRunRepoGraphWhileId} While`));
assert(ambiguousRunRepoGraphResolvedLine, "original while handle survives the auto-resolved import");
assert.match(ambiguousRunRepoGraphResolvedLine, /line:8/, "original while handle follows the statement that kept its body");
// Identical edited candidates at shifted orders stay genuinely ambiguous; the
// same edit also widens fill's signature, so the file-scope rewrite escape
// stays closed (function set changed) and the import keeps RGP007.
const tieRunRepoGraphRoot = join("/tmp", `zero-repo-graph-tie-run-${process.pid}`);
const tieRunRepoGraphSource = join(tieRunRepoGraphRoot, "main.0");
const tieRunRepoGraphStore = join(tieRunRepoGraphRoot, "zero.graph");
rmSync(tieRunRepoGraphRoot, { force: true, recursive: true });
mkdirSync(tieRunRepoGraphRoot, { recursive: true });
writeFileSync(tieRunRepoGraphSource, insertRunRepoGraphOriginal);
json(["import", "--format", "text", "--json", tieRunRepoGraphSource]);
const tieRunRepoGraphStoreBefore = readFileSync(tieRunRepoGraphStore, "utf8");
writeFileSync(
  tieRunRepoGraphSource,
  insertRunRepoGraphOriginal.replace(
    "    while i < n {\n        p = p + 1_usize\n        i = i + 1_usize\n    }\n",
    "    var shift: usize = 0\n    while i <= n {\n        p = p + 2_usize\n        i = i + 1_usize\n    }\n    while i <= n {\n        p = p + 2_usize\n        i = i + 1_usize\n    }\n",
  ).replace(
    "fn fill(out: MutSpan<u8>, n: usize) -> usize {",
    "fn fill(out: MutSpan<u8>, n: usize, pad: usize) -> usize {",
  ),
);
const tieRunRepoGraphSync = json(["import", "--format", "text", "--json", tieRunRepoGraphSource], { allowFailure: true });
assert.notEqual(tieRunRepoGraphSync.code, 0);
assert.equal(tieRunRepoGraphSync.body.diagnostics[0].code, "RGP007");
assert.equal(tieRunRepoGraphSync.body.diagnostics[0].message, "repository graph source identity is ambiguous");
assert.match(tieRunRepoGraphSync.body.diagnostics[0].actual, /matches 2 edited candidates at main\.0:\d+-\d+/);
assert.match(tieRunRepoGraphSync.body.diagnostics[0].help, /split the text edit: import the change touching main\.0:\d+-\d+ on its own first/);
assert.equal(readFileSync(tieRunRepoGraphStore, "utf8"), tieRunRepoGraphStoreBefore);
rmSync(tieRunRepoGraphRoot, { force: true, recursive: true });
// Whole-file rewrite escape: a single-file rewrite of a multi-function module
// whose function set still matches by name and signature imports in one pass;
// the file's node identities are regenerated and other files keep theirs.
const fileScopeRoot = join(outDir, "repository-graph-file-scope-rewrite");
const fileScopeStore = join(fileScopeRoot, "zero.graph");
rmSync(fileScopeRoot, { recursive: true, force: true });
mkdirSync(join(fileScopeRoot, "src"), { recursive: true });
writeZeroTomlSync(fileScopeRoot, { package: { name: "file-scope", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "src/main.0" } } });
const fileScopeUtilOriginal = [
  "pub fn fill(n: usize) -> usize {",
  "    var i: usize = 0",
  "    var p: usize = 0",
  "    while i < n {",
  "        p = p + 1_usize",
  "        i = i + 1_usize",
  "    }",
  "    return p",
  "}",
  "",
  "pub fn cap(n: usize) -> usize {",
  "    return n",
  "}",
  "",
].join("\n");
const fileScopeUtilRewrite = [
  "pub fn fill(n: usize) -> usize {",
  "    var i: usize = 0",
  "    var p: usize = 0",
  "    var shift: usize = 0",
  "    while i <= n {",
  "        p = p + 2_usize",
  "        i = i + 1_usize",
  "    }",
  "    while i <= n {",
  "        p = p + 2_usize",
  "        i = i + 1_usize",
  "    }",
  "    return p + shift",
  "}",
  "",
  "pub fn cap(n: usize) -> usize {",
  "    return n + 0_usize",
  "}",
  "",
].join("\n");
const fileScopeMainOriginal = [
  "use util",
  "",
  "pub fn main(world: World) -> Void raises {",
  "    if fill(2) >= cap(1) {",
  '        check world.out.write("file scope ok\\n")',
  "    }",
  "}",
  "",
].join("\n");
writeFileSync(join(fileScopeRoot, "src", "util.0"), fileScopeUtilOriginal);
writeFileSync(join(fileScopeRoot, "src", "main.0"), fileScopeMainOriginal);
assert.equal(json(["import", "--format", "text", "--json", fileScopeRoot]).code, 0);
assert.equal(zero(["check", fileScopeRoot]).stdout, "ok\n");
const fileScopeStoreBefore = readFileSync(fileScopeStore, "utf8");
const fileScopeMainId = fileScopeStoreBefore.match(/^node (#[^ ]+) Function name:"main"/m)?.[1];
assert(fileScopeMainId, "expected a main Function node in the file-scope store");
writeFileSync(join(fileScopeRoot, "src", "util.0"), fileScopeUtilRewrite);
const fileScopeSync = zeroWithStderr(["import", "--format", "text", "--json", fileScopeRoot]);
assert.equal(fileScopeSync.code, 0);
assert.match(fileScopeSync.stderr, /note: file-scope rewrite accepted; node identities regenerated for src\/util\.0/);
assert.equal(JSON.parse(fileScopeSync.stdout).repositoryGraph.projectionState, "clean");
const fileScopeStoreAfter = readFileSync(fileScopeStore, "utf8");
assert(fileScopeStoreAfter.includes(`node ${fileScopeMainId} Function name:"main"`), "untouched files keep their node identities");
assert.equal(zero(["check", fileScopeRoot]).stdout, "ok\n");
// Handles and queries resolve against the regenerated identities in one pass.
const fileScopeHandlesView = zero(["view", "--fn", "fill", "--handles", fileScopeRoot]).stdout;
const fileScopeReturnLine = fileScopeHandlesView.split("\n").find((line) => line.includes("return p + shift"));
assert(fileScopeReturnLine, "expected the rewritten return row in the handles view");
const fileScopeHandle = fileScopeReturnLine.match(/\/\/ (#[0-9a-z_.]+)/)?.[1];
assert(fileScopeHandle, "expected a patch handle on the rewritten return row");
const fileScopeQueryHandles = json(["query", "--json", "--fn", "fill", "--handles", fileScopeRoot]).body;
assert.equal(fileScopeQueryHandles.ok, true);
const fileScopePatch = json(["patch", "--json", fileScopeRoot, "--op", `replaceExpr node="${fileScopeHandle}" with="p + shift + 0_usize"`]).body;
assert.equal(fileScopePatch.ok, true);
assert.match(zero(["view", "--fn", "fill", fileScopeRoot]).stdout, /return p \+ shift \+ 0_usize/);
// Cross-file ambiguity keeps RGP007: the same tie-producing rewrite plus an
// edit in a second file is not a file-scope rewrite.
writeFileSync(join(fileScopeRoot, "src", "util.0"), fileScopeUtilOriginal);
writeFileSync(join(fileScopeRoot, "src", "main.0"), fileScopeMainOriginal);
assert.equal(json(["import", "--format", "text", "--json", fileScopeRoot]).code, 0);
const crossFileStoreBefore = readFileSync(fileScopeStore, "utf8");
writeFileSync(join(fileScopeRoot, "src", "util.0"), fileScopeUtilRewrite);
writeFileSync(join(fileScopeRoot, "src", "main.0"), fileScopeMainOriginal.replace("file scope ok", "cross file ok"));
const crossFileSync = json(["import", "--format", "text", "--json", fileScopeRoot], { allowFailure: true });
assert.notEqual(crossFileSync.code, 0);
assert.equal(crossFileSync.body.diagnostics[0].code, "RGP007");
assert.equal(crossFileSync.body.diagnostics[0].message, "repository graph source identity is ambiguous");
assert.equal(readFileSync(fileScopeStore, "utf8"), crossFileStoreBefore);
const mergeRepoGraphRoot = join("/tmp", `zero-repo-graph-merge-${process.pid}`);
const mergeRepoGraphSource = join(mergeRepoGraphRoot, "main.0");
const mergeRepoGraphStore = join(mergeRepoGraphRoot, "zero.graph");
rmSync(mergeRepoGraphRoot, { force: true, recursive: true });
mkdirSync(mergeRepoGraphRoot, { recursive: true });
const mergeRepoGraphOriginal = `fn alpha() -> i32 {
    return 1
}

fn beta() -> i32 {
    return 2
}

pub fn main(world: World) -> Void raises {
    check world.out.write("merge ok\\n")
}
`;
writeFileSync(mergeRepoGraphSource, mergeRepoGraphOriginal);
json(["import", "--format", "text", "--json", mergeRepoGraphSource]);
writeFileSync(join(mergeRepoGraphRoot, "base.graph"), readFileSync(mergeRepoGraphStore, "utf8"));
writeFileSync(mergeRepoGraphSource, mergeRepoGraphOriginal.replace("return 1", "return 10"));
json(["import", "--format", "text", "--json", mergeRepoGraphSource]);
writeFileSync(join(mergeRepoGraphRoot, "left.graph"), readFileSync(mergeRepoGraphStore, "utf8"));
writeFileSync(mergeRepoGraphStore, readFileSync(join(mergeRepoGraphRoot, "base.graph"), "utf8"));
writeFileSync(mergeRepoGraphSource, mergeRepoGraphOriginal.replace("return 2", "return 20"));
json(["import", "--format", "text", "--json", mergeRepoGraphSource]);
writeFileSync(join(mergeRepoGraphRoot, "right.graph"), readFileSync(mergeRepoGraphStore, "utf8"));
const mergeRepoGraphResult = json([
  "merge",
  "--json",
  "--base",
  join(mergeRepoGraphRoot, "base.graph"),
  "--left",
  join(mergeRepoGraphRoot, "left.graph"),
  "--right",
  join(mergeRepoGraphRoot, "right.graph"),
  mergeRepoGraphRoot,
]);
assert.equal(mergeRepoGraphResult.code, 0);
assert.equal(mergeRepoGraphResult.body.mode, "merge");
assert.equal(mergeRepoGraphResult.body.writes, true);
assert.equal(mergeRepoGraphResult.body.repositoryGraph.projectionState, "source-stale");
assert.equal(mergeRepoGraphResult.body.merge.conflicts, 0);
assert.equal(mergeRepoGraphResult.body.merge.leftChanges, 1);
assert.equal(mergeRepoGraphResult.body.merge.rightChanges, 1);
assert.equal(mergeRepoGraphResult.body.merge.target, mergeRepoGraphStore);
assert.deepEqual(mergeRepoGraphResult.body.changedPaths, [mergeRepoGraphStore]);
const mergeRepoGraphText = readFileSync(mergeRepoGraphStore, "utf8");
assert(mergeRepoGraphText.includes('value:"10"'));
assert(mergeRepoGraphText.includes('value:"20"'));
const mergeRepoGraphSyncProjection = json(["export", "--json", mergeRepoGraphSource]);
assert.equal(mergeRepoGraphSyncProjection.body.repositoryGraph.projectionState, "clean");
assert(readFileSync(mergeRepoGraphSource, "utf8").includes("return 10"));
assert(readFileSync(mergeRepoGraphSource, "utf8").includes("return 20"));
const mergeStaleProjectionRoot = join("/tmp", `zero-repo-graph-merge-stale-projection-${process.pid}`);
const mergeStaleProjectionSource = join(mergeStaleProjectionRoot, "main.0");
const mergeStaleProjectionStore = join(mergeStaleProjectionRoot, "zero.graph");
rmSync(mergeStaleProjectionRoot, { force: true, recursive: true });
mkdirSync(mergeStaleProjectionRoot, { recursive: true });
writeFileSync(mergeStaleProjectionSource, mergeRepoGraphOriginal);
json(["import", "--format", "text", "--json", mergeStaleProjectionSource]);
writeFileSync(join(mergeStaleProjectionRoot, "base.graph"), readFileSync(mergeStaleProjectionStore, "utf8"));
writeFileSync(join(mergeStaleProjectionRoot, "right.graph"), readFileSync(mergeStaleProjectionStore, "utf8"));
writeFileSync(mergeStaleProjectionSource, mergeRepoGraphOriginal.replace("return 1", "return 10"));
json(["import", "--format", "text", "--json", mergeStaleProjectionSource]);
writeFileSync(
  join(mergeStaleProjectionRoot, "left.graph"),
  readFileSync(mergeStaleProjectionStore, "utf8").replace("return 10", "return 999"),
);
writeFileSync(mergeStaleProjectionStore, readFileSync(join(mergeStaleProjectionRoot, "base.graph"), "utf8"));
const mergeStaleProjection = json([
  "merge",
  "--json",
  "--base",
  join(mergeStaleProjectionRoot, "base.graph"),
  "--left",
  join(mergeStaleProjectionRoot, "left.graph"),
  "--right",
  join(mergeStaleProjectionRoot, "right.graph"),
  mergeStaleProjectionRoot,
]);
assert.equal(mergeStaleProjection.code, 0);
assert.equal(mergeStaleProjection.body.merge.conflicts, 0);
const mergeStaleProjectionText = readFileSync(mergeStaleProjectionStore, "utf8");
assert(mergeStaleProjectionText.includes('value:"10"'));
assert(!mergeStaleProjectionText.includes('value:"999"'));
const mergeProjectionOnlyConflictRoot = join("/tmp", `zero-repo-graph-merge-projection-only-conflict-${process.pid}`);
const mergeProjectionOnlyConflictSource = join(mergeProjectionOnlyConflictRoot, "main.0");
const mergeProjectionOnlyConflictStore = join(mergeProjectionOnlyConflictRoot, "zero.graph");
rmSync(mergeProjectionOnlyConflictRoot, { force: true, recursive: true });
mkdirSync(mergeProjectionOnlyConflictRoot, { recursive: true });
const mergeProjectionOnlyConflictOriginal = `// base comment
fn alpha() -> i32 {
    return 1
}

pub fn main(world: World) -> Void raises {
    check world.out.write("merge projection-only conflict ok\\n")
}
`;
writeFileSync(mergeProjectionOnlyConflictSource, mergeProjectionOnlyConflictOriginal);
json(["import", "--format", "text", "--json", mergeProjectionOnlyConflictSource]);
writeFileSync(join(mergeProjectionOnlyConflictRoot, "base.graph"), readFileSync(mergeProjectionOnlyConflictStore, "utf8"));
writeFileSync(mergeProjectionOnlyConflictSource, mergeProjectionOnlyConflictOriginal.replace("return 1", "return 10"));
json(["import", "--format", "text", "--json", mergeProjectionOnlyConflictSource]);
writeFileSync(join(mergeProjectionOnlyConflictRoot, "left.graph"), readFileSync(mergeProjectionOnlyConflictStore, "utf8"));
writeFileSync(mergeProjectionOnlyConflictStore, readFileSync(join(mergeProjectionOnlyConflictRoot, "base.graph"), "utf8"));
writeFileSync(mergeProjectionOnlyConflictSource, mergeProjectionOnlyConflictOriginal.replace("base comment", "right comment"));
json(["import", "--format", "text", "--json", mergeProjectionOnlyConflictSource]);
writeFileSync(join(mergeProjectionOnlyConflictRoot, "right.graph"), readFileSync(mergeProjectionOnlyConflictStore, "utf8"));
writeFileSync(mergeProjectionOnlyConflictStore, readFileSync(join(mergeProjectionOnlyConflictRoot, "base.graph"), "utf8"));
const mergeProjectionOnlyConflict = json([
  "merge",
  "--json",
  "--base",
  join(mergeProjectionOnlyConflictRoot, "base.graph"),
  "--left",
  join(mergeProjectionOnlyConflictRoot, "left.graph"),
  "--right",
  join(mergeProjectionOnlyConflictRoot, "right.graph"),
  mergeProjectionOnlyConflictRoot,
], { allowFailure: true });
assert.notEqual(mergeProjectionOnlyConflict.code, 0);
assert.equal(mergeProjectionOnlyConflict.body.writes, false);
assert.equal(mergeProjectionOnlyConflict.body.diagnostics[0].code, "RGM005");
assert.equal(mergeProjectionOnlyConflict.body.diagnostics[0].message, "repository graph projection-only edit conflicts with graph changes");
assert.equal(mergeProjectionOnlyConflict.body.merge.conflicts, 1);
const mergeProjectionOnlyConflictText = readFileSync(mergeProjectionOnlyConflictStore, "utf8");
assert(mergeProjectionOnlyConflictText.includes("base comment"));
assert(!mergeProjectionOnlyConflictText.includes("right comment"));
assert(!mergeProjectionOnlyConflictText.includes('value:"10"'));
const mergeDeleteShiftRoot = join("/tmp", `zero-repo-graph-merge-delete-shift-${process.pid}`);
const mergeDeleteShiftSource = join(mergeDeleteShiftRoot, "main.0");
const mergeDeleteShiftStore = join(mergeDeleteShiftRoot, "zero.graph");
rmSync(mergeDeleteShiftRoot, { force: true, recursive: true });
mkdirSync(mergeDeleteShiftRoot, { recursive: true });
const mergeDeleteShiftOriginal = `fn alpha() -> i32 {
    return 1
}

fn beta() -> i32 {
    return 2
}

pub fn main(world: World) -> Void raises {
    check world.out.write("merge delete shift ok\\n")
}
`;
writeFileSync(mergeDeleteShiftSource, mergeDeleteShiftOriginal);
json(["import", "--format", "text", "--json", mergeDeleteShiftSource]);
writeFileSync(join(mergeDeleteShiftRoot, "base.graph"), readFileSync(mergeDeleteShiftStore, "utf8"));
writeFileSync(mergeDeleteShiftSource, mergeDeleteShiftOriginal.replace(/fn alpha\(\) -> i32 \{\n    return 1\n\}\n\n/, ""));
json(["import", "--format", "text", "--json", mergeDeleteShiftSource]);
writeFileSync(join(mergeDeleteShiftRoot, "left.graph"), readFileSync(mergeDeleteShiftStore, "utf8"));
writeFileSync(mergeDeleteShiftStore, readFileSync(join(mergeDeleteShiftRoot, "base.graph"), "utf8"));
writeFileSync(mergeDeleteShiftSource, mergeDeleteShiftOriginal.replace("return 2", "return 20"));
json(["import", "--format", "text", "--json", mergeDeleteShiftSource]);
writeFileSync(join(mergeDeleteShiftRoot, "right.graph"), readFileSync(mergeDeleteShiftStore, "utf8"));
writeFileSync(mergeDeleteShiftStore, readFileSync(join(mergeDeleteShiftRoot, "base.graph"), "utf8"));
writeFileSync(mergeDeleteShiftSource, mergeDeleteShiftOriginal);
const mergeDeleteShift = json([
  "merge",
  "--json",
  "--base",
  join(mergeDeleteShiftRoot, "base.graph"),
  "--left",
  join(mergeDeleteShiftRoot, "left.graph"),
  "--right",
  join(mergeDeleteShiftRoot, "right.graph"),
  mergeDeleteShiftRoot,
]);
assert.equal(mergeDeleteShift.code, 0);
assert.equal(mergeDeleteShift.body.merge.conflicts, 0);
const mergeDeleteShiftText = readFileSync(mergeDeleteShiftStore, "utf8");
assert(!mergeDeleteShiftText.includes('name:"alpha"'));
assert(mergeDeleteShiftText.includes('name:"beta"'));
assert(mergeDeleteShiftText.includes('value:"20"'));
const mergeDeleteShiftSyncProjection = json(["export", "--json", mergeDeleteShiftSource]);
assert.equal(mergeDeleteShiftSyncProjection.body.repositoryGraph.projectionState, "clean");
assert(!readFileSync(mergeDeleteShiftSource, "utf8").includes("fn alpha"));
assert(readFileSync(mergeDeleteShiftSource, "utf8").includes("return 20"));
const mergeBothDeleteRoot = join("/tmp", `zero-repo-graph-merge-both-delete-${process.pid}`);
const mergeBothDeleteSource = join(mergeBothDeleteRoot, "main.0");
const mergeBothDeleteStore = join(mergeBothDeleteRoot, "zero.graph");
rmSync(mergeBothDeleteRoot, { force: true, recursive: true });
mkdirSync(mergeBothDeleteRoot, { recursive: true });
writeFileSync(mergeBothDeleteSource, mergeDeleteShiftOriginal);
json(["import", "--format", "text", "--json", mergeBothDeleteSource]);
writeFileSync(join(mergeBothDeleteRoot, "base.graph"), readFileSync(mergeBothDeleteStore, "utf8"));
const mergeBothDeleteEdited = mergeDeleteShiftOriginal.replace(/fn alpha\(\) -> i32 \{\n    return 1\n\}\n\n/, "");
writeFileSync(mergeBothDeleteSource, mergeBothDeleteEdited);
json(["import", "--format", "text", "--json", mergeBothDeleteSource]);
writeFileSync(join(mergeBothDeleteRoot, "left.graph"), readFileSync(mergeBothDeleteStore, "utf8"));
writeFileSync(mergeBothDeleteStore, readFileSync(join(mergeBothDeleteRoot, "base.graph"), "utf8"));
writeFileSync(mergeBothDeleteSource, mergeBothDeleteEdited);
json(["import", "--format", "text", "--json", mergeBothDeleteSource]);
writeFileSync(join(mergeBothDeleteRoot, "right.graph"), readFileSync(mergeBothDeleteStore, "utf8"));
writeFileSync(mergeBothDeleteStore, readFileSync(join(mergeBothDeleteRoot, "base.graph"), "utf8"));
const mergeBothDelete = json([
  "merge",
  "--json",
  "--base",
  join(mergeBothDeleteRoot, "base.graph"),
  "--left",
  join(mergeBothDeleteRoot, "left.graph"),
  "--right",
  join(mergeBothDeleteRoot, "right.graph"),
  mergeBothDeleteRoot,
]);
assert.equal(mergeBothDelete.code, 0);
assert.equal(mergeBothDelete.body.merge.conflicts, 0);
const mergeBothDeleteText = readFileSync(mergeBothDeleteStore, "utf8");
assert(!mergeBothDeleteText.includes('name:"alpha"'));
assert(mergeBothDeleteText.includes('name:"beta"'));
const mergeBothDeleteSyncProjection = json(["export", "--json", mergeBothDeleteSource]);
assert.equal(mergeBothDeleteSyncProjection.body.repositoryGraph.projectionState, "clean");
assert(!readFileSync(mergeBothDeleteSource, "utf8").includes("fn alpha"));
const mergeNoopProjectionRoot = join("/tmp", `zero-repo-graph-merge-noop-projection-${process.pid}`);
const mergeNoopProjectionSource = join(mergeNoopProjectionRoot, "main.0");
const mergeNoopProjectionStore = join(mergeNoopProjectionRoot, "zero.graph");
rmSync(mergeNoopProjectionRoot, { force: true, recursive: true });
mkdirSync(mergeNoopProjectionRoot, { recursive: true });
writeFileSync(
  mergeNoopProjectionSource,
  `// keep projection comment
fn alpha() -> i32 {
    return 1
}

pub fn main(world: World) -> Void raises {
    check world.out.write("merge no-op projection ok\\n")
}
`,
);
json(["import", "--format", "text", "--json", mergeNoopProjectionSource]);
writeFileSync(join(mergeNoopProjectionRoot, "base.graph"), readFileSync(mergeNoopProjectionStore, "utf8"));
writeFileSync(join(mergeNoopProjectionRoot, "left.graph"), readFileSync(mergeNoopProjectionStore, "utf8"));
writeFileSync(join(mergeNoopProjectionRoot, "right.graph"), readFileSync(mergeNoopProjectionStore, "utf8"));
const mergeNoopProjection = json([
  "merge",
  "--json",
  "--base",
  join(mergeNoopProjectionRoot, "base.graph"),
  "--left",
  join(mergeNoopProjectionRoot, "left.graph"),
  "--right",
  join(mergeNoopProjectionRoot, "right.graph"),
  mergeNoopProjectionRoot,
]);
assert.equal(mergeNoopProjection.code, 0);
assert.equal(mergeNoopProjection.body.repositoryGraph.projectionState, "clean");
assert(readFileSync(mergeNoopProjectionStore, "utf8").includes("keep projection comment"));
const mergeNoopProjectionVerify = json(["verify-projection", "--json", mergeNoopProjectionSource]);
assert.equal(mergeNoopProjectionVerify.body.ok, true);
const mergeTargetRoot = join("/tmp", `zero-repo-graph-merge-target-${process.pid}`);
const mergeTargetSource = join(mergeTargetRoot, "main.0");
const mergeTargetStore = join(mergeTargetRoot, "zero.graph");
rmSync(mergeTargetRoot, { force: true, recursive: true });
mkdirSync(mergeTargetRoot, { recursive: true });
writeFileSync(mergeTargetSource, readFileSync("conformance/native/pass/meta-typed-target-type.0", "utf8"));
json(["import", "--format", "text", "--json", "--target", "linux-musl-x64", mergeTargetSource]);
const mergeTargetBefore = readFileSync(mergeTargetStore, "utf8");
writeFileSync(join(mergeTargetRoot, "base.graph"), mergeTargetBefore);
writeFileSync(join(mergeTargetRoot, "left.graph"), mergeTargetBefore);
writeFileSync(join(mergeTargetRoot, "right.graph"), mergeTargetBefore);
const mergeTarget = json([
  "merge",
  "--json",
  "--target",
  "linux-musl-x64",
  "--base",
  join(mergeTargetRoot, "base.graph"),
  "--left",
  join(mergeTargetRoot, "left.graph"),
  "--right",
  join(mergeTargetRoot, "right.graph"),
  mergeTargetRoot,
]);
assert.equal(mergeTarget.code, 0);
assert.equal(mergeTarget.body.merge.conflicts, 0);
assert.equal(readFileSync(mergeTargetStore, "utf8"), mergeTargetBefore);
const mergeTargetVerify = json(["verify-projection", "--json", "--target", "linux-musl-x64", mergeTargetSource]);
assert.equal(mergeTargetVerify.body.ok, true);
const mergePathMoveRoot = join("/tmp", `zero-repo-graph-merge-path-move-${process.pid}`);
const mergePathMoveSource = join(mergePathMoveRoot, "main.0");
const mergePathMoveRenamedSource = join(mergePathMoveRoot, "renamed.0");
const mergePathMoveStore = join(mergePathMoveRoot, "zero.graph");
rmSync(mergePathMoveRoot, { force: true, recursive: true });
mkdirSync(mergePathMoveRoot, { recursive: true });
writeZeroTomlSync(mergePathMoveRoot, { package: { name: "merge-path-move", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "main.0" } } });
writeFileSync(
  mergePathMoveSource,
  `fn alpha() -> i32 {
    return 1
}

pub fn main(world: World) -> Void raises {
    check world.out.write("merge path move ok\\n")
}
`,
);
json(["import", "--format", "text", "--json", mergePathMoveRoot]);
writeFileSync(join(mergePathMoveRoot, "base.graph"), readFileSync(mergePathMoveStore, "utf8"));
writeFileSync(join(mergePathMoveRoot, "right.graph"), readFileSync(mergePathMoveStore, "utf8"));
renameSync(mergePathMoveSource, mergePathMoveRenamedSource);
writeZeroTomlSync(mergePathMoveRoot, { package: { name: "merge-path-move", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "renamed.0" } } });
json(["import", "--format", "text", "--json", mergePathMoveRoot]);
writeFileSync(join(mergePathMoveRoot, "left.graph"), readFileSync(mergePathMoveStore, "utf8"));
const mergePathMove = json([
  "merge",
  "--json",
  "--base",
  join(mergePathMoveRoot, "base.graph"),
  "--left",
  join(mergePathMoveRoot, "left.graph"),
  "--right",
  join(mergePathMoveRoot, "right.graph"),
  mergePathMoveRoot,
]);
assert.equal(mergePathMove.code, 0);
const mergePathMoveText = readFileSync(mergePathMoveStore, "utf8");
assert(mergePathMoveText.includes('source path:"renamed.0"'));
assert(mergePathMoveText.includes('projection path:"renamed.0"'));
assert(!mergePathMoveText.includes('source path:"main.0"'));
assert(!mergePathMoveText.includes('projection path:"main.0"'));
const mergePathMoveVerify = json(["verify-projection", "--json", mergePathMoveRoot]);
assert.equal(mergePathMoveVerify.body.ok, true);
const mergeMissingInput = json(["merge", "--json", "--base", join(mergeRepoGraphRoot, "base.graph"), "--left", join(mergeRepoGraphRoot, "left.graph"), mergeRepoGraphRoot], { allowFailure: true });
assert.notEqual(mergeMissingInput.code, 0);
assert.equal(mergeMissingInput.body.diagnostics[0].code, "RGM001");
assert.equal(mergeMissingInput.body.diagnostics[0].actual, "missing --right");
const mergeConflictRepoGraphRoot = join("/tmp", `zero-repo-graph-merge-conflict-${process.pid}`);
const mergeConflictRepoGraphSource = join(mergeConflictRepoGraphRoot, "main.0");
const mergeConflictRepoGraphStore = join(mergeConflictRepoGraphRoot, "zero.graph");
rmSync(mergeConflictRepoGraphRoot, { force: true, recursive: true });
mkdirSync(mergeConflictRepoGraphRoot, { recursive: true });
const mergeConflictOriginal = `fn alpha() -> i32 {
    return 1
}

pub fn main(world: World) -> Void raises {
    check world.out.write("merge conflict ok\\n")
}
`;
writeFileSync(mergeConflictRepoGraphSource, mergeConflictOriginal);
json(["import", "--format", "text", "--json", mergeConflictRepoGraphSource]);
writeFileSync(join(mergeConflictRepoGraphRoot, "base.graph"), readFileSync(mergeConflictRepoGraphStore, "utf8"));
writeFileSync(mergeConflictRepoGraphSource, mergeConflictOriginal.replace("return 1", "return 10"));
json(["import", "--format", "text", "--json", mergeConflictRepoGraphSource]);
writeFileSync(join(mergeConflictRepoGraphRoot, "left.graph"), readFileSync(mergeConflictRepoGraphStore, "utf8"));
writeFileSync(mergeConflictRepoGraphStore, readFileSync(join(mergeConflictRepoGraphRoot, "base.graph"), "utf8"));
writeFileSync(mergeConflictRepoGraphSource, mergeConflictOriginal.replace("return 1", "return 20"));
json(["import", "--format", "text", "--json", mergeConflictRepoGraphSource]);
writeFileSync(join(mergeConflictRepoGraphRoot, "right.graph"), readFileSync(mergeConflictRepoGraphStore, "utf8"));
const mergeConflict = json([
  "merge",
  "--json",
  "--base",
  join(mergeConflictRepoGraphRoot, "base.graph"),
  "--left",
  join(mergeConflictRepoGraphRoot, "left.graph"),
  "--right",
  join(mergeConflictRepoGraphRoot, "right.graph"),
  mergeConflictRepoGraphRoot,
], { allowFailure: true });
assert.notEqual(mergeConflict.code, 0);
assert.equal(mergeConflict.body.mode, "merge");
assert.equal(mergeConflict.body.writes, false);
assert.equal(mergeConflict.body.diagnostics[0].code, "RGM002");
assert.equal(mergeConflict.body.diagnostics[0].message, "repository graph node was changed on both sides");
assert.equal(mergeConflict.body.diagnostics[0].related[0].kind, "graphNode");
assert.equal(mergeConflict.body.diagnostics[0].related[1].kind, "semanticObject");
assert.equal(mergeConflict.body.diagnostics[0].related[2].kind, "field");
assert.equal(mergeConflict.body.merge.conflicts, 1);
assert(readFileSync(mergeConflictRepoGraphStore, "utf8").includes('value:"20"'));
writeFileSync(standaloneRepoGraphSource, readFileSync(standaloneRepoGraphSource, "utf8").replace("hello from zero", "hello from graph store"));
const standaloneRepoGraphVerifyDrift = json(["verify-projection", "--json", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(standaloneRepoGraphVerifyDrift.code, 0);
assert.equal(standaloneRepoGraphVerifyDrift.body.diagnostics[0].code, "RGP006");
assert.deepEqual(standaloneRepoGraphVerifyDrift.body.repairCommands, [`zero import ${resolve(standaloneRepoGraphSource)}`, `zero export ${resolve(standaloneRepoGraphSource)}`]);
const standaloneRepoGraphVerifyDriftText = zero(["verify-projection", resolve(standaloneRepoGraphSource)], { allowFailure: true });
assert.notEqual(standaloneRepoGraphVerifyDriftText.code, 0);
assert.match(standaloneRepoGraphVerifyDriftText.stderr, new RegExp(`repair: zero import ${resolve(standaloneRepoGraphSource).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
assert.match(standaloneRepoGraphVerifyDriftText.stderr, new RegExp(`repair: zero export ${resolve(standaloneRepoGraphSource).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
const repositoryGraphVerifyDriftScript = repositoryGraphVerifyScript(standaloneRepoGraphRoot, { allowFailure: true });
assert.notEqual(repositoryGraphVerifyDriftScript.code, 0);
assert.match(repositoryGraphVerifyDriftScript.stderr, /repository graph verify-projection failed/);
assert.match(repositoryGraphVerifyDriftScript.stderr, /repair: zero import/);
assert.match(repositoryGraphVerifyDriftScript.stderr, /repair: zero export/);
const repoGraphVerify = json(["verify-projection", "--json", "."], { allowFailure: true });
assert.notEqual(repoGraphVerify.code, 0);
assert.equal(repoGraphVerify.body.ok, false);
assert.equal(repoGraphVerify.body.mode, "verify-projection");
assert.equal(repoGraphVerify.body.writes, false);
assert.equal(repoGraphVerify.body.diagnostics[0].code, "RGP001");
assert.equal(repoGraphVerify.body.diagnostics[0].actual, "missing zero.graph");
assert(repoGraphVerify.body.repairCommands.includes("zero import ."));
assert(!repoGraphVerify.body.repairCommands.includes("zero export ."));
const repoGraphExport = json(["export", "--json", "."], { allowFailure: true });
assert.notEqual(repoGraphExport.code, 0);
assert.equal(repoGraphExport.body.mode, "export");
assert.equal(repoGraphExport.body.writes, false);
assert.equal(repoGraphExport.body.diagnostics[0].code, "RGP001");
assert.doesNotMatch(graphDump, /node id=/);
const graphDumpJson = json(["dump", "--json", "examples/hello.0"]).body;
assert.equal(graphDumpJson.schemaVersion, 1);
assert.equal(graphDumpJson.moduleIdentity, "module:hello");
assert.equal(graphDumpJson.validation.ok, true);
assert.match(graphDumpJson.graphHash, /^graph:[0-9a-f]{16}$/);
const graphNodeBy = (predicate) => {
  const node = graphDumpJson.nodes.find(predicate);
  assert(node);
  return node;
};
const graphModuleNode = graphNodeBy((node) => node.kind === "Module" && node.name === "hello");
const graphMainFunctionNode = graphNodeBy((node) => node.kind === "Function" && node.name === "main");
const graphMainBodyNode = graphNodeBy((node) => node.kind === "Block" && node.name === "body");
const graphHelloLiteralNode = graphNodeBy((node) => node.kind === "Literal" && node.type === "String" && node.value === "hello from zero\n");
assert.match(graphModuleNode.id, /^#mod_[0-9a-f]{8}$/);
assert.equal(graphModuleNode.symbolId, "symbol:hello::module");
assert.equal(graphMainFunctionNode.symbolId, "symbol:hello::value.main");
assert.doesNotMatch(graphDump, /node #000001/);
assert(graphDumpJson.edges.some((edge) => edge.from === graphModuleNode.id && edge.to === graphMainFunctionNode.id && edge.kind === "function" && edge.order === 0));
const graphDumpPath = join(outDir, "hello.program-graph");
const graphDumpBinaryPath = join(outDir, "hello.binary.graph");
const graphDumpJsonPath = join(outDir, "hello.dump-json.program-graph");
const graphStableSiblingSourcePath = join(outDir, "hello-stable-sibling.0");
const graphImportPath = join(outDir, "hello.imported.program-graph");
const graphImportJsonPath = join(outDir, "hello.imported-json.program-graph");
const graphImportDirectorySourcePath = join(outDir, "hello-source-directory.0");
const graphImportDirectoryOutPath = join(outDir, "hello-source-directory.program-graph");
const graphInspectOutPath = join(outDir, "hello.inspect.json");
const graphSourceMapOutPath = join(outDir, "hello.source-map.json");
const graphReconcileBasePath = join(outDir, "hello.reconcile-base.program-graph");
const graphReconcileSourcePath = join(outDir, "hello.0");
const graphCanonicalPath = join(outDir, "hello.canonical.program-graph");
const graphSourceTextOutPath = join(outDir, "hello.source-output.0");
const graphValidateSourceTextOutPath = join(outDir, "hello.validate-source-output.0");
const checkedInGraphSourcePath = "conformance/program-graph/hello.0";
const checkedInGraphPackageDir = "conformance/program-graph";
const checkedInGraphDefaultBuildPath = ".zero/out/hello";
const checkedInGraphBuildPath = join(outDir, "checked-in-graph-build");
const checkedInGraphRunPath = join(outDir, "checked-in-graph-run");
const graphRecordRoot = join(outDir, "repository-graph-record");
const graphRecordBuildPath = join(outDir, "repository-graph-record-build");
const graphRecordObjPath = join(outDir, "repository-graph-record.o");
const graphRecordDirectLlvmIrPath = join(outDir, "repository-graph-record-direct.ll");
const graphRecordLlvmIrPath = join(outDir, "repository-graph-record.ll");
const graphViewPath = join(outDir, "hello.graph-view.0");
const graphViewWrongOutPath = join(outDir, "hello.program-graph.txt");
const graphViewParentFilePath = join(outDir, "hello.graph-view-parent-file");
const graphViewParentFileOutPath = join(graphViewParentFilePath, "out.0");
const graphCheckViewPath = join(outDir, "hello.checked.graph-view.0");
const graphSizePath = join(outDir, "hello.program-graph.size.json");
const graphBuildPath = join(outDir, "hello.program-graph-build");
const graphObjPath = join(outDir, "hello.program-graph.o");
const graphRunPath = join(outDir, "hello.program-graph-run");
const graphArgsDumpPath = join(outDir, "std-args.program-graph");
const graphArgsRunPath = join(outDir, "std-args.program-graph-run");
const graphTestDumpPath = join(outDir, "test-blocks.program-graph");
const graphPackageTestDumpPath = join(outDir, "test-app.program-graph");
const graphManifestPackageDir = join(outDir, "graph-manifest-package");
const graphManifestSourcePath = join(graphManifestPackageDir, "src", "main.0");
const graphManifestStorePath = join(graphManifestPackageDir, "zero.graph");
const graphManifestArtifactPath = join(graphManifestPackageDir, "artifacts", "test-app.program-graph");
const graphManifestBuildPath = join(outDir, "graph-manifest-package-build");
const graphManifestRunPath = join(outDir, "graph-manifest-package-run");
const directGraphManifestBuildPath = join(outDir, "direct-graph-manifest-package-build");
const directGraphManifestRunPath = join(outDir, "direct-graph-manifest-package-run");
const directGraphTargetGateRoot = join(outDir, "direct-graph-target-gate");
const directGraphTargetGatePackageDir = join(directGraphTargetGateRoot, "app");
const directGraphTargetGateDepDir = join(directGraphTargetGateRoot, "target-webbits");
const directGraphTargetGateArtifactPath = join(directGraphTargetGatePackageDir, "artifacts", "app.program-graph");
const directGraphHostLeakPackageDir = join(outDir, "direct-graph-host-leak-package");
const directGraphHostLeakArtifactPath = join(directGraphHostLeakPackageDir, "artifacts", "app.program-graph");
const directGraphHostLeakBuildPath = join(outDir, "direct-graph-host-leak-build");
const graphSizeNoisePatchPath = join(outDir, "hello.program-graph.size-noise.patch");
const graphSizeNoisePath = join(outDir, "hello.program-graph.size-noise.program-graph");
const graphSourceRoundtripPath = join(outDir, "hello.source-roundtrip.program-graph");
const graphSourceRoundtripSourceTextPath = join(outDir, "hello.source-roundtrip.0");
const graphArtifactRoundtripPath = join(outDir, "hello.roundtrip.program-graph");
const graphArtifactRoundtripSourceTextPath = join(outDir, "hello.roundtrip.0");
const graphPatchPath = join(outDir, "hello.program-graph.patch");
const graphPatchDirectoryPath = join(outDir, "hello.program-graph.patch-directory");
const graphPatchDirectoryOutPath = join(outDir, "hello.directory-patch.program-graph");
const graphPatchedPath = join(outDir, "hello.patched.program-graph");
const graphInlinePatchedPath = join(outDir, "hello.inline-patched.program-graph");
const graphPatchInsertPath = join(outDir, "hello.insert.program-graph.patch");
const graphInsertedPath = join(outDir, "hello.insert.program-graph");
const graphPatchDeletePath = join(outDir, "hello.delete.program-graph.patch");
const graphDeletedPath = join(outDir, "hello.delete.program-graph");
const graphPatchDeleteThenInsertPath = join(outDir, "hello.delete-then-insert.program-graph.patch");
const graphDeleteThenInsertedPath = join(outDir, "hello.delete-then-insert.program-graph");
const graphPatchDeleteNodeFactPath = join(outDir, "hello.delete-node-fact.program-graph.patch");
const graphDeletedNodeFactPath = join(outDir, "hello.delete-node-fact.program-graph");
const graphRepositoryPatchPackageDir = join(outDir, "repository-graph-patch-package");
const graphRepositoryNewOpsPackageDir = join(outDir, "repository-graph-new-patch-ops");
const graphRepositoryBodyPatchPath = join(outDir, "repository-graph.replace-body.patch");
const graphRepositoryNewOpsPatchPath = join(outDir, "repository-graph.new-ops.patch");
const graphRepositoryBlockPatchPath = join(outDir, "repository-graph.replace-block.patch");
const graphRepositoryInvalidBlockRowsPatchPath = join(outDir, "repository-graph.invalid-block-rows.patch");
const graphRepositoryCheckExprPatchPath = join(outDir, "repository-graph.check-expr.patch");
const graphRepositoryRowErgonomicsPatchPath = join(outDir, "repository-graph.row-ergonomics.patch");
const graphRepositoryInvalidRowsPatchPath = join(outDir, "repository-graph.invalid-rows.patch");
const graphRepositoryInvalidBodyPatchPath = join(outDir, "repository-graph.invalid-body.patch");
const graphPatchDeleteExternalRootRefPath = join(outDir, "hello.delete-external-root-ref.program-graph.patch");
const graphPatchDeleteExtraOwnerPath = join(outDir, "hello.delete-extra-owner.program-graph.patch");
const graphPatchReplacePath = join(outDir, "hello.replace.program-graph.patch");
const graphReplacedPath = join(outDir, "hello.replace.program-graph");
const graphPatchStaleReplacePath = join(outDir, "hello.stale-replace.program-graph.patch");
const graphPatchInsertEdgePath = join(outDir, "hello.insert-edge.program-graph.patch");
const graphInsertedEdgePath = join(outDir, "hello.insert-edge.program-graph");
const graphPatchEmptyTypeEdgePath = join(outDir, "hello.empty-type-edge.program-graph.patch");
const graphPatchRenamePath = join(outDir, "hello.rename.program-graph.patch");
const graphRenamedPath = join(outDir, "hello.rename.program-graph");
const graphPatchInvalidRenamePath = join(outDir, "hello.invalid-rename.program-graph.patch");
const graphUncheckedPatchPath = join(outDir, "hello.unchecked.program-graph.patch");
const graphUncheckedPath = join(outDir, "hello.unchecked.program-graph");
const graphBorrowDumpPath = join(outDir, "borrow.program-graph");
const graphBorrowConflictPatchPath = join(outDir, "borrow-conflict.program-graph.patch");
const graphBorrowConflictPath = join(outDir, "borrow-conflict.program-graph");
const graphPatchEmptyPath = join(outDir, "hello.empty.program-graph.patch");
const graphPatchControlPath = join(outDir, "hello.control.program-graph.patch");
const graphPatchHighBytePath = join(outDir, "hello.high-byte.program-graph.patch");
const graphPatchBadEscapePath = join(outDir, "hello.bad-escape.program-graph.patch");
const graphPatchNullEscapePath = join(outDir, "hello.null-escape.program-graph.patch");
const graphPatchRawNullPath = join(outDir, "hello.raw-null.program-graph.patch");
const graphPatchInvalidNamePath = join(outDir, "hello.invalid-name.program-graph.patch");
const graphPatchInvalidTypePath = join(outDir, "hello.invalid-type.program-graph.patch");
const graphPatchReservedParamPath = join(outDir, "hello.reserved-param.program-graph.patch");
const graphReservedParamPath = join(outDir, "hello.reserved-param.program-graph");
const graphPatchInternalFunctionPath = join(outDir, "hello.internal-function.program-graph.patch");
const graphInternalFunctionPath = join(outDir, "hello.internal-function.program-graph");
const graphPayloadDumpPath = join(outDir, "payload-match.program-graph");
const graphPatchInvalidMatchReplacePath = join(outDir, "payload-match.invalid-replace.program-graph.patch");
const graphPatchInvalidMatchInsertPath = join(outDir, "payload-match.invalid-insert.program-graph.patch");
const graphPackageDumpPath = join(outDir, "systems-package.program-graph");
const graphPackageViewPath = join(outDir, "systems-package.graph-view.0");
const graphPackagePathMismatchPatchPath = join(outDir, "systems-package.path-mismatch.program-graph.patch");
const graphPackagePathMismatchPath = join(outDir, "systems-package.path-mismatch.program-graph");
const graphPatchInvalidImportAliasPath = join(outDir, "systems-package.invalid-import-alias.program-graph.patch");
const graphPatchInvalidImportNamePath = join(outDir, "systems-package.invalid-import-name.program-graph.patch");
const graphInvalidImportNamePath = join(outDir, "systems-package.invalid-import-name.program-graph");
const graphPatchMissingImportPath = join(outDir, "systems-package.missing-import.program-graph.patch");
const graphMissingImportPath = join(outDir, "systems-package.missing-import.program-graph");
const graphPatchMismatchPath = join(outDir, "hello.mismatch.program-graph.patch");
const graphPatchBadHashPath = join(outDir, "hello.bad-hash.program-graph.patch");
const graphSparseOrderPath = join(outDir, "hello.sparse-order.program-graph");
const graphSparseArgPath = join(outDir, "hello.sparse-arg.program-graph");
rmSync(graphDumpPath, { force: true });
rmSync(graphDumpJsonPath, { force: true });
rmSync(graphStableSiblingSourcePath, { force: true });
rmSync(graphImportPath, { force: true });
rmSync(graphImportJsonPath, { force: true });
rmSync(graphImportDirectorySourcePath, { recursive: true, force: true });
rmSync(graphImportDirectoryOutPath, { force: true });
rmSync(graphCanonicalPath, { force: true });
rmSync(graphSourceTextOutPath, { force: true });
rmSync(graphValidateSourceTextOutPath, { force: true });
rmSync(checkedInGraphDefaultBuildPath, { force: true });
rmSync(checkedInGraphBuildPath, { force: true });
rmSync(checkedInGraphRunPath, { force: true });
rmSync(graphRecordRoot, { force: true, recursive: true });
rmSync(graphRecordBuildPath, { force: true });
rmSync(graphRecordObjPath, { force: true });
rmSync(graphRecordDirectLlvmIrPath, { force: true });
rmSync(graphRecordLlvmIrPath, { force: true });
rmSync(graphViewPath, { force: true });
rmSync(graphViewWrongOutPath, { force: true });
rmSync(graphViewParentFilePath, { force: true, recursive: true });
rmSync(graphCheckViewPath, { force: true });
rmSync(graphSizePath, { force: true });
rmSync(graphBuildPath, { force: true });
rmSync(graphObjPath, { force: true });
rmSync(graphRunPath, { force: true });
rmSync(graphArgsDumpPath, { force: true });
rmSync(graphArgsRunPath, { force: true });
rmSync(graphTestDumpPath, { force: true });
rmSync(graphPackageTestDumpPath, { force: true });
rmSync(graphManifestPackageDir, { force: true, recursive: true });
rmSync(graphManifestBuildPath, { force: true });
rmSync(graphManifestRunPath, { force: true });
rmSync(directGraphManifestBuildPath, { force: true });
rmSync(directGraphManifestRunPath, { force: true });
rmSync(directGraphTargetGateRoot, { force: true, recursive: true });
rmSync(directGraphHostLeakPackageDir, { force: true, recursive: true });
rmSync(directGraphHostLeakBuildPath, { force: true });
rmSync(graphSizeNoisePatchPath, { force: true });
rmSync(graphSizeNoisePath, { force: true });
rmSync(graphSourceRoundtripPath, { force: true });
rmSync(graphSourceRoundtripSourceTextPath, { force: true });
rmSync(graphArtifactRoundtripPath, { force: true });
rmSync(graphArtifactRoundtripSourceTextPath, { force: true });
rmSync(graphPatchPath, { force: true });
rmSync(graphPatchDirectoryPath, { recursive: true, force: true });
rmSync(graphPatchDirectoryOutPath, { force: true });
rmSync(graphPatchedPath, { force: true });
rmSync(graphInlinePatchedPath, { force: true });
rmSync(graphPatchInsertPath, { force: true });
rmSync(graphInsertedPath, { force: true });
rmSync(graphPatchDeletePath, { force: true });
rmSync(graphDeletedPath, { force: true });
rmSync(graphPatchDeleteThenInsertPath, { force: true });
rmSync(graphDeleteThenInsertedPath, { force: true });
rmSync(graphPatchDeleteNodeFactPath, { force: true });
rmSync(graphDeletedNodeFactPath, { force: true });
rmSync(graphPatchDeleteExternalRootRefPath, { force: true });
rmSync(graphPatchDeleteExtraOwnerPath, { force: true });
rmSync(graphPatchReplacePath, { force: true });
rmSync(graphReplacedPath, { force: true });
rmSync(graphPatchStaleReplacePath, { force: true });
rmSync(graphPatchInsertEdgePath, { force: true });
rmSync(graphInsertedEdgePath, { force: true });
rmSync(graphPatchEmptyTypeEdgePath, { force: true });
rmSync(graphPatchRenamePath, { force: true });
rmSync(graphRenamedPath, { force: true });
rmSync(graphPatchInvalidRenamePath, { force: true });
rmSync(graphUncheckedPatchPath, { force: true });
rmSync(graphUncheckedPath, { force: true });
rmSync(graphBorrowDumpPath, { force: true });
rmSync(graphBorrowConflictPatchPath, { force: true });
rmSync(graphBorrowConflictPath, { force: true });
rmSync(graphPatchEmptyPath, { force: true });
rmSync(graphPatchControlPath, { force: true });
rmSync(graphPatchHighBytePath, { force: true });
rmSync(graphPatchBadEscapePath, { force: true });
rmSync(graphPatchNullEscapePath, { force: true });
rmSync(graphPatchRawNullPath, { force: true });
rmSync(graphPatchInvalidNamePath, { force: true });
rmSync(graphPatchInvalidTypePath, { force: true });
rmSync(graphPatchReservedParamPath, { force: true });
rmSync(graphReservedParamPath, { force: true });
rmSync(graphPatchInternalFunctionPath, { force: true });
rmSync(graphInternalFunctionPath, { force: true });
rmSync(graphPayloadDumpPath, { force: true });
rmSync(graphPatchInvalidMatchReplacePath, { force: true });
rmSync(graphPatchInvalidMatchInsertPath, { force: true });
rmSync(graphPackageDumpPath, { force: true });
rmSync(graphPackagePathMismatchPatchPath, { force: true });
rmSync(graphPackagePathMismatchPath, { force: true });
rmSync(graphPatchInvalidImportAliasPath, { force: true });
rmSync(graphPatchInvalidImportNamePath, { force: true });
rmSync(graphInvalidImportNamePath, { force: true });
rmSync(graphPatchMissingImportPath, { force: true });
rmSync(graphMissingImportPath, { force: true });
rmSync(graphPatchMismatchPath, { force: true });
rmSync(graphPatchBadHashPath, { force: true });
rmSync(graphSparseOrderPath, { force: true });
rmSync(graphSparseArgPath, { force: true });
writeFileSync(graphStableSiblingSourcePath, readFileSync("examples/hello.0", "utf8"));
importProjectionSidecar(graphStableSiblingSourcePath);
const graphStableBaseJson = json(["dump", "--json", graphStableSiblingSourcePath]).body;
const graphStableBaseMain = graphStableBaseJson.nodes.find((node) => node.kind === "Function" && node.name === "main");
const graphStableBaseLiteral = graphStableBaseJson.nodes.find((node) => node.kind === "Literal" && node.type === "String" && node.value === "hello from zero\n");
assert(graphStableBaseMain);
assert(graphStableBaseLiteral);
writeFileSync(graphStableSiblingSourcePath, `fn helper() -> u32 {\n    return 1\n}\n\n${readFileSync("examples/hello.0", "utf8")}`);
importProjectionSidecar(graphStableSiblingSourcePath);
const graphStableSiblingJson = json(["dump", "--json", graphStableSiblingSourcePath]).body;
assert.equal(graphStableSiblingJson.nodes.find((node) => node.kind === "Function" && node.name === "main")?.id, graphStableBaseMain.id);
assert.equal(graphStableSiblingJson.nodes.find((node) => node.kind === "Literal" && node.type === "String" && node.value === "hello from zero\n")?.id, graphStableBaseLiteral.id);
assert.equal(zero(["dump", "--out", graphDumpPath, "examples/hello.0"]).stdout, "");
assert.equal(readFileSync(graphDumpPath, "utf8"), graphDump);
assert.equal(zero(["dump", "--format", "binary", "--out", graphDumpBinaryPath, "examples/hello.0"]).stdout, "");
assert.equal(readFileSync(graphDumpBinaryPath).subarray(0, 8).toString("latin1"), "ZRGBIN1\0");
assert.equal(zero(["validate", graphDumpBinaryPath]).stdout, "program graph ok\n");
assert.equal(zero(["view", graphDumpBinaryPath]).stdout, zero(["view", graphDumpPath]).stdout);
const graphDumpOutJson = json(["dump", "--json", "--out", graphDumpJsonPath, "examples/hello.0"]).body;
assert.deepEqual(graphDumpOutJson, graphDumpJson);
assert.equal(readFileSync(graphDumpJsonPath, "utf8"), graphDump);
assert.equal(zero(["validate", graphDumpJsonPath]).stdout, "program graph ok\n");
assert.equal(zero(["dump", "--out", graphDumpPath, "examples/hello.0"]).stdout, "");
assert.equal(readFileSync(graphDumpPath, "utf8"), graphDump);
const graphImportText = zero(["import", "examples/hello.0"]).stdout;
assert.match(graphImportText, / path:"examples\/hello\.0"/);
assert.match(graphImportText, /hash "graph:[0-9a-f]{16}"/);
assert.equal(zero(["import", "--out", graphImportPath, "examples/hello.0"]).stdout, "");
assert.equal(readFileSync(graphImportPath, "utf8"), graphImportText);
const graphImportJson = json(["import", "--json", "examples/hello.0"]).body;
assert.equal(graphImportJson.moduleIdentity, graphDumpJson.moduleIdentity);
assert.equal(graphImportJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphImportJson.validation.ok, true);
const graphImportOutJson = json(["import", "--json", "--out", graphImportJsonPath, "examples/hello.0"]).body;
assert.equal(graphImportOutJson.ok, true);
assert.equal(graphImportOutJson.moduleIdentity, graphDumpJson.moduleIdentity);
assert.equal(graphImportOutJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphImportOutJson.validation.ok, true);
assert.equal(graphImportOutJson.saved.path, graphImportJsonPath);
assert.equal(readFileSync(graphImportJsonPath, "utf8"), graphImportText);
assert.equal(zero(["validate", graphImportJsonPath]).stdout, "program graph ok\n");
mkdirSync(graphImportDirectorySourcePath, { recursive: true });
const graphImportDirectoryJson = json(["import", "--json", "--out", graphImportDirectoryOutPath, graphImportDirectorySourcePath], { allowFailure: true });
assert.notEqual(graphImportDirectoryJson.code, 0);
assert.equal(graphImportDirectoryJson.body.diagnostics[0].path, graphImportDirectorySourcePath);
assert.match(graphImportDirectoryJson.body.diagnostics[0].message, /^failed to read '.+hello-source-directory\.0': /);
assert.equal(existsSync(graphImportDirectoryOutPath), false);
const graphInspectJson = json(["inspect", "--json", "examples/hello.0"]).body;
assert.equal(graphInspectJson.schemaVersion, 1);
assert.match(graphInspectJson.programGraph.graphHash, /^graph:[0-9a-f]{16}$/);
assert.equal(graphInspectJson.programGraph.moduleIdentity, graphDumpJson.moduleIdentity);
assert.equal(graphInspectJson.programGraph.canonicalSource, false);
assert.equal(graphInspectJson.programGraph.validation.ok, true);
assert.equal(graphInspectJson.callResolution.schemaVersion, 1);
const graphStdIoLinesInspectJson = json(["inspect", "--json", "conformance/native/pass/std-io-lines.graph"]).body;
assert.equal(graphStdIoLinesInspectJson.targetReadiness.ok, true);
assert.equal(graphStdIoLinesInspectJson.targetReadiness.buildable, true);
assert.deepEqual(graphStdIoLinesInspectJson.targetReadiness.diagnostics, []);
const graphInspectText = zero(["inspect", "examples/hello.0"]).stdout;
assert.match(graphInspectText, /^program graph inspect\n/);
assert.match(graphInspectText, /graph: module:hello graph:[0-9a-f]{16} \(13 nodes, 12 edges, shape-valid\)/);
assert.match(graphInspectText, /capabilities: world/);
assert.match(graphInspectText, /next: use `zero query examples\/hello\.0` for patch-ready node handles/);
const graphInspectOutJson = json(["inspect", "--json", "--out", graphInspectOutPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphInspectOutJson.code, 0);
assert.equal(graphInspectOutJson.body.diagnostics[0].message, "inspect does not support --out");
assert.equal(graphInspectOutJson.body.diagnostics[0].expected, "zero inspect [--json] [graph-input]");
assert.equal(zero(["validate", graphDumpPath]).stdout, "program graph ok\n");
const graphSourceMapJson = json(["source-map", "--json", "examples/hello.0"]).body;
assert.equal(graphSourceMapJson.ok, true);
assert.match(graphSourceMapJson.graphHash, /^graph:[0-9a-f]{16}$/);
  const graphMainSourceMapping = graphSourceMapJson.mappings.find((mapping) => mapping.nodeId === graphMainFunctionNode.id);
  assert(graphMainSourceMapping && graphMainSourceMapping.sourceRange.path === "hello.0");
  assert.equal(graphMainSourceMapping.sourceAvailable, false);
  assert.deepEqual(graphMainSourceMapping.sourceRange.start, { line: 1, column: 1 });
  assert.deepEqual(graphMainSourceMapping.sourceRange.end, { line: 1, column: 2 });
assert.equal(zero(["source-map", "examples/hello.0"]).stdout, `program graph source map ok: ${graphSourceMapJson.counts.mappings} mappings\n`);
const graphSourceMapOutJson = json(["source-map", "--json", "--out", graphSourceMapOutPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphSourceMapOutJson.code, 0);
assert.equal(graphSourceMapOutJson.body.diagnostics[0].message, "source-map does not support --out");
assert.equal(graphSourceMapOutJson.body.diagnostics[0].expected, "zero source-map [--json] [graph-input]");
assert.equal(existsSync(graphSourceMapOutPath), false);
writeFileSync(graphReconcileSourcePath, readFileSync("examples/hello.0", "utf8"));
const graphReconcileBaseJson = json(["import", "--json", "--out", graphReconcileBasePath, graphReconcileSourcePath]).body;
assert.equal(graphReconcileBaseJson.saved.path, graphReconcileBasePath);
writeFileSync(graphReconcileSourcePath, readFileSync(graphReconcileSourcePath, "utf8").replace("hello from zero\\n", "hello from reconcile\\n"));
const graphReconcileJson = json(["reconcile", "--json", graphReconcileBasePath, "--source", graphReconcileSourcePath]).body;
assert.equal(graphReconcileJson.ok, true);
assert.equal(graphReconcileJson.base.graphHash, graphReconcileBaseJson.graphHash);
assert.equal(graphReconcileJson.identity.ambiguous, 0);
assert.equal(graphReconcileJson.graphPatch.available, true);
assert.match(graphReconcileJson.graphPatch.text, /set node="#expr_[^"]+" field="value"/);
const graphReconcileLiteralDecision = graphReconcileJson.decisions.find((decision) => decision.status === "edited" && decision.kind === "Literal");
assert.deepEqual(graphReconcileLiteralDecision?.sourceRange.start, { line: 2, column: 27 });
assert.deepEqual(graphReconcileLiteralDecision?.sourceRange.end, { line: 2, column: 51 });
assert.equal(zero(["reconcile", graphReconcileBasePath, "--source", graphReconcileSourcePath]).stdout, "program graph reconcile ok\n");
const graphReconcileMissingSource = json(["reconcile", "--json", graphReconcileBasePath], { allowFailure: true });
assert.notEqual(graphReconcileMissingSource.code, 0);
assert.equal(graphReconcileMissingSource.body.diagnostics[0].message, "graph reconcile requires edited source input");
assert.equal(graphReconcileMissingSource.body.diagnostics[0].actual, "missing --source");
const graphValidateJson = json(["validate", "--json", "--out", graphCanonicalPath, graphDumpPath]).body;
assert.equal(graphValidateJson.ok, true);
assert.equal(graphValidateJson.moduleIdentity, "module:hello");
assert.equal(graphValidateJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphValidateJson.saved.path, graphCanonicalPath);
assert.equal(readFileSync(graphCanonicalPath, "utf8"), graphDump);
const graphSourceTextOutJson = json(["dump", "--json", "--out", graphSourceTextOutPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphSourceTextOutJson.code, 0);
assert.equal(graphSourceTextOutJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphSourceTextOutJson.body.diagnostics[0].expected, "zero dump --out <program-graph-artifact> [graph-input]");
assert.equal(graphSourceTextOutJson.body.diagnostics[0].help, ".0 files are canonical source text; write derived ProgramGraph artifacts to a non-source path");
assert.equal(existsSync(graphSourceTextOutPath), false);
const graphValidateSourceTextOutJson = json(["validate", "--json", "--out", graphValidateSourceTextOutPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphValidateSourceTextOutJson.code, 0);
assert.equal(graphValidateSourceTextOutJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphValidateSourceTextOutJson.body.diagnostics[0].expected, "zero validate --out <program-graph-artifact> [graph-input]");
assert.equal(existsSync(graphValidateSourceTextOutPath), false);
const checkedInGraphSource = readFileSync(checkedInGraphSourcePath, "utf8");
assert.equal(checkedInGraphSource, readFileSync("examples/hello.0", "utf8"));
const checkedInRepositoryGraphStorePath = join(checkedInGraphPackageDir, "zero.graph");
const checkedInGraphValidateJson = json(["validate", "--json", checkedInGraphSourcePath]).body;
assert.equal(checkedInGraphValidateJson.ok, true);
assert.equal(checkedInGraphValidateJson.artifact, checkedInRepositoryGraphStorePath);
assert.equal(checkedInGraphValidateJson.moduleIdentity, "package:program-graph-fixture@0.1.0");
const checkedInGraphViewJson = json(["view", "--json", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphViewJson.ok, true);
assert.equal(checkedInGraphViewJson.canonicalSource, true);
assert.match(checkedInGraphViewJson.view, /pub fn main\(world: World\) -> Void raises/);
const checkedInGraphQueryJson = json(["query", "--json", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphQueryJson.ok, true);
assert.equal(checkedInGraphQueryJson.inputKind, "repository-graph");
assert.equal(checkedInGraphQueryJson.functions.some((fun) => fun.name === "main" && fun.public === true), true);
assert.equal(checkedInGraphQueryJson.patchOperations.includes("insert node=\"#id\" kind=\"Literal\" parent=\"#parent\" edge=\"arg\" order=\"0\" type=\"String\" value=\"text\""), true);
assert.equal(checkedInGraphQueryJson.patchOperations.includes("replace node=\"#id\" expect=\"nodehash:abc123\" kind=\"Literal\" type=\"String\" value=\"text\""), true);
assert.equal(checkedInGraphQueryJson.patchOperations.some((op) => op.includes("setMain")), false);
assert.equal(checkedInGraphQueryJson.patchOperations.some((op) => op.startsWith("replaceFunctionBody main\n")), true);
assert.equal(checkedInGraphQueryJson.patchOperations.some((op) => op.startsWith("replaceBlockBody #block_id\n")), true);
assert.equal(checkedInGraphQueryJson.patchOperations.includes("addLetLiteral fn=\"main\" name=\"count\" type=\"u32\" value=\"0\""), true);
assert.equal(checkedInGraphQueryJson.patchOperations.includes("addReturnValue fn=\"identity\" value=\"input\" type=\"i32\""), true);
assert.equal(checkedInGraphQueryJson.patchOperations.includes("addReturnExpr fn=\"maybe\" expr=\"null\""), true);
assert.equal(checkedInGraphQueryJson.patchOperations.includes("appendStmt fn=\"main\" stmt=\"check std.http.listen(world, 3000_u16)\""), true);
assert.equal(checkedInGraphQueryJson.patchOperations.some((op) => op.startsWith("addTestBody name=\"api add\"\n")), true);
assert.equal(checkedInGraphQueryJson.patchOperations.includes("renameTest name=\"api add\" value=\"api add route\""), true);
assert.equal(checkedInGraphQueryJson.patchOperations.includes("deleteTest name=\"api add\""), true);
assert.equal(checkedInGraphQueryJson.patchOperations.some((op) => op.startsWith("upsertFunction handle\n")), true);
rmSync(graphRepositoryNewOpsPackageDir, { recursive: true, force: true });
mkdirSync(graphRepositoryNewOpsPackageDir, { recursive: true });
writeFileSync(join(graphRepositoryNewOpsPackageDir, "zero.toml"), readFileSync(join(checkedInGraphPackageDir, "zero.toml"), "utf8"));
writeFileSync(join(graphRepositoryNewOpsPackageDir, "zero.graph"), readFileSync(checkedInRepositoryGraphStorePath));
writeFileSync(join(graphRepositoryNewOpsPackageDir, "hello.0"), checkedInGraphSource);
const repositoryNewOpsHash = json(["query", "--json", graphRepositoryNewOpsPackageDir]).body.graphHash;
writeFileSync(graphRepositoryNewOpsPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${repositoryNewOpsHash}"`,
  "upsertFunction main",
  "pub fn main(world: World) -> Void raises {",
  "}",
  "end",
  'appendStmt fn="main" stmt="check world.out.write(\\"new ops ok\\")"',
  'addFunction name="answer" ret="i32"',
  'addReturnExpr fn="answer" expr="42"',
  'addTestBody name="answer works"',
  "  expect (answer() == 42)",
  "end",
  "",
].join("\n"));
const repositoryNewOpsDryRunJson = json(["patch", "--json", "--check-only", graphRepositoryNewOpsPackageDir, graphRepositoryNewOpsPatchPath]).body;
assert.equal(repositoryNewOpsDryRunJson.ok, true);
assert.equal(repositoryNewOpsDryRunJson.checkOnly, true);
assert.equal(repositoryNewOpsDryRunJson.saved, null);
assert.deepEqual(repositoryNewOpsDryRunJson.operations.map((operation) => operation.op), ["upsertFunction", "appendStmt", "addFunction", "addReturnExpr", "addTestBody"]);
assert.equal(zero(["view", graphRepositoryNewOpsPackageDir]).stdout, checkedInGraphSource);
const repositoryNewOpsPatchJson = json(["patch", "--json", graphRepositoryNewOpsPackageDir, graphRepositoryNewOpsPatchPath]).body;
assert.equal(repositoryNewOpsPatchJson.ok, true);
assert.deepEqual(repositoryNewOpsPatchJson.operations.map((operation) => operation.op), ["upsertFunction", "appendStmt", "addFunction", "addReturnExpr", "addTestBody"]);
assert.equal(repositoryNewOpsPatchJson.saved.path, join(graphRepositoryNewOpsPackageDir, "zero.graph"));
assert.match(zero(["view", "--fn", "main", graphRepositoryNewOpsPackageDir]).stdout, /check world\.out\.write\("new ops ok"\)/);
assert.match(zero(["view", "--fn", "answer", graphRepositoryNewOpsPackageDir]).stdout, /return 42/);
assert.equal(zero(["check", graphRepositoryNewOpsPackageDir]).stdout, "ok\n");
assert.equal(zero(["run", graphRepositoryNewOpsPackageDir]).stdout, "new ops ok");
assert.equal(zero(["test", graphRepositoryNewOpsPackageDir]).stdout, "1 test(s) ok\n");
const repositoryRenameTestJson = json(["patch", "--json", graphRepositoryNewOpsPackageDir, "--op", 'renameTest name="answer works" value="answer route works"']).body;
assert.equal(repositoryRenameTestJson.ok, true);
assert.equal(repositoryRenameTestJson.operations[0].op, "renameTest");
assert.equal(repositoryRenameTestJson.operations[0].actual, "answer works");
assert.equal(zero(["test", graphRepositoryNewOpsPackageDir]).stdout, "1 test(s) ok\n");
const repositoryUnsupportedTestPatch = zeroWithInput(["patch", graphRepositoryNewOpsPackageDir, "--patch-text", "-"], [
  "zero-program-graph-patch v1",
  'addTestBody name="unsupported buffer test"',
  "  var response: [16]u8 = [0_u8; 16]",
  "  expect true",
  "end",
  "",
].join("\n"));
assertPatchOkOutput(repositoryUnsupportedTestPatch.stdout, join(graphRepositoryNewOpsPackageDir, "zero.graph"));
const repositoryUnsupportedTestRun = zero(["test", graphRepositoryNewOpsPackageDir], { allowFailure: true });
assert.notEqual(repositoryUnsupportedTestRun.code, 0);
assert.match(repositoryUnsupportedTestRun.stderr, /zero graph test runner does not support .* expression yet/);
assert.match(repositoryUnsupportedTestRun.stderr, /supported graph tests are pure locals/);
const repositoryDeleteUnsupportedTestJson = json(["patch", "--json", graphRepositoryNewOpsPackageDir, "--op", 'deleteTest name="unsupported buffer test"']).body;
assert.equal(repositoryDeleteUnsupportedTestJson.ok, true);
assert.equal(repositoryDeleteUnsupportedTestJson.operations[0].op, "deleteTest");
assert.equal(zero(["test", graphRepositoryNewOpsPackageDir]).stdout, "1 test(s) ok\n");
const repositoryDeleteTestJson = json(["patch", "--json", graphRepositoryNewOpsPackageDir, "--op", 'deleteTest name="answer route works"']).body;
assert.equal(repositoryDeleteTestJson.ok, true);
assert.equal(repositoryDeleteTestJson.operations[0].op, "deleteTest");
const repositoryNoTests = zero(["test", graphRepositoryNewOpsPackageDir]).stdout;
assert.match(repositoryNoTests, /0 test\(s\) ok/);
assert.match(repositoryNoTests, /note: no test blocks found/);
assert.match(zero(["export", graphRepositoryNewOpsPackageDir]).stdout, /repository graph export ok/);
assert.equal(zero(["verify-projection", graphRepositoryNewOpsPackageDir]).stdout, "repository graph verify-projection ok\n");
rmSync(graphRepositoryPatchPackageDir, { recursive: true, force: true });
mkdirSync(graphRepositoryPatchPackageDir, { recursive: true });
writeFileSync(join(graphRepositoryPatchPackageDir, "zero.toml"), readFileSync(join(checkedInGraphPackageDir, "zero.toml"), "utf8"));
writeFileSync(join(graphRepositoryPatchPackageDir, "zero.graph"), readFileSync(checkedInRepositoryGraphStorePath));
writeFileSync(join(graphRepositoryPatchPackageDir, "hello.0"), checkedInGraphSource);
const repositoryPatchQueryJson = json(["query", "--json", graphRepositoryPatchPackageDir]).body;
writeFileSync(graphRepositoryBodyPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${repositoryPatchQueryJson.graphHash}"`,
  "replaceFunctionBody main",
  "  let name Maybe<String> = std.args.get 1",
  "  if name.has",
  "    check world.out.write \"hello \"",
  "    check world.out.write name.value",
  "    check world.out.write \"\\n\"",
  "  else",
  "    check world.out.write \"hello anonymous\\n\"",
  "end",
  "",
].join("\n"));
const repositoryPatchDryRunJson = json(["patch", "--json", "--check-only", graphRepositoryPatchPackageDir, graphRepositoryBodyPatchPath]).body;
assert.equal(repositoryPatchDryRunJson.ok, true);
assert.equal(repositoryPatchDryRunJson.checkOnly, true);
assert.equal(repositoryPatchDryRunJson.saved, null);
assert.equal(zero(["view", graphRepositoryPatchPackageDir]).stdout, checkedInGraphSource);
const repositoryBodyPatchJson = json(["patch", "--json", graphRepositoryPatchPackageDir, graphRepositoryBodyPatchPath]).body;
assert.equal(repositoryBodyPatchJson.ok, true);
assert.equal(repositoryBodyPatchJson.checkOnly, false);
assert.equal(repositoryBodyPatchJson.operations[0].op, "replaceFunctionBody");
assert.equal(repositoryBodyPatchJson.saved.path, join(graphRepositoryPatchPackageDir, "zero.graph"));
assert.match(zero(["view", graphRepositoryPatchPackageDir]).stdout, /let name: Maybe<String> = std\.args\.get\(1\)/);
assert.equal(zero(["check", graphRepositoryPatchPackageDir]).stdout, "ok\n");
assert.equal(zero(["run", graphRepositoryPatchPackageDir, "--", "Ada"]).stdout, "hello Ada\n");
const repositoryBlockQueryJson = json(["query", "--json", "--find", "Block", graphRepositoryPatchPackageDir]).body;
const repositoryThenBlock = repositoryBlockQueryJson.matches.find((node) => node.kind === "Block" && node.name === "then");
assert(repositoryThenBlock, "expected replaceFunctionBody patch to expose a then block handle");
writeFileSync(graphRepositoryInvalidBlockRowsPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${repositoryBlockQueryJson.graphHash}"`,
  `replaceBlockBody ${repositoryThenBlock.id}`,
  "  check world.out.write \"name: \"",
  "  let same Bool = std.mem.eql (std.mem.span name \"Ada\"",
  "end",
  "",
].join("\n"));
const repositoryInvalidBlockRowsDryRun = json(["patch", "--json", "--check-only", graphRepositoryPatchPackageDir, graphRepositoryInvalidBlockRowsPatchPath], { allowFailure: true });
assert.notEqual(repositoryInvalidBlockRowsDryRun.code, 0);
assert.equal(repositoryInvalidBlockRowsDryRun.body.diagnostic.code, "GPH001");
assert.match(repositoryInvalidBlockRowsDryRun.body.diagnostic.actual, /row 2: let same Bool = std\.mem\.eql/);
writeFileSync(graphRepositoryBlockPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${repositoryBlockQueryJson.graphHash}"`,
  `replaceBlockBody ${repositoryThenBlock.id}`,
  "  check world.out.write \"name: \"",
  "  check world.out.write name.value",
  "  check world.out.write \"\\n\"",
  "end",
  "",
].join("\n"));
const repositoryBlockPatchDryRunJson = json(["patch", "--json", "--check-only", graphRepositoryPatchPackageDir, graphRepositoryBlockPatchPath]).body;
assert.equal(repositoryBlockPatchDryRunJson.ok, true);
assert.equal(repositoryBlockPatchDryRunJson.checkOnly, true);
assert.equal(repositoryBlockPatchDryRunJson.saved, null);
const repositoryBlockPatchJson = json(["patch", "--json", graphRepositoryPatchPackageDir, graphRepositoryBlockPatchPath]).body;
assert.equal(repositoryBlockPatchJson.ok, true);
assert.equal(repositoryBlockPatchJson.operations[0].op, "replaceBlockBody");
assert.equal(repositoryBlockPatchJson.saved.path, join(graphRepositoryPatchPackageDir, "zero.graph"));
const repositoryBlockView = zero(["view", graphRepositoryPatchPackageDir]).stdout;
assert.match(repositoryBlockView, /check world\.out\.write\("name: "\)/);
assert.match(repositoryBlockView, /check world\.out\.write\("hello anonymous\\n"\)/);
assert.equal(zero(["check", graphRepositoryPatchPackageDir]).stdout, "ok\n");
assert.equal(zero(["run", graphRepositoryPatchPackageDir, "--", "Ada"]).stdout, "name: Ada\n");
assert.match(zero(["status", graphRepositoryPatchPackageDir]).stdout, /source-stale/);
const statusStorePathStdout = zero(["status", join(graphRepositoryPatchPackageDir, "zero.graph")]).stdout;
assert.match(statusStorePathStdout, /repository graph status: source-stale/);
assert.match(statusStorePathStdout, new RegExp(`root: ${graphRepositoryPatchPackageDir.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}\n`));
assert.match(zero(["export", graphRepositoryPatchPackageDir]).stdout, /repository graph export ok/);
assert.equal(zero(["verify-projection", graphRepositoryPatchPackageDir]).stdout, "repository graph verify-projection ok\n");
assert.equal(zero(["verify-projection", join(graphRepositoryPatchPackageDir, "zero.graph")]).stdout, "repository graph verify-projection ok\n");
const repositoryRowQueryJson = json(["query", "--json", graphRepositoryPatchPackageDir]).body;
writeFileSync(graphRepositoryCheckExprPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${repositoryRowQueryJson.graphHash}"`,
  "replaceFunctionBody main",
  "  var path_storage [32]u8 = repeat 0_u8 32",
  "  let name String = check std.path.join path_storage \".zero\" \"row-ergonomics\"",
  "  if !std.mem.eql name \".zero/row-ergonomics\"",
  "    check world.err.write \"wrong name\\n\"",
  "end",
  "",
].join("\n"));
const repositoryCheckExprDryRunJson = json(["patch", "--json", "--check-only", graphRepositoryPatchPackageDir, graphRepositoryCheckExprPatchPath]).body;
assert.equal(repositoryCheckExprDryRunJson.ok, true);
assert.equal(repositoryCheckExprDryRunJson.checkOnly, true);
assert.equal(repositoryCheckExprDryRunJson.saved, null);
assert.equal(repositoryCheckExprDryRunJson.operations[0].op, "replaceFunctionBody");
writeFileSync(graphRepositoryRowErgonomicsPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${repositoryRowQueryJson.graphHash}"`,
  "replaceFunctionBody main",
  "  let name String = \".zero/row-ergonomics\"",
  "  let same Bool = std.mem.eql (std.mem.span name) \".zero/row-ergonomics\"",
  "  let status i32 = 200_u16 as i32",
  "  let count i32 = 2",
  "  let math Bool = count + 1 > 2",
  "  if !same || status != 200",
  "    check world.err.write \"wrong name\\n\"",
  "  else if math || status == 200",
  "    check world.out.write \"row ergonomics ok\\n\"",
  "    return",
  "  else",
  "    return",
  "end",
  "",
].join("\n"));
const repositoryRowErgonomicsDryRunJson = json(["patch", "--json", "--check-only", graphRepositoryPatchPackageDir, graphRepositoryRowErgonomicsPatchPath]).body;
assert.equal(repositoryRowErgonomicsDryRunJson.ok, true);
assert.equal(repositoryRowErgonomicsDryRunJson.checkOnly, true);
assert.equal(repositoryRowErgonomicsDryRunJson.saved, null);
const repositoryRowErgonomicsPatchJson = json(["patch", "--json", graphRepositoryPatchPackageDir, graphRepositoryRowErgonomicsPatchPath]).body;
assert.equal(repositoryRowErgonomicsPatchJson.ok, true);
assert.equal(repositoryRowErgonomicsPatchJson.operations[0].op, "replaceFunctionBody");
const repositoryRowErgonomicsView = zero(["view", graphRepositoryPatchPackageDir]).stdout;
assert.match(repositoryRowErgonomicsView, /let name: String = "\.zero\/row-ergonomics"/);
assert.match(repositoryRowErgonomicsView, /let same: Bool = std\.mem\.eql\(std\.mem\.span\(name\), "\.zero\/row-ergonomics"\)/);
assert.match(repositoryRowErgonomicsView, /let status: i32 = 200_u16 as i32/);
assert.match(repositoryRowErgonomicsView, /let math: Bool = count \+ 1 > 2/);
assert.match(repositoryRowErgonomicsView, /if !same \|\| status != 200/);
assert.match(repositoryRowErgonomicsView, /else if math \|\| status == 200/);
assert.match(repositoryRowErgonomicsView, /return/);
assert.equal(zero(["check", graphRepositoryPatchPackageDir]).stdout, "ok\n");
assert.equal(zero(["run", graphRepositoryPatchPackageDir]).stdout, "row ergonomics ok\n");
assert.match(zero(["export", graphRepositoryPatchPackageDir]).stdout, /repository graph export ok/);
assert.equal(zero(["verify-projection", graphRepositoryPatchPackageDir]).stdout, "repository graph verify-projection ok\n");
const repositorySyncedQueryJson = json(["query", "--json", graphRepositoryPatchPackageDir]).body;
writeFileSync(graphRepositoryInvalidRowsPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${repositorySyncedQueryJson.graphHash}"`,
  "replaceFunctionBody main",
  "  let name String = \"Ada\"",
  "  let same Bool = std.mem.eql (std.mem.span name \"Ada\"",
  "end",
  "",
].join("\n"));
const repositoryInvalidRowsDryRun = json(["patch", "--json", "--check-only", graphRepositoryPatchPackageDir, graphRepositoryInvalidRowsPatchPath], { allowFailure: true });
assert.notEqual(repositoryInvalidRowsDryRun.code, 0);
assert.equal(repositoryInvalidRowsDryRun.body.diagnostic.code, "GPH001");
assert.match(repositoryInvalidRowsDryRun.body.diagnostic.actual, /row 2: let same Bool = std\.mem\.eql/);
writeFileSync(graphRepositoryInvalidBodyPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${repositorySyncedQueryJson.graphHash}"`,
  "replaceFunctionBody main",
  "  world.out.write \"bad\\n\"",
  "end",
  "",
].join("\n"));
const repositoryInvalidBodyDryRun = json(["patch", "--json", "--check-only", graphRepositoryPatchPackageDir, graphRepositoryInvalidBodyPatchPath], { allowFailure: true });
assert.notEqual(repositoryInvalidBodyDryRun.code, 0);
assert.equal(repositoryInvalidBodyDryRun.body.diagnostics[0].code, "ERR003");
assert.equal(repositoryInvalidBodyDryRun.body.diagnostics[0].message, "fallible function call must be checked");
assert.equal(repositoryInvalidBodyDryRun.body.diagnostics[0].path, "hello.0");
const repositoryInvalidBodyPatch = json(["patch", "--json", graphRepositoryPatchPackageDir, graphRepositoryInvalidBodyPatchPath], { allowFailure: true });
assert.notEqual(repositoryInvalidBodyPatch.code, 0);
assert.equal(repositoryInvalidBodyPatch.body.diagnostics[0].code, "ERR003");
assert.equal(json(["query", "--json", graphRepositoryPatchPackageDir]).body.graphHash, repositorySyncedQueryJson.graphHash);
const replaceFnBodyPath = join(outDir, "repository-graph.replace-fn.body");
writeFileSync(replaceFnBodyPath, [
  "  let name: Maybe<String> = std.args.get(1)",
  "  if name.has {",
  "    check world.out.write(\"hi \")",
  "    check world.out.write(name.value)",
  "    check world.out.write(\"\\n\")",
  "  } else {",
  "    check world.out.write(\"hi anonymous\\n\")",
  "  }",
  "",
].join("\n"));
const replaceFnPatchJson = json(["patch", "--json", graphRepositoryPatchPackageDir, "--expect-graph-hash", repositorySyncedQueryJson.graphHash, "--replace-fn", "main", "--body-file", replaceFnBodyPath]).body;
assert.equal(replaceFnPatchJson.ok, true);
assert.equal(replaceFnPatchJson.patch, replaceFnBodyPath);
assert.equal(replaceFnPatchJson.operationCount, 1);
assert.equal(replaceFnPatchJson.operations[0].op, "replaceFunctionBody");
assert.equal(replaceFnPatchJson.saved.path, join(graphRepositoryPatchPackageDir, "zero.graph"));
assert.match(zero(["view", "--fn", "main", graphRepositoryPatchPackageDir]).stdout, /check world\.out\.write\("hi "\)/);
assert.equal(zero(["run", graphRepositoryPatchPackageDir, "--", "Ada"]).stdout, "hi Ada\n");
const replaceFnStaleHash = json(["patch", "--json", graphRepositoryPatchPackageDir, "--expect-graph-hash", repositorySyncedQueryJson.graphHash, "--replace-fn", "main", "--body-file", replaceFnBodyPath], { allowFailure: true });
assert.notEqual(replaceFnStaleHash.code, 0);
assert.equal(replaceFnStaleHash.body.diagnostic.code, "GPH002");
const replaceFnInvalidBodyPath = join(outDir, "repository-graph.replace-fn.invalid.body");
writeFileSync(replaceFnInvalidBodyPath, "  let same Bool = std.mem.eql (std.mem.span name \"Ada\"\n");
const replaceFnInvalidText = zero(["patch", graphRepositoryPatchPackageDir, "--replace-fn", "main", "--body-file", replaceFnInvalidBodyPath], { allowFailure: true });
assert.notEqual(replaceFnInvalidText.code, 0);
assert.match(replaceFnInvalidText.stderr, /replaceFunctionBody rows did not parse as a Zero function body/);
assert.match(replaceFnInvalidText.stderr, /row 1: let same Bool = std\.mem\.eql \(std\.mem\.span name "Ada"/);
const replaceFnInvalidJson = json(["patch", "--json", graphRepositoryPatchPackageDir, "--replace-fn", "main", "--body-file", replaceFnInvalidBodyPath], { allowFailure: true });
assert.notEqual(replaceFnInvalidJson.code, 0);
assert.equal(replaceFnInvalidJson.body.diagnostic.code, "GPH001");
assert.match(replaceFnInvalidJson.body.diagnostic.actual, /row 1: let same Bool = std\.mem\.eql/);
const replaceFnEmptyBodyPath = join(outDir, "repository-graph.replace-fn.empty.body");
writeFileSync(replaceFnEmptyBodyPath, "\n");
const replaceFnEmptyJson = json(["patch", "--json", graphRepositoryPatchPackageDir, "--replace-fn", "main", "--body-file", replaceFnEmptyBodyPath], { allowFailure: true });
assert.notEqual(replaceFnEmptyJson.code, 0);
assert.equal(replaceFnEmptyJson.body.diagnostic.code, "GPH001");
assert.equal(replaceFnEmptyJson.body.diagnostic.message, "function body file is empty");
const replaceFnMissingBodyFlag = json(["patch", "--json", graphRepositoryPatchPackageDir, "--replace-fn", "main"], { allowFailure: true });
assert.notEqual(replaceFnMissingBodyFlag.code, 0);
assert.match(replaceFnMissingBodyFlag.body.diagnostics[0].message, /needs both --replace-fn and --body-file/);
assert.equal(replaceFnMissingBodyFlag.body.diagnostics[0].actual, "missing --body-file");
const replaceFnAmbiguousSource = json(["patch", "--json", graphRepositoryPatchPackageDir, graphRepositoryBodyPatchPath, "--replace-fn", "main", "--body-file", replaceFnBodyPath], { allowFailure: true });
assert.notEqual(replaceFnAmbiguousSource.code, 0);
assert.match(replaceFnAmbiguousSource.body.diagnostics[0].message, /graph patch source is ambiguous/);
const replaceFnStdinHash = json(["query", "--json", graphRepositoryPatchPackageDir]).body.graphHash;
const replaceFnStdinBody = '  check world.out.write("quoted \\"rows\\" with $HOME and C:\\\\path\\n")\n';
const replaceFnStdinText = zeroWithInput(["patch", graphRepositoryPatchPackageDir, "--expect-graph-hash", replaceFnStdinHash, "--replace-fn", "main", "--body-file", "-"], replaceFnStdinBody);
assertPatchOkOutput(replaceFnStdinText.stdout, join(graphRepositoryPatchPackageDir, "zero.graph"));
assert.match(zero(["view", "--fn", "main", graphRepositoryPatchPackageDir]).stdout, /quoted \\"rows\\" with \$HOME and C:\\\\path/);
assert.equal(zero(["run", graphRepositoryPatchPackageDir]).stdout, 'quoted "rows" with $HOME and C:\\path\n');
const replaceFnStdinJsonHash = json(["query", "--json", graphRepositoryPatchPackageDir]).body.graphHash;
const replaceFnStdinJsonRun = zeroWithInput(["patch", "--json", graphRepositoryPatchPackageDir, "--expect-graph-hash", replaceFnStdinJsonHash, "--replace-fn", "main", "--body-file", "-"], '  check world.out.write("stdin body update\\n")\n');
const replaceFnStdinJson = JSON.parse(replaceFnStdinJsonRun.stdout);
assert.equal(replaceFnStdinJson.ok, true);
assert.equal(replaceFnStdinJson.patch, "<stdin>");
assert.equal(replaceFnStdinJson.operationCount, 1);
assert.equal(replaceFnStdinJson.operations[0].op, "replaceFunctionBody");
assert.equal(replaceFnStdinJson.saved.path, join(graphRepositoryPatchPackageDir, "zero.graph"));
assert.equal(zero(["run", graphRepositoryPatchPackageDir]).stdout, "stdin body update\n");
const replaceFnStdinEmpty = zeroWithInput(["patch", "--json", graphRepositoryPatchPackageDir, "--replace-fn", "main", "--body-file", "-"], "\n", { allowFailure: true });
assert.notEqual(replaceFnStdinEmpty.code, 0);
const replaceFnStdinEmptyBody = JSON.parse(replaceFnStdinEmpty.stdout);
assert.equal(replaceFnStdinEmptyBody.diagnostic.code, "GPH001");
assert.equal(replaceFnStdinEmptyBody.diagnostic.message, "function body file is empty");
assert.equal(replaceFnStdinEmptyBody.diagnostic.actual, "<stdin>");
// Surgical in-function replacement: --replace-in-fn finds --old as a literal
// substring of the canonical body projection (the text zero view --fn prints),
// requires it unique within the function, and revalidates exactly like
// --replace-fn, all in one call.
const replaceInFnHash = json(["query", "--json", graphRepositoryPatchPackageDir]).body.graphHash;
const replaceInFnSingle = zero(["patch", graphRepositoryPatchPackageDir, "--expect-graph-hash", replaceInFnHash, "--replace-in-fn", "main", "--old", "stdin body update\\n", "--new", "surgical update\\n"]);
assertPatchOkOutput(replaceInFnSingle.stdout, join(graphRepositoryPatchPackageDir, "zero.graph"));
assert.equal(zero(["run", graphRepositoryPatchPackageDir]).stdout, "surgical update\n");
const replaceInFnBodyRows = [
  '  let name: Maybe<String> = std.args.get(1)',
  '  if name.has {',
  '    check world.out.write("hey ")',
  '    check world.out.write(name.value)',
  '    check world.out.write("\\n")',
  '  } else {',
  '    check world.out.write("hey anonymous\\n")',
  '  }',
  '',
].join("\n");
zeroWithInput(["patch", graphRepositoryPatchPackageDir, "--replace-fn", "main", "--body-file", "-"], replaceInFnBodyRows);
const replaceInFnView = zero(["view", "--fn", "main", graphRepositoryPatchPackageDir]).stdout;
const replaceInFnViewLines = replaceInFnView.split("\n");
const replaceInFnHeyIndex = replaceInFnViewLines.findIndex((line) => line.includes('"hey "'));
assert(replaceInFnHeyIndex > 0, "expected the replaced body in zero view --fn main");
// multi-line --old/--new through inline \n escapes, exactly as view prints the rows
const replaceInFnMultiOld = `${replaceInFnViewLines[replaceInFnHeyIndex]}\\n${replaceInFnViewLines[replaceInFnHeyIndex + 1]}`;
const replaceInFnMultiNew = replaceInFnMultiOld.replace('"hey "', '"hi "');
const replaceInFnMulti = zero(["patch", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old", replaceInFnMultiOld, "--new", replaceInFnMultiNew]);
assertPatchOkOutput(replaceInFnMulti.stdout, join(graphRepositoryPatchPackageDir, "zero.graph"));
assert.equal(zero(["run", graphRepositoryPatchPackageDir, "--", "Ada"]).stdout, "hi Ada\n");
// non-unique --old fails with the occurrence count and a view-oriented hint
const replaceInFnAmbiguous = zero(["patch", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old", "check world.out.write(", "--new", "check world.err.write("], { allowFailure: true });
assert.notEqual(replaceInFnAmbiguous.code, 0);
assert.match(replaceInFnAmbiguous.stderr, /replace-in-fn --old text matches 4 places in function 'main'/);
assert.match(replaceInFnAmbiguous.stderr, /zero view --fn main/);
// missing --old fails without touching the store
const replaceInFnMissBefore = json(["query", "--json", graphRepositoryPatchPackageDir]).body.graphHash;
const replaceInFnMiss = zero(["patch", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old", "zzz-not-here", "--new", "anything"], { allowFailure: true });
assert.notEqual(replaceInFnMiss.code, 0);
assert.match(replaceInFnMiss.stderr, /replace-in-fn --old text was not found in function 'main'/);
assert.match(replaceInFnMiss.stderr, /zero view --fn main/);
assert.equal(json(["query", "--json", graphRepositoryPatchPackageDir]).body.graphHash, replaceInFnMissBefore);
// stdin variants: --old-file - and --new-file - read the text verbatim
const replaceInFnStdinOld = zeroWithInput(["patch", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old-file", "-", "--new", replaceInFnViewLines[replaceInFnHeyIndex].replace('"hey "', '"hello "')], replaceInFnViewLines[replaceInFnHeyIndex].replace('"hey "', '"hi "'));
assertPatchOkOutput(replaceInFnStdinOld.stdout, join(graphRepositoryPatchPackageDir, "zero.graph"));
assert.equal(zero(["run", graphRepositoryPatchPackageDir, "--", "Ada"]).stdout, "hello Ada\n");
const replaceInFnStdinNew = zeroWithInput(["patch", "--json", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old", '"hello "', "--new-file", "-"], '"howdy "');
const replaceInFnStdinNewBody = JSON.parse(replaceInFnStdinNew.stdout);
assert.equal(replaceInFnStdinNewBody.ok, true);
assert.equal(replaceInFnStdinNewBody.patch, "<replace-in-fn>");
assert.equal(replaceInFnStdinNewBody.operationCount, 1);
assert.equal(replaceInFnStdinNewBody.operations[0].op, "replaceFunctionBody");
assert.equal(zero(["run", graphRepositoryPatchPackageDir, "--", "Ada"]).stdout, "howdy Ada\n");
// a replacement that introduces a diagnostic fails revalidation with
// baseline-diff behavior and leaves the store untouched
const replaceInFnInvalidBefore = json(["query", "--json", graphRepositoryPatchPackageDir]).body.graphHash;
const replaceInFnInvalid = zero(["patch", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old", 'check world.out.write("howdy ")', "--new", 'world.out.write("howdy ")'], { allowFailure: true });
assert.notEqual(replaceInFnInvalid.code, 0);
assert.match(replaceInFnInvalid.stderr, /ERR003: fallible function call must be checked/);
assert.match(replaceInFnInvalid.stderr, /program graph patch failed revalidation: 1 diagnostic introduced by this patch/);
assert.equal(json(["query", "--json", graphRepositoryPatchPackageDir]).body.graphHash, replaceInFnInvalidBefore);
// incomplete or doubled flag sets fail with actionable diagnostics
const replaceInFnMissingNew = json(["patch", "--json", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old", "x"], { allowFailure: true });
assert.notEqual(replaceInFnMissingNew.code, 0);
assert.match(replaceInFnMissingNew.body.diagnostics[0].message, /needs --replace-in-fn, --old, and --new/);
assert.equal(replaceInFnMissingNew.body.diagnostics[0].actual, "missing --new");
const replaceInFnDoubleStdin = json(["patch", "--json", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old-file", "-", "--new-file", "-"], { allowFailure: true });
assert.notEqual(replaceInFnDoubleStdin.code, 0);
assert.match(replaceInFnDoubleStdin.body.diagnostics[0].message, /cannot read both texts from stdin/);
const replaceInFnMixedSource = json(["patch", "--json", graphRepositoryPatchPackageDir, "--replace-in-fn", "main", "--old", "x", "--new", "y", "--replace-fn", "main", "--body-file", "-"], { allowFailure: true });
assert.notEqual(replaceInFnMixedSource.code, 0);
assert.match(replaceInFnMixedSource.body.diagnostics[0].message, /graph patch source is ambiguous/);
const genericPatchRoot = join(outDir, "repository-graph-generic-patch");
rmSync(genericPatchRoot, { recursive: true, force: true });
mkdirSync(join(genericPatchRoot, "src"), { recursive: true });
writeZeroTomlSync(genericPatchRoot, { package: { name: "generic-patch", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "src/main.0" } } });
writeFileSync(join(genericPatchRoot, "src", "main.0"), [
  "fn tail(data: Span<u8>, offset: usize) -> Span<u8> {",
  "    return std.mem.dropPrefix(data, offset)",
  "}",
  "",
  "fn pick(data: Span<u8>, limit: usize) -> usize {",
  "    var i: usize = 0",
  "    var total: usize = 0",
  "    while i < limit {",
  "        total = total + std.mem.len(tail(data, i))",
  "        i = i + 1_usize",
  "    }",
  "    return total",
  "}",
  "",
  "pub fn main(world: World) -> Void raises {",
  "    let bytes: Span<u8> = \"abc\"",
  "    if pick(bytes, 2) > 0 {",
  "        check world.out.write(\"generic patch ok\\n\")",
  "    }",
  "}",
  "",
].join("\n"));
assert.equal(json(["import", "--format", "text", "--json", genericPatchRoot]).code, 0);
assert.equal(zero(["check", genericPatchRoot]).stdout, "ok\n");
const genericPatchDump = zero(["dump", genericPatchRoot]).stdout;
const genericComparisonNode = genericPatchDump.match(/node (#\S+) Call name:"<"/);
assert(genericComparisonNode, "expected a comparison Call node in the generic-bearing package dump");
const genericOperatorFlip = json(["patch", "--json", genericPatchRoot, "--op", `set node="${genericComparisonNode[1]}" field="name" expect="<" value="<="`]).body;
assert.equal(genericOperatorFlip.ok, true);
assert.equal(genericOperatorFlip.operations[0].ok, true);
assert.equal(zero(["check", genericPatchRoot]).stdout, "ok\n");
assert.match(zero(["view", genericPatchRoot]).stdout, /while i <= limit/);
assert.equal(zero(["run", genericPatchRoot]).stdout, "generic patch ok\n");
const genericViewBodyQuery = json(["query", "--json", genericPatchRoot]).body;
const genericViewSource = zero(["view", genericPatchRoot]).stdout.split("\n");
const genericPickStart = genericViewSource.findIndex((line) => line.startsWith("fn pick("));
assert(genericPickStart >= 0, "expected zero view to print fn pick");
const genericPickBody: string[] = [];
let genericPickDepth = 0;
for (const line of genericViewSource.slice(genericPickStart)) {
  genericPickDepth += (line.match(/\{/g) ?? []).length - (line.match(/\}/g) ?? []).length;
  genericPickBody.push(line);
  if (genericPickDepth === 0 && genericPickBody.length > 1) break;
}
const genericVerbatimPatchPath = join(outDir, "repository-graph-generic-verbatim.patch");
writeFileSync(genericVerbatimPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${genericViewBodyQuery.graphHash}"`,
  "replaceFunctionBody pick",
  ...genericPickBody.slice(1, -1).map((line) => `  ${line}`),
  "end",
  "",
].join("\n"));
const genericVerbatimPatch = json(["patch", "--json", genericPatchRoot, genericVerbatimPatchPath]).body;
assert.equal(genericVerbatimPatch.ok, true);
assert.equal(genericVerbatimPatch.operations[0].op, "replaceFunctionBody");
assert.equal(zero(["check", genericPatchRoot]).stdout, "ok\n");
assert.equal(zero(["run", genericPatchRoot]).stdout, "generic patch ok\n");
const genericTypedRowsQuery = json(["query", "--json", genericPatchRoot]).body;
const genericTypedRowsPatchPath = join(outDir, "repository-graph-generic-typed-rows.patch");
writeFileSync(genericTypedRowsPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${genericTypedRowsQuery.graphHash}"`,
  "replaceFunctionBody pick",
  "  let n: usize = std.mem.len(tail(data, 0))",
  "  var total: usize = 0",
  "  if n > 0 && limit > 0 {",
  "      total = n + limit",
  "  } else {",
  "      total = limit",
  "  }",
  "  return total",
  "end",
  "",
].join("\n"));
const genericTypedRowsPatch = json(["patch", "--json", genericPatchRoot, genericTypedRowsPatchPath]).body;
assert.equal(genericTypedRowsPatch.ok, true);
assert.equal(genericTypedRowsPatch.operations[0].op, "replaceFunctionBody");
const genericTypedRowsView = zero(["view", genericPatchRoot]).stdout;
assert.match(genericTypedRowsView, /let n: usize = std\.mem\.len\(tail\(data, 0\)\)/);
assert.match(genericTypedRowsView, /if n > 0 && limit > 0/);
assert.equal(zero(["check", genericPatchRoot]).stdout, "ok\n");
const genericUnknownOpPatchPath = join(outDir, "repository-graph-generic-unknown-op.patch");
writeFileSync(genericUnknownOpPatchPath, [
  "zero-program-graph-patch v1",
  'setNodeField node="#missing" value="oops"',
  "",
].join("\n"));
const genericUnknownOp = json(["patch", "--json", "--check-only", genericPatchRoot, genericUnknownOpPatchPath], { allowFailure: true });
assert.notEqual(genericUnknownOp.code, 0);
assert.equal(genericUnknownOp.body.diagnostic.code, "GPH001");
assert.match(genericUnknownOp.body.diagnostic.message, /unknown program graph patch operation/);
assert.match(genericUnknownOp.body.diagnostic.message, /zero patch --op help/);
assert.match(genericUnknownOp.body.diagnostic.expected, /set, insert, insertEdge, replace, replaceExpr, delete, rename/);
assert.match(genericUnknownOp.body.diagnostic.expected, /replaceFunctionBody/);
assert.equal(genericUnknownOp.body.diagnostic.line, 2);
const genericUnknownOpText = zero(["patch", "--check-only", genericPatchRoot, genericUnknownOpPatchPath], { allowFailure: true });
assert.notEqual(genericUnknownOpText.code, 0);
assert.match(genericUnknownOpText.stderr, /unknown program graph patch operation/);
assert.match(genericUnknownOpText.stderr, new RegExp(`${genericUnknownOpPatchPath.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}:2: setNodeField`));
assert.match(genericUnknownOpText.stderr, /help: a minimal complete patch file looks exactly like this:\nzero-program-graph-patch v1\nreplaceFunctionBody main\n {2}check world\.out\.write "hello\\n"\nend\n/);
const genericMissingHeaderPatchPath = join(outDir, "repository-graph-generic-missing-header.patch");
writeFileSync(genericMissingHeaderPatchPath, [
  "replaceFunctionBody main",
  '  check world.out.write("missing header\\n")',
  "end",
  "",
].join("\n"));
const genericMissingHeader = zero(["patch", "--check-only", genericPatchRoot, genericMissingHeaderPatchPath], { allowFailure: true });
assert.notEqual(genericMissingHeader.code, 0);
assert.match(genericMissingHeader.stderr, /unknown program graph patch schema/);
assert.match(genericMissingHeader.stderr, new RegExp(`${genericMissingHeaderPatchPath.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}:1: replaceFunctionBody main`));
assert.match(genericMissingHeader.stderr, /expected: zero-program-graph-patch v1/);
assert.match(genericMissingHeader.stderr, /help: a minimal complete patch file looks exactly like this:/);
const genericMissingEndPatchPath = join(outDir, "repository-graph-generic-missing-end.patch");
writeFileSync(genericMissingEndPatchPath, [
  "zero-program-graph-patch v1",
  "replaceFunctionBody main",
  '  check world.out.write("missing end\\n")',
  "",
].join("\n"));
const genericMissingEnd = zero(["patch", "--check-only", genericPatchRoot, genericMissingEndPatchPath], { allowFailure: true });
assert.notEqual(genericMissingEnd.code, 0);
assert.match(genericMissingEnd.stderr, /body patch is missing end marker/);
assert.match(genericMissingEnd.stderr, new RegExp(`${genericMissingEndPatchPath.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}:2: replaceFunctionBody main`));
assert.match(genericMissingEnd.stderr, /help: a minimal complete patch file looks exactly like this:/);
const genericIndentedHeaderPatchPath = join(outDir, "repository-graph-generic-indented-header.patch");
writeFileSync(genericIndentedHeaderPatchPath, [
  "  zero-program-graph-patch v1",
  "  replaceFunctionBody main",
  '    check world.out.write("indented header ok\\n")',
  "  end",
  "",
].join("\n"));
const genericIndentedHeaderCheckOnly = zero(["patch", "--check-only", genericPatchRoot, genericIndentedHeaderPatchPath]).stdout;
assert.match(genericIndentedHeaderCheckOnly, /^program graph patch ok \(check-only\)\n/);
assert.match(genericIndentedHeaderCheckOnly, /validated: check-equivalent\n$/);

const headerlessOpsRoot = join(outDir, "repository-graph-headerless-ops");
rmSync(headerlessOpsRoot, { recursive: true, force: true });
mkdirSync(join(headerlessOpsRoot, "src"), { recursive: true });
writeZeroTomlSync(headerlessOpsRoot, { package: { name: "headerless-ops", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "src/main.0" } } });
writeFileSync(join(headerlessOpsRoot, "src", "main.0"), [
  "const LIMIT: usize = 4",
  "",
  "fn bump(value: usize) -> usize {",
  "    return value + LIMIT",
  "}",
  "",
  "fn answer() -> usize {",
  "    return LIMIT",
  "}",
  "",
  "pub fn main(world: World) -> Void raises {",
  "    if bump(1) == answer() + 1 {",
  "        check world.out.write(\"headerless ops ok\\n\")",
  "    }",
  "}",
  "",
  "test \"answer works\" {",
  "    expect (answer() == 4)",
  "}",
  "",
].join("\n"));
assert.equal(json(["import", "--format", "text", "--json", headerlessOpsRoot]).code, 0);
const headerlessAnswerHandles = zero(["view", "--fn", "answer", "--handles", headerlessOpsRoot]).stdout;
const headerlessReturnHandle = headerlessAnswerHandles.match(/return LIMIT\s+\/\/ (#\S+)/);
assert(headerlessReturnHandle, "expected answer return handle for headerless replaceExpr detection");
function assertHeaderlessPatchFileDetected(name: string, rows: string[]) {
  const patchPath = join(headerlessOpsRoot, `${name}.patch`);
  writeFileSync(patchPath, [...rows, ""].join("\n"));
  const result = zeroWithStderr(["patch", "--check-only", `${name}.patch`], { cwd: headerlessOpsRoot });
  assert.notEqual(result.code, 0);
  assert.match(result.stderr, /unknown program graph patch schema/);
  assert.match(result.stderr, new RegExp(`${name}\\.patch:1: ${rows[0].replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}`));
  assert.match(result.stderr, /expected: zero-program-graph-patch v1/);
}
assertHeaderlessPatchFileDetected("set-const", ['setConst name="LIMIT" value="5"']);
assertHeaderlessPatchFileDetected("set-return-type", [
  'setReturnType fn="answer" type="i64"',
  "replaceFunctionBody answer",
  "  return 4 as i64",
  "end",
]);
assertHeaderlessPatchFileDetected("replace-expr", [`replaceExpr node="${headerlessReturnHandle[1]}" with="LIMIT + 0"`]);
assertHeaderlessPatchFileDetected("delete-test", ['deleteTest name="answer works"']);
assertHeaderlessPatchFileDetected("rename-test", ['renameTest name="answer works" value="answer still works"']);
assertHeaderlessPatchFileDetected("add-param-to", ['addParamTo fn="bump" name="bias" type="usize" default="0"']);

// Declaration-level patch ops: setConst, addParamTo, setReturnType.
const declOpsRoot = join(outDir, "repository-graph-decl-ops");
rmSync(declOpsRoot, { recursive: true, force: true });
mkdirSync(join(declOpsRoot, "src"), { recursive: true });
writeZeroTomlSync(declOpsRoot, { package: { name: "decl-ops", version: "0.1.0" }, targets: { cli: { kind: "exe", main: "src/main.0" } } });
writeFileSync(join(declOpsRoot, "src", "main.0"), [
  "const LIMIT: usize = 4",
  "",
  "fn bump(value: usize) -> usize {",
  "    return value + LIMIT",
  "}",
  "",
  "fn twice(value: usize) -> usize {",
  "    return bump(bump(value))",
  "}",
  "",
  "fn answer() -> usize {",
  "    return LIMIT",
  "}",
  "",
  "fn wide(a: Span<u8>, b: Span<u8>, c: Span<u8>, d: usize) -> usize {",
  "    return std.mem.len(a) + std.mem.len(b) + std.mem.len(c) + d",
  "}",
  "",
  "pub fn main(world: World) -> Void raises {",
  "    let start: usize = bump(1)",
  "    let total: usize = twice(start)",
  "    let padding: usize = wide(\"a\", \"b\", \"c\", 0)",
  "    if total + padding > answer() {",
  "        check world.out.write(\"decl ops ok\\n\")",
  "    }",
  "}",
  "",
].join("\n"));
assert.equal(json(["import", "--format", "text", "--json", declOpsRoot]).code, 0);
assert.equal(zero(["check", declOpsRoot]).stdout, "ok\n");
const declOpsViewBefore = zero(["view", declOpsRoot]).stdout;
assert.match(declOpsViewBefore, /const LIMIT: usize = 4/);
// setConst round trip: change the initializer, then restore it.
const declOpsSetConst = zero(["patch", declOpsRoot, "--op", 'setConst name="LIMIT" value="8"']);
assertPatchOkOutput(declOpsSetConst.stdout, join(declOpsRoot, "zero.graph"));
assert.match(declOpsSetConst.stdout, /validated: check-equivalent\n$/);
assert.match(zero(["view", declOpsRoot]).stdout, /const LIMIT: usize = 8/);
assert.equal(zero(["check", declOpsRoot]).stdout, "ok\n");
const declOpsSetConstBack = json(["patch", "--json", declOpsRoot, "--op", 'setConst name="LIMIT" value="4"']).body;
assert.equal(declOpsSetConstBack.ok, true);
assert.equal(declOpsSetConstBack.operations[0].op, "setConst");
assert.equal(zero(["view", declOpsRoot]).stdout, declOpsViewBefore);
// setConst revalidation failure rolls the store back untouched.
const declOpsStoreBefore = readFileSync(join(declOpsRoot, "zero.graph"), "utf8");
const declOpsBadConst = zero(["patch", declOpsRoot, "--op", 'setConst name="LIMIT" value="missingName"'], { allowFailure: true });
assert.notEqual(declOpsBadConst.code, 0);
assert.match(declOpsBadConst.stderr, /program graph patch failed revalidation: 1 diagnostic introduced by this patch/);
assert.equal(readFileSync(join(declOpsRoot, "zero.graph"), "utf8"), declOpsStoreBefore);
assert.match(zero(["view", declOpsRoot]).stdout, /const LIMIT: usize = 4/);
// setConst misses fail with the nearest const name.
const declOpsConstMiss = zero(["patch", declOpsRoot, "--op", 'setConst name="LIMTI" value="8"'], { allowFailure: true });
assert.notEqual(declOpsConstMiss.code, 0);
assert.match(declOpsConstMiss.stderr, /setConst const was not found; nearest: LIMIT/);
// addParamTo without default fails with the call-site count.
const declOpsNoDefault = zero(["patch", declOpsRoot, "--op", 'addParamTo fn="bump" name="bias" type="usize"'], { allowFailure: true });
assert.notEqual(declOpsNoDefault.code, 0);
assert.match(declOpsNoDefault.stderr, /addParamTo needs a default to update existing call sites/);
assert.match(declOpsNoDefault.stderr, /3 call sites call bump/);
// addParamTo with default appends the parameter and updates every call site,
// including the nested bump(bump(value)) calls inside twice.
const declOpsAddParam = zero(["patch", declOpsRoot, "--op", 'addParamTo fn="bump" name="bias" type="usize" default="0"']);
assertPatchOkOutput(declOpsAddParam.stdout, join(declOpsRoot, "zero.graph"));
assert.match(declOpsAddParam.stdout, /updated 3 call sites\n/);
const declOpsAddParamView = zero(["view", declOpsRoot]).stdout;
assert.match(declOpsAddParamView, /fn bump\(value: usize, bias: usize\) -> usize/);
assert.match(declOpsAddParamView, /return bump\(bump\(value, 0\), 0\)/);
assert.match(declOpsAddParamView, /let start: usize = bump\(1, 0\)/);
assert.equal(zero(["check", declOpsRoot]).stdout, "ok\n");
const declOpsAddParamJson = json(["patch", "--json", declOpsRoot, "--op", 'addParamTo fn="answer" name="extra" type="usize" default="1"']).body;
assert.equal(declOpsAddParamJson.ok, true);
assert.equal(declOpsAddParamJson.operations[0].op, "addParamTo");
assert.equal(declOpsAddParamJson.operations[0].callSites, 1);
// The direct backends cap signatures at 8 ABI argument slots (byte views take
// 2); addParamTo rejects an over-cap signature at patch time.
const declOpsAbiCap = zero(["patch", declOpsRoot, "--op", 'addParamTo fn="wide" name="e" type="Span<u8>"'], { allowFailure: true });
assert.notEqual(declOpsAbiCap.code, 0);
assert.match(declOpsAbiCap.stderr, /direct backend object buildability has too many ABI argument slots/);
assert.match(declOpsAbiCap.stderr, /wide would have 9 ABI argument slot\(s\)/);
// Signature ops compose with other ops in one batched patch.
const declOpsBatchPath = join(outDir, "repository-graph-decl-ops-batch.patch");
writeFileSync(declOpsBatchPath, [
  "zero-program-graph-patch v1",
  'setConst name="LIMIT" value="6"',
  'setReturnType fn="answer" type="i64"',
  "replaceFunctionBody answer",
  "  return 9 as i64",
  "end",
  "",
].join("\n"));
const declOpsBatch = json(["patch", "--json", declOpsRoot, declOpsBatchPath]).body;
assert.equal(declOpsBatch.ok, true);
assert.equal(declOpsBatch.operationCount, 3);
assert.equal(declOpsBatch.operations[0].op, "setConst");
assert.equal(declOpsBatch.operations[1].op, "setReturnType");
assert.equal(declOpsBatch.operations[2].op, "replaceFunctionBody");
const declOpsBatchView = zero(["view", declOpsRoot]).stdout;
assert.match(declOpsBatchView, /const LIMIT: usize = 6/);
assert.match(declOpsBatchView, /fn answer\(extra: usize\) -> i64/);
assert.equal(zero(["check", declOpsRoot]).stdout, "ok\n");
const checkedInGraphCallsJson = json(["query", "--json", "--fn", "main", "--calls", "write", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphCallsJson.ok, true);
assert.equal(checkedInGraphCallsJson.query.function, "main");
assert.equal(checkedInGraphCallsJson.query.calls, "write");
assert.equal(checkedInGraphCallsJson.references.length, 0);
assert.equal(checkedInGraphCallsJson.calls.some((call) => call.function === "main" && call.name === "write" && call.resolved === true), true);
const checkedInGraphPackageCheckJson = json(["check", "--json", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageCheckJson.ok, true);
assert.equal(checkedInGraphPackageCheckJson.sourceFile, checkedInRepositoryGraphStorePath);
assert.equal(checkedInGraphPackageCheckJson.package.name, "program-graph-fixture");
assert.equal(checkedInGraphPackageCheckJson.package.manifestPath, join(checkedInGraphPackageDir, "zero.toml"));
assertSourceGraph(checkedInGraphPackageCheckJson, checkedInRepositoryGraphStorePath, "package:program-graph-fixture@0.1.0", "graph-native-check", false);
assertProgramGraphCompilerInput(checkedInGraphPackageCheckJson, checkedInRepositoryGraphStorePath);
assertRepositoryGraphNativeCheck(checkedInGraphPackageCheckJson);
const sourceFreeCopiedGraphRoot = join(outDir, "source-free-program-graph");
const sourceFreeCopiedGraphStorePath = join(sourceFreeCopiedGraphRoot, "zero.graph");
const sourceFreeCopiedGraphBuildPath = join(outDir, "source-free-program-graph-build");
const sourceFreeCopiedGraphRunPath = join(outDir, "source-free-program-graph-run");
const sourceFreeCImportRoot = join(outDir, "source-free-c-import");
const sourceFreeCImportStorePath = join(sourceFreeCImportRoot, "zero.graph");
const sourceFreeCImportRunPath = join(outDir, "source-free-c-import-run");
const sourceFreeCImportCwdRoot = join(outDir, "source-free-c-import-unrelated-cwd");
const sourceFreeCImportCwdBuildPath = join(outDir, "source-free-c-import-cwd-build");
const sourceFreeIdentityMismatchRoot = join(outDir, "source-free-identity-mismatch");
const sourceFreeMissingPackageNameRoot = join(outDir, "source-free-missing-package-name");
const sourceFreeBadProjectionRoot = join(outDir, "source-free-bad-projection");
rmSync(sourceFreeCopiedGraphRoot, { recursive: true, force: true });
rmSync(sourceFreeCImportRoot, { recursive: true, force: true });
rmSync(sourceFreeCImportRunPath, { recursive: true, force: true });
rmSync(sourceFreeCImportCwdRoot, { recursive: true, force: true });
rmSync(sourceFreeCImportCwdBuildPath, { recursive: true, force: true });
rmSync(sourceFreeIdentityMismatchRoot, { recursive: true, force: true });
rmSync(sourceFreeMissingPackageNameRoot, { recursive: true, force: true });
rmSync(sourceFreeBadProjectionRoot, { recursive: true, force: true });
mkdirSync(sourceFreeCopiedGraphRoot, { recursive: true });
writeFileSync(join(sourceFreeCopiedGraphRoot, "zero.toml"), readFileSync(join(checkedInGraphPackageDir, "zero.toml"), "utf8"));
writeFileSync(sourceFreeCopiedGraphStorePath, readFileSync(checkedInRepositoryGraphStorePath));
const sourceFreeCopiedGraphCheckJson = json(["check", "--json", sourceFreeCopiedGraphRoot]).body;
assert.equal(sourceFreeCopiedGraphCheckJson.ok, true);
assert.equal(sourceFreeCopiedGraphCheckJson.sourceFile, sourceFreeCopiedGraphStorePath);
assertSourceGraph(sourceFreeCopiedGraphCheckJson, sourceFreeCopiedGraphStorePath, "package:program-graph-fixture@0.1.0", "graph-native-check", false, "missing");
assertProgramGraphCompilerInput(sourceFreeCopiedGraphCheckJson, sourceFreeCopiedGraphStorePath);
assertRepositoryGraphNativeCheck(sourceFreeCopiedGraphCheckJson, "missing");
const sourceFreeCopiedGraphSizeJson = json(["size", "--json", "--target", "linux-musl-x64", sourceFreeCopiedGraphRoot]).body;
assertSourceGraph(sourceFreeCopiedGraphSizeJson, sourceFreeCopiedGraphStorePath, "package:program-graph-fixture@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(sourceFreeCopiedGraphSizeJson, sourceFreeCopiedGraphStorePath);
const sourceFreeCopiedGraphBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", sourceFreeCopiedGraphBuildPath, sourceFreeCopiedGraphRoot]).body;
assert.equal(sourceFreeCopiedGraphBuildJson.sourceFile, sourceFreeCopiedGraphStorePath);
assertSourceGraph(sourceFreeCopiedGraphBuildJson, sourceFreeCopiedGraphStorePath, "package:program-graph-fixture@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(sourceFreeCopiedGraphBuildJson, sourceFreeCopiedGraphStorePath);
assert.equal(zero(["run", "--out", sourceFreeCopiedGraphRunPath, sourceFreeCopiedGraphRoot]).stdout, "hello from zero\n");
const sourceFreeCopiedGraphTestJson = json(["test", "--json", sourceFreeCopiedGraphRoot]).body;
assert.equal(sourceFreeCopiedGraphTestJson.ok, true);
assertSourceGraph(sourceFreeCopiedGraphTestJson, sourceFreeCopiedGraphStorePath, "package:program-graph-fixture@0.1.0", "direct-program-graph", false, "missing");
assert.equal(sourceFreeCopiedGraphTestJson.testBackend, "direct-program-graph");
assert.equal(sourceFreeCopiedGraphTestJson.testDiscovery.mode, "package-graph");
const sourceFreeCopiedGraphMemJson = json(["mem", "--json", sourceFreeCopiedGraphRoot]).body;
assertSourceGraph(sourceFreeCopiedGraphMemJson, sourceFreeCopiedGraphStorePath, "package:program-graph-fixture@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(sourceFreeCopiedGraphMemJson, sourceFreeCopiedGraphStorePath);
const sourceFreeCopiedGraphVerify = json(["verify-projection", "--json", sourceFreeCopiedGraphRoot], { allowFailure: true });
assert.notEqual(sourceFreeCopiedGraphVerify.code, 0);
assert.equal(sourceFreeCopiedGraphVerify.body.diagnostics[0].code, "RGP006");
assert.equal(sourceFreeCopiedGraphVerify.body.diagnostics[0].actual, "missing source projection file");
const sourceFreeCopiedGraphExport = json(["export", "--json", sourceFreeCopiedGraphRoot]).body;
assert.equal(sourceFreeCopiedGraphExport.ok, true);
assert.deepEqual(sourceFreeCopiedGraphExport.changedPaths, [join(sourceFreeCopiedGraphRoot, "hello.0")]);
assert.equal(readFileSync(join(sourceFreeCopiedGraphRoot, "hello.0"), "utf8"), readFileSync(checkedInGraphSourcePath, "utf8"));
const sourceFreeCopiedGraphVerifyAfter = json(["verify-projection", "--json", sourceFreeCopiedGraphRoot]).body;
assert.equal(sourceFreeCopiedGraphVerifyAfter.ok, true);
assert.equal(sourceFreeCopiedGraphVerifyAfter.repositoryGraph.projectionValidity, "clean");
assert.deepEqual(json(["export", "--json", sourceFreeCopiedGraphRoot]).body.changedPaths, []);
mkdirSync(join(sourceFreeCImportRoot, "src"), { recursive: true });
mkdirSync(join(sourceFreeCImportRoot, "vendor/include"), { recursive: true });
writeZeroTomlSync(sourceFreeCImportRoot, {
  package: { name: "source-free-c-import", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  c: {
    libs: {
      ext: { headers: ["vendor/include/zero_ext.h"], include: ["vendor/include"], lib: [], link: [], mode: "static" },
    },
  },
});
writeFileSync(join(sourceFreeCImportRoot, "vendor/include/zero_ext.h"), "int zero_ext_add(int a, int b);\n");
writeFileSync(join(sourceFreeCImportRoot, "src/main.0"), `extern c "vendor/include/zero_ext.h" as c

pub fn main(world: World) -> Void raises {
    check world.out.write("source-free c import ok\\n")
}
`);
const sourceFreeCImportSync = json(["import", "--format", "text", "--json", sourceFreeCImportRoot]).body;
assert.equal(sourceFreeCImportSync.ok, true);
assert.equal(sourceFreeCImportSync.repositoryGraph.projectionValidity, "clean");
rmSync(join(sourceFreeCImportRoot, "src"), { recursive: true, force: true });
const sourceFreeCImportCheck = json(["check", "--json", sourceFreeCImportRoot]).body;
assert.equal(sourceFreeCImportCheck.ok, true);
assertSourceGraph(sourceFreeCImportCheck, sourceFreeCImportStorePath, "package:source-free-c-import@0.1.0", "graph-native-check", false, "missing");
assertProgramGraphCompilerInput(sourceFreeCImportCheck, sourceFreeCImportStorePath);
assertRepositoryGraphNativeCheck(sourceFreeCImportCheck, "missing");
assert.equal(zero(["run", "--out", sourceFreeCImportRunPath, sourceFreeCImportRoot]).stdout, "source-free c import ok\n");
mkdirSync(sourceFreeCImportCwdRoot, { recursive: true });
writeZeroTomlSync(sourceFreeCImportCwdRoot, {
  package: { name: "unrelated-cwd-package", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
});
const sourceFreeCImportCwdBuild = JSON.parse(execFileSync(zeroBin, [
  "build",
  "--json",
  "--out",
  resolve(sourceFreeCImportCwdBuildPath),
  resolve(sourceFreeCImportRoot),
], { cwd: sourceFreeCImportCwdRoot, encoding: "utf8", maxBuffer: execMaxBuffer, stdio: ["ignore", "pipe", "pipe"] }));
assertSourceGraph(sourceFreeCImportCwdBuild, resolve(sourceFreeCImportStorePath), "package:source-free-c-import@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(sourceFreeCImportCwdBuild, resolve(sourceFreeCImportStorePath));
mkdirSync(sourceFreeIdentityMismatchRoot, { recursive: true });
writeZeroTomlSync(sourceFreeIdentityMismatchRoot, {
  package: { name: "wrong-program-graph-fixture", version: "9.9.9" },
  targets: { cli: { kind: "exe", main: "hello.0" } },
});
writeFileSync(join(sourceFreeIdentityMismatchRoot, "zero.graph"), readFileSync(checkedInRepositoryGraphStorePath));
const sourceFreeIdentityMismatchCheck = json(["check", "--json", sourceFreeIdentityMismatchRoot], { allowFailure: true });
assert.notEqual(sourceFreeIdentityMismatchCheck.code, 0);
assert.equal(sourceFreeIdentityMismatchCheck.body.diagnostics[0].code, "RGP007");
assert.equal(sourceFreeIdentityMismatchCheck.body.diagnostics[0].expected, "package:wrong-program-graph-fixture@9.9.9");
assert.equal(sourceFreeIdentityMismatchCheck.body.diagnostics[0].actual, "package:program-graph-fixture@0.1.0");
const sourceFreeIdentityMismatchSize = json(["size", "--json", sourceFreeIdentityMismatchRoot], { allowFailure: true });
assert.notEqual(sourceFreeIdentityMismatchSize.code, 0);
assert.equal(sourceFreeIdentityMismatchSize.body.diagnostics[0].code, "RGP007");
mkdirSync(sourceFreeMissingPackageNameRoot, { recursive: true });
writeZeroTomlSync(sourceFreeMissingPackageNameRoot, {
  targets: { cli: { kind: "exe", main: "hello.0" } },
});
writeFileSync(join(sourceFreeMissingPackageNameRoot, "zero.graph"), readFileSync(checkedInRepositoryGraphStorePath));
const sourceFreeMissingPackageNameCheck = json(["check", "--json", sourceFreeMissingPackageNameRoot], { allowFailure: true });
assert.notEqual(sourceFreeMissingPackageNameCheck.code, 0);
assert.equal(sourceFreeMissingPackageNameCheck.body.diagnostics[0].code, "RGP007");
assert.equal(sourceFreeMissingPackageNameCheck.body.diagnostics[0].message, "repository graph compiler input requires package.name");
assert.match(sourceFreeMissingPackageNameCheck.body.diagnostics[0].actual, /package:program-graph-fixture@0\.1\.0/);
mkdirSync(sourceFreeBadProjectionRoot, { recursive: true });
writeFileSync(join(sourceFreeBadProjectionRoot, "zero.toml"), readFileSync(join(checkedInGraphPackageDir, "zero.toml"), "utf8"));
writeFileSync(join(sourceFreeBadProjectionRoot, "hello.0"), checkedInGraphSource);
zero(["import", "--format", "text", sourceFreeBadProjectionRoot]);
const sourceFreeBadProjectionStoreText = readFileSync(join(sourceFreeBadProjectionRoot, "zero.graph"), "utf8");
writeFileSync(join(sourceFreeBadProjectionRoot, "zero.graph"), sourceFreeBadProjectionStoreText.replace(
  /^projection path:"hello\.0" text:.*$/m,
  `projection path:"hello.0" text:${JSON.stringify("pub fn broken( {\n")}`,
));
const sourceFreeBadProjectionStatus = json(["status", "--json", sourceFreeBadProjectionRoot]).body;
assert.equal(sourceFreeBadProjectionStatus.repositoryGraph.projectionState, "conflict");
assert.equal(sourceFreeBadProjectionStatus.repositoryGraph.projectionValidity, "conflict");
const sourceFreeBadProjectionCheck = json(["check", "--json", sourceFreeBadProjectionRoot]).body;
assert.equal(sourceFreeBadProjectionCheck.ok, true);
assertSourceGraph(sourceFreeBadProjectionCheck, join(sourceFreeBadProjectionRoot, "zero.graph"), "package:program-graph-fixture@0.1.0", "graph-native-check", false, "conflict");
assertRepositoryGraphNativeCheck(sourceFreeBadProjectionCheck, "conflict");
const sourceFreeBadProjectionSync = json(["export", "--json", sourceFreeBadProjectionRoot], { allowFailure: true });
assert.notEqual(sourceFreeBadProjectionSync.code, 0);
assert.equal(sourceFreeBadProjectionSync.body.diagnostics[0].code, "RGP004");
const missingRepoGraphStoreRoot = join(outDir, "repository-graph-missing-store");
rmSync(missingRepoGraphStoreRoot, { recursive: true, force: true });
mkdirSync(missingRepoGraphStoreRoot, { recursive: true });
writeZeroTomlSync(missingRepoGraphStoreRoot, {
  package: { name: "repository-graph-missing-store", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
writeFileSync(join(missingRepoGraphStoreRoot, "main.0"), "pub fn main() -> i32 { return 0 }\n");
const missingRepoGraphStoreCheck = json(["check", "--json", missingRepoGraphStoreRoot], { allowFailure: true });
assert.notEqual(missingRepoGraphStoreCheck.code, 0);
assert.equal(missingRepoGraphStoreCheck.body.ok, false);
assert.equal(missingRepoGraphStoreCheck.body.mode, "compiler-input");
assert.equal(missingRepoGraphStoreCheck.body.repositoryGraph.storePresent, false);
assert.equal(missingRepoGraphStoreCheck.body.diagnostics[0].code, "RGP001");
assert.equal(missingRepoGraphStoreCheck.body.diagnostics[0].path, missingRepoGraphStoreRoot);
assert.match(missingRepoGraphStoreCheck.body.repairCommands.join("\n"), /zero import/);
const invalidRepoGraphStoreRoot = join(outDir, "repository-graph-invalid-store");
rmSync(invalidRepoGraphStoreRoot, { recursive: true, force: true });
mkdirSync(invalidRepoGraphStoreRoot, { recursive: true });
writeZeroTomlSync(invalidRepoGraphStoreRoot, {
  package: { name: "repository-graph-invalid-store", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
writeFileSync(join(invalidRepoGraphStoreRoot, "main.0"), "pub fn main() -> i32 { return 0 }\n");
writeFileSync(join(invalidRepoGraphStoreRoot, "zero.graph"), "not a repository graph\n");
const invalidRepoGraphStoreCheck = json(["check", "--json", invalidRepoGraphStoreRoot], { allowFailure: true });
assert.notEqual(invalidRepoGraphStoreCheck.code, 0);
assert.equal(invalidRepoGraphStoreCheck.body.ok, false);
assert.equal(invalidRepoGraphStoreCheck.body.mode, "compiler-input");
assert.equal(invalidRepoGraphStoreCheck.body.repositoryGraph.storePresent, true);
assert.equal(invalidRepoGraphStoreCheck.body.repositoryGraph.storeValid, false);
assert.equal(invalidRepoGraphStoreCheck.body.diagnostics[0].code, "RGP003");
assert.equal(invalidRepoGraphStoreCheck.body.diagnostics[0].path, invalidRepoGraphStoreRoot);
assert.match(invalidRepoGraphStoreCheck.body.repairCommands.join("\n"), /zero import/);
const directoryRepoGraphStoreRoot = join(outDir, "repository-graph-directory-store");
rmSync(directoryRepoGraphStoreRoot, { recursive: true, force: true });
mkdirSync(join(directoryRepoGraphStoreRoot, "zero.graph"), { recursive: true });
writeZeroTomlSync(directoryRepoGraphStoreRoot, {
  package: { name: "repository-graph-directory-store", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
writeFileSync(join(directoryRepoGraphStoreRoot, "main.0"), "pub fn main() -> i32 { return 0 }\n");
const directoryRepoGraphStoreCheck = json(["check", "--json", directoryRepoGraphStoreRoot], { allowFailure: true });
assert.notEqual(directoryRepoGraphStoreCheck.code, 0);
assert.equal(directoryRepoGraphStoreCheck.body.ok, false);
assert.equal(directoryRepoGraphStoreCheck.body.repositoryGraph.storePresent, true);
assert.equal(directoryRepoGraphStoreCheck.body.repositoryGraph.projectionState, "store-invalid");
assert.equal(directoryRepoGraphStoreCheck.body.diagnostics[0].code, "RGP003");
assert.equal(directoryRepoGraphStoreCheck.body.diagnostics[0].actual, "failed to read repository graph store");
const sourceFreeGraphPackageRoot = join(outDir, "source-free-graph-package");
rmSync(sourceFreeGraphPackageRoot, { recursive: true, force: true });
mkdirSync(join(sourceFreeGraphPackageRoot, "src"), { recursive: true });
writeZeroTomlSync(sourceFreeGraphPackageRoot, {
  package: { name: "source-free-graph-package", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
});
writeFileSync(join(sourceFreeGraphPackageRoot, "src", "main.0"), readFileSync("conformance/packages/test-app/src/main.0", "utf8"));
writeFileSync(join(sourceFreeGraphPackageRoot, "src", "helper.0"), readFileSync("conformance/packages/test-app/src/helper.0", "utf8"));
const sourceFreeGraphPackageSync = json(["import", "--format", "text", "--json", sourceFreeGraphPackageRoot]);
assert.equal(sourceFreeGraphPackageSync.body.ok, true);
rmSync(join(sourceFreeGraphPackageRoot, "src"), { recursive: true, force: true });
const sourceFreeGraphPackageStorePath = join(sourceFreeGraphPackageRoot, "zero.graph");
const sourceFreeGraphPackageCheckJson = json(["check", "--json", sourceFreeGraphPackageRoot]).body;
assert.equal(sourceFreeGraphPackageCheckJson.ok, true);
assertSourceGraph(sourceFreeGraphPackageCheckJson, sourceFreeGraphPackageStorePath, "package:source-free-graph-package@0.1.0", "graph-native-check", false, "missing");
assertProgramGraphCompilerInput(sourceFreeGraphPackageCheckJson, sourceFreeGraphPackageStorePath);
assertRepositoryGraphNativeCheck(sourceFreeGraphPackageCheckJson, "missing");
assert(sourceFreeGraphPackageCheckJson.interfaceFingerprints.modules.some((module) => module.name === "main"));
assert(sourceFreeGraphPackageCheckJson.interfaceFingerprints.modules.some((module) => module.name === "main" && module.imports.some((entry) => entry.module === "helper")));
const sourceFreeGraphPackageSizeJson = json(["size", "--json", "--target", "linux-musl-x64", sourceFreeGraphPackageRoot]).body;
assertSourceGraph(sourceFreeGraphPackageSizeJson, sourceFreeGraphPackageStorePath, "package:source-free-graph-package@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(sourceFreeGraphPackageSizeJson, sourceFreeGraphPackageStorePath);
const sourceFreeGraphPackageSizeMir = sourceFreeGraphPackageSizeJson.compilerCaches.find((cache) => cache.name === "mappedFinalMir");
assert.equal(sourceFreeGraphPackageSizeMir.codegenImmediate, sourceFreeGraphPackageSizeMir.hit === true);
assert.equal(sourceFreeGraphPackageSizeMir.programReconstructed, false);
const sourceFreeGraphPackageBuildPath = join(outDir, "source-free-graph-package-build");
const sourceFreeGraphPackageBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", sourceFreeGraphPackageBuildPath, sourceFreeGraphPackageRoot]).body;
assert.equal(sourceFreeGraphPackageBuildJson.sourceFile, sourceFreeGraphPackageStorePath);
assertSourceGraph(sourceFreeGraphPackageBuildJson, sourceFreeGraphPackageStorePath, "package:source-free-graph-package@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(sourceFreeGraphPackageBuildJson, sourceFreeGraphPackageStorePath);
const sourceFreeGraphPackageBuildMir = sourceFreeGraphPackageBuildJson.compilerCaches.find((cache) => cache.name === "mappedFinalMir");
assert.equal(sourceFreeGraphPackageBuildMir.hit, true);
assert.equal(sourceFreeGraphPackageBuildMir.codegenImmediate, true);
assert.equal(sourceFreeGraphPackageBuildMir.programReconstructed, false);
const sourceFreeGraphPackageRunPath = join(outDir, "source-free-graph-package-run");
assert.equal(zero(["run", "--out", sourceFreeGraphPackageRunPath, sourceFreeGraphPackageRoot]).stdout, "package tests\n");
const sourceFreeGraphPackageTestJson = json(["test", "--json", sourceFreeGraphPackageRoot]).body;
assert.equal(sourceFreeGraphPackageTestJson.ok, true);
assertSourceGraph(sourceFreeGraphPackageTestJson, sourceFreeGraphPackageStorePath, "package:source-free-graph-package@0.1.0", "direct-program-graph", false, "missing");
assert.equal(sourceFreeGraphPackageTestJson.testBackend, "direct-program-graph");
assert.equal(sourceFreeGraphPackageTestJson.selectedTests, 3);
const sourceFreeGraphPackageMemJson = json(["mem", "--json", sourceFreeGraphPackageRoot]).body;
assertSourceGraph(sourceFreeGraphPackageMemJson, sourceFreeGraphPackageStorePath, "package:source-free-graph-package@0.1.0", "mapped-final-mir", false, "missing");
assertProgramGraphCompilerInput(sourceFreeGraphPackageMemJson, sourceFreeGraphPackageStorePath);
const sourceFreeGraphPackageStatus = json(["status", "--json", sourceFreeGraphPackageRoot]).body;
assert.equal(sourceFreeGraphPackageStatus.repositoryGraph.semanticValidity, "shape-valid");
assert.equal(sourceFreeGraphPackageStatus.repositoryGraph.projectionValidity, "missing");
const sourceFreeGraphPackageVerify = json(["verify-projection", "--json", sourceFreeGraphPackageRoot], { allowFailure: true });
assert.notEqual(sourceFreeGraphPackageVerify.code, 0);
assert.equal(sourceFreeGraphPackageVerify.body.ok, false);
assert.equal(sourceFreeGraphPackageVerify.body.diagnostics[0].code, "RGP006");
assert.equal(sourceFreeGraphPackageVerify.body.diagnostics[0].actual, "missing source projection file");
const sourceFreeGraphPackageExport = json(["export", "--json", sourceFreeGraphPackageRoot]).body;
assert.equal(sourceFreeGraphPackageExport.ok, true);
assert.deepEqual(sourceFreeGraphPackageExport.changedPaths, [
  join(sourceFreeGraphPackageRoot, "src", "helper.0"),
  join(sourceFreeGraphPackageRoot, "src", "main.0"),
]);
assert.equal(readFileSync(join(sourceFreeGraphPackageRoot, "src", "main.0"), "utf8"), readFileSync("conformance/packages/test-app/src/main.0", "utf8"));
assert.equal(readFileSync(join(sourceFreeGraphPackageRoot, "src", "helper.0"), "utf8"), readFileSync("conformance/packages/test-app/src/helper.0", "utf8"));
const sourceFreeGraphPackageVerifyAfter = json(["verify-projection", "--json", sourceFreeGraphPackageRoot]).body;
assert.equal(sourceFreeGraphPackageVerifyAfter.ok, true);
assert.equal(sourceFreeGraphPackageVerifyAfter.repositoryGraph.projectionValidity, "clean");
assert.deepEqual(json(["export", "--json", sourceFreeGraphPackageRoot]).body.changedPaths, []);
const graphTargetWebbitsRoot = join(outDir, "repo-graph-target-webbits");
const graphTargetIncompatibleRoot = join(outDir, "repo-graph-target-incompatible-app");
rmSync(graphTargetWebbitsRoot, { recursive: true, force: true });
rmSync(graphTargetIncompatibleRoot, { recursive: true, force: true });
mkdirSync(join(graphTargetIncompatibleRoot, "src"), { recursive: true });
mkdirSync(graphTargetWebbitsRoot, { recursive: true });
writeFileSync(join(graphTargetWebbitsRoot, "zero.toml"), readFileSync("conformance/packages/target-webbits/zero.toml", "utf8"));
writeZeroTomlSync(graphTargetIncompatibleRoot, {
  package: { name: "repo-graph-target-incompatible-app", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
  dependencies: { "target-webbits": { path: "../repo-graph-target-webbits", version: "0.1.0", targets: ["win32-x64.exe"] } },
});
writeFileSync(join(graphTargetIncompatibleRoot, "src", "main.0"), `pub fn main(world: World) -> Void raises {
    check world.out.write("target incompatible\\n")
}
`);
assert.equal(json(["import", "--format", "text", "--json", graphTargetIncompatibleRoot]).body.ok, true);
const graphTargetIncompatibleCheck = json(["check", "--json", "--target", "linux-musl-x64", graphTargetIncompatibleRoot], { allowFailure: true });
assert.notEqual(graphTargetIncompatibleCheck.code, 0);
assert.equal(graphTargetIncompatibleCheck.body.diagnostics[0].code, "PKG004");
assert.match(graphTargetIncompatibleCheck.body.diagnostics[0].actual, /target-webbits targets/);
const graphTargetCapabilityRoot = join(outDir, "repo-graph-target-capability-app");
rmSync(graphTargetCapabilityRoot, { recursive: true, force: true });
mkdirSync(graphTargetCapabilityRoot, { recursive: true });
writeZeroTomlSync(graphTargetCapabilityRoot, {
  package: { name: "repo-graph-target-capability-app", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
writeFileSync(join(graphTargetCapabilityRoot, "main.0"), `pub fn main(world: World) -> Void raises {
    let fs: Fs = std.fs.host()
    if std.fs.writeFile(fs, ".zero/out/repo-graph-target-capability.txt", "ok\\n") {
        check world.out.write("ok\\n")
    }
}
`);
assert.equal(json(["import", "--format", "text", "--json", graphTargetCapabilityRoot]).body.ok, true);
const graphTargetCapabilityCheck = json(["check", "--json", "--target", "linux-arm64", graphTargetCapabilityRoot], { allowFailure: true });
assert.notEqual(graphTargetCapabilityCheck.code, 0);
assert.equal(graphTargetCapabilityCheck.body.diagnostics[0].code, "TAR002");
assert.match(graphTargetCapabilityCheck.body.diagnostics[0].actual, /lacks Fs/);
const graphTargetBackendMismatchCheck = json(["check", "--json", "--backend", "zero-coff-x64", "--target", "linux-musl-x64", checkedInGraphPackageDir]);
assert.equal(graphTargetBackendMismatchCheck.body.ok, true);
assert.equal(graphTargetBackendMismatchCheck.body.targetReadiness.ok, false);
assert.equal(graphTargetBackendMismatchCheck.body.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(graphTargetBackendMismatchCheck.body.targetReadiness.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
const sourceFreeStdGraphRoot = join(outDir, "source-free-std-str");
rmSync(sourceFreeStdGraphRoot, { recursive: true, force: true });
mkdirSync(sourceFreeStdGraphRoot, { recursive: true });
writeZeroTomlSync(sourceFreeStdGraphRoot, {
  package: { name: "source-free-std-str", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
writeFileSync(join(sourceFreeStdGraphRoot, "main.0"), readFileSync("examples/std-str.0", "utf8"));
const sourceFreeStdGraphSync = json(["import", "--format", "text", "--json", sourceFreeStdGraphRoot]);
assert.equal(sourceFreeStdGraphSync.body.ok, true);
rmSync(join(sourceFreeStdGraphRoot, "main.0"), { force: true });
const sourceFreeStdGraphCheckJson = json(["check", "--json", sourceFreeStdGraphRoot]).body;
assert.equal(sourceFreeStdGraphCheckJson.ok, true);
assertSourceGraph(sourceFreeStdGraphCheckJson, join(sourceFreeStdGraphRoot, "zero.graph"), "package:source-free-std-str@0.1.0", "graph-native-check", false, "missing");
assertProgramGraphCompilerInput(sourceFreeStdGraphCheckJson, join(sourceFreeStdGraphRoot, "zero.graph"));
assertRepositoryGraphNativeCheck(sourceFreeStdGraphCheckJson, "missing");
assert.equal(sourceFreeStdGraphCheckJson.targetReadiness.ok, true);
assert.equal(sourceFreeStdGraphCheckJson.targetReadiness.diagnostics.length, 0);
assert(sourceFreeStdGraphCheckJson.graphCompiler.semanticFacts.calls.some((call) => call.qualifiedName === "std.str.reverse" && call.contract.kind === "stdlib" && call.resolution.targetKind === "stdlib" && call.returnType === "Maybe<Span<u8>>"));
assert(sourceFreeStdGraphCheckJson.graphCompiler.tables.capability > 0);
const checkedInGraphPackageSizeJson = json(["size", "--json", "--target", "linux-musl-x64", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageSizeJson.sourceFile, checkedInRepositoryGraphStorePath);
assertSourceGraph(checkedInGraphPackageSizeJson, checkedInRepositoryGraphStorePath, "package:program-graph-fixture@0.1.0", "mapped-final-mir", false);
assertProgramGraphCompilerInput(checkedInGraphPackageSizeJson, checkedInRepositoryGraphStorePath);
const checkedInGraphPackageSizeMir = checkedInGraphPackageSizeJson.compilerCaches.find((cache) => cache.name === "mappedFinalMir");
assert.equal(checkedInGraphPackageSizeMir.codegenImmediate, checkedInGraphPackageSizeMir.hit === true);
assert.equal(checkedInGraphPackageSizeMir.programReconstructed, false);
const checkedInGraphPackageBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", checkedInGraphBuildPath, checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageBuildJson.sourceFile, checkedInRepositoryGraphStorePath);
assertSourceGraph(checkedInGraphPackageBuildJson, checkedInRepositoryGraphStorePath, "package:program-graph-fixture@0.1.0", "mapped-final-mir", false);
assertProgramGraphCompilerInput(checkedInGraphPackageBuildJson, checkedInRepositoryGraphStorePath);
const checkedInGraphPackageBuildMir = checkedInGraphPackageBuildJson.compilerCaches.find((cache) => cache.name === "mappedFinalMir");
assert.equal(checkedInGraphPackageBuildMir.hit, true);
assert.equal(checkedInGraphPackageBuildMir.codegenImmediate, true);
assert.equal(checkedInGraphPackageBuildMir.programReconstructed, false);
assert.equal(checkedInGraphPackageBuildJson.artifactPath, checkedInGraphBuildPath);
mkdirSync(graphRecordRoot, { recursive: true });
writeZeroTomlSync(graphRecordRoot, {
  package: { name: "repository-graph-record", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "main.0" } },
});
writeFileSync(join(graphRecordRoot, "main.0"), [
  "type Point {",
  "    x: i32,",
  "    y: i32,",
  "}",
  "",
  "pub fn main(world: World) -> Void raises {",
  "    let point: Point = Point { x: 40, y: 2 }",
  "    if point.x == 40 {",
  "        check world.out.write(\"ok\\n\")",
  "    }",
  "}",
].join("\n") + "\n");
assert.equal(json(["import", "--format", "text", "--json", graphRecordRoot]).body.ok, true);
const graphRecordCheckJson = json(["check", "--json", graphRecordRoot]).body;
assert.equal(graphRecordCheckJson.ok, true);
assertRepositoryGraphNativeCheck(graphRecordCheckJson);
assert.equal(graphRecordCheckJson.targetReadiness.ok, true);
assert.equal(graphRecordCheckJson.targetReadiness.diagnostics.length, 0);
assert.equal(graphRecordCheckJson.graphCompiler.defaultReadiness.compilerInputReady, true);
assert.equal(graphRecordCheckJson.graphCompiler.defaultReadiness.claim, "ready-for-repository-graph-input");
const graphRecordBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", graphRecordBuildPath, graphRecordRoot]).body;
assertSourceGraph(graphRecordBuildJson, join(graphRecordRoot, "zero.graph"), "package:repository-graph-record@0.1.0", "mapped-final-mir", false, "clean");
assert.equal(graphRecordBuildJson.artifactPath, graphRecordBuildPath);
assert.equal(existsSync(graphRecordBuildPath), true);
const graphRecordObjJson = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "--out", graphRecordObjPath, graphRecordRoot]).body;
assertSourceGraph(graphRecordObjJson, join(graphRecordRoot, "zero.graph"), "package:repository-graph-record@0.1.0", "mapped-final-mir", false, "clean");
assert.equal(graphRecordObjJson.artifactPath, graphRecordObjPath);
assert.equal(existsSync(graphRecordObjPath), true);
const graphRecordDirectLlvmIrJson = json(["build", "--json", "--emit", "llvm-ir", "--target", "linux-musl-x64", "--out", graphRecordDirectLlvmIrPath, graphRecordRoot], { allowFailure: true });
assert.notEqual(graphRecordDirectLlvmIrJson.code, 0);
assert.equal(graphRecordDirectLlvmIrJson.body.diagnostics[0].message, "direct backend does not support --emit llvm-ir");
assert.equal(graphRecordDirectLlvmIrJson.body.diagnostics[0].path, join(graphRecordRoot, "zero.graph"));
assert.equal(graphRecordDirectLlvmIrJson.body.diagnostics[0].backendBlocker.backend, "direct");
assert.equal(graphRecordDirectLlvmIrJson.body.diagnostics[0].backendBlocker.stage, "buildability");
assert.equal(graphRecordDirectLlvmIrJson.body.diagnostics[0].backendBlocker.unsupportedFeature, "llvm-ir");
assert.equal(existsSync(graphRecordDirectLlvmIrPath), false);
const graphRecordLlvmIrJson = json(["build", "--json", "--backend", "llvm", "--emit", "llvm-ir", "--target", "linux-musl-x64", "--out", graphRecordLlvmIrPath, graphRecordRoot], { allowFailure: true });
assert.notEqual(graphRecordLlvmIrJson.code, 0);
assert.equal(graphRecordLlvmIrJson.body.diagnostics[0].path, join(graphRecordRoot, "zero.graph"));
assert.equal(graphRecordLlvmIrJson.body.diagnostics[0].message, "LLVM IR backend local type is unsupported");
assert.equal(graphRecordLlvmIrJson.body.diagnostics[0].backendBlocker.backend, "llvm");
assert.equal(graphRecordLlvmIrJson.body.diagnostics[0].backendBlocker.stage, "lower");
assert.equal(graphRecordLlvmIrJson.body.diagnostics[0].backendBlocker.unsupportedFeature, "unsupported local type");
assert.equal(existsSync(graphRecordLlvmIrPath), false);
const checkedInGraphPackageDefaultBuildJson = json(["build", "--json", "--target", "linux-musl-x64", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageDefaultBuildJson.artifactPath, checkedInGraphDefaultBuildPath);
assert.equal(zero(["run", "--out", checkedInGraphRunPath, checkedInGraphPackageDir]).stdout, "hello from zero\n");
const checkedInGraphPackageTestJson = json(["test", "--json", checkedInGraphPackageDir]).body;
assert.equal(checkedInGraphPackageTestJson.ok, true);
assert.equal(checkedInGraphPackageTestJson.sourceFile, checkedInRepositoryGraphStorePath);
assertSourceGraph(checkedInGraphPackageTestJson, checkedInRepositoryGraphStorePath, "package:program-graph-fixture@0.1.0", "direct-program-graph", false);
assert.equal(checkedInGraphPackageTestJson.testBackend, "direct-program-graph");
assert.equal(checkedInGraphPackageTestJson.testDiscovery.mode, "package-graph");
assert.equal(checkedInGraphPackageTestJson.selectedTests, 0);
const checkedInRepositoryGraphStoreBytes = readFileSync(checkedInRepositoryGraphStorePath);
assert.equal(checkedInRepositoryGraphStoreBytes.subarray(0, 8).toString("binary"), "ZRGBIN1\0");
const sourceLocationOnlyGraphRoot = join(outDir, "repository-graph-source-location-only");
rmSync(sourceLocationOnlyGraphRoot, { recursive: true, force: true });
mkdirSync(sourceLocationOnlyGraphRoot, { recursive: true });
writeFileSync(join(sourceLocationOnlyGraphRoot, "zero.toml"), readFileSync(join(checkedInGraphPackageDir, "zero.toml"), "utf8"));
writeFileSync(join(sourceLocationOnlyGraphRoot, "hello.0"), checkedInGraphSource);
zero(["import", "--format", "text", sourceLocationOnlyGraphRoot]);
const sourceLocationOnlyStoreText = readFileSync(join(sourceLocationOnlyGraphRoot, "zero.graph"), "utf8");
writeFileSync(join(sourceLocationOnlyGraphRoot, "zero.graph"), sourceLocationOnlyStoreText.replace("line:1 column:1", "line:99 column:77"));
const sourceLocationOnlyVerify = json(["verify-projection", "--json", sourceLocationOnlyGraphRoot]).body;
assert.equal(sourceLocationOnlyVerify.ok, true);
assert.equal(sourceLocationOnlyVerify.repositoryGraph.projectionState, "clean");
const checkedInRepositoryGraphStatus = json(["status", "--json", "--target", "linux-musl-x64", checkedInGraphPackageDir]).body;
assert.equal(checkedInRepositoryGraphStatus.repositoryGraph.storePresent, true);
assert.equal(checkedInRepositoryGraphStatus.repositoryGraph.storeValid, true);
assert.equal(checkedInRepositoryGraphStatus.repositoryGraph.projectionState, "clean");
assert.equal(checkedInRepositoryGraphStatus.repositoryGraph.compilerInput, "repository-graph");
const checkedInRepositoryGraphVerify = json(["verify-projection", "--json", "--target", "linux-musl-x64", checkedInGraphPackageDir]);
assert.equal(checkedInRepositoryGraphVerify.body.ok, true);
assert.equal(checkedInRepositoryGraphVerify.body.writes, false);
const checkedInRepositoryGraphExport = json(["export", "--json", "--target", "linux-musl-x64", checkedInGraphPackageDir]);
assert.equal(checkedInRepositoryGraphExport.body.ok, true);
assert.deepEqual(checkedInRepositoryGraphExport.body.changedPaths, []);
assert.equal(readFileSync(checkedInGraphSourcePath, "utf8"), checkedInGraphSource);
assert.equal(Buffer.compare(readFileSync(checkedInRepositoryGraphStorePath), checkedInRepositoryGraphStoreBytes), 0);
const checkedInRepositoryGraphScript = repositoryGraphVerifyScript(checkedInGraphPackageDir, { target: "linux-musl-x64" });
assert.equal(checkedInRepositoryGraphScript.code, 0);
assert.match(checkedInRepositoryGraphScript.stdout, /repository graph verify-projection ok \(1 store\)/);
const checkedInGraphDriftRoot = join(outDir, "repository-graph-drift-package");
rmSync(checkedInGraphDriftRoot, { recursive: true, force: true });
mkdirSync(checkedInGraphDriftRoot, { recursive: true });
writeFileSync(join(checkedInGraphDriftRoot, "zero.toml"), readFileSync(join(checkedInGraphPackageDir, "zero.toml"), "utf8"));
writeFileSync(join(checkedInGraphDriftRoot, "zero.graph"), checkedInRepositoryGraphStoreBytes);
writeFileSync(join(checkedInGraphDriftRoot, "hello.0"), checkedInGraphSource.replace("hello from zero", "hello from drift"));
const checkedInGraphDriftVerify = json(["verify-projection", "--json", checkedInGraphDriftRoot], { allowFailure: true });
assert.notEqual(checkedInGraphDriftVerify.code, 0);
assert.equal(checkedInGraphDriftVerify.body.ok, false);
assert.equal(checkedInGraphDriftVerify.body.mode, "verify-projection");
assert.equal(checkedInGraphDriftVerify.body.repositoryGraph.compilerInput, "repository-graph");
assert.equal(checkedInGraphDriftVerify.body.diagnostics[0].code, "RGP006");
assert.match(checkedInGraphDriftVerify.body.repairCommands.join("\n"), /zero import/);
assert.match(checkedInGraphDriftVerify.body.repairCommands.join("\n"), /zero export/);
const checkedInGraphDriftCheck = json(["check", "--json", checkedInGraphDriftRoot]);
assert.equal(checkedInGraphDriftCheck.body.ok, true);
assertSourceGraph(checkedInGraphDriftCheck.body, join(checkedInGraphDriftRoot, "zero.graph"), "package:program-graph-fixture@0.1.0", "graph-native-check", false, "clean");
assertProgramGraphCompilerInput(checkedInGraphDriftCheck.body, join(checkedInGraphDriftRoot, "zero.graph"));
assertRepositoryGraphNativeCheck(checkedInGraphDriftCheck.body, "clean");
assert.match(zero(["view", join(checkedInGraphDriftRoot, "zero.graph")]).stdout, /hello from drift/);
const checkedInGraphDriftVerifyAfterRefresh = json(["verify-projection", "--json", checkedInGraphDriftRoot]);
assert.equal(checkedInGraphDriftVerifyAfterRefresh.body.ok, true);
const graphView = zero(["view", graphDumpPath]).stdout;
assert.equal(zero(["view", graphDumpPath]).stdout, graphView);
assert.equal(zero(["diff", graphDumpPath]).stdout, graphView);
assert.match(graphView, /^pub fn main\(world: World\) -> Void raises \{\n/);
assert.match(graphView, /check world\.out\.write\("hello from zero\\n"\)/);
const graphViewJson = json(["view", "--json", graphDumpPath]).body;
assert.equal(graphViewJson.ok, true);
assert.equal(graphViewJson.canonicalSource, false);
assert.equal(graphViewJson.moduleIdentity, "module:hello");
assert.equal(graphViewJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphViewJson.view, graphView);
assert.equal(zero(["view", "--out", graphViewPath, graphDumpPath]).stdout, "");
assert.equal(readFileSync(graphViewPath, "utf8"), graphView);
const graphViewOutJson = json(["view", "--json", "--out", graphViewPath, graphDumpPath]).body;
assert.equal(graphViewOutJson.ok, true);
assert.equal(graphViewOutJson.saved.path, graphViewPath);
assert.equal(graphViewOutJson.view, null);
assert.equal(readFileSync(graphViewPath, "utf8"), graphView);
const graphViewWrongOutJson = json(["view", "--json", "--out", graphViewWrongOutPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphViewWrongOutJson.code, 0);
assert.equal(graphViewWrongOutJson.body.diagnostics[0].message, "graph view output must use .0 extension");
assert.equal(graphViewWrongOutJson.body.diagnostics[0].expected, "zero view --out <file.0> [graph-input]");
assert.equal(existsSync(graphViewWrongOutPath), false);
writeFileSync(graphViewParentFilePath, "");
const graphViewParentFileJson = json(["view", "--json", "--out", graphViewParentFileOutPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphViewParentFileJson.code, 0);
assert.equal(graphViewParentFileJson.body.diagnostics[0].path, graphViewParentFileOutPath);
assert.equal(graphViewParentFileJson.body.diagnostics[0].message, `path component is not a directory: '${graphViewParentFilePath}'`);
assert.equal(existsSync(graphViewParentFileOutPath), false);
const graphDiffOutJson = json(["diff", "--json", "--out", graphViewPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphDiffOutJson.code, 0);
assert.equal(graphDiffOutJson.body.diagnostics[0].message, "diff textconv output does not support --out");
assert.equal(graphDiffOutJson.body.diagnostics[0].expected, "zero diff [--fn <name>] [graph-input]");
assert.equal(zero(["check", graphDumpPath]).stdout, "ok\n");
const graphCheckJson = json(["check", "--json", graphDumpPath]).body;
assert.equal(graphCheckJson.ok, true);
assert.equal(graphCheckJson.canonicalSource, false);
assert.equal(graphCheckJson.moduleIdentity, "module:hello");
assert.equal(graphCheckJson.graphHash, graphDumpJson.graphHash);
assert.equal(graphCheckJson.check.ok, true);
assert.equal(graphCheckJson.check.phase, "typecheck");
assert.match(graphCheckJson.check.target, /^(darwin|linux|win32)-/);
assert.equal(graphCheckJson.check.lowering, "graph-native-check");
assert.equal(graphCheckJson.check.sourcePath, null);
assert.equal(graphCheckJson.targetReadiness.ok, true);
assert.equal(graphCheckJson.targetReadiness.languageOk, true);
assert.equal(graphCheckJson.targetReadiness.buildable, true);
assert.equal(graphCheckJson.graphCompiler.graphNativeCheckerUsed, true);
assert.equal(graphCheckJson.graphCompiler.graphHirToMirUsed, true);
assert.equal(graphCheckJson.graphCompiler.input, "program-graph-artifact");
assert.equal(graphCheckJson.safetyFacts.schemaVersion, 1);
assert.equal(graphCheckJson.safetyFacts.bounds.runtimeTraps, true);
assert.equal(graphCheckJson.safetyFacts.bounds.optimizerElision, false);
assert.equal(graphCheckJson.safetyFacts.overflow.runtimeArithmetic, "unchecked-machine-wrap");
assert.equal(graphCheckJson.safetyFacts.overflow.unchecked, true);
assert.equal(graphCheckJson.safetyFacts.initialization.maybePayloadReads, "guard-checked");
assert.equal(graphCheckJson.safetyFacts.mir.invalidMemoryContractsBlockEmission, true);
assert.deepEqual(graphCheckJson.diagnostics, []);
assert.equal(graphCheckJson.saved, null);
assert.equal(graphCheckJson.view, null);
const graphDirectImportCheckJson = json(["check", "--json", "examples/direct-package-arrays"]).body;
assert.equal(graphDirectImportCheckJson.ok, true);
assert.equal(graphDirectImportCheckJson.graph.canonicalSource, false);
assert.equal(graphDirectImportCheckJson.graph.lowering, "graph-native-check");
assert.equal(graphDirectImportCheckJson.graphCompiler.input, "repository-graph-store");
assert.equal(graphDirectImportCheckJson.targetReadiness.ok, true);
assert.equal(graphDirectImportCheckJson.safetyFacts.initialization.locals, "initializer-required");
assert.deepEqual(graphDirectImportCheckJson.diagnostics, []);
const graphDirectImportView = zero(["view", "examples/direct-package-arrays"]).stdout;
assert.match(graphDirectImportView, /fn package_sum\(\) -> i32/);
assert.match(graphDirectImportView, /export c fn main\(\) -> i32/);
const graphDirectStdCheckJson = json(["check", "--json", "examples/std-str.graph"]).body;
assert.equal(graphDirectStdCheckJson.ok, true);
assert.equal(graphDirectStdCheckJson.artifact, "examples/std-str.graph");
assert.equal(graphDirectStdCheckJson.canonicalSource, false);
assert.equal(graphDirectStdCheckJson.check.lowering, "graph-native-check");
assert.equal(graphDirectStdCheckJson.graphCompiler.input, "program-graph-artifact");
assert.equal(graphDirectStdCheckJson.targetReadiness.ok, true);
assert.deepEqual(graphDirectStdCheckJson.diagnostics, []);
const graphCheckOutJson = json(["check", "--json", "--out", graphCheckViewPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphCheckOutJson.code, 0);
assert.equal(graphCheckOutJson.body.diagnostics[0].message, "zero check does not support --out");
assert.equal(graphCheckOutJson.body.diagnostics[0].expected, "zero view --out <file.0> [graph-input]");
const graphSizeJson = json(["size", "--json", "--target", "linux-musl-x64", graphDumpPath]).body;
assert.equal(graphSizeJson.schemaVersion, 1);
assert.equal(graphSizeJson.sourceFile, graphDumpPath);
assert.equal(graphSizeJson.graph.artifact, graphDumpPath);
assert.equal(graphSizeJson.graph.canonicalSource, false);
assert.equal(graphSizeJson.graph.moduleIdentity, "module:hello");
assert.equal(graphSizeJson.graph.graphHash, graphInspectJson.programGraph.graphHash);
assert.equal(graphSizeJson.graph.graphHash, graphDumpJson.graphHash);
assert.equal(graphSizeJson.graph.lowering, "mapped-final-mir");
assert.equal(graphSizeJson.target, "linux-musl-x64");
assert.equal(graphSizeJson.generatedCBytes, 0);
assert.equal(graphSizeJson.cBridgeFallback, false);
assert.equal(graphSizeJson.sizeBreakdown.target, "linux-musl-x64");
assert.equal(graphSizeJson.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(graphSizeJson.artifactPath, null);
assertProgramGraphCompilerInput(graphSizeJson, graphDumpPath);
const graphSizeMirCache = graphSizeJson.compilerCaches.find((cache) => cache.name === "mappedFinalMir");
assert.equal(graphSizeMirCache.codegenImmediate, graphSizeMirCache.hit === true);
assert.equal(graphSizeMirCache.programReconstructed, false);
assert.equal(graphSizeJson.incrementalInvalidation.changedInputs.graphArtifact, graphDumpPath);
assert.equal(graphSizeJson.incrementalInvalidation.interfaceFingerprints.sourceKind, "program-graph");
assert.equal(graphSizeJson.incrementalInvalidation.interfaceFingerprints.graphHash, graphSizeJson.graph.graphHash);
const graphSizeHelloInterface = graphSizeJson.incrementalInvalidation.interfaceFingerprints.modules.find((item) => item.name === "hello");
assert(graphSizeHelloInterface);
assert.equal(graphSizeHelloInterface.publicSymbolCount, 1);
assert.deepEqual(graphSizeHelloInterface.publicSymbols, [{ name: "main", kind: "function" }]);
const graphSizeOutJson = json(["size", "--json", "--target", "linux-musl-x64", "--out", graphSizePath, graphDumpPath]).body;
assert.equal(graphSizeOutJson.graph.graphHash, graphSizeJson.graph.graphHash);
assert.equal(graphSizeOutJson.artifactPath, graphSizePath);
assert.equal(graphSizeOutJson.artifactBytes, statSync(graphSizePath).size);
assert.match(readFileSync(graphSizePath, "utf8"), /"kind": "zero-size-metadata"/);
const graphBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", graphBuildPath, graphDumpPath]).body;
assert.equal(graphBuildJson.schemaVersion, 1);
assert.equal(graphBuildJson.sourceFile, graphDumpPath);
assert.equal(graphBuildJson.graph.artifact, graphDumpPath);
assert.equal(graphBuildJson.graph.canonicalSource, false);
assert.equal(graphBuildJson.graph.moduleIdentity, "module:hello");
assert.equal(graphBuildJson.graph.graphHash, graphDumpJson.graphHash);
assert.equal(graphBuildJson.graph.lowering, "mapped-final-mir");
assert.equal(graphBuildJson.emit, "exe");
assert.equal(graphBuildJson.target, "linux-musl-x64");
assert.equal(graphBuildJson.generatedCBytes, 0);
assert.equal(graphBuildJson.artifactPath, graphBuildPath);
assert.equal(graphBuildJson.artifactBytes, statSync(graphBuildPath).size);
assertProgramGraphCompilerInput(graphBuildJson, graphDumpPath);
const graphBuildMirCache = graphBuildJson.compilerCaches.find((cache) => cache.name === "mappedFinalMir");
assert.equal(graphBuildMirCache.hit, true);
assert.equal(graphBuildMirCache.codegenImmediate, true);
assert.equal(graphBuildMirCache.programReconstructed, false);
const graphObjJson = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "--out", graphObjPath, graphDumpPath]).body;
assert.equal(graphObjJson.sourceFile, graphDumpPath);
assert.equal(graphObjJson.graph.graphHash, graphDumpJson.graphHash);
assert.equal(graphObjJson.graph.lowering, "mapped-final-mir");
assert.equal(graphObjJson.emit, "obj");
assert.equal(graphObjJson.target, "linux-musl-x64");
assert.equal(graphObjJson.generatedCBytes, 0);
assert.equal(graphObjJson.artifactPath, graphObjPath);
assert.equal(graphObjJson.artifactBytes, statSync(graphObjPath).size);
assertProgramGraphCompilerInput(graphObjJson, graphDumpPath);
assert.equal(zero(["run", "--out", graphRunPath, graphDumpPath]).stdout, "hello from zero\n");
assert.equal(existsSync(graphRunPath), true);
assert.equal(zero(["dump", "--out", graphArgsDumpPath, "conformance/native/pass/std-args.0"]).stdout, "");
assert.equal(zero(["run", "--out", graphArgsRunPath, graphArgsDumpPath, "--", "alpha", "beta"]).stdout, "alpha\n");
assert.equal(existsSync(graphArgsRunPath), true);
const graphRunJson = json(["run", "--json", graphDumpPath], { allowFailure: true });
assert.equal(graphRunJson.code, 1);
assert.equal(graphRunJson.body.diagnostics[0].message, "zero run does not support --json");
assert.equal(zero(["dump", "--out", graphTestDumpPath, "conformance/native/pass/test-blocks.0"]).stdout, "");
assert.equal(zero(["test", graphTestDumpPath]).stdout, "1 test(s) ok\n");
const graphTestOutJson = json(["test", "--json", "--out", graphCheckViewPath, graphTestDumpPath], { allowFailure: true });
assert.notEqual(graphTestOutJson.code, 0);
assert.equal(graphTestOutJson.body.diagnostics[0].message, "zero test does not support --out");
assert.equal(graphTestOutJson.body.diagnostics[0].expected, "zero test [--json] [--filter <name>] [--target <target>] [graph-input]");
const graphTestJson = json(["test", "--json", "--filter", "addition", graphTestDumpPath]).body;
assert.equal(graphTestJson.ok, true);
assert.equal(graphTestJson.sourceFile, graphTestDumpPath);
assert.equal(graphTestJson.graph.artifact, graphTestDumpPath);
assert.equal(graphTestJson.graph.canonicalSource, false);
assert.equal(graphTestJson.graph.lowering, "direct-program-graph");
assert.match(graphTestJson.graph.graphHash, /^graph:[0-9a-f]{16}$/);
assert.equal(graphTestJson.testBackend, "direct-program-graph");
assert.equal(graphTestJson.fixtures.snapshotKey, "zero-test-graph-native-v1");
assert.equal(graphTestJson.testDiscovery.mode, "program-graph");
assert.equal(graphTestJson.testDiscovery.filter, "addition");
assert.equal(graphTestJson.selectedTests, 1);
assert.equal(graphTestJson.results[0].status, "passed");
assert.equal(graphTestJson.results[0].location.sourceFile, "test-blocks.0");
assert.equal(graphTestJson.results[0].location.line, 5);
assert.equal(zero(["dump", "--out", graphPackageTestDumpPath, "conformance/packages/test-app"]).stdout, "");
const graphPackageTestJson = json(["test", "--json", graphPackageTestDumpPath]).body;
assert.equal(graphPackageTestJson.ok, true);
assert.equal(graphPackageTestJson.graph.artifact, graphPackageTestDumpPath);
assert.equal(graphPackageTestJson.graph.moduleIdentity, "package:test-app@0.1.0");
assert.equal(graphPackageTestJson.testDiscovery.mode, "package-graph");
assert.equal(graphPackageTestJson.testDiscovery.sourceFileCount, 2);
assert.equal(graphPackageTestJson.selectedTests, 3);
assert(graphPackageTestJson.fixtures.sourceFiles.includes("src/helper.0"));
assert(graphPackageTestJson.fixtures.sourceFiles.includes("src/main.0"));
const graphPackageHelperTest = graphPackageTestJson.results.find((item) => item.name === "package helper double");
assert(graphPackageHelperTest);
assert.equal(graphPackageHelperTest.location.sourceFile, "src/helper.0");
assert.equal(graphPackageHelperTest.location.line, 5);
const graphPackageExpectedFail = graphPackageTestJson.results.find((item) => item.status === "expected-fail");
assert(graphPackageExpectedFail);
assert.equal(graphPackageExpectedFail.location.sourceFile, "src/helper.0");
assert.equal(graphPackageExpectedFail.location.line, 9);
assert.equal(graphPackageExpectedFail.failure.sourceFile, "src/helper.0");
assert.equal(graphPackageExpectedFail.failure.line, 10);
// a test reaching a callee the runner cannot execute names that callee in the failure,
// not just the test label; std.* outside std.testing gets the not-implemented framing
const unknownCalleePackageDir = join(outDir, "test-unknown-callee-package");
mkdirSync(join(unknownCalleePackageDir, "src"), { recursive: true });
writeFileSync(
  join(unknownCalleePackageDir, "src", "main.0"),
  'pub fn main(world: World) -> Void raises {\n    check world.out.write("unknown callee\\n")\n}\n\nfn spanLensMatch(a: String, b: String) -> Bool {\n    return std.mem.len(std.mem.span(a)) == std.mem.len(std.mem.span(b))\n}\n\ntest "std call coverage" {\n    expect spanLensMatch("abc", "xyz")\n}\n',
);
writeZeroTomlSync(unknownCalleePackageDir, {
  package: { name: "test-unknown-callee", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
});
assert.match(zero(["import", unknownCalleePackageDir]).stdout, /repository graph import ok/);
const unknownCalleeTestJson = json(["test", "--json", unknownCalleePackageDir], { allowFailure: true });
assert.equal(unknownCalleeTestJson.code, 1);
assert.equal(unknownCalleeTestJson.body.failedTests, 1);
const unknownCalleeResult = unknownCalleeTestJson.body.results.find((item) => item.name === "std call coverage");
assert(unknownCalleeResult);
assert.equal(unknownCalleeResult.status, "failed");
assert.match(unknownCalleeResult.failure.message, /does not implement 'std\.mem\.(span|len)' yet/);
assert.equal(unknownCalleeResult.failure.sourceFile, "src/main.0");
mkdirSync(join(graphManifestPackageDir, "src"), { recursive: true });
mkdirSync(join(graphManifestPackageDir, "artifacts"), { recursive: true });
writeFileSync(join(graphManifestPackageDir, "src", "main.0"), readFileSync("conformance/packages/test-app/src/main.0", "utf8"));
writeFileSync(join(graphManifestPackageDir, "src", "helper.0"), readFileSync("conformance/packages/test-app/src/helper.0", "utf8"));
writeZeroTomlSync(graphManifestPackageDir, {
  package: { name: "graph-manifest-package", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0", graph: "artifacts/test-app.program-graph" } },
});
assert.equal(zero(["dump", "--out", graphManifestArtifactPath, "conformance/packages/test-app"]).stdout, "");
assert.equal(zero(["import", graphManifestPackageDir]).stdout, `repository graph import ok\nwrote: ${graphManifestStorePath}\n`);
assert.equal(zero(["validate", graphManifestPackageDir]).stdout, "program graph ok\n");
const graphManifestCheckJson = json(["check", "--json", graphManifestPackageDir]).body;
assert.equal(graphManifestCheckJson.ok, true);
assert.equal(graphManifestCheckJson.graph.artifact, graphManifestStorePath);
assert.equal(graphManifestCheckJson.graph.moduleIdentity, "package:graph-manifest-package@0.1.0");
assert.equal(graphManifestCheckJson.graph.lowering, "graph-native-check");
const graphManifestSizeJson = json(["size", "--json", "--target", "linux-musl-x64", graphManifestPackageDir]).body;
assertSourceGraph(graphManifestSizeJson, graphManifestStorePath, "package:graph-manifest-package@0.1.0", "mapped-final-mir", false, "clean");
assertProgramGraphCompilerInput(graphManifestSizeJson, graphManifestStorePath);
assert.equal(graphManifestSizeJson.sourceFile, graphManifestStorePath);
const graphManifestBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", graphManifestBuildPath, graphManifestPackageDir]).body;
assertSourceGraph(graphManifestBuildJson, graphManifestStorePath, "package:graph-manifest-package@0.1.0", "mapped-final-mir", false, "clean");
assertProgramGraphCompilerInput(graphManifestBuildJson, graphManifestStorePath);
assert.equal(graphManifestBuildJson.sourceFile, graphManifestStorePath);
assert.equal(zero(["run", "--out", graphManifestRunPath, graphManifestPackageDir]).stdout, "package tests\n");
const graphManifestTestJson = json(["test", "--json", graphManifestPackageDir]).body;
assert.equal(graphManifestTestJson.ok, true);
assertSourceGraph(graphManifestTestJson, graphManifestStorePath, "package:graph-manifest-package@0.1.0", "direct-program-graph", false, "clean");
assert.equal(graphManifestTestJson.testDiscovery.mode, "package-graph");
const graphManifestRoundtripJson = json(["roundtrip", "--json", graphManifestPackageDir]).body;
assert.equal(graphManifestRoundtripJson.ok, true);
assert.equal(graphManifestRoundtripJson.artifact, graphManifestStorePath);
assert.equal(graphManifestRoundtripJson.semanticStable, true);
assert.equal(graphManifestRoundtripJson.lowering, "direct-program-graph");
assert.equal(graphManifestRoundtripJson.moduleIdentity, "package:graph-manifest-package@0.1.0");
assert.equal(graphManifestRoundtripJson.roundtripModuleIdentity, "package:graph-manifest-package@0.1.0");
assert.equal(graphManifestRoundtripJson.view, null);
const directGraphManifestCheckJson = json(["check", "--json", graphManifestPackageDir]).body;
assert.equal(directGraphManifestCheckJson.ok, true);
assertSourceGraph(directGraphManifestCheckJson, graphManifestStorePath, "package:graph-manifest-package@0.1.0", "graph-native-check", false, "clean");
assert.equal(directGraphManifestCheckJson.sourceFile, graphManifestStorePath);
assert.equal(directGraphManifestCheckJson.package.name, "graph-manifest-package");
assert.equal(directGraphManifestCheckJson.package.manifestPath, join(graphManifestPackageDir, "zero.toml"));
assert.notEqual(directGraphManifestCheckJson.package.manifestHash, "0000000000000000");
assertProgramGraphCompilerInput(directGraphManifestCheckJson, graphManifestStorePath);
assert.equal(directGraphManifestCheckJson.incrementalInvalidation.changedInputs.manifestPath, join(graphManifestPackageDir, "zero.toml"));
const directGraphManifestSizeJson = json(["size", "--json", "--target", "linux-musl-x64", graphManifestPackageDir]).body;
assertSourceGraph(directGraphManifestSizeJson, graphManifestStorePath, "package:graph-manifest-package@0.1.0", "mapped-final-mir", false, "clean");
assertProgramGraphCompilerInput(directGraphManifestSizeJson, graphManifestStorePath);
assert.equal(directGraphManifestSizeJson.sourceFile, graphManifestStorePath);
const directGraphManifestMemJson = json(["mem", "--json", graphManifestPackageDir]).body;
assertSourceGraph(directGraphManifestMemJson, graphManifestStorePath, "package:graph-manifest-package@0.1.0", "mapped-final-mir", false, "clean");
assertProgramGraphCompilerInput(directGraphManifestMemJson, graphManifestStorePath);
assert.equal(directGraphManifestMemJson.sourceFile, graphManifestStorePath);
const directGraphManifestBuildJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", directGraphManifestBuildPath, graphManifestPackageDir]).body;
assertSourceGraph(directGraphManifestBuildJson, graphManifestStorePath, "package:graph-manifest-package@0.1.0", "mapped-final-mir", false, "clean");
assertProgramGraphCompilerInput(directGraphManifestBuildJson, graphManifestStorePath);
assert.equal(directGraphManifestBuildJson.sourceFile, graphManifestStorePath);
assert.equal(zero(["run", "--out", directGraphManifestRunPath, graphManifestPackageDir]).stdout, "package tests\n");
const directGraphManifestTestJson = json(["test", "--json", graphManifestPackageDir]).body;
assert.equal(directGraphManifestTestJson.ok, true);
assertSourceGraph(directGraphManifestTestJson, graphManifestStorePath, "package:graph-manifest-package@0.1.0", "direct-program-graph", false, "clean");
assert.equal(directGraphManifestTestJson.testDiscovery.mode, "package-graph");
const sourcePackageCheckJson = json(["check", "--json", "conformance/packages/test-app"]).body;
assert.equal(sourcePackageCheckJson.ok, true);
assertSourceGraph(sourcePackageCheckJson, "conformance/packages/test-app/zero.graph", "package:test-app@0.1.0", "graph-native-check", false);
mkdirSync(join(directGraphTargetGatePackageDir, "src"), { recursive: true });
mkdirSync(join(directGraphTargetGatePackageDir, "artifacts"), { recursive: true });
mkdirSync(join(directGraphTargetGateDepDir, "src"), { recursive: true });
writeFileSync(join(directGraphTargetGatePackageDir, "src", "main.0"), "pub fn main(world: World) -> Void raises {\n    check world.out.write(\"target gate\\n\")\n}\n");
writeFileSync(join(directGraphTargetGateDepDir, "src", "main.0"), "pub fn main(world: World) -> Void raises {\n    check world.out.write(\"webbits\\n\")\n}\n");
writeZeroTomlSync(directGraphTargetGateDepDir, {
  package: { name: "target-webbits", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
});
writeZeroTomlSync(directGraphTargetGatePackageDir, {
  package: { name: "direct-graph-target-gate", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0", graph: "artifacts/app.program-graph" } },
  dependencies: {
    "target-webbits": {
      path: "../target-webbits",
      version: "0.1.0",
      targets: ["win32-x64.exe"],
    },
  },
});
assert.equal(zero(["import", directGraphTargetGateDepDir]).stdout, `repository graph import ok\nwrote: ${join(directGraphTargetGateDepDir, "zero.graph")}\n`);
assert.equal(zero(["import", directGraphTargetGatePackageDir]).stdout, `repository graph import ok\nwrote: ${join(directGraphTargetGatePackageDir, "zero.graph")}\n`);
assert.equal(zero(["dump", "--out", directGraphTargetGateArtifactPath, directGraphTargetGatePackageDir]).stdout, "");
const directGraphTargetGateJson = json(["check", "--json", "--target", "linux-musl-x64", directGraphTargetGatePackageDir], { allowFailure: true });
assert.equal(directGraphTargetGateJson.code, 1);
assert.equal(directGraphTargetGateJson.body.diagnostics[0].code, "PKG004");
assert.equal(directGraphTargetGateJson.body.diagnostics[0].message, "package dependency is not compatible with target");
mkdirSync(join(directGraphHostLeakPackageDir, "src"), { recursive: true });
mkdirSync(join(directGraphHostLeakPackageDir, "artifacts"), { recursive: true });
writeFileSync(join(directGraphHostLeakPackageDir, "src", "main.0"), "pub fn main(world: World) -> Void raises {\n    check world.out.write(\"host leak\\n\")\n}\n");
writeZeroTomlSync(directGraphHostLeakPackageDir, {
  package: { name: "direct-graph-host-leak", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0", graph: "artifacts/app.program-graph" } },
  c: {
    libs: {
      hostonly: {
        headers: ["hostonly.h"],
        include: ["/usr/include"],
        lib: ["/usr/lib"],
        link: ["hostonly"],
        pkg_config: "hostonly",
      },
    },
  },
});
assert.equal(zero(["import", directGraphHostLeakPackageDir]).stdout, `repository graph import ok\nwrote: ${join(directGraphHostLeakPackageDir, "zero.graph")}\n`);
assert.equal(zero(["dump", "--out", directGraphHostLeakArtifactPath, directGraphHostLeakPackageDir]).stdout, "");
const directGraphHostLeakJson = json(["build", "--json", "--target", "linux-musl-x64", "--out", directGraphHostLeakBuildPath, directGraphHostLeakPackageDir], { allowFailure: true });
assert.equal(directGraphHostLeakJson.code, 1);
assert.equal(directGraphHostLeakJson.body.diagnostics[0].code, "CIMP003");
assert.equal(directGraphHostLeakJson.body.diagnostics[0].message, "foreign target C dependency would use host discovery");
const graphPackageSourceCheckJson = json(["check", "--json", "conformance/packages/test-app"]).body;
assert.equal(graphPackageSourceCheckJson.ok, true);
assert.equal(graphPackageSourceCheckJson.graph.moduleIdentity, "package:test-app@0.1.0");
assert.equal(graphPackageSourceCheckJson.graph.lowering, "graph-native-check");
writeFileSync(graphSizeNoisePatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insertEdge from="${graphHelloLiteralNode.id}" to="${graphHelloLiteralNode.typeId}" edge="sizeProbeType" target="type" order="0"`,
  "",
].join("\n"));
const graphSizeNoisePatchJson = json(["patch", "--json", "--out", graphSizeNoisePath, graphDumpPath, graphSizeNoisePatchPath]).body;
assert.equal(graphSizeNoisePatchJson.ok, true);
assert.equal(zero(["check", graphSizeNoisePath]).stdout, "ok\n");
const graphSizeNoiseJson = json(["size", "--json", "--target", "linux-musl-x64", graphSizeNoisePath]).body;
const graphSizeNoiseInterface = graphSizeNoiseJson.incrementalInvalidation.interfaceFingerprints.modules.find((item) => item.name === "hello");
assert(graphSizeNoiseInterface);
assert.deepEqual(graphSizeNoiseInterface.publicSymbols, [{ name: "main", kind: "function" }]);
writeFileSync(graphPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="hello patched\\n"`,
  "",
].join("\n"));
assertPatchOkOutput(zero(["patch", "--out", graphPatchedPath, graphDumpPath, graphPatchPath]).stdout, graphPatchedPath);
const graphPatchedStdout = zero(["patch", graphDumpPath, graphPatchPath]).stdout;
assert.match(graphPatchedStdout, /^zero-graph v1\n/);
assert.match(graphPatchedStdout, /value:"hello patched\\n"/);
assert.equal(readFileSync(graphPatchedPath, "utf8"), graphPatchedStdout);
const graphPatchJson = json(["patch", "--json", "--out", graphPatchedPath, graphDumpPath, graphPatchPath]).body;
assert.equal(graphPatchJson.ok, true);
assert.equal(graphPatchJson.canonicalSource, false);
assert.equal(graphPatchJson.patch, graphPatchPath);
assert.equal(graphPatchJson.originalGraphHash, graphDumpJson.graphHash);
assert.match(graphPatchJson.patchedGraphHash, /^graph:[0-9a-f]{16}$/);
assert.notEqual(graphPatchJson.patchedGraphHash, graphDumpJson.graphHash);
assert.equal(graphPatchJson.operationCount, 1);
assert.equal(graphPatchJson.operations[0].ok, true);
assert.equal(graphPatchJson.operations[0].node, graphHelloLiteralNode.id);
assert.equal(graphPatchJson.operations[0].field, "value");
assert.equal(graphPatchJson.operations[0].actual, "hello from zero\n");
assert.equal(graphPatchJson.operations[0].value, "hello patched\n");
assert.equal(graphPatchJson.diagnostic, null);
assert.equal(graphPatchJson.saved.path, graphPatchedPath);
mkdirSync(graphPatchDirectoryPath, { recursive: true });
const graphPatchDirectoryJson = json(["patch", "--json", "--out", graphPatchDirectoryOutPath, graphDumpPath, graphPatchDirectoryPath], { allowFailure: true });
assert.notEqual(graphPatchDirectoryJson.code, 0);
assert.equal(graphPatchDirectoryJson.body.diagnostics[0].path, graphPatchDirectoryPath);
assert.match(graphPatchDirectoryJson.body.diagnostics[0].message, /^failed to read '.+hello\.program-graph\.patch-directory': /);
assert.equal(existsSync(graphPatchDirectoryOutPath), false);
assert.equal(zero(["validate", graphPatchedPath]).stdout, "program graph ok\n");
assert.match(zero(["view", graphPatchedPath]).stdout, /check world\.out\.write\("hello patched\\n"\)/);
assert.equal(zero(["check", graphPatchedPath]).stdout, "ok\n");
const graphSourcePatchPath = join(outDir, "hello.graph-sidecar.0");
writeProjectionFile(graphSourcePatchPath, graphView);
const graphSourcePatchSidecar = importProjectionSidecar(graphSourcePatchPath);
const graphSourcePatchDumpJson = json(["dump", "--json", graphSourcePatchPath]).body;
assert.equal(graphSourcePatchDumpJson.canonicalSource, false);
const graphSourceLiteralNode = graphSourcePatchDumpJson.nodes.find((node) => node.kind === "Literal" && node.type === "String" && node.value === "hello from zero\n");
assert(graphSourceLiteralNode);
const graphSourcePatchJson = json([
  "patch",
  "--json",
  "--out",
  graphSourcePatchSidecar,
  graphSourcePatchSidecar,
  "--expect-graph-hash",
  graphSourcePatchDumpJson.graphHash,
  "--op",
  `set node="${graphSourceLiteralNode.id}" field="value" expect="hello from zero\\n" value="hello sidecar graph\\n"`,
]).body;
assert.equal(graphSourcePatchJson.ok, true);
assert.equal(graphSourcePatchJson.canonicalSource, false);
assert.equal(graphSourcePatchJson.saved.path, graphSourcePatchSidecar);
zero(["view", "--out", graphSourcePatchPath, graphSourcePatchSidecar]);
assert.match(readFileSync(graphSourcePatchPath, "utf8"), /hello sidecar graph\\n/);
assertProjectionCheckOk(graphSourcePatchPath);
const graphSourceProjectionPatchRejected = json(["patch", "--json", graphSourcePatchPath, "--op", "addMain"], { allowFailure: true });
assert.notEqual(graphSourceProjectionPatchRejected.code, 0);
assert.equal(graphSourceProjectionPatchRejected.body.diagnostics[0].code, "BLD002");
assert.equal(graphSourceProjectionPatchRejected.body.diagnostics[0].message, "graph patch requires graph input");
assert.equal(graphSourceProjectionPatchRejected.body.diagnostics[0].expected, "package zero.graph store, .graph store, or ProgramGraph artifact");
const graphSourcePackageDir = join(outDir, "graph-source-package");
const graphSourcePackageMain = join(graphSourcePackageDir, "src", "main.0");
const graphSourcePackageHelper = join(graphSourcePackageDir, "src", "helper.0");
rmSync(graphSourcePackageDir, { recursive: true, force: true });
mkdirSync(join(graphSourcePackageDir, "src"), { recursive: true });
writeZeroTomlSync(graphSourcePackageDir, {
  package: { name: "graph-source-package", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.0" } },
});
writeProjectionFile(
  graphSourcePackageHelper,
  "pub fn value() -> i32 {\n" +
    "    return 41\n" +
    "}\n",
);
writeProjectionFile(
  graphSourcePackageMain,
  "use helper\n\n" +
    "pub fn main() -> i32 {\n" +
    "    return value() + 1\n" +
    "}\n",
);
assert.match(zero(["import", graphSourcePackageDir]).stdout, /^repository graph import ok\n/);
const graphSourcePackageDumpJson = json(["dump", "--json", graphSourcePackageMain]).body;
const graphSourcePackageLiteralNode = graphSourcePackageDumpJson.nodes.find(
  (node) => node.kind === "Literal" && node.value === "1",
);
assert(graphSourcePackageLiteralNode);
const graphSourcePackagePatchJson = json([
  "patch",
  "--json",
  graphSourcePackageDir,
  "--expect-graph-hash",
  graphSourcePackageDumpJson.graphHash,
  "--op",
  `set node="${graphSourcePackageLiteralNode.id}" field="value" expect="1" value="2"`,
]).body;
assert.equal(graphSourcePackagePatchJson.ok, true);
assert.equal(graphSourcePackagePatchJson.saved.path, join(graphSourcePackageDir, "zero.graph"));
assert.match(zero(["export", graphSourcePackageDir]).stdout, /^repository graph export ok\n/);
const graphSourcePackageMainText = readFileSync(graphSourcePackageMain, "utf8");
assert.match(graphSourcePackageMainText, /^use helper\n\n/);
assert.match(graphSourcePackageMainText, /return value\(\) \+ 2/);
assert.doesNotMatch(graphSourcePackageMainText, /pub fn value/);
assert.equal(readFileSync(graphSourcePackageHelper, "utf8"), "pub fn value() -> i32 {\n    return 41\n}\n");
assert.equal(zero(["check", graphSourcePackageDir]).stdout, "ok\n");
const graphUserDerefSourcePath = join(outDir, "deref-member.0");
const graphUserDerefViewPath = join(outDir, "deref-member.view.0");
const graphPrefixDerefSourcePath = join(outDir, "prefix-deref-member.0");
const graphNestedCastSourcePath = join(outDir, "nested-cast.0");
const graphNestedCastViewPath = join(outDir, "nested-cast.view.0");
const graphPublicExportSourcePath = join(outDir, "public-export-c.0");
const graphPublicExportViewPath = join(outDir, "public-export-c.view.0");
const graphPublicInterfaceSourcePath = join(outDir, "public-interface-method.0");
const graphPublicInterfaceViewPath = join(outDir, "public-interface-method.view.0");
const graphPublicSumsSourcePath = join(outDir, "public-sums.0");
const graphExternFieldsSourcePath = join(outDir, "extern-fields.0");
const graphCommentsSourcePath = join(outDir, "comments.0");
const graphDerefMemberSource = [
  "type Point {",
  "    x: i32,",
  "}",
  "",
  "type Wrapper {",
  "    x: Point,",
  "}",
  "",
  "fn deref(value: Wrapper) -> Point {",
  "    return value.x",
  "}",
  "",
  "pub fn main() -> i32 {",
  "    let wrapped: Wrapper = Wrapper { x: Point { x: 7 } }",
  "    return deref(wrapped).x",
  "}",
  "",
].join("\n");
writeProjectionFile(graphUserDerefSourcePath, graphDerefMemberSource);
assertProjectionCheckOk(graphUserDerefSourcePath);
const graphUserDerefView = zero(["view", graphUserDerefSourcePath]).stdout;
assert.match(graphUserDerefView, /return deref\(wrapped\)\.x/);
assert.doesNotMatch(graphUserDerefView, /return \*wrapped\.x/);
assert.equal(zero(["view", "--out", graphUserDerefViewPath, graphUserDerefSourcePath]).stdout, "");
assertProjectionCheckOk(graphUserDerefViewPath);
writeProjectionFile(graphPrefixDerefSourcePath, graphDerefMemberSource.replace("return deref(wrapped).x", "return (*wrapped).x"));
assertProjectionCheckOk(graphPrefixDerefSourcePath);
const graphPrefixDerefView = zero(["view", graphPrefixDerefSourcePath]).stdout;
assert.match(graphPrefixDerefView, /return \(\*wrapped\)\.x/);
assert.equal(zero(["check", projectionSidecarGraphPath(graphPrefixDerefSourcePath)]).stdout, "ok\n");
writeProjectionFile(graphNestedCastSourcePath, [
  "pub fn main() -> u32 {",
  "    let x: i32 = 1",
  "    return (x as u16) as u32",
  "}",
  "",
].join("\n"));
assertProjectionCheckOk(graphNestedCastSourcePath);
const graphNestedCastView = zero(["view", graphNestedCastSourcePath]).stdout;
assert.match(graphNestedCastView, /return \(x as u16\) as u32/);
assert.equal(zero(["view", "--out", graphNestedCastViewPath, graphNestedCastSourcePath]).stdout, "");
assertProjectionCheckOk(graphNestedCastViewPath);
assert.equal(json(["roundtrip", "--json", graphNestedCastSourcePath]).body.view, null);
writeProjectionFile(graphPublicExportSourcePath, [
  "pub export c fn main() -> i32 {",
  "    return 1",
  "}",
  "",
].join("\n"));
assertProjectionCheckOk(graphPublicExportSourcePath);
assert.equal(zero(["view", "--out", graphPublicExportViewPath, graphPublicExportSourcePath]).stdout, "");
assertProjectionCheckOk(graphPublicExportViewPath);
assert.equal(json(["roundtrip", "--json", graphPublicExportSourcePath]).body.view, null);
writeProjectionFile(graphPublicInterfaceSourcePath, [
  "interface Reader {",
  "    pub fn read() -> i32",
  "}",
  "",
  "pub fn main() -> i32 {",
  "    return 1",
  "}",
  "",
].join("\n"));
assertProjectionCheckOk(graphPublicInterfaceSourcePath);
assert.equal(zero(["view", "--out", graphPublicInterfaceViewPath, graphPublicInterfaceSourcePath]).stdout, "");
assertProjectionCheckOk(graphPublicInterfaceViewPath);
assert.equal(json(["roundtrip", "--json", graphPublicInterfaceSourcePath]).body.view, null);
writeProjectionFile(graphPublicSumsSourcePath, [
  "pub enum Mode: u8 {",
  "    ready,",
  "}",
  "",
  "pub choice Result {",
  "    ok: i32,",
  "    err: String,",
  "}",
  "",
  "pub fn main() -> i32 {",
  "    return 1",
  "}",
  "",
].join("\n"));
const graphPublicSumsSidecar = importProjectionSidecar(graphPublicSumsSourcePath);
const graphPublicSumsDumpJson = json(["dump", "--json", graphPublicSumsSourcePath]).body;
const graphPublicEnumNode = graphPublicSumsDumpJson.nodes.find((node) => node.kind === "Enum" && node.name === "Mode");
const graphPublicChoiceNode = graphPublicSumsDumpJson.nodes.find((node) => node.kind === "Choice" && node.name === "Result");
const graphPublicLiteralNode = graphPublicSumsDumpJson.nodes.find((node) => node.kind === "Literal" && node.value === "1");
assert.equal(graphPublicEnumNode?.public, true);
assert.equal(graphPublicChoiceNode?.public, true);
assert(graphPublicLiteralNode);
const graphPublicSumsPatchJson = json([
  "patch",
  "--json",
  "--out",
  graphPublicSumsSidecar,
  graphPublicSumsSidecar,
  "--expect-graph-hash",
  graphPublicSumsDumpJson.graphHash,
  "--op",
  `set node="${graphPublicLiteralNode.id}" field="value" expect="1" value="2"`,
]).body;
assert.equal(graphPublicSumsPatchJson.ok, true);
assert.equal(graphPublicSumsPatchJson.canonicalSource, false);
assert.equal(graphPublicSumsPatchJson.saved.path, graphPublicSumsSidecar);
zero(["view", "--out", graphPublicSumsSourcePath, graphPublicSumsSidecar]);
const graphPublicSumsText = readFileSync(graphPublicSumsSourcePath, "utf8");
assert.match(graphPublicSumsText, /^pub enum Mode/m);
assert.match(graphPublicSumsText, /^pub choice Result/m);
assert.match(graphPublicSumsText, /return 2/);
assertProjectionCheckOk(graphPublicSumsSourcePath);
writeProjectionFile(graphExternFieldsSourcePath, [
  "extern type CPoint {",
  "    x: i32,",
  "    y: i32,",
  "}",
  "",
  "pub fn main() -> i32 {",
  "    return 1",
  "}",
  "",
].join("\n"));
const graphExternFieldsSidecar = importProjectionSidecar(graphExternFieldsSourcePath);
const graphExternFieldsDumpJson = json(["dump", "--json", graphExternFieldsSourcePath]).body;
const graphExternFieldsLiteralNode = graphExternFieldsDumpJson.nodes.find((node) => node.kind === "Literal" && node.value === "1");
assert(graphExternFieldsLiteralNode);
const graphExternFieldsPatchJson = json([
  "patch",
  "--json",
  "--out",
  graphExternFieldsSidecar,
  graphExternFieldsSidecar,
  "--expect-graph-hash",
  graphExternFieldsDumpJson.graphHash,
  "--op",
  `set node="${graphExternFieldsLiteralNode.id}" field="value" expect="1" value="2"`,
]).body;
assert.equal(graphExternFieldsPatchJson.ok, true);
assert.equal(graphExternFieldsPatchJson.canonicalSource, false);
assert.equal(graphExternFieldsPatchJson.saved.path, graphExternFieldsSidecar);
zero(["view", "--out", graphExternFieldsSourcePath, graphExternFieldsSidecar]);
const graphExternFieldsText = readFileSync(graphExternFieldsSourcePath, "utf8");
assert.match(graphExternFieldsText, /^extern type CPoint \{/m);
assert.match(graphExternFieldsText, /^\s+x: i32,/m);
assert.match(graphExternFieldsText, /^\s+y: i32,/m);
assert.match(graphExternFieldsText, /return 2/);
assertProjectionCheckOk(graphExternFieldsSourcePath);
writeProjectionFile(graphCommentsSourcePath, [
  "// module comment",
  "",
  "pub fn main() -> i32 {",
  "    // keep this comment",
  "    return 1",
  "}",
  "",
].join("\n"));
const graphCommentsOriginal = readFileSync(graphCommentsSourcePath, "utf8");
const graphCommentsPatchJson = json([
  "patch",
  "--json",
  graphCommentsSourcePath,
  "--op",
  "addMain",
], { allowFailure: true });
assert.notEqual(graphCommentsPatchJson.code, 0);
assert.equal(graphCommentsPatchJson.body.diagnostics[0].code, "BLD002");
assert.equal(graphCommentsPatchJson.body.diagnostics[0].message, "graph patch requires graph input");
assert.equal(readFileSync(graphCommentsSourcePath, "utf8"), graphCommentsOriginal);
const graphInlinePatchJson = json([
  "patch",
  "--json",
  "--out",
  graphInlinePatchedPath,
  graphDumpPath,
  "--expect-graph-hash",
  graphDumpJson.graphHash,
  "--op",
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="hello inline\\n"`,
]).body;
assert.equal(graphInlinePatchJson.ok, true);
assert.equal(graphInlinePatchJson.patch, "<inline>");
assert.equal(graphInlinePatchJson.originalGraphHash, graphDumpJson.graphHash);
assert.equal(graphInlinePatchJson.operationCount, 1);
assert.equal(graphInlinePatchJson.operations[0].node, graphHelloLiteralNode.id);
assert.equal(graphInlinePatchJson.operations[0].value, "hello inline\n");
assert.equal(graphInlinePatchJson.saved.path, graphInlinePatchedPath);
assert.match(zero(["view", graphInlinePatchedPath]).stdout, /check world\.out\.write\("hello inline\\n"\)/);
writeFileSync(graphPatchInsertPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_check" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="1" path="examples/hello.0" line="3" column="3"`,
  `insert node="#patch_call" kind="MethodCall" parent="#patch_check" edge="expr" name="write" type="Void" path="examples/hello.0" line="3" column="25"`,
  `insert node="#patch_write" kind="FieldAccess" parent="#patch_call" edge="left" order="0" name="write" path="examples/hello.0" line="3" column="18"`,
  `insert node="#patch_out" kind="FieldAccess" parent="#patch_write" edge="left" order="0" name="out" path="examples/hello.0" line="3" column="14"`,
  `insert node="#patch_world" kind="Identifier" parent="#patch_out" edge="left" order="0" name="world" path="examples/hello.0" line="3" column="9"`,
  `insert node="#patch_lit" kind="Literal" parent="#patch_call" edge="arg" order="0" type="String" value="second line\\n" path="examples/hello.0" line="3" column="25"`,
  "",
].join("\n"));
const graphInsertPatchJson = json(["patch", "--json", "--out", graphInsertedPath, graphDumpPath, graphPatchInsertPath]).body;
assert.equal(graphInsertPatchJson.ok, true);
assert.equal(graphInsertPatchJson.operationCount, 6);
assert.equal(graphInsertPatchJson.operations[0].op, "insert");
assert.equal(graphInsertPatchJson.operations[0].parent, graphMainBodyNode.id);
assert.equal(graphInsertPatchJson.operations[0].edge, "statement");
assert.equal(graphInsertPatchJson.operations[0].order, 1);
assert.equal(graphInsertPatchJson.operations[1].order, 0);
assert.equal(graphInsertPatchJson.operations[5].kind, "Literal");
assert.equal(graphInsertPatchJson.operations[5].type, "String");
assert.equal(graphInsertPatchJson.operations[5].value, "second line\n");
assert.equal(graphInsertPatchJson.saved.path, graphInsertedPath);
const graphInsertedView = zero(["view", graphInsertedPath]).stdout;
assert.match(graphInsertedView, /check world\.out\.write\("hello from zero\\n"\)\n    check world\.out\.write\("second line\\n"\)/);
assert.equal(zero(["check", graphInsertedPath]).stdout, "ok\n");
assert.equal(zero(["roundtrip", graphInsertedPath]).stdout, "program graph roundtrip ok\n");
const graphInsertedText = readFileSync(graphInsertedPath, "utf8");
const graphInsertedCheckLine = graphInsertedText.split("\n").find((line) => line.startsWith("node #patch_check ")) ?? "";
const graphInsertedCheckHash = graphQuotedField(graphInsertedCheckLine, "nodeHash") || graphNodeHash(graphInsertedCheckLine);
assert.match(graphInsertedCheckHash, /^nodehash:[0-9a-f]{16}$/);
writeFileSync(graphPatchDeletePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphInsertPatchJson.patchedGraphHash}"`,
  `delete node="#patch_check" expect="${graphInsertedCheckHash}"`,
  "",
].join("\n"));
const graphDeletePatchJson = json(["patch", "--json", "--out", graphDeletedPath, graphInsertedPath, graphPatchDeletePath]).body;
assert.equal(graphDeletePatchJson.ok, true);
assert.equal(graphDeletePatchJson.operations[0].op, "delete");
assert.equal(graphDeletePatchJson.operations[0].actual, graphInsertedCheckHash);
assert.equal(readFileSync(graphDeletedPath, "utf8"), graphDump);
const graphOriginalCheckNode = graphDumpJson.nodes.find((node) => node.kind === "Check");
assert(graphOriginalCheckNode);
writeFileSync(graphPatchDeleteThenInsertPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphInsertPatchJson.patchedGraphHash}"`,
  `delete node="${graphOriginalCheckNode.id}" expect="${graphOriginalCheckNode.nodeHash}"`,
  `insert node="#patch_replacement_check" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="0" path="examples/hello.0" line="2" column="3"`,
  `insert node="#patch_replacement_call" kind="MethodCall" parent="#patch_replacement_check" edge="expr" order="0" name="write" type="Void" path="examples/hello.0" line="2" column="25"`,
  `insert node="#patch_replacement_write" kind="FieldAccess" parent="#patch_replacement_call" edge="left" order="0" name="write" path="examples/hello.0" line="2" column="18"`,
  `insert node="#patch_replacement_out" kind="FieldAccess" parent="#patch_replacement_write" edge="left" order="0" name="out" path="examples/hello.0" line="2" column="14"`,
  `insert node="#patch_replacement_world" kind="Identifier" parent="#patch_replacement_out" edge="left" order="0" name="world" path="examples/hello.0" line="2" column="9"`,
  `insert node="#patch_replacement_lit" kind="Literal" parent="#patch_replacement_call" edge="arg" order="0" type="String" value="replacement line\\n" path="examples/hello.0" line="2" column="25"`,
  "",
].join("\n"));
const graphDeleteThenInsertPatchJson = json(["patch", "--json", "--out", graphDeleteThenInsertedPath, graphInsertedPath, graphPatchDeleteThenInsertPath]).body;
assert.equal(graphDeleteThenInsertPatchJson.ok, true);
assert.equal(graphDeleteThenInsertPatchJson.operations[0].op, "delete");
assert.equal(graphDeleteThenInsertPatchJson.operations[1].op, "insert");
const graphDeleteThenInsertedView = zero(["view", graphDeleteThenInsertedPath]).stdout;
assert.match(graphDeleteThenInsertedView, /check world\.out\.write\("replacement line\\n"\)\n    check world\.out\.write\("second line\\n"\)/);
assert.equal(zero(["check", graphDeleteThenInsertedPath]).stdout, "ok\n");
assert.equal(zero(["roundtrip", graphDeleteThenInsertedPath]).stdout, "program graph roundtrip ok\n");
writeFileSync(graphPatchDeleteNodeFactPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_node_fact" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="1"`,
  `insertEdge from="#patch_node_fact" to="${graphModuleNode.id}" edge="backlink" target="node" order="0"`,
  `delete node="#patch_node_fact"`,
  "",
].join("\n"));
const graphDeleteNodeFactPatchJson = json(["patch", "--json", "--out", graphDeletedNodeFactPath, graphDumpPath, graphPatchDeleteNodeFactPath]).body;
assert.equal(graphDeleteNodeFactPatchJson.ok, true);
assert.equal(graphDeleteNodeFactPatchJson.operationCount, 3);
assert.equal(graphDeleteNodeFactPatchJson.operations[1].op, "insertEdge");
assert.equal(graphDeleteNodeFactPatchJson.operations[1].target, "node");
assert.equal(graphDeleteNodeFactPatchJson.operations[2].op, "delete");
assert.equal(readFileSync(graphDeletedNodeFactPath, "utf8"), graphDump);
writeFileSync(graphPatchDeleteExternalRootRefPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_external_root_ref" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="1"`,
  `insertEdge from="${graphModuleNode.id}" to="#patch_external_root_ref" edge="backlink" target="node" order="0"`,
  `delete node="#patch_external_root_ref"`,
  "",
].join("\n"));
const graphDeleteExternalRootRefPatchJson = json(["patch", "--json", graphDumpPath, graphPatchDeleteExternalRootRefPath], { allowFailure: true });
assert.notEqual(graphDeleteExternalRootRefPatchJson.code, 0);
assert.equal(graphDeleteExternalRootRefPatchJson.body.ok, false);
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[0].ok, true);
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[1].ok, true);
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[2].ok, false);
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[2].code, "GPH005");
assert.equal(graphDeleteExternalRootRefPatchJson.body.operations[2].actual, "#patch_external_root_ref");
assert.equal(graphDeleteExternalRootRefPatchJson.body.saved, null);
writeFileSync(graphPatchDeleteExtraOwnerPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insert node="#patch_extra_owner" kind="Check" parent="${graphMainBodyNode.id}" edge="statement" order="1"`,
  `insertEdge from="${graphModuleNode.id}" to="#patch_extra_owner" edge="function" target="node" order="1"`,
  `delete node="#patch_extra_owner"`,
  "",
].join("\n"));
const graphDeleteExtraOwnerPatchJson = json(["patch", "--json", graphDumpPath, graphPatchDeleteExtraOwnerPath], { allowFailure: true });
assert.notEqual(graphDeleteExtraOwnerPatchJson.code, 0);
assert.equal(graphDeleteExtraOwnerPatchJson.body.ok, false);
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[0].ok, true);
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[1].ok, true);
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[2].ok, false);
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[2].code, "GPH005");
assert.equal(graphDeleteExtraOwnerPatchJson.body.operations[2].actual, "#patch_extra_owner");
assert.equal(graphDeleteExtraOwnerPatchJson.body.saved, null);
const graphLiteralNode = graphDumpJson.nodes.find((node) => node.kind === "Literal" && node.type === "String");
assert(graphLiteralNode);
writeFileSync(graphPatchReplacePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `replace node="${graphLiteralNode.id}" expect="${graphLiteralNode.nodeHash}" kind="Literal" type="String" value="hello replaced structurally\\n" public="true"`,
  "",
].join("\n"));
const graphReplacePatchJson = json(["patch", "--json", "--out", graphReplacedPath, graphDumpPath, graphPatchReplacePath]).body;
assert.equal(graphReplacePatchJson.ok, true);
assert.equal(graphReplacePatchJson.operations[0].op, "replace");
assert.equal(graphReplacePatchJson.operations[0].actual, graphLiteralNode.nodeHash);
assert.equal(graphReplacePatchJson.operations[0].value, "hello replaced structurally\n");
assert.equal(graphReplacePatchJson.operations[0].public, true);
assert.match(zero(["view", graphReplacedPath]).stdout, /check world\.out\.write\("hello replaced structurally\\n"\)/);
writeFileSync(graphPatchStaleReplacePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `replace node="${graphLiteralNode.id}" expect="${graphLiteralNode.nodeHash}" kind="Literal" type="String" value="first structural value\\n"`,
  `replace node="${graphLiteralNode.id}" expect="${graphLiteralNode.nodeHash}" kind="Literal" type="String" value="second structural value\\n"`,
  "",
].join("\n"));
const graphStaleReplacePatchJson = json(["patch", "--json", graphDumpPath, graphPatchStaleReplacePath], { allowFailure: true });
assert.notEqual(graphStaleReplacePatchJson.code, 0);
assert.equal(graphStaleReplacePatchJson.body.ok, false);
assert.equal(graphStaleReplacePatchJson.body.operations[0].ok, true);
assert.equal(graphStaleReplacePatchJson.body.operations[1].ok, false);
assert.equal(graphStaleReplacePatchJson.body.operations[1].code, "GPH005");
assert.notEqual(graphStaleReplacePatchJson.body.operations[1].actual, graphLiteralNode.nodeHash);
writeFileSync(graphPatchInsertEdgePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insertEdge from="${graphLiteralNode.id}" to="${graphLiteralNode.typeId}" edge="resolvedType" target="type" order="0"`,
  "",
].join("\n"));
const graphInsertEdgePatchJson = json(["patch", "--json", "--out", graphInsertedEdgePath, graphDumpPath, graphPatchInsertEdgePath]).body;
assert.equal(graphInsertEdgePatchJson.ok, true);
assert.equal(graphInsertEdgePatchJson.operations[0].op, "insertEdge");
assert.equal(graphInsertEdgePatchJson.operations[0].from, graphLiteralNode.id);
assert.equal(graphInsertEdgePatchJson.operations[0].to, graphLiteralNode.typeId);
assert.equal(graphInsertEdgePatchJson.operations[0].target, "type");
assert.equal(zero(["validate", graphInsertedEdgePath]).stdout, "program graph ok\n");
writeFileSync(graphPatchEmptyTypeEdgePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `insertEdge from="${graphModuleNode.id}" to="" edge="resolvedType" target="type" order="0"`,
  "",
].join("\n"));
const graphEmptyTypeEdgePatchJson = json(["patch", "--json", graphDumpPath, graphPatchEmptyTypeEdgePath], { allowFailure: true });
assert.notEqual(graphEmptyTypeEdgePatchJson.code, 0);
assert.equal(graphEmptyTypeEdgePatchJson.body.ok, false);
assert.equal(graphEmptyTypeEdgePatchJson.body.diagnostic.code, "GPH004");
assert.equal(graphEmptyTypeEdgePatchJson.body.operations[0].ok, false);
assert.equal(graphEmptyTypeEdgePatchJson.body.operations[0].code, "GPH004");
assert.equal(graphEmptyTypeEdgePatchJson.body.operations[0].to, "");
assert.equal(graphEmptyTypeEdgePatchJson.body.saved, null);
const graphEmptyTypeEdgePatchText = zeroWithStderr(["patch", graphDumpPath, graphPatchEmptyTypeEdgePath]);
assert.notEqual(graphEmptyTypeEdgePatchText.code, 0);
assert.match(graphEmptyTypeEdgePatchText.stderr, /zero query --fn <name> --handles to list stmt and param patch handles/);
writeFileSync(graphPatchRenamePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `rename node="${graphMainFunctionNode.id}" expect="main" value="start"`,
  "",
].join("\n"));
const graphRenamePatchJson = json(["patch", "--json", "--out", graphRenamedPath, graphDumpPath, graphPatchRenamePath]).body;
assert.equal(graphRenamePatchJson.ok, true);
assert.equal(graphRenamePatchJson.operations[0].op, "rename");
assert.equal(graphRenamePatchJson.operations[0].actual, "main");
assert.match(zero(["view", graphRenamedPath]).stdout, /pub fn start\(world: World\) -> Void raises/);
const graphRenamedCheck = json(["check", "--json", graphRenamedPath]);
assert.equal(graphRenamedCheck.body.ok, true);
assert.equal(graphRenamedCheck.body.check.lowering, "graph-native-check");
assert.equal(graphRenamedCheck.body.targetReadiness.ok, false);
assert.equal(graphRenamedCheck.body.targetReadiness.diagnostics[0].message, "typed graph MIR requires at least one exported C ABI entry function");
writeFileSync(graphPatchInvalidRenamePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `rename node="${graphMainFunctionNode.id}" expect="main" value="bad\\nname"`,
  "",
].join("\n"));
const graphInvalidRenamePatchJson = json(["patch", "--json", graphDumpPath, graphPatchInvalidRenamePath], { allowFailure: true });
assert.notEqual(graphInvalidRenamePatchJson.code, 0);
assert.equal(graphInvalidRenamePatchJson.body.ok, false);
assert.equal(graphInvalidRenamePatchJson.body.diagnostic.code, "GPH003");
assert.equal(graphInvalidRenamePatchJson.body.operations[0].actual, "bad\nname");
assert.equal(graphInvalidRenamePatchJson.body.saved, null);
writeFileSync(graphUncheckedPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphMainFunctionNode.id}" field="type" expect="Void" value="u32"`,
  "",
].join("\n"));
assertPatchOkOutput(zero(["patch", "--out", graphUncheckedPath, graphDumpPath, graphUncheckedPatchPath]).stdout, graphUncheckedPath);
const graphUncheckedInline = json(["check", "--json", graphUncheckedPath], { allowFailure: true });
assert.equal(graphUncheckedInline.code, 0);
assert.equal(graphUncheckedInline.body.ok, true);
assert.equal(graphUncheckedInline.body.canonicalSource, false);
assert.equal(graphUncheckedInline.body.check.ok, true);
assert.equal(graphUncheckedInline.body.check.phase, "typecheck");
assert.equal(graphUncheckedInline.body.check.lowering, "graph-native-check");
assert.equal(graphUncheckedInline.body.check.sourcePath, null);
assert.equal(graphUncheckedInline.body.targetReadiness.ok, false);
assert.equal(graphUncheckedInline.body.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(graphUncheckedInline.body.targetReadiness.diagnostics[0].actual, "no exported function");
assert.equal(graphUncheckedInline.body.saved, null);
assert.deepEqual(graphUncheckedInline.body.diagnostics, []);
assert.equal(graphUncheckedInline.body.view, null);
assert.doesNotMatch(JSON.stringify(graphUncheckedInline.body), /<generated-graph-view>|zero-graph-check/);
assert.equal(zero(["dump", "--out", graphBorrowDumpPath, "conformance/native/pass/borrow-field-independent-assignment.0"]).stdout, "");
const graphBorrowDumpJson = json(["dump", "--json", "conformance/native/pass/borrow-field-independent-assignment.0"]).body;
assert.match(graphBorrowDumpJson.graphHash, /^graph:[0-9a-f]{16}$/);
const graphBorrowRightFieldAccess = graphBorrowDumpJson.nodes.filter((node) => node.kind === "FieldAccess" && node.name === "right").at(-1);
assert(graphBorrowRightFieldAccess);
writeFileSync(graphBorrowConflictPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphBorrowDumpJson.graphHash}"`,
  `set node="${graphBorrowRightFieldAccess.id}" field="name" expect="right" value="left"`,
  "",
].join("\n"));
assertPatchOkOutput(zero(["patch", "--out", graphBorrowConflictPath, graphBorrowDumpPath, graphBorrowConflictPatchPath]).stdout, graphBorrowConflictPath);
const graphBorrowConflictInline = json(["check", "--json", graphBorrowConflictPath], { allowFailure: true });
assert.equal(graphBorrowConflictInline.code, 0);
assert.equal(graphBorrowConflictInline.body.ok, true);
assert.equal(graphBorrowConflictInline.body.check.ok, true);
assert.equal(graphBorrowConflictInline.body.targetReadiness.ok, false);
assert.equal(graphBorrowConflictInline.body.targetReadiness.diagnostics[0].code, "BLD004");
assert.deepEqual(graphBorrowConflictInline.body.diagnostics, []);
assert.equal(graphBorrowConflictInline.body.view, null);
assert.doesNotMatch(JSON.stringify(graphBorrowConflictInline.body), /<generated-graph-view>|zero-graph-check/);
writeFileSync(graphPatchEmptyPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value=""`,
  "",
].join("\n"));
const graphPatchEmpty = json(["patch", "--json", graphDumpPath, graphPatchEmptyPath]).body;
assert.equal(graphPatchEmpty.ok, true);
assert.equal(graphPatchEmpty.operations[0].value, "");
writeFileSync(graphPatchControlPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="\\u0001"`,
  "",
].join("\n"));
const graphPatchControl = json(["patch", "--json", graphDumpPath, graphPatchControlPath]).body;
assert.equal(graphPatchControl.ok, true);
assert.equal(graphPatchControl.operations[0].value, "\u0001");
writeFileSync(graphPatchHighBytePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="\\u0080"`,
  "",
].join("\n"));
const graphPatchHighByteStdout = execFileSync(zeroBin, ["patch", "--json", graphDumpPath, graphPatchHighBytePath], { stdio: ["ignore", "pipe", "pipe"] });
assert.equal(graphPatchHighByteStdout.includes(Buffer.from([0x80])), false);
const graphPatchHighByteText = graphPatchHighByteStdout.toString("utf8");
assert.match(graphPatchHighByteText, /"value": "\\u0080"/);
const graphPatchHighByte = JSON.parse(graphPatchHighByteText);
assert.equal(graphPatchHighByte.ok, true);
assert.equal(graphPatchHighByte.operations[0].value.charCodeAt(0), 0x80);
writeFileSync(graphPatchBadEscapePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="\\q"`,
  "",
].join("\n"));
const graphPatchBadEscape = json(["patch", "--json", graphDumpPath, graphPatchBadEscapePath], { allowFailure: true });
assert.notEqual(graphPatchBadEscape.code, 0);
assert.equal(graphPatchBadEscape.body.ok, false);
assert.equal(graphPatchBadEscape.body.diagnostic.code, "GPH001");
writeFileSync(graphPatchNullEscapePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="\\u0000x"`,
  "",
].join("\n"));
const graphPatchNullEscape = json(["patch", "--json", graphDumpPath, graphPatchNullEscapePath], { allowFailure: true });
assert.notEqual(graphPatchNullEscape.code, 0);
assert.equal(graphPatchNullEscape.body.ok, false);
assert.equal(graphPatchNullEscape.body.diagnostic.code, "GPH001");
writeFileSync(graphPatchRawNullPath, Buffer.concat([
  Buffer.from([
    "zero-program-graph-patch v1",
    `expect graphHash "${graphDumpJson.graphHash}"`,
    `set node="${graphHelloLiteralNode.id}" field="value" expect="hello from zero\\n" value="raw nul prefix"`,
    "",
  ].join("\n")),
  Buffer.from([0]),
  Buffer.from(`set node="${graphHelloLiteralNode.id}" field="value" value="ignored"\n`),
]));
const graphPatchRawNull = json(["patch", "--json", graphDumpPath, graphPatchRawNullPath], { allowFailure: true });
assert.notEqual(graphPatchRawNull.code, 0);
assert.equal(graphPatchRawNull.body.ok, false);
assert.equal(graphPatchRawNull.body.diagnostic.code, "GPH001");
assert.equal(graphPatchRawNull.body.diagnostic.message, "program graph patch contains NUL byte");
writeFileSync(graphPatchInvalidNamePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphMainFunctionNode.id}" field="name" expect="main" value="main\\npub fn injected Void"`,
  "",
].join("\n"));
const graphPatchInvalidName = json(["patch", "--json", graphDumpPath, graphPatchInvalidNamePath], { allowFailure: true });
assert.notEqual(graphPatchInvalidName.code, 0);
assert.equal(graphPatchInvalidName.body.ok, false);
assert.equal(graphPatchInvalidName.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidName.body.operations[0].field, "name");
assert.equal(graphPatchInvalidName.body.operations[0].value, "main\npub fn injected Void");
writeFileSync(graphPatchInvalidTypePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphMainFunctionNode.id}" field="type" expect="Void" value="Void\\npub fn injected Void"`,
  "",
].join("\n"));
const graphPatchInvalidType = json(["patch", "--json", graphDumpPath, graphPatchInvalidTypePath], { allowFailure: true });
assert.notEqual(graphPatchInvalidType.code, 0);
assert.equal(graphPatchInvalidType.body.ok, false);
assert.equal(graphPatchInvalidType.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidType.body.operations[0].field, "type");
assert.equal(graphPatchInvalidType.body.operations[0].value, "Void\npub fn injected Void");
assert.equal(zero(["dump", "--out", graphPayloadDumpPath, "conformance/check/pass/payload-match.0"]).stdout, "");
const graphPayloadDumpJson = json(["dump", "--json", "conformance/check/pass/payload-match.0"]).body;
const graphMatchNode = graphPayloadDumpJson.nodes.find((node) => node.kind === "Match");
const graphMatchArmNode = graphPayloadDumpJson.nodes.find((node) => node.kind === "MatchArm" && node.name === "ok");
assert(graphMatchNode);
assert(graphMatchArmNode);
writeFileSync(graphPatchInvalidMatchReplacePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPayloadDumpJson.graphHash}"`,
  `replace node="${graphMatchArmNode.id}" expect="${graphMatchArmNode.nodeHash}" kind="MatchArm" value="payload\\nname"`,
  "",
].join("\n"));
const graphPatchInvalidMatchReplace = json(["patch", "--json", graphPayloadDumpPath, graphPatchInvalidMatchReplacePath], { allowFailure: true });
assert.notEqual(graphPatchInvalidMatchReplace.code, 0);
assert.equal(graphPatchInvalidMatchReplace.body.ok, false);
assert.equal(graphPatchInvalidMatchReplace.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidMatchReplace.body.diagnostic.message, "patch match payload value must be a Zero identifier path or operator");
assert.equal(graphPatchInvalidMatchReplace.body.operations[0].op, "replace");
assert.equal(graphPatchInvalidMatchReplace.body.operations[0].value, "payload\nname");
assert.equal(graphPatchInvalidMatchReplace.body.saved, null);
writeFileSync(graphPatchInvalidMatchInsertPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPayloadDumpJson.graphHash}"`,
  `insert node="#patch_bad_match_arm" kind="MatchArm" parent="${graphMatchNode.id}" edge="arm" order="2" name="ok" value="payload\\nname"`,
  "",
].join("\n"));
const graphPatchInvalidMatchInsert = json(["patch", "--json", graphPayloadDumpPath, graphPatchInvalidMatchInsertPath], { allowFailure: true });
assert.notEqual(graphPatchInvalidMatchInsert.code, 0);
assert.equal(graphPatchInvalidMatchInsert.body.ok, false);
assert.equal(graphPatchInvalidMatchInsert.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidMatchInsert.body.diagnostic.message, "patch match payload value must be a Zero identifier path or operator");
assert.equal(graphPatchInvalidMatchInsert.body.operations[0].op, "insert");
assert.equal(graphPatchInvalidMatchInsert.body.operations[0].value, "payload\nname");
assert.equal(graphPatchInvalidMatchInsert.body.saved, null);
const graphWorldParamNode = graphDumpJson.nodes.find((node) => node.kind === "Param" && node.name === "world");
assert(graphWorldParamNode);
writeFileSync(graphPatchReservedParamPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphWorldParamNode.id}" field="name" expect="world" value="pub"`,
  "",
].join("\n"));
assertPatchOkOutput(zero(["patch", "--out", graphReservedParamPath, graphDumpPath, graphPatchReservedParamPath]).stdout, graphReservedParamPath);
const graphReservedParam = json(["check", "--json", graphReservedParamPath], { allowFailure: true });
assert.notEqual(graphReservedParam.code, 0);
assert.equal(graphReservedParam.body.ok, false);
assert.equal(graphReservedParam.body.check.phase, "lower");
assert.equal(graphReservedParam.body.check.lowering, "graph-native-check");
assert.equal(graphReservedParam.body.diagnostics[0].message, "program graph parameter name is not valid Zero identifier syntax");
writeFileSync(graphPatchInternalFunctionPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphMainFunctionNode.id}" field="name" expect="main" value="__zero_bad"`,
  "",
].join("\n"));
assertPatchOkOutput(zero(["patch", "--out", graphInternalFunctionPath, graphDumpPath, graphPatchInternalFunctionPath]).stdout, graphInternalFunctionPath);
const graphInternalFunction = json(["check", "--json", graphInternalFunctionPath], { allowFailure: true });
assert.notEqual(graphInternalFunction.code, 0);
assert.equal(graphInternalFunction.body.ok, false);
assert.equal(graphInternalFunction.body.check.phase, "lower");
assert.equal(graphInternalFunction.body.check.lowering, "graph-native-check");
assert.equal(graphInternalFunction.body.diagnostics[0].message, "program graph declaration uses a reserved compiler-internal symbol name");
assert.equal(zero(["dump", "--out", graphPackageDumpPath, "examples/direct-package-call-order"]).stdout, "");
assert.equal(zero(["view", "--out", graphPackageViewPath, "examples/direct-package-call-order"]).stdout, "");
const graphPackageView = readFileSync(graphPackageViewPath, "utf8");
assert.match(graphPackageView, /fn right_score/);
assert.match(graphPackageView, /fn left_score/);
assert.doesNotMatch(graphPackageView, /^use (helpers|types)$/m);
assertProjectionCheckOk(graphPackageViewPath);
const graphPackageDumpJson = json(["dump", "--json", "examples/direct-package-call-order"]).body;
const graphStatusFunctionNode = graphPackageDumpJson.nodes.find((node) => node.kind === "Function" && node.name === "right_score");
assert(graphStatusFunctionNode);
writeFileSync(graphPackagePathMismatchPatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPackageDumpJson.graphHash}"`,
  `replace node="${graphStatusFunctionNode.id}" expect="${graphStatusFunctionNode.nodeHash}" path="examples/direct-package-call-order/src/main.0" public="true"`,
  "",
].join("\n"));
assertPatchOkOutput(zero(["patch", "--out", graphPackagePathMismatchPath, graphPackageDumpPath, graphPackagePathMismatchPatchPath]).stdout, graphPackagePathMismatchPath);
assert.equal(zero(["check", graphPackagePathMismatchPath]).stdout, "ok\n");
const graphPackagePathMismatchSize = json(["size", "--json", "--target", "linux-musl-x64", graphPackagePathMismatchPath]).body;
const graphHelpersInterface = graphPackagePathMismatchSize.incrementalInvalidation.interfaceFingerprints.modules.find((item) => item.name === "right");
assert(graphHelpersInterface);
assert(graphHelpersInterface.publicSymbols.some((item) => item.name === "right_score" && item.kind === "function"));
const graphMainInterface = graphPackagePathMismatchSize.incrementalInvalidation.interfaceFingerprints.modules.find((item) => item.name === "main");
assert(graphMainInterface);
assert(!graphMainInterface.publicSymbols.some((item) => item.name === "right_score"));
const graphImportNode = graphPackageDumpJson.nodes.find((node) => node.kind === "Import");
assert(graphImportNode);
writeFileSync(graphPatchInvalidImportAliasPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPackageDumpJson.graphHash}"`,
  `set node="${graphImportNode.id}" field="value" value="alias\\npub fn injected Void\\n"`,
  "",
].join("\n"));
const graphPatchInvalidImportAlias = json(["patch", "--json", graphPackageDumpPath, graphPatchInvalidImportAliasPath], { allowFailure: true });
assert.notEqual(graphPatchInvalidImportAlias.code, 0);
assert.equal(graphPatchInvalidImportAlias.body.ok, false);
assert.equal(graphPatchInvalidImportAlias.body.diagnostic.code, "GPH003");
assert.equal(graphPatchInvalidImportAlias.body.diagnostic.message, "patch import alias value must be a Zero identifier");
assert.equal(graphPatchInvalidImportAlias.body.operations[0].field, "value");
assert.equal(graphPatchInvalidImportAlias.body.operations[0].value, "alias\npub fn injected Void\n");
assert.equal(graphPatchInvalidImportAlias.body.saved, null);
writeFileSync(graphPatchInvalidImportNamePath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPackageDumpJson.graphHash}"`,
  `set node="${graphImportNode.id}" field="name" value="+"`,
  "",
].join("\n"));
assertPatchOkOutput(zero(["patch", "--out", graphInvalidImportNamePath, graphPackageDumpPath, graphPatchInvalidImportNamePath]).stdout, graphInvalidImportNamePath);
const graphInvalidImportName = json(["check", "--json", graphInvalidImportNamePath], { allowFailure: true });
assert.notEqual(graphInvalidImportName.code, 0);
assert.equal(graphInvalidImportName.body.ok, false);
assert.equal(graphInvalidImportName.body.check.phase, "lower");
assert.equal(graphInvalidImportName.body.check.lowering, "graph-native-check");
assert.equal(graphInvalidImportName.body.diagnostics[0].message, "program graph import module is not valid Zero import syntax");
writeFileSync(graphPatchMissingImportPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphPackageDumpJson.graphHash}"`,
  `set node="${graphImportNode.id}" field="name" value="missing"`,
  "",
].join("\n"));
assertPatchOkOutput(zero(["patch", "--out", graphMissingImportPath, graphPackageDumpPath, graphPatchMissingImportPath]).stdout, graphMissingImportPath);
const graphMissingImport = json(["check", "--json", graphMissingImportPath], { allowFailure: true });
assert.notEqual(graphMissingImport.code, 0);
assert.equal(graphMissingImport.body.ok, false);
assert.equal(graphMissingImport.body.check.phase, "lower");
assert.equal(graphMissingImport.body.check.lowering, "graph-native-check");
assert.equal(graphMissingImport.body.diagnostics[0].message, "program graph import target module is missing");
writeFileSync(graphPatchBadHashPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "graph:0000000000000000"`,
  `set node="${graphHelloLiteralNode.id}" field="value" value="unreachable\\n"`,
  "",
].join("\n"));
const graphPatchBadHash = json(["patch", "--json", graphDumpPath, graphPatchBadHashPath], { allowFailure: true });
assert.notEqual(graphPatchBadHash.code, 0);
assert.equal(graphPatchBadHash.body.ok, false);
assert.equal(graphPatchBadHash.body.diagnostic.code, "GPH002");
assert.equal(graphPatchBadHash.body.patchedGraphHash, null);
assert.equal(graphPatchBadHash.body.saved, null);
writeFileSync(graphPatchMismatchPath, [
  "zero-program-graph-patch v1",
  `expect graphHash "${graphDumpJson.graphHash}"`,
  `set node="${graphHelloLiteralNode.id}" field="value" expect="not this" value="unreachable\\n"`,
  "",
].join("\n"));
const graphPatchMismatch = json(["patch", "--json", graphDumpPath, graphPatchMismatchPath], { allowFailure: true });
assert.notEqual(graphPatchMismatch.code, 0);
assert.equal(graphPatchMismatch.body.ok, false);
assert.equal(graphPatchMismatch.body.diagnostic.code, "GPH005");
assert.equal(graphPatchMismatch.body.operations[0].ok, false);
assert.equal(graphPatchMismatch.body.operations[0].actual, "hello from zero\n");
assert.equal(graphPatchMismatch.body.saved, null);
assert.equal(zero(["roundtrip", "examples/hello.0"]).stdout, "program graph roundtrip ok\n");
const graphRoundtripJson = json(["roundtrip", "--json", "examples/hello.0"]).body;
assert.equal(graphRoundtripJson.ok, true);
assert.equal(graphRoundtripJson.canonicalSource, false);
assert.equal(graphRoundtripJson.semanticStable, true);
assert.equal(graphRoundtripJson.lowering, "direct-program-graph");
assert.equal(graphRoundtripJson.moduleIdentity, "module:hello");
assert.equal(graphRoundtripJson.roundtripModuleIdentity, "module:hello");
assert.match(graphRoundtripJson.originalGraphHash, /^graph:[0-9a-f]{16}$/);
assert.equal(graphRoundtripJson.roundtripGraphHash, graphRoundtripJson.originalGraphHash);
assert.deepEqual(graphRoundtripJson.counts.original, { nodes: 13, edges: 12 });
assert.deepEqual(graphRoundtripJson.counts.roundtrip, { nodes: 13, edges: 12 });
assert.deepEqual(graphRoundtripJson.semanticCounts.original, { nodes: 13, edges: 12 });
assert.deepEqual(graphRoundtripJson.semanticCounts.roundtrip, { nodes: 13, edges: 12 });
assert.equal(graphRoundtripJson.comparison.ok, true);
assert.equal(graphRoundtripJson.saved, null);
assert.equal(graphRoundtripJson.view, null);
assert.equal(zero(["roundtrip", graphDumpPath]).stdout, "program graph roundtrip ok\n");
const graphArtifactRoundtripJson = json(["roundtrip", "--json", graphDumpPath]).body;
assert.equal(graphArtifactRoundtripJson.ok, true);
assert.equal(graphArtifactRoundtripJson.artifact, graphDumpPath);
assert.equal(graphArtifactRoundtripJson.canonicalSource, false);
assert.equal(graphArtifactRoundtripJson.semanticStable, true);
assert.equal(graphArtifactRoundtripJson.lowering, "direct-program-graph");
assert.equal(graphArtifactRoundtripJson.moduleIdentity, "module:hello");
assert.equal(graphArtifactRoundtripJson.roundtripModuleIdentity, "module:hello");
assert.equal(graphArtifactRoundtripJson.originalGraphHash, graphDumpJson.graphHash);
assert.equal(graphArtifactRoundtripJson.roundtripGraphHash, graphDumpJson.graphHash);
assert.deepEqual(graphArtifactRoundtripJson.semanticCounts.original, graphArtifactRoundtripJson.semanticCounts.roundtrip);
assert.equal(graphArtifactRoundtripJson.comparison.ok, true);
assert.equal(graphArtifactRoundtripJson.view, null);
assert.equal(zero(["roundtrip", "--out", graphArtifactRoundtripPath, graphDumpPath]).stdout, "program graph roundtrip ok\n");
assert.equal(readFileSync(graphArtifactRoundtripPath, "utf8"), graphDump);
const graphArtifactRoundtripOutJson = json(["roundtrip", "--json", "--out", graphArtifactRoundtripPath, graphDumpPath]).body;
assert.equal(graphArtifactRoundtripOutJson.ok, true);
assert.equal(graphArtifactRoundtripOutJson.saved.path, graphArtifactRoundtripPath);
assert.equal(graphArtifactRoundtripOutJson.saved.kind, "program-graph");
assert.equal(graphArtifactRoundtripOutJson.view, null);
assert.equal(readFileSync(graphArtifactRoundtripPath, "utf8"), graphDump);
const graphArtifactRoundtripSourceTextJson = json(["roundtrip", "--json", "--out", graphArtifactRoundtripSourceTextPath, graphDumpPath], { allowFailure: true });
assert.notEqual(graphArtifactRoundtripSourceTextJson.code, 0);
assert.equal(graphArtifactRoundtripSourceTextJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphArtifactRoundtripSourceTextJson.body.diagnostics[0].expected, "zero roundtrip --out <program-graph-artifact> [graph-input]");
assert.equal(existsSync(graphArtifactRoundtripSourceTextPath), false);
assert.equal(zero(["roundtrip", "--out", graphSourceRoundtripPath, "examples/hello.0"]).stdout, "program graph roundtrip ok\n");
assert.match(readFileSync(graphSourceRoundtripPath, "utf8"), /^zero-graph v1\n/);
assert.equal(zero(["validate", graphSourceRoundtripPath]).stdout, "program graph ok\n");
const graphRoundtripOutJson = json(["roundtrip", "--json", "--out", graphSourceRoundtripPath, "examples/hello.0"]).body;
assert.equal(graphRoundtripOutJson.ok, true);
assert.equal(graphRoundtripOutJson.saved.path, graphSourceRoundtripPath);
assert.equal(graphRoundtripOutJson.saved.kind, "program-graph");
assert.equal(graphRoundtripOutJson.view, null);
assert.match(readFileSync(graphSourceRoundtripPath, "utf8"), /^zero-graph v1\n/);
assert.equal(zero(["validate", graphSourceRoundtripPath]).stdout, "program graph ok\n");
const graphRoundtripSourceTextOutJson = json(["roundtrip", "--json", "--out", graphSourceRoundtripSourceTextPath, "examples/hello.0"], { allowFailure: true });
assert.notEqual(graphRoundtripSourceTextOutJson.code, 0);
assert.equal(graphRoundtripSourceTextOutJson.body.diagnostics[0].message, "program graph output must not use source text extension");
assert.equal(graphRoundtripSourceTextOutJson.body.diagnostics[0].expected, "zero roundtrip --out <program-graph-artifact> [graph-input]");
assert.equal(existsSync(graphSourceRoundtripSourceTextPath), false);
const graphPackageRoundtripJson = json(["roundtrip", "--json", "examples/systems-package"]).body;
assert.equal(graphPackageRoundtripJson.ok, true);
assert.equal(graphPackageRoundtripJson.semanticStable, true);
assert.equal(graphPackageRoundtripJson.moduleIdentity, "package:systems-package@0.1.0");
assert.equal(graphPackageRoundtripJson.roundtripModuleIdentity, graphPackageRoundtripJson.moduleIdentity);
assert.equal(graphPackageRoundtripJson.comparison.ok, true);
assert.deepEqual(graphPackageRoundtripJson.semanticCounts.original, graphPackageRoundtripJson.semanticCounts.roundtrip);
const graphTestBlockRoundtripJson = json(["roundtrip", "--json", "conformance/native/pass/test-blocks.0"]).body;
assert.equal(graphTestBlockRoundtripJson.ok, true);
assert.equal(graphTestBlockRoundtripJson.semanticStable, true);
assert.equal(graphTestBlockRoundtripJson.roundtripModuleIdentity, graphTestBlockRoundtripJson.moduleIdentity);
assert.equal(graphTestBlockRoundtripJson.comparison.ok, true);
let sparseOrderGraph = graphDump.replace(`edge ${graphModuleNode.id} function ${graphMainFunctionNode.id} order:0`, `edge ${graphModuleNode.id} function ${graphMainFunctionNode.id} order:1000000000000`);
sparseOrderGraph = sparseOrderGraph.replace(/hash "graph:[0-9a-f]{16}"/, `hash "${recomputeGraphHash(sparseOrderGraph)}"`);
writeFileSync(graphSparseOrderPath, sparseOrderGraph);
const sparseOrderValidate = json(["validate", "--json", graphSparseOrderPath], { allowFailure: true });
assert.notEqual(sparseOrderValidate.code, 0);
assert.match(sparseOrderValidate.body.diagnostics[0].actual, /^GRF013\b/);
assert.match(sparseOrderValidate.body.diagnostics[0].message, /ordered edge group is sparse/);
let sparseArgGraph = graphDump.replace(/edge (#[^ ]+) arg (#[^ ]+) order:0/, "edge $1 arg $2 order:1000000000000");
sparseArgGraph = sparseArgGraph.replace(/hash "graph:[0-9a-f]{16}"/, `hash "${recomputeGraphHash(sparseArgGraph)}"`);
writeFileSync(graphSparseArgPath, sparseArgGraph);
const sparseArgValidate = json(["validate", "--json", graphSparseArgPath], { allowFailure: true });
assert.notEqual(sparseArgValidate.code, 0);
assert.match(sparseArgValidate.body.diagnostics[0].actual, /^GRF013\b/);
assert.match(sparseArgValidate.body.diagnostics[0].message, /ordered edge group is sparse/);
const graphWrongSchemaPath = join(outDir, "wrong-schema.program-graph");
writeFileSync(graphWrongSchemaPath, "zero-graph v2\n");
const graphWrongSchema = json(["validate", "--json", graphWrongSchemaPath], { allowFailure: true });
assert(graphWrongSchema.code);
assert.equal(graphWrongSchema.body.diagnostics[0].message, "unknown program graph schema version");
const graphFailedArtifactPath = join(outDir, "failed-validation.program-graph");
writeFileSync(graphFailedArtifactPath, [
  "zero-graph v1",
  "origin source-text",
  "module \"main\"",
  "hash \"\"",
  "validation \"decoded\" failed",
  "diagnostic code:\"GRF001\" message:\"program graph construction failed\"",
  "",
].join("\n"));
const graphFailedArtifact = json(["validate", "--json", graphFailedArtifactPath], { allowFailure: true });
assert(graphFailedArtifact.code);
assert.equal(graphFailedArtifact.body.diagnostics[0].message, "program graph input reports failed validation");
const graphTrailingArtifactPath = join(outDir, "trailing-content.program-graph");
writeFileSync(graphTrailingArtifactPath, `${graphDump}\nextra\n`);
const graphTrailingArtifact = json(["validate", "--json", graphTrailingArtifactPath], { allowFailure: true });
assert(graphTrailingArtifact.code);
assert.equal(graphTrailingArtifact.body.diagnostics[0].message, "unexpected content after graph header");

const skillsList = json(["skills", "list", "--json"]).body;
assert.equal(skillsList.success, true);
assert(skillsList.data.some((skill) => skill.name === "zero" && /Zero/.test(skill.description)));
const skillNames = new Set(skillsList.data.map((skill) => skill.name));
for (const name of [
  "zero",
  "agent",
  "builds",
  "diagnostics",
  "graph",
  "language",
  "packages",
  "stdlib",
  "testing",
]) {
  assert(skillNames.has(name), `missing bundled skill ${name}`);
}

const zeroSkill = json(["skills", "get", "zero", "--full", "--json"]).body;
assert.equal(zeroSkill.success, true);
assert.match(zeroSkill.data[0].content, /# Zero/);
assert.match(zeroSkill.data[0].content, /zero skills get zero --full/);
assert.match(zeroSkill.data[0].content, /zero check \[graph-or-package\]/);
assert.doesNotMatch(zeroSkill.data[0].content, /<file-or-package>/);
assert.equal(zeroSkill.data[0].files, undefined);
assert.match(zeroSkill.data[0].content, /at most once per session/);
for (const skillName of skillsList.data.map((skill) => skill.name)) {
  assert(zeroSkill.data[0].content.includes(`\`${skillName}\` (~`), `zero stub should index topic ${skillName} with a size`);
}

const agentSkill = json(["skills", "get", "agent", "--json"]).body;
assert.equal(agentSkill.success, true);
assert.match(agentSkill.data[0].content, /zero query --fn <name> --handles/);
assert.match(agentSkill.data[0].content, /--replace-in-fn handleLine/);
assert.match(agentSkill.data[0].content, /addParamTo fn="scan" name="bias" type="i32" default="0"/);
assert.match(agentSkill.data[0].content, /setConst name="limit" value="64"/);
assert.match(agentSkill.data[0].content, /upsertFunction handle/);
assert.match(agentSkill.data[0].content, /addReturnExpr fn="maybe" expr="null"/);
assert.match(agentSkill.data[0].content, /appendStmt fn="main" stmt="check std\.http\.listen\(world, 3000_u16\)"/);
assert.match(agentSkill.data[0].content, /addTestBody name="api add"/);
assert.match(agentSkill.data[0].content, /deleteTest name="api add"/);
assert.match(agentSkill.data[0].content, /--patch-text -/);
assert.match(agentSkill.data[0].content, /--no-help/);
assert.match(agentSkill.data[0].content, /validated: check-equivalent/);
assert.match(agentSkill.data[0].content, /zero skills get graph/);
assert.doesNotMatch(agentSkill.data[0].content, /--rewrite/);
const agentReplaceInFnIdx = agentSkill.data[0].content.indexOf("--replace-in-fn");
const agentReplaceFnIdx = agentSkill.data[0].content.indexOf("--replace-fn greet");
const agentDeclOpsIdx = agentSkill.data[0].content.indexOf("setConst");
assert(agentReplaceInFnIdx >= 0 && agentReplaceInFnIdx < agentReplaceFnIdx && agentReplaceFnIdx < agentDeclOpsIdx, "agent topic leads with --replace-in-fn, then --replace-fn, then decl ops");
assert.match(agentSkill.data[0].content, /Use JSON only when another tool must parse stable fields/);
assert.match(agentSkill.data[0].content, /zero skills get stdlib --topic std\.time/);
assert.match(agentSkill.data[0].content, /zero view --outline <module-or-file>/);
assert.doesNotMatch(agentSkill.data[0].content, /<file-or-package>/);
assert(agentSkill.data[0].content.length < 4096, `agent topic should stay compact, got ${agentSkill.data[0].content.length}`);

const languageSkill = json(["skills", "get", "language", "--json"]).body;
assert.equal(languageSkill.success, true);
assert.match(languageSkill.data[0].content, /# zerolang Language/);
assert.match(languageSkill.data[0].content, /pub fn main/);
assert.match(languageSkill.data[0].content, /human-readable projection syntax/);
assert.match(languageSkill.data[0].content, /not the package compiler input/);
assert.doesNotMatch(languageSkill.data[0].content, /\.0 source is canonical text/);
for (const typeName of [
  "Bool",
  "Void",
  "String",
  "Type",
  "World",
  "Maybe<T>",
  "Span<T>",
  "MutSpan<T>",
  "ref<T>",
  "mutref<T>",
  "owned<T>",
]) {
  assert.match(languageSkill.data[0].content, new RegExp(typeName.replace(/[<>]/g, "\\$&")));
}

const graphSkill = json(["skills", "get", "graph", "--json"]).body;
assert.equal(graphSkill.success, true);
assert.match(graphSkill.data[0].content, /# Zero Graph Authoring/);
assert.match(graphSkill.data[0].content, /primary agent authoring surface/);
assert.match(graphSkill.data[0].content, /zero query --find Block/);
assert.match(graphSkill.data[0].content, /zero dump --out \.zero\/agent\/app\.program-graph/);
assert.doesNotMatch(graphSkill.data[0].content, /<file-or-package>/);
for (const op of graphPatchHelpJson.operations) {
  assert(graphSkill.data[0].content.includes(op), `graph skill should include patch operation ${op}`);
}
for (const lowLevelOp of [
  "expect graphHash",
  "insert node=",
  "insertEdge from=",
  "replace node=",
]) {
  assert(graphSkill.data[0].content.includes(lowLevelOp), `graph skill should include low-level operation ${lowLevelOp}`);
}

const stdlibSkill = json(["skills", "get", "stdlib", "--json"]).body;
assert.equal(stdlibSkill.success, true);
assert.match(stdlibSkill.data[0].content, /std\.str/);
assert.match(stdlibSkill.data[0].content, /non-overlapping reverse/);
assert.match(stdlibSkill.data[0].content, /## Function Signatures/);
const stdSigText = readFileSync("native/zero-c/src/std_sig.c", "utf8");
const stdHelperNames = [...stdSigText.matchAll(/\{"(std\.[^"]+)",/g)].map((match) => match[1]);
const stdlibCatalog = stdlibSkill.data[0].content.split("## Function Signatures")[1].split("## Maybe Pattern")[0];
assert.equal((stdlibCatalog.match(/^[A-Za-z_][A-Za-z0-9_]*\(.*\) -> /gm) ?? []).length, stdHelperNames.length);
for (const helperName of stdHelperNames) {
  const moduleName = helperName.split(".").slice(0, 2).join(".");
  const shortName = helperName.slice(moduleName.length + 1);
  assert(stdlibCatalog.includes(`### ${moduleName}`), `stdlib skill should include module ${moduleName}`);
  assert(stdlibCatalog.includes(`${shortName}(`), `stdlib skill should include helper ${helperName}`);
}

const stdlibTopicSkill = json(["skills", "get", "stdlib", "--topic", "std.time", "--json"]).body;
assert.equal(stdlibTopicSkill.success, true);
assert.equal(stdlibTopicSkill.data[0].topic, "std.time");
assert.match(stdlibTopicSkill.data[0].content, /^### std\.time/);
assert.match(stdlibTopicSkill.data[0].content, /isRfc3339DateTime/);
assert.doesNotMatch(stdlibTopicSkill.data[0].content, /### std\.args/);
assert(stdlibTopicSkill.data[0].content.length < 4096, `stdlib --topic std.time should serve one section, got ${stdlibTopicSkill.data[0].content.length}`);
assert.equal(zero(["skills", "get", "stdlib", "--topic", "std.time"]).stdout, stdlibTopicSkill.data[0].content);
const stdlibTopicMiss = zero(["skills", "get", "stdlib", "--topic", "std.nosuch"], { allowFailure: true });
assert.notEqual(stdlibTopicMiss.code, 0);
assert.match(stdlibTopicMiss.stderr, /No section in skill 'stdlib' matches --topic std\.nosuch/);
assert.match(stdlibTopicMiss.stderr, /std\.time/);
assert.match(zeroSkill.data[0].content, /zero skills get stdlib --topic std\.time/);

const diagnosticSkill = json(["skills", "get", "diagnostics", "--json"]).body;
assert.equal(diagnosticSkill.success, true);
assert.match(diagnosticSkill.data[0].content, /fixSafety/);

const missingSkill = zero(["skills", "get", "missing", "--json"], { allowFailure: true });
assert.notEqual(missingSkill.code, 0);
assert.equal(JSON.parse(missingSkill.stdout).success, false);

const removedSkillsPath = zero(["skills", "path", "zero", "--json"], { allowFailure: true });
assert.notEqual(removedSkillsPath.code, 0);
assert.match(JSON.parse(removedSkillsPath.stdout).error, /Unknown skills subcommand: path/);

const badSkillsFlag = zero(["skills", "-x"], { allowFailure: true });
assert.notEqual(badSkillsFlag.code, 0);
assert.match(badSkillsFlag.stderr, /Unknown skills flag: -x/);

const badSkillsListFlag = zero(["skills", "list", "--unknown", "--json"], { allowFailure: true });
assert.notEqual(badSkillsListFlag.code, 0);
assert.match(JSON.parse(badSkillsListFlag.stdout).error, /Unknown skills flag: --unknown/);

const badSkillsGetFlag = zero(["skills", "get", "language", "--unknown", "--json"], { allowFailure: true });
assert.notEqual(badSkillsGetFlag.code, 0);
assert.match(JSON.parse(badSkillsGetFlag.stdout).error, /Unknown skills flag: --unknown/);

const lexerTokens = json(["tokens", "--json", "conformance/lexer/compiler-smoke.0"]).body;
assert.equal(lexerTokens.schemaVersion, 1);
assert.equal(lexerTokens.syntax, "canonical");
assert.match(lexerTokens.sourceFile, /compiler-smoke\.0$/);
assert.deepEqual(lexerTokens.tokens.slice(0, 4).map((token) => `${token.kind}:${token.text}`), [
  "word:use",
  "word:std",
  "symbol:.",
  "word:mem",
]);
assert.deepEqual(lexerTokens.tokens.filter((token) => token.kind === "number").map((token) => token.text), ["123", "0xff", "0b101", "42_u8"]);
assert.deepEqual(lexerTokens.tokens.filter((token) => token.kind === "string" || token.kind === "char").map((token) => `${token.kind}:${token.text}`), [
  'string:"hi"',
  "char:'x'",
]);
assert.equal(lexerTokens.tokens[0].line, 1);
assert.equal(lexerTokens.tokens[0].column, 1);
assert.equal(lexerTokens.tokens[0].offset, 0);
assert.equal(lexerTokens.tokens[0].length, 3);
assert.equal(lexerTokens.tokens[5].offset, 12);
assert.equal(lexerTokens.tokens[5].length, 1);
assert.equal(lexerTokens.tokens[8].kind, "word");
assert.equal(lexerTokens.tokens[8].text, "main");
assert.equal(lexerTokens.tokens[8].line, 3);
assert.equal(lexerTokens.tokens[8].column, 8);
assert.equal(lexerTokens.tokens.at(-1).kind, "eof");
assert.equal(lexerTokens.tokens.at(-1).length, 0);

const parseTree = json(["parse", "--json", "conformance/format/functions-blocks.0"]).body;
assert.equal(parseTree.schemaVersion, 1);
assert.equal(parseTree.root.kind, "module");
assert.equal(parseTree.root.shapeCount, 0);
assert.equal(parseTree.root.enumCount, 0);
assert.equal(parseTree.root.choiceCount, 0);
assert.equal(parseTree.root.functionCount, 2);
assert.equal(parseTree.functions[0].name, "helper");
assert.equal(parseTree.functions[0].paramCount, 1);
assert.deepEqual(parseTree.functions[0].bodyKinds, ["if"]);
assert.equal(parseTree.functions[1].name, "main");
assert.equal(parseTree.functions[1].paramCount, 0);
assert.deepEqual(parseTree.functions[1].bodyKinds, ["let", "while"]);

const unsupportedSourceFixture = join(outDir, "unsupported-source.txt");
const unsupportedSourceText =
  "# command fixture\n" +
  "fn inc i32 value i32\n" +
  "  ret + value 1\n" +
  "\n" +
  "pub fn main Void\n" +
  "  let total i32 inc 41\n";
rmSync(unsupportedSourceFixture, { force: true });
writeFileSync(unsupportedSourceFixture, unsupportedSourceText);
for (const args of [
  ["check", "--json", unsupportedSourceFixture],
  ["build", "--json", unsupportedSourceFixture],
  ["fix", "--plan", "--json", unsupportedSourceFixture],
] as string[][]) {
  const rejected = json(args, { allowFailure: true });
  assert.notEqual(rejected.code, 0);
  assert.equal(rejected.body.diagnostics[0].code, "BLD002");
  assert.equal(rejected.body.diagnostics[0].message, "compiler command requires graph input");
  assert.equal(rejected.body.diagnostics[0].expected, "package zero.graph store, .graph store, or ProgramGraph artifact");
}
const rawProjectionWithGraph = join(outDir, "raw-projection-with-graph.0");
writeProjectionFile(rawProjectionWithGraph, "pub fn main() -> Void { }\n");
importProjectionSidecar(rawProjectionWithGraph);
const rawProjectionWithGraphRejected = jsonRaw(["check", "--json", rawProjectionWithGraph], { allowFailure: true });
assert.notEqual(rawProjectionWithGraphRejected.code, 0);
assert.equal(rawProjectionWithGraphRejected.body.diagnostics[0].code, "BLD002");
assert.equal(rawProjectionWithGraphRejected.body.diagnostics[0].message, "compiler command requires graph input");
assert.equal(rawProjectionWithGraphRejected.body.diagnostics[0].expected, "package zero.graph store, .graph store, or ProgramGraph artifact");
const rawProjectionNoGraph = join(outDir, "raw-projection-no-graph.0");
rmSync(rawProjectionNoGraph, { force: true });
writeFileSync(rawProjectionNoGraph, "pub fn main() -> Void { }\n");
for (const args of [
  ["check", "--json", rawProjectionNoGraph],
  ["build", "--json", rawProjectionNoGraph],
  ["test", "--json", rawProjectionNoGraph],
  ["size", "--json", rawProjectionNoGraph],
  ["doc", "--json", rawProjectionNoGraph],
  ["dev", "--json", rawProjectionNoGraph],
  ["abi", "dump", "--json", rawProjectionNoGraph],
  ["fix", "--plan", "--json", rawProjectionNoGraph],
] as string[][]) {
  const rejected = jsonRaw(args, { allowFailure: true });
  assert.notEqual(rejected.code, 0);
  assert.equal(rejected.body.diagnostics[0].code, "BLD002");
  assert.equal(rejected.body.diagnostics[0].message, "compiler command requires graph input");
  assert.equal(rejected.body.diagnostics[0].expected, "package zero.graph store, .graph store, or ProgramGraph artifact");
}
const rawProjectionRunRejected = zeroRaw(["run", rawProjectionNoGraph], { allowFailure: true });
assert.notEqual(rawProjectionRunRejected.code, 0);
assert.match(rawProjectionRunRejected.stderr, /compiler command requires graph input/);
assert.match(rawProjectionRunRejected.stderr, /package zero\.graph store, \.graph store, or ProgramGraph artifact/);
for (const args of [
  ["tokens", "--json", unsupportedSourceFixture],
  ["parse", "--json", unsupportedSourceFixture],
] as string[][]) {
  const rejected = json(args, { allowFailure: true });
  assert.notEqual(rejected.code, 0);
  assert.equal(rejected.body.diagnostics[0].code, "BLD002");
  assert.equal(rejected.body.diagnostics[0].message, "expected Zero source file or package");
  assert.equal(rejected.body.diagnostics[0].expected, ".0 source file, zero.toml, zero.json, or package directory");
}
for (const args of [
  ["view", "--json", unsupportedSourceFixture],
] as string[][]) {
  const rejected = json(args, { allowFailure: true });
  assert.notEqual(rejected.code, 0);
  assert.equal(rejected.body.diagnostics[0].code, "PAR100");
  assert.equal(rejected.body.diagnostics[0].message, "expected zero-graph v1 header");
}
const unsupportedFmtRejected = zero(["fmt", "--check", unsupportedSourceFixture], { allowFailure: true });
assert.notEqual(unsupportedFmtRejected.code, 0);
assert.match(unsupportedFmtRejected.stderr, /expected Zero source file or package/);
assert.match(unsupportedFmtRejected.stderr, /\.0 source file, zero\.toml, zero\.json, or package directory/);

const unsupportedSourcePackage = join(outDir, "unsupported-source-package");
rmSync(unsupportedSourcePackage, { recursive: true, force: true });
mkdirSync(join(unsupportedSourcePackage, "src"), { recursive: true });
writeZeroTomlSync(unsupportedSourcePackage, {
  package: { name: "unsupported-source-package", version: "0.1.0" },
  targets: { cli: { kind: "exe", main: "src/main.txt" } },
});
writeFileSync(join(unsupportedSourcePackage, "src", "main.txt"), unsupportedSourceText);
const unsupportedPackageRejected = json(["check", "--json", unsupportedSourcePackage], { allowFailure: true });
assert.notEqual(unsupportedPackageRejected.code, 0);
assert.equal(unsupportedPackageRejected.body.diagnostics[0].code, "RGP001");
assert.equal(unsupportedPackageRejected.body.diagnostics[0].message, "repository graph store is missing");
assert.equal(unsupportedPackageRejected.body.diagnostics[0].expected, "checked-in zero.graph repository graph store");

const testJson = json(["test", "--json", "--filter", "addition", "conformance/native/pass/test-blocks.graph"]).body;
assert.equal(testJson.schemaVersion, 1);
assert.equal(testJson.ok, true);
assert.match(testJson.stdout, /1 test\(s\) ok/);
assert.equal(testJson.testBackend, "direct-program-graph");
assertSourceGraph(testJson, "conformance/native/pass/test-blocks.graph", "module:test-blocks", "direct-program-graph", false);
assert.equal(testJson.testDiscovery.mode, "program-graph");
assert.equal(testJson.testDiscovery.filter, "addition");
assert.equal(testJson.fixtures.snapshotKey, "zero-test-graph-native-v1");
assert.equal(testJson.targetFacts.capabilitySupport.status, "supported");
assert.equal(testJson.results[0].status, "passed");

const packageTestJson = json(["test", "--json", "conformance/packages/test-app"]).body;
assert.equal(packageTestJson.ok, true);
assertSourceGraph(packageTestJson, "conformance/packages/test-app/zero.graph", "package:test-app@0.1.0", "direct-program-graph", false);
assert.equal(packageTestJson.testDiscovery.mode, "package-graph");
assert.equal(packageTestJson.discoveredTests, 3);
assert.equal(packageTestJson.expectedFailures, 1);
assert(packageTestJson.fixtures.sourceFiles.some((path) => path.endsWith("helper.0")));

const expectedFailTestJson = json(["test", "--json", "conformance/native/pass/test-expected-fail.graph"]).body;
assert.equal(expectedFailTestJson.ok, true);
assert.equal(expectedFailTestJson.expectedFailures, 1);
assert.equal(expectedFailTestJson.results[0].status, "expected-fail");

assert.match(zero(["fmt", "--check", "conformance/native/pass/test-blocks.0"]).stdout, /fmt ok/);

const unknownFlag = zero(["check", "--jsoon", "examples/hello.graph"], { allowFailure: true });
assert.notEqual(unknownFlag.code, 0);
assert.match(unknownFlag.stderr, /unknown flag: --jsoon/);
assert.match(unknownFlag.stderr, /--json/);
assert.equal(hasAnsiControlBytes(unknownFlag.stderr), false);

const cleanProbe = join(".zero", "out", "contract-clean", "tmp.txt");
mkdirSync(join(".zero", "out", "contract-clean"), { recursive: true });
writeFileSync(cleanProbe, "tmp");
assert(existsSync(cleanProbe));
const clean = zero(["clean"]).stdout;
assert.match(clean, /removed:/);
assert(!existsSync(cleanProbe));

for (const kind of ["cli", "lib", "package"]) {
  const project = join(outDir, `init-template-${kind}`);
  rmSync(project, { recursive: true, force: true });
  const created = zero(["init", "--template", kind, project]).stdout;
  assert.match(created, /graph template init ok/);
  assert.match(created, new RegExp(`template: ${kind}`));
  assert(existsSync(join(project, "zero.toml")));
  assert(!existsSync(join(project, "zero.json")));
  const manifest = readGeneratedManifest(project);
  const readme = readFileSync(join(project, "README.md"), "utf8");
  readFileSync(join(project, ".gitignore"), "utf8");
  assertTemplateManifest(kind, manifest, readme);
  zero(["check", project]);
  zero(["test", project]);
  assert.match(zero(["fmt", "--check", project]).stdout, /fmt ok/);
  if (kind !== "lib") {
    const templateRun = zero(["run", "--out", join(project, "run-app"), project]).stdout;
    assert.match(templateRun, kind === "cli" ? /hello from zero\n/ : /package ok\n/);
  }
  const devReport = json(["dev", "--json", "--target", "linux-musl-x64", project]).body;
  assertDevReport(devReport, kind);
  if (kind === "lib") {
    const docReport = json(["doc", "--json", project]).body;
    assert.equal(docReport.schemaVersion, 1);
    assert.equal(docReport.generatedCBytes, 0);
  }
  if (kind === "lib") {
    removeInlineTests(join(project, "src", "lib.0"));
    zero(["parse", "--json", join(project, "src", "lib.0")]);
  } else {
    const buildOut = join(project, "app");
    const templateBuild = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", project, "--out", buildOut]).body;
    assert.equal(templateBuild.generatedCBytes, 0);
    assert.equal(templateBuild.objectBackend.objectEmission.path, "direct-elf64-exe");
  }
}

const tinyHello = join(outDir, "tiny-hello");
rmSync(tinyHello, { force: true });
zero(["build", "--release", "tiny", "--target", "linux-musl-x64", "examples/hello.graph", "--out", tinyHello]);
assert(statSync(tinyHello).size < 10 * 1024);
const profileCacheCheckSource = join(outDir, "profile-cache-check.0");
writeProjectionFile(profileCacheCheckSource, `pub fn main(world: World) -> Void raises {
    check world.out.write("profile cache ${process.pid}\\n")
}
`);
const profileCacheCheckGraph = importProjectionSidecar(profileCacheCheckSource);
const profileCacheCheck = json(["size", "--json", "--profile", "fast", profileCacheCheckGraph]).body;
assert.equal(profileCacheCheck.graph.artifact, profileCacheCheckGraph);
const profileCacheSpecialization = profileCacheCheck.compilerCaches.find((cache) => cache.name === "specialization");
assert(profileCacheSpecialization);
assert.equal(profileCacheCheck.incrementalInvalidation.profileDependency, "fast");
assert.equal(existsSync(join(".zero", "cache", "native", `specialization-${profileCacheSpecialization.key}.cache`)), true);
const buildReport = json(["build", "--json", "--target", "linux-musl-x64", "examples/hello.graph", "--out", join(outDir, "hello-linux-report")]).body;
assert.equal(buildReport.schemaVersion, 1);
assert.equal(buildReport.emit, "exe");
assert.equal(buildReport.hostTarget, version.host);
assert.equal(buildReport.target, "linux-musl-x64");
assert.equal(buildReport.compiler, "zero-elf64");
assert(buildReport.artifactBytes > 0);
assert.equal(buildReport.generatedCBytes, 0);
assert(buildReport.loweredIrBytes > 0);
assert.equal(buildReport.targetSupport.fsAvailable, true);
assert.equal(buildReport.profileSemantics.profileKey, "small");
assert.equal(buildReport.profileSemantics.profileBudget.generatedCBytes, 0);
assert.equal(buildReport.safetyFacts.profileKey, "small");
assert.equal(buildReport.safetyFacts.bounds.policy, "checked");
assert.equal(buildReport.safetyFacts.bounds.optimizerElision, false);
assert.equal(buildReport.safetyFacts.overflow.policy, "literal-range-checked-runtime-unchecked");
assert.equal(buildReport.safetyFacts.initialization.locals, "initializer-required");
assert.equal(buildReport.safetyFacts.initialization.maybePayloadReads, "guard-checked");
assert.equal(buildReport.safetyFacts.aliasing.mutableAliases, "diagnostic");
assert.equal(buildReport.safetyFacts.mir.invalidMemoryContractsBlockEmission, true);
assert.equal(buildReport.profileBudget.cBridgeFallback, false);
assert.equal(buildReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(buildReport.objectBackend.linking.externalToolchain, "none");
assertReleaseTargetContract(buildReport, {
  target: "linux-musl-x64",
  emit: "exe",
  objectFormat: "elf",
  artifactKind: "native-executable",
  linkerFlavor: "elf64",
  targetLibcMode: "bundled-libc",
});
repeatBuildHash(["build", "--json", "--target", "linux-musl-x64", "examples/hello.graph", "--out", join(outDir, "hello-linux-report")], join(outDir, "hello-linux-report"), join(outDir, "hello-linux-report.repeat"));

const runArtifact = join(outDir, "run-add");
rmSync(runArtifact, { force: true });
rmSync(`${runArtifact}.exe`, { force: true });
rmSync(`${runArtifact}.c`, { force: true });
const runResult = zero(["run", "--out", runArtifact, "examples/add.graph"]);
assert.match(runResult.stdout, /math works\n/);
assert(existsSync(version.host.startsWith("win32") ? `${runArtifact}.exe` : runArtifact));
assert.equal(existsSync(`${runArtifact}.c`), false);

for (const [requestedProfile, canonicalProfile, profileKey] of [
  ["debug", "debug", "debug"],
  ["fast", "release-fast", "fast"],
  ["small", "release-small", "small"],
  ["tiny", "tiny", "tiny"],
]) {
  const profileOut = join(outDir, `profile-${requestedProfile}-hello`);
  const profileReport = json(["build", "--json", "--profile", requestedProfile, "--target", "linux-musl-x64", "examples/hello.graph", "--out", profileOut]).body;
  assert.equal(profileReport.generatedCBytes, 0);
  assert.equal(profileReport.profileSemantics.canonical, canonicalProfile);
  assert.equal(profileReport.profileSemantics.profileKey, profileKey);
  assert.equal(profileReport.profileSemantics.boundsPolicy, "checked");
  assert.equal(profileReport.profileSemantics.overflowPolicy, "literal-range-checked-runtime-unchecked");
  assert.equal(profileReport.profileSemantics.profileBudget.generatedCBytes, 0);
  assert.equal(profileReport.safetyFacts.profile, canonicalProfile);
  assert.equal(profileReport.safetyFacts.profileKey, profileKey);
  assert.equal(profileReport.profileBudget.helperBudgetPolicy, profileReport.profileSemantics.profileBudget.helperBudgetPolicy);
  repeatBuildHash(["build", "--json", "--profile", requestedProfile, "--target", "linux-musl-x64", "examples/hello.graph", "--out", profileOut], profileOut, `${profileOut}.repeat`);
}

const profileSizeReport = json(["size", "--json", "--profile", "debug", "--target", "linux-musl-x64", "examples/hello.graph"]).body;
assert.equal(profileSizeReport.generatedCBytes, 0);
assert.equal(profileSizeReport.graph.artifact, "examples/hello.graph");
assert.equal(profileSizeReport.graph.lowering, "mapped-final-mir");
assert.equal(profileSizeReport.profileSemantics.profileKey, "debug");
assert.equal(profileSizeReport.safetyFacts.profileKey, "debug");
assert.equal(profileSizeReport.safetyFacts.uncheckedSurfaces[0].policy, "externally-trusted");
assert.equal(profileSizeReport.sizeBreakdown.profileKey, "debug");
assert(Array.isArray(profileSizeReport.sizeBreakdown.functions));
assert(profileSizeReport.sizeBreakdown.sections.some((item) => item.name === "debug-metadata"));
assert(Array.isArray(profileSizeReport.sizeBreakdown.stdlibHelpers));
assert(Array.isArray(profileSizeReport.sizeBreakdown.imports));
assert(Array.isArray(profileSizeReport.sizeBreakdown.runtimeShims));
assert(profileSizeReport.sizeBreakdown.debugMetadata.bytes > 0);
assert(profileSizeReport.compilerCaches.some((item) => item.name === "mappedFinalMir" && item.sourceKind === "program-graph" && item.programReconstructed === false));
assert(profileSizeReport.retentionReasons.some((item) => item.kind === "debugMetadata"));
assert(profileSizeReport.optimizationHints.some((item) => item.id === "profile-debug-metadata"));
assert.equal(profileSizeReport.profileBudget.debugMetadataAllowed, true);

const directObjPath = join(outDir, "direct-obj-add.o");
rmSync(directObjPath, { force: true });
const directObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-obj-add.graph", "--out", directObjPath]).body;
const directObjBytes = readFileSync(directObjPath);
assert.equal(directObjReport.emit, "obj");
assert.equal(directObjReport.compiler, "zero-elf64");
assert.equal(directObjReport.generatedCBytes, 0);
assert(directObjReport.loweredIrBytes > 0);
assert.equal(directObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directObjReport.objectBackend.linking.externalToolchain, "none");
assert.equal(directObjBytes[0], 0x7f);
assert.equal(directObjBytes[1], 0x45);
assert.equal(directObjBytes[2], 0x4c);
assert.equal(directObjBytes[3], 0x46);
assert.equal(directObjBytes.readUInt16LE(16), 1);
assert.equal(directObjBytes.readUInt16LE(18), 62);
const directI64ObjPath = join(outDir, "direct-i64-return.o");
rmSync(directI64ObjPath, { force: true });
const directI64ObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-i64-return.graph", "--out", directI64ObjPath]).body;
const directI64ObjBytes = readFileSync(directI64ObjPath);
assert.equal(directI64ObjReport.emit, "obj");
assert.equal(directI64ObjReport.compiler, "zero-elf64");
assert.equal(directI64ObjReport.generatedCBytes, 0);
assert.equal(directI64ObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert(directI64ObjBytes.includes(Buffer.from([0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f])));
assert(directI64ObjBytes.includes(Buffer.from([0x48, 0x01, 0xc8])));
const directShapeObjPath = join(outDir, "direct-token-shape.o");
rmSync(directShapeObjPath, { force: true });
const directShapeObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-token-shape.graph", "--out", directShapeObjPath]).body;
const directShapeObjBytes = readFileSync(directShapeObjPath);
assert.equal(directShapeObjReport.emit, "obj");
assert.equal(directShapeObjReport.compiler, "zero-elf64");
assert.equal(directShapeObjReport.generatedCBytes, 0);
assert.equal(directShapeObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directShapeObjBytes[0], 0x7f);
assert.equal(directShapeObjBytes[1], 0x45);
const directSpanReadObjPath = join(outDir, "direct-span-read.o");
rmSync(directSpanReadObjPath, { force: true });
const directSpanReadObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-span-read.graph", "--out", directSpanReadObjPath]).body;
const directSpanReadObjBytes = readFileSync(directSpanReadObjPath);
assert.equal(directSpanReadObjReport.emit, "obj");
assert.equal(directSpanReadObjReport.compiler, "zero-elf64");
assert.equal(directSpanReadObjReport.generatedCBytes, 0);
assert.equal(directSpanReadObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directSpanReadObjReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directSpanReadObjReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert.equal(directSpanReadObjBytes[0], 0x7f);
assert.equal(directSpanReadObjBytes[1], 0x45);
const directByteViewLocalsObjPath = join(outDir, "direct-byte-view-reloc.o");
rmSync(directByteViewLocalsObjPath, { force: true });
const directByteViewLocalsObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-byte-view-reloc.graph", "--out", directByteViewLocalsObjPath]).body;
const directByteViewLocalsObjBytes = readFileSync(directByteViewLocalsObjPath);
assert.equal(directByteViewLocalsObjReport.emit, "obj");
assert.equal(directByteViewLocalsObjReport.compiler, "zero-elf64");
assert.equal(directByteViewLocalsObjReport.generatedCBytes, 0);
assert.equal(directByteViewLocalsObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directByteViewLocalsObjReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directByteViewLocalsObjReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert(directByteViewLocalsObjBytes.includes(Buffer.from(".rodata\0")));
assert(directByteViewLocalsObjBytes.includes(Buffer.from(".rela.text\0")));
const directRescueObjPath = join(outDir, "direct-rescue-basic.o");
rmSync(directRescueObjPath, { force: true });
const directRescueObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-rescue-basic.graph", "--out", directRescueObjPath]).body;
const directRescueObjBytes = readFileSync(directRescueObjPath);
assert.equal(directRescueObjReport.emit, "obj");
assert.equal(directRescueObjReport.compiler, "zero-elf64");
assert.equal(directRescueObjReport.generatedCBytes, 0);
assert.equal(directRescueObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directRescueObjBytes[0], 0x7f);
assert.equal(directRescueObjBytes[1], 0x45);
const directExePath = join(outDir, "direct-exe-return");
rmSync(directExePath, { force: true });
const directExeReport = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-exe-return.graph", "--out", directExePath]).body;
const directExeBytes = readFileSync(directExePath);
assert.equal(directExeReport.emit, "exe");
assert.equal(directExeReport.compiler, "zero-elf64");
assert.equal(directExeReport.generatedCBytes, 0);
assert(directExeReport.loweredIrBytes > 0);
assert(directExeReport.artifactBytes < 512);
assert.equal(directExeReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directExeReport.objectBackend.objectEmission.dataSections, false);
assert.equal(directExeReport.objectBackend.linking.externalToolchain, "none");
assert.equal(directExeBytes[0], 0x7f);
assert.equal(directExeBytes[1], 0x45);
assert.equal(directExeBytes[2], 0x4c);
assert.equal(directExeBytes[3], 0x46);
assert.equal(directExeBytes.readUInt16LE(16), 2);
assert.equal(directExeBytes.readUInt16LE(18), 62);
assert.equal(directExeBytes.readUInt16LE(54), 56);
assert.equal(directExeBytes.readUInt16LE(56), 1);
const removedEmitC = json(["build", "--json", "--emit", "c", "--target", "linux-musl-x64", "examples/direct-exe-return.graph", "--out", join(outDir, "removed-c-backend.c")], { allowFailure: true });
assert.notEqual(removedEmitC.code, 0);
assert.equal(removedEmitC.body.diagnostics[0].code, "BLD003");
assert.equal(removedEmitC.body.diagnostics[0].repair.id, "use-direct-emitter");
const removedLegacyFlag = json(["build", "--json", "--legacy-backend", "--target", "linux-musl-x64", "examples/direct-exe-return.graph", "--out", join(outDir, "removed-legacy-flag")], { allowFailure: true });
assert.notEqual(removedLegacyFlag.code, 0);
assert.equal(removedLegacyFlag.body.diagnostics[0].code, "BLD003");
assert.equal(removedLegacyFlag.body.diagnostics[0].repair.id, "use-direct-emitter");
const directMachOExePath = join(outDir, "direct-macho-exe-return");
rmSync(directMachOExePath, { force: true });
const directMachOExeReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-macho64", "--target", "darwin-arm64", "examples/direct-exe-return.graph", "--out", directMachOExePath]).body;
const directMachOExeBytes = readFileSync(directMachOExePath);
assert.equal(directMachOExeReport.emit, "exe");
assert.equal(directMachOExeReport.compiler, "zero-macho64");
assert.equal(directMachOExeReport.generatedCBytes, 0);
assert.equal(directMachOExeReport.objectBackend.objectEmission.path, "direct-macho64-exe");
assert.equal(directMachOExeReport.objectBackend.targetFacts.status, "native-exe");
assertReleaseTargetContract(directMachOExeReport, {
  target: "darwin-arm64",
  emit: "exe",
  objectFormat: "macho",
  artifactKind: "native-executable",
  linkerFlavor: "macho64",
  targetLibcMode: "host-default",
});
repeatBuildHash(["build", "--json", "--emit", "exe", "--backend", "zero-macho64", "--target", "darwin-arm64", "examples/direct-exe-return.graph", "--out", directMachOExePath], directMachOExePath, `${directMachOExePath}.repeat`);
assert.equal(directMachOExeBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOExeBytes.readUInt32LE(12), 2);
const directMachOExeUuid = assertMachOLoadCommand(directMachOExeBytes, 0x1b, 24);
assert(!directMachOExeUuid.subarray(8, 24).every((byte) => byte === 0));
assert(directMachOExeBytes.includes(Buffer.from("/usr/lib/dyld")));
assert(directMachOExeBytes.includes(Buffer.from("zero-direct")));
const directMachOX64ExePath = join(outDir, "direct-macho-x64-hello");
rmSync(directMachOX64ExePath, { force: true });
const directMachOX64ExeReport = json(["build", "--json", "--emit", "exe", "--target", "darwin-x64", "examples/hello.graph", "--out", directMachOX64ExePath]).body;
const directMachOX64ExeBytes = readFileSync(directMachOX64ExePath);
assert.equal(directMachOX64ExeReport.emit, "exe");
assert.equal(directMachOX64ExeReport.compiler, "zero-macho-x64");
assert.equal(directMachOX64ExeReport.generatedCBytes, 0);
assert.equal(directMachOX64ExeReport.objectBackend.objectEmission.path, "direct-macho-x64-exe");
assert.equal(directMachOX64ExeReport.objectBackend.targetFacts.status, "native-exe");
assertReleaseTargetContract(directMachOX64ExeReport, {
  target: "darwin-x64",
  emit: "exe",
  objectFormat: "macho",
  artifactKind: "native-executable",
  linkerFlavor: "macho64",
  targetLibcMode: "sysroot",
});
repeatBuildHash(["build", "--json", "--emit", "exe", "--target", "darwin-x64", "examples/hello.graph", "--out", directMachOX64ExePath], directMachOX64ExePath, `${directMachOX64ExePath}.repeat`);
assert.equal(directMachOX64ExeBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOX64ExeBytes.readUInt32LE(4), 0x01000007);
assert.equal(directMachOX64ExeBytes.readUInt32LE(8), 3);
assert.equal(directMachOX64ExeBytes.readUInt32LE(12), 2);
const directMachOX64ExeUuid = assertMachOLoadCommand(directMachOX64ExeBytes, 0x1b, 24);
assert(!directMachOX64ExeUuid.subarray(8, 24).every((byte) => byte === 0));
assert(directMachOX64ExeBytes.includes(Buffer.from("/usr/lib/dyld")));
assert(directMachOX64ExeBytes.includes(Buffer.from("zero-direct-x64")));
assert(directMachOX64ExeBytes.includes(Buffer.from("hello from zero")));
const directMachOX64UnhandledPath = join(outDir, "direct-macho-x64-unhandled-error");
rmSync(directMachOX64UnhandledPath, { force: true });
const directMachOX64UnhandledReport = json(["build", "--json", "--emit", "exe", "--target", "darwin-x64", "examples/direct-unhandled-error-exit.graph", "--out", directMachOX64UnhandledPath]).body;
const directMachOX64UnhandledBytes = readFileSync(directMachOX64UnhandledPath);
assert.equal(directMachOX64UnhandledReport.emit, "exe");
assert.equal(directMachOX64UnhandledReport.compiler, "zero-macho-x64");
assert.equal(directMachOX64UnhandledReport.generatedCBytes, 0);
assert.equal(directMachOX64UnhandledReport.objectBackend.objectEmission.path, "direct-macho-x64-exe");
assert.equal(directMachOX64UnhandledReport.objectBackend.directFacts.runtimeHelperCount, 0);
assertReleaseTargetContract(directMachOX64UnhandledReport, {
  target: "darwin-x64",
  emit: "exe",
  objectFormat: "macho",
  artifactKind: "native-executable",
  linkerFlavor: "macho64",
  targetLibcMode: "sysroot",
});
assert.equal(directMachOX64UnhandledBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOX64UnhandledBytes.readUInt32LE(4), 0x01000007);
assert.equal(directMachOX64UnhandledBytes.readUInt32LE(8), 3);
assert.equal(directMachOX64UnhandledBytes.readUInt32LE(12), 2);
assert(directMachOX64UnhandledBytes.includes(Buffer.from("/usr/lib/dyld")));
assert(directMachOX64UnhandledBytes.includes(Buffer.from("zero-direct-x64")));
const directMachOU8ExePath = join(outDir, "direct-macho-u8-return");
rmSync(directMachOU8ExePath, { force: true });
const directMachOU8ExeReport = json(["build", "--json", "--emit", "exe", "--target", "darwin-arm64", "examples/direct-string-literal.graph", "--out", directMachOU8ExePath]).body;
assert.equal(directMachOU8ExeReport.emit, "exe");
assert.equal(directMachOU8ExeReport.compiler, "zero-macho64");
assert.equal(directMachOU8ExeReport.generatedCBytes, 0);
assert.equal(directMachOU8ExeReport.objectBackend.objectEmission.path, "direct-macho64-exe");
const directCoffExePath = join(outDir, "direct-coff-exe-return");
rmSync(`${directCoffExePath}.exe`, { force: true });
const directCoffExeReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-coff-x64", "--target", "win32-x64.exe", "examples/direct-exe-return.graph", "--out", directCoffExePath]).body;
const directCoffExeBytes = readFileSync(`${directCoffExePath}.exe`);
const directCoffPeOffset = directCoffExeBytes.readUInt32LE(0x3c);
assert.equal(directCoffExeReport.emit, "exe");
assert.equal(directCoffExeReport.compiler, "zero-coff-x64");
assert.equal(directCoffExeReport.generatedCBytes, 0);
assert.equal(directCoffExeReport.objectBackend.objectEmission.path, "direct-coff-x64-exe");
assert.equal(directCoffExeReport.objectBackend.targetFacts.status, "native-exe");
assertReleaseTargetContract(directCoffExeReport, {
  target: "win32-x64.exe",
  emit: "exe",
  objectFormat: "coff",
  artifactKind: "native-executable",
  linkerFlavor: "coff",
  targetLibcMode: "sysroot",
});
assert.equal(directCoffExeReport.releaseTargetContract.sysroot.requiredByTarget, true);
assert.equal(directCoffExeReport.releaseTargetContract.sysroot.status, "not-used-by-direct-artifact");
repeatBuildHash(["build", "--json", "--emit", "exe", "--backend", "zero-coff-x64", "--target", "win32-x64.exe", "examples/direct-exe-return.graph", "--out", directCoffExePath], `${directCoffExePath}.exe`, `${directCoffExePath}.repeat`, `${directCoffExePath}.repeat.exe`);
assert.equal(directCoffExeBytes.toString("ascii", directCoffPeOffset, directCoffPeOffset + 4), "PE\u0000\u0000");
assert.equal(directCoffExeBytes.readUInt16LE(directCoffPeOffset + 4), 0x8664);
assert(directCoffExeBytes.includes(Buffer.from("KERNEL32.dll")));
const directCoffArm64ExePath = join(outDir, "direct-coff-arm64-exe-return");
rmSync(`${directCoffArm64ExePath}.exe`, { force: true });
const directCoffArm64ExeReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-coff-aarch64", "--target", "win32-arm64.exe", "examples/direct-exe-return.graph", "--out", directCoffArm64ExePath]).body;
const directCoffArm64ExeBytes = readFileSync(`${directCoffArm64ExePath}.exe`);
const directCoffArm64PeOffset = directCoffArm64ExeBytes.readUInt32LE(0x3c);
assert.equal(directCoffArm64ExeReport.emit, "exe");
assert.equal(directCoffArm64ExeReport.compiler, "zero-coff-aarch64");
assert.equal(directCoffArm64ExeReport.generatedCBytes, 0);
assert.equal(directCoffArm64ExeReport.objectBackend.objectEmission.path, "direct-coff-aarch64-exe");
assert.equal(directCoffArm64ExeReport.objectBackend.targetFacts.status, "native-exe");
assertReleaseTargetContract(directCoffArm64ExeReport, {
  target: "win32-arm64.exe",
  emit: "exe",
  objectFormat: "coff",
  artifactKind: "native-executable",
  linkerFlavor: "coff",
  targetLibcMode: "sysroot",
});
assert.equal(directCoffArm64ExeReport.releaseTargetContract.sysroot.requiredByTarget, true);
assert.equal(directCoffArm64ExeReport.releaseTargetContract.sysroot.status, "not-used-by-direct-artifact");
assert.equal(directCoffArm64ExeBytes.toString("ascii", directCoffArm64PeOffset, directCoffArm64PeOffset + 4), "PE\u0000\u0000");
assert.equal(directCoffArm64ExeBytes.readUInt16LE(directCoffArm64PeOffset + 4), 0xaa64);
assert(directCoffArm64ExeBytes.includes(Buffer.from("KERNEL32.dll")));
const directCoffArm64HelloPath = join(outDir, "direct-coff-arm64-hello");
rmSync(`${directCoffArm64HelloPath}.exe`, { force: true });
const directCoffArm64HelloReport = json(["build", "--json", "--emit", "exe", "--target", "win32-arm64.exe", "examples/hello.graph", "--out", directCoffArm64HelloPath]).body;
const directCoffArm64HelloBytes = readFileSync(`${directCoffArm64HelloPath}.exe`);
assert.equal(directCoffArm64HelloReport.compiler, "zero-coff-aarch64");
assert.equal(directCoffArm64HelloReport.generatedCBytes, 0);
assert.equal(directCoffArm64HelloReport.objectBackend.objectEmission.path, "direct-coff-aarch64-exe");
assert.equal(directCoffArm64HelloReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert(directCoffArm64HelloBytes.includes(Buffer.from("hello from zero")));
assert(hasAarch64Instruction(directCoffArm64HelloBytes, 0xf90017fe));
assert(hasAarch64Instruction(directCoffArm64HelloBytes, 0xf94017fe));
const directCoffU8ExePath = join(outDir, "direct-coff-u8-return");
rmSync(`${directCoffU8ExePath}.exe`, { force: true });
const directCoffU8ExeReport = json(["build", "--json", "--emit", "exe", "--target", "win32-x64.exe", "examples/direct-string-literal.graph", "--out", directCoffU8ExePath]).body;
assert.equal(directCoffU8ExeReport.emit, "exe");
assert.equal(directCoffU8ExeReport.compiler, "zero-coff-x64");
assert.equal(directCoffU8ExeReport.generatedCBytes, 0);
assert.equal(directCoffU8ExeReport.objectBackend.objectEmission.path, "direct-coff-x64-exe");
const directAarch64ExePath = join(outDir, "direct-aarch64-exe-return");
rmSync(directAarch64ExePath, { force: true });
const directAarch64ExeReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf-aarch64", "--target", "linux-musl-arm64", "examples/direct-exe-return.graph", "--out", directAarch64ExePath]).body;
const directAarch64ExeBytes = readFileSync(directAarch64ExePath);
assert.equal(directAarch64ExeReport.emit, "exe");
assert.equal(directAarch64ExeReport.compiler, "zero-elf-aarch64");
assert.equal(directAarch64ExeReport.generatedCBytes, 0);
assert(directAarch64ExeReport.artifactBytes < 512);
assert.equal(directAarch64ExeReport.objectBackend.objectEmission.path, "direct-elf-aarch64-exe");
const directAarch64HelloPath = join(outDir, "direct-aarch64-hello");
rmSync(directAarch64HelloPath, { force: true });
const directAarch64HelloReport = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-arm64", "examples/hello.graph", "--out", directAarch64HelloPath]).body;
const directAarch64HelloBytes = readFileSync(directAarch64HelloPath);
assert.equal(directAarch64HelloReport.compiler, "zero-elf-aarch64");
assert.equal(directAarch64HelloReport.generatedCBytes, 0);
assert.equal(directAarch64HelloReport.objectBackend.objectEmission.path, "direct-elf-aarch64-exe");
assert.equal(directAarch64HelloReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert(directAarch64HelloBytes.includes(Buffer.from("hello from zero")));
assert(hasAarch64VoidReturnEpilogue(directAarch64HelloBytes));
const directAarch64VoidImplicitSource = join(outDir, "direct-aarch64-void-implicit.0");
const directAarch64VoidImplicitPath = join(outDir, "direct-aarch64-void-implicit");
writeProjectionFile(directAarch64VoidImplicitSource, `export c fn main() -> Void {
    let ok: Bool = true
}
`);
const directAarch64VoidImplicitGraph = importProjectionSidecar(directAarch64VoidImplicitSource);
rmSync(directAarch64VoidImplicitPath, { force: true });
const directAarch64VoidImplicitReport = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-arm64", directAarch64VoidImplicitGraph, "--out", directAarch64VoidImplicitPath]).body;
const directAarch64VoidImplicitBytes = readFileSync(directAarch64VoidImplicitPath);
assert.equal(directAarch64VoidImplicitReport.compiler, "zero-elf-aarch64");
assert.equal(directAarch64VoidImplicitReport.generatedCBytes, 0);
assert(hasAarch64VoidReturnEpilogue(directAarch64VoidImplicitBytes));
assert.equal(directAarch64ExeReport.objectBackend.targetFacts.status, "native-exe");
assert.equal(directAarch64ExeBytes.readUInt16LE(16), 2);
assert.equal(directAarch64ExeBytes.readUInt16LE(18), 183);
assert(directAarch64ExeBytes.includes(Buffer.from([0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6])));
assert(directAarch64ExeBytes.includes(Buffer.from([0xa8, 0x0b, 0x80, 0xd2, 0x01, 0x00, 0x00, 0xd4])));
const directCallObjPath = join(outDir, "direct-call-add.o");
rmSync(directCallObjPath, { force: true });
const directCallObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-call-add.graph", "--out", directCallObjPath]).body;
const directCallObjBytes = readFileSync(directCallObjPath);
assert.equal(directCallObjReport.emit, "obj");
assert.equal(directCallObjReport.generatedCBytes, 0);
assert.equal(directCallObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directCallObjBytes.readUInt16LE(16), 1);
assert.equal(directCallObjBytes.readUInt16LE(18), 62);
const directArrayObjPath = join(outDir, "direct-array-fill.o");
rmSync(directArrayObjPath, { force: true });
const directArrayObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/direct-array-fill.graph", "--out", directArrayObjPath]).body;
const directArrayObjBytes = readFileSync(directArrayObjPath);
assert.equal(directArrayObjReport.emit, "obj");
assert.equal(directArrayObjReport.generatedCBytes, 0);
assert(directArrayObjReport.objectBackend.directFacts.maxFrameBytes > 0);
assert.equal(directArrayObjReport.objectBackend.objectEmission.path, "direct-elf64-object");
assert.equal(directArrayObjBytes.readUInt16LE(16), 1);
assert.equal(directArrayObjBytes.readUInt16LE(18), 62);
const directArm64ObjPath = join(outDir, "direct-arm64-return.o");
rmSync(directArm64ObjPath, { force: true });
const directArm64ObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", "examples/direct-exe-return.graph", "--out", directArm64ObjPath]).body;
const directArm64ObjBytes = readFileSync(directArm64ObjPath);
assert.equal(directArm64ObjReport.emit, "obj");
assert.equal(directArm64ObjReport.compiler, "zero-elf-aarch64");
assert.equal(directArm64ObjReport.generatedCBytes, 0);
assert.equal(directArm64ObjReport.objectBackend.objectEmission.path, "direct-elf-aarch64-object");
assert.equal(directArm64ObjReport.objectBackend.targetFacts.status, "native-exe");
assert.equal(directArm64ObjBytes.readUInt16LE(16), 1);
assert.equal(directArm64ObjBytes.readUInt16LE(18), 183);
assert(directArm64ObjBytes.includes(Buffer.from([0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6])));
const directArm64IndexStoreSource = join(outDir, "direct-arm64-index-store-scratch.0");
const directArm64IndexStoreObjPath = join(outDir, "direct-arm64-index-store-scratch.o");
writeProjectionFile(directArm64IndexStoreSource, `export c fn main() -> u32 {
    var values: [4]u32 = [0, 0, 0, 0]
    values[5_u32 % 4_u32] = 7_u32
    return values[1]
}
`);
const directArm64IndexStoreGraph = importProjectionSidecar(directArm64IndexStoreSource);
rmSync(directArm64IndexStoreObjPath, { force: true });
const directArm64IndexStoreObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", directArm64IndexStoreGraph, "--out", directArm64IndexStoreObjPath]).body;
const directArm64IndexStoreObjBytes = readFileSync(directArm64IndexStoreObjPath);
assert.equal(directArm64IndexStoreObjReport.compiler, "zero-elf-aarch64");
assert.equal(directArm64IndexStoreObjReport.generatedCBytes, 0);
assert(hasAarch64Instruction(directArm64IndexStoreObjBytes, 0xb90003ea));
assert(hasAarch64Instruction(directArm64IndexStoreObjBytes, 0xb94003ea));
const directWhilePath = join(outDir, "direct-while-sum");
rmSync(directWhilePath, { force: true });
const directWhileReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-while-sum.graph", "--out", directWhilePath]).body;
const directWhileBytes = readFileSync(directWhilePath);
assert.equal(directWhileReport.emit, "exe");
assert.equal(directWhileReport.compiler, "zero-elf64");
assert.equal(directWhileReport.generatedCBytes, 0);
assert.equal(directWhileReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directWhileBytes.readUInt16LE(16), 2);
assert.equal(directWhileBytes.readUInt16LE(18), 62);
const directCallLoopPath = join(outDir, "direct-call-loop");
rmSync(directCallLoopPath, { force: true });
const directCallLoopReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-call-loop.graph", "--out", directCallLoopPath]).body;
const directCallLoopBytes = readFileSync(directCallLoopPath);
assert.equal(directCallLoopReport.emit, "exe");
assert.equal(directCallLoopReport.compiler, "zero-elf64");
assert.equal(directCallLoopReport.generatedCBytes, 0);
assert.equal(directCallLoopReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directCallLoopBytes.readUInt16LE(16), 2);
assert.equal(directCallLoopBytes.readUInt16LE(18), 62);
const directPackagePath = join(outDir, "direct-package-arrays");
rmSync(directPackagePath, { force: true });
const directPackageReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "examples/direct-package-arrays", "--out", directPackagePath]).body;
const directPackageBytes = readFileSync(directPackagePath);
assert.equal(directPackageReport.emit, "exe");
assert.equal(directPackageReport.compiler, "zero-elf64");
assert.equal(directPackageReport.generatedCBytes, 0);
assert.equal(directPackageReport.objectBackend.directFacts.moduleCount, 2);
assert.equal(directPackageReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert.equal(directPackageBytes.readUInt16LE(16), 2);
assert.equal(directPackageBytes.readUInt16LE(18), 62);
const directLinuxGnuObjPath = join(outDir, "direct-linux-gnu.o");
rmSync(directLinuxGnuObjPath, { force: true });
const directLinuxGnuObjReport = json(["build", "--json", "--emit", "obj", "--target", "linux-x64", "examples/direct-call-add.graph", "--out", directLinuxGnuObjPath]).body;
const directLinuxGnuObjBytes = readFileSync(directLinuxGnuObjPath);
assert.equal(directLinuxGnuObjReport.target, "linux-x64");
assert.equal(directLinuxGnuObjReport.compiler, "zero-elf64");
assert.equal(directLinuxGnuObjReport.generatedCBytes, 0);
assert.equal(directLinuxGnuObjReport.objectBackend.targetFacts.selectedEmitter, "zero-elf64");
assert.equal(directLinuxGnuObjBytes[0], 0x7f);
assert.equal(directLinuxGnuObjBytes[1], 0x45);
assert.equal(directLinuxGnuObjBytes.readUInt16LE(16), 1);
assert.equal(directLinuxGnuObjBytes.readUInt16LE(18), 62);
const directStringEqlTargets: Array<{ target: string; outName: string; compiler: string; emissionPath: string; magic: Buffer }> = [
  { target: "linux-musl-x64", outName: "direct-string-eql-linux-musl.o", compiler: "zero-elf64", emissionPath: "direct-elf64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-x64", outName: "direct-string-eql-linux-gnu.o", compiler: "zero-elf64", emissionPath: "direct-elf64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-arm64", outName: "direct-string-eql-linux-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-musl-arm64", outName: "direct-string-eql-linux-musl-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "darwin-arm64", outName: "direct-string-eql-darwin-arm64.o", compiler: "zero-macho64", emissionPath: "direct-macho64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "darwin-x64", outName: "direct-string-eql-darwin-x64.o", compiler: "zero-macho-x64", emissionPath: "direct-macho-x64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "win32-x64.exe", outName: "direct-string-eql-win-x64.obj", compiler: "zero-coff-x64", emissionPath: "direct-coff-x64-object", magic: Buffer.from([0x64, 0x86]) },
  { target: "win32-arm64.exe", outName: "direct-string-eql-win-arm64.obj", compiler: "zero-coff-aarch64", emissionPath: "direct-coff-aarch64-object", magic: Buffer.from([0x64, 0xaa]) },
];
for (const { target, outName, compiler, emissionPath, magic } of directStringEqlTargets) {
  const directStringEqlPath = join(outDir, outName);
  rmSync(directStringEqlPath, { force: true });
  const directStringEqlReport = json(["build", "--json", "--emit", "obj", "--target", target, "examples/direct-string-eql.graph", "--out", directStringEqlPath]).body;
  const directStringEqlBytes = readFileSync(directStringEqlPath);
  assert.equal(directStringEqlReport.compiler, compiler);
  assert.equal(directStringEqlReport.generatedCBytes, 0);
  assert.equal(directStringEqlReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStringEqlBytes.subarray(0, magic.length).equals(magic));
  assert(directStringEqlBytes.includes(Buffer.from("token")));
}
const directArm64DynamicStringEqlSource = join(outDir, "direct-arm64-dynamic-string-eql.0");
writeProjectionFile(directArm64DynamicStringEqlSource, `export c fn main() -> u8 {
    let text: String = "token"
    let start: usize = 0
    let end: usize = 5
    if std.mem.eqlBytes(text[(start % 1_usize)..end], "token"[(start % 1_usize)..end]) {
        return 1_u8
    }
    return 0_u8
}
`);
const directArm64DynamicStringEqlGraph = importProjectionSidecar(directArm64DynamicStringEqlSource);
for (const { target, outName, compiler, emissionPath, magic } of [
  { target: "linux-arm64", outName: "direct-dynamic-string-eql-linux-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "darwin-arm64", outName: "direct-dynamic-string-eql-darwin-arm64.o", compiler: "zero-macho64", emissionPath: "direct-macho64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "win32-arm64.exe", outName: "direct-dynamic-string-eql-win-arm64.obj", compiler: "zero-coff-aarch64", emissionPath: "direct-coff-aarch64-object", magic: Buffer.from([0x64, 0xaa]) },
]) {
  const directArm64DynamicStringEqlPath = join(outDir, outName);
  rmSync(directArm64DynamicStringEqlPath, { force: true });
  const directArm64DynamicStringEqlReport = json(["build", "--json", "--emit", "obj", "--target", target, directArm64DynamicStringEqlGraph, "--out", directArm64DynamicStringEqlPath]).body;
  const directArm64DynamicStringEqlBytes = readFileSync(directArm64DynamicStringEqlPath);
  assert.equal(directArm64DynamicStringEqlReport.compiler, compiler);
  assert.equal(directArm64DynamicStringEqlReport.generatedCBytes, 0);
  assert.equal(directArm64DynamicStringEqlReport.objectBackend.objectEmission.path, emissionPath);
  assert(directArm64DynamicStringEqlBytes.subarray(0, magic.length).equals(magic));
  assert(hasAarch64Instruction(directArm64DynamicStringEqlBytes, 0xb94003ea));
}
const directByteCopyFillTargets: Array<{ target: string; outName: string; compiler: string; emissionPath: string; magic: Buffer }> = [
  { target: "linux-musl-x64", outName: "direct-byte-copy-fill-linux-musl.o", compiler: "zero-elf64", emissionPath: "direct-elf64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-x64", outName: "direct-byte-copy-fill-linux-gnu.o", compiler: "zero-elf64", emissionPath: "direct-elf64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-arm64", outName: "direct-byte-copy-fill-linux-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "linux-musl-arm64", outName: "direct-byte-copy-fill-linux-musl-arm64.o", compiler: "zero-elf-aarch64", emissionPath: "direct-elf-aarch64-object", magic: Buffer.from([0x7f, 0x45, 0x4c, 0x46]) },
  { target: "darwin-arm64", outName: "direct-byte-copy-fill-darwin-arm64.o", compiler: "zero-macho64", emissionPath: "direct-macho64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "darwin-x64", outName: "direct-byte-copy-fill-darwin-x64.o", compiler: "zero-macho-x64", emissionPath: "direct-macho-x64-object", magic: Buffer.from([0xcf, 0xfa, 0xed, 0xfe]) },
  { target: "win32-x64.exe", outName: "direct-byte-copy-fill-win-x64.obj", compiler: "zero-coff-x64", emissionPath: "direct-coff-x64-object", magic: Buffer.from([0x64, 0x86]) },
  { target: "win32-arm64.exe", outName: "direct-byte-copy-fill-win-arm64.obj", compiler: "zero-coff-aarch64", emissionPath: "direct-coff-aarch64-object", magic: Buffer.from([0x64, 0xaa]) },
];
for (const { target, outName, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directByteCopyFillPath = join(outDir, outName);
  rmSync(directByteCopyFillPath, { force: true });
  const directByteCopyFillReport = json(["build", "--json", "--emit", "obj", "--target", target, "examples/direct-byte-copy-fill.graph", "--out", directByteCopyFillPath]).body;
  const directByteCopyFillBytes = readFileSync(directByteCopyFillPath);
  assert.equal(directByteCopyFillReport.compiler, compiler);
  assert.equal(directByteCopyFillReport.generatedCBytes, 0);
  assert.equal(directByteCopyFillReport.objectBackend.objectEmission.path, emissionPath);
  assert(directByteCopyFillBytes.subarray(0, magic.length).equals(magic));
  assert(directByteCopyFillBytes.includes(Buffer.from("token")));
}
const directStdPathSource = join(outDir, "direct-std-path-matrix.0");
writeProjectionFile(directStdPathSource, `export c fn main() -> u8 {
    var norm: [32]u8 = [0_u8; 32]
    let normalized: Maybe<String> = std.path.normalize(norm, "src//./main.0/")
    var parent: [32]u8 = [0_u8; 32]
    let parent_normalized: Maybe<String> = std.path.normalize(parent, "src/a/../main.0")
    var joined_buf: [32]u8 = [0_u8; 32]
    let joined: Maybe<String> = std.path.join(joined_buf, "src/", "/main.0")
    let root_expected: Span<u8> = "/x"[..1]
    var empty_abs_join_buf: [32]u8 = [0_u8; 32]
    let empty_abs_joined: Maybe<String> = std.path.join(empty_abs_join_buf, "", "/main.0")
    var rel: [32]u8 = [0_u8; 32]
    let relative: Maybe<String> = std.path.relative(rel, "src", "src/main.0")
    var fallback_buf: [32]u8 = [0_u8; 32]
    let fallback: Maybe<String> = std.path.relative(fallback_buf, "other", "src/main.0")
    var small: [3]u8 = [0_u8; 3]
    let overflow: Maybe<String> = std.path.normalize(small, "abcd")
    var root: [32]u8 = [0_u8; 32]
    let root_normalized: Maybe<String> = std.path.normalize(root, "/src//./main.0/")
    var root_parent_buf: [32]u8 = [0_u8; 32]
    let root_parent: Maybe<String> = std.path.normalize(root_parent_buf, "/../src")
    var leading_parent_buf: [32]u8 = [0_u8; 32]
    let leading_parent: Maybe<String> = std.path.normalize(leading_parent_buf, "../src")
    var nested_parent_buf: [32]u8 = [0_u8; 32]
    let nested_parent: Maybe<String> = std.path.normalize(nested_parent_buf, "a/../../b")
    var trim_fit_buf: [1]u8 = [0_u8; 1]
    let trim_fit: Maybe<String> = std.path.normalize(trim_fit_buf, "a/")
    var ok: Bool = true
    if !normalized.has {
        ok = false
    }
    if normalized.has {
        if !std.mem.eql(normalized.value, "src/main.0") {
            ok = false
        }
    }
    if !parent_normalized.has {
        ok = false
    }
    if parent_normalized.has {
        if !std.mem.eql(parent_normalized.value, "src/main.0") {
            ok = false
        }
    }
    if !joined.has {
        ok = false
    }
    if joined.has {
        if !std.mem.eql(joined.value, "src/main.0") {
            ok = false
        }
    }
    if !empty_abs_joined.has {
        ok = false
    }
    if empty_abs_joined.has {
        if !std.mem.eql(empty_abs_joined.value, "/main.0") {
            ok = false
        }
    }
    if !relative.has {
        ok = false
    }
    if relative.has {
        if !std.mem.eql(relative.value, "main.0") {
            ok = false
        }
    }
    if !fallback.has {
        ok = false
    }
    if fallback.has {
        if !std.mem.eql(fallback.value, "src/main.0") {
            ok = false
        }
    }
    if overflow.has {
        ok = false
    }
    if !root_normalized.has {
        ok = false
    }
    if root_normalized.has {
        if !std.mem.eql(root_normalized.value, "/src/main.0") {
            ok = false
        }
    }
    if !root_parent.has {
        ok = false
    }
    if root_parent.has {
        if !std.mem.eql(root_parent.value, "/src") {
            ok = false
        }
    }
    if !leading_parent.has {
        ok = false
    }
    if leading_parent.has {
        if !std.mem.eql(leading_parent.value, "../src") {
            ok = false
        }
    }
    if !nested_parent.has {
        ok = false
    }
    if nested_parent.has {
        if !std.mem.eql(nested_parent.value, "../b") {
            ok = false
        }
    }
    if !trim_fit.has {
        ok = false
    }
    if trim_fit.has {
        if !std.mem.eql(trim_fit.value, "a") {
            ok = false
        }
    }
    if normalized.has {
        if !std.mem.eql(std.path.basename(normalized.value), "main.0") {
            ok = false
        }
        if !std.mem.eql(std.path.dirname(normalized.value), "src") {
            ok = false
        }
    }
    if !std.mem.eql(std.path.dirname("/main.0"), root_expected) {
        ok = false
    }
    if !std.mem.eql(std.path.dirname(root_expected), root_expected) {
        ok = false
    }
    if normalized.has {
        if !std.mem.eql(std.path.extension(normalized.value), "0") {
            ok = false
        }
    }
    if ok {
        return 1_u8
    }
    return 0_u8
}
`);
const directStdPathGraph = importProjectionSidecar(directStdPathSource);
for (const { target, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directStdPathPath = join(outDir, `direct-std-path-${target.replace(/[^a-z0-9]+/gi, "-")}.o`);
  rmSync(directStdPathPath, { force: true });
  const directStdPathReport = json(["build", "--json", "--emit", "obj", "--target", target, directStdPathGraph, "--out", directStdPathPath]).body;
  const directStdPathBytes = readFileSync(directStdPathPath);
  assert.equal(directStdPathReport.compiler, compiler);
  assert.equal(directStdPathReport.generatedCBytes, 0);
  assert.equal(directStdPathReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStdPathBytes.subarray(0, magic.length).equals(magic));
  assert(directStdPathBytes.includes(Buffer.from("src/main.0")));
}
const directStdStrSource = join(outDir, "direct-std-str-matrix.0");
writeProjectionFile(directStdStrSource, `export c fn main() -> u8 {
    var reversed_buf: [6]u8 = [0_u8; 6]
    let reversed: Maybe<Span<u8>> = std.str.reverse(reversed_buf, "drawer")
    let trimmed: Span<u8> = std.str.trimAscii("  zero text  ")
    var small: [3]u8 = [0_u8; 3]
    let overflow: Maybe<Span<u8>> = std.str.reverse(small, "drawer")
    var ok: Bool = true
    if !reversed.has {
        ok = false
    }
    if reversed.has {
        if !std.mem.eql(reversed.value, "reward") {
            ok = false
        }
    }
    if overflow.has {
        ok = false
    }
    if std.str.countByte("banana", 97_u8) != 3 {
        ok = false
    }
    if !std.str.startsWith("zero text syntax", "zero") {
        ok = false
    }
    if !std.str.endsWith("zero text syntax", "syntax") {
        ok = false
    }
    if !std.str.contains("zero text syntax", "text") {
        ok = false
    }
    if std.str.contains("zero text syntax", "column") {
        ok = false
    }
    if !std.str.contains("zero", "") {
        ok = false
    }
    if !std.mem.eql(trimmed, "zero text") {
        ok = false
    }
    if std.str.wordCountAscii("zero text syntax") != 3 {
        ok = false
    }
    if ok {
        return 1_u8
    }
    return 0_u8
}
`);
const directStdStrGraph = importProjectionSidecar(directStdStrSource);
for (const { target, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directStdStrPath = join(outDir, `direct-std-str-${target.replace(/[^a-z0-9]+/gi, "-")}.o`);
  rmSync(directStdStrPath, { force: true });
  const directStdStrReport = json(["build", "--json", "--emit", "obj", "--target", target, directStdStrGraph, "--out", directStdStrPath]).body;
  const directStdStrBytes = readFileSync(directStdStrPath);
  assert.equal(directStdStrReport.compiler, compiler);
  assert.equal(directStdStrReport.generatedCBytes, 0);
  assert.equal(directStdStrReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStdStrBytes.subarray(0, magic.length).equals(magic));
  assert(directStdStrBytes.includes(Buffer.from("zero text syntax")));
}
const directStdMathSource = join(outDir, "direct-std-math-matrix.0");
writeProjectionFile(directStdMathSource, `export c fn main() -> u8 {
    var ok: Bool = true
    if std.math.minU32(8, 3) != 3 {
        ok = false
    }
    if std.math.minU32(4000000000_u32, 3) != 3 {
        ok = false
    }
    if std.math.minI32(0_i32 - 8_i32, 3) != 0_i32 - 8_i32 {
        ok = false
    }
    if std.math.minUsize(8_usize, 3_usize) != 3_usize {
        ok = false
    }
    if std.math.minI64(8_i64, 3_i64) != 3_i64 {
        ok = false
    }
    if std.math.minU64(8_u64, 3_u64) != 3_u64 {
        ok = false
    }
    if std.math.maxU32(8, 3) != 8 {
        ok = false
    }
    if std.math.maxU32(4000000000_u32, 3) != 4000000000_u32 {
        ok = false
    }
    if std.math.maxI32(0_i32 - 8_i32, 3) != 3 {
        ok = false
    }
    if std.math.maxUsize(8_usize, 3_usize) != 8_usize {
        ok = false
    }
    if std.math.maxI64(8_i64, 3_i64) != 8_i64 {
        ok = false
    }
    if std.math.maxU64(8_u64, 3_u64) != 8_u64 {
        ok = false
    }
    if std.math.clampU32(10, 2, 7) != 7 {
        ok = false
    }
    if std.math.clampU32(4000000000_u32, 2, 7) != 7 {
        ok = false
    }
    if std.math.clampU32(1, 7, 2) != 2 {
        ok = false
    }
    if std.math.clampI32(0_i32 - 10_i32, 0_i32 - 5_i32, 7) != 0_i32 - 5_i32 {
        ok = false
    }
    if std.math.clampUsize(1_usize, 7_usize, 2_usize) != 2_usize {
        ok = false
    }
    if std.math.clampI64(10_i64, 2_i64, 7_i64) != 7_i64 {
        ok = false
    }
    if std.math.clampU64(1_u64, 7_u64, 2_u64) != 2_u64 {
        ok = false
    }
    if std.math.absI32(0_i32 - 7_i32) != 7_u32 {
        ok = false
    }
    if std.math.absI64(0_i64 - 9_i64) != 9_u64 {
        ok = false
    }
    let checked_add_u32: Maybe<u32> = std.math.checkedAddU32(40_u32, 2_u32)
    let checked_sub_u32: Maybe<u32> = std.math.checkedSubU32(44_u32, 2_u32)
    let checked_mul_u32: Maybe<u32> = std.math.checkedMulU32(6_u32, 7_u32)
    let checked_add_usize: Maybe<usize> = std.math.checkedAddUsize(40_usize, 2_usize)
    let checked_sub_usize: Maybe<usize> = std.math.checkedSubUsize(44_usize, 2_usize)
    let checked_mul_usize: Maybe<usize> = std.math.checkedMulUsize(6_usize, 7_usize)
    let checked_add_i32: Maybe<i32> = std.math.checkedAddI32(40, 2)
    let checked_sub_i32: Maybe<i32> = std.math.checkedSubI32(40, 0_i32 - 2_i32)
    let checked_mul_i32: Maybe<i32> = std.math.checkedMulI32(6, 7)
    if !checked_add_u32.has || !checked_sub_u32.has || !checked_mul_u32.has || !checked_add_usize.has || !checked_sub_usize.has || !checked_mul_usize.has || !checked_add_i32.has || !checked_sub_i32.has || !checked_mul_i32.has {
        ok = false
    }
    let checked_add_u32_overflow: Maybe<u32> = std.math.checkedAddU32(4294967295_u32, 1_u32)
    let checked_sub_u32_overflow: Maybe<u32> = std.math.checkedSubU32(1_u32, 2_u32)
    let checked_mul_u32_overflow: Maybe<u32> = std.math.checkedMulU32(4000000000_u32, 2_u32)
    if checked_add_u32_overflow.has || checked_sub_u32_overflow.has || checked_mul_u32_overflow.has {
        ok = false
    }
    if std.math.saturatingAddU32(4294967295_u32, 1_u32) != 4294967295_u32 || std.math.saturatingSubU32(1_u32, 2_u32) != 0_u32 || std.math.saturatingMulU32(4000000000_u32, 2_u32) != 4294967295_u32 {
        ok = false
    }
    if std.math.saturatingAddUsize(18446744073709551615_usize, 1_usize) != 18446744073709551615_usize || std.math.saturatingSubUsize(1_usize, 2_usize) != 0_usize || std.math.saturatingMulUsize(18446744073709551615_usize, 2_usize) != 18446744073709551615_usize {
        ok = false
    }
    if std.math.saturatingAddI32(2147483647, 1) != 2147483647 || std.math.saturatingSubI32(0_i32 - 2147483647_i32 - 1_i32, 1) != 0_i32 - 2147483647_i32 - 1_i32 || std.math.saturatingMulI32(2147483647, 2) != 2147483647 {
        ok = false
    }
    if std.math.gcdU32(84, 30) != 6 {
        ok = false
    }
    if std.math.lcmU32(21, 6) != 42 {
        ok = false
    }
    let checked_lcm: Maybe<u32> = std.math.checkedLcmU32(21_u32, 6_u32)
    if !checked_lcm.has {
        ok = false
    }
    if std.math.powU32(3, 4) != 81 {
        ok = false
    }
    let checked_pow: Maybe<u32> = std.math.checkedPowU32(2_u32, 10_u32)
    if !checked_pow.has {
        ok = false
    }
    if std.math.modPowU32(4, 13, 497) != 445 {
        ok = false
    }
    if std.math.modPowU32(65536_u32, 2_u32, 4294967295_u32) != 1_u32 {
        ok = false
    }
    if std.math.modPowU32(9, 0, 1) != 0 {
        ok = false
    }
    if !std.math.isPrimeU32(31) {
        ok = false
    }
    if std.math.isPrimeU32(33) {
        ok = false
    }
    if !std.math.isEvenU32(42) || !std.math.isOddU32(41) {
        ok = false
    }
    if std.math.sqrtFloorU32(99) != 9 {
        ok = false
    }
    let factorial: Maybe<u32> = std.math.factorialU32(5_u32)
    let binomial: Maybe<u32> = std.math.binomialU32(5_u32, 2_u32)
    if !factorial.has || !binomial.has {
        ok = false
    }
    let larger_binomial: Maybe<u32> = std.math.binomialU32(31_u32, 15_u32)
    if !larger_binomial.has {
        ok = false
    } else {
        if larger_binomial.value != 300540195_u32 {
            ok = false
        }
    }
    if std.math.divisorCountU32(28) != 6 {
        ok = false
    }
    if std.math.properDivisorSumU32(28) != 28 {
        ok = false
    }
    if ok {
        return 1_u8
    }
    return 0_u8
}
`);
const directStdMathGraph = importProjectionSidecar(directStdMathSource);
for (const { target, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directStdMathPath = join(outDir, `direct-std-math-${target.replace(/[^a-z0-9]+/gi, "-")}.o`);
  rmSync(directStdMathPath, { force: true });
  const directStdMathReport = json(["build", "--json", "--emit", "obj", "--target", target, directStdMathGraph, "--out", directStdMathPath]).body;
  const directStdMathBytes = readFileSync(directStdMathPath);
  assert.equal(directStdMathReport.compiler, compiler);
  assert.equal(directStdMathReport.generatedCBytes, 0);
  assert.equal(directStdMathReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStdMathBytes.subarray(0, magic.length).equals(magic));
  if (compiler === "zero-coff-x64") {
    assert(directStdMathBytes.includes(Buffer.from("zero_math_op")));
    assert(directStdMathBytes.includes(Buffer.from("zero_math_usize_op")));
  }
}
const directStdTimeRandSource = join(outDir, "direct-std-time-rand-matrix.0");
writeProjectionFile(directStdTimeRandSource, `export c fn main() -> u8 {
    var ok: Bool = true
    let duration: Duration = std.time.add(std.time.us(2500_i64), std.time.ms(1))
    let window: Duration = std.time.add(std.time.minutes(1), std.time.seconds(30))
    let clamped: Duration = std.time.clamp(std.time.hours(2), std.time.seconds(1), std.time.minutes(10))
    let zero: Duration = std.time.zero()
    if std.time.asNs(std.time.ns(42_i64)) != 42_i64 {
        ok = false
    }
    if std.time.asUsFloor(std.time.ns(1999_i64)) != 1_i64 {
        ok = false
    }
    if std.time.asMsFloor(duration) != 3 {
        ok = false
    }
    if std.time.asSecondsFloor(window) != 90_i64 {
        ok = false
    }
    if std.time.asSecondsFloor(clamped) != 600_i64 {
        ok = false
    }
    let negative_tick: Duration = std.time.sub(std.time.zero(), std.time.ns(1_i64))
    if std.time.asUsFloor(negative_tick) != 0_i64 - 1_i64 {
        ok = false
    }
    if std.time.asMsFloor(negative_tick) != 0 - 1 {
        ok = false
    }
    if std.time.asSecondsFloor(negative_tick) != 0_i64 - 1_i64 {
        ok = false
    }
    if !std.time.lessThan(zero, duration) || !std.time.isZero(zero) {
        ok = false
    }
    if std.time.asSecondsFloor(std.time.max(duration, window)) != 90_i64 {
        ok = false
    }
    if std.time.asNs(std.time.min(duration, window)) != 3500000_i64 {
        ok = false
    }
    var rng: RandSource = std.rand.seed(7_u32)
    let first: u32 = std.rand.nextU32(&mut rng)
    let bit: Bool = std.rand.nextBool(&mut rng)
    let third: u32 = std.rand.nextU32(&mut rng)
    if first != 1025555898_u32 || !bit || third != 2630631676_u32 {
        ok = false
    }
    var bool_rng: RandSource = std.rand.seed(7_u32)
    let bit_one: Bool = std.rand.nextBool(&mut bool_rng)
    let bit_two: Bool = std.rand.nextBool(&mut bool_rng)
    let bit_three: Bool = std.rand.nextBool(&mut bool_rng)
    if bit_one || !bit_two || !bit_three {
        ok = false
    }
    if ok {
        return 1_u8
    }
    return 0_u8
}
`);
const directStdTimeRandGraph = importProjectionSidecar(directStdTimeRandSource);
for (const { target, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directStdTimeRandPath = join(outDir, `direct-std-time-rand-${target.replace(/[^a-z0-9]+/gi, "-")}.o`);
  rmSync(directStdTimeRandPath, { force: true });
  const directStdTimeRandReport = json(["build", "--json", "--emit", "obj", "--target", target, directStdTimeRandGraph, "--out", directStdTimeRandPath]).body;
  const directStdTimeRandBytes = readFileSync(directStdTimeRandPath);
  assert.equal(directStdTimeRandReport.compiler, compiler);
  assert.equal(directStdTimeRandReport.generatedCBytes, 0);
  assert.equal(directStdTimeRandReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStdTimeRandBytes.subarray(0, magic.length).equals(magic));
}
const directStdDataSource = join(outDir, "direct-std-codec-json-url-matrix.0");
writeProjectionFile(directStdDataSource, `export c fn main() -> u8 {
    var ok: Bool = true
    var b64_buf: [4]u8 = [0_u8; 4]
    let b64: Maybe<Span<u8>> = std.codec.base64Decode(b64_buf, "emVybw==")
    if !b64.has || !std.mem.eql(b64.value, "zero") {
        ok = false
    }
    var hex_buf: [2]u8 = [0_u8; 2]
    let hex: Maybe<Span<u8>> = std.codec.hexDecode(hex_buf, "417a")
    if !hex.has || !std.mem.eql(hex.value, "Az") {
        ok = false
    }
    var word_buf: [4]u8 = [0_u8; 4]
    let word: Maybe<Span<u8>> = std.codec.writeU32Be(word_buf, 0x41424344_u32)
    if !word.has {
        ok = false
    }
    if word.has {
        let read_word: Maybe<u32> = std.codec.readU32Be(word.value)
        if !read_word.has || read_word.value != 0x41424344_u32 {
            ok = false
        }
    }
    let json: Span<u8> = "{\\"name\\":\\"zero\\",\\"count\\":42,\\"ok\\":true}"
    var name_buf: [8]u8 = [0_u8; 8]
    let name: Maybe<Span<u8>> = std.json.string(name_buf, json, "name")
    let count: Maybe<u32> = std.json.u32(json, "count")
    let flag: Maybe<Bool> = std.json.bool(json, "ok")
    var json_out_buf: [16]u8 = [0_u8; 16]
    let json_out: Maybe<Span<u8>> = std.json.writeObject1U32(json_out_buf, "count", 42_u32)
    if !name.has || !std.mem.eql(name.value, "zero") || !count.has || count.value != 42_u32 || !flag.has || !flag.value || !json_out.has || std.json.validateError(json_out.value) != std.json.errorNone() {
        ok = false
    }
    var query_param_buf: [16]u8 = [0_u8; 16]
    let query_param: Maybe<Span<u8>> = std.url.writeQueryParam(query_param_buf, "q", "zero lang")
    var url_buf: [48]u8 = [0_u8; 48]
    var url: Maybe<Span<u8>> = null
    if query_param.has {
        url = std.url.appendQuery(url_buf, "https://example.com/path", query_param.value)
    }
    if !url.has || !std.mem.eql(url.value, "https://example.com/path?q=zero+lang") {
        ok = false
    }
    if ok {
        return 1_u8
    }
    return 0_u8
}
`);
const directStdDataGraph = importProjectionSidecar(directStdDataSource);
for (const { target, compiler, emissionPath, magic } of directByteCopyFillTargets) {
  const directStdDataPath = join(outDir, `direct-std-codec-json-url-${target.replace(/[^a-z0-9]+/gi, "-")}.o`);
  rmSync(directStdDataPath, { force: true });
  const directStdDataReport = json(["build", "--json", "--emit", "obj", "--target", target, directStdDataGraph, "--out", directStdDataPath]).body;
  const directStdDataBytes = readFileSync(directStdDataPath);
  assert.equal(directStdDataReport.compiler, compiler);
  assert.equal(directStdDataReport.generatedCBytes, 0);
  assert.equal(directStdDataReport.objectBackend.objectEmission.path, emissionPath);
  assert(directStdDataBytes.subarray(0, magic.length).equals(magic));
  assert(directStdDataBytes.includes(Buffer.from("zero+lang")));
}
const directMachOPath = join(outDir, "direct-darwin-arm64.o");
rmSync(directMachOPath, { force: true });
const directMachOReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/direct-call-add.graph", "--out", directMachOPath]).body;
const directMachOBytes = readFileSync(directMachOPath);
assert.equal(directMachOReport.compiler, "zero-macho64");
assert.equal(directMachOReport.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(directMachOReport.objectBackend.linking.objectFormat, "macho");
assert.equal(directMachOReport.objectBackend.directFacts.stackBytes, 544);
assert.equal(directMachOReport.objectBackend.directFacts.maxFrameBytes, 272);
assert.equal(directMachOBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOBytes.readUInt32LE(4), 0x0100000c);
assert.equal(directMachOBytes.readUInt32LE(12), 1);
assert(directMachOBytes.includes(Buffer.concat([Buffer.from("_main"), Buffer.from([0])])));
assert(directMachOBytes.includes(Buffer.concat([Buffer.from("_add"), Buffer.from([0])])));
assert(directMachOBytes.includes(Buffer.from([0x00, 0x01, 0x09, 0x0b])));
const directMachOSection = 32 + 72;
const directMachORelocOffset = directMachOBytes.readUInt32LE(directMachOSection + 56);
assert(directMachORelocOffset > 0);
assert(directMachOBytes.readUInt32LE(directMachOSection + 60) > 0);
assert.equal((directMachOBytes.readUInt32LE(directMachORelocOffset + 4) >>> 28) & 15, 2);
const directMachODataPath = join(outDir, "direct-darwin-arm64-data.o");
rmSync(directMachODataPath, { force: true });
const directMachODataReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/direct-byte-view-reloc.graph", "--out", directMachODataPath]).body;
const directMachODataBytes = readFileSync(directMachODataPath);
assert.equal(directMachODataReport.compiler, "zero-macho64");
assert.equal(directMachODataReport.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(directMachODataReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directMachODataReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert(directMachODataBytes.includes(Buffer.from("__const\0")));
assert(directMachODataBytes.includes(Buffer.from("l_.zero_rodata\0")));
assert(directMachODataBytes.includes(Buffer.from("token")));
const directMachODataSection = 32 + 72;
const directMachODataRelocOffset = directMachODataBytes.readUInt32LE(directMachODataSection + 56);
const directMachODataRelocCount = directMachODataBytes.readUInt32LE(directMachODataSection + 60);
assert(directMachODataRelocOffset > 0);
assert(directMachODataRelocCount > 0);
let sawMachOPageReloc = false;
let sawMachOPageoffReloc = false;
for (let i = 0; i < directMachODataRelocCount; i++) {
  const info = directMachODataBytes.readUInt32LE(directMachODataRelocOffset + i * 8 + 4);
  sawMachOPageReloc ||= ((info >>> 28) & 15) === 3;
  sawMachOPageoffReloc ||= ((info >>> 28) & 15) === 4;
}
assert.equal(sawMachOPageReloc, true);
assert.equal(sawMachOPageoffReloc, true);
const directMachOWorldPath = join(outDir, "direct-darwin-arm64-world.o");
rmSync(directMachOWorldPath, { force: true });
const directMachOWorldReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/hello.graph", "--out", directMachOWorldPath]).body;
const directMachOWorldBytes = readFileSync(directMachOWorldPath);
assert.equal(directMachOWorldReport.compiler, "zero-macho64");
assert.equal(directMachOWorldReport.generatedCBytes, 0);
assert.equal(directMachOWorldReport.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(directMachOWorldReport.objectBackend.objectEmission.symbolCount, 3);
assert.equal(directMachOWorldReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert.equal(directMachOWorldBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOWorldBytes.readUInt32LE(4), 0x0100000c);
assert.equal(directMachOWorldBytes.readUInt32LE(12), 1);
assert(directMachOWorldBytes.includes(Buffer.from("hello from zero")));
assert(directMachOWorldBytes.includes(Buffer.from("_zero_world_write")));
const directMachOWorldSection = 32 + 72;
const directMachOWorldRelocOffset = directMachOWorldBytes.readUInt32LE(directMachOWorldSection + 56);
const directMachOWorldRelocCount = directMachOWorldBytes.readUInt32LE(directMachOWorldSection + 60);
assert(directMachOWorldRelocOffset > 0);
assert(directMachOWorldRelocCount >= 2);
let sawMachOWorldBranchReloc = false;
for (let i = 0; i < directMachOWorldRelocCount; i++) {
  const info = directMachOWorldBytes.readUInt32LE(directMachOWorldRelocOffset + i * 8 + 4);
  sawMachOWorldBranchReloc ||= ((info >>> 28) & 15) === 2;
}
assert.equal(sawMachOWorldBranchReloc, true);
const directMachOX64WorldPath = join(outDir, "direct-darwin-x64-world.o");
rmSync(directMachOX64WorldPath, { force: true });
const directMachOX64WorldReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-x64", "examples/hello.graph", "--out", directMachOX64WorldPath]).body;
const directMachOX64WorldBytes = readFileSync(directMachOX64WorldPath);
assert.equal(directMachOX64WorldReport.compiler, "zero-macho-x64");
assert.equal(directMachOX64WorldReport.generatedCBytes, 0);
assert.equal(directMachOX64WorldReport.objectBackend.objectEmission.path, "direct-macho-x64-object");
assert.equal(directMachOX64WorldReport.objectBackend.objectEmission.symbolCount, 3);
assert.equal(directMachOX64WorldReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert.equal(directMachOX64WorldBytes.readUInt32LE(0), 0xfeedfacf);
assert.equal(directMachOX64WorldBytes.readUInt32LE(4), 0x01000007);
assert.equal(directMachOX64WorldBytes.readUInt32LE(8), 3);
assert.equal(directMachOX64WorldBytes.readUInt32LE(12), 1);
assert(directMachOX64WorldBytes.includes(Buffer.from("hello from zero")));
assert(directMachOX64WorldBytes.includes(Buffer.from("_zero_world_write")));
const directMachOX64WorldSection = 32 + 72;
const directMachOX64WorldRelocOffset = directMachOX64WorldBytes.readUInt32LE(directMachOX64WorldSection + 56);
const directMachOX64WorldRelocCount = directMachOX64WorldBytes.readUInt32LE(directMachOX64WorldSection + 60);
assert(directMachOX64WorldRelocOffset > 0);
assert(directMachOX64WorldRelocCount >= 2);
let sawMachOX64SignedReloc = false;
let sawMachOX64BranchReloc = false;
for (let i = 0; i < directMachOX64WorldRelocCount; i++) {
  const info = directMachOX64WorldBytes.readUInt32LE(directMachOX64WorldRelocOffset + i * 8 + 4);
  sawMachOX64SignedReloc ||= ((info >>> 28) & 15) === 1;
  sawMachOX64BranchReloc ||= ((info >>> 28) & 15) === 2;
}
assert.equal(sawMachOX64SignedReloc, true);
assert.equal(sawMachOX64BranchReloc, true);
const directCoffPath = join(outDir, "direct-win-x64.obj");
rmSync(directCoffPath, { force: true });
const directCoffReport = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", "examples/direct-call-add.graph", "--out", directCoffPath]).body;
const directCoffBytes = readFileSync(directCoffPath);
assert.equal(directCoffReport.compiler, "zero-coff-x64");
assert.equal(directCoffReport.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(directCoffReport.objectBackend.linking.objectFormat, "coff");
assert.equal(directCoffBytes.readUInt16LE(0), 0x8664);
assert.equal(directCoffBytes.readUInt16LE(2), 1);
assert(directCoffBytes.includes(Buffer.concat([Buffer.from("main"), Buffer.from([0])])));
assert(directCoffBytes.includes(Buffer.concat([Buffer.from("add"), Buffer.from([0])])));
assert(directCoffBytes.includes(Buffer.from([0xe8])));
const directCoffRelocOffset = directCoffBytes.readUInt32LE(20 + 24);
const directCoffRelocCount = directCoffBytes.readUInt16LE(20 + 32);
assert(directCoffRelocOffset > 0);
assert(directCoffRelocCount > 0);
assert.equal(directCoffBytes.readUInt16LE(directCoffRelocOffset + 8), 4);
const directCoffDataPath = join(outDir, "direct-win-x64-data.obj");
rmSync(directCoffDataPath, { force: true });
const directCoffDataReport = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", "examples/direct-byte-view-reloc.graph", "--out", directCoffDataPath]).body;
const directCoffDataBytes = readFileSync(directCoffDataPath);
assert.equal(directCoffDataReport.compiler, "zero-coff-x64");
assert.equal(directCoffDataReport.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(directCoffDataReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directCoffDataReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert.equal(directCoffDataBytes.readUInt16LE(0), 0x8664);
assert.equal(directCoffDataBytes.readUInt16LE(2), 2);
assert(directCoffDataBytes.includes(Buffer.from(".rdata\0")));
assert(directCoffDataBytes.includes(Buffer.from("token")));
const directCoffDataRelocOffset = directCoffDataBytes.readUInt32LE(20 + 24);
const directCoffDataRelocCount = directCoffDataBytes.readUInt16LE(20 + 32);
assert(directCoffDataRelocOffset > 0);
assert(directCoffDataRelocCount > 0);
let sawCoffAddr64Reloc = false;
for (let i = 0; i < directCoffDataRelocCount; i++) {
  sawCoffAddr64Reloc ||= directCoffDataBytes.readUInt16LE(directCoffDataRelocOffset + i * 10 + 8) === 1;
}
assert.equal(sawCoffAddr64Reloc, true);
const directCoffArm64DataPath = join(outDir, "direct-win-arm64-data.obj");
rmSync(directCoffArm64DataPath, { force: true });
const directCoffArm64DataReport = json(["build", "--json", "--emit", "obj", "--target", "win32-arm64.exe", "examples/direct-byte-view-reloc.graph", "--out", directCoffArm64DataPath]).body;
const directCoffArm64DataBytes = readFileSync(directCoffArm64DataPath);
assert.equal(directCoffArm64DataReport.compiler, "zero-coff-aarch64");
assert.equal(directCoffArm64DataReport.objectBackend.objectEmission.path, "direct-coff-aarch64-object");
assert.equal(directCoffArm64DataReport.objectBackend.objectEmission.dataSections, true);
assert.equal(directCoffArm64DataReport.objectBackend.directFacts.readonlyDataBytes, 6);
assert.equal(directCoffArm64DataBytes.readUInt16LE(0), 0xaa64);
assert.equal(directCoffArm64DataBytes.readUInt16LE(2), 2);
assert(directCoffArm64DataBytes.includes(Buffer.from(".rdata\0")));
assert(directCoffArm64DataBytes.includes(Buffer.from("token")));
const directCoffArm64DataRelocOffset = directCoffArm64DataBytes.readUInt32LE(20 + 24);
const directCoffArm64DataRelocCount = directCoffArm64DataBytes.readUInt16LE(20 + 32);
assert(directCoffArm64DataRelocOffset > 0);
assert(directCoffArm64DataRelocCount > 0);
let sawCoffArm64Addr64Reloc = false;
for (let i = 0; i < directCoffArm64DataRelocCount; i++) {
  sawCoffArm64Addr64Reloc ||= directCoffArm64DataBytes.readUInt16LE(directCoffArm64DataRelocOffset + i * 10 + 8) === 0x000e;
}
assert.equal(sawCoffArm64Addr64Reloc, true);
const directCoffWorldPath = join(outDir, "direct-win-x64-world.obj");
rmSync(directCoffWorldPath, { force: true });
const directCoffWorldReport = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", "examples/hello.graph", "--out", directCoffWorldPath]).body;
const directCoffWorldBytes = readFileSync(directCoffWorldPath);
assert.equal(directCoffWorldReport.compiler, "zero-coff-x64");
assert.equal(directCoffWorldReport.generatedCBytes, 0);
assert.equal(directCoffWorldReport.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(directCoffWorldReport.objectBackend.objectEmission.symbolCount, 4);
assert.equal(directCoffWorldReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert.equal(directCoffWorldBytes.readUInt16LE(0), 0x8664);
assert.equal(directCoffWorldBytes.readUInt16LE(2), 2);
assert(directCoffWorldBytes.includes(Buffer.from("hello from zero")));
assert(directCoffWorldBytes.includes(Buffer.from("zero_world_write")));
const directCoffWorldRelocOffset = directCoffWorldBytes.readUInt32LE(20 + 24);
const directCoffWorldRelocCount = directCoffWorldBytes.readUInt16LE(20 + 32);
assert(directCoffWorldRelocOffset > 0);
assert(directCoffWorldRelocCount >= 2);
let sawCoffWorldRel32Reloc = false;
for (let i = 0; i < directCoffWorldRelocCount; i++) {
  sawCoffWorldRel32Reloc ||= directCoffWorldBytes.readUInt16LE(directCoffWorldRelocOffset + i * 10 + 8) === 4;
}
assert.equal(sawCoffWorldRel32Reloc, true);
const directElfFsFallibleResourcesPath = join(outDir, "direct-std-fs-fallible-resources");
rmSync(directElfFsFallibleResourcesPath, { force: true });
const directElfFsFallibleResourcesReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "conformance/native/pass/std-fs-fallible-resources.graph", "--out", directElfFsFallibleResourcesPath]).body;
const directElfFsFallibleResourcesBytes = readFileSync(directElfFsFallibleResourcesPath);
assert.equal(directElfFsFallibleResourcesReport.generatedCBytes, 0);
assert.equal(directElfFsFallibleResourcesReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert(directElfFsFallibleResourcesBytes.includes(elfPackedErrorBytes(2)));
assert(directElfFsFallibleResourcesBytes.includes(elfPackedErrorBytes(4)));
const directElfFsFalliblePath = join(outDir, "direct-std-fs-fallible");
rmSync(directElfFsFalliblePath, { force: true });
const directElfFsFallibleReport = json(["build", "--json", "--emit", "exe", "--backend", "zero-elf64", "--target", "linux-musl-x64", "conformance/native/pass/std-fs-fallible.graph", "--out", directElfFsFalliblePath]).body;
const directElfFsFallibleBytes = readFileSync(directElfFsFalliblePath);
assert.equal(directElfFsFallibleReport.generatedCBytes, 0);
assert.equal(directElfFsFallibleReport.objectBackend.objectEmission.path, "direct-elf64-exe");
assert(directElfFsFallibleBytes.includes(elfPackedErrorBytes(2, true)));
assert(directElfFsFallibleBytes.includes(elfPackedErrorBytes(3, true)));
assert(directElfFsFallibleBytes.includes(elfPackedErrorBytes(4, true)));
const directArm64ElfPath = join(outDir, "direct-arm64.o");
rmSync(directArm64ElfPath, { force: true });
const directArm64ElfReport = json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", "examples/direct-call-add.graph", "--out", directArm64ElfPath]).body;
const directArm64ElfBytes = readFileSync(directArm64ElfPath);
assert.equal(directArm64ElfReport.compiler, "zero-elf-aarch64");
assert.equal(directArm64ElfReport.generatedCBytes, 0);
assert.equal(directArm64ElfReport.objectBackend.objectEmission.path, "direct-elf-aarch64-object");
assert(directArm64ElfReport.objectBackend.directFacts.functionCount >= 2);
assert(directArm64ElfBytes.subarray(0, 4).equals(Buffer.from([0x7f, 0x45, 0x4c, 0x46])));
assert(directArm64ElfBytes.includes(Buffer.from("main")));
const hostLeakGraph = json(["inspect", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package"]).body;
assert.equal(hostLeakGraph.cLibraries[0].targetValidation.status, "blocked");
assert.equal(hostLeakGraph.cLibraries[0].linkPlan.hostDiscovery, "blocked");
const hostLeakReadiness = json(["check", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package"]).body;
assert.equal(hostLeakReadiness.ok, true);
assert.equal(hostLeakReadiness.diagnostics.length, 0);
assert.equal(hostLeakReadiness.targetReadiness.ok, false);
assert.equal(hostLeakReadiness.targetReadiness.buildable, false);
assert.equal(hostLeakReadiness.targetReadiness.diagnostics[0].code, "CIMP003");
assert.match(hostLeakReadiness.targetReadiness.diagnostics[0].help, /target sysroot|vendored/);
const hostLeakBuild = json(["build", "--json", "--target", "linux-musl-x64", "conformance/c/host-leak-package", "--out", join(outDir, "host-leak-package")], { allowFailure: true });
assert.notEqual(hostLeakBuild.code, 0);
assert.equal(hostLeakBuild.body.diagnostics[0].code, "CIMP003");
const depGraph = json(["inspect", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app"]).body;
assert.equal(depGraph.package.name, "dep-app");
assert.equal(depGraph.package.resolver.deterministic, true);
assert(depGraph.package.dependencies.some((item) => item.name === "dep-lib" && item.status === "path-resolved"));
assert(depGraph.package.dependencies.some((item) => item.name === "remote-tools" && item.status === "registry-reference"));
assert.match(depGraph.package.lockfile.path, /\.zero\/package-locks\/[0-9a-f]+\.lock\.json/);
assert.match(depGraph.packageCache.cacheKeyInputs.dependencyGraphHash, /^[0-9a-f]{16}$/);
const depDoc = json(["doc", "--json", "conformance/packages/dep-app"]).body;
assert.equal(depDoc.package.name, "dep-app");
assert.equal(depDoc.publicationGate.requiresExamplesForPublicApi, true);
const depBuild = json(["build", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app", "--out", join(outDir, "dep-app")]).body;
assert.equal(depBuild.packageCache.invalidationReasons.includes("dependency graph changed"), true);
assert.equal(depBuild.compilerCaches.every((item) => item.compilerVersion === "0.3.4" && item.packageVersion === "0.1.0"), true);
const depDevTrace = json(["dev", "--json", "--trace", "--target", "linux-musl-x64", "conformance/packages/dep-app"]).body;
assert.equal(depDevTrace.trace.requested, true);
assert.equal(depDevTrace.partialDiagnostics.stable, true);
assert(depDevTrace.watch.files.some((item) => item.endsWith("src/main.0")));
assert.match(depDevTrace.watch.packageLocks[0], /\.zero\/package-locks\/[0-9a-f]+\.lock\.json/);
assert.equal(depDevTrace.interfaceFingerprints.algorithm, "fnv1a64-zero-interface-v1");
assert(depDevTrace.interfaceFingerprints.modules.some((item) => item.name === "main" && /^[0-9a-f]{16}$/.test(item.publicInterfaceHash)));
assert.equal(depDevTrace.incrementalInvalidation.partialDiagnosticsStable, true);
assert.equal(depDevTrace.incrementalInvalidation.interfaceFingerprints.importedPackageMetadataHash, depDevTrace.interfaceFingerprints.importedPackageMetadataHash);
const depTime = json(["time", "--json", "--target", "linux-musl-x64", "conformance/packages/dep-app"]).body;
assert.equal(depTime.interfaceFingerprints.algorithm, "fnv1a64-zero-interface-v1");
assert.match(depTime.interfaceFingerprints.targetFactsHash, /^[0-9a-f]{16}$/);
assert(depTime.cacheSummary.entries >= 5);
assert(depTime.incrementalInvalidation.changedInputs.sourceFiles.some((item) => item.endsWith("src/main.0")));
if (process.getuid?.() !== 0) {
  const unreadableDepRoot = join("/tmp", `zero-unreadable-dep-manifest-${process.pid}`);
  const unreadableDepManifest = join(unreadableDepRoot, "dep-lib", "zero.toml");
  rmSync(unreadableDepRoot, { force: true, recursive: true });
  cpSync("conformance/packages/dep-app", join(unreadableDepRoot, "dep-app"), { recursive: true });
  cpSync("conformance/packages/dep-lib", join(unreadableDepRoot, "dep-lib"), { recursive: true });
  rmSync(join(unreadableDepRoot, "dep-app", ".zero"), { force: true, recursive: true });
  rmSync(join(unreadableDepRoot, "dep-lib", ".zero"), { force: true, recursive: true });
  chmodSync(unreadableDepManifest, 0o000);
  const unreadableDepCheck = json(["check", "--json", join(unreadableDepRoot, "dep-app")], { allowFailure: true });
  assert.equal(unreadableDepCheck.code, 1);
  assert.equal(unreadableDepCheck.body.ok, false);
  assert.equal(unreadableDepCheck.body.diagnostics[0].code, "PAR100");
  assert.equal(unreadableDepCheck.body.diagnostics[0].path, unreadableDepManifest);
  assert.match(unreadableDepCheck.body.diagnostics[0].message, /failed to read '.*dep-lib\/zero\.toml'/);
  chmodSync(unreadableDepManifest, 0o644);
  rmSync(unreadableDepRoot, { force: true, recursive: true });
}
const zeroHashSize = json(["size", "--json", "--target", "linux-musl-x64", "examples/zero-hash", "--out", join(outDir, "zero-hash-sized")]).body;
assert.equal(zeroHashSize.generatedCBytes, 0);
assert(zeroHashSize.usedStdlibHelpers.some((helper) => helper.name === "std.codec.crc32Bytes"));
assert(zeroHashSize.artifactBytes < 100 * 1024);
const invalidCheckEmit = json(["check", "--json", "--emit", "bogus", "examples/hello.graph"], { allowFailure: true });
assert.equal(invalidCheckEmit.code, 1);
assert.equal(invalidCheckEmit.body.ok, false);
assert.equal(invalidCheckEmit.body.diagnostics[0].code, "BLD002");
assert.equal(invalidCheckEmit.body.diagnostics[0].actual, "--emit bogus");
assert.equal(invalidCheckEmit.body.targetReadiness, undefined);
const unknownBackend = json(["check", "--json", "--backend", "bogus", "examples/hello.graph"], { allowFailure: true });
assert.equal(unknownBackend.code, 1);
assert.equal(unknownBackend.body.diagnostics[0].code, "BLD002");
assert.equal(unknownBackend.body.diagnostics[0].actual, "--backend bogus");
assert.match(unknownBackend.body.diagnostics[0].expected, /direct, llvm/);
const unknownBackendWithLlvmEmit = json(["check", "--json", "--emit", "llvm-ir", "--backend", "bogus", "examples/hello.graph"], { allowFailure: true });
assert.equal(unknownBackendWithLlvmEmit.code, 1);
assert.equal(unknownBackendWithLlvmEmit.body.diagnostics[0].code, "BLD002");
assert.equal(unknownBackendWithLlvmEmit.body.diagnostics[0].actual, "--backend bogus");
const llvmReadiness = json(["check", "--json", "--backend", "llvm", "examples/add.graph"]).body;
assert.equal(llvmReadiness.ok, true);
const llvmHostReady = llvmReadiness.targetReadiness.ok;
assert.equal(llvmReadiness.targetReadiness.backend, "llvm");
assert.equal(llvmReadiness.targetReadiness.emit, "exe");
assert.equal(llvmReadiness.targetReadiness.backendLifecycle.stability, "experimental");
assert.equal(llvmReadiness.targetReadiness.backendLifecycle.defaultEligible, false);
assert.equal(llvmReadiness.targetReadiness.backendLifecycle.releaseEligible, false);
if (llvmHostReady) {
  assert.equal(llvmReadiness.targetReadiness.stage, "ready");
  assert.equal(llvmReadiness.targetReadiness.diagnostics.length, 0);
} else {
  assert.equal(llvmReadiness.targetReadiness.diagnostics[0].code, "BLD004");
  assert.equal(llvmReadiness.targetReadiness.diagnostics[0].backendBlocker.backend, "llvm");
  assert(["toolchain", "target-selection"].includes(llvmReadiness.targetReadiness.stage));
}
const llvmMissingToolReadiness = json(["check", "--json", "--backend", "llvm", "examples/add.graph"], { env: { ZERO_LLVM_CLANG: "/tmp/zero-missing-clang" } }).body;
assert.equal(llvmMissingToolReadiness.ok, true);
assert.equal(llvmMissingToolReadiness.targetReadiness.ok, false);
assert.equal(llvmMissingToolReadiness.targetReadiness.backend, "llvm");
assert.equal(llvmMissingToolReadiness.targetReadiness.stage, "toolchain");
assert.equal(llvmMissingToolReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(llvmMissingToolReadiness.targetReadiness.diagnostics[0].backendBlocker.unsupportedFeature, "clang");
const llvmNonExecutableToolPath = join(outDir, "not-executable-clang");
writeFileSync(llvmNonExecutableToolPath, "");
chmodSync(llvmNonExecutableToolPath, 0o644);
const llvmNonExecutableToolReadiness = json(["check", "--json", "--backend", "llvm", "examples/add.graph"], { env: { ZERO_LLVM_CLANG: llvmNonExecutableToolPath } }).body;
assert.equal(llvmNonExecutableToolReadiness.ok, true);
assert.equal(llvmNonExecutableToolReadiness.targetReadiness.ok, false);
assert.equal(llvmNonExecutableToolReadiness.targetReadiness.backend, "llvm");
assert.equal(llvmNonExecutableToolReadiness.targetReadiness.stage, "toolchain");
assert.equal(llvmNonExecutableToolReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.match(llvmNonExecutableToolReadiness.targetReadiness.diagnostics[0].actual, /not executable or not found/);
assert.equal(llvmNonExecutableToolReadiness.targetReadiness.diagnostics[0].backendBlocker.unsupportedFeature, "clang");
const llvmIrReadiness = json(["check", "--json", "--emit", "llvm-ir", "--backend", "llvm", "examples/add.graph"]).body;
assert.equal(llvmIrReadiness.ok, true);
assert.equal(llvmIrReadiness.targetReadiness.ok, true);
assert.equal(llvmIrReadiness.targetReadiness.backend, "llvm");
assert.equal(llvmIrReadiness.targetReadiness.emit, "llvm-ir");
assert.equal(llvmIrReadiness.targetReadiness.stage, "ready");
assert.equal(llvmIrReadiness.targetReadiness.diagnostics.length, 0);
const llvmPracticalSubsetExpected = "LLVM IR scalar, fixed-array, and byte-view MIR subset";
for (const fixture of [
  "examples/direct-array-sum.graph",
  "examples/direct-string-len.graph",
  "examples/direct-byte-copy-fill.graph",
  "examples/direct-string-eql.graph",
  "examples/direct-byte-view-locals.graph",
  "examples/direct-span-read.graph",
]) {
  const readiness = json(["check", "--json", "--emit", "llvm-ir", "--backend", "llvm", fixture]).body;
  assert.equal(readiness.ok, true);
  assert.equal(readiness.targetReadiness.ok, true);
  assert.equal(readiness.targetReadiness.backend, "llvm");
  assert.equal(readiness.targetReadiness.stage, "ready");
  assert.equal(readiness.targetReadiness.diagnostics.length, 0);
}
const llvmIrLoweringBlockedReadiness = json(["check", "--json", "--emit", "llvm-ir", "--backend", "llvm", "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.graph"]).body;
assert.equal(llvmIrLoweringBlockedReadiness.ok, true);
assert.equal(llvmIrLoweringBlockedReadiness.targetReadiness.ok, false);
assert.equal(llvmIrLoweringBlockedReadiness.targetReadiness.backend, "llvm");
assert.equal(llvmIrLoweringBlockedReadiness.targetReadiness.stage, "lower");
assert.equal(llvmIrLoweringBlockedReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(llvmIrLoweringBlockedReadiness.targetReadiness.diagnostics[0].expected, "typed program graph MIR subset");
assert.equal(llvmIrLoweringBlockedReadiness.targetReadiness.diagnostics[0].actual, "owned<Tracked>");
assert.deepEqual(llvmIrLoweringBlockedReadiness.targetReadiness.diagnostics[0].backendBlocker, {
  target: version.host,
  objectFormat: version.host.startsWith("win32") ? "coff" : (version.host.startsWith("linux") ? "elf" : "macho"),
  backend: "llvm",
  stage: "lower",
  unsupportedFeature: "owned<Tracked>",
});
const llvmNativeLoweringBlockedReadiness = json(["check", "--json", "--backend", "llvm", "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.graph"]).body;
assert.equal(llvmNativeLoweringBlockedReadiness.ok, true);
assert.equal(llvmNativeLoweringBlockedReadiness.targetReadiness.ok, false);
assert.equal(llvmNativeLoweringBlockedReadiness.targetReadiness.backend, "llvm");
assert.equal(llvmNativeLoweringBlockedReadiness.targetReadiness.stage, "lower");
assert.equal(llvmNativeLoweringBlockedReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(llvmNativeLoweringBlockedReadiness.targetReadiness.diagnostics[0].expected, "typed program graph MIR subset");
assert.equal(llvmNativeLoweringBlockedReadiness.targetReadiness.diagnostics[0].actual, "owned<Tracked>");
assert.doesNotMatch(llvmNativeLoweringBlockedReadiness.targetReadiness.diagnostics[0].message, /direct backend/);
const llvmBuild = json(["build", "--json", "--backend", "llvm", "examples/add.graph", "--out", join(outDir, "add-llvm")], { allowFailure: !llvmHostReady });
if (llvmHostReady) {
  assert.equal(llvmBuild.code, 0);
  assert.equal(llvmBuild.body.emit, "exe");
  assert.equal(llvmBuild.body.compiler, llvmBuild.body.toolchain.compiler);
  assert.equal(llvmBuild.body.toolchain.driverKind, "clang");
  assert.equal(llvmBuild.body.toolchain.status, "ready");
  assert.equal(llvmBuild.body.toolchain.backendLifecycle.releaseEligible, false);
  assert.equal(llvmBuild.body.releaseTargetContract.backendFamily, "llvm");
  assert.equal(llvmBuild.body.releaseTargetContract.backendLifecycle.releaseEligible, false);
  assert.equal(llvmBuild.body.releaseTargetContract.artifactKind, "native-executable");
  assert.equal(llvmBuild.body.releaseTargetContract.selectedEmitter, "llvm-clang-exe");
  assert.equal(llvmBuild.body.releaseTargetContract.fallbackPolicy, "none");
  assert.equal(llvmBuild.body.releaseTargetContract.readiness.llvmArtifact, true);
  assert.equal(llvmBuild.body.releaseTargetContract.readiness.releaseEligible, false);
  assert.equal(llvmBuild.body.releaseTargetContract.determinism.reproducible, false);
  assert.equal(llvmBuild.body.releaseTargetContract.determinism.repeatBuildHash, "external-toolchain-not-claimed");
  assert.equal(llvmBuild.body.objectBackend.backendFamily, "llvm");
  assert.equal(llvmBuild.body.objectBackend.backendLifecycle.releaseEligible, false);
  assert.equal(llvmBuild.body.objectBackend.targetFacts.status, "ready");
  assert.equal(llvmBuild.body.objectBackend.linking.externalToolchain, llvmBuild.body.toolchain.compiler);
  assert.equal(llvmBuild.body.objectBackend.linking.toolchainSource, "llvm-ir-clang-link-plan");
  assert.deepEqual(llvmBuild.body.objectBackend.linkerPlan.staticLibraries, ["zero_runtime.o"]);
  assert.equal(llvmBuild.body.objectBackend.linkerPlan.reproducible, false);
  assert.equal(llvmBuild.body.targetSupport.backendFamily, "llvm");
  assert.equal(llvmBuild.body.targetSupport.fallbackPolicy, "none");
  assert.equal(llvmBuild.body.targetSupport.backendLifecycle.releaseEligible, false);
  assert(llvmBuild.body.artifactBytes > 0);
  assert.equal(execFileSync(llvmBuild.body.artifactPath, [], { encoding: "utf8" }), "math works\n");
} else {
  assert.notEqual(llvmBuild.code, 0);
  assert.equal(llvmBuild.body.diagnostics[0].code, "BLD004");
  assert.equal(llvmBuild.body.diagnostics[0].backendBlocker.backend, "llvm");
}
const llvmMissingToolBuild = json(["build", "--json", "--backend", "llvm", "examples/add.graph", "--out", join(outDir, "add-llvm-missing")], { allowFailure: true, env: { ZERO_LLVM_CLANG: "/tmp/zero-missing-clang" } });
assert.notEqual(llvmMissingToolBuild.code, 0);
assert.equal(llvmMissingToolBuild.body.diagnostics[0].code, "BLD004");
assert.equal(llvmMissingToolBuild.body.diagnostics[0].backendBlocker.stage, "toolchain");
assert.equal(llvmMissingToolBuild.body.diagnostics[0].backendBlocker.unsupportedFeature, "clang");
const llvmNativeLoweringBlockedBuild = json(["build", "--json", "--backend", "llvm", "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.graph", "--out", join(outDir, "owned-drop-llvm")], { allowFailure: true });
assert.notEqual(llvmNativeLoweringBlockedBuild.code, 0);
assert.equal(llvmNativeLoweringBlockedBuild.body.diagnostics[0].code, "BLD004");
assert.equal(llvmNativeLoweringBlockedBuild.body.diagnostics[0].expected, "typed program graph MIR subset");
assert.equal(llvmNativeLoweringBlockedBuild.body.diagnostics[0].actual, "owned<Tracked>");
assert.doesNotMatch(llvmNativeLoweringBlockedBuild.body.diagnostics[0].message, /direct backend/);
assert.equal(llvmNativeLoweringBlockedBuild.body.diagnostics[0].backendBlocker.backend, directExeEmitterForTarget(version.host));
assert.equal(llvmNativeLoweringBlockedBuild.body.diagnostics[0].backendBlocker.stage, "lower");
const llvmFailingToolPath = join(outDir, "failing-clang");
writeFileSync(llvmFailingToolPath, "#!/bin/sh\nexit 1\n");
chmodSync(llvmFailingToolPath, 0o755);
const llvmFailingToolRuntimeBuild = json(["build", "--json", "--backend", "llvm", "examples/hello.graph", "--out", join(outDir, "hello-llvm-failing-tool")], { allowFailure: true, env: { ZERO_LLVM_CLANG: llvmFailingToolPath } });
assert.notEqual(llvmFailingToolRuntimeBuild.code, 0);
assert.equal(llvmFailingToolRuntimeBuild.body.diagnostics[0].code, "BLD004");
assert.match(llvmFailingToolRuntimeBuild.body.diagnostics[0].message, /LLVM runtime object build failed/);
assert.equal(llvmFailingToolRuntimeBuild.body.diagnostics[0].backendBlocker.backend, "llvm");
assert.equal(llvmFailingToolRuntimeBuild.body.diagnostics[0].backendBlocker.stage, "toolchain");
assert.equal(llvmFailingToolRuntimeBuild.body.diagnostics[0].backendBlocker.unsupportedFeature, "clang");
const llvmFailingToolLinkBuild = json(["build", "--json", "--backend", "llvm", "examples/direct-exe-return.graph", "--out", join(outDir, "return-llvm-failing-tool")], { allowFailure: true, env: { ZERO_LLVM_CLANG: llvmFailingToolPath } });
assert.notEqual(llvmFailingToolLinkBuild.code, 0);
assert.equal(llvmFailingToolLinkBuild.body.diagnostics[0].code, "BLD004");
assert.match(llvmFailingToolLinkBuild.body.diagnostics[0].message, /LLVM executable link failed/);
assert.equal(llvmFailingToolLinkBuild.body.diagnostics[0].backendBlocker.backend, "llvm");
assert.equal(llvmFailingToolLinkBuild.body.diagnostics[0].backendBlocker.stage, "toolchain");
assert.equal(llvmFailingToolLinkBuild.body.diagnostics[0].backendBlocker.unsupportedFeature, "clang");
const llvmNoOutputToolPath = join(outDir, "no-output-clang");
const llvmNoOutputArtifactPath = join(outDir, "return-llvm-no-output-tool");
writeFileSync(llvmNoOutputToolPath, "#!/bin/sh\nexit 0\n");
chmodSync(llvmNoOutputToolPath, 0o755);
writeFileSync(llvmNoOutputArtifactPath, "stale executable\n");
const llvmNoOutputToolBuild = json(["build", "--json", "--backend", "llvm", "examples/direct-exe-return.graph", "--out", llvmNoOutputArtifactPath], { allowFailure: true, env: { ZERO_LLVM_CLANG: llvmNoOutputToolPath } });
assert.notEqual(llvmNoOutputToolBuild.code, 0);
assert.equal(llvmNoOutputToolBuild.body.diagnostics[0].code, "BLD004");
assert.match(llvmNoOutputToolBuild.body.diagnostics[0].message, /LLVM executable link failed/);
assert.equal(llvmNoOutputToolBuild.body.diagnostics[0].backendBlocker.backend, "llvm");
assert.equal(llvmNoOutputToolBuild.body.diagnostics[0].backendBlocker.stage, "toolchain");
assert.equal(llvmNoOutputToolBuild.body.diagnostics[0].backendBlocker.unsupportedFeature, "clang");
assert.equal(existsSync(llvmNoOutputArtifactPath), false);
const llvmIrBuild = json(["build", "--json", "--emit", "llvm-ir", "examples/add.graph", "--out", join(outDir, "add.ll")], { allowFailure: true });
assert.notEqual(llvmIrBuild.code, 0);
assert.equal(llvmIrBuild.body.diagnostics[0].code, "BLD004");
assert.equal(llvmIrBuild.body.diagnostics[0].actual, "backend=direct emit=llvm-ir");
const explicitLlvmIrBuild = json(["build", "--json", "--emit", "llvm-ir", "--backend", "llvm", "examples/add.graph", "--out", join(outDir, "add-explicit.ll")]);
assert.equal(explicitLlvmIrBuild.code, 0);
assert.equal(explicitLlvmIrBuild.body.emit, "llvm-ir");
assert.equal(explicitLlvmIrBuild.body.compiler, "zero-c-llvm-ir");
assert.equal(explicitLlvmIrBuild.body.generatedCBytes, 0);
assert.equal(explicitLlvmIrBuild.body.toolchain.driverKind, "none");
assert.equal(explicitLlvmIrBuild.body.toolchain.selectionSource, "not-required");
assert.equal(explicitLlvmIrBuild.body.targetSupport.backendFamily, "llvm");
assert.equal(explicitLlvmIrBuild.body.targetSupport.llvmStatus, "ir-only");
assert.equal(explicitLlvmIrBuild.body.targetSupport.backendLifecycle.releaseEligible, false);
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.backendFamily, "llvm");
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.backendLifecycle.releaseEligible, false);
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.artifactKind, "llvm-ir");
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.selectedEmitter, "llvm-ir");
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.linkerFlavor, "none");
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.targetLinker, "none");
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.fallbackPolicy, "none");
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.libc.artifactMode, "none");
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.sysroot.requiredByArtifact, false);
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.readiness.status, "supported");
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.readiness.directArtifact, false);
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.readiness.llvmArtifact, true);
assert.equal(explicitLlvmIrBuild.body.releaseTargetContract.readiness.releaseEligible, false);
assert.equal(explicitLlvmIrBuild.body.objectBackend.backendFamily, "llvm");
assert.equal(explicitLlvmIrBuild.body.objectBackend.backendLifecycle.releaseEligible, false);
assert.equal(explicitLlvmIrBuild.body.objectBackend.targetFacts.status, "ir-only");
assert.equal(explicitLlvmIrBuild.body.objectBackend.linking.targetLibraries, "zero-runtime");
assert.equal(explicitLlvmIrBuild.body.objectBackend.linking.externalToolchain, "none");
assert.equal(explicitLlvmIrBuild.body.objectBackend.linking.toolchainSource, "textual-llvm-ir-runtime-link-plan");
assert.deepEqual(explicitLlvmIrBuild.body.objectBackend.linkerPlan.staticLibraries, ["zero_runtime.o"]);
assert.match(explicitLlvmIrBuild.body.objectBackend.targetFacts.reason, /native host executable output/);
assert.equal(explicitLlvmIrBuild.body.artifactPath, join(outDir, "add-explicit.ll"));
assert(explicitLlvmIrBuild.body.artifactBytes > 0);
const explicitLlvmIrText = readFileSync(explicitLlvmIrBuild.body.artifactPath, "utf8");
assert.match(explicitLlvmIrText, /^; zero llvm-ir v1\n/);
assert.match(explicitLlvmIrText, /target triple = "/);
assert.match(explicitLlvmIrText, /define i32 @main\(\)/);
assert.match(explicitLlvmIrText, /declare i32 @zero_world_write\(i32, ptr, i32\)/);
assert.match(explicitLlvmIrText, /declare void @llvm\.trap\(\)/);
assert.match(explicitLlvmIrText, /call i32 @zero_world_write\(i32 1, ptr %v[0-9]+, i32 (?:11|%v[0-9]+)\)/);
assert.match(explicitLlvmIrText, /%v[0-9]+ = call i32 @zero_world_write\(i32 1, ptr %v[0-9]+, i32 (?:11|%v[0-9]+)\)\n  %v[0-9]+ = icmp eq i32 %v[0-9]+, 0\n  br i1 %v[0-9]+, label %L[0-9]+, label %L[0-9]+\nL[0-9]+:\n  call void @llvm\.trap\(\)\n  unreachable\nL[0-9]+:/);
const llvmIrLoweringBlockedBuild = json(["build", "--json", "--emit", "llvm-ir", "--backend", "llvm", "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.graph", "--out", join(outDir, "owned-drop.ll")], { allowFailure: true });
assert.notEqual(llvmIrLoweringBlockedBuild.code, 0);
assert.equal(llvmIrLoweringBlockedBuild.body.diagnostics[0].code, "BLD004");
assert.equal(llvmIrLoweringBlockedBuild.body.diagnostics[0].expected, "typed program graph MIR subset");
assert.equal(llvmIrLoweringBlockedBuild.body.diagnostics[0].actual, "owned<Tracked>");
assert.equal(llvmIrLoweringBlockedBuild.body.diagnostics[0].backendBlocker.backend, directExeEmitterForTarget(version.host));
assert.equal(llvmIrLoweringBlockedBuild.body.diagnostics[0].backendBlocker.stage, "lower");
assert.equal(llvmIrLoweringBlockedBuild.body.diagnostics[0].backendBlocker.unsupportedFeature, "owned<Tracked>");
const directLlvmIrReadiness = json(["check", "--json", "--emit", "llvm-ir", "--backend", "direct", "examples/add.graph"]).body;
assert.equal(directLlvmIrReadiness.ok, true);
assert.equal(directLlvmIrReadiness.targetReadiness.ok, false);
assert.equal(directLlvmIrReadiness.targetReadiness.diagnostics[0].actual, "backend=direct emit=llvm-ir");
assert.equal(directLlvmIrReadiness.targetReadiness.diagnostics[0].backendBlocker.backend, "direct");
assert.equal(directLlvmIrReadiness.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
const directLlvmIrBuild = json(["build", "--json", "--emit", "llvm-ir", "--backend", "direct", "examples/add.graph", "--out", join(outDir, "add-direct.ll")], { allowFailure: true });
assert.notEqual(directLlvmIrBuild.code, 0);
assert.equal(directLlvmIrBuild.body.diagnostics[0].actual, "backend=direct emit=llvm-ir");
const llvmGraphCheck = json(["check", "--json", "--backend", "llvm", "examples/add.graph"]).body;
assert.equal(llvmGraphCheck.ok, true);
assert.equal(llvmGraphCheck.targetReadiness.ok, llvmHostReady);
assert.equal(llvmGraphCheck.targetReadiness.backend, "llvm");
if (llvmHostReady) {
  assert.equal(llvmGraphCheck.targetReadiness.stage, "ready");
  assert.equal(llvmGraphCheck.targetReadiness.diagnostics.length, 0);
} else {
  assert.equal(llvmGraphCheck.targetReadiness.diagnostics[0].backendBlocker.backend, "llvm");
}
const llvmIrGraphCheck = json(["check", "--json", "--emit", "llvm-ir", "--backend", "llvm", "examples/add.graph"]).body;
assert.equal(llvmIrGraphCheck.ok, true);
assert.equal(llvmIrGraphCheck.targetReadiness.ok, true);
assert.equal(llvmIrGraphCheck.targetReadiness.backend, "llvm");
assert.equal(llvmIrGraphCheck.targetReadiness.stage, "ready");
const directLlvmIrGraphCheck = json(["check", "--json", "--emit", "llvm-ir", "--backend", "direct", "examples/add.graph"]).body;
assert.equal(directLlvmIrGraphCheck.ok, true);
assert.equal(directLlvmIrGraphCheck.targetReadiness.ok, false);
assert.equal(directLlvmIrGraphCheck.targetReadiness.diagnostics[0].backendBlocker.backend, "direct");
const llvmSize = json(["size", "--json", "--backend", "llvm", "examples/add.graph"]);
assert.equal(llvmSize.body.targetSupport.backendFamily, "llvm");
assert.equal(llvmSize.body.targetSupport.fallbackPolicy, "none");
assert.equal(llvmSize.body.targetSupport.backendLifecycle.releaseEligible, false);
assert.equal(llvmSize.body.backendProfile.backendFamily, "llvm");
assert.equal(llvmSize.body.backendProfile.backendLifecycle.releaseEligible, false);
assert.equal(llvmSize.body.backendProfile.selectedEmitter, "llvm-clang-exe");
assert.equal(llvmSize.body.backendProfile.targetTriple, llvmSize.body.targetSupport.llvmTargetTriple);
assert.equal(llvmSize.body.backendProfile.optimizationLevel, "-Oz");
assert.equal(llvmSize.body.backendProfile.retained.functionCount + llvmSize.body.backendProfile.retained.runtimeHelperCount, llvmSize.body.objectBackend.objectEmission.symbolCount);
assert.equal(llvmSize.body.objectBackend.backendFamily, "llvm");
assert.equal(llvmSize.body.objectBackend.backendLifecycle.releaseEligible, false);
assert.equal(llvmSize.body.objectBackend.targetFacts.fallbackPolicy, "none");
assert.equal(llvmSize.body.backendComparison.rows.length, 4);
assert(llvmSize.body.backendComparison.rows.some((row) => row.id === "direct-debug" && row.backendFamily === "direct"));
assert(llvmSize.body.backendComparison.rows.some((row) => row.id === "llvm-no-opt" && row.backendFamily === "llvm" && row.optimizationLevel === "-O0"));
const llvmGraphSize = json(["size", "--json", "--backend", "llvm", graphDumpPath]);
assert.equal(llvmGraphSize.body.graph.artifact, graphDumpPath);
assert.equal(llvmGraphSize.body.targetSupport.backendFamily, "llvm");
assert.equal(llvmGraphSize.body.backendProfile.backendFamily, "llvm");
const llvmIrSize = json(["size", "--json", "--emit", "llvm-ir", "--backend", "llvm", "examples/add.graph"], { allowFailure: true });
assert.notEqual(llvmIrSize.code, 0);
assert.equal(llvmIrSize.body.diagnostics[0].code, "BLD004");
assert.match(llvmIrSize.body.diagnostics[0].message, /writes artifacts only through zero build/);
assert.equal(llvmIrSize.body.diagnostics[0].actual, "size --backend llvm --emit llvm-ir");
assert.equal(llvmIrSize.body.diagnostics[0].backendBlocker.unsupportedFeature, "llvm-ir command");
const llvmIrGraphSize = json(["size", "--json", "--emit", "llvm-ir", "--backend", "llvm", graphDumpPath], { allowFailure: true });
assert.notEqual(llvmIrGraphSize.code, 0);
assert.equal(llvmIrGraphSize.body.diagnostics[0].code, "BLD004");
assert.equal(llvmIrGraphSize.body.diagnostics[0].actual, "size --backend llvm --emit llvm-ir");
assert.equal(llvmIrGraphSize.body.diagnostics[0].backendBlocker.unsupportedFeature, "llvm-ir command");
const directLlvmIrSize = json(["size", "--json", "--emit", "llvm-ir", "--backend", "direct", "examples/add.graph"], { allowFailure: true });
assert.notEqual(directLlvmIrSize.code, 0);
assert.equal(directLlvmIrSize.body.diagnostics[0].actual, "backend=direct emit=llvm-ir");
const llvmMem = json(["mem", "--json", "--backend", "llvm", "examples/add.graph"], { allowFailure: true });
assert.notEqual(llvmMem.code, 0);
assert.equal(llvmMem.body.diagnostics[0].code, "BLD004");
assert.equal(llvmMem.body.diagnostics[0].backendBlocker.backend, "llvm");
assert.equal(llvmMem.body.diagnostics[0].backendBlocker.stage, "buildability");
const llvmIrMem = json(["mem", "--json", "--emit", "llvm-ir", "--backend", "llvm", "examples/add.graph"], { allowFailure: true });
assert.notEqual(llvmIrMem.code, 0);
assert.equal(llvmIrMem.body.diagnostics[0].code, "BLD004");
assert.equal(llvmIrMem.body.diagnostics[0].actual, "mem --backend llvm --emit llvm-ir");
assert.equal(llvmIrMem.body.diagnostics[0].backendBlocker.unsupportedFeature, "llvm-ir command");
const directLlvmIrMem = json(["mem", "--json", "--emit", "llvm-ir", "--backend", "direct", "examples/add.graph"], { allowFailure: true });
assert.notEqual(directLlvmIrMem.code, 0);
assert.equal(directLlvmIrMem.body.diagnostics[0].actual, "backend=direct emit=llvm-ir");
const llvmIrRun = zero(["run", "--emit", "llvm-ir", "--backend", "llvm", "examples/add.graph"], { allowFailure: true });
assert.notEqual(llvmIrRun.code, 0);
assert.match(llvmIrRun.stderr, /BLD004: LLVM IR emission writes artifacts only through zero build/);
assert.match(llvmIrRun.stderr, /actual: run --backend llvm --emit llvm-ir/);
const directLlvmIrRun = zero(["run", "--emit", "llvm-ir", "--backend", "direct", "examples/add.graph"], { allowFailure: true });
assert.notEqual(directLlvmIrRun.code, 0);
assert.match(directLlvmIrRun.stderr, /BLD004: direct backend does not support --emit llvm-ir/);
assert.match(directLlvmIrRun.stderr, /actual: backend=direct emit=llvm-ir/);
const llvmIrGraphRun = zero(["run", "--emit", "llvm-ir", "--backend", "llvm", graphDumpPath], { allowFailure: true });
assert.notEqual(llvmIrGraphRun.code, 0);
assert.match(llvmIrGraphRun.stderr, /BLD004: LLVM IR emission writes artifacts only through zero build/);
assert.match(llvmIrGraphRun.stderr, /actual: run --backend llvm --emit llvm-ir/);
const llvmTest = json(["test", "--json", "--backend", "llvm", "conformance/native/pass/test-blocks.graph"], { allowFailure: true });
assert.equal(llvmTest.code, 0);
assert.equal(llvmTest.body.ok, true);
assert.equal(llvmTest.body.graph.artifact, "conformance/native/pass/test-blocks.graph");
assert.equal(llvmTest.body.testBackend, "direct-program-graph");
const llvmIrTest = json(["test", "--json", "--emit", "llvm-ir", "--backend", "llvm", "conformance/native/pass/test-blocks.graph"], { allowFailure: true });
assert.notEqual(llvmIrTest.code, 0);
assert.equal(llvmIrTest.body.diagnostics[0].code, "BLD004");
assert.equal(llvmIrTest.body.diagnostics[0].actual, "test --backend llvm --emit llvm-ir");
assert.equal(llvmIrTest.body.diagnostics[0].backendBlocker.unsupportedFeature, "llvm-ir command");
const directLlvmIrTest = json(["test", "--json", "--emit", "llvm-ir", "--backend", "direct", "conformance/native/pass/test-blocks.graph"], { allowFailure: true });
assert.notEqual(directLlvmIrTest.code, 0);
assert.equal(directLlvmIrTest.body.diagnostics[0].actual, "backend=direct emit=llvm-ir");
const mismatchedDirectSize = json(["size", "--json", "--backend", "zero-coff-x64", "--target", "linux-x64", "examples/add.graph"], { allowFailure: true });
assert.notEqual(mismatchedDirectSize.code, 0);
assert.equal(mismatchedDirectSize.body.diagnostics[0].actual, "--backend zero-coff-x64");
assert.equal(mismatchedDirectSize.body.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
assert.equal(mismatchedDirectSize.body.diagnostics[0].backendBlocker.stage, "target-selection");
const mismatchedDirectMem = json(["mem", "--json", "--backend", "zero-coff-x64", "--target", "linux-x64", "examples/add.graph"], { allowFailure: true });
assert.notEqual(mismatchedDirectMem.code, 0);
assert.equal(mismatchedDirectMem.body.diagnostics[0].actual, "--backend zero-coff-x64");
assert.equal(mismatchedDirectMem.body.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
assert.equal(mismatchedDirectMem.body.diagnostics[0].backendBlocker.stage, "target-selection");
const mismatchedDirectGraphSize = json(["size", "--json", "--backend", "zero-coff-x64", "--target", "linux-x64", graphDumpPath], { allowFailure: true });
assert.notEqual(mismatchedDirectGraphSize.code, 0);
assert.equal(mismatchedDirectGraphSize.body.diagnostics[0].actual, "--backend zero-coff-x64");
assert.equal(mismatchedDirectGraphSize.body.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
assert.equal(mismatchedDirectGraphSize.body.diagnostics[0].backendBlocker.stage, "target-selection");
const mismatchedDirectTest = json(["test", "--json", "--backend", "zero-coff-x64", "--target", "linux-x64", "conformance/native/pass/test-blocks.graph"], { allowFailure: true });
assert.equal(mismatchedDirectTest.code, 0);
assert.equal(mismatchedDirectTest.body.ok, true);
assert.equal(mismatchedDirectTest.body.graph.artifact, "conformance/native/pass/test-blocks.graph");
assert.equal(mismatchedDirectTest.body.testBackend, "direct-program-graph");
const llvmGraphTest = json(["test", "--json", "--backend", "llvm", graphTestDumpPath], { allowFailure: true });
assert.equal(llvmGraphTest.code, 0);
assert.equal(llvmGraphTest.body.ok, true);
assert.equal(llvmGraphTest.body.testBackend, "direct-program-graph");
assert.equal(llvmGraphTest.body.graph.lowering, "direct-program-graph");
const directLlvmIrGraphTest = json(["test", "--json", "--emit", "llvm-ir", "--backend", "direct", graphTestDumpPath], { allowFailure: true });
assert.notEqual(directLlvmIrGraphTest.code, 0);
assert.equal(directLlvmIrGraphTest.body.diagnostics[0].actual, "backend=direct emit=llvm-ir");
const mismatchedDirectObjReadiness = json(["check", "--json", "--emit", "obj", "--backend", "zero-coff-x64", "--target", "linux-x64", "examples/add.graph"]).body;
assert.equal(mismatchedDirectObjReadiness.ok, true);
assert.equal(mismatchedDirectObjReadiness.targetReadiness.ok, false);
assert.equal(mismatchedDirectObjReadiness.targetReadiness.diagnostics[0].actual, "--backend zero-coff-x64");
assert.equal(mismatchedDirectObjReadiness.targetReadiness.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
assert.equal(mismatchedDirectObjReadiness.targetReadiness.diagnostics[0].backendBlocker.stage, "target-selection");
const mismatchedDirectObjBuild = json(["build", "--json", "--emit", "obj", "--backend", "zero-coff-x64", "--target", "linux-x64", "examples/add.graph", "--out", join(outDir, "mismatch.obj")], { allowFailure: true });
assert.notEqual(mismatchedDirectObjBuild.code, 0);
assert.equal(mismatchedDirectObjBuild.body.diagnostics[0].actual, "--backend zero-coff-x64");
assert.equal(mismatchedDirectObjBuild.body.diagnostics[0].backendBlocker.backend, "zero-coff-x64");
assert.equal(mismatchedDirectObjBuild.body.diagnostics[0].backendBlocker.stage, "target-selection");
const mismatchedDirectExeBuild = json(["build", "--json", "--emit", "exe", "--backend", "zero-coff-x64", "--target", "linux-musl-x64", "examples/direct-exe-return.graph", "--out", join(outDir, "mismatch-exe")], { allowFailure: true });
assert.notEqual(mismatchedDirectExeBuild.code, 0);
assert.equal(mismatchedDirectExeBuild.body.diagnostics[0].actual, "--backend zero-coff-x64");
assert.equal(mismatchedDirectExeBuild.body.diagnostics[0].expected, "direct emitter zero-elf64 for target linux-musl-x64");
assert.match(mismatchedDirectExeBuild.body.diagnostics[0].help, /--backend zero-elf64/);
assert.equal(mismatchedDirectExeBuild.body.diagnostics[0].backendBlocker.stage, "target-selection");
const backendBlockedReadiness = json(["check", "--json", "--emit", "obj", "--target", "linux-musl-x64", "conformance/agent-surface/fixtures/owned-drop-direct-backend-unsupported.graph"]).body;
assert.equal(backendBlockedReadiness.ok, true);
assert.equal(backendBlockedReadiness.diagnostics.length, 0);
assert.equal(backendBlockedReadiness.targetReadiness.ok, false);
assert.equal(backendBlockedReadiness.targetReadiness.buildable, false);
assert.equal(backendBlockedReadiness.targetReadiness.languageOk, true);
assert.equal(backendBlockedReadiness.targetReadiness.emit, "obj");
assert.equal(backendBlockedReadiness.targetReadiness.target, "linux-musl-x64");
assert.equal(backendBlockedReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.deepEqual(backendBlockedReadiness.targetReadiness.diagnostics[0].backendBlocker, {
  target: "linux-musl-x64",
  objectFormat: "elf",
  backend: "zero-elf64",
  stage: "lower",
  unsupportedFeature: "owned<Tracked>",
});
const directExeBlockedReadiness = json(["check", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-call-add.graph"]).body;
assert.equal(directExeBlockedReadiness.ok, true);
assert.equal(directExeBlockedReadiness.diagnostics.length, 0);
assert.equal(directExeBlockedReadiness.targetReadiness.ok, false);
assert.equal(directExeBlockedReadiness.targetReadiness.buildable, false);
assert.equal(directExeBlockedReadiness.targetReadiness.languageOk, true);
assert.equal(directExeBlockedReadiness.targetReadiness.emit, "exe");
assert.equal(directExeBlockedReadiness.targetReadiness.target, "linux-musl-x64");
assert.equal(directExeBlockedReadiness.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(directExeBlockedReadiness.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
assert.match(directExeBlockedReadiness.targetReadiness.diagnostics[0].message, /main must not take parameters/);
const directExeBlockedBuild = json(["build", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-call-add.graph", "--out", join(outDir, "direct-call-add-blocked")], { allowFailure: true });
assert.notEqual(directExeBlockedBuild.code, 0);
for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
  assert.equal(directExeBlockedBuild.body.diagnostics[0][key], directExeBlockedReadiness.targetReadiness.diagnostics[0][key]);
}
assert.equal(directExeBlockedBuild.body.diagnostics[0].backendBlocker.stage, "buildability");
const directExeBlockedGraph = json(["inspect", "--json", "--emit", "exe", "--target", "linux-musl-x64", "examples/direct-call-add.0"]).body;
assert.equal(directExeBlockedGraph.targetReadiness.ok, false);
assert.equal(directExeBlockedGraph.targetReadiness.diagnostics[0].code, "BLD004");
assert.equal(directExeBlockedGraph.targetReadiness.diagnostics[0].path, "direct-call-add.0");
assert.equal(directExeBlockedGraph.targetReadiness.diagnostics[0].actual, "unsupported construct");
assert.equal(directExeBlockedGraph.targetReadiness.diagnostics[0].backendBlocker.stage, "lower");
const coffMaybeByteViewFixture = "conformance/native/pass/coff-maybe-byte-view-buildable.graph";
const coffMaybeByteViewReadiness = json(["check", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffMaybeByteViewFixture]).body;
assert.equal(coffMaybeByteViewReadiness.ok, true);
assert.equal(coffMaybeByteViewReadiness.targetReadiness.ok, true);
assert.equal(coffMaybeByteViewReadiness.targetReadiness.buildable, true);
assert.equal(coffMaybeByteViewReadiness.targetReadiness.backend, "zero-coff-x64");
const coffMaybeByteViewBuild = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffMaybeByteViewFixture, "--out", join(outDir, "coff-maybe-byte-view.obj")]).body;
assert.equal(coffMaybeByteViewBuild.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(coffMaybeByteViewBuild.generatedCBytes, 0);
const coffDynamicSliceFixture = "conformance/native/pass/coff-dynamic-byte-slice.graph";
const coffDynamicSliceReadiness = json(["check", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffDynamicSliceFixture]).body;
assert.equal(coffDynamicSliceReadiness.ok, true);
assert.equal(coffDynamicSliceReadiness.diagnostics.length, 0);
assert.equal(coffDynamicSliceReadiness.targetReadiness.ok, true);
assert.equal(coffDynamicSliceReadiness.targetReadiness.buildable, true);
assert.equal(coffDynamicSliceReadiness.targetReadiness.languageOk, true);
assert.equal(coffDynamicSliceReadiness.targetReadiness.emit, "obj");
assert.equal(coffDynamicSliceReadiness.targetReadiness.target, "win32-x64.exe");
assert.equal(coffDynamicSliceReadiness.targetReadiness.backend, "zero-coff-x64");
const coffDynamicSlicePath = join(outDir, "coff-dynamic-byte-slice.obj");
const coffDynamicSliceBuild = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffDynamicSliceFixture, "--out", coffDynamicSlicePath]).body;
assert.equal(coffDynamicSliceBuild.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(coffDynamicSliceBuild.generatedCBytes, 0);
const coffDynamicSliceBytes = readFileSync(coffDynamicSlicePath);
assert.equal(coffDynamicSliceBytes.readUInt16LE(0), 0x8664);
const coffBoolCopyFixture = "conformance/native/pass/std-mem-bool-copy-items.graph";
const coffBoolCopyReadiness = json(["check", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffBoolCopyFixture]).body;
assert.equal(coffBoolCopyReadiness.ok, true);
assert.equal(coffBoolCopyReadiness.targetReadiness.ok, true);
assert.equal(coffBoolCopyReadiness.targetReadiness.buildable, true);
assert.equal(coffBoolCopyReadiness.targetReadiness.backend, "zero-coff-x64");
const coffBoolCopyPath = join(outDir, "coff-bool-copy-items.obj");
const coffBoolCopyBuild = json(["build", "--json", "--emit", "obj", "--target", "win32-x64.exe", coffBoolCopyFixture, "--out", coffBoolCopyPath]).body;
assert.equal(coffBoolCopyBuild.objectBackend.objectEmission.path, "direct-coff-x64-object");
assert.equal(coffBoolCopyBuild.generatedCBytes, 0);
const coffBoolCopyBytes = readFileSync(coffBoolCopyPath);
assert.equal(coffBoolCopyBytes.readUInt16LE(0), 0x8664);
function assertMachOObjectBuildabilityBlocked(fixture: string, outName: string, expectedMessage: RegExp) {
  const readiness = json(["check", "--json", "--emit", "obj", "--target", "darwin-arm64", fixture]).body;
  assert.equal(readiness.ok, true);
  assert.equal(readiness.diagnostics.length, 0);
  assert.equal(readiness.targetReadiness.ok, false);
  assert.equal(readiness.targetReadiness.buildable, false);
  assert.equal(readiness.targetReadiness.languageOk, true);
  assert.equal(readiness.targetReadiness.emit, "obj");
  assert.equal(readiness.targetReadiness.target, "darwin-arm64");
  assert.equal(readiness.targetReadiness.diagnostics[0].code, "BLD004");
  assert.equal(readiness.targetReadiness.diagnostics[0].backendBlocker.backend, "zero-macho64");
  assert.equal(readiness.targetReadiness.diagnostics[0].backendBlocker.stage, "buildability");
  assert.match(readiness.targetReadiness.diagnostics[0].message, expectedMessage);
  const build = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", fixture, "--out", join(outDir, outName)], { allowFailure: true });
  assert.notEqual(build.code, 0);
  for (const key of ["code", "path", "line", "column", "length", "expected", "actual", "help"]) {
    assert.equal(build.body.diagnostics[0][key], readiness.targetReadiness.diagnostics[0][key]);
  }
  assert.equal(build.body.diagnostics[0].backendBlocker.backend, "zero-macho64");
  assert.equal(build.body.diagnostics[0].backendBlocker.stage, "buildability");
}
assertMachOObjectBuildabilityBlocked(
  "conformance/native/pass/macho-large-byte-slice-blocked.graph",
  "macho-large-byte-slice.o",
  /constant start/,
);
assertMachOObjectBuildabilityBlocked(
  "conformance/native/pass/macho-nested-call-scratch-blocked.graph",
  "macho-nested-call-scratch.o",
  /scratch spill capacity/,
);
const machoOpenByteSliceFixture = "conformance/native/pass/macho-open-byte-slice.graph";
const machoOpenByteSliceReadiness = json(["check", "--json", "--emit", "obj", "--target", "darwin-arm64", machoOpenByteSliceFixture]).body;
assert.equal(machoOpenByteSliceReadiness.ok, true);
assert.equal(machoOpenByteSliceReadiness.diagnostics.length, 0);
assert.equal(machoOpenByteSliceReadiness.targetReadiness.ok, true);
assert.equal(machoOpenByteSliceReadiness.targetReadiness.buildable, true);
assert.equal(machoOpenByteSliceReadiness.targetReadiness.backend, "zero-macho64");
const machoOpenByteSlicePath = join(outDir, "macho-open-byte-slice.o");
const machoOpenByteSliceBuild = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", machoOpenByteSliceFixture, "--out", machoOpenByteSlicePath]).body;
const machoOpenByteSliceBytes = readFileSync(machoOpenByteSlicePath);
assert.equal(machoOpenByteSliceBuild.compiler, "zero-macho64");
assert.equal(machoOpenByteSliceBuild.generatedCBytes, 0);
assert.equal(machoOpenByteSliceBuild.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(machoOpenByteSliceBytes.readUInt32LE(0), 0xfeedfacf);
const aarch64OpenSliceBoundsFixture = join(outDir, "aarch64-open-byte-slice-bounds.0");
writeProjectionFile(aarch64OpenSliceBoundsFixture, `export c fn main() -> u32 {
    let words: [2]u16 = [1_u16, 2_u16]
    let suffix: Span<u16> = words[3_usize..]
    return (std.mem.len(suffix)) as u32
}
`);
const aarch64OpenSliceBoundsGraph = importProjectionSidecar(aarch64OpenSliceBoundsFixture);
const aarch64OpenSliceBoundsPath = join(outDir, "aarch64-open-byte-slice-bounds.o");
json(["build", "--json", "--emit", "obj", "--target", "linux-arm64", aarch64OpenSliceBoundsGraph, "--out", aarch64OpenSliceBoundsPath]);
const aarch64OpenSliceBoundsBytes = readFileSync(aarch64OpenSliceBoundsPath);
assert.equal(aarch64OpenSliceBoundsBytes.readUInt16LE(16), 1);
assert.equal(aarch64OpenSliceBoundsBytes.readUInt16LE(18), 183);
assert(hasAarch64CondBranch(aarch64OpenSliceBoundsBytes, 9));
assert(hasAarch64Instruction(aarch64OpenSliceBoundsBytes, 0xd4200000));
const machoOpenSliceBoundsPath = join(outDir, "macho-open-byte-slice-bounds.o");
json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", aarch64OpenSliceBoundsGraph, "--out", machoOpenSliceBoundsPath]);
const machoOpenSliceBoundsBytes = readFileSync(machoOpenSliceBoundsPath);
assert.equal(machoOpenSliceBoundsBytes.readUInt32LE(0), 0xfeedfacf);
assert(hasAarch64CondBranch(machoOpenSliceBoundsBytes, 9));
assert(hasAarch64Instruction(machoOpenSliceBoundsBytes, 0xd4200000));
const machOMemoryPackageReadiness = json(["check", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/memory-package"]).body;
assert.equal(machOMemoryPackageReadiness.ok, true);
assert.equal(machOMemoryPackageReadiness.diagnostics.length, 0);
assert.equal(machOMemoryPackageReadiness.targetReadiness.ok, true);
assert.equal(machOMemoryPackageReadiness.targetReadiness.buildable, true);
assert.equal(machOMemoryPackageReadiness.targetReadiness.languageOk, true);
assert.equal(machOMemoryPackageReadiness.targetReadiness.emit, "obj");
assert.equal(machOMemoryPackageReadiness.targetReadiness.target, "darwin-arm64");
assert.equal(machOMemoryPackageReadiness.targetReadiness.backend, "zero-macho64");
const machOMemoryPackagePath = join(outDir, "direct-darwin-arm64-memory-package.o");
rmSync(machOMemoryPackagePath, { force: true });
const machOMemoryPackageReport = json(["build", "--json", "--emit", "obj", "--target", "darwin-arm64", "examples/memory-package", "--out", machOMemoryPackagePath]).body;
const machOMemoryPackageBytes = readFileSync(machOMemoryPackagePath);
assert.equal(machOMemoryPackageReport.compiler, "zero-macho64");
assert.equal(machOMemoryPackageReport.generatedCBytes, 0);
assert.equal(machOMemoryPackageReport.objectBackend.objectEmission.path, "direct-macho64-object");
assert.equal(machOMemoryPackageReport.objectBackend.directFacts.moduleCount, 3);
assert.equal(machOMemoryPackageReport.objectBackend.directFacts.runtimeHelperCount, 1);
assert.equal(machOMemoryPackageBytes.readUInt32LE(0), 0xfeedfacf);
assert(machOMemoryPackageBytes.includes(Buffer.from("memory package ok")));

// Package builds must be byte-deterministic: emitting the same package through
// the store-backed manifest pipeline twice must produce identical object bytes,
// so toolchain or allocation randomness can never leak into emitted artifacts.
const crmApiDeterminismPath = join(outDir, "determinism-crm-api.o");
rmSync(crmApiDeterminismPath, { force: true });
const crmApiDeterminismReport = json(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/crm-api", "--out", crmApiDeterminismPath]).body;
assert.equal(crmApiDeterminismReport.generatedCBytes, 0);
assert.equal(crmApiDeterminismReport.cBridgeFallback ?? false, false);
repeatBuildHash(["build", "--json", "--emit", "obj", "--target", "linux-musl-x64", "examples/crm-api", "--out", crmApiDeterminismPath], crmApiDeterminismPath, join(outDir, "determinism-crm-api.repeat.o"));

const graph = json(["inspect", "--json", "--target", "linux-musl-x64", "examples/memory-package"]).body;
assert.equal(graph.schemaVersion, 1);
assert.equal(graph.targetSupport.target, "linux-musl-x64");
assert.equal(typeof graph.targetSupport.hostTarget, "string");
assert.equal(graph.targetSupport.fsAvailable, true);
assert(graph.modules.some((module) => module.name === "main"));
assert.equal(graph.imports.length, 0);
assert(graph.importEdges.some((edge) => edge.from === "main" && edge.to === "buffer"));
assert(graph.functions.some((fun) => fun.name === "main"));
assert(graph.functions.some((fun) => fun.name === "prepare" && fun.requiresCapabilities.includes("memory")));
assert(graph.stdlibHelpers.some((helper) => helper.name === "std.mem.copy" && helper.targetSupport === "target-neutral"));
assert(graph.stdlibHelpers.some((helper) => helper.name === "std.mem.byteBuf" && /explicit allocator/.test(helper.allocationBehavior)));
assert(graph.stdlibHelpers.some((helper) => helper.name === "std.fs.createOrRaise" && helper.targetSupport === "host"));
const graphMemCopyHelper = graph.stdlibHelpers.find((helper) => helper.name === "std.mem.copy");
assert.equal(graphMemCopyHelper.module, "std.mem");
assert(graphMemCopyHelper.effects.includes("memory"));
assert.equal(graphMemCopyHelper.errorBehavior, "infallible");
assert.match(graphMemCopyHelper.ownershipNotes, /caller-owned storage/);
assert.equal(graphMemCopyHelper.example, "examples/memory-primitives.0");
assert.equal(graphMemCopyHelper.apiStability, "bootstrap-stable");

const agentToolsGraph = json(["inspect", "--json", "examples/std-testing-log.0"]).body;
const graphTestingHelper = agentToolsGraph.stdlibHelpers.find((helper) => helper.name === "std.testing.equalBytes");
assert.equal(graphTestingHelper.module, "std.testing");
assert.equal(graphTestingHelper.targetSupport, "target-neutral");
assert.equal(graphTestingHelper.errorBehavior, "infallible");
assert.equal(graphTestingHelper.example, "examples/std-testing-log.0");
const graphLogHelper = agentToolsGraph.stdlibHelpers.find((helper) => helper.name === "std.log.keyValue");
assert.equal(graphLogHelper.module, "std.log");
assert(graphLogHelper.effects.includes("memory"));
assert.match(graphLogHelper.allocationBehavior, /caller buffer/);
assert.equal(graphLogHelper.errorBehavior, "returns null on failure");
assert.match(graphLogHelper.ownershipNotes, /caller-owned storage/);
assert.equal(graphLogHelper.example, "examples/std-testing-log.0");

const stdDataSize = json(["size", "--json", "conformance/native/pass/std-codec-json-url.graph"]).body;
const sizeUrlHostHelper = stdDataSize.stdlibHelpers.find((helper) => helper.name === "std.url.host");
assert.equal(sizeUrlHostHelper.module, "std.url");
assert.equal(sizeUrlHostHelper.example, "conformance/native/pass/std-codec-json-url.0");

const agentToolsSize = json(["size", "--json", "examples/std-testing-log.graph"]).body;
assert(agentToolsSize.usedStdlibHelpers.some((helper) => helper.name === "std.log.keyValue" && helper.module === "std.log"));
assert(agentToolsSize.usedStdlibHelpers.some((helper) => helper.name === "std.testing.containsBytes" && helper.module === "std.testing"));
assert(agentToolsSize.usedStdlibHelpers.every((helper) => helper.module && helper.effects && helper.allocationBehavior && helper.targetSupport && helper.errorBehavior && helper.ownershipNotes && helper.example && helper.apiStability));

const stdTestingTestJson = json(["test", "--json", "conformance/native/pass/std-testing-helpers-test.graph"]).body;
assert.equal(stdTestingTestJson.ok, true);
assert.equal(stdTestingTestJson.passedTests, 1);

const crcOnlySource = join(outDir, "std-codec-crc-only.0");
writeProjectionFile(crcOnlySource, `export c fn main() -> u32 {
    return std.codec.crc32("zero")
}
`);
const crcOnlySourceGraph = importProjectionSidecar(crcOnlySource);
const crcOnlyGraph = json(["inspect", "--json", crcOnlySource]).body;
assert(!crcOnlyGraph.sourceFiles.some((path) => path.endsWith("std/codec.0")));
assert(!crcOnlyGraph.requiresCapabilities.includes("parse"));
const crcOnlySize = json(["size", "--json", crcOnlySourceGraph]).body;
assert(!crcOnlySize.incrementalInvalidation.changedInputs.sourceFiles.some((path) => path.endsWith("std/codec.0")));
assert(!crcOnlySize.retentionReasons.some((item) => item.name === "__zero_std_codec_hex_decode"));

const codecReadOnlySource = join(outDir, "std-codec-read-only.0");
writeProjectionFile(codecReadOnlySource, `export c fn main() -> i32 {
    let value: Maybe<u16> = std.codec.readU16Le("AB")
    if value.has {
        return 0
    }
    return 1
}
`);
const codecReadOnlySourceGraph = importProjectionSidecar(codecReadOnlySource);
const codecReadOnlyGraph = json(["inspect", "--json", codecReadOnlySource]).body;
assert(codecReadOnlyGraph.sourceFiles.some((path) => path.endsWith("std/codec.0")));
assert(!codecReadOnlyGraph.sourceFiles.some((path) => path.endsWith("std/ascii.0")));
assert(codecReadOnlyGraph.requiresCapabilities.includes("codec"));
assert(!codecReadOnlyGraph.requiresCapabilities.includes("parse"));
const codecReadOnlySize = json(["size", "--json", codecReadOnlySourceGraph]).body;
assert.equal(codecReadOnlySize.sizeBreakdown.summary.functionCount, 2);
assert.equal(codecReadOnlySize.sizeBreakdown.summary.stdlibHelperCount, 2);
assert(codecReadOnlySize.topLargestEmittedHelpers.some((helper) => helper.name === "std.codec.readU16Le"));
assert(!codecReadOnlySize.retentionReasons.some((item) => item.name === "__zero_std_codec_hex_decode"));
assert(!codecReadOnlySize.retentionReasons.some((item) => item.name === "std.ascii.hexValue"));
assert(!codecReadOnlySize.retentionReasons.some((item) => item.name === "std.url.percentEncode"));

const jsonStatusOnlySource = join(outDir, "std-json-status-only.0");
writeProjectionFile(jsonStatusOnlySource, `export c fn main() -> i32 {
    let status: u32 = std.json.errorNone()
    if status == 0_u32 {
        return 0
    }
    return 1
}
`);
const jsonStatusOnlySourceGraph = importProjectionSidecar(jsonStatusOnlySource);
const jsonStatusOnlyGraph = json(["inspect", "--json", jsonStatusOnlySource]).body;
assert(!jsonStatusOnlyGraph.sourceFiles.some((path) => path.endsWith("std/json.0")));
assert(!jsonStatusOnlyGraph.sourceFiles.some((path) => path.endsWith("std/ascii.0")));
assert(!jsonStatusOnlyGraph.sourceFiles.some((path) => path.endsWith("std/fmt.0")));
assert(!jsonStatusOnlyGraph.sourceFiles.some((path) => path.endsWith("std/parse.0")));
assert(jsonStatusOnlyGraph.callResolution.calls.some((call) => call.kind === "stdlib" && call.calleeName === "std.json.errorNone"));
const jsonStatusOnlySize = json(["size", "--json", jsonStatusOnlySourceGraph]).body;
assert.equal(jsonStatusOnlySize.objectBackend.directFacts.functionCount, 1);
assert(!jsonStatusOnlySize.retentionReasons.some((item) => item.name === "__zero_std_json_validate_error"));
assert(!jsonStatusOnlySize.retentionReasons.some((item) => item.name === "std.parse.parseU32"));

const httpErrorNameOnlySource = join(outDir, "std-http-error-name-only.0");
writeProjectionFile(httpErrorNameOnlySource, `export c fn main() -> i32 {
    let name: String = std.http.errorName(std.http.errorTimeout())
    if std.mem.len(name) > 0_usize {
        return 0
    }
    return 1
}
`);
const httpErrorNameOnlyGraphPath = importProjectionSidecar(httpErrorNameOnlySource);
const httpErrorNameOnlyGraph = json(["inspect", "--json", "--target", "linux-musl-x64", httpErrorNameOnlySource]).body;
assert.deepEqual(httpErrorNameOnlyGraph.requiresCapabilities, ["memory"]);
assert.equal(json(["check", "--json", "--target", "linux-musl-x64", httpErrorNameOnlyGraphPath]).body.ok, true);

const testingEqualU32OnlySource = join(outDir, "std-testing-equal-u32-only.0");
writeProjectionFile(testingEqualU32OnlySource, `export c fn main() -> i32 {
    if std.testing.equalU32(42_u32, 42_u32) {
        return 0
    }
    return 1
}
`);
importProjectionSidecar(testingEqualU32OnlySource);
const testingEqualU32OnlyGraph = json(["inspect", "--json", testingEqualU32OnlySource]).body;
assert.deepEqual(testingEqualU32OnlyGraph.requiresCapabilities, []);

const logMessageOnlySource = join(outDir, "std-log-message-only.0");
writeProjectionFile(logMessageOnlySource, `export c fn main() -> i32 {
    var storage: [64]u8 = [0_u8; 64]
    let entry: Maybe<Span<u8>> = std.log.message(storage, "info", "ready")
    if entry.has {
        return 0
    }
    return 1
}
`);
importProjectionSidecar(logMessageOnlySource);
const logMessageOnlyGraph = json(["inspect", "--json", logMessageOnlySource]).body;
assert.deepEqual(logMessageOnlyGraph.requiresCapabilities, ["memory"]);

const logMessageFieldSource = join(outDir, "std-log-message-field.0");
writeProjectionFile(logMessageFieldSource, `export c fn main() -> i32 {
    var storage: [96]u8 = [0_u8; 96]
    let entry: Maybe<Span<u8>> = std.log.messageField(storage, "info", "ready", "\\"event\\":\\"startup\\"")
    if entry.has {
        return 0
    }
    return 1
}
`);
importProjectionSidecar(logMessageFieldSource);
const logMessageFieldGraph = json(["inspect", "--json", logMessageFieldSource]).body;
assert.deepEqual(logMessageFieldGraph.requiresCapabilities, ["memory", "parse"]);

const httpErrorsGraph = json(["inspect", "--json", "conformance/native/pass/std-http-errors.0"]).body;
assert.deepEqual(httpErrorsGraph.requiresCapabilities, ["memory"]);
assert.equal(json(["check", "--json", "--target", "linux-musl-x64", "conformance/native/pass/std-http-errors.graph"]).body.ok, true);
const httpTimeoutHelper = httpErrorsGraph.stdlibHelpers.find((helper) => helper.name === "std.http.errorTimeout");
assert.equal(httpTimeoutHelper.returnType, "HttpError");
assert.equal(httpTimeoutHelper.capability, "none");
assert.deepEqual(httpTimeoutHelper.effects, []);
assert.equal(httpTimeoutHelper.allocationBehavior, "no allocation");
assert.equal(httpTimeoutHelper.ownershipNotes, "no ownership transfer");
const httpErrorNameHelper = httpErrorsGraph.stdlibHelpers.find((helper) => helper.name === "std.http.errorName");
assert.equal(httpErrorNameHelper.returnType, "String");
assert.equal(httpErrorNameHelper.capability, "none");
assert.equal(httpErrorNameHelper.targetSupport, "target-neutral");

const coreGraph = json(["inspect", "--json", "examples/static-method.0"]).body;
const counterShape = coreGraph.shapes.find((shape) => shape.name === "Counter");
assert(counterShape);
assert(counterShape.fields.some((field) => field.name === "value" && field.hasDefault === false));
assert(counterShape.methods.some((method) => method.name === "add" && method.staticDispatch === true));

const interfaceGraph = json(["inspect", "--json", "examples/static-interface.0"]).body;
assert(interfaceGraph.interfaces.some((item) => item.name === "Readable" && item.staticOnly === true));
assert(interfaceGraph.functions.some((item) => item.name === "readValue" && item.constraints.some((constraint) => constraint.interface === "Readable<T>")));

const staticValueGraph = json(["inspect", "--json", "examples/static-value-params.0"]).body;
const fixedVecShape = staticValueGraph.shapes.find((shape) => shape.name === "FixedVec");
assert(fixedVecShape);
assert(fixedVecShape.staticParams.some((param) => param.name === "N" && param.type === "usize"));
assert(staticValueGraph.functions.some((fun) => fun.name === "first" && fun.staticParams.some((param) => param.name === "N")));

const fixedVecMethodsGraph = json(["inspect", "--json", "examples/fixed-vec.0"]).body;
const fixedVecMethodsShape = fixedVecMethodsGraph.shapes.find((shape) => shape.name === "FixedVec");
assert(fixedVecMethodsShape);
assert(fixedVecMethodsShape.methods.some((method) => method.name === "push" && method.inheritedShapeParams === true && method.shapeStaticParams.some((param) => param.name === "N")));
assert(fixedVecMethodsShape.methods.some((method) => method.name === "push" && typeof method.doc === "string"));
assert(fixedVecMethodsShape.fields.some((field) => field.name === "len" && field.hasDefault === true));

const aliasGraph = json(["inspect", "--json", "examples/type-alias.0"]).body;
assert(aliasGraph.aliases.some((alias) => alias.name === "BytePair" && alias.target === "Pair<u8, u8>"));

const constGraph = json(["inspect", "--json", "examples/const-arithmetic.0"]).body;
assert(constGraph.consts.some((item) => item.name === "answer" && item.type === "i32"));

const errorGraph = json(["inspect", "--json", "conformance/native/pass/fallibility-error-sets.0"]).body;
const maybeFail = errorGraph.functions.find((item) => item.name === "maybe_fail");
assert(maybeFail);
assert.equal(maybeFail.errorSetKind, "explicit");
assert(maybeFail.errorNames.includes("BadInput"));
const inferredGraph = json(["inspect", "--json", "conformance/native/pass/check-maybe-fallibility.0"]).body;
const inferredPrivate = inferredGraph.functions.find((item) => item.name === "first_or_none");
assert(inferredPrivate);
assert.equal(inferredPrivate.errorSetKind, "inferred");

const size = json(["size", "--json", "--target", "linux-musl-x64", "examples/memory-package"]).body;
assert.equal(size.schemaVersion, 1);
assert(size.stdlibHelpers.some((helper) => helper.name === "std.mem.copy"));
assert(size.usedStdlibHelpers.some((helper) => helper.name === "std.mem.copy"));
assert(size.usedStdlibHelpers.every((helper) => helper.module && helper.effects?.length && helper.errorBehavior && helper.ownershipNotes && helper.example && helper.apiStability));
assert(size.requiresCapabilities.includes("memory"));
assert.equal(size.generatedCBytes, 0);
assert.equal(size.cBridgeFallback, false);
assert(size.loweredIrBytes > 0);
assert(size.sections.some((section) => section.name === "lowered-ir" && section.bytes === size.loweredIrBytes));
assert(size.sections.some((section) => section.name === "direct-size-metadata" && section.kind === "metadata"));
assert(size.topLargestEmittedHelpers.some((helper) => helper.name === "std.mem.copy" && helper.estimatedDirectBytes > 0));
assert.equal(size.objectBackend.objectEmission.path, "direct-elf64-object");
assert(size.compilerRuntimeHelpers.every((helper) => helper.payAsUsed === true && helper.emitted === false));
const sizedArtifact = join(outDir, "sized-memory-package");
rmSync(sizedArtifact, { force: true });
rmSync(`${sizedArtifact}.c`, { force: true });
const sizeWithArtifact = json(["size", "--json", "--out", sizedArtifact, "examples/memory-package"]).body;
assert.equal(sizeWithArtifact.artifactPath, sizedArtifact);
assert(sizeWithArtifact.artifactBytes > 0);
assert(existsSync(sizedArtifact));
const sizeBlockedParent = join(outDir, "size-output-not-dir");
const sizeBlockedOut = join(sizeBlockedParent, "report.json");
rmSync(sizeBlockedParent, { force: true, recursive: true });
writeFileSync(sizeBlockedParent, "not a directory\n");
const sizeBlocked = json(["size", "--json", "--out", sizeBlockedOut, "examples/hello.graph"], { allowFailure: true });
assert.notEqual(sizeBlocked.code, 0);
assert.equal(sizeBlocked.body.diagnostics[0].path, sizeBlockedOut);
const explainRepairId = (code: string) => json(["explain", "--json", code]).body.repair.id;
const explainRepairs = {
  ERR001: explainRepairId("ERR001"),
  ERR003: explainRepairId("ERR003"),
  TYP025: explainRepairId("TYP025"),
  TYP009: explainRepairId("TYP009"),
  FLD002: explainRepairId("FLD002"),
  PUB001: explainRepairId("PUB001"),
};
assert.equal(explainRepairs.ERR001, "add-fallible-marker-or-rescue");
assert.equal(explainRepairs.ERR003, "check-or-rescue-fallible-call");
assert.equal(explainRepairs.TYP025, "add-explicit-generic-type-arguments");
assert.equal(explainRepairs.TYP009, "make-binding-mutable");
assert.equal(explainRepairs.FLD002, "initialize-missing-field");
assert.equal(explainRepairs.PUB001, "add-public-api-type");
const generatedCBytesAfterReadOnlyCommands = json(["size", "--json", "examples/memory-package"]).body.generatedCBytes;
assert.equal(generatedCBytesAfterReadOnlyCommands, generatedCBytesBeforeReadOnlyCommands);

const targets = json(["targets"]).body;
assert.equal(targets.schemaVersion, 1);
assert.equal(typeof targets.host, "string");
const publicTargetNames = ["darwin-arm64", "darwin-x64", "linux-arm64", "linux-musl-arm64", "linux-musl-x64", "linux-x64", "win32-arm64.exe", "win32-x64.exe"];
assert.deepEqual([...targets.targets.map((target) => target.name)].sort(), [...publicTargetNames].sort());
for (const targetName of publicTargetNames) {
  assert(targets.targets.some((target) => target.name === targetName), `${targetName} should be listed`);
}
assert(targets.targets.some((target) => target.hosted === true && target.capabilities.includes("fs")));
assert(targets.targets.some((target) => target.hosted === false && !target.capabilities.includes("fs")));
const linuxGnuTarget = targets.targets.find((target) => target.name === "linux-x64");
const linuxMuslTarget = targets.targets.find((target) => target.name === "linux-musl-x64");
const darwinArm64Target = targets.targets.find((target) => target.name === "darwin-arm64");
const darwinX64Target = targets.targets.find((target) => target.name === "darwin-x64");
const winX64Target = targets.targets.find((target) => target.name === "win32-x64.exe");
const winArm64Target = targets.targets.find((target) => target.name === "win32-arm64.exe");
const linuxArm64Target = targets.targets.find((target) => target.name === "linux-arm64");
const hostTarget = targets.targets.find((target) => target.name === targets.host);
assert(hostTarget);
for (const target of targets.targets) {
  assert.deepEqual(target.backendFamilies.known, ["direct", "llvm"]);
  assert.equal(target.backendFamilies.fallbackPolicy, "none");
  assert(target.backendFamilies.llvm.emit.includes("llvm-ir"));
  assert(["ready", "tool-missing", "unsupported-target"].includes(target.backendFamilies.llvm.status));
  assert.equal(target.backendFamilies.llvm.backendLifecycle.stability, "experimental");
  assert.equal(target.backendFamilies.llvm.backendLifecycle.defaultEligible, false);
  assert.equal(target.backendFamilies.llvm.backendLifecycle.releaseEligible, false);
}
assert.equal(hostTarget.backendFamilies.llvm.status, doctor.llvmToolchain.status);
assert.equal(hostTarget.backendFamilies.llvm.buildable, doctor.llvmToolchain.nativeExecutable);
if (doctor.llvmToolchain.nativeExecutable) assert(hostTarget.backendFamilies.llvm.emit.includes("exe"));
assert.equal(winX64Target.backendFamilies.llvm.emit.includes("exe"), false);
assert.equal(winArm64Target.backendFamilies.llvm.emit.includes("exe"), false);
assert.equal(linuxMuslTarget.directBackend.exeSupported, true);
assert.equal(linuxMuslTarget.directBackend.exeEmitter, "zero-elf64-exe");
assert.equal(linuxGnuTarget.directBackend.objectEmitter, "zero-elf64");
assert.equal(linuxGnuTarget.directBackend.exeSupported, true);
assert.equal(linuxGnuTarget.directBackend.exeEmitter, "zero-elf64-exe");
assert.equal(darwinArm64Target.directBackend.objectEmitter, "zero-macho64");
assert.equal(darwinArm64Target.directBackend.exeSupported, true);
assert.equal(darwinArm64Target.directBackend.exeEmitter, "zero-macho64-exe");
assert.equal(darwinX64Target.directBackend.objectEmitter, "zero-macho-x64");
assert.equal(darwinX64Target.directBackend.exeSupported, true);
assert.equal(darwinX64Target.directBackend.exeEmitter, "zero-macho-x64-exe");
assert.equal(winX64Target.directBackend.objectEmitter, "zero-coff-x64");
assert.equal(winX64Target.directBackend.objectSupported, true);
assert.equal(winX64Target.directBackend.exeSupported, true);
assert.equal(winX64Target.directBackend.exeEmitter, "zero-coff-x64-exe");
assert.equal(winArm64Target.directBackend.status, "native-exe");
assert.equal(winArm64Target.directBackend.objectEmitter, "zero-coff-aarch64");
assert.equal(winArm64Target.directBackend.exeEmitter, "zero-coff-aarch64-exe");
assert.match(winArm64Target.directBackend.reason, /direct object and executable backend available/);
assert.equal(linuxArm64Target.directBackend.status, "native-exe");
assert.equal(linuxArm64Target.directBackend.objectEmitter, "zero-elf-aarch64");
assert.equal(linuxArm64Target.directBackend.exeEmitter, "zero-elf-aarch64-exe");
assert.match(linuxArm64Target.directBackend.reason, /direct object and executable backend available/);
const cAbiExport = zero(["check", "conformance/native/pass/c-abi-export.graph"]);
assert.match(cAbiExport.stdout, /ok/);
const cAbiDump = json(["abi", "dump", "--json", "conformance/native/pass/c-abi-export.graph"]).body;
assert(cAbiDump.cExports.some((item) => item.name === "zero_add" && item.cReturnType === "int32_t"));
assert.match(cAbiDump.generatedHeader.text, /int32_t zero_add\(int32_t a, int32_t b\);/);
const report = {
  generatedAt: new Date().toISOString(),
  productShell: {
    version: version.version,
    host: version.host,
    backend: version.backend,
    doctorStatus: doctor.status,
    cleanRemovedOut: !existsSync(cleanProbe),
  },
  diagnostics: {
    explainRepairs,
  },
  graph: {
    target: graph.targetSupport.target,
    requiresCapabilities: graph.requiresCapabilities,
    stdlibHelperCount: graph.stdlibHelpers.length,
    coreMethodCount: counterShape.methods.length,
    interfaceCount: interfaceGraph.interfaces.length,
    aliasCount: aliasGraph.aliases.length,
    constCount: constGraph.consts.length,
    errorFunction: maybeFail.errorNames,
  },
  size: {
    generatedCBytes: size.generatedCBytes,
    stdlibHelperCount: size.stdlibHelpers.length,
    usedStdlibHelperCount: size.usedStdlibHelpers.length,
    artifactBytes: sizeWithArtifact.artifactBytes,
    unchangedByReadOnlyCommands: generatedCBytesAfterReadOnlyCommands === generatedCBytesBeforeReadOnlyCommands,
  },
  noCDefaultRouteSentinels: {
    defaultNoC: [
      {
        id: "direct-linux-exe",
        generatedCBytes: directExeReport.generatedCBytes,
        cBridgeFallback: directExeReport.selfHostRouting?.cBridge?.required ?? false,
        objectEmissionPath: directExeReport.objectBackend.objectEmission.path,
      },
    ],
    knownDefaultCGap: {
      id: "hello-linux-default",
      generatedCBytes: buildReport.generatedCBytes,
      cBridgeFallback: buildReport.selfHostRouting?.cBridge?.required ?? false,
      objectEmissionPath: buildReport.objectBackend.objectEmission.path,
    },
    removedGeneratedC: {
      emitCDiagnostic: removedEmitC.body.diagnostics[0].code,
      legacyFlagDiagnostic: removedLegacyFlag.body.diagnostics[0].code,
      repair: removedEmitC.body.diagnostics[0].repair.id,
    },
    directLinkedExecutables: {
      darwin: {
        compiler: directMachOExeReport.compiler,
        generatedCBytes: directMachOExeReport.generatedCBytes,
        objectEmissionPath: directMachOExeReport.objectBackend.objectEmission.path,
      },
      windows: {
        compiler: directCoffExeReport.compiler,
        generatedCBytes: directCoffExeReport.generatedCBytes,
        objectEmissionPath: directCoffExeReport.objectBackend.objectEmission.path,
      },
    },
  },
  targets: {
    host: targets.host,
    count: targets.targets.length,
  },
  generatedCAudit: null,
};

writeFileSync(join(outDir, "summary.json"), `${JSON.stringify(report, null, 2)}\n`);
finishAggregateAssert(assert, { suite: "command contract snapshots", reportPath: join(outDir, "failures.json") });
console.log("command contract snapshots ok");
