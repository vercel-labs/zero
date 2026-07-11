#include "program_graph_test.h"

#include "program_graph_contracts.h"
#include "program_graph_format.h"
#include "program_graph_manifest.h"
#include "program_graph_projection.h"
#include "program_graph_resolve.h"
#include "program_graph_semantics.h"
#include "program_graph_size.h"
#include "program_graph_store.h"
#include "program_graph_test_caps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef enum { PGT_VOID, PGT_INT, PGT_BOOL, PGT_STRING, PGT_SHAPE } PgtValueKind;

typedef struct PgtValue PgtValue;

typedef struct {
  char *name;
  PgtValue *value;
} PgtField;

struct PgtValue {
  PgtValueKind kind;
  long long int_value;
  bool bool_value;
  char *string_value;
  PgtField *fields;
  size_t field_len;
};

typedef struct {
  char *name;
  PgtValue value;
} PgtBinding;

typedef struct {
  PgtBinding *items;
  size_t len;
  size_t cap;
} PgtEnv;

typedef struct {
  const char *current_test;
  char message[256];
  const char *path;
  int line;
  int column;
} PgtFailure;

typedef struct {
  const char *name;
  const char *status;
  bool expected_failure;
  long long duration_ms;
  const ZProgramGraphNode *function;
  PgtFailure failure;
  char failure_message[256];
} PgtResult;

typedef struct {
  PgtResult *items;
  size_t len;
  size_t cap;
  size_t discovered;
  size_t selected;
  size_t passed;
  size_t failed;
  size_t expected_failures;
  size_t unexpected_passes;
  PgtFailure first_failure;
  long long duration_ms;
} PgtRun;

static long long pgt_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static bool pgt_eq(const char *left, const char *right) {
  const char *a = left ? left : "";
  const char *b = right ? right : "";
  size_t a_len = strlen(a);
  return a_len == strlen(b) && memcmp(a, b, a_len) == 0;
}

static bool pgt_starts(const char *text, const char *prefix) {
  return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static void pgt_append_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const char *cursor = value ? value : ""; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (ch == '"') zbuf_append(buf, "\\\"");
    else if (ch == '\\') zbuf_append(buf, "\\\\");
    else if (ch == '\n') zbuf_append(buf, "\\n");
    else if (ch == '\r') zbuf_append(buf, "\\r");
    else if (ch == '\t') zbuf_append(buf, "\\t");
    else if (ch < 0x20) zbuf_appendf(buf, "\\u%04x", (unsigned)ch);
    else zbuf_append_char(buf, (char)ch);
  }
  zbuf_append_char(buf, '"');
}

static const char *pgt_diag_code(int code) {
  if (code == 2002) return "BLD002";
  if (code == 2003) return "BLD003";
  if (code == 2004) return "BLD004";
  if (code == 9001) return "PKG001";
  if (code == 9002) return "PKG002";
  if (code == 9003) return "PKG003";
  if (code == 9004) return "PKG004";
  if (code >= 3000 && code < 4000) return "TYP001";
  return "PAR100";
}

static void pgt_print_diag(const char *path, const ZDiag *diag, bool json) {
  if (!json) {
    fprintf(stderr, "%s:%d:%d %s: %s\n", path ? path : "<input>", diag->line, diag->column, pgt_diag_code(diag->code), diag->message);
    if (diag->expected[0]) fprintf(stderr, "  expected: %s\n", diag->expected);
    if (diag->actual[0]) fprintf(stderr, "  actual: %s\n", diag->actual);
    if (diag->help[0]) fprintf(stderr, "  help: %s\n", diag->help);
    fprintf(stderr, "  explain: zero explain %s\n", pgt_diag_code(diag->code));
    return;
  }
  ZBuf out;
  zbuf_init(&out);
  zbuf_append(&out, "{\n  \"schemaVersion\": 1,\n  \"ok\": false,\n  \"diagnostics\": [{\"code\":");
  pgt_append_json_string(&out, pgt_diag_code(diag->code));
  zbuf_append(&out, ",\"message\":");
  pgt_append_json_string(&out, diag->message);
  zbuf_append(&out, ",\"path\":");
  pgt_append_json_string(&out, diag->path ? diag->path : path);
  zbuf_appendf(&out, ",\"line\":%d,\"column\":%d,\"length\":%d,\"expected\":", diag->line, diag->column, diag->length);
  pgt_append_json_string(&out, diag->expected);
  zbuf_append(&out, ",\"actual\":");
  pgt_append_json_string(&out, diag->actual);
  zbuf_append(&out, ",\"help\":");
  pgt_append_json_string(&out, diag->help);
  zbuf_append(&out, "}]\n}\n");
  fputs(out.data, stdout);
  zbuf_free(&out);
}

static void pgt_value_free(PgtValue *value) {
  if (!value) return;
  free(value->string_value);
  for (size_t i = 0; i < value->field_len; i++) {
    free(value->fields[i].name);
    pgt_value_free(value->fields[i].value);
    free(value->fields[i].value);
  }
  free(value->fields);
  *value = (PgtValue){0};
}

static PgtValue pgt_value_copy(const PgtValue *value) {
  PgtValue copy = value ? *value : (PgtValue){0};
  copy.string_value = value && value->string_value ? z_strdup(value->string_value) : NULL;
  copy.fields = NULL;
  copy.field_len = value ? value->field_len : 0;
  if (copy.field_len) copy.fields = z_checked_calloc(copy.field_len, sizeof(PgtField));
  for (size_t i = 0; value && i < value->field_len; i++) {
    copy.fields[i].name = z_strdup(value->fields[i].name);
    copy.fields[i].value = z_checked_calloc(1, sizeof(PgtValue));
    *copy.fields[i].value = pgt_value_copy(value->fields[i].value);
  }
  return copy;
}

static bool pgt_env_set(PgtEnv *env, const char *name, const PgtValue *value) {
  if (!env || !name) return false;
  for (size_t i = 0; i < env->len; i++) {
    if (pgt_eq(env->items[i].name, name)) {
      pgt_value_free(&env->items[i].value);
      env->items[i].value = pgt_value_copy(value);
      return true;
    }
  }
  if (env->len == env->cap) {
    env->cap = env->cap ? env->cap * 2 : 8;
    env->items = z_checked_reallocarray(env->items, env->cap, sizeof(PgtBinding));
  }
  env->items[env->len].name = z_strdup(name);
  env->items[env->len].value = pgt_value_copy(value);
  env->len++;
  return true;
}

static bool pgt_env_assign(PgtEnv *env, const char *name, const PgtValue *value) {
  if (!env || !name) return false;
  for (size_t i = 0; i < env->len; i++) {
    if (!pgt_eq(env->items[i].name, name)) continue;
    pgt_value_free(&env->items[i].value);
    env->items[i].value = pgt_value_copy(value);
    return true;
  }
  return false;
}

static bool pgt_env_get(const PgtEnv *env, const char *name, PgtValue *out) {
  for (size_t i = 0; env && name && i < env->len; i++) {
    if (pgt_eq(env->items[i].name, name)) {
      *out = pgt_value_copy(&env->items[i].value);
      return true;
    }
  }
  return false;
}

static void pgt_env_free(PgtEnv *env) {
  for (size_t i = 0; env && i < env->len; i++) {
    free(env->items[i].name);
    pgt_value_free(&env->items[i].value);
  }
  free(env ? env->items : NULL);
  if (env) *env = (PgtEnv){0};
}

static bool pgt_truthy(const PgtValue *value) {
  if (!value) return false;
  if (value->kind == PGT_BOOL) return value->bool_value;
  if (value->kind == PGT_INT) return value->int_value != 0;
  if (value->kind == PGT_STRING) return value->string_value && value->string_value[0];
  return value->kind == PGT_SHAPE;
}

static bool pgt_value_equals(const PgtValue *left, const PgtValue *right) {
  if (!left || !right) return false;
  if (left->kind == PGT_STRING && right->kind == PGT_STRING) return pgt_eq(left->string_value, right->string_value);
  if (left->kind == PGT_BOOL && right->kind == PGT_BOOL) return left->bool_value == right->bool_value;
  if ((left->kind == PGT_INT || left->kind == PGT_BOOL) && (right->kind == PGT_INT || right->kind == PGT_BOOL)) {
    long long a = left->kind == PGT_BOOL ? (left->bool_value ? 1 : 0) : left->int_value;
    long long b = right->kind == PGT_BOOL ? (right->bool_value ? 1 : 0) : right->int_value;
    return a == b;
  }
  return false;
}

static const ZProgramGraphNode *pgt_node(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) if (pgt_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  return NULL;
}

static const ZProgramGraphEdge *pgt_next_edge(const ZProgramGraph *graph, const char *from, const char *kind, bool have_last, size_t last_order) {
  const ZProgramGraphEdge *best = NULL;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !pgt_eq(edge->from, from) || !pgt_eq(edge->kind, kind) || (have_last && edge->order <= last_order)) continue;
    if (!best || edge->order < best->order) best = edge;
  }
  return best;
}

static const ZProgramGraphNode *pgt_child(const ZProgramGraph *graph, const char *from, const char *kind, size_t order) {
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && edge->order == order && pgt_eq(edge->from, from) && pgt_eq(edge->kind, kind)) return pgt_node(graph, edge->to);
  }
  return NULL;
}

static size_t pgt_child_count(const ZProgramGraph *graph, const char *from, const char *kind) {
  size_t count = 0;
  for (size_t i = 0; graph && from && kind && i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE && pgt_eq(edge->from, from) && pgt_eq(edge->kind, kind)) count++;
  }
  return count;
}

static bool pgt_is_test_function(const ZProgramGraphNode *node) {
  return node && node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && pgt_starts(node->name, "__zero_test_");
}

static const char *pgt_test_name(const ZProgramGraphNode *fun) {
  return fun && fun->value && fun->value[0] ? fun->value : (fun && fun->name ? fun->name : "");
}

static const ZProgramGraphNode *pgt_function(const ZProgramGraph *graph, const char *name) {
  for (size_t i = 0; graph && name && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && pgt_eq(node->name, name)) return node;
  }
  return NULL;
}

static void pgt_fail(PgtFailure *failure, const ZProgramGraphNode *node, const char *message) {
  if (!failure || failure->message[0]) return;
  if (node) {
    failure->path = node->path;
    failure->line = node->line > 0 ? node->line : 1;
    failure->column = node->column > 0 ? node->column : 1;
  }
  snprintf(failure->message, sizeof(failure->message), "%s%s%s", message ? message : "zero test expectation failed", failure->current_test ? ": " : "", failure->current_test ? failure->current_test : "");
}

static void pgt_fail_unsupported(PgtFailure *failure, const ZProgramGraphNode *node, const char *subject) {
  char message[256];
  snprintf(message, sizeof(message), "zero graph test runner does not support %s %s yet; supported graph tests are pure locals, literals, calls, field access, control flow, comparisons, and std.testing helpers; use live smoke tests for hosted IO/HTTP", node ? z_program_graph_node_kind_name(node->kind) : "unknown", subject ? subject : "node");
  pgt_fail(failure, node, message);
}

static bool pgt_expr_name(const ZProgramGraph *graph, const ZProgramGraphNode *expr, ZBuf *out) {
  if (!expr) return false;
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) { zbuf_append(out, expr->name ? expr->name : ""); return true; }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) {
    if (!pgt_expr_name(graph, pgt_child(graph, expr->id, "left", 0), out)) return false;
    zbuf_append_char(out, '.');
    zbuf_append(out, expr->name ? expr->name : "");
    return true;
  }
  if ((expr->kind == Z_PROGRAM_GRAPH_NODE_CALL || expr->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) && expr->name) {
    const ZProgramGraphNode *left = pgt_child(graph, expr->id, "left", 0);
    if (left && pgt_expr_name(graph, left, out)) return true;
    zbuf_append(out, expr->name);
    return true;
  }
  return false;
}

static bool pgt_eval_expr(const ZProgramGraph *graph, PgtEnv *env, const ZProgramGraphNode *expr, PgtValue *out, PgtFailure *failure);

enum { PGT_LOOP_NONE = 0, PGT_LOOP_BREAK = 1, PGT_LOOP_CONTINUE = 2 };

static bool pgt_eval_block(const ZProgramGraph *graph, PgtEnv *env, const ZProgramGraphNode *block, PgtValue *ret, bool *returned, int *loop_signal, PgtFailure *failure) {
  bool have_last = false;
  size_t last_order = 0;
  for (const ZProgramGraphEdge *edge = pgt_next_edge(graph, block ? block->id : NULL, "statement", false, 0);
       edge;
       edge = pgt_next_edge(graph, block ? block->id : NULL, "statement", have_last, last_order)) {
    have_last = true;
    last_order = edge->order;
    const ZProgramGraphNode *stmt = pgt_node(graph, edge->to);
    if (!stmt) continue;
    if (stmt->kind == Z_PROGRAM_GRAPH_NODE_LET) {
      PgtValue value = {0};
      if (!pgt_eval_expr(graph, env, pgt_child(graph, stmt->id, "expr", 0), &value, failure)) return false;
      pgt_env_set(env, stmt->name, &value);
      pgt_value_free(&value);
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_ASSIGNMENT) {
      const ZProgramGraphNode *target = pgt_child(graph, stmt->id, "target", 0);
      if (!target || target->kind != Z_PROGRAM_GRAPH_NODE_IDENTIFIER) { pgt_fail(failure, stmt, "zero graph test runner supports only local assignments"); return false; }
      PgtValue value = {0};
      bool ok = pgt_eval_expr(graph, env, pgt_child(graph, stmt->id, "expr", 0), &value, failure) &&
                pgt_env_assign(env, target->name, &value);
      pgt_value_free(&value);
      if (!ok) { pgt_fail(failure, stmt, "zero graph test assignment target was not found"); return false; }
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_RETURN) {
      const ZProgramGraphNode *expr = pgt_child(graph, stmt->id, "expr", 0);
      if (expr && !pgt_eval_expr(graph, env, expr, ret, failure)) return false;
      if (!expr) ret->kind = PGT_VOID;
      *returned = true;
      return true;
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_EXPRESSION_STATEMENT) {
      const ZProgramGraphNode *expr = pgt_child(graph, stmt->id, "expr", 0);
      PgtValue ignored = {0};
      bool ok = pgt_eval_expr(graph, env, expr, &ignored, failure);
      pgt_value_free(&ignored);
      if (!ok) return false;
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_IF) {
      PgtValue condition = {0};
      if (!pgt_eval_expr(graph, env, pgt_child(graph, stmt->id, "expr", 0), &condition, failure)) return false;
      bool branch = pgt_truthy(&condition);
      pgt_value_free(&condition);
      if (!pgt_eval_block(graph, env, pgt_child(graph, stmt->id, branch ? "then" : "else", branch ? 0 : 1), ret, returned, loop_signal, failure)) return false;
      if (*returned) return true;
      if (loop_signal && *loop_signal != PGT_LOOP_NONE) return true;
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_WHILE) {
      for (size_t iterations = 0; iterations < 100000; iterations++) {
        PgtValue condition = {0};
        if (!pgt_eval_expr(graph, env, pgt_child(graph, stmt->id, "expr", 0), &condition, failure)) return false;
        bool keep_going = pgt_truthy(&condition);
        pgt_value_free(&condition);
        if (!keep_going) break;
        int body_signal = PGT_LOOP_NONE;
        if (!pgt_eval_block(graph, env, pgt_child(graph, stmt->id, "then", 0), ret, returned, &body_signal, failure)) return false;
        if (*returned) return true;
        if (body_signal == PGT_LOOP_BREAK) break;
        if (iterations == 99999) { pgt_fail(failure, stmt, "zero graph test while loop exceeded iteration limit"); return false; }
      }
    } else if (stmt->kind == Z_PROGRAM_GRAPH_NODE_BREAK || stmt->kind == Z_PROGRAM_GRAPH_NODE_CONTINUE) {
      if (!loop_signal) { pgt_fail(failure, stmt, "zero graph test break or continue requires an enclosing loop"); return false; }
      *loop_signal = stmt->kind == Z_PROGRAM_GRAPH_NODE_BREAK ? PGT_LOOP_BREAK : PGT_LOOP_CONTINUE;
      return true;
    } else {
      pgt_fail_unsupported(failure, stmt, "statement");
      return false;
    }
  }
  return true;
}

static bool pgt_eval_function(const ZProgramGraph *graph, const ZProgramGraphNode *fun, const char *callee, const ZProgramGraphNode *call_node, const PgtValue *args, size_t arg_len, PgtValue *out, PgtFailure *failure) {
  if (!fun) {
    char message[256];
    if (callee && pgt_starts(callee, "std.")) {
      snprintf(message, sizeof(message), "zero graph test runner does not implement '%s' yet; tests can call std.testing helpers and pure zerolang functions; use live smoke tests for other std calls", callee);
    } else {
      snprintf(message, sizeof(message), "zero test unknown function '%s'", callee && callee[0] ? callee : "<expr>");
    }
    pgt_fail(failure, call_node, message);
    return false;
  }
  if (pgt_child_count(graph, fun->id, "param") != arg_len) { pgt_fail(failure, fun, "zero test function argument count mismatch"); return false; }
  PgtEnv local = {0};
  for (size_t i = 0; i < arg_len; i++) pgt_env_set(&local, pgt_child(graph, fun->id, "param", i)->name, &args[i]);
  bool returned = false;
  bool ok = pgt_eval_block(graph, &local, pgt_child(graph, fun->id, "body", 0), out, &returned, NULL, failure);
  pgt_env_free(&local);
  if (!ok) return false;
  if (!returned) out->kind = PGT_VOID;
  return true;
}

static bool pgt_eval_binary(const ZProgramGraph *graph, PgtEnv *env, const ZProgramGraphNode *expr, const char *op, PgtValue *out, PgtFailure *failure) {
  PgtValue left = {0}, right = {0};
  if (!pgt_eval_expr(graph, env, pgt_child(graph, expr->id, "left", 0), &left, failure) ||
      !pgt_eval_expr(graph, env, pgt_child(graph, expr->id, "right", 1), &right, failure)) { pgt_value_free(&left); pgt_value_free(&right); return false; }
  if (pgt_eq(op, "==") || pgt_eq(op, "!=")) {
    out->kind = PGT_BOOL;
    out->bool_value = pgt_value_equals(&left, &right);
    if (pgt_eq(op, "!=")) out->bool_value = !out->bool_value;
  } else if (pgt_eq(op, "&&") || pgt_eq(op, "||")) {
    out->kind = PGT_BOOL;
    out->bool_value = pgt_eq(op, "&&") ? (pgt_truthy(&left) && pgt_truthy(&right)) : (pgt_truthy(&left) || pgt_truthy(&right));
  } else {
    long long a = left.kind == PGT_BOOL ? (left.bool_value ? 1 : 0) : left.int_value;
    long long b = right.kind == PGT_BOOL ? (right.bool_value ? 1 : 0) : right.int_value;
    out->kind = PGT_INT;
    if (pgt_eq(op, "+")) out->int_value = a + b; else if (pgt_eq(op, "-")) out->int_value = a - b;
    else if (pgt_eq(op, "*")) out->int_value = a * b; else if (pgt_eq(op, "/")) out->int_value = b == 0 ? 0 : a / b;
    else if (pgt_eq(op, "%")) out->int_value = b == 0 ? 0 : a % b;
    else if (pgt_eq(op, "<") || pgt_eq(op, "<=") || pgt_eq(op, ">") || pgt_eq(op, ">=")) {
      out->kind = PGT_BOOL;
      if (pgt_eq(op, "<")) out->bool_value = a < b; else if (pgt_eq(op, "<=")) out->bool_value = a <= b;
      else if (pgt_eq(op, ">")) out->bool_value = a > b; else out->bool_value = a >= b;
    } else { pgt_fail(failure, expr, "zero test unsupported operator"); pgt_value_free(&left); pgt_value_free(&right); return false; }
  }
  pgt_value_free(&left);
  pgt_value_free(&right);
  return true;
}

static bool pgt_std_call(const char *name, const PgtValue *args, size_t arg_len, PgtValue *out) {
  out->kind = PGT_BOOL;
  if (pgt_eq(name, "std.testing.isTrue") && arg_len == 1) out->bool_value = pgt_truthy(&args[0]);
  else if (pgt_eq(name, "std.testing.isFalse") && arg_len == 1) out->bool_value = !pgt_truthy(&args[0]);
  else if ((pgt_eq(name, "std.mem.eql") || pgt_starts(name, "std.testing.equal")) && arg_len == 2) out->bool_value = pgt_value_equals(&args[0], &args[1]);
  else if (pgt_eq(name, "std.testing.containsBytes") && arg_len == 2 && args[0].kind == PGT_STRING && args[1].kind == PGT_STRING) out->bool_value = strstr(args[0].string_value, args[1].string_value) != NULL;
  else if (pgt_eq(name, "std.testing.startsWith") && arg_len == 2 && args[0].kind == PGT_STRING && args[1].kind == PGT_STRING) out->bool_value = pgt_starts(args[0].string_value, args[1].string_value);
  else if (pgt_eq(name, "std.testing.endsWith") && arg_len == 2 && args[0].kind == PGT_STRING && args[1].kind == PGT_STRING) {
    size_t len = strlen(args[0].string_value), suffix = strlen(args[1].string_value);
    out->bool_value = suffix <= len && memcmp(args[0].string_value + len - suffix, args[1].string_value, suffix) == 0;
  } else return false;
  return true;
}

static bool pgt_eval_call(const ZProgramGraph *graph, PgtEnv *env, const ZProgramGraphNode *expr, PgtValue *out, PgtFailure *failure) {
  const char *op = expr && expr->name ? expr->name : "";
  if (pgt_child(graph, expr->id, "right", 1) && (pgt_eq(op, "==") || pgt_eq(op, "!=") || pgt_eq(op, "&&") || pgt_eq(op, "||") || pgt_eq(op, "+") || pgt_eq(op, "-") || pgt_eq(op, "*") || pgt_eq(op, "/") || pgt_eq(op, "%") || pgt_eq(op, "<") || pgt_eq(op, "<=") || pgt_eq(op, ">") || pgt_eq(op, ">="))) return pgt_eval_binary(graph, env, expr, op, out, failure);
  ZBuf name;
  zbuf_init(&name);
  if (!pgt_expr_name(graph, pgt_child(graph, expr->id, "left", 0), &name) && expr->name) zbuf_append(&name, expr->name);
  const char *callee = name.data ? name.data : "";
  size_t arg_len = pgt_child_count(graph, expr->id, "arg");
  PgtValue *args = z_checked_calloc(arg_len ? arg_len : 1, sizeof(PgtValue));
  for (size_t i = 0; i < arg_len; i++) if (!pgt_eval_expr(graph, env, pgt_child(graph, expr->id, "arg", i), &args[i], failure)) { for (size_t j = 0; j <= i; j++) pgt_value_free(&args[j]); free(args); zbuf_free(&name); return false; }
  bool ok = true;
  if (pgt_eq(callee, "expect") && arg_len == 1) {
    out->kind = PGT_VOID;
    if (!pgt_truthy(&args[0])) { pgt_fail(failure, expr, "zero test expectation failed"); ok = false; }
  } else if (!pgt_std_call(callee, args, arg_len, out)) {
    ok = pgt_eval_function(graph, pgt_function(graph, callee), callee, expr, args, arg_len, out, failure);
  }
  for (size_t i = 0; i < arg_len; i++) pgt_value_free(&args[i]);
  free(args);
  zbuf_free(&name);
  return ok;
}

static bool pgt_eval_expr(const ZProgramGraph *graph, PgtEnv *env, const ZProgramGraphNode *expr, PgtValue *out, PgtFailure *failure) {
  if (!expr || !out) return false;
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_LITERAL) {
    const char *value = expr->value ? expr->value : "";
    if (pgt_eq(value, "true") || pgt_eq(value, "false") || pgt_eq(expr->type, "Bool")) { out->kind = PGT_BOOL; out->bool_value = pgt_eq(value, "true"); }
    else if (pgt_eq(expr->type, "String") || pgt_eq(expr->type, "Span<u8>")) { out->kind = PGT_STRING; out->string_value = z_strdup(value); }
    else { out->kind = PGT_INT; out->int_value = strtoll(value, NULL, 0); }
    return true;
  }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
    if (pgt_env_get(env, expr->name, out)) return true;
    for (size_t i = 0; expr->name && i < graph->node_len; i++) {
      const ZProgramGraphNode *node = &graph->nodes[i];
      if (node->kind == Z_PROGRAM_GRAPH_NODE_CONST && pgt_eq(node->name, expr->name)) {
        return pgt_eval_expr(graph, env, pgt_child(graph, node->id, "value", 0), out, failure);
      }
    }
    pgt_fail(failure, expr, "zero test unknown identifier");
    return false;
  }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) {
    PgtValue base = {0};
    if (!pgt_eval_expr(graph, env, pgt_child(graph, expr->id, "left", 0), &base, failure)) return false;
    for (size_t i = 0; base.kind == PGT_SHAPE && i < base.field_len; i++) if (pgt_eq(base.fields[i].name, expr->name)) { *out = pgt_value_copy(base.fields[i].value); pgt_value_free(&base); return true; }
    pgt_value_free(&base);
    pgt_fail(failure, expr, "zero test unknown field");
    return false;
  }
  if (expr->kind == Z_PROGRAM_GRAPH_NODE_CALL || expr->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) return pgt_eval_call(graph, env, expr, out, failure);
  pgt_fail_unsupported(failure, expr, "expression");
  return false;
}

static size_t pgt_count_tests(const ZProgramGraph *graph, const char *filter) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) if (pgt_is_test_function(&graph->nodes[i]) && (!filter || !filter[0] || strstr(pgt_test_name(&graph->nodes[i]), filter))) count++;
  return count;
}

static bool pgt_expected_failure(const char *name) {
  return pgt_starts(name, "xfail:") || pgt_starts(name, "expected fail:") || (name && strstr(name, "[xfail]"));
}

static void pgt_result_push(PgtRun *run, const PgtResult *result) {
  if (run->len == run->cap) {
    run->cap = run->cap ? run->cap * 2 : 8;
    run->items = z_checked_reallocarray(run->items, run->cap, sizeof(PgtResult));
  }
  run->items[run->len++] = *result;
}

static PgtRun pgt_run_tests(const ZProgramGraph *graph, const char *filter) {
  long long started = pgt_now_ms();
  PgtRun run = {.discovered = pgt_count_tests(graph, NULL)};
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *fun = &graph->nodes[i];
    if (!pgt_is_test_function(fun) || (filter && filter[0] && !strstr(pgt_test_name(fun), filter))) continue;
    PgtResult result = {.name = pgt_test_name(fun), .status = "passed", .function = fun, .expected_failure = pgt_expected_failure(pgt_test_name(fun))};
    result.failure.current_test = result.name;
    result.failure.path = fun->path;
    result.failure.line = fun->line > 0 ? fun->line : 1;
    result.failure.column = fun->column > 0 ? fun->column : 1;
    long long test_started = pgt_now_ms();
    PgtValue ignored = {0};
    bool ok = pgt_eval_function(graph, fun, result.name, fun, NULL, 0, &ignored, &result.failure);
    result.duration_ms = pgt_now_ms() - test_started;
    pgt_value_free(&ignored);
    run.selected++;
    if (result.expected_failure && ok) { result.status = "unexpected-pass"; snprintf(result.failure_message, sizeof(result.failure_message), "expected failure unexpectedly passed"); run.failed++; run.unexpected_passes++; }
    else if (result.expected_failure && !ok) { result.status = "expected-fail"; snprintf(result.failure_message, sizeof(result.failure_message), "%s", result.failure.message[0] ? result.failure.message : "expected failure"); run.expected_failures++; }
    else if (ok) run.passed++;
    else { result.status = "failed"; snprintf(result.failure_message, sizeof(result.failure_message), "%s", result.failure.message[0] ? result.failure.message : "zero test failed"); run.failed++; }
    if (run.failed && !run.first_failure.message[0]) run.first_failure = result.failure;
    pgt_result_push(&run, &result);
  }
  run.duration_ms = pgt_now_ms() - started;
  return run;
}

static void pgt_run_free(PgtRun *run) {
  free(run ? run->items : NULL);
  if (run) *run = (PgtRun){0};
}

static const char *pgt_discovery_mode(const SourceInput *input) {
  return input && input->package_name && input->package_name[0] ? "package-graph" : "program-graph";
}

static void pgt_append_location(ZBuf *buf, const SourceInput *input, const ZProgramGraphNode *node) {
  zbuf_append(buf, "{\"sourceFile\":");
  pgt_append_json_string(buf, node && node->path && node->path[0] ? node->path : (input && input->source_file ? input->source_file : ""));
  zbuf_appendf(buf, ",\"line\":%d,\"column\":%d}", node && node->line > 0 ? node->line : 1, node && node->column > 0 ? node->column : 1);
}

static void pgt_append_graph_json(ZBuf *buf, const ZProgramGraphTestCommand *command, const SourceInput *input, const char *projection_state, bool canonical_source) {
  zbuf_append(buf, ",\n  \"graph\": {\"artifact\":");
  pgt_append_json_string(buf, command ? command->input : "");
  zbuf_append(buf, ",\"canonicalSource\":");
  zbuf_append(buf, canonical_source ? "true" : "false");
  zbuf_append(buf, ",\"moduleIdentity\":");
  pgt_append_json_string(buf, input ? input->program_graph_module_identity : "");
  zbuf_append(buf, ",\"graphHash\":");
  pgt_append_json_string(buf, input ? input->program_graph_hash : "");
  zbuf_append(buf, ",\"lowering\":\"direct-program-graph\"");
  if (projection_state) {
    zbuf_append(buf, ",\"sourceProjectionState\":");
    pgt_append_json_string(buf, projection_state);
  }
  zbuf_append(buf, "}");
}

static void pgt_print_json(const ZProgramGraphTestCommand *command, const SourceInput *input, const ZProgramGraph *graph, const ZTargetInfo *target, const PgtRun *run, const char *projection_state) {
  char stdout_text[64];
  snprintf(stdout_text, sizeof(stdout_text), "%zu test(s) ok\n", run ? run->selected : 0);
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(&buf, run && run->failed == 0 ? "true" : "false");
  zbuf_append(&buf, ",\n  \"sourceFile\": ");
  pgt_append_json_string(&buf, input ? input->source_file : "");
  bool canonical_source = graph && graph->canonical_source && !(command && command->repository_graph_input);
  pgt_append_graph_json(&buf, command, input, projection_state, canonical_source);
  zbuf_append(&buf, ",\n  \"target\": ");
  pgt_append_json_string(&buf, target ? target->name : z_host_target());
  zbuf_append(&buf, ",\n  \"testBackend\": \"direct-program-graph\",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false");
  zbuf_appendf(&buf, ",\n  \"selectedTests\": %zu,\n  \"discoveredTests\": %zu,\n  \"passedTests\": %zu,\n  \"failedTests\": %zu,\n  \"expectedFailures\": %zu,\n  \"unexpectedPasses\": %zu,\n  \"durationMs\": %lld,\n  \"exitCode\": %s",
               run->selected, run->discovered, run->passed, run->failed, run->expected_failures, run->unexpected_passes, run->duration_ms, run->failed == 0 ? "0" : "1");
  zbuf_append(&buf, ",\n  \"stdout\": ");
  pgt_append_json_string(&buf, run->failed == 0 ? stdout_text : "");
  zbuf_append(&buf, ",\n  \"stderr\": ");
  pgt_append_json_string(&buf, run->first_failure.message);
  zbuf_append(&buf, ",\n  \"testDiscovery\": {\"mode\":");
  pgt_append_json_string(&buf, pgt_discovery_mode(input));
  zbuf_append(&buf, ",\"filter\":");
  if (command && command->filter) pgt_append_json_string(&buf, command->filter); else zbuf_append(&buf, "null");
  zbuf_append(&buf, ",\"packageRoot\":");
  pgt_append_json_string(&buf, input && input->package_root ? input->package_root : "");
  zbuf_append(&buf, ",\"manifestPath\":");
  pgt_append_json_string(&buf, input && input->manifest_path ? input->manifest_path : "");
  zbuf_appendf(&buf, ",\"sourceFileCount\":%zu,\"moduleCount\":%zu,\"discoveredTests\":%zu,\"selectedTests\":%zu}",
               input ? input->source_file_count : 0, input ? input->module_count : 0, run->discovered, run->selected);
  zbuf_append(&buf, ",\n  \"fixtures\": {\"sourceFiles\":[");
  for (size_t i = 0; input && i < input->source_file_count; i++) { if (i) zbuf_append(&buf, ", "); pgt_append_json_string(&buf, input->source_files[i]); }
  zbuf_append(&buf, "],\"goldenOutput\":");
  pgt_append_json_string(&buf, stdout_text);
  zbuf_append(&buf, ",\"snapshotKey\":\"zero-test-graph-native-v1\"}");
  zbuf_append(&buf, ",\n  \"targetFacts\": {\"hostTarget\":");
  pgt_append_json_string(&buf, z_host_target());
  zbuf_append(&buf, ",\"capabilitySupport\":{\"status\":\"supported\",\"missingCapabilities\":[]}}");
  zbuf_append(&buf, ",\n  \"results\": [");
  for (size_t i = 0; run && i < run->len; i++) {
    const PgtResult *result = &run->items[i];
    if (i) zbuf_append(&buf, ", ");
    zbuf_append(&buf, "{\"name\":");
    pgt_append_json_string(&buf, result->name);
    zbuf_append(&buf, ",\"status\":");
    pgt_append_json_string(&buf, result->status);
    zbuf_appendf(&buf, ",\"expectedFailure\":%s,\"durationMs\":%lld,\"location\":", result->expected_failure ? "true" : "false", result->duration_ms);
    pgt_append_location(&buf, input, result->function);
    zbuf_append(&buf, ",\"failure\":");
    if (result->failure_message[0]) {
      zbuf_append(&buf, "{\"message\":");
      pgt_append_json_string(&buf, result->failure_message);
      zbuf_append(&buf, ",\"sourceFile\":");
      pgt_append_json_string(&buf, result->failure.path ? result->failure.path : (result->function ? result->function->path : ""));
      zbuf_appendf(&buf, ",\"line\":%d,\"column\":%d}", result->failure.line > 0 ? result->failure.line : 1, result->failure.column > 0 ? result->failure.column : 1);
    } else zbuf_append(&buf, "null");
    zbuf_append(&buf, "}");
  }
  zbuf_append(&buf, "]\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static void pgt_seed_package_name(SourceInput *input, const char *module_identity) {
  if (!input || input->package_name || !pgt_starts(module_identity, "package:")) return;
  const char *start = module_identity + strlen("package:");
  const char *end = strchr(start, '@');
  size_t len = end && end > start ? (size_t)(end - start) : strlen(start);
  input->package_name = z_checked_calloc(len + 1, 1);
  memcpy(input->package_name, start, len);
}

static bool pgt_target_capabilities_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZTargetInfo *target, const char *filter, ZDiag *diag) {
  return z_program_graph_test_target_capabilities_ok(graph, resolution, target, filter, diag);
}

static bool pgt_validate_graph_input(const ZProgramGraph *graph, const SourceInput *input, const ZTargetInfo *target, const char *path, const char *filter, ZDiag *diag) {
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  bool ok = z_program_graph_name_contracts_ok(graph, path, diag) &&
            z_program_graph_collect_resolution_facts(graph, &resolution) &&
            z_program_graph_semantic_contracts_ok(graph, &resolution, path, diag) &&
            pgt_target_capabilities_ok(graph, &resolution, target, filter, diag);
  z_program_graph_resolution_facts_free(&resolution);
  (void)input;
  return ok;
}

int z_program_graph_run_tests_direct(const ZProgramGraphTestCommand *command, const ZTargetInfo *target, ZDiag *diag) {
  if (!command || !command->input) return 1;
  SourceInput input = {0};
  ZProgramGraph graph = {0};
  ZProgramGraphStore store;
  z_program_graph_store_init(&store);
  ZProgramGraph *active_graph = &graph;
  const char *projection_state = NULL;
  bool loaded = false;
  if (command->repository_graph_input) {
    loaded = z_program_graph_store_load_path(command->input, &store, diag);
    active_graph = &store.graph;
    projection_state = z_program_graph_projection_state_label(&store, target, NULL, NULL, NULL);
  } else loaded = z_program_graph_load(command->input, &graph, diag);
  if (!loaded) { pgt_print_diag(diag->path ? diag->path : command->input, diag, command->json); z_program_graph_store_free(&store); z_program_graph_free(&graph); return 1; }
  input.source_file = z_strdup(command->input);
  z_program_graph_seed_source_metadata(&input, active_graph);
  z_program_graph_seed_artifact_source_paths(&input, active_graph, command->input);
  input.program_graph_hash = z_strdup(active_graph->graph_hash ? active_graph->graph_hash : "");
  input.program_graph_module_identity = z_strdup(active_graph->module_identity ? active_graph->module_identity : "");
  pgt_seed_package_name(&input, active_graph->module_identity);
  if (command->repository_source_input && !z_program_graph_manifest_attach_metadata_to_input(&input, command->repository_source_input, diag)) {
    pgt_print_diag(diag->path ? diag->path : command->repository_source_input, diag, command->json);
    z_free_source(&input); z_program_graph_store_free(&store); z_program_graph_free(&graph); return 1;
  }
  if (!pgt_validate_graph_input(active_graph, &input, target, command->input, command->filter, diag)) {
    pgt_print_diag(diag->path ? diag->path : command->input, diag, command->json);
    z_free_source(&input); z_program_graph_store_free(&store); z_program_graph_free(&graph); return 1;
  }
  PgtRun run = pgt_run_tests(active_graph, command->filter);
  if (command->json) pgt_print_json(command, &input, active_graph, target, &run, projection_state);
  else if (run.failed) fprintf(stderr, "%s\n", run.first_failure.message);
  else if (run.selected == 0 && run.discovered > 0) {
    printf("0 of %zu test(s) matched --filter %s; nothing ran\n", run.discovered, command->filter ? command->filter : "");
  } else if (run.selected == 0) {
    size_t function_count = 0;
    size_t module_count = 0;
    for (size_t i = 0; i < active_graph->node_len; i++) {
      const ZProgramGraphNode *node = &active_graph->nodes[i];
      if (node->kind == Z_PROGRAM_GRAPH_NODE_MODULE) module_count++;
      if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && (!node->value || !node->value[0])) function_count++;
    }
    printf("0 test(s) ok\n");
    printf("note: no test blocks found; validated %zu function%s in %zu module%s (name, semantic, and capability contracts)\n",
           function_count,
           function_count == 1 ? "" : "s",
           module_count,
           module_count == 1 ? "" : "s");
    printf("tip: add a test block in source, or zero patch --op 'addTest name=\"...\" call=\"<fn>\" arg0=\"...\" expect=\"...\" type=\"<type>\"'\n");
  } else printf("%zu test(s) ok\n", run.selected);
  int rc = run.failed ? 1 : 0;
  pgt_run_free(&run);
  z_free_source(&input);
  z_program_graph_store_free(&store);
  z_program_graph_free(&graph);
  return rc;
}
