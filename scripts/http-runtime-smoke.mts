#!/usr/bin/env -S node --experimental-strip-types --disable-warning=ExperimentalWarning
import assert from "node:assert/strict";
import { execFile, spawn } from "node:child_process";
import { createServer as createHttpServer, request as createHttpRequest } from "node:http";
import { createServer as createHttpsServer } from "node:https";
import { connect as connectTcp, createServer as createTcpServer } from "node:net";
import { cp, mkdir, mkdtemp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import { basename } from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const zero = "bin/zero";
const outDir = ".zero/native-test";
const zeroRunTimeoutMs = 20000;
// Must match Z_HTTP_LISTEN_RESPONSE_LIMIT in native/zero-c/src/http_listen_runner.c.
const httpListenResponseLimit = 65536;
const target =
  process.platform === "darwin" && process.arch === "arm64" ? "darwin-arm64" :
  process.platform === "linux" && process.arch === "x64" ? "linux-x64" :
  null;

type RawZeroListenerRequestOptions = {
  end?: boolean;
  timeoutMs?: number;
};

function zeroArray(count) {
  return `0_u8; ${count}`;
}

async function canLinkCurl() {
  const src = `/tmp/zero-http-curl-link-${process.pid}.c`;
  const exe = `/tmp/zero-http-curl-link-${process.pid}`;
  await writeFile(src, "#include <curl/curl.h>\nint main(void) { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : 1; }\n");
  try {
    await execFileAsync("cc", [src, "-lcurl", "-o", exe]);
    return true;
  } catch {
    return false;
  } finally {
    await rm(src, { force: true });
    await rm(exe, { force: true });
  }
}

async function canRunOpenSsl() {
  try {
    await execFileAsync("openssl", ["version"]);
    return true;
  } catch {
    return false;
  }
}

async function assertProviderUnavailableRuntime() {
  const src = `/tmp/zero-http-provider-unavailable-${process.pid}.c`;
  const exe = `/tmp/zero-http-provider-unavailable-${process.pid}`;
  await writeFile(src, `#include "zero_runtime.h"

int main(void) {
  const unsigned char request[] = "GET http://127.0.0.1/\\n\\n";
  unsigned char response[64] = {0};
  uint64_t result = zero_http_fetch_result(
    (ZeroByteView){request, sizeof(request) - 1},
    (ZeroMutByteView){response, sizeof(response)},
    10000000
  );
  return zero_http_result_ok(result) == 0 &&
         zero_http_result_status(result) == 0 &&
         zero_http_result_body_len(result) == 0 &&
         zero_http_result_error(result) == ZERO_HTTP_PROVIDER_UNAVAILABLE &&
         zero_http_response_len((ZeroByteView){response, sizeof(response)}) == 0 &&
         zero_http_response_body_offset((ZeroByteView){response, sizeof(response)}) == ZERO_HTTP_RESPONSE_META_BYTES ? 0 : 1;
}
`);
  try {
    await execFileAsync("cc", [
      "-DZERO_RUNTIME_NO_CURL",
      "-Inative/zero-c/include",
      "native/zero-c/runtime/zero_runtime.c",
      "native/zero-c/runtime/zero_http_curl.c",
      src,
      "-o",
      exe,
    ]);
    await execFileAsync(exe, [], { timeout: 5000 });
  } finally {
    await rm(src, { force: true });
    await rm(exe, { force: true });
  }
}

async function createTlsFixture() {
  const dir = await mkdtemp("/tmp/zero-http-tls-");
  const caKey = `${dir}/ca.key`;
  const caCert = `${dir}/ca.pem`;
  const serverKey = `${dir}/server.key`;
  const serverCsr = `${dir}/server.csr`;
  const serverCert = `${dir}/server.pem`;
  const serverConf = `${dir}/server.cnf`;
  await writeFile(serverConf, `[req]
distinguished_name = req_distinguished_name
req_extensions = v3_req
prompt = no
[req_distinguished_name]
CN = localhost
[v3_req]
subjectAltName = @alt_names
[alt_names]
DNS.1 = localhost
IP.1 = 127.0.0.1
`);
  try {
    await execFileAsync("openssl", ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-keyout", caKey, "-out", caCert, "-subj", "/CN=Zero Test CA"]);
    await execFileAsync("openssl", ["req", "-newkey", "rsa:2048", "-nodes", "-keyout", serverKey, "-out", serverCsr, "-subj", "/CN=localhost", "-config", serverConf]);
    await execFileAsync("openssl", ["x509", "-req", "-in", serverCsr, "-CA", caCert, "-CAkey", caKey, "-CAcreateserial", "-out", serverCert, "-days", "1", "-sha256", "-extfile", serverConf, "-extensions", "v3_req"]);
    return { dir, caCert, serverKey, serverCert };
  } catch {
    await rm(dir, { recursive: true, force: true });
    return null;
  }
}

function listen(server) {
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      server.off("error", reject);
      resolve(server.address().port);
    });
  });
}

function listenOn(server, port) {
  return new Promise<void>((resolve, reject) => {
    const onError = (error) => {
      server.off("listening", onListening);
      reject(error);
    };
    const onListening = () => {
      server.off("error", onError);
      resolve();
    };
    server.once("error", onError);
    server.once("listening", onListening);
    server.listen(port, "127.0.0.1");
  });
}

function close(server) {
  return new Promise<void>((resolve, reject) => {
    server.close((error) => error ? reject(error) : resolve());
  });
}

function waitForZeroListener(child, timeoutMs = 30000) {
  return new Promise<{ port: number; stdout: string; stderr: string }>((resolve, reject) => {
    let stdout = "";
    let stderr = "";
    const maybeReady = () => {
      const match = `${stdout}\n${stderr}`.match(/listening on http:\/\/127\.0\.0\.1:(\d+)/);
      if (match) {
        clearTimeout(timer);
        resolve({ port: Number(match[1]), stdout, stderr });
      }
    };
    const timer = setTimeout(() => {
      reject(new Error(`timed out waiting for zero listener\nstdout:\n${stdout}\nstderr:\n${stderr}`));
    }, timeoutMs);

    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString("utf8");
      maybeReady();
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString("utf8");
      maybeReady();
    });
    child.once("exit", (code, signal) => {
      clearTimeout(timer);
      reject(new Error(`zero listener exited before ready: code=${code} signal=${signal}\nstdout:\n${stdout}\nstderr:\n${stderr}`));
    });
  });
}

function stopZeroListener(child) {
  return new Promise<void>((resolve) => {
    if (child.exitCode !== null || child.signalCode !== null) {
      resolve();
      return;
    }
    const killTimer = setTimeout(() => {
      if (child.exitCode === null && child.signalCode === null) child.kill("SIGKILL");
    }, 1000);
    child.once("exit", () => {
      clearTimeout(killTimer);
      resolve();
    });
    child.kill("SIGTERM");
  });
}

function requestZeroListener(port, method, path, body = "", headers = {}) {
  const bodyBuffer = Buffer.from(body);
  return new Promise<{ status: number; body: string }>((resolve, reject) => {
    const timeout = setTimeout(() => {
      request.destroy(new Error(`${method} ${path} timed out`));
    }, 5000);
    const request = createHttpRequest({
      host: "127.0.0.1",
      port,
      path,
      method,
      headers: {
        ...headers,
        ...(bodyBuffer.length > 0 ? { "content-length": String(bodyBuffer.length) } : {}),
      },
    }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => {
        clearTimeout(timeout);
        resolve({
          status: response.statusCode ?? 0,
          body: Buffer.concat(chunks).toString("utf8"),
        });
      });
    });
    request.on("error", (error) => {
      clearTimeout(timeout);
      reject(new Error(`${method} ${path} failed: ${error.message}`));
    });
    if (bodyBuffer.length > 0) request.write(bodyBuffer);
    request.end();
  });
}

function rawZeroListenerRequest(port, payload, options: RawZeroListenerRequestOptions = {}) {
  return new Promise<string>((resolve, reject) => {
    const chunks = [];
    let settled = false;
    const socket = connectTcp({ host: "127.0.0.1", port }, () => {
      if (options.end === false) socket.write(payload);
      else socket.end(payload);
    });
    const timeout = setTimeout(() => {
      finish(new Error("raw HTTP request timed out"));
    }, options.timeoutMs ?? 5000);
    function finish(error = null) {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      socket.destroy();
      if (error) {
        reject(error);
        return;
      }
      resolve(Buffer.concat(chunks).toString("utf8"));
    }
    socket.on("data", (chunk) => chunks.push(chunk));
    socket.on("end", () => finish());
    socket.on("close", () => {
      if (!settled && chunks.length > 0) finish();
    });
    socket.on("error", finish);
  });
}

async function assertExplicitPortConflict() {
  const dir = await mkdtemp("/tmp/zero-http-listen-explicit-");
  const packageDir = `${dir}/ping-pong-api`;
  const patchPath = `${dir}/explicit-port.patch`;
  try {
    await cp("examples/ping-pong-api", packageDir, { recursive: true });
    await writeFile(patchPath, `zero-program-graph-patch v1
replaceFunctionBody main
  check std.http.listen(world, 3000_u16)
end
`);
    await execFileAsync(zero, ["patch", packageDir, patchPath], { timeout: zeroRunTimeoutMs });
    const result = await execFileAsync(zero, ["run", packageDir], { timeout: zeroRunTimeoutMs }).catch((error) => error);
    assert.notEqual(result.code, 0);
    assert.match(`${result.stdout ?? ""}\n${result.stderr ?? ""}`, /std\.http\.listen could not bind the requested port|Address already in use/);
  } finally {
    await rm(dir, { recursive: true, force: true });
  }
}

async function createHttpListenFixture() {
  const dir = await mkdtemp("/tmp/zero-http-listen-capacity-");
  const packageDir = `${dir}/ping-pong-api`;
  const patchPath = `${dir}/capacity.patch`;
  const eightKiBBody = "x".repeat(8192);
  try {
    await cp("examples/ping-pong-api", packageDir, { recursive: true });
    await writeFile(patchPath, `zero-program-graph-patch v1
replaceFunctionBody handle
  if std.http.requestIsGet(request, "/large8k") {
    return std.http.writeTextOk(response, "${eightKiBBody}")
  }
  if std.http.requestIsGet(request, "/too-large") {
    let body: Maybe<Span<u8>> = std.str.repeat(response, "x", ${httpListenResponseLimit})
    if body.has {
      return std.http.writeTextOk(response, body.value)
    }
    return null
  }
  if std.http.requestIsGet(request, "/ping") {
    return std.http.writeJsonOk(response, "{\\"message\\":\\"pong\\"}")
  }
  if std.http.requestIsGet(request, "/health") {
    return std.http.writeJsonOk(response, "{\\"ok\\":true}")
  }
  if std.http.requestIsPost(request, "/echo") {
    let body: Maybe<Span<u8>> = std.http.requestJsonBodyWithin(request, 128)
    if body.has {
      return std.http.writeJsonCreated(response, body.value)
    }
    return std.http.writeJsonBadRequest(response, "{\\"error\\":\\"bad_request\\"}")
  }
  return std.http.writeJsonNotFound(response, "{\\"error\\":\\"not_found\\"}")
end
`);
    await execFileAsync(zero, ["patch", packageDir, patchPath], { timeout: zeroRunTimeoutMs, maxBuffer: 2 * 1024 * 1024 });
    return { dir, packageDir };
  } catch (error) {
    await rm(dir, { recursive: true, force: true });
    throw error;
  }
}

async function runHttpListenExample() {
  const tempDirsBefore = new Set((await readdir("/tmp")).filter((entry) => entry.startsWith("zero-listen-")));
  let fixture = null;
  let reserved3000 = null;
  let port3000Occupied = false;
  const devPortGuard = createTcpServer();
  let child = null;
  try {
    fixture = await createHttpListenFixture();
    try {
      await listenOn(devPortGuard, 3000);
      reserved3000 = devPortGuard;
      port3000Occupied = true;
    } catch (error) {
      try {
        devPortGuard.close();
      } catch {
        // The guard may never have started if another local service owns 3000.
      }
      if (error && error.code === "EADDRINUSE") {
        port3000Occupied = true;
      } else {
        throw error;
      }
    }

    child = spawn(zero, ["run", fixture.packageDir], {
      stdio: ["ignore", "pipe", "pipe"],
    });
    const { port } = await waitForZeroListener(child);
    if (port3000Occupied) {
      assert.notEqual(port, 3000);
    }
    assert.deepEqual(await requestZeroListener(port, "GET", "/ping"), {
      status: 200,
      body: "{\"message\":\"pong\"}",
    });
    const largeResponse = await requestZeroListener(port, "GET", "/large8k");
    assert.equal(largeResponse.status, 200);
    assert.equal(largeResponse.body, "x".repeat(8192));
    const tooLargeResponse = await requestZeroListener(port, "GET", "/too-large");
    assert.deepEqual(tooLargeResponse, {
      status: 500,
      body: "{\"error\":\"response_unavailable\"}",
    });
    assert(!tooLargeResponse.body.includes("x"), "over-limit listener response must not leak a partial body");
    assert.deepEqual(await requestZeroListener(port, "GET", "/ping"), {
      status: 200,
      body: "{\"message\":\"pong\"}",
    });
    assert.deepEqual(await requestZeroListener(port, "GET", "/health"), {
      status: 200,
      body: "{\"ok\":true}",
    });
    assert.deepEqual(await requestZeroListener(port, "GET", "/missing"), {
      status: 404,
      body: "{\"error\":\"not_found\"}",
    });
    assert.deepEqual(await requestZeroListener(port, "POST", "/echo", "{\"ping\":1}", { "content-type": "application/json" }), {
      status: 201,
      body: "{\"ping\":1}",
    });
    assert.deepEqual(await requestZeroListener(port, "POST", "/echo", "not-json"), {
      status: 400,
      body: "{\"error\":\"bad_request\"}",
    });
    const badRequest = await rawZeroListenerRequest(port, "POST /echo\r\ncontent-length: nope\r\n\r\nx");
    assert.match(badRequest, /^HTTP\/1\.1 400 Bad Request/);
    assert.match(badRequest, /\r\nconnection: close\r\n/i);
    assert.match(badRequest, /\{"error":"bad_request"\}/);
    const tooLarge = await rawZeroListenerRequest(port, "POST /echo\r\ncontent-length: 999999\r\n\r\n");
    assert.match(tooLarge, /^HTTP\/1\.1 413 Payload Too Large/);
    assert.match(tooLarge, /\{"error":"payload_too_large"\}/);
    const incomplete = await rawZeroListenerRequest(port, "POST /echo\r\ncontent-length: 4\r\n\r\nx", { end: false, timeoutMs: 8000 });
    assert.match(incomplete, /^HTTP\/1\.1 408 Request Timeout/);
    assert.match(incomplete, /\{"error":"request_timeout"\}/);
    if (port3000Occupied) await assertExplicitPortConflict();
  } finally {
    if (child) await stopZeroListener(child);
    if (reserved3000) await close(reserved3000);
    const tempDirsAfter = (await readdir("/tmp")).filter((entry) => entry.startsWith("zero-listen-"));
    if (fixture) await rm(fixture.dir, { recursive: true, force: true });
    for (const entry of tempDirsAfter) {
      assert(tempDirsBefore.has(entry), `std.http.listen leaked temporary directory /tmp/${entry}`);
    }
  }
}

async function runExitCode(path, env = {}) {
  try {
    await execFileAsync(path, [], { timeout: 5000, env: { ...process.env, ...env } });
    return 0;
  } catch (error) {
    if (typeof error.code === "number") return error.code;
    throw error;
  }
}

async function importGraphInput(sourcePath) {
  const graphPath = `/tmp/zero-http-runtime-${process.pid}-${basename(sourcePath, ".0")}.graph`;
  const projectionSidecar = `${sourcePath.slice(0, -2)}.graph`;
  await rm(projectionSidecar, { force: true });
  await rm(graphPath, { force: true });
  await execFileAsync(zero, ["import", "--format", "text", "--out", graphPath, sourcePath]);
  return graphPath;
}

function assertRuntimeReport(report, targetName) {
  assert.equal(report.generatedCBytes, 0);
  assert.equal(report.objectBackend?.linking?.targetLibraries, "zero-runtime,curl");
  assert.equal(report.objectBackend?.linking?.externalToolchain, "cc");
  assert.equal(report.objectBackend?.httpRuntime?.status, "supported");
  assert.equal(report.objectBackend?.httpRuntime?.provider, "curl");
  assert.equal(report.objectBackend?.httpRuntime?.tlsVerification, true);
  assert.equal(report.objectBackend?.httpRuntime?.customCa?.env, "ZERO_HTTP_TEST_CA_BUNDLE");
  assert(report.objectBackend?.linkerPlan?.staticLibraries?.includes("zero_runtime.o"));
  assert(report.objectBackend?.linkerPlan?.staticLibraries?.includes("zero_http_curl.o"));
  assert(report.objectBackend?.linkerPlan?.systemLibraries?.includes("curl"));
  assert.equal(report.objectBackend?.directFacts?.runtimeHelperCount, 2);
  if (targetName === "darwin-arm64") {
    assert.equal(report.objectBackend?.objectEmission?.path, "direct-macho64-object");
  } else {
    assert.equal(report.objectBackend?.objectEmission?.path, "direct-elf64-object");
  }
}

async function buildAndRun(name, source, expectedCode, env = {}) {
  const src = `/tmp/zero-http-runtime-${process.pid}-${name}.0`;
  const exe = `${outDir}/${name}`;
  const jsonPath = `${exe}.json`;
  await writeFile(src, source);
  const input = await importGraphInput(src);
  const build = await execFileAsync(zero, ["build", "--json", "--emit", "exe", "--target", target, input, "--out", exe], { maxBuffer: 1024 * 1024 });
  await writeFile(jsonPath, build.stdout);
  assertRuntimeReport(JSON.parse(build.stdout), target);
  const code = await runExitCode(exe, env);
  assert.equal(code, expectedCode);
}

async function runHttpJsonExample(baseUrl) {
  const exe = `${outDir}/std-http-json-example`;
  const run = await execFileAsync(zero, ["run", "--out", exe, "examples/std-http-json.graph", "--", `GET ${baseUrl}/ok\n\n`], { timeout: zeroRunTimeoutMs });
  assert.equal(run.stdout, "http json ok\n");
}

async function runHttpRequestExample(baseUrl) {
  const exe = `${outDir}/std-http-request-example`;
  const request = `POST ${baseUrl}/echo\ncontent-type: application/json\nx-zero-test: yes\n\n{"ping":1}`;
  const run = await execFileAsync(zero, ["run", "--out", exe, "examples/std-http-request.graph", "--", request], { timeout: zeroRunTimeoutMs });
  assert.equal(run.stdout, "http request ok\n");
}

async function runHttpHeadersExample(baseUrl) {
  const exe = `${outDir}/std-http-headers-example`;
  const run = await execFileAsync(zero, ["run", "--out", exe, "examples/std-http-headers.graph", "--", `GET ${baseUrl}/headers\n\n`, "connection"], { timeout: zeroRunTimeoutMs });
  assert.equal(run.stdout, "http header found\n");
}

async function runJsonApiClientExample(baseUrl) {
  const exe = `${outDir}/json-api-client-example`;
  const run = await execFileAsync(zero, ["run", "--out", exe, "examples/json-api-client.graph", "--", `${baseUrl}/client`], { timeout: zeroRunTimeoutMs });
  assert.equal(run.stdout, "json api client ok\n");
}

async function runJsonApiRouterExample() {
  const exe = `${outDir}/json-api-router-example`;
  const request = "POST /users?tenant=demo\ncontent-type: application/json\n\n{\"id\":7}";
  const run = await execFileAsync(zero, ["run", "--out", exe, "examples/json-api-router.graph", "--", request], { timeout: zeroRunTimeoutMs });
  assert.equal(run.stdout, "json api router ok\n");
  const corsExe = `${outDir}/json-api-router-cors-example`;
  const preflight = "OPTIONS /users\naccess-control-request-method: POST\n\n";
  const cors = await execFileAsync(zero, ["run", "--out", corsExe, "examples/json-api-router.graph", "--", preflight], { timeout: zeroRunTimeoutMs });
  assert.equal(cors.stdout, "json api router ok\n");
}

function okSource(baseUrl) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [512]u8 = [${zeroArray(512)}]
    let result: HttpResult = std.http.fetch(client, std.mem.span("GET ${baseUrl}/ok\\n\\n"), response, std.time.ms(1000))
    let body_offset: usize = std.http.responseBodyOffset(response)
    let body_len: usize = std.http.resultBodyLen(result)
    if std.http.resultOk(result) == false {
        return 99
    }
    if std.http.resultStatus(result) != 200 {
        return 99
    }
    if body_len != 8 {
        return 99
    }
    if std.http.resultError(result) != std.http.errorNone() {
        return 99
    }
    if std.http.responseLen(response) < body_len {
        return 99
    }
    if response[body_offset] != 123_u8 {
        return 99
    }
    if response[body_offset + 1] != 34_u8 {
        return 99
    }
    if response[body_offset + 2] != 111_u8 {
        return 99
    }
    if response[body_offset + 3] != 107_u8 {
        return 99
    }
    if response[body_offset + 4] != 34_u8 {
        return 99
    }
    if response[body_offset + 5] != 58_u8 {
        return 99
    }
    if response[body_offset + 6] != 49_u8 {
        return 99
    }
    if response[body_offset + 7] != 125_u8 {
        return 99
    }
    return 8
}
`;
}

function fetchRequestSource(baseUrl, method, path, body, expectedStatus, expectedBodyLen, expectedCode) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    let request: Span<u8> = std.mem.span("${method} ${baseUrl}${path}\\ncontent-type: application/json\\nx-zero-test: yes\\n\\n${body}")
    var response: [512]u8 = [${zeroArray(512)}]
    let result: HttpResult = std.http.fetch(client, request, response, std.time.ms(1000))
    let body_offset: usize = std.http.responseBodyOffset(response)
    if std.http.resultOk(result) == false {
        return 99
    }
    if std.http.resultStatus(result) != ${expectedStatus} {
        return 99
    }
    if std.http.resultBodyLen(result) != ${expectedBodyLen} {
        return 99
    }
    if std.http.resultError(result) != std.http.errorNone() {
        return 99
    }
    if response[body_offset] != 123_u8 {
        return 99
    }
    if response[body_offset + 2] == 0_u8 {
        return 99
    }
    return ${expectedCode}
}
`;
}

function resultFailureSource(baseUrl, path, responseSize, timeoutMs, expectedStatus, expectedLen, expectedError, expectedCode) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [${responseSize}]u8 = [${zeroArray(responseSize)}]
    let result: HttpResult = std.http.fetch(client, std.mem.span("GET ${baseUrl}${path}\\n\\n"), response, std.time.ms(${timeoutMs}))
    if std.http.resultOk(result) != false {
        return 99
    }
    if std.http.resultStatus(result) != ${expectedStatus} {
        return 99
    }
    if std.http.resultBodyLen(result) != ${expectedLen} {
        return 99
    }
    if std.http.resultError(result) != ${expectedError} {
        return 99
    }
    return ${expectedCode}
}
`;
}

function headersSource(baseUrl) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [512]u8 = [${zeroArray(512)}]
    let result: HttpResult = std.http.fetch(client, std.mem.span("GET ${baseUrl}/ok\\n\\n"), response, std.time.ms(1000))
    let reply: HttpHeaderValue = std.http.headerValue(response, std.mem.span("x-zero-reply"))
    let reply_bytes: Maybe<Span<u8>> = std.http.headerBytes(response, reply)
    if std.http.resultOk(result) == false {
        return 99
    }
    if std.http.resultStatus(result) != 200 {
        return 99
    }
    if std.http.responseHeadersLen(response) <= 16 {
        return 99
    }
    if std.http.resultError(result) != std.http.errorNone() {
        return 99
    }
    if std.http.headerFound(reply) == false {
        return 99
    }
    if std.http.headerLen(reply) != 3 {
        return 99
    }
    if !reply_bytes.has {
        return 99
    }
    if reply_bytes.value[0] != 121_u8 {
        return 99
    }
    if response[24] != 72_u8 {
        return 99
    }
    if response[25] != 84_u8 {
        return 99
    }
    if response[26] != 84_u8 {
        return 99
    }
    if response[27] != 80_u8 {
        return 99
    }
    return 44
}
`;
}

function interimHeadersSource(baseUrl) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [512]u8 = [${zeroArray(512)}]
    let result: HttpResult = std.http.fetch(client, std.mem.span("GET ${baseUrl}/interim\\n\\n"), response, std.time.ms(1000))
    let reply: HttpHeaderValue = std.http.headerValue(response, std.mem.span("x-zero-reply"))
    let reply_offset: usize = std.http.headerOffset(reply)
    let body_offset: usize = std.http.responseBodyOffset(response)
    if std.http.resultOk(result) == false {
        return 99
    }
    if std.http.resultStatus(result) != 200 {
        return 99
    }
    if std.http.resultBodyLen(result) != 8 {
        return 99
    }
    if std.http.resultError(result) != std.http.errorNone() {
        return 99
    }
    if std.http.headerFound(reply) == false {
        return 99
    }
    if std.http.headerLen(reply) != 5 {
        return 99
    }
    if response[reply_offset] != 102_u8 {
        return 99
    }
    if response[33] != 50_u8 {
        return 99
    }
    if response[34] != 48_u8 {
        return 99
    }
    if response[35] != 48_u8 {
        return 99
    }
    if response[body_offset] != 123_u8 {
        return 99
    }
    return 46
}
`;
}

function headSource(baseUrl) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    let request: Span<u8> = std.mem.span("HEAD ${baseUrl}/ok\\nx-zero-test: yes\\n\\n")
    var response: [512]u8 = [${zeroArray(512)}]
    let result: HttpResult = std.http.fetch(client, request, response, std.time.ms(1000))
    let reply: HttpHeaderValue = std.http.headerValue(response, std.mem.span("x-zero-reply"))
    if std.http.resultOk(result) == false {
        return 99
    }
    if std.http.resultStatus(result) != 200 {
        return 99
    }
    if std.http.resultBodyLen(result) != 0 {
        return 99
    }
    if std.http.responseHeadersLen(response) <= 16 {
        return 99
    }
    if std.http.resultError(result) != std.http.errorNone() {
        return 99
    }
    if std.http.headerFound(reply) == false {
        return 99
    }
    if std.http.headerLen(reply) != 3 {
        return 99
    }
    return 45
}
`;
}

function jsonResultSource(baseUrl) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [512]u8 = [${zeroArray(512)}]
    let result: HttpResult = std.http.fetch(client, std.mem.span("GET ${baseUrl}/ok\\n\\n"), response, std.time.ms(1000))
    let bytes: Maybe<Span<u8>> = std.http.responseBody(response, result)
    var arena_buf: [16]u8 = [${zeroArray(16)}]
    var arena: FixedBufAlloc = std.mem.fixedBufAlloc(arena_buf)
    var parsed: Maybe<JsonDoc> = null
    if bytes.has {
        parsed = std.json.parseBytes(arena, bytes.value)
    }
    if std.http.resultOk(result) == false {
        return 99
    }
    if std.http.resultStatus(result) != 200 {
        return 99
    }
    if !bytes.has {
        return 99
    }
    if std.mem.len(bytes.value) != 8 {
        return 99
    }
    if parsed.has == false {
        return 99
    }
    if std.json.validateBytes(bytes.value) == false {
        return 99
    }
    if std.json.streamTokensBytes(bytes.value) != 3 {
        return 99
    }
    return 37
}
`;
}

function invalidUrlSource() {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [64]u8 = [${zeroArray(64)}]
    let result: HttpResult = std.http.fetch(client, std.mem.span("GET http://\\n\\n"), response, std.time.ms(1000))
    if std.http.resultOk(result) != false {
        return 99
    }
    if std.http.resultStatus(result) != 0 {
        return 99
    }
    if std.http.resultBodyLen(result) != 0 {
        return 99
    }
    if std.http.resultError(result) != std.http.errorInvalidUrl() {
        return 99
    }
    return 36
}
`;
}

function shorthandRejectedSource(baseUrl) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [64]u8 = [${zeroArray(64)}]
    let result: HttpResult = std.http.fetch(client, std.mem.span("${baseUrl}/ok"), response, std.time.ms(1000))
    if std.http.resultOk(result) != false {
        return 99
    }
    if std.http.resultStatus(result) != 0 {
        return 99
    }
    if std.http.resultBodyLen(result) != 0 {
        return 99
    }
    if std.http.resultError(result) != std.http.errorInvalidRequest() {
        return 99
    }
    return 48
}
`;
}

function unterminatedEnvelopeSource(baseUrl) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [64]u8 = [${zeroArray(64)}]
    let result: HttpResult = std.http.fetch(client, std.mem.span("GET ${baseUrl}/ok"), response, std.time.ms(1000))
    if std.http.resultOk(result) != false {
        return 99
    }
    if std.http.resultStatus(result) != 0 {
        return 99
    }
    if std.http.resultBodyLen(result) != 0 {
        return 99
    }
    if std.http.resultError(result) != std.http.errorInvalidRequest() {
        return 99
    }
    return 49
}
`;
}

function invalidRequestSource(baseUrl) {
  return `export c fn main() -> i32 {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [128]u8 = [${zeroArray(128)}]
    let request: Span<u8> = std.mem.span("POST ${baseUrl}/echo\\nbad-header\\n\\n")
    let result: HttpResult = std.http.fetch(client, request, response, std.time.ms(1000))
    if std.http.resultOk(result) != false {
        return 99
    }
    if std.http.resultStatus(result) != 0 {
        return 99
    }
    if std.http.resultBodyLen(result) != 0 {
        return 99
    }
    if std.http.resultError(result) != std.http.errorInvalidRequest() {
        return 99
    }
    return 43
}
`;
}

if (!target) {
  process.stdout.write("http runtime smoke skipped: unsupported host target\n");
  process.exit(0);
}

if (!(await canLinkCurl())) {
  process.stdout.write("http runtime smoke skipped: cc cannot link libcurl\n");
  process.exit(0);
}

await mkdir(outDir, { recursive: true });
await assertProviderUnavailableRuntime();
await runHttpListenExample();

function handleRequest(request, response) {
  if (request.url === "/headers") {
    response.sendDate = false;
    response.writeHead(204, { "connection": "close" });
    response.end();
  } else if (request.url === "/ok") {
    response.writeHead(200, { "content-type": "application/json", "x-zero-reply": "yes" });
    response.end("{\"ok\":1}");
  } else if (request.url === "/echo") {
    const chunks = [];
    request.on("data", (chunk) => chunks.push(chunk));
    request.on("end", () => {
      const body = Buffer.concat(chunks).toString("utf8");
      if (request.method === "POST" &&
          request.headers["x-zero-test"] === "yes" &&
          request.headers["content-type"] === "application/json" &&
          body === "{\"ping\":1}") {
        response.writeHead(201, { "content-type": "application/json", "x-zero-reply": "yes" });
        response.end("{\"echo\":1}");
      } else {
        response.writeHead(400, { "content-type": "text/plain" });
        response.end("bad request");
      }
    });
  } else if (request.url === "/client") {
    const chunks = [];
    request.on("data", (chunk) => chunks.push(chunk));
    request.on("end", () => {
      const body = Buffer.concat(chunks).toString("utf8");
      if (request.method === "POST" &&
          request.headers["content-type"] === "application/json" &&
          body === "{\"ping\":1}") {
        response.writeHead(201, { "content-type": "application/json", "x-zero-reply": "yes" });
        response.end("{\"echo\":1}");
      } else {
        response.writeHead(400, { "content-type": "text/plain" });
        response.end("bad request");
      }
    });
  } else if (request.url === "/replace") {
    const chunks = [];
    request.on("data", (chunk) => chunks.push(chunk));
    request.on("end", () => {
      const body = Buffer.concat(chunks).toString("utf8");
      if (request.method === "PUT" &&
          request.headers["x-zero-test"] === "yes" &&
          request.headers["content-type"] === "application/json" &&
          body === "{\"value\":2}") {
        response.writeHead(202, { "content-type": "application/json", "x-zero-reply": "yes" });
        response.end("{\"put\":1}");
      } else {
        response.writeHead(400, { "content-type": "text/plain" });
        response.end("bad request");
      }
    });
  } else if (request.url === "/missing") {
    response.writeHead(404, { "content-type": "text/plain" });
    response.end("missing");
  } else if (request.url === "/large") {
    response.writeHead(200, { "content-type": "text/plain" });
    response.end("0123456789abcdef0123456789abcdef");
  } else if (request.url === "/slow") {
    setTimeout(() => {
      response.writeHead(200, { "content-type": "text/plain" });
      response.end("slow");
    }, 250);
  } else {
    response.writeHead(500, { "content-type": "text/plain" });
    response.end("unexpected");
  }
}

function createInterimHeaderServer() {
  return createTcpServer((socket) => {
    socket.once("data", () => {
      socket.end([
        "HTTP/1.1 103 Early Hints\r\n",
        "Link: </zero.css>; rel=preload; as=style\r\n",
        "\r\n",
        "HTTP/1.1 200 OK\r\n",
        "Content-Type: application/json\r\n",
        "X-Zero-Reply: final\r\n",
        "Connection: close\r\n",
        "Content-Length: 8\r\n",
        "\r\n",
        "{\"ok\":1}",
      ].join(""));
    });
    socket.on("error", () => {});
  });
}

const server = createHttpServer(handleRequest);
const port = await listen(server);
const baseUrl = `http://127.0.0.1:${port}`;
const interimServer = createInterimHeaderServer();
const interimPort = await listen(interimServer);
const interimBaseUrl = `http://127.0.0.1:${interimPort}`;

try {
  await buildAndRun("http-runtime-ok", okSource(baseUrl), 8);
  await buildAndRun("http-runtime-404", resultFailureSource(baseUrl, "/missing", 512, 1000, 404, 7, "std.http.errorNone()", 32), 32);
  await buildAndRun("http-runtime-overflow", resultFailureSource(baseUrl, "/large", 32, 1000, 200, 0, "std.http.errorTooLarge()", 33), 33);
  await buildAndRun("http-runtime-timeout", resultFailureSource(baseUrl, "/slow", 512, 25, 0, 0, "std.http.errorTimeout()", 34), 34);
  await buildAndRun("http-runtime-post", fetchRequestSource(baseUrl, "POST", "/echo", "{\\\"ping\\\":1}", 201, 10, 42), 42);
  await buildAndRun("http-runtime-put", fetchRequestSource(baseUrl, "PUT", "/replace", "{\\\"value\\\":2}", 202, 9, 47), 47);
  await buildAndRun("http-runtime-headers", headersSource(baseUrl), 44);
  await buildAndRun("http-runtime-interim-headers", interimHeadersSource(interimBaseUrl), 46);
  await buildAndRun("http-runtime-head", headSource(baseUrl), 45);
  await buildAndRun("http-runtime-result-json", jsonResultSource(baseUrl), 37);
  await runHttpJsonExample(baseUrl);
  await runHttpRequestExample(baseUrl);
  await runHttpHeadersExample(baseUrl);
  await runJsonApiClientExample(baseUrl);
  await runJsonApiRouterExample();
  await buildAndRun("http-runtime-shorthand-rejected", shorthandRejectedSource(baseUrl), 48);
  await buildAndRun("http-runtime-unterminated-envelope", unterminatedEnvelopeSource(baseUrl), 49);
  await buildAndRun("http-runtime-invalid-url", invalidUrlSource(), 36);
  await buildAndRun("http-runtime-invalid-request", invalidRequestSource(baseUrl), 43);
} finally {
  await close(server);
  await close(interimServer);
}

if (await canRunOpenSsl()) {
  const tls = await createTlsFixture();
  if (tls) {
    const httpsServer = createHttpsServer({
      key: await readFile(tls.serverKey),
      cert: await readFile(tls.serverCert),
    }, handleRequest);
    const httpsPort = await listen(httpsServer);
    const httpsBaseUrl = `https://127.0.0.1:${httpsPort}`;
    try {
      await buildAndRun("http-runtime-https-ok", okSource(httpsBaseUrl), 8, { ZERO_HTTP_TEST_CA_BUNDLE: tls.caCert });
      await buildAndRun("http-runtime-https-untrusted", resultFailureSource(httpsBaseUrl, "/ok", 512, 1000, 0, 0, "std.http.errorTls()", 35), 35);
    } finally {
      await close(httpsServer);
      await rm(tls.dir, { recursive: true, force: true });
    }
  } else {
    process.stdout.write("https runtime smoke skipped: openssl fixture generation failed\n");
  }
} else {
  process.stdout.write("https runtime smoke skipped: openssl unavailable\n");
}

const okReport = JSON.parse(await readFile(`${outDir}/http-runtime-ok.json`, "utf8"));
assertRuntimeReport(okReport, target);
process.stdout.write(`http runtime smoke ok (${target})\n`);
