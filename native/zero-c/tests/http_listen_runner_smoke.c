#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "zero.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void *z_checked_malloc(size_t size) {
  void *ptr = malloc(size ? size : 1);
  if (!ptr) abort();
  return ptr;
}

void zbuf_init(ZBuf *buf) {
  if (buf) *buf = (ZBuf){0};
}

void zbuf_append(ZBuf *buf, const char *text) {
  (void)buf;
  (void)text;
}

void zbuf_append_char(ZBuf *buf, char ch) {
  (void)buf;
  (void)ch;
}

void zbuf_appendf(ZBuf *buf, const char *fmt, ...) {
  (void)buf;
  (void)fmt;
}

void zbuf_free(ZBuf *buf) {
  if (!buf) return;
  free(buf->data);
  *buf = (ZBuf){0};
}

bool z_write_file(const char *path, const char *text, ZDiag *diag) {
  (void)path;
  (void)text;
  (void)diag;
  return false;
}

bool z_http_listen_temp_path(const char *temp_dir, const char *leaf, char *out, size_t out_cap, ZDiag *diag) {
  (void)temp_dir;
  (void)leaf;
  (void)out;
  (void)out_cap;
  (void)diag;
  return false;
}

bool z_http_listen_create_temp_dir(char *out, size_t out_cap, ZDiag *diag) {
  (void)out;
  (void)out_cap;
  (void)diag;
  return false;
}

void z_http_listen_cleanup_temp_dir(const char *temp_dir) {
  (void)temp_dir;
}

#include "../src/http_listen_runner.c"

static int fail(const char *message) {
  fprintf(stderr, "http_listen_runner_smoke: %s\n", message);
  return 1;
}

static int expect_true(bool condition, const char *message) {
  return condition ? 0 : fail(message);
}

static int make_pair(int fds[2]) {
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return fail("socketpair failed");
  return 0;
}

static int read_peer_text(int fd, char *out, size_t out_cap, size_t *out_len) {
  if (!out || out_cap == 0 || !out_len) return fail("invalid read buffer");
  *out_len = 0;
  while (*out_len + 1 < out_cap) {
    ssize_t n = recv(fd, out + *out_len, out_cap - *out_len - 1, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return fail("recv failed");
    }
    if (n == 0) break;
    *out_len += (size_t)n;
  }
  out[*out_len] = '\0';
  return 0;
}

static int smoke_send_all(void) {
  int fds[2];
  if (make_pair(fds) != 0) return 1;
  const char payload[] = "abc123";
  int status = expect_true(send_all(fds[0], payload, sizeof(payload) - 1), "send_all should send full payload");
  char got[16];
  ssize_t n = recv(fds[1], got, sizeof(got), 0);
  if (n < 0) status |= fail("send_all peer recv failed");
  else status |= expect_true((size_t)n == sizeof(payload) - 1 && memcmp(got, payload, sizeof(payload) - 1) == 0, "send_all peer payload mismatch");
  close(fds[0]);
  close(fds[1]);
  return status;
}

static int smoke_json_error(void) {
  int fds[2];
  if (make_pair(fds) != 0) return 1;
  int status = expect_true(send_json_error(fds[0], 413, "Payload Too Large", "{\"error\":\"payload_too_large\"}"), "send_json_error should send response");
  shutdown(fds[0], SHUT_WR);
  char got[512];
  size_t len = 0;
  status |= read_peer_text(fds[1], got, sizeof(got), &len);
  status |= expect_true(strstr(got, "HTTP/1.1 413 Payload Too Large\r\n") == got, "json error status line");
  status |= expect_true(strstr(got, "connection: close\r\n") != NULL, "json error connection close");
  status |= expect_true(strstr(got, "{\"error\":\"payload_too_large\"}") != NULL, "json error body");
  close(fds[0]);
  close(fds[1]);
  return status;
}

static int smoke_read_request_complete(void) {
  int fds[2];
  if (make_pair(fds) != 0) return 1;
  const char request[] = "POST /echo\r\ncontent-length: 4\r\n\r\npong";
  send_all(fds[0], request, sizeof(request) - 1);
  shutdown(fds[0], SHUT_WR);
  char buffer[Z_HTTP_LISTEN_REQUEST_CAP];
  size_t len = 0;
  unsigned status = 0;
  int result = read_http_request(fds[1], buffer, sizeof(buffer), &len, &status);
  int smoke = expect_true(result && len == sizeof(request) - 1 && memcmp(buffer, request, len) == 0, "complete request should parse");
  close(fds[0]);
  close(fds[1]);
  return smoke;
}

static int smoke_read_request_rejections(void) {
  int malformed[2];
  if (make_pair(malformed) != 0) return 1;
  const char bad_length[] = "POST /echo\r\ncontent-length: nope\r\n\r\nx";
  send_all(malformed[0], bad_length, sizeof(bad_length) - 1);
  shutdown(malformed[0], SHUT_WR);
  char buffer[Z_HTTP_LISTEN_REQUEST_CAP];
  size_t len = 0;
  unsigned status = 0;
  int ok = read_http_request(malformed[1], buffer, sizeof(buffer), &len, &status);
  int smoke = expect_true(!ok && status == 400, "malformed content-length should be bad request");
  close(malformed[0]);
  close(malformed[1]);

  int oversized[2];
  if (make_pair(oversized) != 0) return 1;
  const char too_large[] = "POST /echo\r\ncontent-length: 999999\r\n\r\n";
  send_all(oversized[0], too_large, sizeof(too_large) - 1);
  shutdown(oversized[0], SHUT_WR);
  status = 0;
  len = 0;
  ok = read_http_request(oversized[1], buffer, sizeof(buffer), &len, &status);
  smoke |= expect_true(!ok && status == 413, "oversized content-length should be payload too large");
  close(oversized[0]);
  close(oversized[1]);
  return smoke;
}

static int write_literal_file(const char *path, const char *text, mode_t mode) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
  if (fd < 0) return fail("open fixture file failed");
  size_t len = strlen(text);
  size_t written = 0;
  while (written < len) {
    ssize_t n = write(fd, text + written, len - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return fail("write fixture file failed");
    }
    if (n == 0) {
      close(fd);
      return fail("short fixture write");
    }
    written += (size_t)n;
  }
  if (close(fd) != 0) return fail("close fixture file failed");
  return 0;
}

static int write_response_handler(const char *path, size_t body_len, int exit_code) {
  int header_len = snprintf(NULL, 0, "HTTP/1.1 200 OK\r\ncontent-length: %zu\r\n\r\n", body_len);
  if (header_len < 0) return fail("format response fixture failed");
  size_t cap = (size_t)header_len + 256;
  char *script = z_checked_malloc(cap);
  int written = snprintf(script, cap,
                         "#!/bin/sh\nprintf 'HTTP/1.1 200 OK\\r\\ncontent-length: %zu\\r\\n\\r\\n'\ni=0\nwhile [ \"$i\" -lt %zu ]; do printf x; i=$((i + 1)); done\nexit %d\n",
                         body_len, body_len, exit_code);
  int status = 0;
  if (written < 0 || (size_t)written >= cap) status = fail("format response fixture truncated");
  else status = write_literal_file(path, script, 0700);
  free(script);
  return status;
}

static int smoke_handler_outcome(const char *handler_path, const char *request_path, ZHttpListenHandlerOutcome expected, size_t expected_len) {
  char *response = NULL;
  size_t response_len = 0;
  ZHttpListenHandlerOutcome outcome = run_handler_capture(handler_path, request_path, &response, &response_len);
  int status = expect_true(outcome == expected, "handler capture outcome mismatch");
  if (expected == Z_HTTP_LISTEN_HANDLER_OK) {
    status |= expect_true(response != NULL && response_len == expected_len, "accepted handler response length mismatch");
    status |= expect_true(response_len >= 5 && memcmp(response, "HTTP/", 5) == 0, "accepted handler response should be HTTP");
  } else {
    status |= expect_true(response == NULL && response_len == 0, "rejected handler should not return response");
  }
  free(response);
  return status;
}

static int smoke_write_request_file(void) {
  char dir[] = "/tmp/zero-listen-runner-smoke-XXXXXX";
  if (!mkdtemp(dir)) return fail("mkdtemp failed");
  char path[256];
  snprintf(path, sizeof(path), "%s/request.http", dir);
  const char request[] = "GET /ping\r\n\r\n";
  int status = expect_true(write_request_file(path, request, sizeof(request) - 1), "write_request_file should create request");
  status |= expect_true(!write_request_file(path, request, sizeof(request) - 1), "write_request_file should be exclusive");
  char got[64] = {0};
  int fd = open(path, O_RDONLY);
  if (fd < 0) status |= fail("open written request failed");
  else {
    ssize_t n = read(fd, got, sizeof(got));
    status |= expect_true(n == (ssize_t)(sizeof(request) - 1) && memcmp(got, request, sizeof(request) - 1) == 0, "request file content mismatch");
    close(fd);
  }
  unlink(path);
  rmdir(dir);
  return status;
}

static int smoke_handler_capture(void) {
  char dir[] = "/tmp/zero-listen-handler-smoke-XXXXXX";
  if (!mkdtemp(dir)) return fail("mkdtemp handler failed");
  char request_path[256];
  char ok_handler[256];
  char exact_handler[256];
  char oversized_handler[256];
  char unavailable_handler[256];
  char malformed_handler[256];
  char failed_handler[256];
  snprintf(request_path, sizeof(request_path), "%s/request.http", dir);
  snprintf(ok_handler, sizeof(ok_handler), "%s/ok-handler", dir);
  snprintf(exact_handler, sizeof(exact_handler), "%s/exact-handler", dir);
  snprintf(oversized_handler, sizeof(oversized_handler), "%s/oversized-handler", dir);
  snprintf(unavailable_handler, sizeof(unavailable_handler), "%s/unavailable-handler", dir);
  snprintf(malformed_handler, sizeof(malformed_handler), "%s/malformed-handler", dir);
  snprintf(failed_handler, sizeof(failed_handler), "%s/failed-handler", dir);
  int status = 0;
  status |= write_literal_file(request_path, "GET /ping\r\n\r\n", 0600);
  status |= write_response_handler(ok_handler, 8192, 0);

  size_t exact_body_len = Z_HTTP_LISTEN_RESPONSE_LIMIT;
  for (;;) {
    int header_len = snprintf(NULL, 0, "HTTP/1.1 200 OK\r\ncontent-length: %zu\r\n\r\n", exact_body_len);
    if (header_len < 0 || (size_t)header_len > Z_HTTP_LISTEN_RESPONSE_LIMIT) {
      status |= fail("exact response header length failed");
      break;
    }
    size_t next_body_len = Z_HTTP_LISTEN_RESPONSE_LIMIT - (size_t)header_len;
    if (next_body_len == exact_body_len) break;
    exact_body_len = next_body_len;
  }
  status |= write_response_handler(exact_handler, exact_body_len, 0);
  status |= write_response_handler(oversized_handler, exact_body_len + 1, 0);
  status |= write_literal_file(unavailable_handler, "#!/bin/sh\nprintf '" Z_HTTP_LISTEN_NO_RESPONSE_MARKER "\\n'\n", 0700);
  status |= write_literal_file(malformed_handler, "#!/bin/sh\nprintf 'not http'\n", 0700);
  status |= write_response_handler(failed_handler, 2, 7);

  int normal_header_len = snprintf(NULL, 0, "HTTP/1.1 200 OK\r\ncontent-length: %zu\r\n\r\n", (size_t)8192);
  status |= smoke_handler_outcome(ok_handler, request_path, Z_HTTP_LISTEN_HANDLER_OK,
                                  (size_t)normal_header_len + 8192);
  status |= smoke_handler_outcome(exact_handler, request_path, Z_HTTP_LISTEN_HANDLER_OK, Z_HTTP_LISTEN_RESPONSE_LIMIT);
  status |= smoke_handler_outcome(oversized_handler, request_path, Z_HTTP_LISTEN_HANDLER_RESPONSE_TOO_LARGE, 0);
  status |= smoke_handler_outcome(unavailable_handler, request_path, Z_HTTP_LISTEN_HANDLER_RESPONSE_UNAVAILABLE, 0);
  status |= smoke_handler_outcome(malformed_handler, request_path, Z_HTTP_LISTEN_HANDLER_RESPONSE_INVALID, 0);
  status |= smoke_handler_outcome(failed_handler, request_path, Z_HTTP_LISTEN_HANDLER_PROCESS_FAILED, 0);

  unlink(request_path);
  unlink(ok_handler);
  unlink(exact_handler);
  unlink(oversized_handler);
  unlink(unavailable_handler);
  unlink(malformed_handler);
  unlink(failed_handler);
  rmdir(dir);
  return status;
}

int main(void) {
  int status = 0;
  status |= smoke_send_all();
  status |= smoke_json_error();
  status |= smoke_read_request_complete();
  status |= smoke_read_request_rejections();
  status |= smoke_write_request_file();
  status |= smoke_handler_capture();
  return status;
}
