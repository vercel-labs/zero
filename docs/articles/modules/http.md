## When To Use std.http

In Zerolang, use `std.http` for HTTP request parsing, response envelope writing, hosted
fetch, local listen support, and web API helpers.

Runnable today:

| API | Return | Notes |
| --- | --- | --- |
| `std.http.parseMethod(text)` | `HttpMethod` | Parses a small HTTP method token. |
| `std.http.client(net)` | `HttpClient` | Creates hosted client metadata from a network capability. |
| `std.http.server(net, address)` | `HttpServer` | Creates hosted server metadata from a network capability and address. |
| `std.http.listen(world)` | `Void raises [Io]` | Starts a loopback HTTP listener from `zero run`, auto-selecting a development port from 3000 upward. Each serialized response is limited to 65,536 bytes. |
| `std.http.listen(world, port)` | `Void raises [Io]` | Starts a loopback HTTP listener on exactly `port`, failing if the port is occupied. Each serialized response is limited to 65,536 bytes. |
| `std.http.fetch(client, request, response, timeout)` | `HttpResult` | Performs a hosted HTTP(S) request with a `Duration` timeout and writes response metadata, headers, and body into caller-owned storage. |
| `std.http.resultOk(result)` | `Bool` | True when transport succeeded and the status is 2xx. |
| `std.http.resultStatus(result)` | `u16` | Reads the HTTP status, or `0` when no status was available. |
| `std.http.resultBodyLen(result)` | `usize` | Reads the number of response body bytes written into the response buffer. |
| `std.http.resultError(result)` | `HttpError` | Reads the transport/provider error. |
| `std.http.errorNone()` | `HttpError` | Transport succeeded. |
| `std.http.errorInvalidUrl()` | `HttpError` | The request URL was invalid. |
| `std.http.errorUnsupportedProtocol()` | `HttpError` | The URL protocol is not supported. |
| `std.http.errorDns()` | `HttpError` | DNS lookup failed. |
| `std.http.errorConnect()` | `HttpError` | Connecting to the remote host failed. |
| `std.http.errorTls()` | `HttpError` | TLS verification or connection setup failed. |
| `std.http.errorTimeout()` | `HttpError` | The request timed out. |
| `std.http.errorTooLarge()` | `HttpError` | The response did not fit in the caller-owned buffer. |
| `std.http.errorProviderUnavailable()` | `HttpError` | The hosted HTTP provider is unavailable. |
| `std.http.errorIo()` | `HttpError` | The provider reported an I/O failure. |
| `std.http.errorInvalidRequest()` | `HttpError` | The request envelope was invalid. |
| `std.http.errorName(error)` | `String` | Returns a stable label for a transport error. |
| `std.http.responseLen(response)` | `usize` | Reads the response byte count written after the internal metadata prefix. |
| `std.http.responseHeadersLen(response)` | `usize` | Reads the raw response header byte count. |
| `std.http.responseBodyOffset(response)` | `usize` | Reads the body start offset within the response buffer. |
| `std.http.headerValue(response, name)` | `HttpHeaderValue` | Locates a response header value by case-insensitive header name. |
| `std.http.headerFound(value)` | `Bool` | True when `headerValue` found a matching header. |
| `std.http.headerOffset(value)` | `usize` | Reads the header value byte offset within the response buffer. |
| `std.http.headerLen(value)` | `usize` | Reads the header value byte length. |
| `std.http.tlsBoundary()` | `String` | Names the platform or C-library TLS boundary. |
| `std.http.statusReason(status)` | `String` | Returns a common reason phrase for status-line writing. |
| `std.http.statusIsInformational(status)` | `Bool` | True for 1xx statuses. |
| `std.http.statusIsSuccess(status)` | `Bool` | True for 2xx statuses. |
| `std.http.statusIsRedirect(status)` | `Bool` | True for 3xx statuses. |
| `std.http.statusIsClientError(status)` | `Bool` | True for 4xx statuses. |
| `std.http.statusIsServerError(status)` | `Bool` | True for 5xx statuses. |
| `std.http.writeRequest(buffer, startLine, body)` | `Maybe<Span<u8>>` | Writes `METHOD URL`, optional content length, blank line, and body into caller storage. |
| `std.http.writeRequestWithHeader(buffer, startLine, headerLine, body)` | `Maybe<Span<u8>>` | Writes a request envelope with one validated `name: value` header line. |
| `std.http.writeRequestWithHeaders(buffer, startLine, headers, body)` | `Maybe<Span<u8>>` | Writes a request envelope with a validated newline-separated header block. |
| `std.http.writeMethodRequest(buffer, method, target, body)` | `Maybe<Span<u8>>` | Writes a request envelope from separate method and target spans. |
| `std.http.writeGetRequest(buffer, target)` | `Maybe<Span<u8>>` | Writes an empty GET request envelope. |
| `std.http.writeHeadRequest(buffer, target)` | `Maybe<Span<u8>>` | Writes an empty HEAD request envelope. |
| `std.http.writeDeleteRequest(buffer, target)` | `Maybe<Span<u8>>` | Writes an empty DELETE request envelope. |
| `std.http.writeJsonRequest(buffer, startLine, body)` | `Maybe<Span<u8>>` | Writes a JSON request envelope with `content-type` and `content-length`. |
| `std.http.writeJsonMethodRequest(buffer, method, target, body)` | `Maybe<Span<u8>>` | Writes a JSON request envelope from separate method and target spans. |
| `std.http.writeJsonRequestWithHeader(buffer, startLine, headerLine, body)` | `Maybe<Span<u8>>` | Writes a JSON request envelope with one validated extra header line. |
| `std.http.writeJsonRequestWithHeaders(buffer, startLine, headers, body)` | `Maybe<Span<u8>>` | Writes a JSON request envelope with a validated extra header block. |
| `std.http.writePostJsonRequest(buffer, target, body)` | `Maybe<Span<u8>>` | Writes a POST JSON request envelope. |
| `std.http.writePutJsonRequest(buffer, target, body)` | `Maybe<Span<u8>>` | Writes a PUT JSON request envelope. |
| `std.http.writePatchJsonRequest(buffer, target, body)` | `Maybe<Span<u8>>` | Writes a PATCH JSON request envelope. |
| `std.http.writeResponse(buffer, status, body)` | `Maybe<Span<u8>>` | Writes an HTTP/1.1 response envelope into caller storage. |
| `std.http.writeResponseWithHeader(buffer, status, headerLine, body)` | `Maybe<Span<u8>>` | Writes a response envelope with one validated `name: value` header line. |
| `std.http.writeResponseWithHeaders(buffer, status, headers, body)` | `Maybe<Span<u8>>` | Writes a response envelope with a validated newline-separated header block. |
| `std.http.writeJsonResponse(buffer, status, body)` | `Maybe<Span<u8>>` | Writes a JSON HTTP/1.1 response envelope into caller storage. |
| `std.http.writeJsonResponseWithHeader(buffer, status, headerLine, body)` | `Maybe<Span<u8>>` | Writes a JSON response envelope with one validated `name: value` header line. |
| `std.http.writeJsonResponseWithHeaders(buffer, status, headers, body)` | `Maybe<Span<u8>>` | Writes a JSON response envelope with a validated extra header block. |
| `std.http.writeJsonResponseWithCookie(buffer, status, cookie, body)` | `Maybe<Span<u8>>` | Writes a JSON response envelope with one `Set-Cookie` header value. |
| `std.http.writeJsonError(buffer, status, code)` | `Maybe<Span<u8>>` | Writes `{"error":"code"}` after validating the code is JSON-safe lower-case ASCII, digits, `_`, or `-`. |
| `std.http.writeCorsPreflight(buffer, allowOrigin, allowMethods, allowHeaders)` | `Maybe<Span<u8>>` | Writes a 204 CORS preflight response with caller-provided allow headers. |
| `std.http.writeCorsJsonResponse(buffer, statusLine, body, allowOrigin)` | `Maybe<Span<u8>>` | Writes a JSON response with `access-control-allow-origin`; `statusLine` is a fragment such as `"200 OK"`. |
| `std.http.writeTextResponse(buffer, status, body)` | `Maybe<Span<u8>>` | Writes a `text/plain; charset=utf-8` response envelope into caller storage. |
| `std.http.writeTextOk(buffer, body)` | `Maybe<Span<u8>>` | Writes a 200 plain-text response envelope into caller storage. |
| `std.http.writeHtmlResponse(buffer, status, body)` | `Maybe<Span<u8>>` | Writes a `text/html; charset=utf-8` response envelope into caller storage. |
| `std.http.writeHtmlOk(buffer, body)` | `Maybe<Span<u8>>` | Writes a 200 HTML response envelope into caller storage. |
| `std.http.writeRedirect(buffer, status, location)` | `Maybe<Span<u8>>` | Writes a redirect response with a safe `Location` header; rejects non-3xx statuses and empty or control-character locations. |
| `std.http.writeFound(buffer, location)` | `Maybe<Span<u8>>` | Writes a 302 redirect response. |
| `std.http.writeSeeOther(buffer, location)` | `Maybe<Span<u8>>` | Writes a 303 redirect response. |
| `std.http.writeMovedPermanently(buffer, location)` | `Maybe<Span<u8>>` | Writes a 301 redirect response. |
| `std.http.writePermanentRedirect(buffer, location)` | `Maybe<Span<u8>>` | Writes a 308 redirect response. |
| `std.http.contentTypeForPath(path)` | `String` | Returns a small static-file media type from a path suffix. |
| `std.http.writeStaticResponse(buffer, status, path, body)` | `Maybe<Span<u8>>` | Writes a response with `content-type` inferred from the path suffix. |
| `std.http.writeJsonOk(buffer, body)` | `Maybe<Span<u8>>` | Writes a 200 JSON response envelope into caller storage. |
| `std.http.writeJsonCreated(buffer, body)` | `Maybe<Span<u8>>` | Writes a 201 JSON response envelope into caller storage. |
| `std.http.writeJsonBadRequest(buffer, body)` | `Maybe<Span<u8>>` | Writes a 400 JSON response envelope into caller storage. |
| `std.http.writeJsonUnauthorized(buffer, body)` | `Maybe<Span<u8>>` | Writes a 401 JSON response envelope into caller storage. |
| `std.http.writeJsonForbidden(buffer, body)` | `Maybe<Span<u8>>` | Writes a 403 JSON response envelope into caller storage. |
| `std.http.writeJsonNotFound(buffer, body)` | `Maybe<Span<u8>>` | Writes a 404 JSON response envelope into caller storage. |
| `std.http.writeJsonMethodNotAllowed(buffer, body)` | `Maybe<Span<u8>>` | Writes a 405 JSON response envelope into caller storage. |
| `std.http.writeJsonConflict(buffer, body)` | `Maybe<Span<u8>>` | Writes a 409 JSON response envelope into caller storage. |
| `std.http.writeJsonUnprocessable(buffer, body)` | `Maybe<Span<u8>>` | Writes a 422 JSON response envelope into caller storage. |
| `std.http.writeJsonTooManyRequests(buffer, body)` | `Maybe<Span<u8>>` | Writes a 429 JSON response envelope into caller storage. |
| `std.http.writeJsonInternalServerError(buffer, body)` | `Maybe<Span<u8>>` | Writes a 500 JSON response envelope into caller storage. |
| `std.http.writeNoContent(buffer)` | `Maybe<Span<u8>>` | Writes a 204 response envelope with an empty body. |
| `std.http.requestMethodName(request)` | `Maybe<Span<u8>>` | Borrows the method token from a request envelope. |
| `std.http.requestTarget(request)` | `Maybe<Span<u8>>` | Borrows the raw target from a request envelope. |
| `std.http.requestPath(request)` | `Maybe<Span<u8>>` | Borrows the path from an absolute or origin-form request target. |
| `std.http.pathSegmentCount(path)` | `usize` | Counts non-empty path segments in a path span. |
| `std.http.pathSegment(path, index)` | `Maybe<Span<u8>>` | Borrows a zero-based non-empty path segment from a path span. |
| `std.http.pathMatchesPattern(path, pattern)` | `Bool` | Matches path segments against literals, `:params`, and a trailing `*` wildcard. |
| `std.http.pathParam(path, pattern, name)` | `Maybe<Span<u8>>` | Borrows a named path parameter from a matched path pattern. |
| `std.http.requestPathSegmentCount(request)` | `usize` | Counts non-empty path segments from a request envelope path. |
| `std.http.requestPathSegment(request, index)` | `Maybe<Span<u8>>` | Borrows a zero-based non-empty request path segment. |
| `std.http.requestQuery(request)` | `Maybe<Span<u8>>` | Borrows the query string from a request target. |
| `std.http.requestQueryValue(request, name)` | `Maybe<Span<u8>>` | Borrows a query value by name from a request envelope. |
| `std.http.requestHeader(request, name)` | `Maybe<Span<u8>>` | Borrows a case-insensitive request header value. |
| `std.http.requestBearerToken(request)` | `Maybe<Span<u8>>` | Borrows the bearer token from the `Authorization` request header. |
| `std.http.requestCookie(request, name)` | `Maybe<Span<u8>>` | Borrows a named cookie value from the `Cookie` request header. |
| `std.http.requestContentLength(request)` | `Maybe<usize>` | Parses the `Content-Length` request header. |
| `std.http.requestContentType(request)` | `Maybe<Span<u8>>` | Borrows the media type from `Content-Type`, excluding parameters. |
| `std.http.requestAccepts(request, media)` | `Bool` | Checks whether `Accept` allows an exact media type, type wildcard, or `*/*`; absent `Accept` allows any media and `q=0` rejects a range. |
| `std.http.requestAcceptsJson(request)` | `Bool` | Checks whether `Accept` allows `application/json`. |
| `std.http.requestBody(request)` | `Maybe<Span<u8>>` | Borrows the request body after the blank line. |
| `std.http.requestBodyWithin(request, max)` | `Maybe<Span<u8>>` | Borrows the request body only when it is at most `max` bytes. |
| `std.http.requestHasJsonContentType(request)` | `Bool` | True when the request content type is `application/json`, ignoring ASCII case and allowing parameters. |
| `std.http.requestJsonBodyWithin(request, max)` | `Maybe<Span<u8>>` | Borrows the body only when content type is JSON, body length is within `max`, and bytes validate as JSON. |
| `std.http.requestJsonField(request, name, max)` | `Maybe<Span<u8>>` | Borrows a top-level JSON field from a bounded JSON request body. |
| `std.http.requestMatches(request, method, path)` | `Bool` | True when a request envelope has the exact method and normalized path. |
| `std.http.methodAllowed(method, allowed)` | `Bool` | Checks a method against a comma-separated allow list. |
| `std.http.requestMethodAllowed(request, allowed)` | `Bool` | Checks a request method against a comma-separated allow list. |
| `std.http.requestMethodIs(request, method)` | `Bool` | True when a request envelope has the exact method. |
| `std.http.requestIsGet(request, path)` | `Bool` | True when a request envelope is `GET` for the normalized path. |
| `std.http.requestIsHead(request, path)` | `Bool` | True when a request envelope is `HEAD` for the normalized path. |
| `std.http.requestIsOptions(request, path)` | `Bool` | True when a request envelope is `OPTIONS` for the normalized path. |
| `std.http.requestIsPost(request, path)` | `Bool` | True when a request envelope is `POST` for the normalized path. |
| `std.http.requestIsPut(request, path)` | `Bool` | True when a request envelope is `PUT` for the normalized path. |
| `std.http.requestIsPatch(request, path)` | `Bool` | True when a request envelope is `PATCH` for the normalized path. |
| `std.http.requestIsDelete(request, path)` | `Bool` | True when a request envelope is `DELETE` for the normalized path. |
| `std.http.requestPathStartsWith(request, prefix)` | `Bool` | True when the normalized request path starts with `prefix`. |
| `std.http.requestPathTailAfter(request, prefix)` | `Maybe<Span<u8>>` | Borrows the normalized request path after `prefix`, or `null` when it does not match. |
| `std.http.requestRouteMatches(request, method, pattern)` | `Bool` | True when the method matches and the path matches a segment pattern. |
| `std.http.requestRouteMethodAllowed(request, pattern, allowed)` | `Bool` | True when the path pattern matches and the method is in a comma-separated allow list. |
| `std.http.requestPathParam(request, pattern, name)` | `Maybe<Span<u8>>` | Borrows a named request path parameter from a matched pattern. |
| `std.http.headerBlockSafe(headers)` | `Bool` | Validates a newline-separated header block before writing it. |
| `std.http.headerBytes(response, value)` | `Maybe<Span<u8>>` | Borrows a response header value after validating packed metadata. |
| `std.http.responseBody(response, result)` | `Maybe<Span<u8>>` | Borrows the response body when the transport result succeeded. |
| `std.http.responseBodyBytes(response)` | `Maybe<Span<u8>>` | Borrows the body bytes from a local response envelope written by response helpers. |
| `std.http.responseStatus(response)` | `Maybe<u16>` | Parses the status code from a local response envelope. |
| `std.http.responseStatusIs(response, status)` | `Bool` | Checks the status code in a local response envelope. |
| `std.http.responseHeader(response, name)` | `Maybe<Span<u8>>` | Borrows a case-insensitive header value from a local response envelope. |
| `std.http.responseContentType(response)` | `Maybe<Span<u8>>` | Borrows the media type from a local response envelope. |
| `std.http.responseRedirectLocation(response)` | `Maybe<Span<u8>>` | Borrows a redirect `Location` header from a local response envelope. |
| `std.http.responseBodyEquals(response, expected)` | `Bool` | Compares a local response body with expected bytes. |
| `std.http.responseMatches(response, status, contentType, body)` | `Bool` | Checks local response status, optional content type, and body bytes. |
| `std.http.testRequest(buffer, method, target, body)` | `Maybe<Span<u8>>` | Writes a synthetic request envelope for handler tests. |
| `std.http.testJsonRequest(buffer, method, target, body)` | `Maybe<Span<u8>>` | Writes a synthetic JSON request envelope for handler tests. |

The `WithHeader` and `WithHeaders` writers reject header names they manage
themselves. Request writers reject `content-length` and `transfer-encoding`;
JSON request writers also reject `content-type`; response writers reject
`content-length`, `transfer-encoding`, and `connection`; JSON response writers
also reject `content-type`.

Metadata labels:

- effects: net, memory, parse, or none for named error constants
- allocation behavior: metadata helpers do not allocate; `fetch` writes into a
  caller-owned response buffer and uses the provider runtime internally
- target support: request/response parsing and writing helpers are
  target-neutral; client/server require a net-capable target; `fetch` and
  `listen` run on supported Darwin arm64 and Linux x64 host executable targets
- error behavior: metadata helpers are infallible; `fetch` returns status,
  body length, and error metadata so non-2xx responses are distinguishable from
  transport failures
- ownership notes: HTTP helpers borrow network capability metadata and write
  only to caller-owned buffers
- examples: `conformance/native/pass/std-http-metadata-neutral.graph`,
  `conformance/native/pass/std-http-fetch.graph`,
  `conformance/native/pass/std-http-errors.graph`,
  `conformance/native/pass/std-http-response-helpers.graph`,
  `conformance/native/pass/std-http-api-helpers.graph`,
  `conformance/native/pass/std-http-cors-helpers.graph`,
  `conformance/native/pass/std-http-auth-helpers.graph`,
  `conformance/native/pass/std-http-path-segments.graph`,
  `examples/json-api-client.graph`,
  `examples/json-api-router.graph`,
  `examples/std-http-json.graph`,
  `examples/std-http-request.graph`,
  `examples/std-http-headers.graph`

## Example

Metadata helpers:

```zero
pub fn main(world: World) -> Void raises {
    let net: Net = std.net.host()
    let addr: Address = std.net.address("localhost", 8080_u16)
    let _client: HttpClient = std.http.client(net)
    let _server: HttpServer = std.http.server(net, addr)
    let method: HttpMethod = std.http.parseMethod("GET")
    if method == std.http.parseMethod("GET") && std.mem.len(std.mem.span("body")) == 4 {
        check world.out.write("http ok\n")
    }
}
```

GET request:

```zero
pub fn main(world: World) -> Void raises {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [512]u8 = [0_u8; 512]
    let request: Span<u8> = std.mem.span("GET https://example.com\n\n")
    let result: HttpResult = std.http.fetch(client, request, response, std.time.ms(1000))
    if std.http.resultOk(result) {
        check world.out.write("http get ok\n")
        return
    }
    check world.err.write("http get failed\n")
}
```

Request with headers and body:

```zero
pub fn main(world: World) -> Void raises {
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var request_buf: [256]u8 = [0_u8; 256]
    let request: Maybe<Span<u8>> = std.http.writeJsonRequestWithHeader(request_buf, "POST https://example.com/api", "accept: application/json", "{\"ping\":1}")
    var response: [512]u8 = [0_u8; 512]
    if request.has {
        let result: HttpResult = std.http.fetch(client, request.value, response, std.time.ms(1000))
        if std.http.resultOk(result) {
            check world.out.write("http post ok\n")
            return
        }
    }
    check world.err.write("http post failed\n")
}
```

Request routing:

```zero
pub fn main(world: World) -> Void raises {
    let request: Span<u8> = std.mem.span("POST /users/7?tenant=demo\naccept: application/json\ncontent-type: application/json; charset=utf-8\n\n{\"id\":7}")
    var response: [256]u8 = [0_u8; 256]
    let id: Maybe<Span<u8>> = std.http.requestPathParam(request, "/users/:id", "id")
    let body_id: Maybe<Span<u8>> = std.http.requestJsonField(request, "id", 64)
    let tenant: Maybe<Span<u8>> = std.http.requestQueryValue(request, "tenant")
    if std.http.requestRouteMatches(request, "POST", "/users/:id") && id.has && tenant.has && body_id.has && std.http.requestAcceptsJson(request) {
        let written: Maybe<Span<u8>> = std.http.writeJsonResponseWithHeader(response, 201_u16, "location: /users/7", "{\"created\":true}")
        if written.has {
            check world.out.write("http route ok\n")
            return
        }
    }
    let failed: Maybe<Span<u8>> = std.http.writeJsonError(response, 400, "bad_request")
    if failed.has {
        check world.err.write("http route failed\n")
    }
}
```

CORS preflight and JSON response:

```zero
pub fn main(world: World) -> Void raises {
    let request: Span<u8> = std.mem.span("OPTIONS /users\naccess-control-request-method: POST\n\n")
    var response: [256]u8 = [0_u8; 256]
    if std.http.requestIsOptions(request, "/users") {
        let written: Maybe<Span<u8>> = std.http.writeCorsPreflight(response, "*", "GET, POST, OPTIONS", "content-type, authorization")
        if written.has {
            check world.out.write("http cors preflight ok\n")
            return
        }
    }
    let failed: Maybe<Span<u8>> = std.http.writeCorsJsonResponse(response, "400 Bad Request", "{\"error\":\"bad_request\"}", "*")
    if failed.has {
        check world.err.write("http cors failed\n")
    }
}
```

Authorization and session cookie helpers:

```zero
pub fn main(world: World) -> Void raises {
    let request: Span<u8> = std.mem.span("GET /me\nauthorization: Bearer token-123\ncookie: sid=abc; theme=dark\n\n")
    let token: Maybe<Span<u8>> = std.http.requestBearerToken(request)
    let session: Maybe<Span<u8>> = std.http.requestCookie(request, "sid")
    if token.has && session.has {
        check world.out.write("http auth ok\n")
    }
}
```

Response body:

```zero
pub fn main(world: World) -> Void raises {
    let maybe_request: Maybe<String> = std.args.get(1)
    if !maybe_request.has {
        check world.err.write("usage: pass HTTP request envelope\n")
        return
    }
    let net: Net = std.net.host()
    let client: HttpClient = std.http.client(net)
    var response: [512]u8 = [0_u8; 512]
    let result: HttpResult = std.http.fetch(client, std.mem.span(maybe_request.value), response, std.time.ms(5000))
    let bytes: Maybe<Span<u8>> = std.http.responseBody(response, result)
    var arena_buf: [16]u8 = [0_u8; 16]
    var arena: FixedBufAlloc = std.mem.fixedBufAlloc(arena_buf)
    var parsed: Maybe<JsonDoc> = null
    if bytes.has {
        parsed = std.json.parseBytes(arena, bytes.value)
    }
    if std.http.resultOk(result) && bytes.has && parsed.has {
        check world.out.write("http response json ok\n")
        return
    }
    check world.err.write("http response json failed\n")
}
```

Loopback API server:

The handler receives fixed caller-owned response storage and response writers
do not stream. The listener accepts at most 65,536 bytes for the complete
serialized HTTP response: status line, headers, CRLF separators,
`content-length` metadata, and body. A writer returns `Maybe.none` when its
input is invalid or the supplied storage is insufficient. If a listener
handler returns `Maybe.none`, the client receives HTTP 500
`{"error":"response_unavailable"}`. Malformed handler output, handler process
failure, and a capture over the limit produce `response_invalid`,
`handler_failed`, and `response_too_large`; each listener error closes the
connection. This fixed initial cap does not provide streaming or unbounded
responses.

```zero
pub fn main(world: World) -> Void raises {
    check std.http.listen(world)
}

fn handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>> {
    if std.http.requestIsGet(request, "/ping") {
        return std.http.writeJsonOk(response, "{\"message\":\"pong\"}")
    }
    if std.http.requestIsGet(request, "/robots.txt") {
        return std.http.writeTextOk(response, "user-agent: *\nallow: /\n")
    }
    if std.http.requestIsGet(request, "/old") {
        return std.http.writeMovedPermanently(response, "/ping")
    }
    return std.http.writeJsonError(response, 404, "not_found")
}
```

Run it and use the port printed by the listener:

```sh
zero run .
# listening on http://127.0.0.1:3001
curl -sS -i http://127.0.0.1:3001/ping
```

When no port is passed, `listen(world)` starts at `3000` and increments by one
until it finds a free loopback port. This lets small local services coexist on a
development machine. When the program passes an explicit port, such as
`std.http.listen(world, 3000_u16)`, Zero tries exactly that port and reports the
bind failure instead of auto-incrementing.

Synthetic handler check:

```zero
fn handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>> {
    if std.http.requestRouteMethodAllowed(request, "/users/:id", "GET, HEAD") {
        return std.http.writeJsonResponseWithHeaders(response, 200_u16, "cache-control: no-store", "{\"ok\":true}")
    }
    return std.http.writeJsonError(response, 404_u16, "not_found")
}

pub fn main(world: World) -> Void raises {
    var request_storage: [128]u8 = [0_u8; 128]
    var response_storage: [256]u8 = [0_u8; 256]
    let request: Maybe<Span<u8>> = std.http.testRequest(request_storage, "GET", "/users/7", "")
    if request.has {
        let response: Maybe<Span<u8>> = handle(request.value, response_storage)
        if response.has && std.http.responseMatches(response.value, 200_u16, "application/json", "{\"ok\":true}") {
            check world.out.write("handler ok\n")
            return
        }
    }
    check world.err.write("handler failed\n")
}
```

## Design Notes

`std.http.fetch` is the outbound HTTP client primitive. The request argument is
one byte envelope: `METHOD URL`, followed by zero or more `Header-Name: value`
lines, a blank line, and optional request body bytes. Use `writeRequest` and
`writeJsonRequest` when you want the standard library to write that envelope
into caller storage. The timeout is a `Duration`, typically built with
`std.time.ms(...)`.

`fetch` supports `http://` and `https://` URLs on supported host executable
targets, uses the C-library curl provider, does not follow redirects, and
verifies TLS through the provider. TLS verification is always enabled.
`ZERO_HTTP_TEST_CA_BUNDLE` can point the provider at an explicit CA bundle
while keeping certificate verification on.

The response buffer starts with internal metadata, followed by raw response
headers and then the response body. Use `responseHeadersLen` and
`responseBodyOffset` rather than hard-coding offsets. `headerValue` scans the
response buffer and returns packed offset/length metadata for the matching
value; `headerFound`, `headerOffset`, and `headerLen` inspect that metadata
without allocating. Prefer `headerBytes` and `responseBody` when you need a
borrowed byte span and want metadata bounds checked in one call.

Compare `resultError` with the named `HttpError` helpers rather than raw
numbers. Use `errorName(resultError(result))` for diagnostics and logs.
`errorNone` means the transport succeeded; HTTP non-2xx statuses still carry
`errorNone` and can be inspected with `resultStatus`.

The module does not expose raw socket read/write APIs, streaming bodies, a
global router, or a heap-allocated response object.
