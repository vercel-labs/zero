#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "http_listen_runner.h"
#include "http_listen_temp.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
int z_http_listen_run(const ZHttpListenRunConfig *config, ZDiag *diag) {
  (void)config;
  if (diag) {
    diag->code = 2002;
    diag->line = 1;
    diag->column = 1;
    snprintf(diag->message, sizeof(diag->message), "std.http.listen is not available on this host yet");
    snprintf(diag->expected, sizeof(diag->expected), "POSIX host runtime listener");
    snprintf(diag->actual, sizeof(diag->actual), "Windows host");
    snprintf(diag->help, sizeof(diag->help), "use a Darwin or Linux host for zero run on std.http.listen programs");
  }
  return 1;
}
#else

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define Z_HTTP_LISTEN_REQUEST_CAP 4096
/* The serialized response contract includes the status line, headers, CRLF
 * separators, content-length metadata, and body. The generated handler and
 * parent capture use this same fixed capacity; the parent adds one byte only
 * for C termination. */
#define Z_HTTP_LISTEN_RESPONSE_LIMIT 65536
#define Z_HTTP_LISTEN_NO_RESPONSE_MARKER "ZERO_HTTP_LISTEN_RESPONSE_UNAVAILABLE"
#define Z_HTTP_LISTEN_CLIENT_TIMEOUT_SECONDS 2

typedef enum {
  Z_HTTP_LISTEN_HANDLER_OK,
  Z_HTTP_LISTEN_HANDLER_PROCESS_FAILED,
  Z_HTTP_LISTEN_HANDLER_RESPONSE_INVALID,
  Z_HTTP_LISTEN_HANDLER_RESPONSE_TOO_LARGE,
  Z_HTTP_LISTEN_HANDLER_RESPONSE_UNAVAILABLE,
} ZHttpListenHandlerOutcome;

static volatile sig_atomic_t listen_stop_requested = 0;
static volatile sig_atomic_t listen_server_fd_for_signal = -1;

static void listen_diag(ZDiag *diag, const char *message, const char *expected, const char *actual, const char *help);

static void listen_handle_stop_signal(int signum) {
  (void)signum;
  listen_stop_requested = 1;
  sig_atomic_t fd = listen_server_fd_for_signal;
  if (fd >= 0) {
    listen_server_fd_for_signal = -1;
    close((int)fd);
  }
}

typedef struct {
  struct sigaction old_term_action;
  struct sigaction old_int_action;
} ListenSignalState;

static void listen_install_stop_handlers(int server_fd, ListenSignalState *state) {
  if (!state) return;
  struct sigaction stop_action;
  memset(&stop_action, 0, sizeof(stop_action));
  memset(state, 0, sizeof(*state));
  sigemptyset(&stop_action.sa_mask);
  stop_action.sa_handler = listen_handle_stop_signal;
  listen_stop_requested = 0;
  listen_server_fd_for_signal = server_fd;
  (void)sigaction(SIGTERM, &stop_action, &state->old_term_action);
  (void)sigaction(SIGINT, &stop_action, &state->old_int_action);
}

static void listen_restore_stop_handlers(const ListenSignalState *state) {
  listen_server_fd_for_signal = -1;
  if (!state) return;
  (void)sigaction(SIGTERM, &state->old_term_action, NULL);
  (void)sigaction(SIGINT, &state->old_int_action, NULL);
}

static void listen_diag(ZDiag *diag, const char *message, const char *expected, const char *actual, const char *help) {
  if (!diag) return;
  diag->code = 2002;
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "std.http.listen failed");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "valid std.http.listen run");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "listener setup failed");
  snprintf(diag->help, sizeof(diag->help), "%s", help ? help : "inspect the program graph and host runtime diagnostics");
}

static int run_argv(const char *const *argv, bool suppress_stdout) {
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    if (suppress_stdout) {
      int dev_null = open("/dev/null", O_WRONLY);
      if (dev_null >= 0) {
        dup2(dev_null, STDOUT_FILENO);
        close(dev_null);
      }
    }
    execv(argv[0], (char *const *)argv);
    perror("zero listen");
    _exit(127);
  }
  if (pid < 0) {
    perror("zero listen");
    return 1;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    perror("zero listen");
    return 1;
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 1;
}

static void argv_push(const char **argv, size_t cap, size_t *len, const char *value) {
  if (!argv || !len || *len + 1 >= cap) return;
  argv[(*len)++] = value;
  argv[*len] = NULL;
}

static bool build_handler_graph(const ZHttpListenRunConfig *config, const char *temp_dir, char *handler_exe, size_t handler_exe_cap, ZDiag *diag) {
  if (!config || !config->zero_exe || !config->input || !config->handler || !temp_dir || !handler_exe || handler_exe_cap == 0) {
    listen_diag(diag, "std.http.listen runner is missing required configuration", "zero executable, input graph, and handler", "missing configuration", NULL);
    return false;
  }

  char base_graph[PATH_MAX];
  char patch_path[PATH_MAX];
  char handler_graph[PATH_MAX];
  if (!z_http_listen_temp_path(temp_dir, "base.graph", base_graph, sizeof(base_graph), diag) ||
      !z_http_listen_temp_path(temp_dir, "handler.patch", patch_path, sizeof(patch_path), diag) ||
      !z_http_listen_temp_path(temp_dir, "handler.graph", handler_graph, sizeof(handler_graph), diag) ||
      !z_http_listen_temp_path(temp_dir, "handler", handler_exe, handler_exe_cap, diag)) {
    return false;
  }

  ZBuf patch;
  zbuf_init(&patch);
  zbuf_append(&patch, "zero-program-graph-patch v1\n");
  zbuf_append(&patch, "replaceFunctionBody main\n");
  zbuf_append(&patch, "  let maybe_request_path Maybe<String> = std.args.get 1\n");
  zbuf_append(&patch, "  if !maybe_request_path.has\n");
  zbuf_append(&patch, "    check world.err.write \"usage: pass one HTTP request path\\n\"\n");
  zbuf_append(&patch, "    return\n");
  zbuf_appendf(&patch, "  var request [%" PRIu64 "]u8 = [0_u8; %" PRIu64 "]\n", (uint64_t)Z_HTTP_LISTEN_REQUEST_CAP, (uint64_t)Z_HTTP_LISTEN_REQUEST_CAP);
  zbuf_append(&patch, "  let maybe_request_len Maybe<usize> = std.fs.readBytes(maybe_request_path.value, request)\n");
  zbuf_append(&patch, "  if !maybe_request_len.has\n");
  zbuf_append(&patch, "    check world.err.write \"http request read failed\\n\"\n");
  zbuf_append(&patch, "    return\n");
  zbuf_append(&patch, "  let request_span Span<u8> = request[..maybe_request_len.value]\n");
  zbuf_appendf(&patch, "  var response [%" PRIu64 "]u8 = [0_u8; %" PRIu64 "]\n", (uint64_t)Z_HTTP_LISTEN_RESPONSE_LIMIT, (uint64_t)Z_HTTP_LISTEN_RESPONSE_LIMIT);
  zbuf_appendf(&patch, "  let output Maybe<Span<u8>> = %s(request_span, response)\n", config->handler);
  zbuf_append(&patch, "  if output.has\n");
  zbuf_append(&patch, "    check world.out.write output.value\n");
  zbuf_append(&patch, "    return\n");
  /* Maybe.none remains the std.http writer signal for invalid response input
   * or insufficient caller storage. Keep a private marker on stdout so the
   * listener can report response_unavailable instead of process failure. */
  zbuf_appendf(&patch, "  check world.out.write \"%s\\n\"\n", Z_HTTP_LISTEN_NO_RESPONSE_MARKER);
  zbuf_append(&patch, "end\n");

  bool wrote_patch = z_write_file(patch_path, patch.data ? patch.data : "", diag);
  zbuf_free(&patch);
  if (!wrote_patch) return false;

  const char *dump_argv[] = {config->zero_exe, "dump", "--format", "binary", "--out", base_graph, config->input, NULL};
  if (run_argv(dump_argv, true) != 0) {
    listen_diag(diag, "std.http.listen could not prepare a handler graph", "zero dump writes a temporary graph artifact", "zero dump failed", "run zero dump on the package to inspect graph input readiness");
    return false;
  }

  const char *patch_argv[] = {config->zero_exe, "patch", "--format", "binary", "--out", handler_graph, base_graph, patch_path, NULL};
  if (run_argv(patch_argv, true) != 0) {
    listen_diag(diag, "std.http.listen could not build the handler entry graph", "handler wrapper graph patch applies", "zero patch failed", "inspect the package handler signature and graph patch support");
    return false;
  }

  const char *build_argv[20] = {0};
  size_t build_len = 0;
  argv_push(build_argv, 20, &build_len, config->zero_exe);
  argv_push(build_argv, 20, &build_len, "build");
  if (config->target && config->target[0]) {
    argv_push(build_argv, 20, &build_len, "--target");
    argv_push(build_argv, 20, &build_len, config->target);
  }
  if (config->profile && config->profile[0]) {
    argv_push(build_argv, 20, &build_len, "--profile");
    argv_push(build_argv, 20, &build_len, config->profile);
  }
  if (config->backend && config->backend[0]) {
    argv_push(build_argv, 20, &build_len, "--backend");
    argv_push(build_argv, 20, &build_len, config->backend);
  }
  if (config->cc && config->cc[0]) {
    argv_push(build_argv, 20, &build_len, "--cc");
    argv_push(build_argv, 20, &build_len, config->cc);
  }
  argv_push(build_argv, 20, &build_len, "--out");
  argv_push(build_argv, 20, &build_len, handler_exe);
  argv_push(build_argv, 20, &build_len, handler_graph);
  if (run_argv(build_argv, true) != 0) {
    listen_diag(diag, "std.http.listen could not build the handler executable", "temporary handler graph builds as a host executable", "zero build failed", "run zero check on the package and inspect the handler body");
    return false;
  }
  return true;
}

static bool send_all(int fd, const char *data, size_t len) {
  if (!data && len > 0) return false;
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, data + sent, len - sent, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    sent += (size_t)n;
  }
  return true;
}

static void configure_client_socket(int fd) {
  struct timeval timeout;
  timeout.tv_sec = Z_HTTP_LISTEN_CLIENT_TIMEOUT_SECONDS;
  timeout.tv_usec = 0;
  (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static size_t header_end_offset(const char *buf, size_t len) {
  for (size_t i = 0; i + 3 < len; i++) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return i + 4;
  }
  for (size_t i = 0; i + 1 < len; i++) {
    if (buf[i] == '\n' && buf[i + 1] == '\n') return i + 2;
  }
  return 0;
}

static bool ascii_ieq_n(const char *left, const char *right, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned char a = (unsigned char)left[i];
    unsigned char b = (unsigned char)right[i];
    if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
    if (a != b) return false;
  }
  return true;
}

typedef struct {
  bool present;
  bool valid;
  size_t value;
} ParsedContentLength;

static ParsedContentLength parse_content_length(const char *buf, size_t header_len) {
  ParsedContentLength result = {.present = false, .valid = true, .value = 0};
  const char key[] = "content-length:";
  size_t key_len = sizeof(key) - 1;
  size_t line_start = 0;
  while (line_start < header_len) {
    size_t line_end = line_start;
    while (line_end < header_len && buf[line_end] != '\n') line_end++;
    size_t line_len = line_end - line_start;
    if (line_len > 0 && buf[line_start + line_len - 1] == '\r') line_len--;
    if (line_len >= key_len && ascii_ieq_n(buf + line_start, key, key_len)) {
      result.present = true;
      size_t i = line_start + key_len;
      while (i < line_start + line_len && (buf[i] == ' ' || buf[i] == '\t')) i++;
      size_t value = 0;
      size_t digits = 0;
      while (i < line_start + line_len && buf[i] >= '0' && buf[i] <= '9') {
        if (value > (SIZE_MAX - (size_t)(buf[i] - '0')) / 10u) {
          result.valid = false;
          return result;
        }
        value = value * 10 + (size_t)(buf[i] - '0');
        i++;
        digits++;
      }
      while (i < line_start + line_len && (buf[i] == ' ' || buf[i] == '\t')) i++;
      if (digits == 0 || i != line_start + line_len) {
        result.valid = false;
        return result;
      }
      result.value = value;
      return result;
    }
    line_start = line_end + 1;
  }
  return result;
}

static bool read_http_request(int fd, char *buf, size_t cap, size_t *out_len, unsigned *out_status) {
  if (out_len) *out_len = 0;
  if (out_status) *out_status = 400;
  if (!buf || cap < 2) return false;
  size_t total = 0;
  size_t header_end = 0;
  size_t content_len = 0;
  while (total + 1 < cap) {
    ssize_t n = recv(fd, buf + total, cap - total - 1, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (out_status) *out_status = 408;
      }
      return false;
    }
    if (n == 0) break;
    total += (size_t)n;
    buf[total] = '\0';
    if (header_end == 0) {
      header_end = header_end_offset(buf, total);
      if (header_end > 0) {
        ParsedContentLength parsed = parse_content_length(buf, header_end);
        if (!parsed.valid) return false;
        content_len = parsed.present ? parsed.value : 0;
        if (content_len > cap - header_end - 1) {
          if (out_status) *out_status = 413;
          return false;
        }
      }
    }
    if (header_end > 0 && total >= header_end + content_len) break;
  }
  if (out_len) *out_len = total;
  bool complete = total > 0 && header_end_offset(buf, total) > 0 && total >= header_end + content_len;
  if (!complete && out_status && total + 1 >= cap) *out_status = 413;
  return complete;
}

static bool write_request_file(const char *path, const char *request, size_t request_len) {
  if (!path || !request) return false;
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) return false;
  size_t written = 0;
  while (written < request_len) {
    ssize_t n = write(fd, request + written, request_len - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return false;
    }
    if (n == 0) {
      close(fd);
      return false;
    }
    written += (size_t)n;
  }
  if (close(fd) != 0) return false;
  return true;
}

static ZHttpListenHandlerOutcome run_handler_capture(const char *handler_exe, const char *request_path, char **out_data, size_t *out_len) {
  if (out_data) *out_data = NULL;
  if (out_len) *out_len = 0;
  if (!handler_exe || !request_path) return Z_HTTP_LISTEN_HANDLER_PROCESS_FAILED;
  int fds[2];
  if (pipe(fds) != 0) return Z_HTTP_LISTEN_HANDLER_PROCESS_FAILED;
  pid_t pid = fork();
  if (pid == 0) {
    if (close(fds[0]) != 0) _exit(127);
    if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
    if (close(fds[1]) != 0) _exit(127);
    char *const argv[] = {(char *)handler_exe, (char *)request_path, NULL};
    execv(handler_exe, argv);
    perror("zero listen handler");
    _exit(127);
  }
  close(fds[1]);
  if (pid < 0) {
    close(fds[0]);
    return Z_HTTP_LISTEN_HANDLER_PROCESS_FAILED;
  }

  char *response = z_checked_malloc(Z_HTTP_LISTEN_RESPONSE_LIMIT + 1);
  size_t len = 0;
  bool overflow = false;
  bool read_ok = true;
  char chunk[4096];
  while (true) {
    ssize_t n = read(fds[0], chunk, sizeof(chunk));
    if (n < 0) {
      if (errno == EINTR) continue;
      read_ok = false;
      break;
    }
    if (n == 0) break;
    size_t count = (size_t)n;
    if (overflow || len >= Z_HTTP_LISTEN_RESPONSE_LIMIT || count > Z_HTTP_LISTEN_RESPONSE_LIMIT - len) {
      overflow = true;
    } else {
      memcpy(response + len, chunk, count);
      len += count;
    }
  }
  if (close(fds[0]) != 0) read_ok = false;
  response[len] = '\0';

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    free(response);
    return Z_HTTP_LISTEN_HANDLER_PROCESS_FAILED;
  }
  bool handler_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (!read_ok || !handler_ok) {
    free(response);
    return Z_HTTP_LISTEN_HANDLER_PROCESS_FAILED;
  }
  if (overflow) {
    free(response);
    return Z_HTTP_LISTEN_HANDLER_RESPONSE_TOO_LARGE;
  }
  const char marker[] = Z_HTTP_LISTEN_NO_RESPONSE_MARKER "\n";
  if (len == sizeof(marker) - 1 && memcmp(response, marker, sizeof(marker) - 1) == 0) {
    free(response);
    return Z_HTTP_LISTEN_HANDLER_RESPONSE_UNAVAILABLE;
  }
  if (len < 5 || memcmp(response, "HTTP/", 5) != 0) {
    free(response);
    return Z_HTTP_LISTEN_HANDLER_RESPONSE_INVALID;
  }
  if (out_data) *out_data = response;
  else free(response);
  if (out_len) *out_len = len;
  return Z_HTTP_LISTEN_HANDLER_OK;
}

static const char *handler_outcome_error_name(ZHttpListenHandlerOutcome outcome) {
  switch (outcome) {
    case Z_HTTP_LISTEN_HANDLER_PROCESS_FAILED: return "handler_failed";
    case Z_HTTP_LISTEN_HANDLER_RESPONSE_INVALID: return "response_invalid";
    case Z_HTTP_LISTEN_HANDLER_RESPONSE_TOO_LARGE: return "response_too_large";
    case Z_HTTP_LISTEN_HANDLER_RESPONSE_UNAVAILABLE: return "response_unavailable";
    case Z_HTTP_LISTEN_HANDLER_OK: return NULL;
  }
  return "handler_failed";
}

static bool send_json_error(int fd, unsigned status, const char *reason, const char *body) {
  char response[512];
  size_t body_len = strlen(body ? body : "");
  int len = snprintf(response, sizeof(response),
                     "HTTP/1.1 %u %s\r\ncontent-type: application/json\r\nconnection: close\r\ncontent-length: %zu\r\n\r\n%s",
                     status, reason ? reason : "Error", body_len, body ? body : "");
  if (len < 0 || (size_t)len >= sizeof(response)) return false;
  return send_all(fd, response, (size_t)len);
}

static int listen_open_server_socket(const ZHttpListenRunConfig *config, uint16_t *out_bound_port, ZDiag *diag) {
  if (out_bound_port) *out_bound_port = 0;
  int server_fd = -1;
  unsigned attempts = config->auto_increment_port ? (65535u - (unsigned)config->port + 1u) : 1u;
  for (unsigned attempt = 0; attempt < attempts; attempt++) {
    uint16_t candidate = (uint16_t)((unsigned)config->port + attempt);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
      listen_diag(diag, "std.http.listen could not open a TCP socket", "host socket", strerror(errno), NULL);
      return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
      listen_diag(diag, "std.http.listen could not configure loopback address", "127.0.0.1 address", "inet_pton failed", NULL);
      close(server_fd);
      return -1;
    }
    addr.sin_port = htons(candidate);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
      if (listen(server_fd, 32) == 0) {
        if (out_bound_port) *out_bound_port = candidate;
        return server_fd;
      }
      listen_diag(diag, "std.http.listen could not start listening", "listening TCP socket", strerror(errno), NULL);
      close(server_fd);
      return -1;
    }

    int bind_errno = errno;
    close(server_fd);
    server_fd = -1;
    if (!config->auto_increment_port || bind_errno != EADDRINUSE) {
      listen_diag(diag, "std.http.listen could not bind the requested port", "available loopback TCP port", strerror(bind_errno), config->auto_increment_port ? "choose another start port or stop the process already using it" : "omit the port to auto-select the next free dev port, or stop the process already using it");
      return -1;
    }
  }
  listen_diag(diag, "std.http.listen could not find a free loopback port", "available loopback TCP port at or above the requested port", "all candidate ports were busy", "pass an explicit free port");
  return -1;
}

int z_http_listen_run(const ZHttpListenRunConfig *config, ZDiag *diag) {
  if (!config || config->port == 0 || !config->handler || !config->handler[0]) {
    listen_diag(diag, "std.http.listen requires a port and handler", "literal port and same-module handle function", "missing listener configuration", NULL);
    return 1;
  }

  char handler_exe[PATH_MAX];
  char temp_dir[PATH_MAX];
  if (!z_http_listen_create_temp_dir(temp_dir, sizeof(temp_dir), diag)) return 1;
  if (!build_handler_graph(config, temp_dir, handler_exe, sizeof(handler_exe), diag)) {
    z_http_listen_cleanup_temp_dir(temp_dir);
    return 1;
  }
  signal(SIGPIPE, SIG_IGN);
  uint16_t bound_port = 0;
  int server_fd = listen_open_server_socket(config, &bound_port, diag);
  if (server_fd < 0) {
    z_http_listen_cleanup_temp_dir(temp_dir);
    return 1;
  }

  ListenSignalState signal_state;
  listen_install_stop_handlers(server_fd, &signal_state);
  fprintf(stderr, "listening on http://127.0.0.1:%u\n", (unsigned)bound_port);
  uint64_t request_id = 0;
  for (;;) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
      if (listen_stop_requested) {
        server_fd = -1;
        break;
      }
      if (errno == EINTR) continue;
      perror("zero listen accept");
      break;
    }
    configure_client_socket(client_fd);
    char request[Z_HTTP_LISTEN_REQUEST_CAP];
    size_t request_len = 0;
    unsigned request_status = 400;
    if (!read_http_request(client_fd, request, sizeof(request), &request_len, &request_status)) {
      const char *reason = request_status == 413 ? "Payload Too Large" : request_status == 408 ? "Request Timeout" : "Bad Request";
      const char *body = request_status == 413 ? "{\"error\":\"payload_too_large\"}" : request_status == 408 ? "{\"error\":\"request_timeout\"}" : "{\"error\":\"bad_request\"}";
      send_json_error(client_fd, request_status, reason, body);
      close(client_fd);
      continue;
    }
    request[request_len] = '\0';
    char request_path[PATH_MAX];
    request_id++;
    char request_leaf[64];
    snprintf(request_leaf, sizeof(request_leaf), "request-%" PRIu64 ".http", request_id);
    if (!z_http_listen_temp_path(temp_dir, request_leaf, request_path, sizeof(request_path), NULL)) {
      send_json_error(client_fd, 500, "Internal Server Error", "{\"error\":\"request_spool_failed\"}");
      close(client_fd);
      continue;
    }
    if (!write_request_file(request_path, request, request_len)) {
      unlink(request_path);
      send_json_error(client_fd, 500, "Internal Server Error", "{\"error\":\"request_spool_failed\"}");
      close(client_fd);
      continue;
    }
    char *response = NULL;
    size_t response_len = 0;
    ZHttpListenHandlerOutcome outcome = run_handler_capture(handler_exe, request_path, &response, &response_len);
    if (outcome == Z_HTTP_LISTEN_HANDLER_OK) {
      (void)send_all(client_fd, response, response_len);
      free(response);
    } else {
      const char *error_name = handler_outcome_error_name(outcome);
      char error_body[96];
      snprintf(error_body, sizeof(error_body), "{\"error\":\"%s\"}", error_name ? error_name : "handler_failed");
      send_json_error(client_fd, 500, "Internal Server Error", error_body);
    }
    unlink(request_path);
    close(client_fd);
  }

  listen_restore_stop_handlers(&signal_state);
  if (server_fd >= 0) close(server_fd);
  z_http_listen_cleanup_temp_dir(temp_dir);
  return 1;
}

#endif
