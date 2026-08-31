#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "zero.h"
#include "abi_report.h"
#include "buildability.h"
#include "c_import.h"
#include "capability_summary.h"
#include "canonical_text.h"
#include "cli_help.h"
#include "http_listen_runner.h"
#include "init_template.h"
#include "mir_binary.h"
#include "process_path.h"
#include "program_graph_build.h"
#include "program_graph_check_gate.h"
#include "program_graph_command.h"
#include "program_graph_compare.h"
#include "program_graph_contracts.h"
#include "program_graph_format.h"
#include "program_graph_import.h"
#include "program_graph_lower.h"
#include "program_graph_manifest.h"
#include "program_graph_patch.h"
#include "program_graph_projection.h"
#include "program_graph_query.h"
#include "program_graph_reconcile.h"
#include "program_graph_reconcile_apply.h"
#include "program_graph_report.h"
#include "program_graph_rewrite.h"
#include "program_graph_resolve.h"
#include "program_graph_repository.h"
#include "program_graph_repository_input.h"
#include "program_graph_roundtrip.h"
#include "program_graph_semantics.h"
#include "program_graph_size.h"
#include "program_graph_source_map.h"
#include "program_graph_store.h"
#include "program_graph_store_binary.h"
#include "program_graph_store_tables.h"
#include "program_graph_test.h"
#include "program_graph_view.h"
#include "process_exec.h"
#include "safety_contract.h"
#include "std_sig.h"
#include "std_source.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#else
#include <process.h>
#endif
#include <unistd.h>

#include "embedded_runtime_sources.inc"
#include "embedded_skills.inc"
typedef enum {
  EMIT_C,
  EMIT_EXE,
  EMIT_OBJ,
  EMIT_LLVM_IR
} EmitKind;

typedef struct {
  const char *command;
  const char *kind;
  const char *input;
  char *owned_input;
  const char *repository_graph_source_input;
  const char *out;
  const char *patch_file;
  const char *patch_text;
  const char *patch_expect_graph_hash;
  const char *patch_replace_fn;
  const char *patch_body_file;
  const char *patch_replace_in_fn;
  const char *patch_old_text;
  const char *patch_old_file;
  const char *patch_new_text;
  const char *patch_new_file;
  const char *patch_rewrite;
  const char *patch_rewrite_to;
  const char *reconcile_source;
  const char *merge_base;
  const char *merge_left;
  const char *merge_right;
  const char *store_format;
  const char *manifest_format;
  const char *init_template;
  const char *query_find;
  const char *query_function;
  const char *query_node;
  const char *query_refs;
  const char *query_calls;
  const char *query_depth;
  const char *query_bare_argument;
  const char *view_outline;
  const char *view_around;
  bool query_full;
  bool query_handles;
  bool query_no_help;
  const char **patch_ops;
  size_t patch_op_len;
  size_t patch_op_cap;
  const char *target;
  const char *profile;
  const char *cc;
  const char *backend;
  const char *unknown_flag;
  const char *invalid_emit;
  const char *retired_graph_subcommand;
  const char *filter;
  ZProgramGraphArtifactSource graph_source;
  int run_argc;
  char **run_argv;
  bool json;
  bool plan;
  bool apply;
  bool patch;
  bool all;
  bool fmt_check;
  bool fmt_write;
  bool legacy_backend;
  bool trace;
  bool graph_patch_command;
  bool graph_reconcile_command;
  bool graph_export_from_graph;
  bool graph_import_from_source;
  bool repository_graph_input;
  bool graph_patch_check_only;
  EmitKind emit;
} Command;

static char *command_process_owned_input = NULL;

static void command_free_process_owned_input(void) {
  free(command_process_owned_input);
  command_process_owned_input = NULL;
}

static void command_register_owned_input_cleanup(void) {
  static bool registered = false;
  if (registered) return;
  atexit(command_free_process_owned_input);
  registered = true;
}

static void command_set_owned_input(Command *command, char *input) {
  if (!command) {
    free(input);
    return;
  }
  command_register_owned_input_cleanup();
  if (command->owned_input && command->owned_input != input) {
    if (command_process_owned_input == command->owned_input) command_process_owned_input = NULL;
    free(command->owned_input);
  }
  if (command_process_owned_input && command_process_owned_input != input) free(command_process_owned_input);
  command_process_owned_input = input;
  command->owned_input = input;
  command->input = input;
}

static int command_return(Command *command, int rc) {
  if (command) {
    if (command_process_owned_input == command->owned_input) command_process_owned_input = NULL;
    free(command->owned_input);
    command->owned_input = NULL;
  }
  return rc;
}

static void manifest_path_for_input(const SourceInput *input, char *manifest_path, size_t manifest_path_len);
static bool is_program_graph_root_command(const char *command);

static const char *diag_code(int code) {
  switch (code) {
    case 1001: return "ERR001";
    case 1002: return "ERR002";
    case 1003: return "ERR003";
    case 1004: return "RGP009";
    case 1005: return "RGP003";
    case 1006: return "RGP007";
    case 1007: return "RGP007";
    case 1008: return "RGP004";
    case 1009: return "RGP006";
    case 1010: return "RGM002";
    case 2001: return "APP001";
    case 2002: return "BLD002";
    case 2003: return "BLD003";
    case 2004: return "BLD004";
    case 3001: return "NAM002";
    case 3002: return "NAM002";
    case 3003: return "NAM003";
    case 3004: return "NAM004";
    case 3005: return "TYP001";
    case 3006: return "TYP002";
    case 3007: return "TYP003";
    case 3008: return "NAM004";
    case 3009: return "TYP005";
    case 3010: return "TYP009";
    case 3011: return "STD002";
    case 3012: return "STD003";
    case 3013: return "OWN001";
    case 3014: return "OWN002";
    case 3015: return "MEM001";
    case 3016: return "TYP010";
    case 3017: return "TYP011";
    case 3018: return "TYP012";
    case 3019: return "TYP013";
    case 3020: return "TYP014";
    case 3021: return "TYP015";
    case 3022: return "TYP016";
    case 3023: return "TYP017";
    case 3025: return "TYP019";
    case 3026: return "TYP020";
    case 3027: return "TYP021";
    case 3028: return "TYP022";
    case 3029: return "BOR001";
    case 3030: return "BOR002";
    case 3031: return "ABI001";
    case 3032: return "TYP023";
    case 3033: return "TYP024";
    case 3034: return "TYP025";
    case 3035: return "MET001";
    case 3036: return "TYP026";
    case 3050: return "TYP027";
    case 3051: return "MEM002";
    case 3052: return "MEM003";
    case 3053: return "BOR003";
    case 3037: return "PUB001";
    case 3038: return "IFC001";
    case 3039: return "IFC002";
    case 3040: return "IFC003";
    case 3041: return "IFC004";
    case 3042: return "IFC005";
    case 3043: return "STC001";
    case 3044: return "STC002";
    case 3045: return "STC003";
    case 3046: return "SHM001";
    case 3047: return "SHM002";
    case 3048: return "RCV001";
    case 3049: return "RCV002";
    case 3101: return "FLD001";
    case 3102: return "FLD002";
    case 3103: return "VAR001";
    case 3104: return "VAR002";
    case 3105: return "MAT001";
    case 3106: return "MAT002";
    case 3107: return "MAT003";
    case 3108: return "VAR003";
    case 3109: return "VAR004";
    case 3110: return "MAT004";
    case 3111: return "MAT005";
    case 4004: return "CGEN004";
    case 6001: return "TAR001";
    case 6002: return "TAR002";
    case 7001: return "IMP001";
    case 7002: return "IMP002";
    case 7003: return "IMP003";
    case 8001: return "CIMP001";
    case 8002: return "CIMP002";
    case 8003: return "CIMP003";
    case 8004: return "CIMP004";
    case 8005: return "CIMP005";
    case 9001: return "PKG001";
    case 9002: return "PKG002";
    case 9003: return "PKG003";
    case 9004: return "PKG004";
    default: return "PAR100";
  }
}

static void print_diag(const char *path, const ZDiag *diag) {
  fprintf(stderr, "%s:%d:%d %s: %s\n", path ? path : "<input>", diag->line, diag->column, diag_code(diag->code), diag->message);
  if (diag->expected[0]) fprintf(stderr, "  expected: %s\n", diag->expected);
  if (diag->actual[0]) fprintf(stderr, "  actual: %s\n", diag->actual);
  if (diag->help[0]) fprintf(stderr, "  help: %s\n", diag->help);
  fprintf(stderr, "  explain: zero explain %s\n", diag_code(diag->code));
}

static bool json_utf8_continuation(unsigned char ch) {
  return (ch & 0xc0u) == 0x80u;
}

static size_t json_valid_utf8_len(const unsigned char *cursor) {
  unsigned char ch = cursor ? cursor[0] : 0;
  if (ch < 0x80) return ch ? 1 : 0;
  if (ch >= 0xc2 && ch <= 0xdf) {
    return cursor[1] && json_utf8_continuation(cursor[1]) ? 2 : 0;
  }
  if (ch == 0xe0) {
    return cursor[1] >= 0xa0 && cursor[1] <= 0xbf && cursor[2] && json_utf8_continuation(cursor[2]) ? 3 : 0;
  }
  if (ch >= 0xe1 && ch <= 0xec) {
    return cursor[1] && json_utf8_continuation(cursor[1]) && cursor[2] && json_utf8_continuation(cursor[2]) ? 3 : 0;
  }
  if (ch == 0xed) {
    return cursor[1] >= 0x80 && cursor[1] <= 0x9f && cursor[2] && json_utf8_continuation(cursor[2]) ? 3 : 0;
  }
  if (ch >= 0xee && ch <= 0xef) {
    return cursor[1] && json_utf8_continuation(cursor[1]) && cursor[2] && json_utf8_continuation(cursor[2]) ? 3 : 0;
  }
  if (ch == 0xf0) {
    return cursor[1] >= 0x90 && cursor[1] <= 0xbf && cursor[2] && json_utf8_continuation(cursor[2]) && cursor[3] && json_utf8_continuation(cursor[3]) ? 4 : 0;
  }
  if (ch >= 0xf1 && ch <= 0xf3) {
    return cursor[1] && json_utf8_continuation(cursor[1]) && cursor[2] && json_utf8_continuation(cursor[2]) && cursor[3] && json_utf8_continuation(cursor[3]) ? 4 : 0;
  }
  if (ch == 0xf4) {
    return cursor[1] >= 0x80 && cursor[1] <= 0x8f && cursor[2] && json_utf8_continuation(cursor[2]) && cursor[3] && json_utf8_continuation(cursor[3]) ? 4 : 0;
  }
  return 0;
}

static void append_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const char *cursor = value ? value : ""; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (ch == '"') zbuf_append(buf, "\\\"");
    else if (ch == '\\') zbuf_append(buf, "\\\\");
    else if (ch == '\n') zbuf_append(buf, "\\n");
    else if (ch == '\r') zbuf_append(buf, "\\r");
    else if (ch == '\t') zbuf_append(buf, "\\t");
    else if (ch < 0x20) zbuf_appendf(buf, "\\u%04x", (unsigned)ch);
    else if (ch >= 0x80) {
      size_t utf8_len = json_valid_utf8_len((const unsigned char *)cursor);
      if (utf8_len == 0) {
        zbuf_appendf(buf, "\\u%04x", (unsigned)ch);
      } else {
        for (size_t i = 0; i < utf8_len; i++) zbuf_append_char(buf, cursor[i]);
        cursor += utf8_len - 1;
      }
    }
    else zbuf_append_char(buf, (char)ch);
  }
  zbuf_append_char(buf, '"');
}

static void append_json_string_or_null(ZBuf *buf, const char *value) {
  if (value && value[0]) {
    append_json_string(buf, value);
  } else {
    zbuf_append(buf, "null");
  }
}

static void append_json_nullable_string(ZBuf *buf, const char *value) {
  if (value) append_json_string(buf, value);
  else zbuf_append(buf, "null");
}

static void print_json_string(const char *value) {
  ZBuf buf;
  zbuf_init(&buf);
  append_json_string(&buf, value);
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static const ZeroEmbeddedSkill *find_embedded_skill(const char *name) {
  for (size_t i = 0; i < zero_embedded_skill_count; i++) {
    if (strcmp(zero_embedded_skills[i].name, name) == 0) return &zero_embedded_skills[i];
  }
  return NULL;
}

static void append_embedded_skill_content(ZBuf *buf, const ZeroEmbeddedSkill *skill) {
  if (!skill || !skill->content) return;
  for (size_t i = 0; skill->content[i]; i++) {
    zbuf_append(buf, skill->content[i]);
  }
}

static void print_embedded_skill_content(const ZeroEmbeddedSkill *skill) {
  if (!skill || !skill->content) return;
  for (size_t i = 0; skill->content[i]; i++) {
    fputs(skill->content[i], stdout);
  }
}

static int embedded_skills_error(bool json, const char *message) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":false,\"error\":");
    append_json_string(&buf, message);
    zbuf_append(&buf, "}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "error: %s\n", message);
  }
  return 1;
}

static int embedded_skills_list_command(bool json) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"data\":[");
    bool first = true;
    for (size_t i = 0; i < zero_embedded_skill_count; i++) {
      const ZeroEmbeddedSkill *skill = &zero_embedded_skills[i];
      if (skill->hidden) continue;
      if (!first) zbuf_append(&buf, ",");
      first = false;
      zbuf_append(&buf, "{\"name\":");
      append_json_string(&buf, skill->name);
      zbuf_append(&buf, ",\"description\":");
      append_json_string(&buf, skill->description);
      zbuf_append(&buf, "}");
    }
    zbuf_append(&buf, "]}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
    return 0;
  }

  size_t max_name = 0;
  for (size_t i = 0; i < zero_embedded_skill_count; i++) {
    const ZeroEmbeddedSkill *skill = &zero_embedded_skills[i];
    if (!skill->hidden && strlen(skill->name) > max_name) max_name = strlen(skill->name);
  }
  if (max_name == 0) {
    printf("No skills found\n");
    return 0;
  }
  for (size_t i = 0; i < zero_embedded_skill_count; i++) {
    const ZeroEmbeddedSkill *skill = &zero_embedded_skills[i];
    if (skill->hidden) continue;
    printf("  %-*s  %s\n", (int)max_name, skill->name, skill->description);
  }
  return 0;
}

static size_t embedded_skill_heading_level(const char *line) {
  if (strncmp(line, "## ", 3) == 0) return 2;
  if (strncmp(line, "### ", 4) == 0) return 3;
  return 0;
}

static bool embedded_skill_extract_topic(const char *content, const char *topic, ZBuf *out) {
  size_t topic_len = strlen(topic);
  bool found = false;
  size_t in_section_level = 0;
  const char *line = content;
  while (line && *line) {
    const char *line_end = strchr(line, '\n');
    size_t line_len = line_end ? (size_t)(line_end - line) + 1 : strlen(line);
    size_t level = embedded_skill_heading_level(line);
    if (level > 0) {
      const char *title = line + level + 1;
      size_t title_len = line_len;
      while (title_len > 0 && (line[title_len - 1] == '\n' || line[title_len - 1] == '\r')) title_len--;
      title_len -= (size_t)(title - line);
      bool matches = title_len >= topic_len && strncmp(title, topic, topic_len) == 0;
      if (in_section_level > 0 && level <= in_section_level && !matches) {
        in_section_level = 0;
      }
      if (matches && in_section_level == 0) {
        if (found) zbuf_append_char(out, '\n');
        found = true;
        in_section_level = level;
      }
    }
    if (in_section_level > 0) {
      for (size_t i = 0; i < line_len; i++) zbuf_append_char(out, line[i]);
    }
    if (!line_end) break;
    line = line_end + 1;
  }
  return found;
}

static void embedded_skill_append_topic_headings(const char *content, ZBuf *out) {
  const char *line = content;
  bool first = true;
  while (line && *line) {
    const char *line_end = strchr(line, '\n');
    size_t level = embedded_skill_heading_level(line);
    if (level > 0) {
      const char *title = line + level + 1;
      size_t title_len = line_end ? (size_t)(line_end - title) : strlen(title);
      while (title_len > 0 && (title[title_len - 1] == '\n' || title[title_len - 1] == '\r')) title_len--;
      if (!first) zbuf_append(out, ", ");
      first = false;
      for (size_t i = 0; i < title_len; i++) zbuf_append_char(out, title[i]);
    }
    if (!line_end) break;
    line = line_end + 1;
  }
}

static int embedded_skills_get_command(int argc, char **argv, int subcommand_index, bool json) {
  bool get_all = false;
  const char *topic = NULL;
  const ZeroEmbeddedSkill *targets[64];
  size_t target_count = 0;

  for (int i = subcommand_index + 1; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--all") == 0) {
      get_all = true;
      continue;
    }
    if (strcmp(arg, "--topic") == 0) {
      if (i + 1 >= argc) {
        return embedded_skills_error(json, "Missing --topic value. Usage: zero skills get <name> --topic <section-prefix>");
      }
      topic = argv[++i];
      continue;
    }
    if (strcmp(arg, "--full") == 0 || strcmp(arg, "--json") == 0) continue;
    if (arg[0] == '-') {
      char message[160];
      snprintf(message, sizeof(message), "Unknown skills flag: %s", arg);
      return embedded_skills_error(json, message);
    }
    const ZeroEmbeddedSkill *skill = find_embedded_skill(arg);
    if (!skill) {
      char message[160];
      snprintf(message, sizeof(message), "Skill not found: %s", arg);
      return embedded_skills_error(json, message);
    }
    if (target_count < sizeof(targets) / sizeof(targets[0])) targets[target_count++] = skill;
  }

  if (get_all) {
    target_count = 0;
    for (size_t i = 0; i < zero_embedded_skill_count && target_count < sizeof(targets) / sizeof(targets[0]); i++) {
      if (!zero_embedded_skills[i].hidden) targets[target_count++] = &zero_embedded_skills[i];
    }
  }

  if (target_count == 0) {
    return embedded_skills_error(json, "No skill name provided. Usage: zero skills get <name>");
  }

  if (topic && (get_all || target_count != 1)) {
    return embedded_skills_error(json, "--topic scopes one skill. Usage: zero skills get <name> --topic <section-prefix>");
  }

  ZBuf topic_content;
  zbuf_init(&topic_content);
  if (topic) {
    ZBuf full;
    zbuf_init(&full);
    append_embedded_skill_content(&full, targets[0]);
    bool found = embedded_skill_extract_topic(full.data ? full.data : "", topic, &topic_content);
    if (!found) {
      ZBuf message;
      zbuf_init(&message);
      zbuf_appendf(&message, "No section in skill '%s' matches --topic %s. Sections: ", targets[0]->name, topic);
      embedded_skill_append_topic_headings(full.data ? full.data : "", &message);
      int rc = embedded_skills_error(json, message.data ? message.data : "");
      zbuf_free(&message);
      zbuf_free(&full);
      zbuf_free(&topic_content);
      return rc;
    }
    zbuf_free(&full);
  }

  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"data\":[");
    for (size_t i = 0; i < target_count; i++) {
      if (i > 0) zbuf_append(&buf, ",");
      zbuf_append(&buf, "{\"name\":");
      append_json_string(&buf, targets[i]->name);
      if (topic) {
        zbuf_append(&buf, ",\"topic\":");
        append_json_string(&buf, topic);
        zbuf_append(&buf, ",\"content\":");
        append_json_string(&buf, topic_content.data ? topic_content.data : "");
      } else {
        zbuf_append(&buf, ",\"content\":");
        ZBuf content;
        zbuf_init(&content);
        append_embedded_skill_content(&content, targets[i]);
        append_json_string(&buf, content.data ? content.data : "");
        zbuf_free(&content);
      }
      zbuf_append(&buf, "}");
    }
    zbuf_append(&buf, "]}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
    zbuf_free(&topic_content);
    return 0;
  }

  if (topic) {
    fputs(topic_content.data ? topic_content.data : "", stdout);
    zbuf_free(&topic_content);
    return 0;
  }
  zbuf_free(&topic_content);
  for (size_t i = 0; i < target_count; i++) {
    if (i > 0) printf("\n---\n\n");
    print_embedded_skill_content(targets[i]);
  }
  return 0;
}

static int embedded_skills_command(int argc, char **argv, bool json) {
  int subcommand_index = -1;
  const char *subcommand = "list";
  for (int i = 2; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--json") == 0 || strcmp(arg, "--all") == 0 || strcmp(arg, "--full") == 0) continue;
    if (strcmp(arg, "--topic") == 0) {
      i++;
      continue;
    }
    if (arg[0] == '-') {
      char message[160];
      snprintf(message, sizeof(message), "Unknown skills flag: %s", arg);
      return embedded_skills_error(json, message);
    }
    if (subcommand_index < 0) {
      subcommand = arg;
      subcommand_index = i;
    }
  }

  if (strcmp(subcommand, "help") == 0) {
    z_cli_print_command_help("skills");
    return 0;
  }
  if (strcmp(subcommand, "list") == 0) return embedded_skills_list_command(json);
  if (strcmp(subcommand, "get") == 0) return embedded_skills_get_command(argc, argv, subcommand_index >= 0 ? subcommand_index : 1, json);

  char message[160];
  snprintf(message, sizeof(message), "Unknown skills subcommand: %s", subcommand);
  return embedded_skills_error(json, message);
}

static const char *canonical_token_kind_name(ZCanonicalTokenKind kind) {
  switch (kind) {
    case Z_CANON_TOKEN_WORD: return "word";
    case Z_CANON_TOKEN_STRING: return "string";
    case Z_CANON_TOKEN_CHAR: return "char";
    case Z_CANON_TOKEN_NUMBER: return "number";
    case Z_CANON_TOKEN_SYMBOL: return "symbol";
    case Z_CANON_TOKEN_COMMENT: return "comment";
    case Z_CANON_TOKEN_NEWLINE: return "newline";
    case Z_CANON_TOKEN_EOF: return "eof";
  }
  return "unknown";
}

static void append_canonical_tokens_json(ZBuf *buf, const char *source_file, const ZCanonicalTokenVec *tokens) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, source_file);
  zbuf_append(buf, ",\n  \"syntax\": \"canonical\",\n  \"tokens\": [\n");
  for (size_t i = 0; tokens && i < tokens->len; i++) {
    const ZCanonicalToken *token = &tokens->items[i];
    zbuf_append(buf, "    {\"kind\": ");
    append_json_string(buf, canonical_token_kind_name(token->kind));
    zbuf_append(buf, ", \"text\": ");
    append_json_string(buf, token->text);
    zbuf_appendf(
      buf,
      ", \"line\": %d, \"column\": %d, \"offset\": %zu, \"length\": %zu}%s\n",
      token->line,
      token->column,
      token->offset,
      token->length,
      i + 1 < tokens->len ? "," : ""
    );
  }
  zbuf_append(buf, "  ]\n}\n");
}

static const char *stmt_kind_name(StmtKind kind) {
  switch (kind) {
    case STMT_LET: return "let";
    case STMT_ASSIGN: return "assign";
    case STMT_DEFER: return "defer";
    case STMT_CHECK: return "check";
    case STMT_RETURN: return "return";
    case STMT_EXPR: return "expr";
    case STMT_IF: return "if";
    case STMT_WHILE: return "while";
    case STMT_FOR: return "for";
    case STMT_BREAK: return "break";
    case STMT_CONTINUE: return "continue";
    case STMT_MATCH: return "match";
    case STMT_RAISE: return "raise";
  }
  return "unknown";
}

static void append_parse_json(ZBuf *buf, const char *source_file, const Program *program) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, source_file);
  zbuf_appendf(
    buf,
    ",\n  \"root\": {\"kind\": \"module\", \"shapeCount\": %zu, \"enumCount\": %zu, \"choiceCount\": %zu, \"functionCount\": %zu},\n",
    program ? program->shapes.len : 0,
    program ? program->enums.len : 0,
    program ? program->choices.len : 0,
    program ? program->functions.len : 0
  );
  zbuf_append(buf, "  \"shapes\": [");
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    const Shape *shape = &program->shapes.items[i];
    zbuf_append(buf, i == 0 ? "\n    " : ",\n    ");
    zbuf_append(buf, "{\"kind\":\"shape\",\"name\":");
    append_json_string(buf, shape->name);
    zbuf_appendf(buf, ",\"fieldCount\":%zu,\"methodCount\":%zu,\"line\":%d,\"column\":%d}", shape->fields.len, shape->methods.len, shape->line, shape->column);
  }
  zbuf_append(buf, program && program->shapes.len ? "\n  ],\n" : "],\n");
  zbuf_append(buf, "  \"enums\": [");
  for (size_t i = 0; program && i < program->enums.len; i++) {
    const EnumDecl *item = &program->enums.items[i];
    zbuf_append(buf, i == 0 ? "\n    " : ",\n    ");
    zbuf_append(buf, "{\"kind\":\"enum\",\"name\":");
    append_json_string(buf, item->name);
    zbuf_appendf(buf, ",\"public\":%s,\"caseCount\":%zu,\"line\":%d,\"column\":%d}", item->is_public ? "true" : "false", item->cases.len, item->line, item->column);
  }
  zbuf_append(buf, program && program->enums.len ? "\n  ],\n" : "],\n");
  zbuf_append(buf, "  \"choices\": [");
  for (size_t i = 0; program && i < program->choices.len; i++) {
    const Choice *item = &program->choices.items[i];
    zbuf_append(buf, i == 0 ? "\n    " : ",\n    ");
    zbuf_append(buf, "{\"kind\":\"choice\",\"name\":");
    append_json_string(buf, item->name);
    zbuf_appendf(buf, ",\"public\":%s,\"caseCount\":%zu,\"line\":%d,\"column\":%d}", item->is_public ? "true" : "false", item->cases.len, item->line, item->column);
  }
  zbuf_append(buf, program && program->choices.len ? "\n  ],\n" : "],\n");
  zbuf_append(buf, "  \"functions\": [");
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    zbuf_append(buf, i == 0 ? "\n    " : ",\n    ");
    zbuf_append(buf, "{\"kind\":\"function\",\"name\":");
    append_json_string(buf, fun->name);
    zbuf_append(buf, ",\"returnType\":");
    append_json_string(buf, fun->return_type);
    zbuf_appendf(buf, ",\"paramCount\":%zu,\"bodyKinds\":[", fun->params.len);
    for (size_t j = 0; j < fun->body.len; j++) {
      if (j > 0) zbuf_append(buf, ",");
      append_json_string(buf, stmt_kind_name(fun->body.items[j]->kind));
    }
    zbuf_appendf(buf, "],\"line\":%d,\"column\":%d}", fun->line, fun->column);
  }
  zbuf_append(buf, program && program->functions.len ? "\n  ]\n}\n" : "]\n}\n");
}

static const char *zero_commit(void) {
  const char *commit = getenv("ZERO_COMMIT");
  return commit && commit[0] ? commit : ZERO_BUILD_HASH;
}

static bool command_available(const char *name) {
  return z_process_command_available(name);
}

typedef struct {
  char *source_file;
  char *include_dir;
  char *header_file;
} RuntimeCompileInputs;

static char *runtime_path_with_suffix(const char *path, const char *suffix) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, path ? path : "");
  zbuf_append(&buf, suffix ? suffix : "");
  return buf.data;
}

static char *runtime_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left ? left : "");
  if (buf.len > 0 && buf.data[buf.len - 1] != '/' && buf.data[buf.len - 1] != '\\') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right ? right : "");
  return buf.data;
}

static char *runtime_dirname(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  if (slash == path) return z_strdup("/");
  return z_strndup(path, (size_t)(slash - path));
}

static bool runtime_make_dir(const char *path, ZDiag *diag) {
#if defined(_WIN32)
  int rc = mkdir(path);
#else
  int rc = mkdir(path, 0777);
#endif
  if (rc == 0 || errno == EEXIST) return true;
  if (diag) {
    diag->code = 2002;
    z_diag_set_path_copy(diag, path);
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to create runtime compile directory '%s': %s", path, strerror(errno));
    snprintf(diag->help, sizeof(diag->help), "choose a writable output path");
  }
  return false;
}

static void runtime_compile_inputs_free(RuntimeCompileInputs *inputs) {
  if (!inputs) return;
  if (inputs->source_file) remove(inputs->source_file);
  if (inputs->header_file) remove(inputs->header_file);
  if (inputs->include_dir) rmdir(inputs->include_dir);
  free(inputs->source_file);
  free(inputs->include_dir);
  free(inputs->header_file);
  *inputs = (RuntimeCompileInputs){0};
}

static bool write_runtime_chunks_file(const char *path, const char *const *chunks, ZDiag *diag) {
  if (!path || !chunks) return false;
  ZBuf text;
  zbuf_init(&text);
  for (size_t i = 0; chunks[i]; i++) zbuf_append(&text, chunks[i]);
  bool ok = z_write_file(path, text.data ? text.data : "", diag);
  zbuf_free(&text);
  return ok;
}

static bool write_runtime_compile_inputs(
  const char *runtime_object_file,
  const char *source_suffix,
  const char *const *source_chunks,
  RuntimeCompileInputs *out,
  ZDiag *diag
) {
  if (!runtime_object_file || !source_suffix || !source_chunks || !out) return false;
  *out = (RuntimeCompileInputs){0};
  out->source_file = runtime_path_with_suffix(runtime_object_file, source_suffix);
  out->include_dir = runtime_path_with_suffix(runtime_object_file, ".include");
  out->header_file = runtime_join_path(out->include_dir, "zero_runtime.h");
  if (!out->source_file || !out->include_dir || !out->header_file) {
    runtime_compile_inputs_free(out);
    return false;
  }
  if (!runtime_make_dir(out->include_dir, diag) ||
      !write_runtime_chunks_file(out->header_file, zero_embedded_zero_runtime_h, diag) ||
      !write_runtime_chunks_file(out->source_file, source_chunks, diag)) {
    runtime_compile_inputs_free(out);
    return false;
  }
  return true;
}

static const char *unsafe_cc_override_for_command(const Command *command) {
  const char *cli = command && command->cc && command->cc[0] ? command->cc : NULL;
  if (cli && !z_toolchain_compiler_override_safe(cli)) return cli;
  const char *env = getenv("ZERO_CC");
  if (env && env[0] && !z_toolchain_compiler_override_safe(env)) return env;
  return NULL;
}

static uint64_t runtime_object_cache_fold_text(uint64_t hash, const char *text) {
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    hash ^= (uint64_t)*p;
    hash *= 1099511628211ull;
  }
  hash ^= (uint64_t)0x1f;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t runtime_object_cache_fold_chunks(uint64_t hash, const char *const *chunks) {
  for (size_t i = 0; chunks && chunks[i]; i++) hash = runtime_object_cache_fold_text(hash, chunks[i]);
  return hash;
}

/*
 * Fold the resolved compiler binary identity (path, size, mtime) into the
 * cache key so a toolchain upgrade at the same path can never serve a stale
 * runtime object. An unresolvable compiler contributes no file facts and the
 * compile itself reports the failure.
 */
static uint64_t runtime_object_cache_fold_compiler(uint64_t hash, const ZToolchainPlan *plan) {
  const char *compiler = plan && plan->compiler && plan->compiler[0] ? plan->compiler : "cc";
  hash = runtime_object_cache_fold_text(hash, compiler);
  char *resolved = strchr(compiler, '/') ? z_strdup(compiler) : z_process_resolve_executable(compiler);
  if (resolved) {
    struct stat st;
    if (stat(resolved, &st) == 0) {
      char facts[128];
      snprintf(facts, sizeof(facts), "%lld:%lld", (long long)st.st_size, (long long)st.st_mtime);
      hash = runtime_object_cache_fold_text(hash, resolved);
      hash = runtime_object_cache_fold_text(hash, facts);
    }
    free(resolved);
  }
  return hash;
}

static char *runtime_object_cache_dir(void) {
  const char *override = getenv("ZERO_CACHE_DIR");
  if (override && override[0]) return z_strdup(override);
  const char *home = getenv("HOME");
  if (!home || !home[0]) home = getenv("USERPROFILE");
  return (home && home[0]) ? runtime_join_path(home, ".zero/cache/native") : z_strdup(".zero/cache/native");
}

/*
 * The embedded runtime sources are fixed for a given compiler binary and the
 * runtime object compile command is a pure function of the toolchain plan,
 * profile, and target, so the compiled object can be reused across builds.
 * The key folds in every compile input including the resolved C compiler
 * identity; any change misses and recompiles.
 */
static char *runtime_object_cache_file(const char *kind, const char *const *source_chunks, const ZToolchainPlan *plan, const Command *command, const ZTargetInfo *target) {
  uint64_t hash = 1469598103934665603ull;
  hash = runtime_object_cache_fold_text(hash, "zero-runtime-object-cache-v1");
  hash = runtime_object_cache_fold_text(hash, kind);
  hash = runtime_object_cache_fold_text(hash, ZERO_VERSION);
  hash = runtime_object_cache_fold_text(hash, command && command->profile ? command->profile : "");
  hash = runtime_object_cache_fold_text(hash, target && target->name ? target->name : "");
  if (plan) {
    hash = runtime_object_cache_fold_text(hash, plan->driver_kind ? plan->driver_kind : "");
    hash = runtime_object_cache_fold_text(hash, plan->target_triple ? plan->target_triple : "");
    hash = runtime_object_cache_fold_text(hash, plan->libc_mode ? plan->libc_mode : "");
    hash = runtime_object_cache_fold_text(hash, plan->sysroot_path ? plan->sysroot_path : "");
  hash = runtime_object_cache_fold_compiler(hash, plan);
  }
  hash = runtime_object_cache_fold_chunks(hash, zero_embedded_zero_runtime_h);
  hash = runtime_object_cache_fold_chunks(hash, source_chunks);
  char *cache_dir = runtime_object_cache_dir();
  ZBuf path;
  zbuf_init(&path);
  zbuf_append(&path, cache_dir);
  if (path.len > 0 && path.data[path.len - 1] != '/' && path.data[path.len - 1] != '\\') zbuf_append_char(&path, '/');
  zbuf_appendf(&path, "%s-%016llx.o", kind, (unsigned long long)hash);
  free(cache_dir);
  return path.data;
}

static bool runtime_object_cache_restore(const char *cache_path, const char *object_file) {
  if (!cache_path || !object_file) return false;
  unsigned char *data = NULL;
  size_t len = 0;
  ZDiag ignored = {0};
  if (!z_read_binary_file(cache_path, &data, &len, &ignored)) { free((char *)ignored.path); return false; }
  bool ok = len > 0 && z_write_binary_file(object_file, data, len, &ignored);
  free((char *)ignored.path); free(data);
  return ok && z_process_output_file_ready(object_file);
}

static void runtime_object_cache_store(const char *cache_path, const char *object_file) {
  if (!cache_path || !object_file) return;
  unsigned char *data = NULL;
  size_t len = 0;
  ZDiag ignored = {0};
  if (!z_read_binary_file(object_file, &data, &len, &ignored)) { free((char *)ignored.path); return; }
  if (len > 0) (void)z_write_binary_file(cache_path, data, len, &ignored);
  free((char *)ignored.path); free(data);
}

static bool compile_zero_runtime_object(const char *runtime_object_file, const ZToolchainPlan *plan, const Command *command, const ZTargetInfo *target, bool llvm_backend, ZDiag *diag) {
  char *cache_path = runtime_object_cache_file("runtime-object", zero_embedded_zero_runtime_c, plan, command, target);
  if (runtime_object_cache_restore(cache_path, runtime_object_file)) {
    free(cache_path);
    return true;
  }
  RuntimeCompileInputs inputs = {0};
  if (!write_runtime_compile_inputs(runtime_object_file, ".zero_runtime.c", zero_embedded_zero_runtime_c, &inputs, diag)) {
    free(cache_path);
    return false;
  }
  bool ok = z_toolchain_compile_c_object(plan, command ? command->profile : NULL, target, inputs.source_file, runtime_object_file, inputs.include_dir, "-std=c11 -Wall -Wextra -Wpedantic");
  runtime_compile_inputs_free(&inputs);
  if (ok) runtime_object_cache_store(cache_path, runtime_object_file);
  free(cache_path);
  if (!ok && diag) {
    const char *unsafe_cc = unsafe_cc_override_for_command(command);
    diag->code = llvm_backend ? 2004 : 2003;
    diag->line = diag->column = diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s runtime object build failed", llvm_backend ? "LLVM" : "host");
    snprintf(diag->expected, sizeof(diag->expected), "%s", llvm_backend ? "clang can compile the embedded Zero runtime source for the LLVM executable link" : "C compiler can compile the embedded Zero runtime source");
    snprintf(diag->actual, sizeof(diag->actual), "%s", unsafe_cc ? "compiler override contains unsafe shell characters" : (llvm_backend ? "clang runtime object compile command failed" : "runtime object compile command failed"));
    snprintf(diag->help, sizeof(diag->help), "%s", unsafe_cc ? "pass a compiler path or command name without flags, whitespace, or shell syntax" : (llvm_backend ? "inspect the emitted LLVM IR, runtime object, and clang installation; use --emit llvm-ir to write the IR only" : "install a host C compiler or pass --cc for the runtime link plan"));
    if (llvm_backend) z_backend_blocker_set(&diag->backend_blocker, target && target->name ? target->name : "unknown", target && target->object_format ? target->object_format : "unknown", "llvm", "toolchain", "clang");
  }
  return ok;
}

static bool compile_zero_http_curl_object(const char *runtime_object_file, const ZToolchainPlan *plan, const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  char *cache_path = runtime_object_cache_file("http-curl-object", zero_embedded_zero_http_curl_c, plan, command, target);
  if (runtime_object_cache_restore(cache_path, runtime_object_file)) {
    free(cache_path);
    return true;
  }
  RuntimeCompileInputs inputs = {0};
  if (!write_runtime_compile_inputs(runtime_object_file, ".zero_http_curl.c", zero_embedded_zero_http_curl_c, &inputs, diag)) {
    free(cache_path);
    return false;
  }
  bool ok = z_toolchain_compile_c_object(plan, command ? command->profile : NULL, target, inputs.source_file, runtime_object_file, inputs.include_dir, "-std=c11 -Wall -Wextra -Wpedantic");
  runtime_compile_inputs_free(&inputs);
  if (ok) runtime_object_cache_store(cache_path, runtime_object_file);
  free(cache_path);
  if (!ok && diag) {
    const char *unsafe_cc = unsafe_cc_override_for_command(command);
    diag->code = 2003;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "HTTP runtime provider build failed");
    snprintf(diag->expected, sizeof(diag->expected), "C compiler can compile the embedded HTTP provider source");
    snprintf(diag->actual, sizeof(diag->actual), "%s", unsafe_cc ? "compiler override contains unsafe shell characters" : "HTTP provider compile command failed");
    snprintf(diag->help, sizeof(diag->help), "%s", unsafe_cc ? "pass a compiler path or command name without flags, whitespace, or shell syntax" : "install libcurl headers or pass --cc for the runtime link plan");
  }
  return ok;
}

static bool json_array_next_string(const char **cursor, char *out, size_t out_len) {
  if (!cursor || !*cursor || !out || out_len == 0) return false;
  const char *start = strchr(*cursor, '"');
  if (!start) return false;
  start++;
  size_t len = 0;
  while (start[len] && start[len] != '"') len++;
  if (!start[len]) return false;
  size_t copy_len = len < out_len - 1 ? len : out_len - 1;
  memcpy(out, start, copy_len);
  out[copy_len] = 0;
  *cursor = start + len + 1;
  return true;
}

static void append_shell_single_quoted(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '\'');
  for (size_t i = 0; value && value[i]; i++) {
    if (value[i] == '\'') zbuf_append(buf, "'\\''");
    else zbuf_append_char(buf, value[i]);
  }
  zbuf_append_char(buf, '\'');
}

static bool c_link_name_is_safe(const char *name) {
  if (!name || !name[0]) return false;
  for (size_t i = 0; name[i]; i++) {
    unsigned char ch = (unsigned char)name[i];
    if (isalnum(ch) || ch == '_' || ch == '-' || ch == '.' || ch == '+') continue;
    return false;
  }
  return true;
}

static bool c_link_path_is_safe(const char *path) {
  if (!path || !path[0]) return false;
  for (size_t i = 0; path[i]; i++) {
    unsigned char ch = (unsigned char)path[i];
    if (ch < 0x20 || ch == 0x7f) return false;
  }
  return true;
}

static bool runtime_path_is_absolute(const char *path) {
  if (!path || !path[0]) return false;
  return path[0] == '/' || (strlen(path) > 2 && path[1] == ':');
}

static void source_input_clear_direct_c_import_headers(SourceInput *input) {
  if (!input) return;
  for (size_t i = 0; i < input->direct_c_import_header_count; i++) {
    free(input->direct_c_import_headers[i]);
    free(input->direct_c_import_resolved_headers[i]);
  }
  free(input->direct_c_import_headers);
  free(input->direct_c_import_resolved_headers);
  input->direct_c_import_headers = NULL;
  input->direct_c_import_resolved_headers = NULL;
  input->direct_c_import_header_count = 0;
}

static bool c_import_header_text_equal(const char *left, const char *right) {
  return left && left[0] && right && right[0] && strcmp(left, right) == 0;
}

static bool source_input_has_direct_c_import_header(const SourceInput *input, const char *header, const char *resolved_header) {
  for (size_t i = 0; input && i < input->direct_c_import_header_count; i++) {
    const char *candidate_header = input->direct_c_import_headers[i];
    const char *candidate_resolved = input->direct_c_import_resolved_headers[i];
    if (c_import_header_text_equal(candidate_header, header) ||
        c_import_header_text_equal(candidate_header, resolved_header) ||
        c_import_header_text_equal(candidate_resolved, header) ||
        c_import_header_text_equal(candidate_resolved, resolved_header)) {
      return true;
    }
  }
  return false;
}

static void source_input_add_direct_c_import_header(SourceInput *input, const char *header, const char *resolved_header) {
  if (!input || ((!header || !header[0]) && (!resolved_header || !resolved_header[0]))) return;
  if (source_input_has_direct_c_import_header(input, header, resolved_header)) return;
  size_t next = input->direct_c_import_header_count + 1;
  input->direct_c_import_headers = z_checked_reallocarray(input->direct_c_import_headers, next, sizeof(char *));
  input->direct_c_import_resolved_headers = z_checked_reallocarray(input->direct_c_import_resolved_headers, next, sizeof(char *));
  input->direct_c_import_headers[input->direct_c_import_header_count] = header && header[0] ? z_strdup(header) : NULL;
  input->direct_c_import_resolved_headers[input->direct_c_import_header_count] = resolved_header && resolved_header[0] ? z_strdup(resolved_header) : NULL;
  input->direct_c_import_header_count = next;
}

static bool c_lib_header_matches_direct_c_imports(const SourceInput *input, const char *manifest_header) {
  if (!input || input->direct_c_import_header_count == 0) return true;
  if (!manifest_header || !manifest_header[0]) return false;
  char *resolved = runtime_path_is_absolute(manifest_header)
    ? z_strdup(manifest_header)
    : runtime_join_path(input->package_root && input->package_root[0] ? input->package_root : ".", manifest_header);
  bool matches = source_input_has_direct_c_import_header(input, manifest_header, resolved);
  free(resolved);
  return matches;
}

static bool c_lib_matches_direct_c_imports(const SourceInput *input, const ZManifestCLib *lib) {
  if (!input || input->direct_c_import_header_count == 0) return true;
  const char *cursor = lib && lib->headers_json ? lib->headers_json : "";
  char header[512];
  while (json_array_next_string(&cursor, header, sizeof(header))) {
    if (c_lib_header_matches_direct_c_imports(input, header)) return true;
  }
  return false;
}

static void append_c_link_file_flags(ZBuf *flags, const char *package_root, const char *lib_json) {
  const char *cursor = lib_json ? lib_json : "";
  char item[512];
  while (json_array_next_string(&cursor, item, sizeof(item))) {
    char *path = runtime_path_is_absolute(item) ? z_strdup(item) : runtime_join_path(package_root && package_root[0] ? package_root : ".", item);
    zbuf_append_char(flags, ' ');
    append_shell_single_quoted(flags, path);
    free(path);
  }
}

static void append_c_link_name_flags(ZBuf *flags, const char *link_json) {
  const char *cursor = link_json ? link_json : "";
  char item[256];
  while (json_array_next_string(&cursor, item, sizeof(item))) {
    if (c_link_name_is_safe(item)) zbuf_appendf(flags, " -l%s", item);
  }
}

static void append_json_array_item_string(ZBuf *buf, bool *first, const char *value) {
  if (!buf || !first) return;
  if (!*first) zbuf_append(buf, ",");
  append_json_string(buf, value ? value : "");
  *first = false;
}

static void append_manifest_c_link_plan_items_json(ZBuf *static_libraries, bool *static_first, ZBuf *system_libraries, bool *system_first, const SourceInput *input) {
  char manifest_path[512];
  if (!input) return;
  manifest_path_for_input(input, manifest_path, sizeof(manifest_path)); if (!manifest_path[0]) return;
  ZDiag read_diag = {0}; char *manifest = z_read_file(manifest_path, &read_diag); if (!manifest) return;
  const char *package_root = input->package_root && input->package_root[0] ? input->package_root : ".";
  ZManifest parsed = {0};
  if (!z_parse_manifest_json(manifest, &parsed, &read_diag)) { free(manifest); return; }
  for (size_t i = 0; i < parsed.c_lib_count; i++) {
    const ZManifestCLib *lib = &parsed.c_libs[i];
    if (!c_lib_matches_direct_c_imports(input, lib)) continue;
    const char *lib_cursor = lib->lib_json ? lib->lib_json : "";
    char lib_item[512];
    while (json_array_next_string(&lib_cursor, lib_item, sizeof(lib_item))) {
      char *path = runtime_path_is_absolute(lib_item) ? z_strdup(lib_item) : runtime_join_path(package_root, lib_item);
      append_json_array_item_string(static_libraries, static_first, path);
      free(path);
    }
    const char *link_cursor = lib->link_json ? lib->link_json : "";
    char link_item[256];
    while (json_array_next_string(&link_cursor, link_item, sizeof(link_item))) {
      append_json_array_item_string(system_libraries, system_first, link_item);
    }
  }
  z_free_manifest(&parsed);
  free(manifest);
}

static void append_manifest_c_link_flags(ZBuf *flags, const SourceInput *input) {
  char manifest_path[512];
  if (!flags || !input) return;
  manifest_path_for_input(input, manifest_path, sizeof(manifest_path)); if (!manifest_path[0]) return;
  ZDiag read_diag = {0}; char *manifest = z_read_file(manifest_path, &read_diag); if (!manifest) return;
  const char *package_root = input->package_root && input->package_root[0] ? input->package_root : ".";
  ZManifest parsed = {0};
  if (!z_parse_manifest_json(manifest, &parsed, &read_diag)) { free(manifest); return; }
  for (size_t i = 0; i < parsed.c_lib_count; i++) {
    const ZManifestCLib *lib = &parsed.c_libs[i];
    if (!c_lib_matches_direct_c_imports(input, lib)) continue;
    append_c_link_file_flags(flags, package_root, lib->lib_json);
    append_c_link_name_flags(flags, lib->link_json);
  }
  z_free_manifest(&parsed);
  free(manifest);
}

static void init_executable_finalize_diag(ZDiag *diag, const char *path);
static bool prepare_executable_output_file_or_diag(const char *exe_file, ZDiag *diag) { if (z_process_prepare_output_file(exe_file)) return true; init_executable_finalize_diag(diag, exe_file); return false; }

static bool link_direct_object_executable(const char *object_file, const char *runtime_object_file, const char *http_object_file, const char *exe_file, const ZToolchainPlan *plan, const ZTargetInfo *target, const SourceInput *input, bool links_zero_runtime, ZDiag *diag) {
#if defined(__linux__)
  const char *linux_no_pie = " -no-pie";
#else
  const char *linux_no_pie = "";
#endif
  const char *object_files[3] = {0}; size_t object_count = 0;
  if (object_file && object_file[0]) object_files[object_count++] = object_file;
  if (runtime_object_file && runtime_object_file[0]) object_files[object_count++] = runtime_object_file;
  if (http_object_file && http_object_file[0]) object_files[object_count++] = http_object_file;
  ZBuf post_flags;
  zbuf_init(&post_flags);
  if (input && input->direct_c_import_call_count > 0) append_manifest_c_link_flags(&post_flags, input);
  if (http_object_file && http_object_file[0]) zbuf_append(&post_flags, " -lcurl");
  zbuf_append(&post_flags, " 2>/dev/null");
  bool output_ready = prepare_executable_output_file_or_diag(exe_file, diag); bool ok = output_ready && z_toolchain_link_objects(plan, target, object_files, object_count, exe_file, linux_no_pie, post_flags.data ? post_flags.data : "2>/dev/null");
  zbuf_free(&post_flags);
  if (!ok && output_ready && diag) {
    diag->code = 2003; diag->line = 1; diag->column = 1; diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", links_zero_runtime ? "host runtime link failed" : "extern C link failed");
    snprintf(diag->expected, sizeof(diag->expected), "%s", links_zero_runtime ? "direct object plus zero runtime object link successfully" : "direct object plus extern C link inputs link successfully");
    snprintf(diag->actual, sizeof(diag->actual), "%s", links_zero_runtime ? "runtime link command failed" : "extern C link command failed");
    snprintf(diag->help, sizeof(diag->help), http_object_file && http_object_file[0] ? "install libcurl or inspect the direct object, runtime objects, and host C linker diagnostics" : "inspect the direct object, optional runtime object, extern C link inputs, and host C linker diagnostics");
  }
  return ok;
}

static bool path_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static int zero_mkdir(const char *path) {
#if defined(_WIN32)
  return mkdir(path);
#else
  return mkdir(path, 0777);
#endif
}

static int zero_lstat(const char *path, struct stat *st) {
#if defined(_WIN32)
  return stat(path, st);
#else
  return lstat(path, st);
#endif
}

static char *join_cli_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left);
  if (buf.len > 0 && buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right);
  return buf.data;
}

static bool remove_tree(const char *path, ZBuf *deleted) {
  struct stat st;
  if (zero_lstat(path, &st) != 0) return errno == ENOENT;
  if (S_ISDIR(st.st_mode)) {
    DIR *dir = opendir(path);
    if (!dir) return false;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      char *child = join_cli_path(path, entry->d_name);
      bool ok = remove_tree(child, deleted);
      free(child);
      if (!ok) {
        closedir(dir);
        return false;
      }
    }
    closedir(dir);
    if (rmdir(path) != 0) return false;
  } else if (unlink(path) != 0) {
    return false;
  }
  if (deleted) {
    if (deleted->len > 0) zbuf_append_char(deleted, '\n');
    zbuf_append(deleted, path);
  }
  return true;
}

typedef struct {
  bool *used;
  size_t len;
} HelperUseSummary;

typedef struct {
  const char *name;
  const char *symbol;
  const char *category;
  int estimated_direct_bytes;
} CompilerRuntimeHelperInfo;

typedef struct {
  const char *name;
  size_t byte_len;
  size_t owner_id;
  size_t binding_id;
} MemoryArrayBinding;

typedef struct {
  MemoryArrayBinding arrays[128];
  size_t len;
  size_t owner_id;
  size_t next_binding_id;
  const ZTargetInfo *target;
} MemoryScope;

typedef struct {
  bool present;
  size_t owner_id;
  size_t binding_id;
  size_t byte_len;
} MemoryIrStorageRef;

typedef struct {
  const IrFunction *fun;
  MemoryIrStorageRef *aliases;
  size_t alias_len;
  size_t owner_id;
  const ZTargetInfo *target;
} MemoryIrScope;

typedef struct {
  size_t stack_estimate_bytes;
  size_t readonly_literal_bytes;
  size_t fixed_allocator_count;
  size_t arena_count;
  size_t null_allocator_count;
  size_t page_allocator_count;
  size_t general_allocator_count;
  size_t alloc_bytes_calls;
  size_t byte_buf_calls;
  size_t reset_calls;
  size_t capacity_calls;
  size_t vec_count;
  size_t vec_push_calls;
  size_t vec_set_calls;
  size_t vec_view_calls;
  size_t vec_clear_calls;
  size_t vec_pop_calls;
  size_t vec_truncate_calls;
  size_t vec_remove_swap_calls;
  size_t vec_len_calls;
  size_t vec_capacity_calls;
  size_t vec_capacity_bytes;
  size_t collection_storage_count;
  size_t collection_push_calls;
  size_t collection_append_calls;
  size_t collection_view_calls;
  size_t collection_query_calls;
  size_t collection_mutation_calls;
  size_t fixed_allocator_capacity_bytes;
  size_t arena_capacity_bytes;
  size_t fixed_collection_capacity_bytes;
  size_t allocation_requested_bytes;
  size_t unknown_capacity_sites;
  MemoryArrayBinding collection_storages[128];
} MemoryModelSummary;



static const CompilerRuntimeHelperInfo compiler_runtime_helpers[] = {
  {"runtime.arenaPlan", "compiler_runtime_arena_plan", "alloc", 160},
  {"runtime.sourceInputMode", "compiler_source_input_mode", "source-input", 176},
  {"runtime.sourceBuffer", "compiler_accept_source_buffer", "source-input", 96},
  {"runtime.tokenSummary", "compiler_token_summary", "lexer-json", 240},
  {"runtime.sourceHash", "compiler_source_hash", "source-input", 144},
  {"runtime.sourceLocation", "compiler_source_location_at", "source-map", 176},
  {"runtime.sourceOrder", "compiler_source_order_key", "source-order", 96},
  {"runtime.hostedPackageSourcePlan", "compiler_hosted_package_source_plan", "source-input", 160},
  {"runtime.abiSummary", "compiler_runtime_abi_summary", "runtime-abi", 192},
  {"runtime.memoryLayout", "compiler_runtime_memory_layout", "runtime-abi", 96},
  {"runtime.shimCost", "compiler_runtime_shim_cost", "runtime-abi", 96},
  {"runtime.helperSummary", "compiler_runtime_helper_summary", "runtime-helpers", 192},
  {"runtime.manifestSummary", "compiler_manifest_summary", "manifest", 320},
  {"runtime.moduleImportSummary", "compiler_module_import_summary", "module-graph", 240},
  {"runtime.moduleImportDiag", "compiler_module_import_diag_code", "module-graph", 112},
  {"runtime.moduleGraphReject", "compiler_module_graph_reject_code", "module-graph", 128},
  {"runtime.scopeSummary", "compiler_scope_summary", "name-resolution", 192},
  {"runtime.nameResolutionReport", "compiler_name_resolution_report", "name-resolution", 128},
  {"runtime.memberNameSummary", "compiler_member_name_summary", "name-resolution", 192},
  {"runtime.typeSurfaceSummary", "compiler_type_surface_summary", "checker-json", 192},
  {"runtime.expressionSurfaceSummary", "compiler_expression_surface_summary", "checker-json", 192},
  {"runtime.controlFlowSummary", "compiler_control_flow_summary", "checker-json", 128},
  {"runtime.genericStaticSummary", "compiler_generic_static_summary", "checker-json", 192},
  {"runtime.fallibilitySummary", "compiler_fallibility_summary", "checker-json", 160},
  {"runtime.fallibilityDiag", "compiler_fallibility_diag_code", "checker-json", 96},
  {"runtime.capabilitySummary", "compiler_capability_summary", "checker-json", 160},
  {"runtime.capabilityDiag", "compiler_capability_diag_code", "checker-json", 96},
  {"runtime.ownershipSummary", "compiler_ownership_summary", "checker-json", 160},
  {"runtime.ownershipDiag", "compiler_ownership_diag_code", "checker-json", 96},
  {"runtime.mirLoweringSummary", "compiler_mir_lowering_summary", "mir-json", 192},
  {"runtime.mirHash", "compiler_mir_hash", "mir-json", 144},
  {"runtime.mirModuleOrderHash", "compiler_mir_module_order_hash", "mir-json", 96},
  {"runtime.mirJsonContract", "compiler_mir_json_contract", "mir-json", 96},
  {"runtime.parseItemSummary", "compiler_parse_item_summary", "parse-json", 320},
  {"runtime.parseStmtExprSummary", "compiler_parse_stmt_expr_summary", "parse-json", 320},
  {"runtime.parseRootSummary", "compiler_parse_root_summary", "parse-json", 192},
  {"runtime.parsePublicApiSummary", "compiler_parse_public_api_summary", "graph-json", 192},
  {"runtime.parseAstSummary", "compiler_parse_ast_summary", "parse-json", 192},
  {"runtime.byteBufferPlan", "compiler_byte_buffer_plan", "buffer", 144},
  {"runtime.diagnosticReportBuild", "compiler_diagnostic_report_build", "diagnostics-json", 192},
  {"runtime.diagnosticDetailSummary", "compiler_diagnostic_detail_summary", "diagnostics-json", 128},
  {"runtime.relatedSpanSummary", "compiler_related_span_summary", "diagnostics-json", 96},
  {NULL, NULL, NULL, 0},
};

static void helper_summary_mark(HelperUseSummary *helpers, const char *name) {
  int index = helpers ? z_std_helper_index(name, helpers->len) : -1;
  if (helpers && helpers->used && index >= 0) helpers->used[index] = true;
}

static HelperUseSummary helper_summary_new(void) {
  HelperUseSummary helpers = {.len = z_std_helper_count()};
  helpers.used = z_checked_calloc(helpers.len ? helpers.len : 1, sizeof(bool));
  return helpers;
}

static void helper_summary_free(HelperUseSummary *helpers) {
  if (!helpers) return;
  free(helpers->used);
  helpers->used = NULL;
  helpers->len = 0;
}

static bool helper_summary_used(const HelperUseSummary *helpers, size_t index) {
  return helpers && helpers->used && index < helpers->len && helpers->used[index];
}

static void append_capability_json_array(ZBuf *buf, const CapabilitySummary *caps) {
  bool wrote = false;
  zbuf_append(buf, "[");
#define APPEND_CAP(name, enabled) do { \
    if (enabled) { \
      if (wrote) zbuf_append(buf, ", "); \
      append_json_string(buf, name); \
      wrote = true; \
    } \
  } while (0)
  APPEND_CAP("args", caps && caps->args);
  APPEND_CAP("env", caps && caps->env);
  APPEND_CAP("fs", caps && caps->fs);
  APPEND_CAP("memory", caps && caps->memory);
  APPEND_CAP("alloc", caps && caps->alloc);
  APPEND_CAP("path", caps && caps->path);
  APPEND_CAP("codec", caps && caps->codec);
  APPEND_CAP("parse", caps && caps->parse);
  APPEND_CAP("time", caps && caps->time);
  APPEND_CAP("rand", caps && caps->rand);
  APPEND_CAP("net", caps && caps->net);
  APPEND_CAP("proc", caps && caps->proc);
  APPEND_CAP("web", caps && caps->web);
  APPEND_CAP("world", caps && caps->world);
#undef APPEND_CAP
  zbuf_append(buf, "]");
}

static void collect_member_name(const Expr *expr, ZBuf *buf) {
  if (!expr) return;
  if (expr->kind == EXPR_IDENT) {
    zbuf_append(buf, expr->text);
  } else if (expr->kind == EXPR_MEMBER) {
    collect_member_name(expr->left, buf);
    zbuf_append_char(buf, '.');
    zbuf_append(buf, expr->text);
  }
}

static char *expr_callee_name(const Expr *callee) {
  ZBuf name;
  zbuf_init(&name);
  collect_member_name(callee, &name);
  if (!name.data) zbuf_append(&name, "");
  return name.data;
}

static int abi_pointer_size(const ZTargetInfo *target);

static size_t memory_element_size(const char *element, const ZTargetInfo *target, bool *is_u8) {
  if (is_u8) *is_u8 = false;
  if (!element) return 0;
  while (*element == ' ' || *element == '\t') element++;
  if (strcmp(element, "u8") == 0 || strcmp(element, "i8") == 0 || strcmp(element, "Bool") == 0 || strcmp(element, "bool") == 0) {
    if (is_u8 && strcmp(element, "u8") == 0) *is_u8 = true;
    return 1;
  }
  if (strcmp(element, "u16") == 0 || strcmp(element, "i16") == 0) return 2;
  if (strcmp(element, "u32") == 0 || strcmp(element, "i32") == 0 || strcmp(element, "f32") == 0) return 4;
  if (strcmp(element, "usize") == 0 || strcmp(element, "isize") == 0) return (size_t)abi_pointer_size(target);
  if (strcmp(element, "u64") == 0 || strcmp(element, "i64") == 0 || strcmp(element, "f64") == 0) return 8;
  return 0;
}

static bool memory_parse_fixed_array_type(const char *type, const ZTargetInfo *target, size_t *out_len, size_t *out_bytes, bool *out_u8) {
  if (out_len) *out_len = 0;
  if (out_bytes) *out_bytes = 0;
  if (out_u8) *out_u8 = false;
  if (!type || type[0] != '[') return false;
  const char *cursor = type + 1;
  size_t len = 0;
  bool saw_digit = false;
  while (*cursor && *cursor != ']') {
    if (*cursor == '_') {
      cursor++;
      continue;
    }
    if (*cursor < '0' || *cursor > '9') return false;
    saw_digit = true;
    len = len * 10 + (size_t)(*cursor - '0');
    cursor++;
  }
  if (!saw_digit || *cursor != ']') return false;
  cursor++;
  bool is_u8 = false;
  size_t element_size = memory_element_size(cursor, target, &is_u8);
  if (element_size == 0) return false;
  if (out_len) *out_len = len;
  if (out_bytes) *out_bytes = len * element_size;
  if (out_u8) *out_u8 = is_u8;
  return true;
}

static bool memory_parse_number_literal(const Expr *expr, size_t *out_value) {
  if (out_value) *out_value = 0;
  if (!expr || expr->kind != EXPR_NUMBER || !expr->text) return false;
  const char *cursor = expr->text;
  size_t value = 0;
  bool saw_digit = false;
  while (*cursor) {
    if (*cursor == '_') {
      cursor++;
      continue;
    }
    if (*cursor < '0' || *cursor > '9') break;
    saw_digit = true;
    value = value * 10 + (size_t)(*cursor - '0');
    cursor++;
  }
  if (!saw_digit) return false;
  if (out_value) *out_value = value;
  return true;
}

static void memory_scope_note_array(MemoryScope *scope, const char *name, size_t byte_len) {
  if (!scope || !name || scope->len >= sizeof(scope->arrays) / sizeof(scope->arrays[0])) return;
  scope->arrays[scope->len++] = (MemoryArrayBinding){.name = name, .byte_len = byte_len, .owner_id = scope->owner_id, .binding_id = ++scope->next_binding_id};
}

static void memory_scope_note_array_alias(MemoryScope *scope, const char *name, const MemoryArrayBinding *target) {
  if (!scope || !name || !target || scope->len >= sizeof(scope->arrays) / sizeof(scope->arrays[0])) return;
  scope->arrays[scope->len++] = (MemoryArrayBinding){.name = name, .byte_len = target->byte_len, .owner_id = target->owner_id, .binding_id = target->binding_id};
}

static const MemoryArrayBinding *memory_scope_lookup_array_binding(const MemoryScope *scope, const char *name) {
  if (!scope || !name) return NULL;
  for (size_t i = scope->len; i > 0; i--) {
    if (scope->arrays[i - 1].name && strcmp(scope->arrays[i - 1].name, name) == 0) return &scope->arrays[i - 1];
  }
  return NULL;
}

static size_t memory_scope_lookup_array(const MemoryScope *scope, const char *name) {
  const MemoryArrayBinding *binding = memory_scope_lookup_array_binding(scope, name);
  return binding ? binding->byte_len : 0;
}

static size_t memory_byte_capacity_expr(const MemoryScope *scope, const Expr *expr) {
  if (!expr) return 0;
  if (expr->kind == EXPR_BORROW) return memory_byte_capacity_expr(scope, expr->left);
  if (expr->kind == EXPR_IDENT) return memory_scope_lookup_array(scope, expr->text);
  return 0;
}

static const MemoryArrayBinding *memory_array_binding_expr(const MemoryScope *scope, const Expr *expr) {
  if (!expr) return NULL;
  if (expr->kind == EXPR_BORROW) return memory_array_binding_expr(scope, expr->left);
  if (expr->kind == EXPR_IDENT) return memory_scope_lookup_array_binding(scope, expr->text);
  return NULL;
}

static bool memory_type_is_span_view(const char *type) {
  if (!type) return false;
  while (*type == ' ' || *type == '\t') type++;
  return strncmp(type, "Span<", strlen("Span<")) == 0 ||
         strncmp(type, "MutSpan<", strlen("MutSpan<")) == 0;
}

static bool memory_name_has_base(const char *name, const char *base) {
  if (!name || !base) return false;
  size_t len = strlen(base);
  if (strncmp(name, base, len) != 0) return false;
  return name[len] == '\0' || name[len] == '_';
}

static void memory_model_collect_expr(const Expr *expr, MemoryScope *scope, MemoryModelSummary *summary);

static void memory_model_note_fixed_collection_storage(MemoryModelSummary *summary, const MemoryScope *scope, const Expr *expr) {
  if (!summary) return;
  const Expr *storage = expr;
  if (storage && storage->kind == EXPR_BORROW) storage = storage->left;
  if (!storage || storage->kind != EXPR_IDENT || !storage->text) {
    summary->unknown_capacity_sites++;
    return;
  }
  const MemoryArrayBinding *binding = memory_scope_lookup_array_binding(scope, storage->text);
  if (!binding || binding->byte_len == 0) {
    summary->unknown_capacity_sites++;
    return;
  }
  for (size_t i = 0; i < summary->collection_storage_count; i++) {
    if (summary->collection_storages[i].name &&
        summary->collection_storages[i].owner_id == binding->owner_id &&
        summary->collection_storages[i].binding_id == binding->binding_id) return;
  }
  if (summary->collection_storage_count >= sizeof(summary->collection_storages) / sizeof(summary->collection_storages[0])) {
    summary->unknown_capacity_sites++;
    return;
  }
  summary->collection_storages[summary->collection_storage_count++] = *binding;
  summary->fixed_collection_capacity_bytes += binding->byte_len;
}

static void memory_model_note_allocator_capacity(MemoryModelSummary *summary, size_t capacity, bool arena) {
  if (!summary) return;
  if (capacity == 0) {
    summary->unknown_capacity_sites++;
    return;
  }
  if (arena) summary->arena_capacity_bytes += capacity;
  else summary->fixed_allocator_capacity_bytes += capacity;
}

static void memory_model_collect_expr_vec(const ExprVec *exprs, MemoryScope *scope, MemoryModelSummary *summary) {
  if (!exprs) return;
  for (size_t i = 0; i < exprs->len; i++) memory_model_collect_expr(exprs->items[i], scope, summary);
}

static void memory_model_collect_expr(const Expr *expr, MemoryScope *scope, MemoryModelSummary *summary) {
  if (!expr || !summary) return;
  if (expr->kind == EXPR_STRING && expr->text) {
    summary->readonly_literal_bytes += strlen(expr->text) + 1;
  }
  if (expr->kind == EXPR_CALL) {
    char *callee = expr_callee_name(expr->left);
    if (strcmp(callee, "std.mem.nullAlloc") == 0) summary->null_allocator_count++;
    else if (strcmp(callee, "std.mem.pageAlloc") == 0) summary->page_allocator_count++;
    else if (strcmp(callee, "std.mem.generalAlloc") == 0) summary->general_allocator_count++;
    else if (strcmp(callee, "std.mem.fixedBufAlloc") == 0) {
      summary->fixed_allocator_count++;
      memory_model_note_allocator_capacity(summary, expr->args.len > 0 ? memory_byte_capacity_expr(scope, expr->args.items[0]) : 0, false);
    } else if (strcmp(callee, "std.mem.arena") == 0) {
      summary->arena_count++;
      memory_model_note_allocator_capacity(summary, expr->args.len > 0 ? memory_byte_capacity_expr(scope, expr->args.items[0]) : 0, true);
    } else if (strcmp(callee, "std.mem.allocBytes") == 0) {
      summary->alloc_bytes_calls++;
      size_t requested = 0;
      if (expr->args.len > 1 && memory_parse_number_literal(expr->args.items[1], &requested)) summary->allocation_requested_bytes += requested;
    } else if (strcmp(callee, "std.mem.byteBuf") == 0) {
      summary->byte_buf_calls++;
      size_t requested = 0;
      if (expr->args.len > 1 && memory_parse_number_literal(expr->args.items[1], &requested)) summary->allocation_requested_bytes += requested;
    } else if (strcmp(callee, "std.mem.reset") == 0) summary->reset_calls++;
    else if (strcmp(callee, "std.mem.capacity") == 0) summary->capacity_calls++;
    else if (strcmp(callee, "std.mem.vec") == 0) {
      summary->vec_count++;
      size_t capacity = expr->args.len > 0 ? memory_byte_capacity_expr(scope, expr->args.items[0]) : 0;
      if (capacity > 0) summary->vec_capacity_bytes += capacity;
      else summary->unknown_capacity_sites++;
    } else if (strcmp(callee, "std.mem.vecPush") == 0) summary->vec_push_calls++;
    else if (strcmp(callee, "std.mem.vecSet") == 0) summary->vec_set_calls++;
    else if (strcmp(callee, "std.mem.vecBytes") == 0) summary->vec_view_calls++;
    else if (strcmp(callee, "std.mem.vecGet") == 0) summary->vec_view_calls++;
    else if (strcmp(callee, "std.mem.vecClear") == 0) summary->vec_clear_calls++;
    else if (strcmp(callee, "std.mem.vecPop") == 0) summary->vec_pop_calls++;
    else if (strcmp(callee, "std.mem.vecTruncate") == 0) summary->vec_truncate_calls++;
    else if (strcmp(callee, "std.mem.vecRemoveSwap") == 0) summary->vec_remove_swap_calls++;
    else if (strcmp(callee, "std.mem.vecIndex") == 0) summary->vec_view_calls++;
    else if (strcmp(callee, "std.mem.vecContains") == 0) summary->vec_view_calls++;
    else if (strcmp(callee, "std.mem.vecInsertUnique") == 0) summary->vec_push_calls++;
    else if (strcmp(callee, "std.mem.vecRemoveValue") == 0) summary->vec_remove_swap_calls++;
    else if (strcmp(callee, "std.mem.vecLen") == 0) summary->vec_len_calls++;
    else if (strcmp(callee, "std.mem.vecCapacity") == 0) summary->vec_capacity_calls++;
    else if (strcmp(callee, "std.collections.push") == 0 ||
             strcmp(callee, "std.collections.dequePushBack") == 0) {
      summary->collection_push_calls++;
      summary->collection_mutation_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
    } else if (strcmp(callee, "std.collections.append") == 0) {
      summary->collection_append_calls++;
      summary->collection_mutation_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
    } else if (strcmp(callee, "std.collections.view") == 0) {
      summary->collection_view_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
    } else if (strcmp(callee, "std.collections.setView") == 0 ||
               strcmp(callee, "std.collections.mapKeys") == 0) {
      summary->collection_view_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
    } else if (strcmp(callee, "std.collections.contains") == 0 ||
               strcmp(callee, "std.collections.count") == 0 ||
               strcmp(callee, "std.collections.clear") == 0 ||
               strcmp(callee, "std.collections.dequeBack") == 0 ||
               strcmp(callee, "std.collections.dequeFront") == 0 ||
               strcmp(callee, "std.collections.dequePopBack") == 0 ||
               strcmp(callee, "std.collections.first") == 0 ||
               strcmp(callee, "std.collections.remaining") == 0 ||
               strcmp(callee, "std.collections.isFull") == 0 ||
               strcmp(callee, "std.collections.last") == 0 ||
               strcmp(callee, "std.collections.pop") == 0 ||
               strcmp(callee, "std.collections.truncate") == 0 ||
               strcmp(callee, "std.collections.setContains") == 0 ||
               strcmp(callee, "std.collections.setClear") == 0 ||
               strcmp(callee, "std.collections.setRemaining") == 0 ||
               strcmp(callee, "std.collections.setIsFull") == 0 ||
               strcmp(callee, "std.collections.setTruncate") == 0 ||
               strcmp(callee, "std.collections.mapContains") == 0 ||
               strcmp(callee, "std.collections.mapIndex") == 0) {
      summary->collection_query_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
    } else if (strcmp(callee, "std.collections.mapGet") == 0 ||
               strcmp(callee, "std.collections.mapClear") == 0 ||
               strcmp(callee, "std.collections.mapRemaining") == 0 ||
               strcmp(callee, "std.collections.mapIsFull") == 0 ||
               strcmp(callee, "std.collections.mapTruncate") == 0) {
      summary->collection_query_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 1 ? expr->args.items[1] : NULL);
    } else if (strcmp(callee, "std.collections.mapValues") == 0) {
      summary->collection_view_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 1 ? expr->args.items[1] : NULL);
    } else if (strcmp(callee, "std.collections.insertUnique") == 0 ||
               strcmp(callee, "std.collections.insertAt") == 0 ||
               strcmp(callee, "std.collections.dequePopFront") == 0 ||
               strcmp(callee, "std.collections.dequePushFront") == 0 ||
               strcmp(callee, "std.collections.fill") == 0 ||
               strcmp(callee, "std.collections.replaceAt") == 0 ||
               strcmp(callee, "std.collections.removeAt") == 0 ||
               strcmp(callee, "std.collections.removeValue") == 0 ||
               strcmp(callee, "std.collections.reverse") == 0 ||
               strcmp(callee, "std.collections.rotateLeft") == 0 ||
               strcmp(callee, "std.collections.rotateRight") == 0 ||
               strcmp(callee, "std.collections.setInsert") == 0 ||
               strcmp(callee, "std.collections.setRemove") == 0 ||
               strcmp(callee, "std.collections.swapAt") == 0 ||
               strcmp(callee, "std.collections.removeSwap") == 0 ||
               strcmp(callee, "std.collections.moveToFront") == 0) {
      summary->collection_mutation_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
    } else if (strcmp(callee, "std.collections.mapPut") == 0 ||
               strcmp(callee, "std.collections.mapRemove") == 0) {
      summary->collection_mutation_calls++;
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 0 ? expr->args.items[0] : NULL);
      memory_model_note_fixed_collection_storage(summary, scope, expr->args.len > 1 ? expr->args.items[1] : NULL);
    }
    free(callee);
  }
  memory_model_collect_expr(expr->left, scope, summary);
  memory_model_collect_expr(expr->right, scope, summary);
  memory_model_collect_expr_vec(&expr->args, scope, summary);
  for (size_t i = 0; i < expr->fields.len; i++) memory_model_collect_expr(expr->fields.items[i].value, scope, summary);
}

static void memory_model_collect_stmt_vec(const StmtVec *body, MemoryScope *scope, MemoryModelSummary *summary) {
  if (!body || !summary) return;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    if (!stmt) continue;
    if (stmt->kind == STMT_LET) {
      const char *type = stmt->resolved_type ? stmt->resolved_type : stmt->type;
      size_t array_bytes = 0;
      if (memory_parse_fixed_array_type(type, scope ? scope->target : NULL, NULL, &array_bytes, NULL)) {
        summary->stack_estimate_bytes += array_bytes;
        memory_scope_note_array(scope, stmt->name, array_bytes);
      } else if (memory_type_is_span_view(type)) {
        memory_scope_note_array_alias(scope, stmt->name, memory_array_binding_expr(scope, stmt->expr));
      }
    }
    memory_model_collect_expr(stmt->target, scope, summary);
    memory_model_collect_expr(stmt->expr, scope, summary);
    memory_model_collect_expr(stmt->range_end, scope, summary);
    memory_model_collect_stmt_vec(&stmt->then_body, scope, summary);
    memory_model_collect_stmt_vec(&stmt->else_body, scope, summary);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      memory_model_collect_expr(stmt->match_arms.items[arm_index].guard, scope, summary);
      memory_model_collect_stmt_vec(&stmt->match_arms.items[arm_index].body, scope, summary);
    }
  }
}

static MemoryModelSummary memory_model_summary_from_program(const Program *program, const ZTargetInfo *target) {
  MemoryModelSummary summary = {0};
  if (!program) return summary;
  for (size_t i = 0; i < program->consts.len; i++) memory_model_collect_expr(program->consts.items[i].expr, &(MemoryScope){.target = target}, &summary);
  for (size_t i = 0; i < program->shapes.len; i++) {
    for (size_t field_index = 0; field_index < program->shapes.items[i].fields.len; field_index++) {
      memory_model_collect_expr(program->shapes.items[i].fields.items[field_index].default_value, &(MemoryScope){.target = target}, &summary);
    }
  }
  for (size_t i = 0; i < program->functions.len; i++) {
    MemoryScope scope = {.owner_id = i + 1, .target = target};
    memory_model_collect_stmt_vec(&program->functions.items[i].body, &scope, &summary);
  }
  return summary;
}

static size_t memory_ir_type_size(IrTypeKind type, const ZTargetInfo *target) {
  switch (type) {
    case IR_TYPE_BOOL:
    case IR_TYPE_U8: return 1;
    case IR_TYPE_U16: return 2;
    case IR_TYPE_I32:
    case IR_TYPE_U32: return 4;
    case IR_TYPE_USIZE: return (size_t)abi_pointer_size(target);
    case IR_TYPE_I64:
    case IR_TYPE_U64: return 8;
    default: return 0;
  }
}

static bool memory_ir_function_is_stdlib(const IrFunction *fun) {
  return fun && fun->name && strncmp(fun->name, "__zero_std_", strlen("__zero_std_")) == 0;
}

static MemoryIrStorageRef memory_ir_empty_storage_ref(void) {
  return (MemoryIrStorageRef){0};
}

static MemoryIrStorageRef memory_ir_local_storage_ref(const MemoryIrScope *scope, unsigned local_index, size_t byte_len, size_t binding_extra) {
  if (!scope || !scope->fun || local_index >= scope->fun->local_len || byte_len == 0) return memory_ir_empty_storage_ref();
  return (MemoryIrStorageRef){
    .present = true,
    .owner_id = scope->owner_id,
    .binding_id = ((size_t)local_index + 1) * 1315423911u + binding_extra,
    .byte_len = byte_len
  };
}

static const IrFunction *memory_ir_call_callee(const IrProgram *ir, const IrValue *value) {
  if (!ir || !value || value->kind != IR_VALUE_CALL || value->callee_index >= ir->function_len) return NULL;
  return &ir->functions[value->callee_index];
}

static MemoryIrStorageRef memory_ir_storage_ref_for_value(const IrProgram *ir, const MemoryIrScope *scope, const IrValue *value);

static MemoryIrStorageRef memory_ir_storage_ref_for_array_view(const MemoryIrScope *scope, const IrValue *value) {
  if (!scope || !scope->fun || !value || value->array_index >= scope->fun->local_len) return memory_ir_empty_storage_ref();
  const IrLocal *local = &scope->fun->locals[value->array_index];
  if (!local->is_array && !local->is_record) return memory_ir_empty_storage_ref();
  size_t byte_len = local->byte_size;
  if (value->data_len > 0) {
    size_t element_size = memory_ir_type_size(value->element_type, scope->target);
    if (element_size > 0) byte_len = (size_t)value->data_len * element_size;
  }
  return memory_ir_local_storage_ref(scope, value->array_index, byte_len, (size_t)value->field_offset);
}

static MemoryIrStorageRef memory_ir_storage_ref_for_value(const IrProgram *ir, const MemoryIrScope *scope, const IrValue *value) {
  if (!value || !scope) return memory_ir_empty_storage_ref();
  switch (value->kind) {
    case IR_VALUE_ARRAY_BYTE_VIEW:
      return memory_ir_storage_ref_for_array_view(scope, value);
    case IR_VALUE_LOCAL:
      if (value->local_index < scope->alias_len && scope->aliases[value->local_index].present) return scope->aliases[value->local_index];
      if (scope->fun && value->local_index < scope->fun->local_len) {
        const IrLocal *local = &scope->fun->locals[value->local_index];
        if (local->is_array) return memory_ir_local_storage_ref(scope, value->local_index, local->byte_size, 0);
      }
      return memory_ir_empty_storage_ref();
    case IR_VALUE_BYTE_SLICE:
    case IR_VALUE_MAYBE_BYTE_VIEW_LITERAL:
      return memory_ir_storage_ref_for_value(ir, scope, value->left);
    case IR_VALUE_CALL: {
      const IrFunction *callee = memory_ir_call_callee(ir, value);
      if (callee && memory_name_has_base(callee->name, "__zero_std_collections_view") && value->arg_len > 0) {
        return memory_ir_storage_ref_for_value(ir, scope, value->args[0]);
      }
      return memory_ir_empty_storage_ref();
    }
    default:
      return memory_ir_empty_storage_ref();
  }
}

static void memory_model_note_ir_storage(MemoryModelSummary *summary, MemoryIrStorageRef ref) {
  if (!summary) return;
  if (!ref.present || ref.byte_len == 0) {
    summary->unknown_capacity_sites++;
    return;
  }
  for (size_t i = 0; i < summary->collection_storage_count; i++) {
    if (summary->collection_storages[i].owner_id == ref.owner_id &&
        summary->collection_storages[i].binding_id == ref.binding_id) return;
  }
  if (summary->collection_storage_count >= sizeof(summary->collection_storages) / sizeof(summary->collection_storages[0])) {
    summary->unknown_capacity_sites++;
    return;
  }
  summary->collection_storages[summary->collection_storage_count++] = (MemoryArrayBinding){
    .byte_len = ref.byte_len,
    .owner_id = ref.owner_id,
    .binding_id = ref.binding_id
  };
  summary->fixed_collection_capacity_bytes += ref.byte_len;
}

static void memory_model_note_ir_allocator_storage(MemoryModelSummary *summary, MemoryIrStorageRef ref, bool arena) {
  if (!summary) return;
  if (!ref.present || ref.byte_len == 0) {
    summary->unknown_capacity_sites++;
    return;
  }
  if (arena) summary->arena_capacity_bytes += ref.byte_len;
  else summary->fixed_allocator_capacity_bytes += ref.byte_len;
}

static bool memory_ir_collection_op(const char *name, const char **op) {
  if (op) *op = NULL;
  if (memory_name_has_base(name, "__zero_std_collections_push")) { if (op) *op = "push"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_deque_push_back")) { if (op) *op = "push"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_append")) { if (op) *op = "append"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_view")) { if (op) *op = "view"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_set_view")) { if (op) *op = "view"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_keys")) { if (op) *op = "view"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_values")) { if (op) *op = "map-view"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_contains")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_count")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_clear")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_deque_back")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_deque_front")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_deque_pop_back")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_first")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_remaining")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_is_full")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_last")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_pop")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_truncate")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_set_clear")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_set_contains")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_set_remaining")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_set_is_full")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_set_truncate")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_contains")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_index")) { if (op) *op = "query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_clear")) { if (op) *op = "map-query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_get")) { if (op) *op = "map-query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_remaining")) { if (op) *op = "map-query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_is_full")) { if (op) *op = "map-query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_truncate")) { if (op) *op = "map-query"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_deque_pop_front")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_deque_push_front")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_fill")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_insert_at")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_insert_unique")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_replace_at")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_remove_at")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_remove_value")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_reverse")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_rotate_left")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_rotate_right")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_set_insert")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_set_remove")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_swap_at")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_remove_swap")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_move_to_front")) { if (op) *op = "mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_put")) { if (op) *op = "map-mutation"; return true; }
  if (memory_name_has_base(name, "__zero_std_collections_map_remove")) { if (op) *op = "map-mutation"; return true; }
  return false;
}

static void memory_model_collect_ir_value(const IrProgram *ir, MemoryIrScope *scope, const IrValue *value, MemoryModelSummary *summary, bool count_stdlib_calls);

static void memory_model_collect_ir_call(const IrProgram *ir, MemoryIrScope *scope, const IrValue *value, MemoryModelSummary *summary, bool count_stdlib_calls) {
  if (!ir || !scope || !value || !summary) return;
  const IrFunction *callee = memory_ir_call_callee(ir, value);
  const char *op = NULL;
  if (count_stdlib_calls && callee && memory_ir_collection_op(callee->name, &op)) {
    if (strcmp(op, "push") == 0) {
      summary->collection_push_calls++;
      summary->collection_mutation_calls++;
    } else if (strcmp(op, "append") == 0) {
      summary->collection_append_calls++;
      summary->collection_mutation_calls++;
    } else if (strcmp(op, "view") == 0) {
      summary->collection_view_calls++;
    } else if (strcmp(op, "query") == 0) {
      summary->collection_query_calls++;
    } else if (strcmp(op, "mutation") == 0) {
      summary->collection_mutation_calls++;
    } else if (strcmp(op, "map-view") == 0) {
      summary->collection_view_calls++;
    } else if (strcmp(op, "map-query") == 0) {
      summary->collection_query_calls++;
    } else if (strcmp(op, "map-mutation") == 0) {
      summary->collection_mutation_calls++;
    }
    memory_model_note_ir_storage(summary, value->arg_len > 0 ? memory_ir_storage_ref_for_value(ir, scope, value->args[0]) : memory_ir_empty_storage_ref());
    if (strcmp(op, "map-view") == 0 || strcmp(op, "map-query") == 0 || strcmp(op, "map-mutation") == 0) {
      memory_model_note_ir_storage(summary, value->arg_len > 1 ? memory_ir_storage_ref_for_value(ir, scope, value->args[1]) : memory_ir_empty_storage_ref());
    }
  }
}

static void memory_model_collect_ir_value(const IrProgram *ir, MemoryIrScope *scope, const IrValue *value, MemoryModelSummary *summary, bool count_stdlib_calls) {
  if (!value || !summary) return;
  switch (value->kind) {
    case IR_VALUE_STRING_LITERAL:
      summary->readonly_literal_bytes += value->data_len + 1;
      break;
    case IR_VALUE_VEC_INIT: {
      summary->vec_count++;
      MemoryIrStorageRef ref = memory_ir_storage_ref_for_value(ir, scope, value->left);
      if (ref.present && ref.byte_len > 0) summary->vec_capacity_bytes += ref.byte_len;
      else summary->unknown_capacity_sites++;
      break;
    }
    case IR_VALUE_VEC_PUSH:
      summary->vec_push_calls++;
      break;
    case IR_VALUE_VEC_SET:
      summary->vec_set_calls++;
      break;
    case IR_VALUE_VEC_BYTES:
      summary->vec_view_calls++;
      break;
    case IR_VALUE_VEC_GET:
      summary->vec_view_calls++;
      break;
    case IR_VALUE_VEC_CLEAR:
      summary->vec_clear_calls++;
      break;
    case IR_VALUE_VEC_POP:
      summary->vec_pop_calls++;
      break;
    case IR_VALUE_VEC_TRUNCATE:
      summary->vec_truncate_calls++;
      break;
    case IR_VALUE_VEC_REMOVE_SWAP:
      summary->vec_remove_swap_calls++;
      break;
    case IR_VALUE_VEC_INDEX:
    case IR_VALUE_VEC_CONTAINS:
      summary->vec_view_calls++;
      break;
    case IR_VALUE_VEC_INSERT_UNIQUE:
      summary->vec_push_calls++;
      break;
    case IR_VALUE_VEC_REMOVE_VALUE:
      summary->vec_remove_swap_calls++;
      break;
    case IR_VALUE_VEC_LEN:
      summary->vec_len_calls++;
      break;
    case IR_VALUE_VEC_CAPACITY:
      summary->vec_capacity_calls++;
      break;
    case IR_VALUE_FIXED_BUF_ALLOC:
      summary->fixed_allocator_count++;
      memory_model_note_ir_allocator_storage(summary, memory_ir_storage_ref_for_value(ir, scope, value->left), false);
      break;
    case IR_VALUE_ALLOC_BYTES:
      summary->alloc_bytes_calls++;
      if (value->left && value->left->kind == IR_VALUE_INT) summary->allocation_requested_bytes += (size_t)value->left->int_value;
      break;
    case IR_VALUE_CALL:
      memory_model_collect_ir_call(ir, scope, value, summary, count_stdlib_calls);
      break;
    default:
      break;
  }
  memory_model_collect_ir_value(ir, scope, value->left, summary, count_stdlib_calls);
  memory_model_collect_ir_value(ir, scope, value->right, summary, count_stdlib_calls);
  memory_model_collect_ir_value(ir, scope, value->index, summary, count_stdlib_calls);
  for (size_t i = 0; i < value->arg_len; i++) memory_model_collect_ir_value(ir, scope, value->args[i], summary, count_stdlib_calls);
}

static void memory_model_collect_ir_instrs(const IrProgram *ir, MemoryIrScope *scope, const IrInstr *instrs, size_t len, MemoryModelSummary *summary, bool count_stdlib_calls) {
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    memory_model_collect_ir_value(ir, scope, instr->value, summary, count_stdlib_calls);
    memory_model_collect_ir_value(ir, scope, instr->index, summary, count_stdlib_calls);
    if (instr->kind == IR_INSTR_LOCAL_SET && instr->local_index < scope->alias_len) {
      scope->aliases[instr->local_index] = memory_ir_storage_ref_for_value(ir, scope, instr->value);
    }
    memory_model_collect_ir_instrs(ir, scope, instr->then_instrs, instr->then_len, summary, count_stdlib_calls);
    memory_model_collect_ir_instrs(ir, scope, instr->else_instrs, instr->else_len, summary, count_stdlib_calls);
  }
}

static MemoryModelSummary memory_model_summary_from_ir(const IrProgram *ir, const ZTargetInfo *target) {
  MemoryModelSummary summary = {0};
  if (!ir) return summary;
  for (size_t i = 0; i < ir->function_len; i++) {
    const IrFunction *fun = &ir->functions[i];
    MemoryIrScope scope = {
      .fun = fun,
      .alias_len = fun->local_len,
      .owner_id = i + 1,
      .target = target
    };
    if (scope.alias_len > 0) scope.aliases = z_checked_calloc(scope.alias_len, sizeof(MemoryIrStorageRef));
    memory_model_collect_ir_instrs(ir, &scope, fun->instrs, fun->instr_len, &summary, !memory_ir_function_is_stdlib(fun));
    free(scope.aliases);
  }
  return summary;
}

static void collect_capabilities_from_expr(const Expr *expr, CapabilitySummary *caps) {
  if (!expr || !caps) return;
  if (expr->kind == EXPR_CALL) {
    char *callee = expr_callee_name(expr->left);
    z_capability_summary_collect_std_name(callee, caps);
    free(callee);
  }
  collect_capabilities_from_expr(expr->left, caps);
  collect_capabilities_from_expr(expr->right, caps);
  for (size_t i = 0; i < expr->args.len; i++) collect_capabilities_from_expr(expr->args.items[i], caps);
  for (size_t i = 0; i < expr->fields.len; i++) collect_capabilities_from_expr(expr->fields.items[i].value, caps);
}

static void collect_helpers_from_expr(const Expr *expr, HelperUseSummary *helpers) {
  if (!expr || !helpers) return;
  if (expr->kind == EXPR_CALL) {
    char *callee = expr_callee_name(expr->left);
    helper_summary_mark(helpers, callee);
    free(callee);
  }
  collect_helpers_from_expr(expr->left, helpers);
  collect_helpers_from_expr(expr->right, helpers);
  for (size_t i = 0; i < expr->args.len; i++) collect_helpers_from_expr(expr->args.items[i], helpers);
  for (size_t i = 0; i < expr->fields.len; i++) collect_helpers_from_expr(expr->fields.items[i].value, helpers);
}

static void collect_capabilities_from_stmt_vec(const StmtVec *body, CapabilitySummary *caps) {
  if (!body || !caps) return;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    collect_capabilities_from_expr(stmt->target, caps);
    collect_capabilities_from_expr(stmt->expr, caps);
    collect_capabilities_from_expr(stmt->range_end, caps);
    collect_capabilities_from_stmt_vec(&stmt->then_body, caps);
    collect_capabilities_from_stmt_vec(&stmt->else_body, caps);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      collect_capabilities_from_stmt_vec(&stmt->match_arms.items[arm_index].body, caps);
    }
  }
}

static void collect_helpers_from_stmt_vec(const StmtVec *body, HelperUseSummary *helpers) {
  if (!body || !helpers) return;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    collect_helpers_from_expr(stmt->target, helpers);
    collect_helpers_from_expr(stmt->expr, helpers);
    collect_helpers_from_expr(stmt->range_end, helpers);
    collect_helpers_from_stmt_vec(&stmt->then_body, helpers);
    collect_helpers_from_stmt_vec(&stmt->else_body, helpers);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      collect_helpers_from_stmt_vec(&stmt->match_arms.items[arm_index].body, helpers);
    }
  }
}

static CapabilitySummary function_capabilities(const Function *fun) {
  CapabilitySummary caps = {0};
  for (size_t i = 0; fun && i < fun->params.len; i++) {
    const char *type = fun->params.items[i].type;
    if (strcmp(type, "World") == 0) caps.world = true;
    else if (strcmp(type, "Fs") == 0) caps.fs = true;
    else if (strcmp(type, "Net") == 0) caps.net = true;
    else if (strcmp(type, "Proc") == 0) caps.proc = true;
    else if (strcmp(type, "Clock") == 0) caps.time = true;
    else if (strcmp(type, "Rand") == 0) caps.rand = true;
    else if (strcmp(type, "Alloc") == 0 || strcmp(type, "FixedBufAlloc") == 0 || strcmp(type, "NullAlloc") == 0) z_capability_summary_set(&caps, "alloc");
    if (strstr(type, "Span<") || strstr(type, "MutSpan<") || strstr(type, "ByteBuf")) caps.memory = true;
  }
  if (fun) collect_capabilities_from_stmt_vec(&fun->body, &caps);
  return caps;
}

static CapabilitySummary program_capabilities(const Program *program) {
  CapabilitySummary caps = {0};
  for (size_t i = 0; program && i < program->functions.len; i++) {
    CapabilitySummary fun_caps = function_capabilities(&program->functions.items[i]);
    z_capability_summary_merge(&caps, &fun_caps);
  }
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    for (size_t field_index = 0; field_index < program->shapes.items[i].fields.len; field_index++) {
      collect_capabilities_from_expr(program->shapes.items[i].fields.items[field_index].default_value, &caps);
    }
    for (size_t method_index = 0; method_index < program->shapes.items[i].methods.len; method_index++) {
      CapabilitySummary fun_caps = function_capabilities(&program->shapes.items[i].methods.items[method_index]);
      z_capability_summary_merge(&caps, &fun_caps);
    }
  }
  return caps;
}

static CapabilitySummary program_or_ir_capabilities(const Program *program, const IrProgram *ir) {
  CapabilitySummary caps = program_capabilities(program);
  if ((!program || program->functions.len == 0) && ir) {
    CapabilitySummary ir_caps = z_ir_program_capabilities(ir);
    z_capability_summary_merge(&caps, &ir_caps);
  }
  return caps;
}

static HelperUseSummary program_used_helpers(const Program *program) {
  HelperUseSummary helpers = helper_summary_new();
  for (size_t i = 0; program && i < program->functions.len; i++) {
    collect_helpers_from_stmt_vec(&program->functions.items[i].body, &helpers);
  }
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    for (size_t field_index = 0; field_index < program->shapes.items[i].fields.len; field_index++) {
      collect_helpers_from_expr(program->shapes.items[i].fields.items[field_index].default_value, &helpers);
    }
    for (size_t method_index = 0; method_index < program->shapes.items[i].methods.len; method_index++) {
      collect_helpers_from_stmt_vec(&program->shapes.items[i].methods.items[method_index].body, &helpers);
    }
  }
  return helpers;
}

static bool target_is_host(const ZTargetInfo *target) {
  return z_target_is_host(target);
}

static bool validate_target_capabilities(const Program *program, const ZTargetInfo *target, ZDiag *diag, const char *path) {
  CapabilitySummary caps = program_capabilities(program);
#define DENY_CAP(field, cap_name, label) do { \
    if (caps.field && !z_target_has_capability(target, cap_name)) { \
      diag->code = 6002; \
      diag->path = path; \
      diag->line = 1; \
      diag->column = 1; \
      diag->length = 1; \
      snprintf(diag->message, sizeof(diag->message), "target does not provide required %s capability", label); \
      snprintf(diag->expected, sizeof(diag->expected), "target with %s capability", label); \
      snprintf(diag->actual, sizeof(diag->actual), "target %s lacks %s", target ? target->name : "<unknown>", label); \
      if (strcmp(cap_name, "fs") == 0) snprintf(diag->help, sizeof(diag->help), "build for host target %s or remove hosted std.fs usage from this target-neutral build", z_host_target()); \
      else snprintf(diag->help, sizeof(diag->help), "build for a target that declares %s or remove that capability from this target-neutral entry point", label); \
      return false; \
    } \
  } while (0)
  DENY_CAP(fs, "fs", "Fs");
  DENY_CAP(args, "args", "Args");
  DENY_CAP(env, "env", "Env");
  DENY_CAP(time, "time", "Clock");
  DENY_CAP(rand, "rand", "Rand");
  DENY_CAP(net, "net", "Net");
  DENY_CAP(proc, "proc", "Proc");
  DENY_CAP(web, "web", "Web");
#undef DENY_CAP
  if (caps.world && !z_target_has_capability(target, "stdio")) {
    diag->code = 6002;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "target does not provide World stdio capability");
    snprintf(diag->expected, sizeof(diag->expected), "target with stdio capability");
    snprintf(diag->actual, sizeof(diag->actual), "target %s lacks stdio", target ? target->name : "<unknown>");
    snprintf(diag->help, sizeof(diag->help), "select a target with stdio or use a narrower capability");
    return false;
  }
  return true;
}

static int abi_pointer_size(const ZTargetInfo *target) {
  (void)target;
  return 8;
}

static void append_c_imports_json(ZBuf *buf, const Program *program, const ZTargetInfo *target);

static uint64_t fnv1a_text(const char *text) {
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    hash ^= (uint64_t)*cursor;
    hash *= 1099511628211ull;
  }
  return hash;
}

static char *read_optional_file(const char *path) {
  unsigned char *bytes = NULL;
  size_t len = 0;
  if (!z_read_binary_file(path, &bytes, &len, NULL)) return NULL;
  (void)len;
  return (char *)bytes;
}

static char *read_optional_manifest_file(const SourceInput *input) {
  if (input && input->manifest_path && input->manifest_path[0]) return read_optional_file(input->manifest_path);
  char *manifest_path = z_manifest_path_for_root(".");
  if (!manifest_path) return NULL;
  char *manifest = read_optional_file(manifest_path);
  free(manifest_path);
  return manifest;
}

static void append_hash_json(ZBuf *buf, const char *key, uint64_t hash) {
  zbuf_appendf(buf, ",\n    \"%s\": \"%016llx\"", key, (unsigned long long)hash);
}

static uint64_t mix_hash_text(uint64_t hash, const char *text) {
  hash ^= fnv1a_text(text ? text : "");
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t source_interface_hash(const SourceInput *input) {
  uint64_t hash = fnv1a_text("interface");
  if (!input) return hash;
  for (size_t i = 0; i < input->module_count; i++) {
    hash = mix_hash_text(hash, input->module_names[i]);
    hash = mix_hash_text(hash, input->module_paths[i]);
  }
  for (size_t i = 0; i < input->symbol_count; i++) {
    hash = mix_hash_text(hash, input->symbol_names[i]);
    hash = mix_hash_text(hash, input->symbol_modules[i]);
    hash = mix_hash_text(hash, input->symbol_kinds[i]);
    hash ^= input->symbol_public[i] ? 0xa5a5a5a5u : 0x5a5a5a5au;
  }
  return hash;
}

static uint64_t graph_interface_cache_key(const SourceInput *input, const char *graph_hash) {
  uint64_t hash = fnv1a_text("program-graph-interface");
  hash ^= source_interface_hash(input);
  hash *= 1099511628211ull;
  hash ^= z_std_source_graph_fingerprint();
  hash *= 1099511628211ull;
  return mix_hash_text(hash, graph_hash);
}

static uint64_t source_dependency_hash_ex(const SourceInput *input, bool include_source_files) {
  uint64_t hash = fnv1a_text("dependencies"); if (!input) return hash;
  hash = mix_hash_text(hash, input->package_name);
  hash = mix_hash_text(hash, input->package_version);
  if (include_source_files) hash = mix_hash_text(hash, input->manifest_path);
  hash ^= input->manifest_hash; hash *= 1099511628211ull;
  hash ^= input->dependency_graph_hash; hash *= 1099511628211ull;
  hash ^= input->lockfile_hash; hash *= 1099511628211ull;
  if (include_source_files) for (size_t i = 0; i < input->source_file_count; i++) hash = mix_hash_text(hash, input->source_files[i]);
  for (size_t i = 0; i < input->import_edge_count; i++) {
    hash = mix_hash_text(hash, input->import_from[i]);
    hash = mix_hash_text(hash, input->import_to[i]);
    hash = mix_hash_text(hash, input->import_paths[i]);
  }
  for (size_t i = 0; i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    hash = mix_hash_text(hash, dep->resolved_name);
    hash = mix_hash_text(hash, dep->resolved_version);
    hash = mix_hash_text(hash, dep->resolved_manifest);
    hash = mix_hash_text(hash, dep->status);
    hash ^= dep->fingerprint;
    hash *= 1099511628211ull;
  }
  return hash;
}

static uint64_t source_dependency_hash(const SourceInput *input) { return source_dependency_hash_ex(input, true); }
static uint64_t graph_dependency_hash(const SourceInput *input) { return source_dependency_hash_ex(input, false); }

static uint64_t graph_package_dependency_hash(const SourceInput *input) {
  uint64_t hash = fnv1a_text("dependencies");
  if (!input) return hash;
  hash = mix_hash_text(hash, input->package_name);
  hash = mix_hash_text(hash, input->package_version);
  hash ^= input->manifest_hash; hash *= 1099511628211ull;
  hash ^= input->dependency_count; hash *= 1099511628211ull;
  for (size_t i = 0; i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    hash = mix_hash_text(hash, dep->name);
    hash = mix_hash_text(hash, dep->version);
    hash = mix_hash_text(hash, dep->path);
    hash = mix_hash_text(hash, dep->resolved_name);
    hash = mix_hash_text(hash, dep->resolved_version);
    hash = mix_hash_text(hash, dep->targets_json);
    hash = mix_hash_text(hash, dep->status);
    hash ^= dep->direct ? 1u : 0u;
    hash *= 1099511628211ull;
  }
  return hash;
}

static uint64_t source_imported_package_metadata_hash(const SourceInput *input) {
  uint64_t hash = fnv1a_text("imported-package-metadata");
  if (!input) return hash;
  hash = mix_hash_text(hash, input->package_name);
  hash = mix_hash_text(hash, input->package_version);
  hash = mix_hash_text(hash, input->manifest_path);
  hash ^= input->manifest_hash;
  hash *= 1099511628211ull;
  hash ^= input->dependency_graph_hash;
  hash *= 1099511628211ull;
  hash ^= input->lockfile_hash;
  hash *= 1099511628211ull;
  for (size_t i = 0; i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    hash = mix_hash_text(hash, dep->name);
    hash = mix_hash_text(hash, dep->resolved_name);
    hash = mix_hash_text(hash, dep->resolved_version);
    hash = mix_hash_text(hash, dep->resolved_manifest);
    hash = mix_hash_text(hash, dep->targets_json);
    hash ^= dep->fingerprint;
    hash *= 1099511628211ull;
  }
  return hash;
}

static uint64_t source_target_facts_hash(const ZTargetInfo *target) {
  uint64_t hash = fnv1a_text("target-facts");
  hash = mix_hash_text(hash, target ? target->name : z_host_target());
  hash = mix_hash_text(hash, target ? target->arch : "");
  hash = mix_hash_text(hash, target ? target->os : "");
  hash = mix_hash_text(hash, target ? target->abi : "");
  hash = mix_hash_text(hash, target ? target->libc_mode : "");
  hash = mix_hash_text(hash, target ? target->object_format : "");
  hash = mix_hash_text(hash, target ? target->capabilities : "");
  return hash;
}

static uint64_t source_file_hash_for_path(const SourceInput *input, const char *path) {
  char *source = read_optional_file(path);
  const char *text = source ? source : ((input && input->source_file && path && strcmp(path, input->source_file) == 0 && input->source) ? input->source : path);
  uint64_t hash = fnv1a_text(text ? text : "");
  free(source);
  return hash;
}

static size_t module_public_symbol_count(const SourceInput *input, const char *module_name) {
  size_t count = 0;
  for (size_t i = 0; input && i < input->symbol_count; i++) {
    if (!input->symbol_public[i]) continue;
    if (!input->symbol_modules || strcmp(input->symbol_modules[i], module_name) != 0) continue;
    count++;
  }
  return count;
}

static uint64_t module_public_interface_hash(const SourceInput *input, const char *module_name) {
  uint64_t hash = fnv1a_text("module-public-interface");
  hash = mix_hash_text(hash, module_name);
  for (size_t i = 0; input && i < input->symbol_count; i++) {
    if (!input->symbol_public[i]) continue;
    if (!input->symbol_modules || strcmp(input->symbol_modules[i], module_name) != 0) continue;
    hash = mix_hash_text(hash, input->symbol_names[i]);
    hash = mix_hash_text(hash, input->symbol_kinds ? input->symbol_kinds[i] : "unknown");
  }
  return hash;
}

static uint64_t graph_interface_module_source_hash(const char *graph_hash, const char *module_name, const char *module_path) { uint64_t hash = fnv1a_text("program-graph-module-source"); hash = mix_hash_text(hash, graph_hash); hash = mix_hash_text(hash, module_name); return mix_hash_text(hash, module_path); }

static void append_interface_fingerprints_json_ex(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *graph_hash) {
  zbuf_append(buf, "{\"schemaVersion\":1,\"algorithm\":\"fnv1a64-zero-interface-v1\",\"targetFactsHash\":\"");
  zbuf_appendf(buf, "%016llx", (unsigned long long)source_target_facts_hash(target));
  zbuf_append(buf, "\",\"importedPackageMetadataHash\":\"");
  zbuf_appendf(buf, "%016llx", (unsigned long long)source_imported_package_metadata_hash(input));
  if (graph_hash && graph_hash[0]) {
    zbuf_append(buf, "\",\"sourceKind\":\"program-graph\",\"graphHash\":");
    append_json_string(buf, graph_hash);
    zbuf_append(buf, ",\"modules\":[");
  } else {
    zbuf_append(buf, "\",\"modules\":[");
  }
  for (size_t i = 0; input && i < input->module_count; i++) {
    if (i > 0) zbuf_append(buf, ",");
    const char *module_name = input->module_names[i];
    const char *module_path = input->module_paths[i];
    uint64_t source_hash = graph_hash && graph_hash[0]
      ? graph_interface_module_source_hash(graph_hash, module_name, module_path)
      : source_file_hash_for_path(input, module_path);
    uint64_t interface_hash = module_public_interface_hash(input, module_name);
    size_t public_count = module_public_symbol_count(input, module_name);
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, module_name);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, module_path);
    zbuf_appendf(buf, ",\"sourceHash\":\"%016llx\",\"publicInterfaceHash\":\"%016llx\",\"publicSymbolCount\":%zu,\"publicSymbols\":[",
                 (unsigned long long)source_hash,
                 (unsigned long long)interface_hash,
                 public_count);
    bool wrote_symbol = false;
    for (size_t j = 0; j < input->symbol_count; j++) {
      if (!input->symbol_public[j]) continue;
      if (!input->symbol_modules || strcmp(input->symbol_modules[j], module_name) != 0) continue;
      if (wrote_symbol) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, input->symbol_names[j]);
      zbuf_append(buf, ",\"kind\":");
      append_json_string(buf, input->symbol_kinds ? input->symbol_kinds[j] : "unknown");
      zbuf_append(buf, "}");
      wrote_symbol = true;
    }
    zbuf_append(buf, "],\"imports\":[");
    bool wrote_import = false;
    for (size_t j = 0; j < input->import_edge_count; j++) {
      if (strcmp(input->import_from[j], module_name) != 0) continue;
      if (wrote_import) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"module\":");
      append_json_string(buf, input->import_to[j]);
      zbuf_append(buf, ",\"path\":");
      append_json_string(buf, input->import_paths[j]);
      zbuf_append(buf, "}");
      wrote_import = true;
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "]}");
}

static void append_interface_fingerprints_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target) { append_interface_fingerprints_json_ex(buf, input, target, NULL); }

static uint64_t compile_cache_key(const SourceInput *input, const ZTargetInfo *target, const char *profile, const char *kind) {
  uint64_t hash = fnv1a_text(kind);
  hash = mix_hash_text(hash, ZERO_VERSION);
  hash = mix_hash_text(hash, input ? input->source : "");
  hash = mix_hash_text(hash, target ? target->name : z_host_target());
  hash = mix_hash_text(hash, profile ? profile : "release");
  hash ^= source_dependency_hash(input);
  return hash;
}

static uint64_t graph_compile_cache_key(const SourceInput *input, const ZTargetInfo *target, const char *profile, const char *kind, const char *graph_hash) {
  uint64_t hash = fnv1a_text(kind);
  hash = mix_hash_text(hash, ZERO_VERSION);
  hash = mix_hash_text(hash, graph_hash ? graph_hash : "");
  hash ^= z_std_source_graph_fingerprint();
  hash *= 1099511628211ull;
  hash = mix_hash_text(hash, target ? target->name : z_host_target());
  hash = mix_hash_text(hash, profile ? profile : "release");
  hash ^= graph_dependency_hash(input);
  return hash;
}

static uint64_t command_compile_cache_key(const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *profile, const char *kind) {
  if (command && z_program_graph_artifact_source_present(&command->graph_source)) {
    return graph_compile_cache_key(input, target, profile, kind, command->graph_source.graph_hash);
  }
  return compile_cache_key(input, target, profile, kind);
}

static uint64_t linked_executable_compile_cache_key(const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *profile, const char *kind) {
  if (command && z_program_graph_artifact_source_present(&command->graph_source)) {
    uint64_t hash = fnv1a_text(kind);
    hash = mix_hash_text(hash, ZERO_VERSION);
    hash = mix_hash_text(hash, command->graph_source.graph_hash ? command->graph_source.graph_hash : "");
    hash ^= z_std_source_graph_fingerprint();
    hash *= 1099511628211ull;
    hash = mix_hash_text(hash, target ? target->name : z_host_target());
    hash = mix_hash_text(hash, profile ? profile : "release");
    hash ^= graph_package_dependency_hash(input);
    return hash;
  }
  return compile_cache_key(input, target, profile, kind);
}

static char *native_cache_file_for_input(const SourceInput *input, const char *name) {
  const char *cache_dir = getenv("ZERO_CACHE_DIR");
  if (cache_dir && cache_dir[0]) return runtime_join_path(cache_dir, name);
  if (input && input->package_root && input->package_root[0]) {
    char *dir = runtime_join_path(input->package_root, ".zero/cache/native");
    char *path = runtime_join_path(dir, name);
    free(dir);
    return path;
  }
  return runtime_join_path(".zero/cache/native", name);
}

static char *shared_native_cache_file(const char *name) {
  char *cache_dir = runtime_object_cache_dir();
  char *path = runtime_join_path(cache_dir, name);
  free(cache_dir);
  return path;
}

static char *linked_executable_cache_path(
  const Command *command,
  const SourceInput *input,
  const ZTargetInfo *target,
  const ZToolchainPlan *plan,
  bool needs_zero_runtime,
  bool needs_http_runtime,
  const char *artifact_kind
) {
  if (!command || !input || !target || input->direct_c_import_call_count > 0) return NULL;
  uint64_t key = linked_executable_compile_cache_key(command, input, target, command->profile, artifact_kind);
  key = runtime_object_cache_fold_text(key, "zero-linked-executable-cache-v7"); key = runtime_object_cache_fold_text(key, ZERO_BUILD_HASH); key = runtime_object_cache_fold_text(key, needs_zero_runtime ? "zero-runtime" : "no-zero-runtime");
  key = runtime_object_cache_fold_text(key, needs_http_runtime ? "http-runtime" : "no-http-runtime");
  if (needs_zero_runtime) { key = runtime_object_cache_fold_chunks(key, zero_embedded_zero_runtime_h); key = runtime_object_cache_fold_chunks(key, zero_embedded_zero_runtime_c); }
  if (needs_http_runtime) key = runtime_object_cache_fold_chunks(key, zero_embedded_zero_http_curl_c);
  key = runtime_object_cache_fold_text(key, target->exe_suffix ? target->exe_suffix : "");
  if (plan) {
    key = runtime_object_cache_fold_text(key, plan->driver_kind ? plan->driver_kind : "");
    key = runtime_object_cache_fold_text(key, plan->target_triple ? plan->target_triple : "");
    key = runtime_object_cache_fold_text(key, plan->libc_mode ? plan->libc_mode : "");
    key = runtime_object_cache_fold_text(key, plan->sysroot_path ? plan->sysroot_path : "");
    key = runtime_object_cache_fold_compiler(key, plan);
  }
  ZBuf name;
  zbuf_init(&name);
  zbuf_appendf(&name, "linked-exe-%016llx", (unsigned long long)key);
  if (target->exe_suffix && target->exe_suffix[0]) zbuf_append(&name, target->exe_suffix);
  bool graph_keyed = z_program_graph_artifact_source_present(&command->graph_source);
  char *path = graph_keyed
    ? shared_native_cache_file(name.data ? name.data : "linked-exe-cache")
    : native_cache_file_for_input(input, name.data ? name.data : "linked-exe-cache");
  zbuf_free(&name);
  return path;
}

static bool executable_cache_restore_to_path(const char *cache_path, const char *exe_file) {
  if (!cache_path || !exe_file || !z_process_executable_file_ready(cache_path)) return false;
  unsigned char *data = NULL;
  size_t len = 0;
  ZDiag ignored = {0};
  if (!z_read_binary_file(cache_path, &data, &len, &ignored)) {
    free((char *)ignored.path);
    return false;
  }
  bool ok = len > 0 && z_write_binary_file(exe_file, data, len, &ignored) && z_process_mark_executable(exe_file);
  free((char *)ignored.path);
  free(data);
  return ok && z_process_executable_file_ready(exe_file);
}

static void executable_cache_store_path(const char *cache_path, const char *exe_file) {
  if (!cache_path || !exe_file || !z_process_executable_file_ready(exe_file)) return;
  unsigned char *data = NULL;
  size_t len = 0;
  ZDiag ignored = {0};
  if (!z_read_binary_file(exe_file, &data, &len, &ignored)) {
    free((char *)ignored.path);
    return;
  }
  if (len > 0 && z_write_binary_file(cache_path, data, len, &ignored)) (void)z_process_mark_executable(cache_path);
  free((char *)ignored.path);
  free(data);
}

static void free_linked_executable_paths(char *exe_cache_path, char *http_object_file, char *runtime_object_file, char *object_file, char *exe_file) {
  free(exe_cache_path);
  free(http_object_file);
  free(runtime_object_file);
  free(object_file);
  free(exe_file);
}

static bool json_array_contains_string_literal(const char *json, const char *value) {
  if (!json || !value || !value[0]) return false;
  ZBuf needle;
  zbuf_init(&needle);
  zbuf_append_char(&needle, '"');
  zbuf_append(&needle, value);
  zbuf_append_char(&needle, '"');
  bool found = needle.data && strstr(json, needle.data) != NULL;
  zbuf_free(&needle);
  return found;
}

static bool json_array_nonempty_literal(const char *json) {
  if (!json) return false;
  for (const char *cursor = json; *cursor; cursor++) {
    if (*cursor == '"') return true;
  }
  return false;
}

static bool compiler_cache_touch(const char *kind, uint64_t key) {
  if (!kind || !kind[0]) return false;
  const char *cache_dir = getenv("ZERO_CACHE_DIR");
  if (!cache_dir || !cache_dir[0]) cache_dir = ".zero/cache/native";
  ZBuf path;
  zbuf_init(&path);
  zbuf_append(&path, cache_dir);
  if (path.len > 0 && path.data[path.len - 1] != '/' && path.data[path.len - 1] != '\\') zbuf_append_char(&path, '/');
  zbuf_appendf(&path, "%s-%016llx.cache", kind, (unsigned long long)key);
  ZBuf text;
  zbuf_init(&text);
  zbuf_appendf(&text, "%s %016llx\n", kind, (unsigned long long)key);
  bool hit = path.data && path_exists(path.data);
  ZDiag ignored = {0};
  if (path.data && text.data) z_write_file(path.data, text.data, &ignored);
  zbuf_free(&text);
  zbuf_free(&path);
  return hit;
}

static void touch_program_graph_compiler_caches(SourceInput *input, const ZTargetInfo *target, const char *profile, const char *graph_hash) {
  if (!input || !graph_hash || !graph_hash[0]) return;
  input->parse_cache_hit = compiler_cache_touch("parse-tree", graph_compile_cache_key(input, NULL, NULL, "parse-tree", graph_hash));
  input->interface_cache_hit = compiler_cache_touch("interface", graph_interface_cache_key(input, graph_hash));
  input->check_cache_hit = compiler_cache_touch("checked-body", graph_compile_cache_key(input, target, NULL, "checked-body", graph_hash));
  input->specialization_cache_hit = compiler_cache_touch("specialization", graph_compile_cache_key(input, target, profile ? profile : "release", "specialization", graph_hash));
  /* Compile commands write caches, so any projection match verdicts that
   * read-only verification left pending can persist alongside them. */
  z_program_graph_projection_match_verdicts_flush();
}

static void append_compiler_phases_json(ZBuf *buf, const SourceInput *input) {
  zbuf_append(buf, "[");
#define APPEND_PHASE(name, field, cacheable) do { \
    if (strcmp(name, "resolve") != 0) zbuf_append(buf, ", "); \
    zbuf_append(buf, "{\"name\":"); \
    append_json_string(buf, name); \
    zbuf_appendf(buf, ",\"elapsedMs\":%lld,\"cacheable\":%s}", input ? input->field : 0, cacheable ? "true" : "false"); \
  } while (0)
  APPEND_PHASE("resolve", resolve_ms, true);
  APPEND_PHASE("parse", parse_ms, true);
  APPEND_PHASE("interface", interface_ms, true);
  APPEND_PHASE("check", check_ms, true);
  APPEND_PHASE("lower", lower_ms, true);
  APPEND_PHASE("codegen", codegen_ms, true);
  APPEND_PHASE("object", object_ms, true);
  APPEND_PHASE("link", link_ms, false);
#undef APPEND_PHASE
  zbuf_append(buf, "]");
}

static void append_graph_build_timing_facts_json(ZBuf *buf, const SourceInput *input) {
  long long mir_load_or_lower_ms = 0;
  if (input) {
    mir_load_or_lower_ms =
      input->graph_mir_cache_load_ms +
      input->graph_mir_lower_ms +
      input->graph_mir_cache_write_ms +
      input->graph_mir_cache_reload_ms;
  }
  zbuf_append(buf, "{\"schemaVersion\":1,\"sourceKind\":\"program-graph\",\"unit\":\"ms\",\"phaseTiming\":true");
  zbuf_append(buf, ",\"timings\":{");
  zbuf_appendf(buf, "\"graphLoadMs\":%lld", input ? input->graph_load_ms : 0);
  zbuf_appendf(buf, ",\"stdlibMergeMs\":%lld", input ? input->graph_stdlib_merge_ms : 0);
  zbuf_appendf(buf, ",\"stdlibReferenceScanMs\":%lld", input ? input->graph_stdlib_reference_scan_ms : 0);
  zbuf_appendf(buf, ",\"stdlibCleanupMs\":%lld", input ? input->graph_stdlib_cleanup_ms : 0);
  zbuf_appendf(buf, ",\"stdlibModuleLoadMs\":%lld", input ? input->graph_stdlib_module_load_ms : 0);
  zbuf_appendf(buf, ",\"stdlibNodeMergeMs\":%lld", input ? input->graph_stdlib_node_merge_ms : 0);
  zbuf_appendf(buf, ",\"stdlibEdgeMergeMs\":%lld", input ? input->graph_stdlib_edge_merge_ms : 0);
  zbuf_appendf(buf, ",\"stdlibFinalizeMs\":%lld", input ? input->graph_stdlib_finalize_ms : 0);
  zbuf_appendf(buf, ",\"stdlibPruneMs\":%lld", input ? input->graph_stdlib_prune_ms : 0);
  zbuf_appendf(buf, ",\"stdlibIdentityMs\":%lld", input ? input->graph_stdlib_identity_ms : 0);
  zbuf_appendf(buf, ",\"readinessCheckMs\":%lld", input ? input->graph_readiness_check_ms : 0);
  zbuf_appendf(buf, ",\"mirCacheLoadMs\":%lld", input ? input->graph_mir_cache_load_ms : 0);
  zbuf_appendf(buf, ",\"mirLowerMs\":%lld", input ? input->graph_mir_lower_ms : 0);
  zbuf_appendf(buf, ",\"mirCacheWriteMs\":%lld", input ? input->graph_mir_cache_write_ms : 0);
  zbuf_appendf(buf, ",\"mirCacheReloadMs\":%lld", input ? input->graph_mir_cache_reload_ms : 0);
  zbuf_appendf(buf, ",\"mirLoadOrLowerMs\":%lld", mir_load_or_lower_ms);
  zbuf_appendf(buf, ",\"lowerPhaseMs\":%lld", input ? input->lower_ms : 0);
  zbuf_appendf(buf, ",\"codegenMs\":%lld", input ? input->codegen_ms : 0);
  zbuf_appendf(buf, ",\"objectMs\":%lld", input ? input->object_ms : 0);
  zbuf_appendf(buf, ",\"linkMs\":%lld", input ? input->link_ms : 0);
  zbuf_append(buf, "}");
  zbuf_appendf(buf, ",\"stdlibMerge\":{\"modulesMerged\":%zu,\"nodesMerged\":%zu,\"edgesMerged\":%zu,\"cacheHit\":%s,\"cacheStored\":%s}",
               input ? input->graph_stdlib_modules_merged : 0,
               input ? input->graph_stdlib_nodes_merged : 0,
               input ? input->graph_stdlib_edges_merged : 0,
               input && input->graph_stdlib_merge_cache_hit ? "true" : "false",
               input && input->graph_stdlib_merge_cache_stored ? "true" : "false");
  if (input && input->mapped_mir_cache_path) {
    zbuf_append(buf, ",\"mappedFinalMir\":{\"path\":");
    append_json_string(buf, input->mapped_mir_cache_path);
    zbuf_appendf(buf, ",\"byteLength\":%zu,\"hit\":%s,\"written\":%s,\"memoryMapped\":%s,\"borrowedStorage\":%s,\"codegenImmediate\":%s,\"programReconstructed\":%s}",
                 input->mapped_mir_cache_bytes,
                 input->mapped_mir_cache_hit ? "true" : "false",
                 input->mapped_mir_cache_written ? "true" : "false",
                 input->mapped_mir_memory_mapped ? "true" : "false",
                 input->mapped_mir_borrowed_storage ? "true" : "false",
                 input->mapped_mir_codegen_immediate ? "true" : "false",
                 input->mapped_mir_program_reconstructed ? "true" : "false");
  }
  zbuf_append(buf, "}");
}

static const char *GRAPH_CACHE_INPUTS_PARSE = "[\"graphHash\",\"stdlibGraph\",\"moduleHash\",\"nodeHashes\",\"typeFacts\",\"symbolFacts\",\"importGraph\",\"importPaths\",\"compilerVersion\",\"packageDependencies\"]";
static const char *GRAPH_CACHE_INPUTS_INTERFACE = "[\"graphHash\",\"stdlibGraph\",\"moduleHash\",\"modulePaths\",\"symbolFacts\",\"importGraph\"]";
static const char *GRAPH_CACHE_INPUTS_CHECK = "[\"graphHash\",\"stdlibGraph\",\"moduleHash\",\"nodeHashes\",\"typeFacts\",\"symbolFacts\",\"importGraph\",\"importPaths\",\"targetFacts\",\"compilerVersion\",\"packageDependencies\"]";
static const char *GRAPH_CACHE_INPUTS_SPECIALIZATION = "[\"graphHash\",\"stdlibGraph\",\"moduleHash\",\"nodeHashes\",\"typeFacts\",\"symbolFacts\",\"importGraph\",\"importPaths\",\"targetFacts\",\"profile\",\"compilerVersion\",\"packageDependencies\"]";
static const char *GRAPH_CACHE_INPUTS_MAPPED_MIR = "[\"graphHash\",\"stdlibGraph\",\"moduleHash\",\"nodeHashes\",\"typeFacts\",\"symbolFacts\",\"importGraph\",\"importPaths\",\"targetFacts\",\"compilerVersion\",\"packageDependencies\",\"emitKind\",\"backend\"]";
static const char *GRAPH_CACHE_INPUTS_OBJECT = "[\"graphHash\",\"stdlibGraph\",\"moduleHash\",\"nodeHashes\",\"typeFacts\",\"symbolFacts\",\"importGraph\",\"importPaths\",\"targetFacts\",\"profile\",\"compilerVersion\",\"packageDependencies\"]";
static const char *GRAPH_CACHE_INPUTS_AGGREGATE = "[\"graphHash\",\"stdlibGraph\",\"moduleHash\",\"modulePaths\",\"nodeHashes\",\"typeFacts\",\"symbolFacts\",\"importGraph\",\"importPaths\",\"targetFacts\",\"profile\",\"compilerVersion\",\"packageDependencies\"]";

static void append_compiler_caches_json_ex(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *profile, const char *source_kind, const char *graph_hash) {
  bool graph_input = source_kind && strcmp(source_kind, "program-graph") == 0;
  uint64_t parse_key = graph_input
    ? graph_compile_cache_key(input, NULL, NULL, "parse-tree", graph_hash)
    : compile_cache_key(input, NULL, NULL, "parse-tree");
  uint64_t interface_key = graph_input ? graph_interface_cache_key(input, graph_hash) : source_interface_hash(input);
  uint64_t check_key = graph_input
    ? graph_compile_cache_key(input, target, NULL, "checked-body", graph_hash)
    : compile_cache_key(input, target, NULL, "checked-body");
  uint64_t specialization_key = graph_input
    ? graph_compile_cache_key(input, target, profile, "specialization", graph_hash)
    : compile_cache_key(input, target, profile, "specialization");
  uint64_t object_key = graph_input
    ? graph_compile_cache_key(input, target, profile, "emitted-object", graph_hash)
    : compile_cache_key(input, target, profile, "emitted-object");
  zbuf_append(buf, "[");
#define APPEND_CACHE(label, key, hit, invalidates, key_inputs) do { \
    if (wrote) zbuf_append(buf, ", "); \
    zbuf_append(buf, "{\"name\":"); \
    append_json_string(buf, label); \
    zbuf_appendf(buf, ",\"key\":\"%016llx\",\"hit\":%s,\"stored\":true,\"compilerVersion\":\"%s\",\"packageVersion\":", (unsigned long long)(key), (hit) ? "true" : "false", ZERO_VERSION); \
    append_json_string(buf, input && input->package_version ? input->package_version : ""); \
    if (source_kind && source_kind[0]) { \
      zbuf_append(buf, ",\"sourceKind\":"); \
      append_json_string(buf, source_kind); \
    } \
    if (graph_hash && graph_hash[0]) { \
      zbuf_append(buf, ",\"graphHash\":"); \
      append_json_string(buf, graph_hash); \
    } \
    if (graph_input) zbuf_appendf(buf, ",\"stdlibGraphHash\":\"%016llx\"", (unsigned long long)z_std_source_graph_fingerprint()); \
    if (graph_input) { zbuf_append(buf, ",\"graphKeyInputs\":"); zbuf_append(buf, key_inputs); zbuf_append(buf, ",\"parserArtifactsInKey\":false"); } \
    zbuf_appendf(buf, ",\"dependencyGraphHash\":\"%016llx\",\"lockfileHash\":\"%016llx\",\"invalidatesOn\":", \
                 (unsigned long long)(input ? input->dependency_graph_hash : 0), \
                 (unsigned long long)(input ? input->lockfile_hash : 0)); \
    append_json_string(buf, invalidates); \
    zbuf_append(buf, "}"); \
    wrote = true; \
  } while (0)
  bool wrote = false;
  APPEND_CACHE("parseTree", parse_key, input && input->parse_cache_hit, graph_input ? "ProgramGraph input" : "source", GRAPH_CACHE_INPUTS_PARSE);
  APPEND_CACHE("interface", interface_key, input && input->interface_cache_hit, graph_input ? "graph public symbols/import graph" : "public symbols/import graph", GRAPH_CACHE_INPUTS_INTERFACE);
  APPEND_CACHE("checkedBody", check_key, input && input->check_cache_hit, graph_input ? "ProgramGraph input or target" : "source or target", GRAPH_CACHE_INPUTS_CHECK);
  APPEND_CACHE("specialization", specialization_key, input && input->specialization_cache_hit, graph_input ? "ProgramGraph input, target, or profile" : "source, target, or profile", GRAPH_CACHE_INPUTS_SPECIALIZATION);
  if (graph_input && input && input->mapped_mir_cache_path) {
    uint64_t mapped_mir_key = graph_compile_cache_key(input, target, profile, "mapped-final-mir", graph_hash);
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":\"mappedFinalMir\",\"key\":\"");
    zbuf_appendf(buf, "%016llx", (unsigned long long)mapped_mir_key);
    zbuf_appendf(buf, "\",\"hit\":%s,\"stored\":true,\"compilerVersion\":\"%s\",\"packageVersion\":",
                 input->mapped_mir_cache_hit ? "true" : "false",
                 ZERO_VERSION);
    append_json_string(buf, input->package_version ? input->package_version : "");
    zbuf_append(buf, ",\"sourceKind\":\"program-graph\",\"graphHash\":");
    append_json_string(buf, graph_hash ? graph_hash : "");
    zbuf_appendf(buf, ",\"stdlibGraphHash\":\"%016llx\"", (unsigned long long)z_std_source_graph_fingerprint());
    zbuf_append(buf, ",\"graphKeyInputs\":");
    zbuf_append(buf, GRAPH_CACHE_INPUTS_MAPPED_MIR);
    zbuf_append(buf, ",\"parserArtifactsInKey\":false");
    zbuf_appendf(buf, ",\"dependencyGraphHash\":\"%016llx\",\"lockfileHash\":\"%016llx\",\"invalidatesOn\":",
                 (unsigned long long)input->dependency_graph_hash,
                 (unsigned long long)input->lockfile_hash);
    append_json_string(buf, "ProgramGraph input, target, emit kind, backend, or compiler version");
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->mapped_mir_cache_path);
    zbuf_appendf(buf, ",\"byteLength\":%zu,\"memoryMapped\":%s,\"borrowedStorage\":%s,\"written\":%s,\"codegenImmediate\":%s,\"programReconstructed\":%s}",
                 input->mapped_mir_cache_bytes,
                 input->mapped_mir_memory_mapped ? "true" : "false",
                 input->mapped_mir_borrowed_storage ? "true" : "false",
                 input->mapped_mir_cache_written ? "true" : "false",
                 input->mapped_mir_codegen_immediate ? "true" : "false",
                 input->mapped_mir_program_reconstructed ? "true" : "false");
    wrote = true;
  }
  APPEND_CACHE("emittedObject", object_key, input && input->emitted_object_cache_hit, graph_input ? "ProgramGraph input, target, profile, or backend" : "source, target, profile, or backend", GRAPH_CACHE_INPUTS_OBJECT);
#undef APPEND_CACHE
  zbuf_append(buf, "]");
}

static void append_compiler_caches_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *profile) { append_compiler_caches_json_ex(buf, input, target, profile, NULL, NULL); }

static void source_cache_hit_miss_counts(const SourceInput *input, size_t *hits, size_t *misses) {
  *hits = 0;
  *misses = 0;
#define COUNT_CACHE_HIT(field) do { if (input && input->field) (*hits)++; else (*misses)++; } while (0)
  COUNT_CACHE_HIT(parse_cache_hit);
  COUNT_CACHE_HIT(interface_cache_hit);
  COUNT_CACHE_HIT(check_cache_hit);
  COUNT_CACHE_HIT(specialization_cache_hit);
  if (input && input->mapped_mir_cache_path) {
    if (input->mapped_mir_cache_hit) (*hits)++;
    else (*misses)++;
  }
  COUNT_CACHE_HIT(emitted_object_cache_hit);
#undef COUNT_CACHE_HIT
}

static void append_incremental_invalidations_json_ex(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *profile, const char *graph_artifact, const char *graph_hash, const char *graph_lowering) {
  size_t hits = 0;
  size_t misses = 0;
  source_cache_hit_miss_counts(input, &hits, &misses);
  zbuf_append(buf, "{\"moduleDependencies\":");
  zbuf_append(buf, "[");
  for (size_t i = 0; input && i < input->import_edge_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"from\":");
    append_json_string(buf, input->import_from[i]);
    zbuf_append(buf, ",\"to\":");
    append_json_string(buf, input->import_to[i]);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->import_paths[i]);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\"targetDependency\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"profileDependency\":");
  append_json_string(buf, profile ? profile : "release");
  if (graph_hash && graph_hash[0]) {
    zbuf_append(buf, ",\"sourceKind\":\"program-graph\",\"graphInput\":{\"artifact\":");
    append_json_string(buf, graph_artifact ? graph_artifact : "");
    zbuf_append(buf, ",\"graphHash\":");
    append_json_string(buf, graph_hash);
    zbuf_append(buf, ",\"lowering\":");
    append_json_string(buf, graph_lowering && graph_lowering[0] ? graph_lowering : "direct-program-graph");
    zbuf_append(buf, ",\"parserArtifactsInKey\":false,\"keyedBy\":");
    zbuf_append(buf, GRAPH_CACHE_INPUTS_AGGREGATE);
    if (input && input->mapped_mir_cache_path) {
      zbuf_append(buf, ",\"mappedFinalMir\":{\"path\":");
      append_json_string(buf, input->mapped_mir_cache_path);
      zbuf_appendf(buf, ",\"byteLength\":%zu,\"hit\":%s,\"written\":%s,\"memoryMapped\":%s,\"borrowedStorage\":%s,\"codegenImmediate\":%s,\"programReconstructed\":%s}",
                   input->mapped_mir_cache_bytes,
                   input->mapped_mir_cache_hit ? "true" : "false",
                   input->mapped_mir_cache_written ? "true" : "false",
                   input->mapped_mir_memory_mapped ? "true" : "false",
                   input->mapped_mir_borrowed_storage ? "true" : "false",
                   input->mapped_mir_codegen_immediate ? "true" : "false",
                   input->mapped_mir_program_reconstructed ? "true" : "false");
    }
    zbuf_append(buf, "}");
  }
  zbuf_appendf(buf, ",\"affectedModules\":%zu,\"recheckStrategy\":\"fingerprint changed modules and dependent bodies\"", input ? input->module_count : 0);
  zbuf_append(buf, ",\"changedInputs\":{\"sourceFiles\":[");
  for (size_t i = 0; input && i < input->source_file_count; i++) {
    if (i > 0) zbuf_append(buf, ",");
    append_json_string(buf, input->source_files[i]);
  }
  zbuf_append(buf, "],\"manifestPath\":");
  append_json_string(buf, input && input->manifest_path ? input->manifest_path : "");
  zbuf_append(buf, ",\"packageLockfile\":");
  append_json_string(buf, input && input->lockfile_path ? input->lockfile_path : "");
  if (graph_hash && graph_hash[0]) {
    zbuf_append(buf, ",\"graphArtifact\":");
    append_json_string(buf, graph_artifact ? graph_artifact : "");
  }
  zbuf_append(buf, "},\"cacheHits\":");
  zbuf_appendf(buf, "%zu", hits);
  zbuf_append(buf, ",\"cacheMisses\":");
  zbuf_appendf(buf, "%zu", misses);
  zbuf_append(buf, ",\"partialDiagnosticsStable\":true,\"interfaceFingerprints\":");
  append_interface_fingerprints_json_ex(buf, input, target, graph_hash);
  zbuf_append(buf, "}");
}

static void append_incremental_invalidations_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *profile) { append_incremental_invalidations_json_ex(buf, input, target, profile, NULL, NULL, NULL); }

static bool package_dependency_target_compatible(const SourceDependency *dep, const ZTargetInfo *target) {
  if (!dep || !json_array_nonempty_literal(dep->targets_json)) return true;
  return json_array_contains_string_literal(dep->targets_json, target ? target->name : z_host_target()) ||
         json_array_contains_string_literal(dep->targets_json, target && target->zig_target ? target->zig_target : "") ||
         json_array_contains_string_literal(dep->targets_json, z_host_target());
}

static void append_package_metadata_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"name\":");
  append_json_string(buf, input && input->package_name ? input->package_name : "");
  zbuf_append(buf, ",\"version\":");
  append_json_string(buf, input && input->package_version ? input->package_version : "");
  zbuf_append(buf, ",\"root\":");
  append_json_string(buf, input && input->package_root ? input->package_root : "");
  zbuf_append(buf, ",\"manifestPath\":");
  append_json_string(buf, input && input->manifest_path ? input->manifest_path : "");
  zbuf_appendf(buf, ",\"manifestHash\":\"%016llx\",\"dependencyGraphHash\":\"%016llx\"",
               (unsigned long long)(input ? input->manifest_hash : 0),
               (unsigned long long)(input ? input->dependency_graph_hash : 0));
  zbuf_append(buf, ",\"lockfile\":{\"format\":\"zero-lock-v1\",\"path\":");
  append_json_string(buf, input && input->lockfile_path ? input->lockfile_path : "");
  zbuf_appendf(buf, ",\"hash\":\"%016llx\",\"generated\":%s}",
               (unsigned long long)(input ? input->lockfile_hash : 0),
               input && input->lockfile_path ? "true" : "false");
  zbuf_append(buf, ",\"resolver\":{\"deterministic\":true,\"registryReadyMetadata\":true,\"remoteFetch\":false,\"versionSolver\":\"exact-version-or-path\"}");
  zbuf_append(buf, ",\"dependencies\":[");
  for (size_t i = 0; input && i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, dep->name);
    zbuf_append(buf, ",\"version\":");
    append_json_string(buf, dep->version);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, dep->path);
    zbuf_append(buf, ",\"resolvedManifest\":");
    append_json_string(buf, dep->resolved_manifest);
    zbuf_append(buf, ",\"resolvedName\":");
    append_json_string(buf, dep->resolved_name);
    zbuf_append(buf, ",\"resolvedVersion\":");
    append_json_string(buf, dep->resolved_version);
    zbuf_append(buf, ",\"status\":");
    append_json_string(buf, dep->status);
    zbuf_appendf(buf, ",\"direct\":%s,\"fingerprint\":\"%016llx\",\"targetCompatible\":%s,\"targets\":",
                 dep->direct ? "true" : "false",
                 (unsigned long long)dep->fingerprint,
                 package_dependency_target_compatible(dep, target) ? "true" : "false");
    zbuf_append(buf, dep->targets_json ? dep->targets_json : "[]");
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]}");
}

static void append_package_cache_audit_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *profile) {
  zbuf_append(buf, "{\"cacheKeyInputs\":{\"compilerVersion\":");
  append_json_string(buf, ZERO_VERSION);
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"packageVersion\":");
  append_json_string(buf, input && input->package_version ? input->package_version : "");
  zbuf_appendf(buf, ",\"dependencyGraphHash\":\"%016llx\",\"manifestHash\":\"%016llx\",\"lockfileHash\":\"%016llx\"}",
               (unsigned long long)(input ? input->dependency_graph_hash : 0),
               (unsigned long long)(input ? input->manifest_hash : 0),
               (unsigned long long)(input ? input->lockfile_hash : 0));
  zbuf_append(buf, ",\"invalidationReasons\":[\"source changed\",\"manifest changed\",\"dependency graph changed\",\"target changed\",\"profile changed\",\"compiler version changed\"],\"profile\":");
  append_json_string(buf, profile ? profile : "release");
  zbuf_append(buf, "}");
}

static void append_self_host_routing_json(ZBuf *buf, const char *command_name, const char *emit_kind, const Program *program, const CapabilitySummary *caps, const ZTargetInfo *target);
static void append_target_capability_facts_json(ZBuf *buf, const ZTargetInfo *target, const CapabilitySummary *caps);
static void append_target_readiness_json(ZBuf *buf, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command);
static void append_target_readiness_from_ir_json(ZBuf *buf, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command, const IrProgram *ir);
static bool repository_graph_target_readiness_select_diag(const Command *command, const SourceInput *input, const ZTargetInfo *target, const IrProgram *ir, ZDiag *diag);
static const char *emit_kind_name(EmitKind emit);
static void append_backend_blocker_json(ZBuf *buf, const ZBackendBlocker *blocker);
static void complete_backend_blocker_diag(ZDiag *diag, const ZTargetInfo *target, const Command *command, const char *emit_kind, const char *stage);
static void init_direct_backend_diag(ZDiag *diag, const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const char *reason);
static void init_lowering_backend_diag(ZDiag *diag, const SourceInput *input, const ZTargetInfo *target, const Command *command, const IrProgram *ir);
static void append_used_stdlib_helpers_json(ZBuf *buf, const HelperUseSummary *helpers);
static void append_runtime_shims_json(ZBuf *buf, const char *emitted_symbol_text, const CapabilitySummary *caps);
static const Function *find_program_function(const Program *program, const char *name);
static void append_function_effects_json(ZBuf *buf, const Function *fun);
static void append_function_ownership_json(ZBuf *buf, const Function *fun);

static const char *static_param_kind_json(const Program *program, const char *type) {
  if (type && strcmp(type, "Bool") == 0) return "bool";
  if (type) {
    for (size_t i = 0; program && i < program->enums.len; i++) {
      if (strcmp(program->enums.items[i].name, type) == 0) return "enum";
    }
  }
  return "integer";
}

static void append_type_param_names_json(ZBuf *buf, const ParamVec *params) {
  zbuf_append(buf, "[");
  for (size_t i = 0; params && i < params->len; i++) {
    if (i > 0) zbuf_append(buf, ",");
    append_json_string(buf, params->items[i].name);
  }
  zbuf_append(buf, "]");
}

static void append_static_params_json(ZBuf *buf, const Program *program, const ParamVec *params) {
  zbuf_append(buf, "[");
  bool wrote_static_param = false;
  for (size_t i = 0; params && i < params->len; i++) {
    const Param *type_param = &params->items[i];
    if (!type_param->is_static) continue;
    if (wrote_static_param) zbuf_append(buf, ",");
    wrote_static_param = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, type_param->name);
    zbuf_append(buf, ",\"type\":");
    append_json_string(buf, type_param->type ? type_param->type : "usize");
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, static_param_kind_json(program, type_param->type));
    zbuf_append(buf, ",\"staticDispatch\":true}");
  }
  zbuf_append(buf, "]");
}

static void append_type_param_constraints_json(ZBuf *buf, const ParamVec *params) {
  zbuf_append(buf, "[");
  bool wrote_constraint = false;
  for (size_t i = 0; params && i < params->len; i++) {
    const Param *type_param = &params->items[i];
    if (type_param->is_static) continue;
    if (!type_param->type || strcmp(type_param->type, "Type") == 0) continue;
    if (wrote_constraint) zbuf_append(buf, ",");
    wrote_constraint = true;
    zbuf_append(buf, "{\"typeParam\":");
    append_json_string(buf, type_param->name);
    zbuf_append(buf, ",\"interface\":");
    append_json_string(buf, type_param->type);
    zbuf_append(buf, ",\"staticDispatch\":true}");
  }
  zbuf_append(buf, "]");
}

static void append_compile_time_json(ZBuf *buf, const Program *program, const SourceInput *input, const ZTargetInfo *target) {
  char *manifest = read_optional_manifest_file(input);
  zbuf_append(buf, "{");
  zbuf_append(buf, "\"deterministic\":true,\"releaseMetadataDefault\":false");
  zbuf_append(buf, ",\"sandbox\":{\"filesystem\":\"denied\",\"network\":\"denied\",\"ambientEnv\":\"denied\",\"process\":\"denied\"}");
  zbuf_append(buf, ",\"limits\":{\"maxDepth\":64,\"maxSteps\":1024,\"stringBytes\":127,\"memory\":\"bounded-evaluator-state-only\",\"time\":\"step-counted\"}");
  zbuf_append(buf, ",\"cacheKeyInputs\":{\"algorithm\":\"fnv1a64-zero-meta-v1\"");
  append_hash_json(buf, "sourceHash", fnv1a_text(input ? input->source : ""));
  append_hash_json(buf, "targetHash", fnv1a_text(target ? target->name : z_host_target()));
  append_hash_json(buf, "manifestHash", fnv1a_text(manifest ? manifest : ""));
  zbuf_append(buf, ",\"compilerVersion\":");
  append_json_string(buf, ZERO_VERSION);
  zbuf_append(buf, ",\"declaredInputs\":[\"source\",\"target\",\"compilerVersion\",\"manifest\",\"targetFacts\"]}");
  zbuf_append(buf, ",\"meta\":{\"supportedFacts\":[\"literal arithmetic\",\"literal comparisons\",\"Bool logic\",\"target.os\",\"target.arch\",\"target.abi\",\"target.libc\",\"target.objectFormat\",\"target.endian\",\"target.pointerWidth\",\"target.hasCapability\",\"fieldCount\",\"hasField\",\"fieldType\",\"enumCaseCount\",\"hasEnumCase\",\"choiceCaseCount\",\"hasChoiceCase\"],\"unsupportedEffects\":[\"filesystem\",\"network\",\"process\",\"ambient environment\"],\"cycleDiagnostic\":\"MET001\",\"safetyLimitDiagnostic\":\"MET001\"}");
  zbuf_append(buf, ",\"reflection\":{\"typed\":true,\"compileTimeOnly\":true,\"releaseMetadataRetained\":false,\"facts\":[\"target\",\"shape fields\",\"enum cases\",\"choice cases\"]}");
  zbuf_append(buf, ",\"staticValues\":{\"supported\":[\"integer\",\"Bool\",\"enum\"],\"concreteSpecializations\":true,\"runtimeRegistries\":false,\"reflectionTables\":false,\"mismatchesDiagnostic\":\"STC003\"}");
  zbuf_append(buf, ",\"typedBuilders\":{\"status\":\"limited-v1\",\"supported\":[\"shape literals\",\"enum case values\",\"choice constructors\"],\"rawTokenStrings\":false,\"compileTimeOnly\":true,\"runtimeBuilderRegistry\":false}");
  zbuf_append(buf, ",\"programSurface\":{\"shapeCount\":");
  zbuf_appendf(buf, "%zu", program ? program->shapes.len : 0);
  zbuf_append(buf, ",\"enumCount\":");
  zbuf_appendf(buf, "%zu", program ? program->enums.len : 0);
  zbuf_append(buf, ",\"choiceCount\":");
  zbuf_appendf(buf, "%zu", program ? program->choices.len : 0);
  zbuf_append(buf, "}}");
  free(manifest);
}

static void append_program_graph_artifact_source_json(ZBuf *buf, const ZProgramGraphArtifactSource *source);
static void append_safety_facts_json(ZBuf *buf, const char *profile);
typedef struct RepositoryGraphCheckReadiness RepositoryGraphCheckReadiness;
static bool append_repository_graph_target_readiness_json(ZBuf *buf, SourceInput *input, const ZProgramGraphStore *store, const ZProgramGraphResolutionFacts *resolution, const ZTargetInfo *target, const Command *command, RepositoryGraphCheckReadiness *readiness, long long *lower_ms_out, bool *graph_mir_used_out);
static bool validate_package_dependencies_for_target(const SourceInput *input, const ZTargetInfo *target, ZDiag *diag);

static bool graph_check_text_eq(const char *left, const char *right) {
  const unsigned char *a = (const unsigned char *)(left ? left : "");
  const unsigned char *b = (const unsigned char *)(right ? right : "");
  while (*a || *b) {
    if (*a != *b) return false;
    a++;
    b++;
  }
  return true;
}

static const ZProgramGraphNode *graph_check_node_by_id(const ZProgramGraph *graph, const char *id) {
  for (size_t i = 0; graph && id && i < graph->node_len; i++) {
    if (graph_check_text_eq(graph->nodes[i].id, id)) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphResolutionReference *graph_check_first_bad_reference(const ZProgramGraphResolutionFacts *facts) {
  for (size_t i = 0; facts && i < facts->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &facts->references[i];
    if (!ref->resolved || ref->ambiguous) return ref;
  }
  return NULL;
}

static bool graph_check_resolution_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *facts, const char *path, ZDiag *diag) {
  if (facts && facts->diagnostic_len == 0) return true;
  const ZProgramGraphResolutionReference *ref = graph_check_first_bad_reference(facts);
  const ZProgramGraphNode *node = graph_check_node_by_id(graph, ref ? ref->node_id : NULL);
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = ref && ref->ambiguous ? 3004 : 3003;
    diag->path = node && node->path && node->path[0] ? node->path : path;
    diag->line = node && node->line > 0 ? node->line : 1;
    diag->column = node && node->column > 0 ? node->column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "%s", ref && ref->ambiguous ? "ambiguous graph reference" : "unresolved graph reference");
    snprintf(diag->expected, sizeof(diag->expected), "resolved graph symbol reference");
    snprintf(diag->actual, sizeof(diag->actual), "node %s name %s", ref && ref->node_id ? ref->node_id : "<unknown>", ref && ref->name ? ref->name : "<unknown>");
    snprintf(diag->help, sizeof(diag->help), "inspect zero query and zero source-map, then repair the referenced graph node or import binding");
  }
  return false;
}

static bool capability_available_on_target(const ZTargetInfo *target, const char *capability);

static void graph_check_target_capability_diag(ZDiag *diag, const ZProgramGraphNode *node, const ZTargetInfo *target, const char *capability, const char *label) {
  if (!diag) return;
  *diag = (ZDiag){0};
  diag->code = 6002;
  diag->path = node && node->path && node->path[0] ? node->path : NULL;
  diag->line = node && node->line > 0 ? node->line : 1;
  diag->column = node && node->column > 0 ? node->column : 1;
  diag->length = 1;
  if (graph_check_text_eq(capability, "world")) {
    snprintf(diag->message, sizeof(diag->message), "target does not provide World stdio capability");
    snprintf(diag->expected, sizeof(diag->expected), "target with stdio capability");
    snprintf(diag->actual, sizeof(diag->actual), "target %s lacks stdio", target ? target->name : "<unknown>");
    snprintf(diag->help, sizeof(diag->help), "select a target with stdio or use a narrower capability");
    return;
  }
  snprintf(diag->message, sizeof(diag->message), "target does not provide required %s capability", label ? label : capability);
  snprintf(diag->expected, sizeof(diag->expected), "target with %s capability", label ? label : capability);
  snprintf(diag->actual, sizeof(diag->actual), "target %s lacks %s", target ? target->name : "<unknown>", label ? label : capability);
  if (graph_check_text_eq(capability, "fs")) {
    snprintf(diag->help, sizeof(diag->help), "build for host target %s or remove hosted std.fs usage from this target-neutral build", z_host_target());
  } else {
    snprintf(diag->help, sizeof(diag->help), "build for a target that declares %s or remove that capability from this target-neutral entry point", label ? label : capability);
  }
}

static bool graph_check_target_capabilities_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZTargetInfo *target, ZDiag *diag) {
  ZProgramGraphCapabilitySummary caps;
  z_program_graph_collect_capabilities(graph, resolution, &caps);
#define DENY_GRAPH_CAP(field, cap_name, label) do { \
    if (caps.field && !capability_available_on_target(target, cap_name)) { \
      graph_check_target_capability_diag(diag, caps.field##_node, target, cap_name, label); \
      return false; \
    } \
  } while (0)
  DENY_GRAPH_CAP(fs, "fs", "Fs");
  DENY_GRAPH_CAP(args, "args", "Args");
  DENY_GRAPH_CAP(env, "env", "Env");
  DENY_GRAPH_CAP(time, "time", "Clock");
  DENY_GRAPH_CAP(rand, "rand", "Rand");
  DENY_GRAPH_CAP(net, "net", "Net");
  DENY_GRAPH_CAP(proc, "proc", "Proc");
  DENY_GRAPH_CAP(web, "web", "Web");
  DENY_GRAPH_CAP(world, "world", "World");
#undef DENY_GRAPH_CAP
  return true;
}

static bool graph_stored_compiler_input_ok(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const SourceInput *input, const ZTargetInfo *target, const char *path, ZDiag *diag) {
  if (!graph_check_resolution_ok(graph, resolution, path, diag)) return false;
  if (!z_program_graph_fixed_array_length_contracts_ok(graph, resolution, path, diag)) return false;
  if (!graph_check_target_capabilities_ok(graph, resolution, target, diag)) return false;
  return validate_package_dependencies_for_target(input, target, diag);
}

static void append_repository_graph_default_readiness_json(ZBuf *buf, const char *projection_state, const ZProgramGraphResolutionFacts *resolution, long long load_ms, long long validate_ms, long long resolve_ms, long long check_ms, long long lower_ms, long long cache_ms, bool validation_in_load, bool target_ready, bool graph_mir_used) {
  bool resolution_ok = resolution && resolution->diagnostic_len == 0;
  bool compiler_input_ready = resolution_ok && graph_mir_used && target_ready;
  bool within_budget =
    load_ms <= 50 &&
    validate_ms <= 50 &&
    resolve_ms <= 50 &&
    check_ms <= 50 &&
    lower_ms <= 100 &&
    cache_ms <= 25;
  zbuf_append(buf, "{\"schemaVersion\":1,\"compilerInputReady\":");
  zbuf_append(buf, compiler_input_ready ? "true" : "false");
  zbuf_append(buf, ",\"claim\":");
  append_json_string(buf, compiler_input_ready ? "ready-for-repository-graph-input" : "blocked");
  zbuf_appendf(buf, ",\"sourceFreeCompile\":%s,\"sourceProjectionRequired\":false,\"sourceProjectionState\":", compiler_input_ready ? "true" : "false");
  append_json_string(buf, projection_state ? projection_state : "unavailable");
  zbuf_append(buf, ",\"targetReadinessOk\":");
  zbuf_append(buf, target_ready ? "true" : "false");
  zbuf_append(buf, ",\"targetReadinessReportedIn\":\"targetReadiness\"");
  zbuf_append(buf, ",\"graphMir\":{\"used\":");
  zbuf_append(buf, graph_mir_used ? "true" : "false");
  zbuf_append(buf, "}");
  zbuf_append(buf, ",\"unsupportedGraphFacts\":{\"count\":0,\"facts\":[]}");
  zbuf_append(buf, ",\"performance\":{\"withinBudget\":");
  zbuf_append(buf, within_budget ? "true" : "false");
  zbuf_append(buf, ",\"unit\":\"ms\",\"validationInLoad\":");
  zbuf_append(buf, validation_in_load ? "true" : "false");
  zbuf_append(buf, ",\"budgets\":{\"loadMs\":50,\"validateMs\":50,\"resolveMs\":50,\"checkMs\":50,\"lowerMs\":100,\"cacheMs\":25},\"timings\":{\"loadMs\":");
  zbuf_appendf(buf, "%lld,\"validateMs\":%lld,\"resolveMs\":%lld,\"checkMs\":%lld,\"lowerMs\":%lld,\"cacheMs\":%lld}", load_ms, validate_ms, resolve_ms, check_ms, lower_ms, cache_ms);
  zbuf_append(buf, "}");
  zbuf_append(buf, ",\"cacheInvalidation\":{\"sourceKind\":\"program-graph\",\"parserArtifactsInKey\":false,\"keyedBy\":");
  zbuf_append(buf, GRAPH_CACHE_INPUTS_AGGREGATE);
  zbuf_append(buf, "}");
  zbuf_append(buf, "}");
}

static void append_repository_graph_compiler_path_json(ZBuf *buf, const ZTargetInfo *target, const ZProgramGraphStore *store, const ZProgramGraphResolutionFacts *resolution, const char *projection_state, long long load_ms, long long validate_ms, long long resolve_ms, long long check_ms, long long lower_ms, long long cache_ms, bool validation_in_load, bool target_ready, bool graph_mir_used) {
  ZProgramGraphStoreTableCounts tables;
  z_program_graph_store_table_counts_for_graph(store ? &store->graph : NULL,
                                               store ? store->source_path_len : 0,
                                               store ? store->projection_len : 0,
                                               &tables);
  (void)target;
  zbuf_append(buf, "{\"schemaVersion\":1,\"input\":\"repository-graph-store\",\"graphStoreLoaded\":true");
  zbuf_append(buf, ",\"sourceProjectionRequiredForCompilerInput\":false,\"sourceProjectionState\":");
  append_json_string(buf, projection_state);
  zbuf_append(buf, ",\"graphNativeCheckerUsed\":true,\"graphHirToMirUsed\":");
  zbuf_append(buf, graph_mir_used ? "true" : "false");
  zbuf_append(buf, ",\"unsupportedGraphFacts\":{\"count\":0,\"facts\":[]}");
  zbuf_appendf(buf, ",\"timings\":{\"loadMs\":%lld,\"validateMs\":%lld,\"resolveMs\":%lld,\"checkMs\":%lld,\"lowerMs\":%lld,\"cacheMs\":%lld,\"validationInLoad\":%s}", load_ms, validate_ms, resolve_ms, check_ms, lower_ms, cache_ms, validation_in_load ? "true" : "false");
  zbuf_append(buf, ",\"defaultReadiness\":");
  append_repository_graph_default_readiness_json(buf, projection_state, resolution, load_ms, validate_ms, resolve_ms, check_ms, lower_ms, cache_ms, validation_in_load, target_ready, graph_mir_used);
  zbuf_append(buf, ",\"tables\":");
  z_program_graph_store_append_table_counts_json(buf, &tables);
  zbuf_append(buf, ",\"resolution\":{\"state\":\"resolved-graph-facts\",\"ok\":");
  zbuf_append(buf, resolution && resolution->diagnostic_len == 0 ? "true" : "false");
  zbuf_appendf(buf, ",\"references\":%zu,\"diagnostics\":%zu}", resolution ? resolution->reference_len : 0, resolution ? resolution->diagnostic_len : 0);
  zbuf_append(buf, ",\"checking\":{\"state\":\"checked-graph-readiness-facts\",\"ok\":");
  zbuf_append(buf, resolution && resolution->diagnostic_len == 0 ? "true" : "false");
  zbuf_append(buf, ",\"scope\":\"resolution-package-target-and-graph-mir-readiness\",\"semanticDiagnosticsEnforced\":false,\"semanticDiagnosticsAuthority\":\"stored-typed-graph-facts\",\"authority\":\"ProgramGraphStore\",\"sourceTextAuthority\":false}");
  zbuf_append(buf, ",\"semanticFacts\":");
  z_program_graph_append_semantics_json(buf, store ? &store->graph : NULL);
  zbuf_append(buf, "}");
}

static void print_repository_graph_check_json_success(const Command *command, const ZTargetInfo *target, SourceInput *input, const ZProgramGraphStore *store, const ZProgramGraphResolutionFacts *resolution, RepositoryGraphCheckReadiness *readiness, long long load_ms, long long resolve_ms, long long check_ms, long long cache_ms) {
  ZBuf target_readiness;
  zbuf_init(&target_readiness);
  long long lower_ms = 0;
  bool graph_mir_used = false;
  bool target_ready = append_repository_graph_target_readiness_json(&target_readiness, input, store, resolution, target, command, readiness, &lower_ms, &graph_mir_used);
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"sourceFile\": ");
  append_json_string(&buf, store && store->path ? store->path : (command ? command->input : ""));
  const char *projection_state = z_program_graph_projection_state_label(store, target, NULL, NULL, NULL);
  ZProgramGraphArtifactSource graph_source = {
    .artifact = store && store->path ? store->path : "",
    .graph_hash = store && store->graph.graph_hash ? store->graph.graph_hash : "",
    .module_identity = store && store->graph.module_identity ? store->graph.module_identity : "",
    .lowering = "graph-native-check",
    .source_projection_state = projection_state,
    .canonical_source = false,
  };
  append_program_graph_artifact_source_json(&buf, &graph_source);
  const char *profile = command && command->profile ? command->profile : "release";
  zbuf_append(&buf, ",\n  \"package\": ");
  append_package_metadata_json(&buf, input, target);
  zbuf_append(&buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(&buf, input, target, profile);
  zbuf_append(&buf, ",\n  \"diagnostics\": [],\n  \"compileTime\": ");
  append_compile_time_json(&buf, NULL, input, target);
  zbuf_append(&buf, ",\n  \"targetReadiness\": ");
  zbuf_append(&buf, target_readiness.data ? target_readiness.data : "{\"schemaVersion\":1,\"ok\":false,\"languageOk\":true,\"buildable\":false,\"target\":\"unknown\",\"emit\":\"exe\",\"objectFormat\":\"unknown\",\"backend\":\"none\",\"stage\":\"select\",\"diagnostics\":[]}");
  zbuf_append(&buf, ",\n  \"safetyFacts\": ");
  append_safety_facts_json(&buf, profile);
  zbuf_append(&buf, ",\n  \"graphCompiler\": ");
  append_repository_graph_compiler_path_json(&buf, target, store, resolution, projection_state, load_ms, 0, resolve_ms, check_ms, lower_ms, cache_ms, true, target_ready, graph_mir_used);
  zbuf_append(&buf, ",\n  \"compilerPhases\": ");
  append_compiler_phases_json(&buf, input);
  zbuf_append(&buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json_ex(&buf, input, target, profile, "program-graph", store && store->graph.graph_hash ? store->graph.graph_hash : "");
  zbuf_append(&buf, ",\n  \"interfaceFingerprints\": ");
  append_interface_fingerprints_json_ex(&buf, input, target, store && store->graph.graph_hash ? store->graph.graph_hash : "");
  zbuf_append(&buf, ",\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json_ex(&buf, input, target, profile, store && store->path ? store->path : "", store && store->graph.graph_hash ? store->graph.graph_hash : "", "graph-native-check");
  zbuf_append(&buf, "\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
  zbuf_free(&target_readiness);
}

static void print_check_json_success(const char *path, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command, const IrProgram *prepared_ir) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"sourceFile\": ");
  append_json_string(&buf, path);
  if (command) append_program_graph_artifact_source_json(&buf, &command->graph_source);
  bool graph_input = command && z_program_graph_artifact_source_present(&command->graph_source);
  const char *profile = command && command->profile ? command->profile : "release";
  CapabilitySummary caps = program_capabilities(program);
  ZMetaCacheStats meta = z_meta_cache_stats();
  char *manifest = read_optional_manifest_file(input);
  zbuf_append(&buf, ",\n  \"package\": ");
  append_package_metadata_json(&buf, input, target);
  zbuf_append(&buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(&buf, input, target, profile);
  zbuf_append(&buf, ",\n  \"diagnostics\": [],\n  \"metaCache\": {");
  zbuf_appendf(&buf, "\n    \"hits\": %zu,\n    \"misses\": %zu,\n    \"entries\": %zu", meta.hits, meta.misses, meta.entries);
  append_hash_json(&buf, "sourceHash", fnv1a_text(input ? input->source : ""));
  append_hash_json(&buf, "targetHash", fnv1a_text(target ? target->name : z_host_target()));
  append_hash_json(&buf, "manifestHash", fnv1a_text(manifest ? manifest : ""));
  zbuf_append(&buf, "\n  },\n  \"compileTime\": ");
  append_compile_time_json(&buf, program, input, target);
  zbuf_append(&buf, ",\n  \"targetReadiness\": ");
  if (prepared_ir && command && z_program_graph_artifact_source_present(&command->graph_source)) append_target_readiness_from_ir_json(&buf, input, program, target, command, prepared_ir);
  else append_target_readiness_json(&buf, input, program, target, command);
  zbuf_append(&buf, ",\n  \"safetyFacts\": ");
  append_safety_facts_json(&buf, profile);
  zbuf_append(&buf, ",\n  \"compilerPhases\": ");
  append_compiler_phases_json(&buf, input);
  zbuf_append(&buf, ",\n  \"compilerCaches\": ");
  if (graph_input) append_compiler_caches_json_ex(&buf, input, target, profile, "program-graph", command->graph_source.graph_hash);
  else append_compiler_caches_json(&buf, input, target, profile);
  zbuf_append(&buf, ",\n  \"interfaceFingerprints\": ");
  if (graph_input) append_interface_fingerprints_json_ex(&buf, input, target, command->graph_source.graph_hash);
  else append_interface_fingerprints_json(&buf, input, target);
  zbuf_append(&buf, ",\n  \"incrementalInvalidation\": ");
  if (graph_input) append_incremental_invalidations_json_ex(&buf, input, target, profile, command->graph_source.artifact, command->graph_source.graph_hash, command->graph_source.lowering);
  else append_incremental_invalidations_json(&buf, input, target, profile);
  zbuf_append(&buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(&buf, "check", NULL, program, &caps, target);
  zbuf_append(&buf, "\n}\n");
  free(manifest);
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static void append_public_docs_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target) {
  CapabilitySummary caps = program_capabilities(program);
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\n  \"package\": ");
  append_package_metadata_json(buf, input, target);
  zbuf_append(buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(buf, input, target, "release");
  zbuf_append(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"packageSurface\": {\"sourceFileCount\": ");
  zbuf_appendf(buf, "%zu", input ? input->source_file_count : 0);
  zbuf_append(buf, ", \"moduleCount\": ");
  zbuf_appendf(buf, "%zu", input ? input->module_count : 0);
  zbuf_append(buf, "},\n  \"modules\": [");
  for (size_t i = 0; input && i < input->module_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, input->module_names[i]);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->module_paths[i]);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"symbols\": [");
  bool wrote_symbol = false;
  for (size_t i = 0; input && i < input->symbol_count; i++) {
    if (!input->symbol_public[i]) continue;
    if (wrote_symbol) zbuf_append(buf, ", ");
    wrote_symbol = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, input->symbol_names[i]);
    zbuf_append(buf, ",\"module\":");
    append_json_string(buf, input->symbol_modules[i]);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, input->symbol_kinds ? input->symbol_kinds[i] : "unknown");
    zbuf_append(buf, ",\"doc\":\"\",\"examples\":[],\"targetSupport\":");
    const Function *fun = (input->symbol_kinds && strcmp(input->symbol_kinds[i], "function") == 0) ? find_program_function(program, input->symbol_names[i]) : NULL;
    if (fun) {
      CapabilitySummary fun_caps = function_capabilities(fun);
      append_target_capability_facts_json(buf, target, &fun_caps);
      zbuf_append(buf, ",\"effects\":");
      append_function_effects_json(buf, fun);
      zbuf_append(buf, ",\"ownership\":");
      append_function_ownership_json(buf, fun);
    } else {
      append_target_capability_facts_json(buf, target, &caps);
      zbuf_append(buf, ",\"effects\":[],\"ownership\":{\"params\":[],\"returnOwnership\":\"value\",\"movesOwnership\":false}");
    }
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"requiresCapabilities\": ");
  append_capability_json_array(buf, &caps);
  zbuf_append(buf, ",\n  \"publicationGate\": {\"releaseGrade\": false, \"requiresExamplesForPublicApi\": true, \"missingExampleCount\": ");
  zbuf_appendf(buf, "%zu", wrote_symbol ? (size_t)1 : (size_t)0);
  zbuf_append(buf, ", \"message\": \"public package APIs must carry examples and docs metadata before registry publication\"}");
  zbuf_append(buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(buf, "doc", NULL, program, &caps, target);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(buf, input, target, "release");
  zbuf_append(buf, "\n}\n");
}

static size_t program_test_count(const Program *program, const char *filter) {
  size_t count = 0;
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (!fun->is_test) continue;
    if (filter && filter[0] && (!fun->test_name || !strstr(fun->test_name, filter))) continue;
    count++;
  }
  return count;
}

static void append_program_graph_artifact_source_json(ZBuf *buf, const ZProgramGraphArtifactSource *source) {
  if (!z_program_graph_artifact_source_present(source)) return;
  zbuf_append(buf, ",\n  \"graph\": {\"artifact\":");
  append_json_string(buf, source->artifact ? source->artifact : "");
  zbuf_appendf(buf, ",\"canonicalSource\":%s,\"moduleIdentity\":", source->canonical_source ? "true" : "false");
  append_json_string(buf, source->module_identity ? source->module_identity : "");
  zbuf_append(buf, ",\"graphHash\":");
  append_json_string(buf, source->graph_hash ? source->graph_hash : "");
  zbuf_append(buf, ",\"lowering\":");
  append_json_string(buf, source->lowering ? source->lowering : "direct-program-graph");
  if (source->source_projection_state) {
    zbuf_append(buf, ",\"sourceProjectionState\":");
    append_json_string(buf, source->source_projection_state);
  }
  zbuf_append(buf, "}");
}

static void append_test_source_json(ZBuf *buf, const SourceInput *input, const Command *command) {
  zbuf_append(buf, ",\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  if (command) append_program_graph_artifact_source_json(buf, &command->graph_source);
}

static const char *test_json_source_path_for_line(const SourceInput *input, int parser_line) {
  if (!input) return "";
  if (parser_line > 0) {
    size_t index = (size_t)parser_line - 1;
    if (index < input->source_line_count && input->source_line_paths[index] && input->source_line_paths[index][0]) {
      return input->source_line_paths[index];
    }
  }
  return input->source_file ? input->source_file : "";
}

static int test_json_source_line_for_line(const SourceInput *input, int parser_line) {
  if (parser_line <= 0) return 1;
  if (input) {
    size_t index = (size_t)parser_line - 1;
    if (index < input->source_line_count && input->source_line_numbers[index] > 0) {
      return input->source_line_numbers[index];
    }
  }
  return parser_line;
}

static void append_test_json_location(ZBuf *buf, const SourceInput *input, int parser_line, int column) {
  zbuf_append(buf, "{\"sourceFile\":");
  append_json_string(buf, test_json_source_path_for_line(input, parser_line));
  zbuf_appendf(buf, ",\"line\":%d,\"column\":%d}", test_json_source_line_for_line(input, parser_line), column > 0 ? column : 1);
}

static const char *test_discovery_mode(const SourceInput *input, const Command *command) {
  if (command && z_program_graph_artifact_source_present(&command->graph_source)) {
    return input && input->package_name && input->package_name[0] ? "package-graph" : "program-graph";
  }
  return input && input->package_root ? "package" : "single-file";
}

static void append_c_header_inputs_json(ZBuf *buf, const Program *program) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->c_imports.len; i++) {
    if (i > 0) zbuf_append(buf, ",");
    append_json_string(buf, program->c_imports.items[i].header);
  }
  zbuf_append(buf, "]");
}

static void append_json_string_array_item(ZBuf *buf, bool *first, const char *name) {
  if (!buf || !first || !name) return;
  if (!*first) zbuf_append(buf, ", ");
  *first = false;
  append_json_string(buf, name);
}

static const char *portable_runtime_kind(const ZTargetInfo *target) {
  (void)target;
  return "native";
}

static bool append_capability_imports_json(ZBuf *buf, const CapabilitySummary *caps, const ZTargetInfo *target) {
  (void)caps;
  (void)target;
  zbuf_append(buf, "[");
  zbuf_append(buf, "]");
  return false;
}

static void append_portable_capability_restrictions_json(ZBuf *buf, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"filesystem\":");
  append_json_string(buf, z_target_has_capability(target, "fs") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"environment\":");
  append_json_string(buf, z_target_has_capability(target, "env") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"arguments\":");
  append_json_string(buf, z_target_has_capability(target, "args") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"stdio\":");
  append_json_string(buf, z_target_has_capability(target, "stdio") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"dom\":");
  append_json_string(buf, "unavailable");
  zbuf_append(buf, ",\"network\":");
  append_json_string(buf, z_target_has_capability(target, "net") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"process\":");
  append_json_string(buf, z_target_has_capability(target, "proc") ? "target-capability" : "unavailable");
  zbuf_append(buf, "}");
}

static void append_local_runtime_plan_json(ZBuf *buf, const CapabilitySummary *caps, const ZTargetInfo *target) {
  ZBuf imports;
  zbuf_init(&imports);
  append_capability_imports_json(&imports, caps, target);
  zbuf_append(buf, "{\"schemaVersion\":1,\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"runtimeKind\":");
  append_json_string(buf, portable_runtime_kind(target));
  zbuf_append(buf, ",\"productionLikeImports\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"providerSpecificDeployment\":false,\"hostedDeployment\":\"out-of-scope\",\"command\":");
  append_json_string(buf, "zero dev");
  zbuf_append(buf, ",\"imports\":{\"explicit\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"module\":");
  zbuf_append(buf, "null");
  zbuf_append(buf, ",\"functions\":");
  zbuf_append(buf, imports.data);
  zbuf_append(buf, ",\"adapter\":");
  append_json_string(buf, "native-target-runtime");
  zbuf_append(buf, "},\"capabilityRestrictions\":");
  append_portable_capability_restrictions_json(buf, target);
  zbuf_append(buf, ",\"memoryFloorBudgetBytes\":");
  zbuf_append(buf, "0");
  zbuf_append(buf, ",\"frameworkTaxBytes\":0}");
  zbuf_free(&imports);
}

static void append_dev_plan_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command) {
  CapabilitySummary caps = program_capabilities(program);
  size_t test_count = program_test_count(program, command ? command->filter : NULL);
  const Function *main_fun = find_program_function(program, "main");
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"mode\": \"watch-plan\",\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"watch\": {\"strategy\":\"fingerprint changed modules and dependent tests\",\"persistent\": false, \"planOnly\": true, \"sourceFileCount\": ");
  zbuf_appendf(buf, "%zu", input ? input->source_file_count : 0);
  zbuf_append(buf, ", \"moduleCount\": ");
  zbuf_appendf(buf, "%zu", input ? input->module_count : 0);
  zbuf_append(buf, ", \"files\": [");
  for (size_t i = 0; input && i < input->source_file_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_json_string(buf, input->source_files[i]);
  }
  zbuf_append(buf, "], \"manifest\": ");
  const char *watch_manifest = "package manifest";
  if (input && input->manifest_path && input->manifest_path[0]) {
    const char *slash = strrchr(input->manifest_path, '/');
    watch_manifest = slash ? slash + 1 : input->manifest_path;
  }
  append_json_string(buf, watch_manifest);
  zbuf_append(buf, ", \"packageLocks\": [");
  if (input && input->lockfile_path) append_json_string(buf, input->lockfile_path);
  zbuf_append(buf, "], \"generatedBindingInputs\": ");
  append_c_header_inputs_json(buf, program);
  zbuf_append(buf, ", \"rerun\": [\"check\", \"test\", \"examples\"], \"restartOnSuccess\": ");
  zbuf_append(buf, main_fun ? "true" : "false");
  zbuf_append(buf, "},\n  \"affected\": {\"tests\": ");
  zbuf_appendf(buf, "%zu", test_count);
  zbuf_append(buf, ", \"examples\": ");
  zbuf_appendf(buf, "%zu", input && input->package_root && strstr(input->package_root, "examples") ? (size_t)1 : (size_t)0);
  zbuf_append(buf, ", \"modules\": ");
  zbuf_appendf(buf, "%zu", input ? input->module_count : 0);
  zbuf_append(buf, "},\n  \"actions\": [{\"kind\":\"check\",\"when\":\"source-or-manifest-changed\"}, {\"kind\":\"test\",\"selectedTests\":");
  zbuf_appendf(buf, "%zu", test_count);
  zbuf_append(buf, "}, {\"kind\":\"examples\",\"selectedExamples\":");
  zbuf_appendf(buf, "%zu", input && input->package_root && strstr(input->package_root, "examples") ? (size_t)1 : (size_t)0);
  zbuf_append(buf, "}, {\"kind\":\"restart\",\"enabled\":");
  zbuf_append(buf, main_fun ? "true" : "false");
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, "}],\n  \"restart\": {\"runnableCli\":");
  zbuf_append(buf, main_fun ? "true" : "false");
  zbuf_append(buf, ",\"strategy\":\"rebuild-and-restart-after-successful-checks\",\"binaryOut\":\".zero/dev/app\"},\n  \"partialDiagnostics\": {\"stable\":true,\"whileCodegenPending\":true,\"sourceOfTruth\":\"zero check --json\",\"schemaVersion\":1},\n  \"trace\": {\"enabled\":true,\"requested\":");
  zbuf_append(buf, command && command->trace ? "true" : "false");
  zbuf_append(buf, ",\"phaseTiming\":true,\"cacheFacts\":true,\"diagnosticsPassthrough\":true");
  zbuf_append(buf, "},\n  \"compilerPhases\": ");
  append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(buf, input, target, command ? command->profile : "dev");
  zbuf_append(buf, ",\n  \"interfaceFingerprints\": ");
  append_interface_fingerprints_json(buf, input, target);
  zbuf_append(buf, ",\n  \"package\": ");
  append_package_metadata_json(buf, input, target);
  zbuf_append(buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(buf, input, target, command ? command->profile : "dev");
  zbuf_append(buf, ",\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(buf, input, target, command ? command->profile : "dev");
  zbuf_append(buf, ",\n  \"requiresCapabilities\": ");
  append_capability_json_array(buf, &caps);
  zbuf_append(buf, ",\n  \"localRuntime\": ");
  append_local_runtime_plan_json(buf, &caps, target);
  zbuf_append(buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(buf, "dev", NULL, program, &caps, target);
  zbuf_append(buf, "\n}\n");
}

static void append_time_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command) {
  CapabilitySummary caps = program_capabilities(program);
  size_t hits = 0;
  size_t misses = 0;
  source_cache_hit_miss_counts(input, &hits, &misses);
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"compilerPhases\": ");
  append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, ",\n  \"package\": ");
  append_package_metadata_json(buf, input, target);
  zbuf_append(buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, ",\n  \"cacheSummary\": {\"hits\": ");
  zbuf_appendf(buf, "%zu", hits);
  zbuf_append(buf, ", \"misses\": ");
  zbuf_appendf(buf, "%zu", misses);
  zbuf_append(buf, ", \"entries\": 5, \"warmRebuildExpected\": ");
  zbuf_append(buf, hits > 0 ? "true" : "false");
  zbuf_append(buf, "},\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, ",\n  \"interfaceFingerprints\": ");
  append_interface_fingerprints_json(buf, input, target);
  zbuf_append(buf, ",\n  \"requiresCapabilities\": ");
  append_capability_json_array(buf, &caps);
  zbuf_append(buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(buf, "time", NULL, program, &caps, target);
  zbuf_append(buf, "\n}\n");
}

static const char *diag_fix_safety(int code) {
  switch (code) {
    case 7001:
    case 7002:
    case 7003:
    case 2002:
    case 2003:
    case 6002:
    case 8003:
    case 8004:
    case 8005:
    case 9001:
    case 9002:
    case 9003:
    case 9004:
      return "requires-human-review";
    case 1001:
    case 1002:
    case 1003:
      return "api-changing";
    case 3004:
    case 3005:
    case 3006:
    case 3010:
    case 3011:
    case 3012:
    case 3028:
    case 3029:
    case 3032:
    case 3033:
    case 3034:
    case 3036:
    case 3050:
    case 3037:
    case 3038:
    case 3039:
    case 3040:
    case 3041:
    case 3042:
    case 3043:
    case 3044:
    case 3045:
    case 3046:
    case 3047:
    case 3048:
    case 3049:
      return "behavior-preserving";
    default:
      return "requires-human-review";
  }
}

static const char *diag_repair_id(int code) {
  switch (code) {
    case 100: return "repair-syntax";
    case 1001: return "add-fallible-marker-or-rescue";
    case 1002: return "add-missing-error-name";
    case 1003: return "check-or-rescue-fallible-call";
    case 1004: return "rebuild-store-with-matching-compiler";
    case 1005: return "reimport-repository-graph-store";
    case 1006: return "resolve-source-identity";
    case 1007: return "resolve-source-identity";
    case 1008: return "regenerate-source-projection";
    case 1009: return "reconcile-projection-with-store";
    case 1010: return "resolve-repository-graph-merge-conflict";
    case 7001: return "fix-import-path";
    case 7002: return "break-import-cycle";
    case 3003: return "declare-missing-symbol";
    case 3007: return "match-return-type";
    case 3010: return "make-binding-mutable";
    case 3011: return "use-known-stdlib-helper";
    case 3012: return "match-stdlib-argument-type";
    case 3013: return "avoid-use-after-move";
    case 3014: return "fix-drop-signature";
    case 3015: return "add-memory-type-argument";
    case 3029: return "end-conflicting-borrow";
    case 3030: return "return-owned-value";
    case 3053: return "report-provenance-analysis-budget";
    case 3031: return "make-c-abi-safe";
    case 2003: return "use-direct-emitter";
    case 2004: return "choose-supported-backend";
    case 3032: return "match-generic-type-arguments";
    case 3033: return "make-generic-argument-types-match";
    case 3034: return "add-explicit-generic-type-arguments";
    case 3035: return "remove-unsupported-meta-expression";
    case 3036: return "fix-type-alias-declaration";
    case 3050: return "keep-recursive-generic-arguments-stable";
    case 3051: return "guard-maybe-payload";
    case 3052: return "move-large-locals-off-stack";
    case 3037: return "add-public-api-type";
    case 3038: return "declare-or-use-static-interface";
    case 3039: return "add-required-interface-method";
    case 3040: return "match-interface-method-arity";
    case 3041: return "match-interface-return-type";
    case 3042: return "match-interface-parameter-type";
    case 3043: return "use-supported-static-value-type";
    case 3044: return "pass-constant-static-value";
    case 3045: return "match-static-value-argument";
    case 3046: return "bind-generic-shape-method";
    case 3047: return "match-shape-method-self";
    case 3048: return "call-declared-receiver-method";
    case 3049: return "make-receiver-addressable-or-mutable";
    case 3102: return "initialize-missing-field";
    case 4004: return "report-codegen-invariant";
    case 6002: return "choose-target-with-required-capability";
    case 8003: return "configure-target-c-dependency";
    case 8004: return "fix-c-import-call";
    case 8005: return "configure-c-link-plan";
    case 9001: return "fix-package-dependency-path";
    case 9002: return "break-package-dependency-cycle";
    case 9003: return "choose-one-package-version";
    case 9004: return "choose-target-compatible-package";
    default: return code == 0 ? "repair-syntax" : "manual-review";
  }
}

static const char *diag_repair_summary(int code) {
  switch (code) {
    case 100: return "Repair the syntax at the reported parser span, then rerun zero check.";
    case 1001: return "Add `raises` or an explicit error set to the function signature, or handle the fallible expression with rescue.";
    case 1002: return "Add the missing error name to the `raises [...]` set or rescue the call locally.";
    case 1003: return "Wrap the fallible call in check or rescue so error flow is explicit.";
    case 1004: return "Rebuild zero.graph with this binary via `zero import .`, or install the zero build that wrote the store.";
    case 1005: return "Run zero import after reviewing the source projection so the store is rebuilt from source.";
    case 1006: return "Split the text edit into smaller passes or make the change with zero patch so graph identity stays unambiguous.";
    case 1007: return "Add package.name to zero.toml or zero.json so it matches the zero.graph module identity.";
    case 1008: return "Fix the projection target path or rerun zero export after reviewing graph changes.";
    case 1009: return "Review the diff, then run zero import if the .0 projection is authoritative or zero export if zero.graph is authoritative.";
    case 1010: return "Resolve the conflicting graph node or edge before merging.";
    case 7001: return "Change the import to a package-local module path that resolves under src/.";
    case 7002: return "Break the import cycle by moving shared declarations into a third module or removing one import edge.";
    case 3003: return "Declare the referenced symbol, import the module that provides it, or correct the identifier spelling.";
    case 3007: return "Change the returned expression or the function return annotation so both types agree.";
    case 3010: return "Change the root binding to `var` before passing it to a mutable API.";
    case 3011: return "Use one of the supported std helpers or add compiler support for the new helper.";
    case 3012: return "Pass the expected stdlib argument type, such as MutSpan<u8> for writable byte APIs.";
    case 3013: return "Stop using the moved binding, or keep ownership in the original binding until the final use.";
    case 3014: return "Use the canonical non-raising drop signature: `fn drop(self: mutref<Self>) -> Void`.";
    case 3015: return "Add the missing type argument, for example Maybe<u8> or Span<const u8>.";
    case 3029: return "End the active lexical borrow before taking a conflicting borrow or assigning to the borrowed root.";
    case 3030: return "Return an owned value, or keep references to local bindings inside the current function.";
    case 3031: return "Use explicit scalar, ref, or mutref types at C ABI boundaries and keep exported C functions non-raising.";
    case 2003: return "Use a direct emitter such as --emit exe or --emit obj.";
    case 2004: return "Choose an available backend and artifact kind, or simplify the program to the selected backend subset.";
    case 3032: return "Pass one type argument for each generic parameter, or remove type arguments from non-generic calls.";
    case 3033: return "Make all values that bind the same generic parameter use the same concrete type.";
    case 3034: return "Add explicit generic type arguments when the compiler cannot infer them from runtime arguments.";
    case 3035: return "Use deterministic meta expressions within the bounded evaluator: literal arithmetic, Bool logic, target facts, or typed reflection facts.";
    case 3036: return "Make the alias name unique and point it at a non-cyclic concrete type.";
    case 3050: return "Call recursively with the same generic type parameters or use a concrete helper.";
    case 3051: return "Prove the Maybe is present with `.has`, or use `check`/`rescue` so absence is handled explicitly.";
    case 3052: return "Split the buffer into smaller buffers in helper functions so each frame stays within the limit, or process the data in fixed-size chunks.";
    case 3037: return "Add an explicit public type annotation so graph and docs metadata stay stable.";
    case 3038: return "Declare the referenced interface or pass a concrete shape that satisfies the constraint.";
    case 3039: return "Add the required static method to the concrete shape.";
    case 3040: return "Make the concrete shape method take the same number of parameters as the interface method.";
    case 3041: return "Change the concrete shape method return type to match the interface.";
    case 3042: return "Change the concrete shape method parameter type to match the interface.";
    case 3043: return "Use a concrete integer, Bool, or enum type for static value parameters.";
    case 3044: return "Pass a literal, top-level const, or supported meta value with the static parameter type.";
    case 3045: return "Use the same static value in the explicit call and the value's annotated type.";
    case 3046: return "Pass a concrete self value or explicit shape arguments so the method can specialize.";
    case 3047: return "Make every argument agree with the same generic shape instantiation.";
    case 3048: return "Call a declared receiver method, or use namespace syntax for static methods without self.";
    case 3049: return "Store the receiver in an addressable binding and declare it with `var` for mutating methods.";
    case 3102: return "Initialize the missing shape field or add a default to the shape declaration.";
    case 4004: return "Report this compiler bug with the source program and target that produced it.";
    case 6002: return "Build for a target that provides the required capability, or move that capability behind a target-specific entry point.";
    case 8003: return "Use package-relative vendored headers/libraries or set the target sysroot instead of relying on host include, lib, or pkg-config discovery.";
    case 8004: return "Call a function declared by the imported C header, or wrap unsupported C ABI types behind a scalar C shim.";
    case 8005: return "Add matching c.libs headers plus package-relative C libraries or safe system library names to the package manifest for extern C calls.";
    case 9001: return "Create the local dependency package or update the path in zero.toml or zero.json.";
    case 9002: return "Remove the package cycle or move shared code into an acyclic dependency.";
    case 9003: return "Resolve the graph to one version of each package name.";
    case 9004: return "Select a target supported by the dependency or gate the dependency behind a compatible target.";
    case 3053: return "Simplify the recursive call cycle around the named function and report this compiler defect with the source program.";
    default: return code == 0 ? "Repair the syntax at the reported parser span, then rerun zero check." : "Inspect the diagnostic fields and choose a repair manually.";
  }
}

static bool diag_has_borrow_trace(const ZDiag *diag) {
  return diag && diag->code == 3029 && diag->borrow_trace_count > 0;
}

static void append_diag_borrow_trace_json(ZBuf *buf, const char *path, const ZDiag *diag) {
  zbuf_append(buf, "{\"rule\":\"lexical\",\"activeBorrows\":[");
  for (size_t i = 0; i < diag->borrow_trace_count; i++) {
    const ZBorrowTrace *trace = &diag->borrow_traces[i];
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"root\":");
    append_json_string(buf, trace->root);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, trace->path);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, trace->kind);
    zbuf_append(buf, ",\"binding\":");
    append_json_string_or_null(buf, trace->binding);
    zbuf_append(buf, ",\"bindingDecl\":");
    if (trace->binding_line > 0) {
      zbuf_append(buf, "{\"path\":");
      append_json_string(buf, trace->binding_decl_path ? trace->binding_decl_path : (diag->path ? diag->path : path));
      zbuf_appendf(buf, ",\"line\":%d,\"column\":%d}", trace->binding_line, trace->binding_column > 0 ? trace->binding_column : 1);
    } else {
      zbuf_append(buf, "null");
    }
    zbuf_append(buf, ",\"scopeExit\":null}");
  }
  zbuf_append(buf, "],\"truncated\":");
  zbuf_append(buf, diag->borrow_trace_truncated ? "true" : "false");
  zbuf_append(buf, ",\"repair\":");
  append_json_string(buf, diag->borrow_repair[0] ? diag->borrow_repair : diag_repair_summary(diag->code));
  zbuf_append(buf, "}");
}

typedef struct {
  const char *code;
  const char *category;
  const char *title;
  const char *summary;
  const char *why;
  const char *canonical_repair;
  const char *bad_example;
  const char *good_example;
} ExplainInfo;

static void print_diag_json(const char *path, const ZDiag *diag);
static void print_command_diag(const Command *command, const char *path, const ZDiag *diag);

static const ExplainInfo explain_infos[] = {
  {
    "TAR001",
    "target",
    "Unknown target",
    "The requested target name is not in the native bootstrap target table.",
    "Zero keeps target names explicit so cross builds do not silently use host assumptions.",
    "Run `zero targets` and choose one of the listed names.",
    "zero check --target not-a-target examples/hello.graph",
    "zero check --target linux-musl-x64 examples/hello.graph",
  },
  {
    "TAR002",
    "target",
    "Target capability unavailable",
    "The selected target does not provide a capability required by this program.",
    "Zero checks required standard-library capabilities against the target manifest so native, web, and embedded builds do not silently inherit host behavior.",
    "Build for a target that provides the required capability, or move that capability behind a target-specific entry point.",
    "zero check --target linux-musl-x64 conformance/common/fail/unsupported-target-feature.graph",
    "zero build --target host examples/memory-package --out .zero/out/memory-package",
  },
  {
    "ERR001",
    "fallibility",
    "Fallible function must declare errors",
    "A function body can raise errors but the function signature does not expose that fallibility.",
    "Error flow is part of a Zero function contract, so callers and agents need it visible at the boundary.",
    "Add `raises` or an explicit error set to the function signature, or handle the fallible expression with rescue.",
    "pub fn save(world: World) -> Void {\n    check world.out.write(\"saved\\n\")\n}",
    "pub fn save(world: World) -> Void raises {\n    check world.out.write(\"saved\\n\")\n}",
  },
  {
    "BLD003",
    "build",
    "Generated C backend removed",
    "Generated C output is no longer part of the supported Zero toolchain.",
    "`--emit c` and `--legacy-backend` were removed once direct artifacts became the product path.",
    "Use a direct emitter such as `--emit exe` or `--emit obj`.",
    "zero build --emit c examples/hello.graph",
    "zero build --emit exe examples/hello.graph",
  },
  {
    "TYP009",
    "type",
    "Mutable storage required",
    "A mutable API was given storage rooted in an immutable binding.",
    "`MutSpan<T>` and `mutref<T>` must come from storage that the source explicitly marks mutable.",
    "Change the root binding to `var`, or pass a writable `MutSpan<T>`/`mutref<T>` from another mutable owner.",
    "let dst: [4]u8 = [0, 0, 0, 0]\nlet _copied: usize = std.mem.copy(dst, src)",
    "var dst: [4]u8 = [0, 0, 0, 0]\nlet _copied: usize = std.mem.copy(dst, src)",
  },
  {
    "ERR002",
    "fallibility",
    "Error set mismatch",
    "A caller with an explicit `raises [...]` set checked a callee that may raise an unlisted error.",
    "Public and explicit error sets are part of the function contract, so propagation must keep the set complete.",
    "Add the missing error name to the caller's `raises [...]` set or handle the call locally with `rescue`.",
    "pub fn main(world: World) -> Void raises [NotFound] {\n    check std.fs.createOrRaise(fs, path)\n}",
    "pub fn main(world: World) -> Void raises [NotFound, TooLarge, Io] {\n    check std.fs.createOrRaise(fs, path)\n}",
  },
  {
    "ERR003",
    "fallibility",
    "Unchecked fallible call",
    "A fallible function or named-error stdlib helper was called without `check` or `rescue`.",
    "Zero keeps error flow visible in source and in function signatures.",
    "Wrap the call in `check`, or handle it locally with `rescue`.",
    "let file: owned<File> = std.fs.createOrRaise(fs, path)",
    "let file: owned<File> = check std.fs.createOrRaise(fs, path)",
  },
  {
    "STD003",
    "stdlib",
    "Stdlib argument mismatch",
    "A supported stdlib helper was called with an argument that does not match its implemented contract.",
    "The bootstrap stdlib surface is intentionally narrow, so helpers reject implicit conversions and unsupported capability shapes.",
    "Pass the exact expected argument type shown in the diagnostic, such as `MutSpan<u8>` for writable byte APIs.",
    "let bytes: [4]u8 = [0, 0, 0, 0]\nlet _filled: usize = std.mem.fill(bytes, 0_u8)",
    "var bytes: [4]u8 = [0, 0, 0, 0]\nlet _filled: usize = std.mem.fill(bytes, 0_u8)",
  },
  {
    "ABI001",
    "c-abi",
    "Unsupported C ABI surface",
    "An exported C function used a parameter, return type, or effect that the native checker cannot expose safely.",
    "Zero lowers exported C functions to direct native ABI symbols, so the boundary must use explicit scalar, `ref`, or `mutref` types and must not raise.",
    "Use explicit scalar, `ref`, or `mutref` types at C ABI boundaries and keep exported C functions non-raising.",
    "export c fn bad(value: String) -> Void raises {\n    raise Io\n}",
    "export c fn add(left: i32, right: i32) -> i32 {\n    return left + right\n}",
  },
  {
    "TYP023",
    "type",
    "Generic type argument mismatch",
    "A generic call provided the wrong number of type arguments, or a non-generic function was called with type arguments.",
    "Zero's first generic slice monomorphizes calls directly, so the concrete type argument list must be explicit and unambiguous.",
    "Pass one type argument for each generic parameter, or remove `<...>` from non-generic calls.",
    "identity<i32, u8>(value)",
    "identity<i32>(value)",
  },
  {
    "TYP024",
    "type",
    "Conflicting generic inference",
    "A single generic parameter was inferred as more than one concrete type.",
    "Generic inference is intentionally local: every occurrence of the same generic parameter in one call must resolve to the same type.",
    "Make the argument types match or pass explicit type arguments with compatible values.",
    "first(1, 2_u8)",
    "first(1, 2)",
  },
  {
    "TYP025",
    "type",
    "Generic type cannot be inferred",
    "A generic function call did not provide enough value context to infer every type parameter.",
    "The bootstrap compiler avoids broad inference across function bodies or return targets to keep public APIs and direct metadata predictable.",
    "Add explicit type arguments such as `identity<i32>(value)`.",
    "makeDefault()",
    "makeDefault<i32>()",
  },
  {
    "MET001",
    "meta",
    "Unsupported meta expression",
    "The compile-time evaluator rejected a `meta` expression because it was unsupported, effectful, cyclic, or outside the safety limits.",
    "Zero keeps compile-time execution sandboxed, deterministic, and bounded, so filesystem, network, process, and ambient environment access fail before code generation.",
    "Use literal arithmetic, Bool logic, target facts, or typed reflection facts within the bounded evaluator.",
    "const bad: usize = meta std.fs.host()",
    "const page: usize = meta target.pointerWidth * 64",
  },
  {
    "TYP026",
    "type",
    "Invalid type alias",
    "A type alias is duplicated, malformed, or cyclic.",
    "Aliases are graph metadata and compile-time spelling only; they must resolve to concrete existing type forms without introducing runtime identity.",
    "Rename the alias or point it at a concrete non-cyclic type.",
    "alias A = B\nalias B = A",
    "alias Bytes = Span<u8>",
  },
  {
    "TYP027",
    "type",
    "Recursive generic call changes type arguments",
    "A recursive generic function call or call cycle instantiated itself with a type argument that still references the current generic parameter.",
    "Zero monomorphizes generic calls directly, so growing recursive instantiations such as Maybe<T>, Maybe<Maybe<T>>, and deeper forms are rejected instead of expanding without a bound.",
    "Call recursively with the same generic type parameters or move the growing case into a concrete helper.",
    "grow<Maybe<T>>(nested)",
    "grow<T>(value)",
  },
  {
    "PUB001",
    "public-api",
    "Public API type required",
    "A public declaration omitted a concrete type annotation.",
    "Public surfaces must stay explicit so docs, graph JSON, and agents can repair uses without whole-body inference.",
    "Add the missing public type annotation.",
    "pub const answer = 42",
    "pub const answer: i32 = 42",
  },
  {
    "IFC001",
    "interface",
    "Interface constraint not satisfied",
    "A generic constraint references an unknown interface, or a concrete type argument does not provide a shape for static checking.",
    "Static interfaces are erased before codegen, so the checker must prove each constrained specialization from concrete shape methods.",
    "Declare the interface or pass a concrete shape that implements the required static methods.",
    "describe<i32>(1)",
    "describe<Person>(&person)",
  },
  {
    "IFC002",
    "interface",
    "Missing interface method",
    "A concrete shape used for a constrained generic call is missing a required static method.",
    "Zero does not create runtime interface values or vtables; satisfying an interface means the concrete shape already has the matching static method.",
    "Add the missing static method to the shape.",
    "type Person {\n    name: String,\n}",
    "type Person {\n    name: String,\n    fn describe(self: ref<Self>) -> String {\n        return self.name\n    }\n}",
  },
  {
    "IFC003",
    "interface",
    "Interface method arity mismatch",
    "A concrete static method has a different parameter count from the interface requirement.",
    "Static method calls are monomorphized directly, so the required signature must match before emission.",
    "Make the shape method use the same parameter count as the interface method.",
    "fn describe() -> String",
    "fn describe(self: ref<Self>) -> String",
  },
  {
    "IFC004",
    "interface",
    "Interface return type mismatch",
    "A concrete static method returns a type that does not match the interface requirement.",
    "Constrained generic bodies rely on the interface return type without runtime adaptation.",
    "Change the concrete method return type to match the interface.",
    "fn describe(self: ref<Self>) -> i32",
    "fn describe(self: ref<Self>) -> String",
  },
  {
    "IFC005",
    "interface",
    "Interface parameter type mismatch",
    "A concrete static method parameter does not match the interface requirement.",
    "Interface checks are compile-time signature checks over concrete static methods.",
    "Change the concrete method parameter type to match the interface.",
    "fn describe(self: i32) -> String",
    "fn describe(self: ref<Self>) -> String",
  },
	  {
	    "STC001",
	    "static",
	    "Unsupported static value parameter type",
	    "A static value parameter uses a type outside the concrete V1 set.",
	    "Static values are erased by monomorphization, so only integer, Bool, and enum values are accepted.",
	    "Change the static parameter type to a concrete integer, Bool, or enum type.",
	    "type Buf<static N: String> {\n    len: usize,\n}",
	    "type Buf<static N: usize> {\n    len: usize,\n}",
	  },
	  {
	    "STC002",
	    "static",
	    "Static value argument is not constant",
	    "A static value argument is not a deterministic literal, top-level const, or supported meta result.",
	    "Static value parameters are erased by monomorphization, so their values must be known before code generation.",
	    "Pass a value with the static parameter type, such as an integer literal, Bool literal, enum case, top-level const, or bounded meta result.",
	    "make<u8, runtime_len>()",
	    "make<u8, 16>()",
	  },
  {
    "STC003",
    "static",
    "Static value argument mismatch",
    "An explicit static argument conflicts with the value carried by an annotated type.",
    "Zero does not infer through whole function bodies; static values must agree at the call boundary.",
    "Use the same static value in the explicit call and the annotated value.",
    "first<u8, 8>(&vec4)",
    "first<u8, 4>(&vec4)",
  },
  {
    "MEM002",
    "memory",
    "Maybe payload read without presence proof",
    "A `Maybe<T>.value` payload read was reached without proving that the value is present.",
    "Maybe payload storage is only initialized when `.has` is true, so source must make the presence proof visible before reading `.value`.",
    "Guard the read with `.has`, or use `check`/`rescue` to handle absence explicitly.",
    "let item: Maybe<u8> = null\nlet byte: u8 = item.value",
    "if item.has {\n    let byte: u8 = item.value\n}",
  },
  {
    "MEM003",
    "memory",
    "Stack frame locals exceed the supported limit",
    "One function declares more fixed-size local storage than the per-function stack frame limit of 131072 bytes.",
    "Direct backends place fixed arrays and scalar locals in one stack frame, so a documented limit keeps frames safely inside thread stacks on every target.",
    "Split the buffer into smaller buffers in helper functions so each frame stays within the limit, or process the data in fixed-size chunks.",
    "var buffer: [262144]u8 = [0; 262144]",
    "var buffer: [65536]u8 = [0; 65536]",
  },
  {
    "FLD002",
    "shape",
    "Missing required field",
    "A shape literal omitted a field that has no default value.",
    "Shape values must be fully initialized before graph-native MIR lowering so memory layout and field reads stay deterministic.",
    "Initialize the missing shape field or add a default to the shape declaration.",
    "let item: Item = Item { id: 1 }",
    "let item: Item = Item { id: 1, name: \"zero\" }",
  },
  {
    "BOR001",
    "borrow",
    "Active lexical borrow conflict",
    "A read, assignment, or borrow conflicts with a reference that remains live until the end of its lexical scope.",
    "Zero tracks borrows lexically so agents can repair by moving code or introducing inner blocks without relying on hidden lifetime inference.",
    "End the active lexical borrow before the conflicting operation by moving the operation after the borrow scope or putting the borrow in a narrower block.",
    "let shared = &data\nupdate(&mut data)",
    "{\n  let shared = &data\n  let observed = shared.value\n}\nupdate(&mut data)",
  },
  {
    "BOR003",
    "borrow",
    "Borrow provenance analysis did not converge",
    "The borrow provenance analysis exceeded its internal work budget while summarizing a recursive call cycle, so the compiler stopped instead of spinning.",
    "Provenance summaries over recursive call cycles are memoized and bounded so the type checker always terminates; exceeding the generous budget indicates a compiler defect, not a program error.",
    "Simplify the recursive call cycle around the named function and report this compiler defect with the source program.",
    "fn deep(n: usize) -> Bool {\n    if n == 0 {\n        return true\n    }\n    let next: Bool = deep(n - 1)\n    return next\n}",
    "fn deep(n: usize) -> Bool {\n    if n == 0 {\n        return true\n    }\n    return deep(n - 1)\n}",
  },
  {
    "SHM001",
    "shape-method",
    "Generic shape method cannot be specialized",
    "A method on a generic shape was called without enough information to bind the inherited shape parameters.",
    "Generic shape methods lower to concrete C functions, so Self, type parameters, and static values must be known at the call boundary.",
    "Pass a concrete self argument or explicit method type arguments matching the shape declaration.",
    "FixedVec.init()",
    "FixedVec.init<u8, 4>()",
  },
  {
    "SHM002",
    "shape-method",
    "Shape method Self instantiation mismatch",
    "Arguments to a generic shape method imply different Self/type/static values.",
    "A specialized method has one concrete layout, so all parameters tied to Self must agree before code generation.",
    "Use one concrete shape instantiation for the method call.",
    "FixedVec.push<u8, 8>(&vec4, 1)",
    "FixedVec.push<u8, 4>(&vec4, 1)",
  },
  {
    "RCV001",
    "receiver",
    "Unknown or non-receiver method",
    "A value-style method call does not name a method with an explicit self parameter on the receiver shape.",
    "Receiver syntax is static sugar for a shape method whose first parameter is self: ref<Self> or self: mutref<Self>.",
    "Call a declared receiver method, or use namespace syntax for static methods without self.",
    "vec.capacity()",
    "FixedVec.capacity<u8, 4>()",
  },
  {
    "RCV002",
    "receiver",
    "Receiver is not addressable or mutable enough",
    "A receiver-style method call needs an addressable value, and mutating methods need a mutable receiver.",
    "The compiler lowers receiver calls by passing an explicit ref<Self> or mutref<Self> argument to a direct C function.",
    "Store the receiver in an addressable `var` binding before calling mutating methods.",
    "let vec: FixedVec<u8, 4> = FixedVec { len: 0, items: [0, 0, 0, 0] }\ncheck vec.push(1_u8)",
    "var vec: FixedVec<u8, 4> = FixedVec { len: 0, items: [0, 0, 0, 0] }\ncheck vec.push(1_u8)",
  },
  {"CIMP004", "c-import", "C import call unsupported or missing", "An extern C call names a symbol that is not declared in the imported header, or the function signature uses a C ABI type Zero cannot call directly.", "Zero validates callable C surfaces from the imported header before lowering so direct artifacts do not depend on undeclared symbols or unsupported ABI layouts.", "Call a function declared by the imported header, or wrap unsupported C ABI types behind a small scalar C shim.", "extern c \"vendor/include/ext.h\" as c\nlet value: i32 = c.missing_symbol(1, 2)", "extern c \"vendor/include/ext.h\" as c\nlet value: i32 = c.ext_add(1, 2)"},
  {
    "CIMP005",
    "c-import",
    "C import link plan missing or unsafe",
    "An extern C call needs package manifest link metadata, or the manifest contains a system library name that is unsafe to pass to the linker.",
    "Zero reports C dependency audit facts before linking so agents can inspect and repair native C surfaces deterministically.",
    "Add the imported header to `c.libs.*.headers`, then add package-relative `lib` entries or safe `link` names using only letters, digits, `_`, `-`, `.`, and `+`.",
    "extern c \"vendor/include/ext.h\" as c\nlet value: i32 = c.ext_add(1, 2)",
    "\"c\": {\"libs\": {\"ext\": {\"headers\": [\"vendor/include/ext.h\"], \"include\": [\"vendor/include\"], \"lib\": [\"vendor/lib/ext.o\"], \"link\": []}}}",
  },
  {"BLD004", "build", "Backend target not buildable", "The selected backend cannot build this source, target, object format, architecture, or artifact kind.", "Target-aware buildability runs before object or executable emission and reports backend limitations with structured blocker facts.", "Choose a target whose `zero targets --json` backend facts advertise the requested artifact, or simplify the program to the backend-supported subset.", "zero check --json --emit obj --target linux-arm64 examples/direct-call-add.graph", "zero check --json --emit obj --target linux-x64 examples/direct-call-add.graph"},
  {"CGEN004", "codegen", "Direct code generation invariant failed", "A direct emitter reached an internal code generation invariant after target buildability accepted the program.", "Ordinary unsupported targets and source features should be reported before emission with BLD004.", "Report this compiler bug with the source program and target that produced it.", "zero build --json --emit obj --target linux-musl-x64 examples/direct-call-add.graph", "zero build --json --emit obj --target linux-x64 examples/direct-call-add.graph"},
  {"APP001", "app", "Missing main function", "An executable build needs an entry point but the program declares no main function and no test blocks.", "Executable targets resolve one deterministic `pub fn main` entry point from the program graph.", "Add `pub fn main() -> Void` (optionally taking `world: World`), or build a library target instead.", "fn helper() -> i32 {\n    return 1\n}", "pub fn main() -> Void {\n    return\n}"},
  {"BLD002", "build", "Build input or build stage failed", "The command could not load or build its input: the path is not a Zero source file, package, or graph store, or an internal build stage such as graph construction or canonical rendering failed.", "Compiler commands resolve a concrete graph or package input before any emission stage runs, so unusable input fails early with the failing stage named.", "Point the command at a package root, zero.toml, zero.graph, or .0 source file, and run `zero check` on the package to see the underlying source diagnostics.", "zero build does-not-exist.0", "zero build examples/hello.graph"},
  {"NAM002", "name", "Duplicate declaration", "A parameter, local binding, or declaration reuses a name that is already declared in the same scope.", "Zero rejects shadowing so graph identities, reads, and patches stay unambiguous.", "Rename one of the conflicting declarations.", "fn add(value: i32, value: i32) -> i32 {\n    return value\n}", "fn add(left: i32, right: i32) -> i32 {\n    return left + right\n}"},
  {"NAM003", "name", "Unknown name", "An identifier, type name, or CLI input such as a diagnostic code does not resolve to any visible declaration.", "Zero resolves every name against explicit declarations and fails with close matches instead of guessing.", "Declare the name, import the module that provides it, or fix the spelling using the close matches in the diagnostic.", "let total: i32 = cuont", "let total: i32 = count"},
  {"NAM004", "name", "Wrong argument count or duplicate field", "A call passes the wrong number of arguments, or a shape literal initializes the same field twice.", "Call arity and shape field lists are fixed by the declaration, so mismatches fail instead of filling defaults.", "Match the declared parameter list exactly and initialize each shape field once.", "check world.out.write(\"a\", \"b\")", "check world.out.write(\"ab\")"},
  {"TYP001", "type", "Invalid call", "A call target is not a function, or an argument type does not match the declared parameter type.", "Calls resolve statically against graph signatures and no implicit conversions are applied.", "Call a declared function and pass each argument with the exact declared parameter type.", "let n: i32 = add(1, \"two\")", "let n: i32 = add(1, 2)"},
  {"TYP002", "type", "Type mismatch", "Two sides of a binding, assignment, operator, comparison, or literal disagree about the concrete type.", "Zero never converts implicitly, so every typed position must already agree on one concrete type.", "Change one side so the types match, or cast explicitly for numeric conversions.", "let n: i32 = 1_u8", "let n: i32 = 1"},
  {"TYP003", "type", "Return type mismatch", "A function returns a value whose type does not match the declared return type, or a non-void function misses a return on some path.", "The declared return type is part of the function contract that callers and the graph rely on.", "Return a value of the declared type on every path, or change the return annotation.", "fn one() -> i32 {\n    return \"one\"\n}", "fn one() -> i32 {\n    return 1\n}"},
  {"TYP005", "type", "Unknown shape literal type", "A shape literal names a type that is not a declared shape.", "Shape literals are typed by an existing shape declaration, not inferred structurally.", "Declare the shape or fix the literal's type name.", "let p: Pointt = Pointt { x: 1, y: 2 }", "let p: Point = Point { x: 1, y: 2 }"},
  {"TYP010", "type", "Condition must be Bool", "An if, while, or loop condition is not a Bool expression.", "Zero has no truthiness; only Bool values select control flow.", "Compare explicitly or use a Bool expression as the condition.", "if count {\n    return\n}", "if count > 0 {\n    return\n}"},
  {"TYP011", "type", "null requires a Maybe context", "`null` was used where the compiler cannot see an explicit Maybe<T> type.", "`null` is only the absent case of Maybe<T>, so the payload type must be visible at the use site.", "Annotate the binding, parameter, or return type as Maybe<T>.", "let value = null", "let value: Maybe<i32> = null"},
  {"TYP012", "type", "break outside a loop", "A break statement appears outside any enclosing loop.", "break is defined only as an exit from the innermost enclosing loop.", "Move the break inside a loop or use return to leave the function.", "fn f() -> Void {\n    break\n}", "fn f() -> Void {\n    while true {\n        break\n    }\n}"},
  {"TYP013", "type", "continue outside a loop", "A continue statement appears outside any enclosing loop.", "continue is defined only as a jump to the next iteration of the innermost enclosing loop.", "Move the continue inside a loop.", "fn f() -> Void {\n    continue\n}", "fn f() -> Void {\n    for i in 0..3 {\n        continue\n    }\n}"},
  {"TYP014", "type", "Range loop bounds must be integers", "A `for x in a..b` loop has a non-integer bound or bounds with mismatched integer types.", "Range loops lower to integer counters, so both bounds must share one integer type.", "Use integer bounds with the same type on both sides of `..`.", "for i in 0..total_f32 {\n}", "for i in 0..total {\n}"},
  {"TYP015", "type", "Malformed integer literal", "An integer literal has invalid digits, separators, or suffix for its base.", "Literals are parsed exactly; malformed digits fail instead of truncating.", "Fix the literal digits, separator placement, or type suffix.", "let n: i32 = 1__0", "let n: i32 = 1_000"},
  {"TYP016", "type", "Integer literal out of range", "An integer literal does not fit the expected integer type.", "Literal values are range-checked against the concrete type instead of wrapping silently.", "Use a value within the type's range or a wider integer type.", "let b: u8 = 300_u8", "let b: u16 = 300_u16"},
  {"TYP017", "type", "Invalid cast", "A cast was applied between types that are not both primitive numeric or byte/char types.", "Casts are explicit numeric conversions only; other type changes need real constructors or parsing.", "Cast only between primitive numeric or byte/char types, or convert through an explicit helper.", "let n: i32 = text as i32", "let parsed: Maybe<i32> = std.parse.parseI32(text)"},
  {"TYP019", "type", "Malformed float literal", "A float literal has invalid digits, separators, or suffix.", "Literals are parsed exactly; malformed float text fails instead of rounding.", "Fix the float literal digits or suffix.", "let x: f64 = 1..5", "let x: f64 = 1.5"},
  {"TYP020", "type", "Float literal out of range", "A float literal does not fit the expected float type.", "Literal values are range-checked against the concrete float type.", "Use a value within the float type's range or a wider float type.", "let x: f32 = 1e40_f32", "let x: f64 = 1e40"},
  {"TYP021", "type", "Invalid member access", "A member access was applied to a value that is not a shape, Maybe value, enum case, or choice case.", "Member access is resolved statically from the declared type's fields and cases.", "Access members only on shapes, Maybe values (`.has`, `.value`), enums, or choices.", "let n: i32 = 42\nlet h: Bool = n.has", "let m: Maybe<i32> = std.parse.parseI32(\"42\")\nlet h: Bool = m.has"},
  {"TYP022", "type", "Index must be an integer", "An array or span index expression is not an integer.", "Indexing lowers to bounds-checked integer offsets.", "Use an integer index expression.", "let item: u8 = bytes[\"0\"]", "let item: u8 = bytes[0]"},
  {"STD002", "stdlib", "Unknown standard-library helper", "A `std.<module>.<helper>` call names a helper this compiler does not provide, or calls a known helper with the wrong number of arguments.", "The stdlib surface is an explicit catalog per compiler build, so unknown helpers fail instead of resolving dynamically.", "Load `zero skills get stdlib` for the full signature catalog and call a documented helper with its exact signature.", "let ok: Bool = std.time.validateRfc3339(text)", "let ok: Bool = std.time.isRfc3339DateTime(text)"},
  {"OWN001", "ownership", "Owned value moved or duplicated", "An owned value was used after being moved, or a construct such as array repeat would duplicate owned values.", "Ownership transfers are tracked statically so destructors run exactly once.", "Keep ownership in one binding until the final use, or borrow with `ref`/`mutref` instead of moving.", "let a: owned<File> = file\nlet b: owned<File> = file", "let a: owned<File> = file\nuseFile(&a)"},
  {"OWN002", "ownership", "Invalid drop method", "A drop method does not use the canonical signature, returns a value, or raises.", "Drop runs implicitly at scope exit, so it must be exactly `fn drop(self: mutref<Self>) -> Void` and non-raising.", "Use the canonical non-raising drop signature: `fn drop(self: mutref<Self>) -> Void`.", "fn drop(self: mutref<Self>) -> Void raises {\n    raise Io\n}", "fn drop(self: mutref<Self>) -> Void {\n    return\n}"},
  {"MEM001", "memory", "Malformed memory or container type", "A memory or container type is missing a type argument or uses a malformed fixed array form.", "Container types are explicit: Maybe<T>, Span<T>, and [N]T must name their element types and lengths.", "Add the missing type argument, for example Maybe<u8>, Span<const u8>, or [4]u8.", "let value: Maybe = null", "let value: Maybe<u8> = null"},
  {"BOR002", "borrow", "Reference outlives its source", "A reference derived from a local binding or call argument would escape through a return or a longer-lived binding.", "References must not outlive the storage they point into, and Zero checks this lexically.", "Return an owned value, or keep references to local storage inside the current function.", "fn first(items: [4]u8) -> ref<u8> {\n    return &items[0]\n}", "fn first(items: [4]u8) -> u8 {\n    return items[0]\n}"},
  {"STC001", "static", "Unsupported static value parameter type", "A static value parameter uses a type other than a concrete integer, Bool, or enum type.", "Static value parameters are compile-time facts that monomorphization must compare and hash deterministically.", "Use a concrete integer, Bool, or enum type for static value parameters.", "type Buf<static N: String> {\n    len: usize,\n}", "type Buf<static N: usize> {\n    len: usize,\n}"},
  {"STC002", "static", "Static value argument not constant", "A static value argument is not a deterministic compile-time value.", "Static arguments specialize types and functions at compile time, so runtime values cannot flow into them.", "Pass a literal, top-level const, or supported meta value with the static parameter's type.", "let buf: Buf<runtime_len> = makeBuf()", "const LEN: usize = 16\nlet buf: Buf<LEN> = makeBuf()"},
  {"FLD001", "field", "Unknown member", "A member access names a field or member the value's type does not declare.", "Members resolve statically from the declared shape fields or the fixed Maybe members `.has` and `.value`.", "Use a declared field name; for Maybe values use `.has` and `.value`.", "if parsed.exists {\n}", "if parsed.has {\n}"},
  {"VAR001", "variant", "Unknown enum case", "An enum expression names a case the enum does not declare.", "Enum cases are a closed set from the declaration.", "Use a declared case name or add the case to the enum.", "let c: Color = Color.Purpel", "let c: Color = Color.Purple"},
  {"VAR002", "variant", "Unknown choice case", "A choice expression or match arm names a case the choice does not declare.", "Choice cases are a closed set from the declaration.", "Use a declared case name or add the case to the choice.", "let r: Result = Result.Okk(1)", "let r: Result = Result.Ok(1)"},
  {"VAR003", "variant", "Choice payload arity mismatch", "A choice case was constructed or matched with the wrong number of payload values.", "Each choice case declares a fixed payload list that constructors and match arms must bind exactly.", "Pass or bind exactly the declared payload values for the case.", "let r: Pair = Pair.Both(1)", "let r: Pair = Pair.Both(1, 2)"},
  {"VAR004", "variant", "Choice payload type mismatch", "A choice case payload value or binding does not match the declared payload type.", "Choice payloads are typed by the declaration, with no implicit conversions.", "Pass payload values with the declared payload types.", "let r: Wrapped = Wrapped.Value(\"1\")", "let r: Wrapped = Wrapped.Value(1)"},
  {"MAT001", "match", "Invalid match subject", "A match subject is not an enum, choice, Bool, or integer value.", "Match lowers to exhaustive case dispatch over a closed set of values.", "Match on an enum, choice, Bool, or integer, or use if/else chains for other types.", "match name {\n}", "match color {\n    Color.Red => 1,\n}"},
  {"MAT002", "match", "Non-exhaustive match", "A match does not cover every case of its subject and has no fallback arm.", "Exhaustiveness is checked statically so new cases fail at compile time instead of trapping at runtime.", "Add the missing case arms or a `_` fallback arm.", "match flag {\n    true => 1,\n}", "match flag {\n    true => 1,\n    false => 0,\n}"},
  {"MAT003", "match", "Duplicate match arm", "A match repeats a case arm or declares more than one fallback arm.", "Each case and the fallback may appear once so arm selection is unambiguous.", "Remove or merge the duplicate arm.", "match flag {\n    _ => 1,\n    _ => 0,\n}", "match flag {\n    true => 1,\n    _ => 0,\n}"},
  {"MAT004", "match", "Scalar match arm cannot bind a payload", "A match arm over a Bool, integer, or enum subject tries to bind a payload value.", "Only choice cases carry payloads; scalar and enum cases have none to bind.", "Remove the payload binding from the scalar arm.", "match flag {\n    true(x) => x,\n}", "match flag {\n    true => 1,\n    false => 0,\n}"},
  {"MAT005", "match", "Match guard must be Bool", "A match arm guard expression is not a Bool.", "Guards select arms, so they must be Bool like every other condition.", "Use a Bool guard expression.", "match n {\n    value if value => 1,\n}", "match n {\n    value if value > 0 => 1,\n    _ => 0,\n}"},
  {"IMP001", "import", "Unknown package-local import", "A `use` names a package-local module with no matching source file.", "Package-local imports resolve to checked-in module source files, not search paths.", "Create the module source file in the package or remove the import.", "use helpers_missing", "use helpers"},
  {"IMP002", "import", "Import cycle", "Package-local modules import each other in a cycle.", "Module initialization and graph construction need an acyclic import order.", "Move shared declarations into a third module or remove one import edge.", "use b (from a.0)\nuse a (from b.0)", "use shared (from a.0)\nuse shared (from b.0)"},
  {"IMP003", "import", "Duplicate public symbol", "Two imported modules export the same public symbol name.", "Public symbols form one flat package namespace, so collisions are rejected instead of shadowed.", "Rename one symbol or keep it private inside its module.", "pub fn parse() in two imported modules", "pub fn parseHeader() and pub fn parseBody()"},
  {"CIMP001", "c-import", "C header could not be read", "An extern C import names a header file that could not be opened or read.", "Imported C surfaces come from checked-in package-relative headers so builds stay reproducible.", "Vendor the header inside the package and point the extern C import at its package-relative path.", "extern c \"missing/ext.h\" as c", "extern c \"vendor/include/ext.h\" as c"},
  {"CIMP002", "c-import", "Unsupported C header surface", "The imported C header uses declarations the bootstrap C importer cannot model.", "Zero validates the whole imported surface up front so direct artifacts never depend on misread C declarations.", "Reduce the header to the supported scalar function surface, or wrap complex C APIs behind a small shim header.", "extern c \"vendor/include/complex_macros.h\" as c", "extern c \"vendor/include/ext.h\" as c"},
  {"CIMP003", "c-import", "C dependency would use host discovery", "A foreign-target build would need host include paths, host libraries, or pkg-config to satisfy a C dependency.", "Cross builds must not inherit host C toolchain state, so host discovery is rejected for foreign targets.", "Use package-relative vendored headers and libraries, or set the target sysroot explicitly.", "zero build --target linux-musl-x64 with a host-discovered C lib", "vendor the lib under the package and list it in c.libs"},
  {"PKG001", "package", "Package dependency missing", "A local dependency path does not contain a zero.toml or compatibility zero.json manifest.", "Dependencies are explicit checked-in packages, not registry lookups.", "Create the dependency package or update the dependency path in the manifest.", "[dependencies]\nhelpers = { path = \"../missing\" }", "[dependencies]\nhelpers = { path = \"../helpers\" }"},
  {"PKG002", "package", "Package dependency cycle", "Packages depend on each other in a cycle.", "Package builds need an acyclic dependency order.", "Move shared code into a third package or remove one dependency edge.", "a depends on b, b depends on a", "a and b both depend on shared"},
  {"PKG003", "package", "Package version conflict", "One package name resolves to conflicting versions in the dependency graph.", "A build uses exactly one version of each package name so graph identities stay stable.", "Update the requested versions so the graph resolves to one version per package.", "helpers = 0.1.0 and helpers = 0.2.0 in one graph", "helpers = 0.2.0 everywhere"},
  {"PKG004", "package", "Package target unsupported", "A dependency does not support the selected build target.", "Target support is part of the package contract, checked before compilation.", "Select a target the dependency supports, or gate the dependency behind a compatible target.", "zero build --target win32-x64.exe with a posix-only dependency", "zero build --target linux-musl-x64 with a posix-only dependency"},
  {"PAR100", "parse", "Parse or input error", "The input could not be parsed as Zero source, a manifest, or a recognized graph input; the span points at the first unparseable token.", "PAR100 is the catch-all parser code, so the message and span carry the specific failure.", "Repair the syntax at the reported span, then rerun zero check.", "pub fn main() -> Void {", "pub fn main() -> Void {\n    return\n}"},
  {"GPH000", "graph-patch", "Graph patch failed", "A patch operation failed without a more specific patch code; the message carries the failing fact.", "Patch failures leave the store unchanged, so the graph is never saved in a half-applied state.", "Read the failure message, fix the operation, and reapply; `zero patch --op help` lists every operation shape.", "zero patch --op 'replace'", "zero patch --op help # copy a working operation shape"},
  {"GPH001", "graph-patch", "Malformed patch operation", "A patch operation or body row is missing required attributes or does not parse.", "Patch operations have fixed shapes so they validate fully before touching the store.", "Run `zero patch --op help` and copy the exact shape of the operation, including required attributes and `end` markers.", "zero patch --op 'addFunction'", "zero patch --op 'addFunction name=double param=value:i32 returns=i32'"},
  {"GPH002", "graph-patch", "Patch precondition failed", "An expect operation's graph hash or fact does not match the current store.", "Preconditions let agents assert the graph state they reasoned about before mutating it.", "Re-read the current facts with `zero query` or `zero status`, then update the expect operation to the current graph hash.", "expect graphHash=stale-hash", "zero status . # read the current graph hash, then patch"},
  {"GPH003", "graph-patch", "Invalid patch operand", "A patch operation operand is not a valid identifier, operator, or value for its slot.", "Operands are validated against the projection grammar before any graph mutation.", "Use canonical projection syntax for operands, the same text `zero view` prints.", "addLetBinary name=x op=plus left=1 right=2", "addLetBinary name=x op=+ left=a right=b"},
  {"GPH004", "graph-patch", "Patch target not found", "A patch operation names a node, edge, function, or parent that does not exist in the graph.", "Patches address graph handles directly, so missing targets fail instead of creating implicit nodes.", "Locate the handle first with `zero query --fn <name> --handles` (stmt and param handles) or `zero query --find <text>` and use the exact node id or name it prints.", "zero patch --op 'replaceFunctionBody missing_fn ...'", "zero query --fn main --handles # then patch the printed handle"},
  {"GPH005", "graph-patch", "Patch conflicts with existing graph facts", "A patch would create a node id, function, or edge slot that already exists, or delete a node that is still referenced outside its subtree.", "The store stays internally consistent, so conflicting inserts and dangling references are rejected.", "Query the existing fact first, then rename, replace, or delete the conflicting fact explicitly in the same patch.", "addFunction name=main # main already exists", "replaceFunctionBody main ... # edit the existing function"},
  {"GPH006", "graph-patch", "Patch produced an invalid graph", "The patched graph failed validation or could not lower, so the patch was rolled back and the store was not saved.", "Every patch validates the whole resulting graph before saving, keeping the store loadable by every command.", "Fix the patch body so the resulting function and graph are complete and well-formed; the message names the failing validation fact.", "replaceFunctionBody main with an unterminated block", "replaceFunctionBody main\n  return\nend"},
  {"GRC000", "graph-reconcile", "Graph reconcile failed", "Reconciling edited source with the previous graph failed without a more specific code; the message carries the failing fact.", "Reconcile preserves node identities across text edits so graph history and patches stay stable.", "Read the failure message; if identity cannot be preserved, split the edit into smaller passes or use zero patch.", "zero reconcile zero.graph --source heavily-rewritten.0", "zero reconcile zero.graph --source small-edit.0"},
  {"GRC001", "graph-reconcile", "Ambiguous source identity", "An edited declaration matches zero or several previous graph nodes, so node identity cannot be preserved deterministically.", "Reconcile refuses to guess which node an edit refers to, because wrong guesses silently rewrite graph history.", "Split the text edit into smaller passes, or make the change with `zero patch` so identity is explicit.", "rename two similar functions in one text edit", "rename one function per import, or zero patch --op 'rename ...'"},
  {"GRC002", "graph-reconcile", "Node count differs", "The compared graphs contain different numbers of nodes.", "Graph comparison reports the first structural difference as a typed fact.", "Inspect both graphs with `zero query --full` and reconcile or re-import the out-of-date side.", "zero reconcile stale-zero.graph --source main.0", "zero import . # rebuild the store, then reconcile"},
  {"GRC003", "graph-reconcile", "Node kind or module identity differs", "A compared node has a different kind, or the edited source declares a different module identity.", "Node kind and module identity anchor graph comparison, so mismatches are reported before field-level diffs.", "Compare both sides with `zero diff` and align the module identity or node kind before reconciling.", "zero reconcile zero.graph --source other-package/main.0", "zero reconcile zero.graph --source main.0"},
  {"GRC004", "graph-reconcile", "Node semantic field differs", "A compared node differs in a semantic field such as name, type, or value.", "Semantic field diffs are reported as typed facts naming the node and field.", "Inspect the named node on both sides with `zero query --node <id>` and align the field.", "zero reconcile with diverged stores", "zero query --node fn-main # inspect, then align"},
  {"GRC005", "graph-reconcile", "Node flag differs", "A compared node differs in a flag such as visibility or fallibility.", "Flags are part of node identity facts, so comparison reports them explicitly.", "Inspect the named node on both sides and align the flag.", "pub fn main on one side, fn main on the other", "pub fn main on both sides"},
  {"GRC006", "graph-reconcile", "Edge count differs", "The compared graphs contain different numbers of edges.", "Edge counts anchor structural comparison before per-edge diffs.", "Inspect both graphs with `zero query --full` and reconcile or re-import the out-of-date side.", "zero reconcile stale-zero.graph --source main.0", "zero import . # rebuild the store, then reconcile"},
  {"GRC007", "graph-reconcile", "Edge semantic fact differs", "A compared edge differs in kind, endpoints, or order.", "Edge facts carry program structure, so comparison names the differing edge.", "Inspect the named edge endpoints on both sides and align the structure.", "zero reconcile with diverged stores", "zero query --node <edge-source> # inspect, then align"},
  {"GRC012", "graph-reconcile", "Schema version differs", "The compared graphs use different store schema versions.", "Stores written by different compiler builds can carry different schemas, which comparison rejects up front.", "Rebuild the older store with this binary via `zero import .`, or compare stores written by the same build.", "zero reconcile old-build-zero.graph --source main.0", "zero import . # rewrite the store with this build"},
  {"GRC013", "graph-reconcile", "Module identity differs", "The compared graphs describe different modules.", "Comparison only relates graphs of the same module identity.", "Compare stores of the same package, or update package.name so identities match.", "zero reconcile other-package.graph --source main.0", "zero reconcile zero.graph --source main.0"},
  {"GRF000", "graph-validate", "Graph validation failed", "The program graph failed validation without a more specific code; the message carries the failing fact.", "Every load, patch, and import validates the whole graph so commands never operate on broken structure.", "Read the failing fact; if the store cannot be repaired, rebuild it from source with `zero import .`.", "zero validate corrupted-zero.graph", "zero import . # rebuild the store from source"},
  {"GRF001", "graph-validate", "Program graph missing", "A command needed a program graph but none was loaded or built.", "Graph-backed commands require a resolvable graph input.", "Point the command at a package root, zero.graph, or .0 source file.", "zero validate", "zero validate examples/memory-package"},
  {"GRF002", "graph-validate", "Node missing identity", "A graph node has no id.", "Every node carries a stable id used by patches, queries, and provenance.", "Rebuild the store from source with `zero import .`; hand-edited stores must keep every node id.", "node row with an empty id in a hand-edited store", "zero import . # regenerate ids from source"},
  {"GRF003", "graph-validate", "Duplicate node id", "Two graph nodes share the same id.", "Node ids are unique handles; duplicates would make patches ambiguous.", "Rebuild the store from source with `zero import .`, or remove the duplicated row from a hand-edited store.", "two node rows with id fn-main", "zero import . # regenerate unique ids"},
  {"GRF004", "graph-validate", "Edge source missing", "An edge references a source node id that does not exist.", "Edges must connect existing nodes so traversal never dangles.", "Rebuild the store from source with `zero import .`, or fix the edge row to reference an existing node.", "edge from deleted-node to fn-main", "zero import . # regenerate consistent edges"},
  {"GRF005", "graph-validate", "Edge target outside declared domain", "An edge references a target that is missing from its declared domain.", "Edge targets resolve within explicit domains (nodes, types, symbols) so references stay checkable.", "Rebuild the store from source with `zero import .`, or fix the edge target to an existing domain entry.", "edge to type:MissingType", "zero import . # regenerate consistent edges"},
  {"GRF006", "graph-validate", "Edge endpoint invalid", "An edge endpoint fact is invalid for the graph; the message names the offending edge.", "Edge endpoint facts are validated against the node table on every load.", "Rebuild the store from source with `zero import .`.", "hand-edited edge row with a bad endpoint", "zero import . # regenerate consistent edges"},
  {"GRF007", "graph-validate", "Node missing content hash", "A graph node has no content hash.", "Content hashes let stores verify node integrity and let merge detect real changes.", "Rebuild the store from source with `zero import .`; hand-edited stores must keep node hashes.", "node row without a hash field", "zero import . # regenerate node hashes"},
  {"GRF008", "graph-validate", "Edge target domain invalid", "An edge declares a target domain the schema does not define.", "Edge domains are a closed schema set so traversal code can dispatch on them.", "Rebuild the store from source with `zero import .`, or fix the edge's domain field.", "edge with target domain 'unknown'", "zero import . # regenerate schema-valid edges"},
  {"GRF009", "graph-validate", "Graph missing module identity", "The program graph carries no module identity.", "Module identity binds the store to its package and is required for import, merge, and status.", "Rebuild the store from source with `zero import .` in a package with package.name set.", "store with an empty moduleIdentity", "zero import . # regenerate module identity"},
  {"GRF010", "graph-validate", "Node identity malformed", "A node id does not match the required identity format.", "Node ids follow a stable format so handles stay parseable and orderable.", "Rebuild the store from source with `zero import .`; do not hand-edit node ids.", "node id with spaces or illegal characters", "zero import . # regenerate well-formed ids"},
  {"GRF011", "graph-validate", "Node or edge kind invalid", "A node or edge declares a kind the schema does not define.", "Kinds are a closed schema set per compiler build, so unknown kinds usually mean a build mismatch.", "Rebuild the store with this binary via `zero import .`, or install the compiler build that wrote the store.", "store written by a newer zero build", "zero import . # rewrite the store with this build"},
  {"GRF012", "graph-validate", "Edge child kind invalid for source node", "An edge connects a parent node to a child kind that is not legal for the parent's kind.", "The graph grammar restricts which child kinds each node kind may own.", "Rebuild the store from source with `zero import .`, or fix the patch that created the illegal edge.", "function node owning a parameter of kind module", "zero import . # regenerate a grammar-valid graph"},
  {"GRF013", "graph-validate", "Ordered edge group sparse", "An ordered edge group skips an order slot.", "Ordered children use dense order indexes so body rows have a deterministic sequence.", "Rebuild the store from source with `zero import .`, or renumber the patched edge orders densely from zero.", "body edges with orders 0 and 2", "body edges with orders 0 and 1"},
  {"GRF014", "graph-validate", "Node missing required payload", "A node is missing a payload its kind requires, such as a name, value, or type.", "Each node kind declares required payload fields the projection and emitters rely on.", "Rebuild the store from source with `zero import .`, or add the missing payload in the patch that created the node.", "let node without a type payload", "zero import . # regenerate complete nodes"},
  {"GRF015", "graph-validate", "Node carries illegal payload", "A node carries a payload its kind forbids, such as a name on a literal node.", "Payload rules keep node kinds unambiguous for projection and lowering.", "Rebuild the store from source with `zero import .`, or remove the illegal payload from the patch.", "literal node with a name payload", "zero import . # regenerate well-formed nodes"},
  {"GRF016", "graph-validate", "Node edge shape invalid", "A node's edges do not match its kind's required shape, such as a call node missing its callee edge.", "Each node kind declares the edge shape lowering depends on, validated before any emission.", "Rebuild the store from source with `zero import .`, or complete the node's edges in the patch that created it.", "call node without a callee edge", "zero import . # regenerate complete call nodes"},
  {"RGM000", "graph-merge", "Repository graph merge failed", "The merge failed without a more specific code; the message carries the failing fact.", "Merge failures leave the target store unchanged.", "Read the failure message and rerun `zero merge` after repairing the named input.", "zero merge --base bad.graph --left l.graph --right r.graph .", "zero merge --base base.graph --left l.graph --right r.graph ."},
  {"RGM001", "graph-merge", "Merge input missing or incompatible", "A merge input store is missing, unreadable, or differs in schema version or module identity.", "Three-way merge needs base, left, and right stores of the same module written by compatible builds.", "Check each input path, rebuild older stores with this binary via `zero import .`, and merge stores of the same package.", "zero merge --base other-module.graph --left l.graph --right r.graph .", "zero merge --base base.graph --left l.graph --right r.graph ."},
  {"RGM002", "graph-merge", "Node changed on both sides", "Both sides changed the same graph node, or its source path, since the base store.", "Conflicting node edits need a human or agent decision; merge never picks a side silently.", "Resolve the conflict by editing one side to match the intended result, then rerun zero merge.", "both sides edit fn main differently", "apply one side's edit, re-export, then merge"},
  {"RGM003", "graph-merge", "Same node id inserted with different content", "Both sides inserted the same node id with different content.", "Node ids must converge to one content during merge.", "Rename or re-create one side's node so ids no longer collide, then rerun zero merge.", "both sides add fn helper with different bodies", "rename one helper before merging"},
  {"RGM004", "graph-merge", "Edge changed on both sides", "Both sides changed the same edge or edge order slot since the base store.", "Conflicting structural edits need an explicit resolution.", "Resolve the conflict by editing one side's structure, then rerun zero merge.", "both sides reorder the same function body", "apply one side's order, then merge"},
  {"RGM005", "graph-merge", "Source projection conflict", "Both sides changed the same source projection, or a projection no longer matches its merged graph.", "Merged stores must keep projections and graph content consistent.", "Resolve the projection conflict by re-exporting one side, then rerun zero merge.", "both sides edit main.0 differently", "zero export on the chosen side, then merge"},
  {"RGM006", "graph-merge", "Merged graph failed validation", "The merged result failed whole-graph validation, so no store was written.", "Merge only writes stores every command can load.", "Inspect the named validation fact, repair the conflicting side, and rerun zero merge.", "merge producing a dangling edge", "repair the side that deleted the node, then merge"},
  {"RGM007", "graph-merge", "Merge could not regenerate source projection", "The merged graph could not regenerate a matching .0 source projection.", "Merged stores ship with projections that match their graph content exactly.", "Re-export the projections on the conflicting side with `zero export`, then rerun zero merge.", "merge with stale checked-in projections", "zero export . # refresh projections, then merge"},
  {"RGP001", "repository-graph", "Repository graph store missing", "The package has no checked-in zero.graph repository graph store.", "Package commands compile from zero.graph; .0 files are projections of it.", "Run `zero import .` to create the repository graph store from the package source.", "zero status . # in a package without zero.graph", "zero import . # create zero.graph, then zero status ."},
  {"RGP002", "repository-graph", "Import or export direction required", "Source text and zero.graph disagree, and the command cannot decide which side is authoritative.", "Choosing a direction silently could discard edits on the other side.", "Run `zero import` if the .0 source text is authoritative, or `zero export` if zero.graph is authoritative.", "zero export . # while main.0 has unimported edits", "zero import . # make the edited source authoritative"},
  {"RGP003", "repository-graph", "Repository graph store invalid", "The zero.graph store exists but could not be parsed or failed validation.", "Commands refuse to guess around a broken store so package state stays deterministic.", "Run `zero import .` after reviewing the source projection to rebuild the store from source.", "zero check . # with a corrupted zero.graph", "zero import . # rebuild zero.graph from source"},
  {"RGP004", "repository-graph", "Source projection generation failed", "The store's .0 source projection could not be generated, written, or kept byte-stable.", "Projections are deterministic renderings of the graph; failures usually mean a broken projection table or unwritable target path.", "Fix the projection target path, or rebuild the store from source with `zero import .` and re-export.", "zero export . # with a non-local projection path in the store", "zero import . && zero export ."},
  {"RGP006", "repository-graph", "Source projection missing or differs", "A checked-in .0 source projection is missing or no longer matches the repository graph.", "Projection drift means source text and graph describe different programs.", "Review the diff, then run `zero import` if the .0 projection is authoritative or `zero export` if zero.graph is authoritative.", "zero verify-projection . # after editing main.0 by hand", "zero import . # accept the edited projection"},
  {"RGP007", "repository-graph", "Source identity ambiguous or mismatched", "An import or status check could not map source declarations onto existing graph identities, or the package manifest identity does not match the store.", "Import resolves most overlaps by structural similarity and accepts a single-file rewrite whose function set still matches, regenerating that file's identities; ties fail only when they span files, change the function set, or the package identity mismatches.", "Split the cross-file edit into one-file passes or use `zero patch`; for identity mismatches, align package.name with the store or re-import.", "zero import . # after tied edits across two files", "zero import . # after one focused rename"},
  {"RGP008", "repository-graph", "Stale package projection", "Package source was edited after the store was written and ZERO_STALE=fail turns that staleness into an error.", "Strict staleness mode lets automation require an explicit refresh instead of an implicit one.", "Run `zero import .` to refresh the store, or unset ZERO_STALE to let commands refresh automatically.", "ZERO_STALE=fail zero check . # after editing main.0", "zero import . && ZERO_STALE=fail zero check ."},
  {"RGP009", "repository-graph", "Binary store unreadable by this compiler", "The binary zero.graph store has the right magic but this compiler build cannot decode it, or its content failed integrity checks.", "Binary store layout can change between zero builds, so a store written by a different build fails to load instead of being misread.", "The store was likely written by a different zero build: rebuild it with this binary via `zero import .`, or install the matching compiler. Check builds with `zero --version`.", "zero query zero.graph # store written by another zero build", "zero import . # rewrite zero.graph with this binary"},
  {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
};

static const ExplainInfo *find_explain_info(const char *code) {
  if (!code) return NULL;
  for (size_t i = 0; explain_infos[i].code; i++) {
    if (strcmp(explain_infos[i].code, code) == 0) return &explain_infos[i];
  }
  return NULL;
}

typedef struct {
  const char *code;
  int numeric;
} ExplainRepairNumeric;

static int explain_repair_numeric(const char *code) {
  static const ExplainRepairNumeric pairs[] = {
    {"PAR100", 100},
    {"ERR001", 1001}, {"ERR002", 1002}, {"ERR003", 1003},
    {"RGP009", 1004}, {"RGP003", 1005}, {"RGP007", 1006}, {"RGP004", 1008}, {"RGP006", 1009}, {"RGM002", 1010},
    {"APP001", 2001}, {"BLD002", 2002}, {"BLD003", 2003}, {"BLD004", 2004},
    {"NAM002", 3002}, {"NAM003", 3003}, {"NAM004", 3004},
    {"TYP001", 3005}, {"TYP002", 3006}, {"TYP003", 3007}, {"TYP005", 3009}, {"TYP009", 3010},
    {"STD002", 3011}, {"STD003", 3012},
    {"OWN001", 3013}, {"OWN002", 3014}, {"MEM001", 3015},
    {"TYP010", 3016}, {"TYP011", 3017}, {"TYP012", 3018}, {"TYP013", 3019}, {"TYP014", 3020},
    {"TYP015", 3021}, {"TYP016", 3022}, {"TYP017", 3023}, {"TYP019", 3025}, {"TYP020", 3026},
    {"TYP021", 3027}, {"TYP022", 3028},
    {"BOR001", 3029}, {"BOR002", 3030}, {"ABI001", 3031},
    {"TYP023", 3032}, {"TYP024", 3033}, {"TYP025", 3034}, {"MET001", 3035}, {"TYP026", 3036},
    {"PUB001", 3037},
    {"IFC001", 3038}, {"IFC002", 3039}, {"IFC003", 3040}, {"IFC004", 3041}, {"IFC005", 3042},
    {"STC001", 3043}, {"STC002", 3044}, {"STC003", 3045},
    {"SHM001", 3046}, {"SHM002", 3047},
    {"RCV001", 3048}, {"RCV002", 3049},
    {"TYP027", 3050}, {"MEM002", 3051}, {"MEM003", 3052}, {"BOR003", 3053},
    {"FLD001", 3101}, {"FLD002", 3102},
    {"VAR001", 3103}, {"VAR002", 3104}, {"VAR003", 3108}, {"VAR004", 3109},
    {"MAT001", 3105}, {"MAT002", 3106}, {"MAT003", 3107}, {"MAT004", 3110}, {"MAT005", 3111},
    {"CGEN004", 4004},
    {"TAR001", 6001}, {"TAR002", 6002},
    {"IMP001", 7001}, {"IMP002", 7002}, {"IMP003", 7003},
    {"CIMP001", 8001}, {"CIMP002", 8002}, {"CIMP003", 8003}, {"CIMP004", 8004}, {"CIMP005", 8005},
    {"PKG001", 9001}, {"PKG002", 9002}, {"PKG003", 9003}, {"PKG004", 9004},
    {NULL, 0},
  };
  if (!code) return -1;
  for (size_t i = 0; pairs[i].code; i++) {
    if (strcmp(pairs[i].code, code) == 0) return pairs[i].numeric;
  }
  return -1;
}

static void print_explain_json(const ExplainInfo *info) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"code\": ");
  append_json_string(&buf, info->code);
  zbuf_append(&buf, ",\n  \"category\": ");
  append_json_string(&buf, info->category);
  zbuf_append(&buf, ",\n  \"title\": ");
  append_json_string(&buf, info->title);
  zbuf_append(&buf, ",\n  \"summary\": ");
  append_json_string(&buf, info->summary);
  zbuf_append(&buf, ",\n  \"why\": ");
  append_json_string(&buf, info->why);
  zbuf_append(&buf, ",\n  \"repair\": {\"id\": ");
  append_json_string(&buf, diag_repair_id(explain_repair_numeric(info->code)));
  zbuf_append(&buf, ", \"summary\": ");
  append_json_string(&buf, info->canonical_repair);
  zbuf_append(&buf, "},\n  \"examples\": {\"bad\": ");
  append_json_string(&buf, info->bad_example);
  zbuf_append(&buf, ", \"good\": ");
  append_json_string(&buf, info->good_example);
  zbuf_append(&buf, "}\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static void print_explain_text(const ExplainInfo *info) {
  printf("%s: %s\n\n", info->code, info->title);
  printf("%s\n\n", info->summary);
  printf("Why: %s\n\n", info->why);
  printf("Repair: %s\n\n", info->canonical_repair);
  printf("Bad:\n%s\n\nGood:\n%s\n", info->bad_example, info->good_example);
}

static int explain_command(const Command *command) {
  const ExplainInfo *info = find_explain_info(command->input);
  if (!info) {
    ZDiag diag = {0};
    diag.code = 3003;
    diag.line = 1;
    diag.column = 1;
    snprintf(diag.message, sizeof(diag.message), "unknown diagnostic code '%s'", command->input ? command->input : "");
    snprintf(diag.expected, sizeof(diag.expected), "known diagnostic code");
    snprintf(diag.actual, sizeof(diag.actual), "%s", command->input ? command->input : "");
    snprintf(diag.help, sizeof(diag.help), "try TAR002, TYP009, TYP023, ERR001, ERR002, ERR003, FLD002, or STD003");
    print_command_diag(command, command->input, &diag);
    return 1;
  }
  if (command->json) print_explain_json(info);
  else print_explain_text(info);
  return 0;
}

static void append_backend_blocker_json(ZBuf *buf, const ZBackendBlocker *blocker) {
  zbuf_append(buf, "{\"target\":");
  append_json_string(buf, blocker && blocker->target[0] ? blocker->target : "unknown");
  zbuf_append(buf, ",\"objectFormat\":");
  append_json_string(buf, blocker && blocker->object_format[0] ? blocker->object_format : "unknown");
  zbuf_append(buf, ",\"backend\":");
  append_json_string(buf, blocker && blocker->backend[0] ? blocker->backend : "unknown");
  zbuf_append(buf, ",\"stage\":");
  append_json_string(buf, blocker && blocker->stage[0] ? blocker->stage : "unknown");
  zbuf_append(buf, ",\"unsupportedFeature\":");
  append_json_string(buf, blocker && blocker->unsupported_feature[0] ? blocker->unsupported_feature : "unsupported construct");
  zbuf_append(buf, "}");
}

static void append_error_diag_json_object(ZBuf *bufp, const char *path, const ZDiag *diag);

static void print_diag_json_list_ex(const char *path, const ZDiag *diags, size_t len, const char *safety_profile) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": false,\n  \"diagnostics\": [\n");
  for (size_t i = 0; i < len; i++) {
    if (i > 0) zbuf_append(&buf, ",\n");
    zbuf_append(&buf, "    {\n");
    append_error_diag_json_object(&buf, path, &diags[i]);
    zbuf_append(&buf, "\n    }");
  }
  zbuf_append(&buf, "\n  ]");
  if (safety_profile) {
    zbuf_append(&buf, ",\n  \"safetyFacts\": ");
    append_safety_facts_json(&buf, safety_profile);
  }
  zbuf_append(&buf, "\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static void print_diag_json_ex(const char *path, const ZDiag *diag, const char *safety_profile) {
  print_diag_json_list_ex(path, diag, 1, safety_profile);
}

static void append_error_diag_json_object(ZBuf *buf, const char *path, const ZDiag *diag) {
  zbuf_append(buf, "      \"severity\": \"error\",\n      \"code\": ");
  append_json_string(buf, diag_code(diag->code));
  zbuf_append(buf, ",\n      \"message\": ");
  append_json_string(buf, diag->message);
  zbuf_append(buf, ",\n      \"path\": ");
  append_json_string(buf, diag->path ? diag->path : path);
  zbuf_appendf(buf, ",\n      \"line\": %d,\n      \"column\": %d,\n      \"length\": %d,\n", diag->line, diag->column, diag->length > 0 ? diag->length : 1);
  zbuf_append(buf, "      \"expected\": ");
  append_json_string(buf, diag->expected);
  zbuf_append(buf, ",\n      \"actual\": ");
  append_json_string(buf, diag->actual);
  zbuf_append(buf, ",\n      \"help\": ");
  append_json_string(buf, diag->help);
  zbuf_append(buf, ",\n      \"fixSafety\": ");
  append_json_string(buf, diag_fix_safety(diag->code));
  zbuf_append(buf, ",\n      \"repair\": {\"id\": ");
  append_json_string(buf, diag_repair_id(diag->code));
  zbuf_append(buf, ", \"summary\": ");
  append_json_string(buf, diag_repair_summary(diag->code));
  zbuf_append(buf, "}");
  if (diag->backend_blocker.present) {
    zbuf_append(buf, ",\n      \"backendBlocker\": ");
    append_backend_blocker_json(buf, &diag->backend_blocker);
  }
  if (diag_has_borrow_trace(diag)) {
    zbuf_append(buf, ",\n      \"borrowTrace\": ");
    append_diag_borrow_trace_json(buf, path, diag);
  }
  zbuf_append(buf, ",\n      \"related\": [");
  if ((diag->code == 7001 || diag->code == 7002 || diag->code == 7003 || diag->code == 1002 || diag->code == 1003 ||
       (diag->code >= 1004 && diag->code <= 1010) ||
       diag->code == 6001 || diag->code == 6002 ||
       diag->code == 3010 || diag->code == 3011 || diag->code == 3012 || diag->code == 3028 || diag->code == 3029) && diag->actual[0]) {
    zbuf_append(buf, "{\"path\":");
    append_json_string(buf, diag->path ? diag->path : path);
    zbuf_appendf(buf, ",\"line\":%d,\"column\":%d,\"message\":", diag->line, diag->column);
    append_json_string(buf, diag->actual);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]");
}

static void print_diag_json(const char *path, const ZDiag *diag) {
  print_diag_json_ex(path, diag, NULL);
}

static void print_check_diag_json(const char *path, const ZDiag *diag, const char *profile) {
  print_diag_json_ex(path, diag, profile ? profile : "release");
}

static void print_command_diag_json(const Command *command, const char *path, const ZDiag *diag) {
  if (command && graph_check_text_eq(command->command, "check")) print_check_diag_json(path, diag, command->profile);
  else print_diag_json(path, diag);
}

static void print_command_diag(const Command *command, const char *path, const ZDiag *diag) {
  if (command && command->json) print_command_diag_json(command, path, diag);
  else print_diag(path, diag);
}

static void append_fix_plan_diagnostic(ZBuf *buf, const char *path, const ZDiag *diag) {
  zbuf_append(buf, "{");
  zbuf_append(buf, "\"code\": ");
  append_json_string(buf, diag_code(diag->code));
  zbuf_append(buf, ", \"message\": ");
  append_json_string(buf, diag->message);
  zbuf_append(buf, ", \"path\": ");
  append_json_string(buf, diag->path ? diag->path : path);
  zbuf_appendf(buf, ", \"line\": %d, \"column\": %d, \"length\": %d", diag->line, diag->column, diag->length > 0 ? diag->length : 1);
  zbuf_append(buf, ", \"expected\": ");
  append_json_string(buf, diag->expected);
  zbuf_append(buf, ", \"actual\": ");
  append_json_string(buf, diag->actual);
  zbuf_append(buf, ", \"help\": ");
  append_json_string(buf, diag->help);
  zbuf_append(buf, ", \"fixSafety\": ");
  append_json_string(buf, diag_fix_safety(diag->code));
  zbuf_append(buf, ", \"repair\": {\"id\": ");
  append_json_string(buf, diag_repair_id(diag->code));
  zbuf_append(buf, ", \"summary\": ");
  append_json_string(buf, diag_repair_summary(diag->code));
  zbuf_append(buf, "}");
  if (diag->backend_blocker.present) {
    zbuf_append(buf, ", \"backendBlocker\": ");
    append_backend_blocker_json(buf, &diag->backend_blocker);
  }
  if (diag_has_borrow_trace(diag)) {
    zbuf_append(buf, ", \"borrowTrace\": ");
    append_diag_borrow_trace_json(buf, path, diag);
  }
  zbuf_append(buf, "}");
}

static void print_fix_plan_json(const char *path, const ZDiag *diag) {
  bool has_diag = diag && diag->code != 0;
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(&buf, has_diag ? "false" : "true");
  zbuf_append(&buf, ",\n  \"mode\": \"plan\",\n  \"appliesEdits\": false,\n  \"safetyLevels\": [\"format-only\", \"behavior-preserving\", \"api-changing\", \"target-changing\", \"requires-human-review\"],\n  \"input\": ");
  append_json_string(&buf, path);
  zbuf_append(&buf, ",\n  \"selfHostRepairPolicy\": {\"unsupportedFeatureSafety\":\"requires-human-review\",\"compatibilityFallback\":\"removed\",\"directFallback\":\"never-c-bridge\"}");
  zbuf_append(&buf, ",\n  \"diagnostics\": [");
  if (has_diag) append_fix_plan_diagnostic(&buf, path, diag);
  zbuf_append(&buf, "],\n  \"fixes\": [");
  if (has_diag) {
    zbuf_append(&buf, "{\"id\": ");
    append_json_string(&buf, diag_repair_id(diag->code));
    zbuf_append(&buf, ", \"diagnosticCode\": ");
    append_json_string(&buf, diag_code(diag->code));
    zbuf_append(&buf, ", \"safety\": ");
    append_json_string(&buf, diag_fix_safety(diag->code));
    zbuf_append(&buf, ", \"summary\": ");
    append_json_string(&buf, diag_repair_summary(diag->code));
    zbuf_append(&buf, ", \"appliesEdits\": false}");
  }
  zbuf_append(&buf, "]\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static bool find_make_binding_mutable_edit(const char *source, int *line_out, char **old_line_out, char **new_line_out) {
  const char *cursor = source ? source : "";
  int line = 1;
  while (*cursor) {
    const char *line_start = cursor;
    const char *line_end = strchr(line_start, '\n');
    if (!line_end) line_end = line_start + strlen(line_start);
    size_t line_len = (size_t)(line_end - line_start);
    char *line_text = z_strndup(line_start, line_len);
    char *let_pos = strstr(line_text, "let ");
    if (let_pos && !strstr(line_text, "var ") && strchr(line_text, '[')) {
      ZBuf replacement;
      zbuf_init(&replacement);
      size_t prefix_len = (size_t)(let_pos - line_text);
      char *prefix = z_strndup(line_text, prefix_len);
      zbuf_append(&replacement, prefix);
      zbuf_append(&replacement, "var ");
      zbuf_append(&replacement, let_pos + strlen("let "));
      free(prefix);
      *line_out = line;
      *old_line_out = line_text;
      *new_line_out = replacement.data;
      return true;
    }
    free(line_text);
    if (!*line_end) break;
    cursor = line_end + 1;
    line++;
  }
  return false;
}

static char *apply_single_line_edit(const char *source, int target_line, const char *new_line) {
  ZBuf out;
  zbuf_init(&out);
  const char *cursor = source ? source : "";
  int line = 1;
  while (*cursor) {
    const char *line_start = cursor;
    const char *line_end = strchr(line_start, '\n');
    bool has_newline = line_end != NULL;
    if (!line_end) line_end = line_start + strlen(line_start);
    if (line == target_line) {
      zbuf_append(&out, new_line);
    } else {
      char *line_text = z_strndup(line_start, (size_t)(line_end - line_start));
      zbuf_append(&out, line_text);
      free(line_text);
    }
    if (has_newline) zbuf_append_char(&out, '\n');
    if (!has_newline) break;
    cursor = line_end + 1;
    line++;
  }
  return out.data;
}

static bool diagnostic_can_apply_edits(const ZDiag *diag) {
  return diag && diag->code == 3010 && strcmp(diag_fix_safety(diag->code), "behavior-preserving") == 0;
}

static int print_or_apply_fix_json(const char *path, SourceInput *input, const ZDiag *diag, bool apply) {
  bool has_diag = diag && diag->code != 0;
  bool can_apply = diagnostic_can_apply_edits(diag);
  const char *edit_path = input ? input->source_file : NULL;
  const char *edit_source = NULL;
  char *loaded_edit_source = NULL;
  if (can_apply && diag && diag->path && diag->path[0]) {
    edit_path = diag->path;
  }
  if (can_apply && edit_path && edit_path[0]) {
    ZDiag read_diag = {0};
    loaded_edit_source = z_read_file(edit_path, &read_diag);
    edit_source = loaded_edit_source;
  }
  int edit_line = 0;
  char *old_line = NULL;
  char *new_line = NULL;
  bool has_edit = can_apply && edit_source && find_make_binding_mutable_edit(edit_source, &edit_line, &old_line, &new_line);
  bool applied = false;
  if (apply && has_edit) {
    char *updated = apply_single_line_edit(edit_source, edit_line, new_line);
    ZDiag write_diag = {0};
    applied = edit_path && z_write_file(edit_path, updated, &write_diag);
    free(updated);
  }

  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(&buf, has_diag ? "false" : "true");
  zbuf_append(&buf, ",\n  \"mode\": ");
  append_json_string(&buf, apply ? "apply" : "patch");
  zbuf_appendf(&buf, ",\n  \"appliesEdits\": %s,\n  \"applied\": %s,\n  \"input\": ", apply ? "true" : "false", applied ? "true" : "false");
  append_json_string(&buf, path);
  zbuf_append(&buf, ",\n  \"selfHostRepairPolicy\": {\"unsupportedFeatureSafety\":\"requires-human-review\",\"compatibilityFallback\":\"removed\",\"directFallback\":\"never-c-bridge\"}");
  zbuf_append(&buf, ",\n  \"diagnostics\": [");
  if (has_diag) append_fix_plan_diagnostic(&buf, path, diag);
  zbuf_append(&buf, "],\n  \"fixes\": [");
  if (has_diag) {
    zbuf_append(&buf, "{\"id\": ");
    append_json_string(&buf, diag_repair_id(diag->code));
    zbuf_append(&buf, ", \"diagnosticCode\": ");
    append_json_string(&buf, diag_code(diag->code));
    zbuf_append(&buf, ", \"safety\": ");
    append_json_string(&buf, diag_fix_safety(diag->code));
    zbuf_append(&buf, ", \"summary\": ");
    append_json_string(&buf, diag_repair_summary(diag->code));
    zbuf_appendf(&buf, ", \"appliesEdits\": %s}", has_edit ? "true" : "false");
  }
  zbuf_append(&buf, "],\n  \"patches\": [");
  if (has_edit) {
    zbuf_append(&buf, "{\"path\":");
    append_json_string(&buf, edit_path);
    zbuf_appendf(&buf, ",\"line\":%d,\"old\":", edit_line);
    append_json_string(&buf, old_line);
    zbuf_append(&buf, ",\"new\":");
    append_json_string(&buf, new_line);
    zbuf_append(&buf, "}");
  }
  zbuf_append(&buf, "]\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
  free(old_line);
  free(new_line);
  free(loaded_edit_source);
  return apply && has_edit && !applied ? 1 : 0;
}

static bool cli_arg_is(const char *arg, const char *expected) {
  return strcmp(arg ? arg : "", expected) == 0;
}

static void print_help_with_fmt_modes(void) {
  z_cli_print_help();
  printf("\nFormatting modes:\n");
  printf("  zero fmt [--check | --write] [--json] <file.0|project|zero.toml|zero.json>\n");
}

static void print_command_help_with_fmt_modes(const char *command) {
  if (!cli_arg_is(command, "fmt")) {
    z_cli_print_command_help(command);
    return;
  }
  printf("Usage: zero fmt [--check | --write] [--json] <file.0|project|zero.toml|zero.json>\n\n");
  printf("Print deterministic bootstrap formatting for Zero source.\n");
  printf("With --check, exit non-zero if input would be reformatted.\n");
  printf("With --write, overwrite the input file in place.\n");
  printf("With --json, emit a structured envelope for --check or --write.\n");
}

static bool is_program_graph_root_command(const char *command) {
  static const char *const commands[] = {"init", "dump", "import", "export", "query", "inspect", "validate", "view", "diff", "source-map", "reconcile", "status", "verify-projection", "merge", "roundtrip", "patch"};
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (cli_arg_is(command, commands[i])) return true;
  }
  return false;
}

static bool command_is_program_graph_command(const Command *command) {
  return command && command->kind && (cli_arg_is(command->command, "graph") || is_program_graph_root_command(command->command));
}
static bool parse_graph_query_option(int argc, char **argv, int *index, Command *command) {
  const char *arg = argv[*index];
  const bool function_selector = cli_arg_is(arg, "--fn") || cli_arg_is(arg, "--function");
  const bool find_selector = cli_arg_is(arg, "--find");
  const bool node_selector = cli_arg_is(arg, "--node");
  const bool refs_selector = cli_arg_is(arg, "--refs");
  const bool calls_selector = cli_arg_is(arg, "--calls");
  const bool depth_selector = cli_arg_is(arg, "--depth");
  const bool full_selector = cli_arg_is(arg, "--full");
  const bool handles_selector = cli_arg_is(arg, "--handles");
  const bool no_help_selector = cli_arg_is(arg, "--no-help");
  const bool outline_selector = cli_arg_is(arg, "--outline");
  const bool around_selector = cli_arg_is(arg, "--around");
  if (!function_selector && !find_selector && !node_selector && !refs_selector && !calls_selector && !depth_selector && !full_selector && !handles_selector && !no_help_selector && !outline_selector && !around_selector) return false;
  const bool query_command = command_is_program_graph_command(command) && cli_arg_is(command->kind, "query");
  const bool view_command = command_is_program_graph_command(command) && (cli_arg_is(command->kind, "view") || cli_arg_is(command->kind, "diff"));
  if (function_selector && view_command) {
    if (*index + 1 >= argc) {
      command->unknown_flag = arg;
      return true;
    }
    command->query_function = argv[++(*index)];
    return true;
  }
  if ((outline_selector || around_selector) && command_is_program_graph_command(command) && cli_arg_is(command->kind, "view")) {
    if (*index + 1 >= argc) {
      command->unknown_flag = arg;
      return true;
    }
    if (outline_selector) command->view_outline = argv[++(*index)];
    else command->view_around = argv[++(*index)];
    return true;
  }
  if (handles_selector && view_command) {
    command->query_handles = true;
    return true;
  }
  const bool patch_command = command_is_program_graph_command(command) && cli_arg_is(command->kind, "patch");
  if (function_selector && patch_command) {
    if (*index + 1 >= argc) {
      command->unknown_flag = arg;
      return true;
    }
    command->query_function = argv[++(*index)];
    return true;
  }
  if (!query_command) {
    command->unknown_flag = arg;
    return true;
  }
  if (full_selector) {
    command->query_full = true;
    return true;
  }
  if (handles_selector) {
    command->query_handles = true;
    return true;
  }
  if (no_help_selector) {
    command->query_no_help = true;
    return true;
  }
  if (outline_selector || around_selector) {
    command->unknown_flag = arg;
    return true;
  }
  if (*index + 1 >= argc) {
    command->unknown_flag = arg;
    return true;
  }
  if (function_selector) command->query_function = argv[++(*index)];
  else if (node_selector) command->query_node = argv[++(*index)];
  else if (refs_selector) command->query_refs = argv[++(*index)];
  else if (calls_selector) command->query_calls = argv[++(*index)];
  else if (depth_selector) command->query_depth = argv[++(*index)];
  else command->query_find = argv[++(*index)];
  return true;
}

static bool parse_emit_option(int argc, char **argv, int *index, Command *command) {
  const char *arg = argv[*index];
  if (!cli_arg_is(arg, "--emit")) return false;
  if (*index + 1 >= argc) {
    command->unknown_flag = arg;
    return true;
  }
  (*index)++;
  if (strcmp(argv[*index], "exe") == 0) command->emit = EMIT_EXE;
  else if (strcmp(argv[*index], "obj") == 0) command->emit = EMIT_OBJ;
  else if (strcmp(argv[*index], "c") == 0) command->emit = EMIT_C;
  else if (strcmp(argv[*index], "llvm-ir") == 0) command->emit = EMIT_LLVM_IR;
  else command->invalid_emit = argv[*index];
  return true;
}

static bool parse_common_value_option(int argc, char **argv, int *index, Command *command) {
  const char *arg = argv[*index];
  const char **slot = NULL;
  if (strcmp(arg, "--out") == 0) slot = &command->out;
  else if (strcmp(arg, "--target") == 0) slot = &command->target;
  else if (strcmp(arg, "--profile") == 0 || strcmp(arg, "--release") == 0) slot = &command->profile;
  else if (strcmp(arg, "--cc") == 0) slot = &command->cc;
  else if (strcmp(arg, "--backend") == 0) slot = &command->backend;
  else if (strcmp(arg, "--format") == 0) slot = &command->store_format;
  else if (strcmp(arg, "--manifest") == 0) slot = &command->manifest_format;
  else if (strcmp(arg, "--template") == 0 && command && cli_arg_is(command->command, "init")) slot = &command->init_template;
  else if (strcmp(arg, "--filter") == 0) slot = &command->filter;
  if (!slot) return false;
  if (*index + 1 >= argc) command->unknown_flag = arg;
  else *slot = argv[++(*index)];
  return true;
}

static const char **graph_patch_value_option_slot(Command *command, const char *arg) {
  if (cli_arg_is(arg, "--patch-text")) return &command->patch_text;
  if (cli_arg_is(arg, "--expect-graph-hash")) return &command->patch_expect_graph_hash;
  if (cli_arg_is(arg, "--replace-fn")) return &command->patch_replace_fn;
  if (cli_arg_is(arg, "--body-file")) return &command->patch_body_file;
  if (cli_arg_is(arg, "--replace-in-fn")) return &command->patch_replace_in_fn;
  if (cli_arg_is(arg, "--old")) return &command->patch_old_text;
  if (cli_arg_is(arg, "--old-file")) return &command->patch_old_file;
  if (cli_arg_is(arg, "--new")) return &command->patch_new_text;
  if (cli_arg_is(arg, "--new-file")) return &command->patch_new_file;
  if (cli_arg_is(arg, "--rewrite")) return &command->patch_rewrite;
  if (cli_arg_is(arg, "--to")) return &command->patch_rewrite_to;
  return NULL;
}

static bool parse_graph_patch_value_option(int argc, char **argv, int *index, Command *command) {
  const char *arg = argv[*index];
  const char **slot = command ? graph_patch_value_option_slot(command, arg) : NULL;
  if (!slot) return false;
  if (!command->graph_patch_command) {
    command->unknown_flag = arg;
    return true;
  }
  if (*index + 1 >= argc) command->unknown_flag = arg;
  else *slot = argv[++(*index)];
  return true;
}

static bool parse_common_option(int argc, char **argv, int *index, Command *command) {
  const char *arg = argv[*index];
  if (parse_emit_option(argc, argv, index, command)) return true;
  if (parse_common_value_option(argc, argv, index, command)) {
    return true;
  } else if (parse_graph_query_option(argc, argv, index, command)) {
    return true;
  } else if (parse_graph_patch_value_option(argc, argv, index, command)) {
    return true;
  } else if (strcmp(arg, "--json") == 0) {
    command->json = true;
    return true;
  } else if (cli_arg_is(arg, "--check-only") || cli_arg_is(arg, "--dry-run")) {
    if (!command || !command->graph_patch_command) {
      command->unknown_flag = arg;
      return true;
    }
    command->graph_patch_check_only = true;
    return true;
  } else if (strcmp(arg, "--plan") == 0) {
    command->plan = true;
    return true;
  } else if (strcmp(arg, "--apply") == 0) {
    command->apply = true;
    return true;
  } else if (strcmp(arg, "--patch") == 0) {
    command->patch = true;
    return true;
  } else if (cli_arg_is(arg, "--op")) {
    if (!command || !command->graph_patch_command) {
      command->unknown_flag = arg;
      return true;
    }
    if (*index + 1 >= argc) {
      command->unknown_flag = arg;
    } else {
      if (command->patch_op_len == command->patch_op_cap) {
        size_t next = command->patch_op_cap ? command->patch_op_cap * 2 : 4;
        command->patch_ops = z_checked_reallocarray(command->patch_ops, next, sizeof(const char *));
        command->patch_op_cap = next;
      }
      command->patch_ops[command->patch_op_len++] = argv[++(*index)];
    }
    return true;
  } else if (cli_arg_is(arg, "--source")) {
    if (!command || !command->graph_reconcile_command) {
      command->unknown_flag = arg;
      return true;
    }
    if (*index + 1 >= argc) command->unknown_flag = arg;
    else command->reconcile_source = argv[++(*index)];
    return true;
  } else if (strcmp(arg, "--all") == 0) {
    command->all = true;
    return true;
  } else if (strcmp(arg, "--check") == 0) {
    command->fmt_check = true;
    return true;
  } else if (strcmp(arg, "--write") == 0) {
    command->fmt_write = true;
    return true;
  } else if (strcmp(arg, "--trace") == 0) {
    command->trace = true;
    return true;
  } else if (strcmp(arg, "--legacy-backend") == 0) {
    command->legacy_backend = true;
    return true;
  } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
    command->kind = "help";
    return true;
  } else if (strncmp(arg, "--", 2) == 0) { command->unknown_flag = arg; return true; }
  return false;
}

static bool parse_graph_merge_option(int argc, char **argv, int *index, Command *command) {
  const char *arg = argv[*index];
  const char **slot = NULL;
  if (cli_arg_is(arg, "--base")) slot = &command->merge_base;
  else if (cli_arg_is(arg, "--left")) slot = &command->merge_left;
  else if (cli_arg_is(arg, "--right")) slot = &command->merge_right;
  else return false;
  if (*index + 1 >= argc) command->unknown_flag = arg;
  else *slot = argv[++(*index)];
  return true;
}

static bool command_defaults_to_current_directory(const Command *command) {
  if (!command || command->input) return false;
  static const char *const current_dir_default_commands[] = {"init", "check", "test", "build", "run", "doc", "size", "mem", "dev", "time", "fix"};
  for (size_t i = 0; i < sizeof(current_dir_default_commands) / sizeof(current_dir_default_commands[0]); i++) {
    if (cli_arg_is(command->command, current_dir_default_commands[i])) return true;
  }
  if (cli_arg_is(command->command, "abi") &&
      command->kind &&
      (cli_arg_is(command->kind, "check") || cli_arg_is(command->kind, "dump"))) return true;
  if (command_is_program_graph_command(command)) return true;
  return false;
}

static void command_apply_current_directory_default(Command *command) {
  if (command_defaults_to_current_directory(command)) command->input = ".";
}

static bool query_argument_looks_like_symbol(const char *arg) {
  if (!arg || !arg[0]) return false;
  if (!isalpha((unsigned char)arg[0]) && arg[0] != '_') return false;
  for (const char *c = arg; *c; c++) {
    if (!isalnum((unsigned char)*c) && *c != '_' && *c != '.') return false;
  }
  static const char *const input_extensions[] = {".0", ".graph", ".toml", ".json"};
  size_t len = strlen(arg);
  for (size_t i = 0; i < sizeof(input_extensions) / sizeof(input_extensions[0]); i++) {
    size_t ext_len = strlen(input_extensions[i]);
    if (len > ext_len && strcmp(arg + len - ext_len, input_extensions[i]) == 0) return false;
  }
  return true;
}

static void command_apply_query_bare_argument(Command *command) {
  if (!command || !command->kind || !cli_arg_is(command->kind, "query")) return;
  if (!command->input || path_exists(command->input)) return;
  if (command->query_find || command->query_function || command->query_refs || command->query_calls || command->query_node) return;
  if (!query_argument_looks_like_symbol(command->input)) return;
  command->query_find = command->input;
  command->query_bare_argument = command->input;
  command->input = NULL;
}

static bool parse_command(int argc, char **argv, Command *command) {
  if (argc < 2) return false;
  command->command = argv[1];
  command->emit = EMIT_EXE;
  command->target = "host";
  command->profile = "release";
  if (strcmp(command->command, "skills") == 0) {
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) command->kind = "help";
      else if (strcmp(argv[i], "--json") == 0) command->json = true;
      else if (strcmp(argv[i], "--all") == 0) command->all = true;
      else if (strcmp(argv[i], "--full") == 0) {
        continue;
      } else if (!command->kind) {
        command->kind = argv[i];
      } else if (!command->input) {
        command->input = argv[i];
      }
    }
    return true;
  }
  int arg_start = 2;
  bool is_graph_command = strcmp(command->command, "graph") == 0;
  bool is_root_graph_command = is_program_graph_root_command(command->command);
  bool is_patch_command = graph_check_text_eq(command->command, "patch");
  if (strcmp(command->command, "abi") == 0 && argc >= 3 && strncmp(argv[2], "--", 2) != 0) {
    command->kind = argv[2];
    arg_start = 3;
  }
  if (is_graph_command && argc >= 3 && (cli_arg_is(argv[2], "check") || cli_arg_is(argv[2], "patch") || z_program_graph_command_kind_is_known(argv[2]))) {
    command->kind = argv[2];
    command->retired_graph_subcommand = argv[2];
    arg_start = 3;
  } else if (is_root_graph_command) {
    command->kind = command->command;
  }
  if (is_patch_command) command->kind = "patch";
  command->graph_patch_command = is_patch_command;
  if (is_root_graph_command && command->kind && cli_arg_is(command->kind, "import")) command->graph_import_from_source = true;
  if (is_root_graph_command && command->kind && cli_arg_is(command->kind, "export")) command->graph_export_from_graph = true;
  command->graph_reconcile_command = (is_graph_command || is_root_graph_command) && command->kind && cli_arg_is(command->kind, "reconcile");
  bool graph_merge_command = (is_graph_command || is_root_graph_command) && command->kind && cli_arg_is(command->kind, "merge");
  if (cli_arg_is(command->command, "run")) {
    for (int i = arg_start; i < argc; i++) {
      if (command->input) {
        if (cli_arg_is(argv[i], "--")) {
          command->run_argc = argc - i - 1;
          command->run_argv = &argv[i + 1];
        } else {
          command->run_argc = argc - i;
          command->run_argv = &argv[i];
        }
        return true;
      }
      if (cli_arg_is(argv[i], "--")) {
        command->input = ".";
        command->run_argc = argc - i - 1;
        command->run_argv = &argv[i + 1];
        return true;
      }
      if (parse_common_option(argc, argv, &i, command)) continue;
      command->input = argv[i];
    }
    return true;
  }
  for (int i = arg_start; i < argc; i++) {
    if (graph_merge_command && parse_graph_merge_option(argc, argv, &i, command)) {
      continue;
    } else if (parse_common_option(argc, argv, &i, command)) {
      continue;
    } else if (command->graph_patch_command && command->input) {
      if (command->patch_file) return false;
      command->patch_file = argv[i];
    } else {
      command->input = argv[i];
    }
  }
  static const char *const known_commands[] = {"--version", "version", "skills", "check", "patch", "test", "fmt", "build", "run", "tokens", "parse", "doc", "size", "mem", "dev", "time", "abi", "explain", "fix", "doctor", "clean", "targets"};
  if (is_graph_command || is_root_graph_command) return true;
  for (size_t i = 0; i < sizeof(known_commands) / sizeof(known_commands[0]); i++) {
    if (cli_arg_is(command->command, known_commands[i])) return true;
  }
  return false;
}

static const char *emit_kind_name(EmitKind emit) {
  switch (emit) {
    case EMIT_OBJ: return "obj";
    case EMIT_C: return "c";
    case EMIT_LLVM_IR: return "llvm-ir";
    case EMIT_EXE:
    default:
      return "exe";
  }
}

static long long now_ms(void);
static bool has_suffix(const char *path, const char *suffix);
static bool direct_source_reserved_word(const char *text);
static size_t source_line_count(const char *source);
static void direct_source_append(SourceInput *input, ZBuf *combined, const char *path, const char *source);

static bool is_zero_source_path(const char *path) {
  return path && has_suffix(path, ".0");
}

static bool is_existing_directory_path(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool path_has_program_graph_storage_header(const char *path) {
  static const char graph_header[] = "zero-graph v";
  static const char repository_header[] = "zero-repository-graph v";
  unsigned char bytes[32];
  size_t read = 0;
  if (!z_read_file_prefix(path, bytes, sizeof(bytes), &read, NULL)) return false;
  return (read >= sizeof(graph_header) - 1 && memcmp(bytes, graph_header, sizeof(graph_header) - 1) == 0) ||
         (read >= sizeof(repository_header) - 1 && memcmp(bytes, repository_header, sizeof(repository_header) - 1) == 0) ||
         z_program_graph_store_bytes_are_binary(bytes, read);
}

static char *source_sidecar_graph_path(const char *source_path) {
  if (!is_zero_source_path(source_path)) return NULL;
  size_t len = strlen(source_path);
  if (len < 2) return NULL;
  char *path = z_checked_malloc(len + 5);
  memcpy(path, source_path, len - 2);
  memcpy(path + len - 2, ".graph", 7);
  return path;
}

static void set_graph_input_required_diag(const char *input_path, const char *graph_path, ZDiag *diag) {
  if (!diag) return;
  diag->code = 2002;
  diag->path = input_path;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "compiler command requires graph input");
  snprintf(diag->expected, sizeof(diag->expected), "package zero.graph store, .graph store, or ProgramGraph artifact");
  snprintf(diag->actual, sizeof(diag->actual), "%s", input_path ? input_path : "");
  snprintf(diag->help, sizeof(diag->help), "%s", graph_path && graph_path[0] ? "run zero import after a human edits the .0 projection, then pass the package or .graph store" : "run zero import through the graph workflow before compiling");
}

static void set_graph_patch_projection_input_diag(const char *input_path, const char *graph_path, ZDiag *diag) {
  if (!diag) return;
  diag->code = 2002;
  diag->path = input_path;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "graph patch requires graph input");
  snprintf(diag->expected, sizeof(diag->expected), "package zero.graph store, .graph store, or ProgramGraph artifact");
  snprintf(diag->actual, sizeof(diag->actual), "%s", input_path ? input_path : "");
  snprintf(diag->help, sizeof(diag->help), "%s", graph_path && graph_path[0] ? "patch the package or .graph store directly, or run zero import after a human edits the .0 projection" : "run zero import to create a graph store before patching");
}

static const char *patch_detect_skip_filler(const char *cursor, const char *end) {
  if (end - cursor >= 3 && memcmp(cursor, "\xEF\xBB\xBF", 3) == 0) cursor += 3;
  while (cursor < end) {
    if (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') { cursor++; continue; }
    if (*cursor == '#') {
      while (cursor < end && *cursor != '\n') cursor++;
      continue;
    }
    break;
  }
  return cursor;
}

static bool path_has_program_graph_patch_header(const char *path) {
  static const char header[] = "zero-program-graph-patch v1";
  char bytes[512];
  size_t read = 0;
  if (!z_read_file_prefix(path, bytes, sizeof(bytes), &read, NULL)) return false;
  const char *cursor = patch_detect_skip_filler(bytes, bytes + read);
  return (size_t)(bytes + read - cursor) >= sizeof(header) - 1 && memcmp(cursor, header, sizeof(header) - 1) == 0;
}

static bool path_starts_with_program_graph_patch_operation(const char *path) {
  static const char *const keywords[] = {
    "expect", "set", "insertEdge", "insert", "replaceFunctionBody", "replaceBlockBody", "replace",
    "replaceExpr", "deleteTest", "renameTest", "delete", "rename", "setConst", "setReturnType",
    "addFunction", "addMain", "addParamTo", "addParam", "addReturnBinary", "addLetLiteral",
    "addLetBinary", "addReturnValue", "addReturnExpr", "appendStmt", "addCheckWriteValue",
    "addCheckWrite", "addTestBody", "addTest", "upsertFunction", NULL,
  };
  char bytes[512];
  size_t read = 0;
  if (!z_read_file_prefix(path, bytes, sizeof(bytes), &read, NULL)) return false;
  const char *cursor = patch_detect_skip_filler(bytes, bytes + read);
  const char *end = bytes + read;
  for (size_t i = 0; keywords[i]; i++) {
    size_t len = strlen(keywords[i]);
    if ((size_t)(end - cursor) < len || memcmp(cursor, keywords[i], len) != 0) continue;
    if (cursor + len == end || cursor[len] == ' ' || cursor[len] == '\t' || cursor[len] == '\r' || cursor[len] == '\n') return true;
  }
  return false;
}

static void direct_input_push_string(char ***items, size_t *len, const char *value) {
  *items = z_checked_reallocarray(*items, *len + 1, sizeof(char *));
  (*items)[(*len)++] = z_strdup(value ? value : "");
}

static void direct_input_push_source_line(SourceInput *input, const char *path, int line) {
  input->source_line_paths = z_checked_reallocarray(input->source_line_paths, input->source_line_count + 1, sizeof(char *));
  input->source_line_numbers = z_checked_reallocarray(input->source_line_numbers, input->source_line_count + 1, sizeof(int));
  input->source_line_paths[input->source_line_count] = z_strdup(path ? path : "");
  input->source_line_numbers[input->source_line_count] = line > 0 ? line : 1;
  input->source_line_count++;
}

static char *direct_module_name_from_path(const char *path) {
  const char *name = path ? strrchr(path, '/') : NULL;
  name = name ? name + 1 : (path ? path : "main");
  size_t len = strlen(name);
  if (len > 2 && strcmp(name + len - 2, ".0") == 0) len -= 2;
  return len > 0 ? z_strndup(name, len) : z_strdup("main");
}

static char *direct_dirname_of(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  return z_strndup(path, (size_t)(slash - path));
}

static char *direct_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left && left[0] ? left : ".");
  if (buf.len == 0 || buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right ? right : "");
  return buf.data;
}

static bool direct_file_exists(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0;
}

static bool direct_input_has_file(const SourceInput *input, const char *path) {
  for (size_t i = 0; input && i < input->source_file_count; i++) {
    if (strcmp(input->source_files[i], path) == 0) return true;
  }
  return false;
}

static char *direct_source_module_name_from_path(const char *root, const char *path) {
  const char *relative = path ? path : "main";
  size_t root_len = root ? strlen(root) : 0;
  if (root && strncmp(relative, root, root_len) == 0) {
    relative += root_len;
    if (*relative == '/') relative++;
  } else {
    const char *slash = strrchr(relative, '/');
    relative = slash ? slash + 1 : relative;
  }
  size_t len = strlen(relative);
  if (len > 2 && strcmp(relative + len - 2, ".0") == 0) len -= 2;
  if (len >= 4 && strcmp(relative + len - 4, "/mod") == 0) len -= 4;
  ZBuf buf;
  zbuf_init(&buf);
  for (size_t i = 0; i < len; i++) zbuf_append_char(&buf, relative[i] == '/' ? '.' : relative[i]);
  if (buf.len == 0) zbuf_append(&buf, "main");
  return buf.data;
}

static void direct_input_push_module(SourceInput *input, const char *module, const char *path) {
  input->module_names = z_checked_reallocarray(input->module_names, input->module_count + 1, sizeof(char *));
  input->module_paths = z_checked_reallocarray(input->module_paths, input->module_count + 1, sizeof(char *));
  input->module_names[input->module_count] = z_strdup(module ? module : "");
  input->module_paths[input->module_count] = z_strdup(path ? path : "");
  input->module_count++;
}

static void direct_input_push_import(SourceInput *input, const char *module) {
  direct_input_push_string(&input->imports, &input->import_count, module);
}

static void direct_input_push_import_edge(SourceInput *input, const char *from, const char *to, const char *path, const char *source_path, int line, int column, int length) {
  input->import_from = z_checked_reallocarray(input->import_from, input->import_edge_count + 1, sizeof(char *));
  input->import_to = z_checked_reallocarray(input->import_to, input->import_edge_count + 1, sizeof(char *));
  input->import_paths = z_checked_reallocarray(input->import_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_source_paths = z_checked_reallocarray(input->import_source_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_lines = z_checked_reallocarray(input->import_lines, input->import_edge_count + 1, sizeof(int));
  input->import_columns = z_checked_reallocarray(input->import_columns, input->import_edge_count + 1, sizeof(int));
  input->import_lengths = z_checked_reallocarray(input->import_lengths, input->import_edge_count + 1, sizeof(int));
  input->import_from[input->import_edge_count] = z_strdup(from ? from : "");
  input->import_to[input->import_edge_count] = z_strdup(to ? to : "");
  input->import_paths[input->import_edge_count] = z_strdup(path ? path : "");
  input->import_source_paths[input->import_edge_count] = z_strdup(source_path ? source_path : "");
  input->import_lines[input->import_edge_count] = line > 0 ? line : 1;
  input->import_columns[input->import_edge_count] = column > 0 ? column : 1;
  input->import_lengths[input->import_edge_count] = length > 0 ? length : 1;
  input->import_edge_count++;
}

static void direct_input_add_source_metadata(SourceInput *input) {
  direct_input_push_string(&input->source_files, &input->source_file_count, input->source_file);
  char *module_name = direct_module_name_from_path(input->source_file);
  direct_input_push_module(input, module_name, input->source_file);
  free(module_name);

  int line = 1;
  direct_input_push_source_line(input, input->source_file, line);
  for (const char *cursor = input->source ? input->source : ""; *cursor; cursor++) {
    if (*cursor == '\n') direct_input_push_source_line(input, input->source_file, ++line);
  }
}

static bool load_direct_source(const char *path, SourceInput *input, ZDiag *diag) {
  input->source_file = z_strdup(path);
  input->source = z_read_file(path, diag);
  if (!input->source) return false;
  input->canonical_text_source = true;
  direct_input_add_source_metadata(input);
  return true;
}

static bool direct_input_push_symbol(SourceInput *input, const char *module, const char *kind, const char *name, bool is_public, ZDiag *diag) {
  if (is_public) {
    for (size_t i = 0; input && i < input->symbol_count; i++) {
      if (input->symbol_public[i] && strcmp(input->symbol_names[i], name ? name : "") == 0 && strcmp(input->symbol_modules[i], module ? module : "") != 0) {
        diag->code = 7003;
        diag->path = z_strdup(input->source_file ? input->source_file : "");
        diag->line = 1;
        diag->column = 1;
        diag->length = 1;
        snprintf(diag->message, sizeof(diag->message), "duplicate public symbol '%s'", name ? name : "");
        snprintf(diag->expected, sizeof(diag->expected), "unique public symbol names across imported modules");
        snprintf(diag->actual, sizeof(diag->actual), "%s also exported by %s", name ? name : "", input->symbol_modules[i]);
        snprintf(diag->help, sizeof(diag->help), "rename one symbol or keep it private inside its module");
        return false;
      }
    }
  }
  input->symbol_names = z_checked_reallocarray(input->symbol_names, input->symbol_count + 1, sizeof(char *));
  input->symbol_modules = z_checked_reallocarray(input->symbol_modules, input->symbol_count + 1, sizeof(char *));
  input->symbol_kinds = z_checked_reallocarray(input->symbol_kinds, input->symbol_count + 1, sizeof(char *));
  input->symbol_public = z_checked_reallocarray(input->symbol_public, input->symbol_count + 1, sizeof(bool));
  input->symbol_names[input->symbol_count] = z_strdup(name ? name : "");
  input->symbol_modules[input->symbol_count] = z_strdup(module ? module : "");
  input->symbol_kinds[input->symbol_count] = z_strdup(kind ? kind : "");
  input->symbol_public[input->symbol_count++] = is_public;
  return true;
}

static bool direct_source_reserved_internal_symbol(const char *name) {
  return name && strncmp(name, "__zero_", strlen("__zero_")) == 0;
}

static bool direct_source_path_has_module_suffix(const char *path, const char *module_path) {
  if (!path || !module_path) return false;
  size_t path_len = strlen(path);
  size_t module_len = strlen(module_path);
  if (path_len < module_len) return false;
  const char *suffix = path + path_len - module_len;
  for (size_t i = 0; i < module_len; i++) {
    char have = suffix[i];
    char want = module_path[i];
    if ((have == '/' || have == '\\') && (want == '/' || want == '\\')) continue;
    if (have != want) return false;
  }
  if (path_len == module_len) return true;
  char before = path[path_len - module_len - 1];
  return before == '/' || before == '\\';
}

static const ZStdSourceModule *direct_std_source_module_for_file(const char *path) {
  for (size_t i = 0; i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *module = z_std_source_module_at(i);
    if (!module || !direct_source_path_has_module_suffix(path, module->path)) continue;
    return module;
  }
  return NULL;
}

static bool direct_source_is_embedded_std_source_file(const char *path) {
  return direct_std_source_module_for_file(path) != NULL;
}

static bool direct_input_add_program_symbol(SourceInput *input, const char *module, const char *kind, const char *name, bool is_public, bool allow_internal_names, int line, int column, ZDiag *diag) {
  if (!name || !name[0]) return true;
  if (!allow_internal_names && direct_source_reserved_internal_symbol(name)) {
    diag->code = 3008;
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->length = (int)strlen(name);
    if (diag->length <= 0) diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "reserved compiler-internal symbol name");
    snprintf(diag->expected, sizeof(diag->expected), "top-level %s name without the __zero_ prefix", kind);
    snprintf(diag->actual, sizeof(diag->actual), "%s", name);
    snprintf(diag->help, sizeof(diag->help), "rename the symbol; __zero_ names are reserved for compiler-provided helpers");
    return false;
  }
  return direct_input_push_symbol(input, module, kind, name, is_public, diag);
}

static bool direct_input_add_program_symbols(SourceInput *input, const Program *program, const char *module, bool allow_internal_names, ZDiag *diag) {
  for (size_t i = 0; program && i < program->aliases.len; i++) {
    TypeAlias *item = &program->aliases.items[i];
    if (!direct_input_add_program_symbol(input, module, "type-alias", item->name, item->is_public, allow_internal_names, item->line, item->column, diag)) return false;
  }
  for (size_t i = 0; program && i < program->consts.len; i++) {
    ConstDecl *item = &program->consts.items[i];
    if (!direct_input_add_program_symbol(input, module, "const", item->name, item->is_public, allow_internal_names, item->line, item->column, diag)) return false;
  }
  for (size_t i = 0; program && i < program->interfaces.len; i++) {
    InterfaceDecl *item = &program->interfaces.items[i];
    if (!direct_input_add_program_symbol(input, module, "interface", item->name, item->is_public, allow_internal_names, item->line, item->column, diag)) return false;
  }
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    Shape *item = &program->shapes.items[i];
    if (!direct_input_add_program_symbol(input, module, "shape", item->name, item->is_public, allow_internal_names, item->line, item->column, diag)) return false;
  }
  for (size_t i = 0; program && i < program->enums.len; i++) {
    EnumDecl *item = &program->enums.items[i];
    if (!direct_input_add_program_symbol(input, module, "enum", item->name, item->is_public, allow_internal_names, item->line, item->column, diag)) return false;
  }
  for (size_t i = 0; program && i < program->choices.len; i++) {
    Choice *item = &program->choices.items[i];
    if (!direct_input_add_program_symbol(input, module, "choice", item->name, item->is_public, allow_internal_names, item->line, item->column, diag)) return false;
  }
  for (size_t i = 0; program && i < program->functions.len; i++) {
    Function *item = &program->functions.items[i];
    if (item->is_test) continue;
    if (!direct_input_add_program_symbol(input, module, "function", item->name, item->is_public, allow_internal_names, item->line, item->column, diag)) return false;
  }
  return !diag || diag->code == 0;
}

static bool direct_canonical_append_std_source_function(SourceInput *input, ZBuf *combined, const ZStdSourceModule *module, const char *target_name, ZDiag *diag);

static bool direct_input_has_function_symbol(const SourceInput *input, const char *module, const char *name) {
  for (size_t i = 0; input && i < input->symbol_count; i++) {
    if (strcmp(input->symbol_modules[i], module ? module : "") == 0 &&
        strcmp(input->symbol_kinds[i], "function") == 0 &&
        strcmp(input->symbol_names[i], name ? name : "") == 0) return true;
  }
  return false;
}

static void direct_input_ensure_std_source_module(SourceInput *input, const ZStdSourceModule *module) {
  if (!input || !module || direct_input_has_file(input, module->path)) return;
  direct_input_push_string(&input->source_files, &input->source_file_count, module->path);
  direct_input_push_module(input, module->module, module->path);
}

static bool direct_expr_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const Expr *expr, const ZStdSourceModule *owner, const ZStdSourceModule *current_module, ZDiag *diag);
static bool direct_stmt_vec_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const StmtVec *body, const ZStdSourceModule *owner, const ZStdSourceModule *current_module, ZDiag *diag);

static bool direct_params_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const ParamVec *params, const ZStdSourceModule *owner, const ZStdSourceModule *current_module, ZDiag *diag) {
  for (size_t i = 0; params && i < params->len; i++) {
    if (!direct_expr_append_referenced_std_sources(input, combined, params->items[i].default_value, owner, current_module, diag)) return false;
  }
  return !diag || diag->code == 0;
}

static bool direct_function_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const Function *fun, const ZStdSourceModule *owner, const ZStdSourceModule *current_module, ZDiag *diag) {
  return fun &&
         direct_params_append_referenced_std_sources(input, combined, &fun->type_params, owner, current_module, diag) &&
         direct_params_append_referenced_std_sources(input, combined, &fun->params, owner, current_module, diag) &&
         direct_params_append_referenced_std_sources(input, combined, &fun->errors, owner, current_module, diag) &&
         direct_stmt_vec_append_referenced_std_sources(input, combined, &fun->body, owner, current_module, diag);
}

static bool direct_function_vec_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const FunctionVec *functions, const ZStdSourceModule *owner, const ZStdSourceModule *current_module, ZDiag *diag) {
  for (size_t i = 0; functions && i < functions->len; i++) {
    if (!direct_function_append_referenced_std_sources(input, combined, &functions->items[i], owner, current_module, diag)) return false;
  }
  return !diag || diag->code == 0;
}

static bool direct_expr_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const Expr *expr, const ZStdSourceModule *owner, const ZStdSourceModule *current_module, ZDiag *diag) {
  if (!expr || (diag && diag->code != 0)) return !diag || diag->code == 0;
  if (expr->kind == EXPR_CALL) {
    char *callee = expr_callee_name(expr->left);
    const ZStdSourceModule *callee_module = z_std_source_module_for_public_call(callee);
    const char *target_name = z_std_source_target_for_public_call(callee);
    bool skip_owner = owner && !current_module && callee_module && strcmp(owner->module, callee_module->module) == 0;
    if (callee_module && target_name && !skip_owner) {
      bool ok = direct_canonical_append_std_source_function(input, combined, callee_module, target_name, diag);
      free(callee);
      if (!ok) return false;
    } else if (current_module && callee && strncmp(callee, "__zero_", strlen("__zero_")) == 0) {
      bool ok = direct_canonical_append_std_source_function(input, combined, current_module, callee, diag);
      free(callee);
      if (!ok) return false;
    } else {
      free(callee);
    }
  }
  if (!direct_expr_append_referenced_std_sources(input, combined, expr->left, owner, current_module, diag) ||
      !direct_expr_append_referenced_std_sources(input, combined, expr->right, owner, current_module, diag)) return false;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (!direct_expr_append_referenced_std_sources(input, combined, expr->args.items[i], owner, current_module, diag)) return false;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (!direct_expr_append_referenced_std_sources(input, combined, expr->fields.items[i].value, owner, current_module, diag)) return false;
  }
  return !diag || diag->code == 0;
}

static bool direct_stmt_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const Stmt *stmt, const ZStdSourceModule *owner, const ZStdSourceModule *current_module, ZDiag *diag) {
  if (!stmt) return true;
  if (!direct_expr_append_referenced_std_sources(input, combined, stmt->target, owner, current_module, diag) ||
      !direct_expr_append_referenced_std_sources(input, combined, stmt->expr, owner, current_module, diag) ||
      !direct_expr_append_referenced_std_sources(input, combined, stmt->range_end, owner, current_module, diag) ||
      !direct_stmt_vec_append_referenced_std_sources(input, combined, &stmt->then_body, owner, current_module, diag) ||
      !direct_stmt_vec_append_referenced_std_sources(input, combined, &stmt->else_body, owner, current_module, diag)) return false;
  for (size_t i = 0; i < stmt->match_arms.len; i++) {
    if (!direct_expr_append_referenced_std_sources(input, combined, stmt->match_arms.items[i].guard, owner, current_module, diag) ||
        !direct_stmt_vec_append_referenced_std_sources(input, combined, &stmt->match_arms.items[i].body, owner, current_module, diag)) return false;
  }
  return !diag || diag->code == 0;
}

static bool direct_stmt_vec_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const StmtVec *body, const ZStdSourceModule *owner, const ZStdSourceModule *current_module, ZDiag *diag) {
  for (size_t i = 0; body && i < body->len; i++) {
    if (!direct_stmt_append_referenced_std_sources(input, combined, body->items[i], owner, current_module, diag)) return false;
  }
  return !diag || diag->code == 0;
}

static bool direct_program_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const Program *program, const ZStdSourceModule *owner, ZDiag *diag) {
  for (size_t i = 0; program && i < program->consts.len; i++) {
    if (!direct_expr_append_referenced_std_sources(input, combined, program->consts.items[i].expr, owner, NULL, diag)) return false;
  }
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    if (!direct_params_append_referenced_std_sources(input, combined, &program->shapes.items[i].type_params, owner, NULL, diag) ||
        !direct_params_append_referenced_std_sources(input, combined, &program->shapes.items[i].fields, owner, NULL, diag) ||
        !direct_function_vec_append_referenced_std_sources(input, combined, &program->shapes.items[i].methods, owner, NULL, diag)) return false;
  }
  for (size_t i = 0; program && i < program->interfaces.len; i++) {
    if (!direct_params_append_referenced_std_sources(input, combined, &program->interfaces.items[i].type_params, owner, NULL, diag) ||
        !direct_function_vec_append_referenced_std_sources(input, combined, &program->interfaces.items[i].methods, owner, NULL, diag)) return false;
  }
  return direct_function_vec_append_referenced_std_sources(input, combined, program ? &program->functions : NULL, owner, NULL, diag);
}

static bool direct_canonical_append_std_source_function(SourceInput *input, ZBuf *combined, const ZStdSourceModule *module, const char *target_name, ZDiag *diag) {
  (void)combined;
  if (!module || !target_name || !target_name[0]) return true;
  if (input && input->source_file &&
      direct_source_path_has_module_suffix(input->source_file, module->path) &&
      direct_source_is_embedded_std_source_file(input->source_file)) {
    return true;
  }
  if (direct_input_has_function_symbol(input, module->module, target_name)) return true;

  direct_input_ensure_std_source_module(input, module);
  return direct_input_add_program_symbol(input, module->module, "function", target_name, false, true, 1, 1, diag);
}

static bool direct_canonical_append_referenced_std_sources(SourceInput *input, ZBuf *combined, const Program *program, const ZStdSourceModule *owner, ZDiag *diag) {
  return direct_program_append_referenced_std_sources(input, combined, program, owner, diag);
}

static char *direct_canonical_module_path(const char *root, const char *module) {
  ZBuf relative;
  zbuf_init(&relative);
  for (const char *cursor = module ? module : ""; *cursor; cursor++) {
    zbuf_append_char(&relative, *cursor == '.' ? '/' : *cursor);
  }
  zbuf_append(&relative, ".0");
  char *file_path = direct_join_path(root, relative.data);
  if (direct_file_exists(file_path)) {
    zbuf_free(&relative);
    return file_path;
  }
  free(file_path);
  if (relative.len >= 2 && strcmp(relative.data + relative.len - 2, ".0") == 0) {
    relative.data[relative.len - 2] = 0;
    relative.len -= 2;
  }
  char *dir_path = direct_join_path(root, relative.data);
  char *mod_path = direct_join_path(dir_path, "mod.0");
  free(dir_path);
  zbuf_free(&relative);
  if (direct_file_exists(mod_path)) return mod_path;
  free(mod_path);
  return NULL;
}

static bool direct_canonical_identifier_text(const char *text) {
  if (!text || !text[0]) return false;
  if (!(isalpha((unsigned char)text[0]) || text[0] == '_')) return false;
  for (size_t i = 1; text[i]; i++) {
    if (!(isalnum((unsigned char)text[i]) || text[i] == '_')) return false;
  }
  return !direct_source_reserved_word(text);
}

static bool direct_canonical_validate_import_module(const UseImport *item, ZDiag *diag) {
  const char *module = item ? item->module : NULL;
  if (!module || !module[0]) {
    diag->code = 100;
    diag->line = item && item->line > 0 ? item->line : 1;
    diag->column = item && item->column > 0 ? item->column : 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "expected import module name");
    snprintf(diag->expected, sizeof(diag->expected), "import module name");
    return false;
  }

  size_t segment_start = 0;
  for (size_t i = 0;; i++) {
    if (module[i] != '.' && module[i] != 0) continue;
    size_t segment_len = i - segment_start;
    char *segment = z_strndup(module + segment_start, segment_len);
    bool ok = direct_canonical_identifier_text(segment);
    free(segment);
    if (!ok) {
      diag->code = 100;
      diag->line = item->line > 0 ? item->line : 1;
      diag->column = item->column > 0 ? item->column : 1;
      diag->length = item->end_column > item->column ? item->end_column - item->column : 1;
      snprintf(diag->message, sizeof(diag->message), segment_start == 0 ? "expected import module name" : "expected import module segment");
      snprintf(diag->expected, sizeof(diag->expected), segment_start == 0 ? "import module name" : "import module segment");
      return false;
    }
    if (module[i] == 0) break;
    segment_start = i + 1;
  }
  return true;
}

static bool direct_source_reserved_word(const char *text) {
  const char *keywords[] = {
    "as", "break", "check", "choice", "const", "continue", "defer", "else", "enum", "export", "extern", "false",
    "fn", "for", "fun", "if", "import", "in", "let", "match", "meta", "mut", "null", "packed", "pub",
    "raise", "raises", "rescue", "ret", "return", "shape", "static", "test", "true", "type",
    "use", "var", "while", NULL
  };
  for (int i = 0; keywords[i]; i++) {
    if (text && strcmp(text, keywords[i]) == 0) return true;
  }
  return false;
}

static bool direct_source_stack_contains(char **stack, size_t len, const char *path) {
  for (size_t i = 0; i < len; i++) {
    if (strcmp(stack[i], path) == 0) return true;
  }
  return false;
}

static void direct_source_format_cycle_chain(const char *root, char **stack, size_t stack_len, const char *path, ZBuf *out) {
  bool started = false;
  for (size_t i = 0; i < stack_len; i++) {
    if (!started && strcmp(stack[i], path) != 0) continue;
    started = true;
    if (out->len > 0) zbuf_append(out, " -> ");
    char *module = direct_source_module_name_from_path(root, stack[i]);
    zbuf_append(out, module);
    free(module);
  }
  if (out->len > 0) zbuf_append(out, " -> ");
  char *module = direct_source_module_name_from_path(root, path);
  zbuf_append(out, module);
  free(module);
}

static void direct_source_set_cycle_diag(ZDiag *diag, const char *root, char **stack, size_t stack_len, const char *cycle_path, const char *diag_path, int line, int column, int length) {
  ZBuf chain;
  zbuf_init(&chain);
  direct_source_format_cycle_chain(root, stack, stack_len, cycle_path, &chain);
  diag->code = 7002;
  diag->path = z_strdup(diag_path ? diag_path : "");
  diag->line = line > 0 ? line : 1;
  diag->column = column > 0 ? column : 1;
  diag->length = length > 0 ? length : 1;
  snprintf(diag->message, sizeof(diag->message), "import cycle detected");
  snprintf(diag->actual, sizeof(diag->actual), "%s", chain.data ? chain.data : "");
  snprintf(diag->help, sizeof(diag->help), "break the module cycle by moving shared declarations into a third module");
  zbuf_free(&chain);
}

static void direct_source_append(SourceInput *input, ZBuf *combined, const char *path, const char *source) {
  const char *line = source ? source : "";
  int original_line = 1;
  if (!*line) {
    zbuf_append_char(combined, '\n');
    direct_input_push_source_line(input, path, original_line);
    return;
  }
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    zbuf_appendf(combined, "%.*s\n", (int)len, line);
    direct_input_push_source_line(input, path, original_line++);
    if (!end) break;
    line = end + 1;
  }
  zbuf_append_char(combined, '\n');
  direct_input_push_source_line(input, path, original_line);
}

static bool direct_canonical_resolve_file(const char *path, const char *root, SourceInput *input, ZBuf *combined, ZDiag *diag, char ***stack, size_t *stack_len) {
  if (direct_input_has_file(input, path)) return true;
  if (direct_source_stack_contains(*stack, *stack_len, path)) {
    direct_source_set_cycle_diag(diag, root, *stack, *stack_len, path, path, 1, 1, 1);
    return false;
  }

  char *source = z_read_file(path, diag);
  if (!source) return false;
  char *module = direct_source_module_name_from_path(root, path);
  *stack = z_checked_reallocarray(*stack, *stack_len + 1, sizeof(char *));
  (*stack)[(*stack_len)++] = z_strdup(path);

  ZCanonicalTokenVec tokens = z_canonical_text_tokenize(source, diag);
  Program program = {0};
  if (diag->code == 0) program = z_parse_canonical_text_program(&tokens, diag);
  if (diag->code != 0) {
    if (!diag->path) diag->path = z_strdup(path);
    z_free_program(&program);
    z_free_canonical_text_tokens(&tokens);
    free((*stack)[--(*stack_len)]);
    free(module);
    free(source);
    return false;
  }

  const ZStdSourceModule *std_owner = direct_std_source_module_for_file(path);
  if (!direct_canonical_append_referenced_std_sources(input, combined, &program, std_owner, diag)) {
    if (!diag->path) diag->path = z_strdup(path);
  }

  for (size_t i = 0; i < program.use_imports.len && diag->code == 0; i++) {
    const UseImport *item = &program.use_imports.items[i];
    if (!direct_canonical_validate_import_module(item, diag)) {
      if (!diag->path) diag->path = z_strdup(path);
      break;
    }
    if (strncmp(item->module, "std.", 4) == 0) continue;
    direct_input_push_import(input, item->module);
    char *import_path = direct_canonical_module_path(root, item->module);
    if (!import_path) {
      diag->code = 7001;
      diag->path = z_strdup(path);
      diag->line = item->line > 0 ? item->line : 1;
      diag->column = item->column > 0 ? item->column : 1;
      diag->length = item->end_column > item->column ? item->end_column - item->column : 1;
      snprintf(diag->message, sizeof(diag->message), "unknown package-local import '%s'", item->module);
      snprintf(diag->expected, sizeof(diag->expected), "%s.0 or %s/mod.0", item->module, item->module);
      snprintf(diag->actual, sizeof(diag->actual), "missing source file");
      snprintf(diag->help, sizeof(diag->help), "create the module source file or remove the import");
      break;
    }
    int import_length = item->end_column > item->column ? item->end_column - item->column : 1;
    if (direct_source_stack_contains(*stack, *stack_len, import_path)) {
      direct_source_set_cycle_diag(diag, root, *stack, *stack_len, import_path, path, item->line, item->column, import_length);
      free(import_path);
      break;
    }
    direct_input_push_import_edge(input, module, item->module, import_path, path, item->line, item->column, import_length);
    bool ok = direct_canonical_resolve_file(import_path, root, input, combined, diag, stack, stack_len);
    free(import_path);
    if (!ok) break;
  }

  if (diag->code == 0) {
    direct_input_push_string(&input->source_files, &input->source_file_count, path);
    direct_input_push_module(input, module, path);
    bool allow_internal_names = direct_source_is_embedded_std_source_file(path);
    if (allow_internal_names && input->source_file && strcmp(input->source_file, path) == 0) input->allow_missing_main = true;
    if (direct_input_add_program_symbols(input, &program, module, allow_internal_names, diag)) {
      direct_source_append(input, combined, path, source);
    } else if (!diag->path) {
      diag->path = z_strdup(path);
    }
  }

  z_free_program(&program);
  z_free_canonical_text_tokens(&tokens);
  free((*stack)[--(*stack_len)]);
  free(module);
  free(source);
  return diag->code == 0;
}

static bool resolve_direct_canonical_source_at_root(const char *path, const char *root, SourceInput *input, ZDiag *diag) { input->source_file = z_strdup(path); input->canonical_text_source = true; ZBuf combined; zbuf_init(&combined); char **stack = NULL; size_t stack_len = 0; bool ok = direct_canonical_resolve_file(path, root, input, &combined, diag, &stack, &stack_len); free(stack); input->source = ok ? combined.data : NULL; if (!ok) zbuf_free(&combined); return ok; }

static bool resolve_direct_canonical_source(const char *path, SourceInput *input, ZDiag *diag) {
  char *root = direct_dirname_of(path);
  bool ok = resolve_direct_canonical_source_at_root(path, root, input, diag);
  free(root);
  return ok;
}

static bool resolve_direct_package_source(const char *input_path, SourceInput *input, ZDiag *diag, bool *handled) {
  *handled = false;
  char *manifest_path = z_manifest_path_for_input(input_path);
  if (!manifest_path) return false;
  *handled = true;

  char *manifest = z_read_file(manifest_path, diag);
  if (!manifest) {
    diag->path = z_strdup(manifest_path);
    free(manifest_path);
    return false;
  }
  ZManifest parsed_manifest = {0};
  if (!z_parse_manifest_json(manifest, &parsed_manifest, diag)) {
    diag->path = z_strdup(manifest_path);
    free(manifest);
    free(manifest_path);
    return false;
  }
  if (!parsed_manifest.main_path || !is_zero_source_path(parsed_manifest.main_path)) {
    diag->code = 2002;
    diag->path = z_strdup(manifest_path);
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "target main source must use canonical Zero source");
    snprintf(diag->expected, sizeof(diag->expected), ".0 source file");
    snprintf(diag->actual, sizeof(diag->actual), "%s", parsed_manifest.main_path ? parsed_manifest.main_path : "missing targets.cli.main");
    snprintf(diag->help, sizeof(diag->help), "set targets.cli.main to a canonical .0 source file");
    z_free_manifest(&parsed_manifest);
    free(manifest);
    free(manifest_path);
    return false;
  }

  bool ok = z_resolve_package_metadata(manifest_path, manifest, &parsed_manifest, input, diag);
  if (ok) {
    char *source_file = direct_join_path(input->package_root, parsed_manifest.main_path);
    if (!direct_file_exists(source_file)) {
      diag->code = 2002;
      diag->path = input->manifest_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "target main source does not exist");
      snprintf(diag->expected, sizeof(diag->expected), "%s", source_file);
      snprintf(diag->actual, sizeof(diag->actual), "missing source file");
      snprintf(diag->help, sizeof(diag->help), "create the main source file or update targets.cli.main");
      ok = false;
    } else {
      char *src_root = direct_join_path(input->package_root, "src");
      ok = resolve_direct_canonical_source_at_root(source_file, src_root, input, diag);
      free(src_root);
    }
    free(source_file);
  }

  z_free_manifest(&parsed_manifest);
  free(manifest);
  free(manifest_path);
  return ok;
}

static void set_source_input_diag(const char *input_path, ZDiag *diag) {
  diag->code = 2002;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "expected Zero source file or package");
  snprintf(diag->expected, sizeof(diag->expected), ".0 source file, zero.toml, zero.json, or package directory");
  snprintf(diag->actual, sizeof(diag->actual), "%s", input_path ? input_path : "");
  snprintf(diag->help, sizeof(diag->help), "pass a canonical .0 source file or a package with targets.cli.main");
}

static bool load_command_source(const char *input_path, SourceInput *input, ZDiag *diag) {
  if (is_zero_source_path(input_path)) {
    return load_direct_source(input_path, input, diag);
  }
  bool handled_package = false;
  if (resolve_direct_package_source(input_path, input, diag, &handled_package)) {
    return true;
  }
  if (handled_package) return false;
  set_source_input_diag(input_path, diag);
  return false;
}

static bool load_format_source(const char *input_path, SourceInput *input, ZDiag *diag) {
  if (is_zero_source_path(input_path)) return load_direct_source(input_path, input, diag);
  SourceInput resolved = {0};
  if (!load_command_source(input_path, &resolved, diag)) return false;
  char *source_file = z_strdup(resolved.source_file ? resolved.source_file : input_path);
  z_free_source(&resolved);
  bool ok = load_direct_source(source_file, input, diag);
  free(source_file);
  return ok;
}

static char *resolve_c_import_header_path(const SourceInput *input, const char *header) {
  if (!header || !header[0]) return z_strdup(header ? header : "");
  if (runtime_path_is_absolute(header)) return z_strdup(header);
  if (input && input->package_root && input->package_root[0]) {
    char *package_path = direct_join_path(input->package_root, header);
    if (direct_file_exists(package_path)) return package_path;
    free(package_path);
  }
  if (input && input->source_file && input->source_file[0]) {
    char *dir = direct_dirname_of(input->source_file);
    char *source_path = direct_join_path(dir, header);
    free(dir);
    if (direct_file_exists(source_path)) return source_path;
    free(source_path);
  }
  if (direct_file_exists(header)) return z_strdup(header);
  return z_strdup(header);
}

static void resolve_program_c_import_header_paths(const SourceInput *input, Program *program) {
  for (size_t i = 0; program && i < program->c_imports.len; i++) {
    CImport *item = &program->c_imports.items[i];
    char *resolved = resolve_c_import_header_path(input, item->header);
    free(item->resolved_header);
    item->resolved_header = resolved;
  }
}

static bool finish_checked_source_input(const ZTargetInfo *target, const char *profile, SourceInput *input, Program *program, ZDiag *diag, long long parse_ms) {
  resolve_program_c_import_header_paths(input, program);
  input->parse_ms = parse_ms;
  input->interface_ms = input->resolve_ms;
  input->parse_cache_hit = compiler_cache_touch("parse-tree", compile_cache_key(input, NULL, NULL, "parse-tree"));
  input->interface_cache_hit = compiler_cache_touch("interface", source_interface_hash(input));
  input->check_cache_hit = compiler_cache_touch("checked-body", compile_cache_key(input, target, NULL, "checked-body"));
  input->specialization_cache_hit = compiler_cache_touch("specialization", compile_cache_key(input, target, profile ? profile : "release", "specialization"));
  z_set_check_target(target);
  long long phase_started = now_ms();
  bool checked = input->allow_missing_main ? z_check_program_library(program, diag) : z_check_program(program, diag);
  if (!checked) {
    z_map_source_diag(input, diag);
    return false;
  }
  input->check_ms = now_ms() - phase_started;
  return true;
}

static bool try_check_canonical_text_input(const ZTargetInfo *target, const char *profile, SourceInput *input, Program *program, ZDiag *diag, long long resolve_started_ms, bool *parsed) {
  if (parsed) *parsed = false;
  input->resolve_ms = now_ms() - resolve_started_ms;
  diag->path = input->source_file;
  long long phase_started = now_ms();
  if (!z_parse_canonical_text_program_source(input->source, program, diag)) {
    z_free_program(program);
    memset(program, 0, sizeof(*program));
    return false;
  }
  input->canonical_text_source = true;
  if (parsed) *parsed = true;
  return finish_checked_source_input(target, profile, input, program, diag, now_ms() - phase_started);
}

static bool compile_input(const char *input_path, const ZTargetInfo *target, const char *profile, SourceInput *input, Program *program, ZDiag *diag) {
  long long phase_started = now_ms();
  bool handled_package = false;
  if (resolve_direct_package_source(input_path, input, diag, &handled_package)) {
    bool parsed_canonical = false;
    if (try_check_canonical_text_input(target, profile, input, program, diag, phase_started, &parsed_canonical)) return true;
    if (!parsed_canonical) z_map_source_diag(input, diag);
    return false;
  }
  if (handled_package) return false;

  if (is_zero_source_path(input_path)) {
    ZDiag canonical_diag = {0};
    bool parsed_canonical = false;
    if (!resolve_direct_canonical_source(input_path, input, diag)) return false;
    if (try_check_canonical_text_input(target, profile, input, program, &canonical_diag, phase_started, &parsed_canonical)) return true;
    if (!parsed_canonical) z_map_source_diag(input, &canonical_diag);
    if (diag) *diag = canonical_diag;
    return false;
  }

  set_source_input_diag(input_path, diag);
  return false;
}

static bool has_suffix(const char *path, const char *suffix) {
  size_t path_len = strlen(path);
  size_t suffix_len = strlen(suffix);
  return path_len >= suffix_len && strcmp(path + path_len - suffix_len, suffix) == 0;
}

static char *apply_target_suffix(const char *path, const ZTargetInfo *target) {
  const char *suffix = target ? target->exe_suffix : "";
  if (suffix && suffix[0] && !has_suffix(path, suffix)) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, path);
    zbuf_append(&buf, suffix);
    return buf.data;
  }
  return z_strdup(path);
}

static void complete_backend_blocker_diag(ZDiag *diag, const ZTargetInfo *target, const Command *command, const char *emit_kind, const char *stage) {
  if (!diag || (diag->code != 4004 && diag->code != 2004)) return;
  const char *blocker_stage = diag->backend_blocker.present && diag->backend_blocker.stage[0] ? diag->backend_blocker.stage : stage;
  const char *unsupported_feature = diag->backend_blocker.present && diag->backend_blocker.unsupported_feature[0] ? diag->backend_blocker.unsupported_feature : diag->actual;
  const char *backend = z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind)
    ? "llvm"
    : z_direct_backend_name_for_emit_kind(target, emit_kind, z_backend_direct_request_name(command ? command->backend : NULL));
  ZBackendBlocker blocker;
  z_backend_blocker_set(&blocker,
                        target && target->name ? target->name : "unknown",
                        target && target->object_format ? target->object_format : "unknown",
                        backend,
                        blocker_stage && blocker_stage[0] ? blocker_stage : "emit",
                        unsupported_feature && unsupported_feature[0] ? unsupported_feature : "unsupported construct");
  z_diag_set_backend_blocker(diag, &blocker);
}

static void init_direct_backend_diag(ZDiag *diag, const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const char *reason) {
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->path = input ? input->source_file : NULL;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "direct backend does not support target '%s' for --emit %s",
           target && target->name ? target->name : "unknown",
           emit_kind ? emit_kind : "unknown");
  snprintf(diag->expected, sizeof(diag->expected), "direct target with matching object format and architecture");
  snprintf(diag->actual, sizeof(diag->actual), "target=%s objectFormat=%s arch=%s abi=%s status=%s",
           target && target->name ? target->name : "unknown",
           target && target->object_format ? target->object_format : "unknown",
           target && target->arch ? target->arch : "unknown",
           target && target->abi ? target->abi : "",
           z_direct_backend_status(target));
  snprintf(diag->help, sizeof(diag->help), "%s", reason ? reason : z_direct_backend_reason(target));
  complete_backend_blocker_diag(diag, target, command, emit_kind, "select");
}

static void init_direct_backend_request_mismatch_diag(ZDiag *diag, const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind) {
  const char *requested = z_backend_direct_request_name(command ? command->backend : NULL);
  const char *selected_request = z_direct_object_emitter(target);
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->path = input ? input->source_file : NULL;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "direct emitter '%s' does not match target '%s' for --emit %s",
           requested ? requested : "",
           target && target->name ? target->name : "unknown",
           emit_kind ? emit_kind : "unknown");
  snprintf(diag->expected, sizeof(diag->expected), "direct emitter %s for target %s",
           selected_request && selected_request[0] ? selected_request : "none",
           target && target->name ? target->name : "unknown");
  snprintf(diag->actual, sizeof(diag->actual), "--backend %s", requested ? requested : "");
  snprintf(diag->help, sizeof(diag->help), "use --backend direct or --backend %s for this target", selected_request && selected_request[0] ? selected_request : "direct");
  ZBackendBlocker blocker;
  z_backend_blocker_set(&blocker,
                        target && target->name ? target->name : "unknown",
                        target && target->object_format ? target->object_format : "unknown",
                        requested ? requested : "direct",
                        "target-selection",
                        "requested direct emitter does not match target");
  z_diag_set_backend_blocker(diag, &blocker);
}

static void init_direct_llvm_ir_unavailable_diag(ZDiag *diag, const Command *command, const ZTargetInfo *target, const char *path) {
  const char *requested = z_backend_direct_request_name(command ? command->backend : NULL);
  const char *backend = requested ? requested : "direct";
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->path = path;
  diag->line = 1; diag->column = 1; diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "direct backend does not support --emit llvm-ir");
  snprintf(diag->expected, sizeof(diag->expected), "LLVM backend for --emit llvm-ir");
  snprintf(diag->actual, sizeof(diag->actual), "backend=%s emit=llvm-ir", backend);
  snprintf(diag->help, sizeof(diag->help), "use --backend llvm --emit llvm-ir to write textual LLVM IR");
  ZBackendBlocker blocker;
  z_backend_blocker_set(&blocker,
                        target && target->name ? target->name : "unknown",
                        target && target->object_format ? target->object_format : "unknown",
                        backend,
                        "buildability",
                        "llvm-ir");
  z_diag_set_backend_blocker(diag, &blocker);
}

static void init_llvm_ir_build_only_diag(ZDiag *diag, const Command *command, const ZTargetInfo *target, const char *path) {
  char command_name[64];
  if (command && command->command && command->kind) snprintf(command_name, sizeof(command_name), "%s %s", command->command, command->kind);
  else snprintf(command_name, sizeof(command_name), "%s", command && command->command ? command->command : "command");
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "LLVM IR emission writes artifacts only through zero build");
  snprintf(diag->expected, sizeof(diag->expected), "zero build --backend llvm --emit llvm-ir");
  snprintf(diag->actual, sizeof(diag->actual), "%s --backend llvm --emit llvm-ir", command_name);
  snprintf(diag->help, sizeof(diag->help), "use zero build for textual LLVM IR, or use zero run --backend llvm --emit exe for native LLVM execution");
  ZBackendBlocker blocker;
  z_backend_blocker_set(&blocker,
                        target && target->name ? target->name : "unknown",
                        target && target->object_format ? target->object_format : "unknown",
                        "llvm",
                        "buildability",
                        "llvm-ir command");
  z_diag_set_backend_blocker(diag, &blocker);
}

static bool command_is_size_metadata_report(const Command *command) {
  if (!command || !command->command) return false;
  if (strcmp(command->command, "size") == 0) return true;
  return strcmp(command->command, "graph") == 0 && command->kind && strcmp(command->kind, "size") == 0;
}

static bool metadata_backend_request_buildable(const Command *command, const SourceInput *input, const ZTargetInfo *target, ZDiag *diag) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  const char *emit_kind = emit_kind_name(emit);
  const char *path = input && input->source_file ? input->source_file : (command ? command->input : NULL);
  if (emit == EMIT_LLVM_IR) {
    if (z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind)) init_llvm_ir_build_only_diag(diag, command, target, path);
    else init_direct_llvm_ir_unavailable_diag(diag, command, target, path);
    return false;
  }
  if (z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind)) {
    if (emit == EMIT_EXE && command_is_size_metadata_report(command)) return true;
    if (emit == EMIT_EXE) {
      z_backend_init_llvm_unavailable_diag(diag, target, emit_kind, path);
      snprintf(diag->message, sizeof(diag->message), "LLVM native executable metadata is not available for this command yet");
      snprintf(diag->expected, sizeof(diag->expected), "zero check/build/run --backend llvm --emit exe");
      snprintf(diag->actual, sizeof(diag->actual), "%s --backend llvm --emit exe", command && command->command ? command->command : "command");
      snprintf(diag->help, sizeof(diag->help), "use zero check, zero build, or zero run for native LLVM executable readiness");
      z_backend_blocker_set(&diag->backend_blocker,
                            target && target->name ? target->name : "unknown",
                            target && target->object_format ? target->object_format : "unknown",
                            "llvm",
                            "buildability",
                            "llvm metadata command");
      return false;
    }
    z_backend_init_llvm_unavailable_diag(diag, target, emit_kind, path);
    return false;
  }
  const char *direct_request = z_backend_direct_request_name(command ? command->backend : NULL);
  if (direct_request) {
    ZDirectObjectTargetFacts direct_obj = z_direct_object_target_facts(target);
    if (!z_direct_requested_backend_matches(direct_request, direct_obj.backend)) {
      init_direct_backend_request_mismatch_diag(diag, command, input, target, emit_kind);
      return false;
    }
  }
  return true;
}

static int return_direct_backend_error(const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const char *reason, IrProgram *ir, Program *program) {
  ZDiag diag;
  init_direct_backend_diag(&diag, command, input, target, emit_kind, reason);
  if (command && command->json) print_diag_json(input ? input->source_file : NULL, &diag);
  else print_diag(input ? input->source_file : NULL, &diag);
  if (ir) z_free_ir_program(ir);
  if (program) z_free_program(program);
  return 1;
}

static int return_buildability_error(const Command *command, const SourceInput *input, ZDiag *diag, IrProgram *ir, Program *program) {
  if (input && diag) {
    z_map_source_diag(input, diag);
    if (!diag->path) diag->path = input->source_file;
  }
  if (command && command->json) print_diag_json(input ? input->source_file : NULL, diag);
  else print_diag(input ? input->source_file : NULL, diag);
  if (ir) z_free_ir_program(ir);
  if (program) z_free_program(program);
  return 1;
}

static void free_loaded_command_state(SourceInput *input, Program *program, IrProgram *ir) {
  if (ir) z_free_ir_program(ir);
  z_free_program(program);
  z_free_source(input);
}

static bool source_input_contains_path_pointer(const SourceInput *input, const char *path) {
  if (!input || !path) return false;
  if (path == input->source_file || path == input->package_root || path == input->manifest_path ||
      path == input->lockfile_path || path == input->program_graph_hash || path == input->program_graph_module_identity ||
      path == input->mapped_mir_cache_path) {
    return true;
  }
  for (size_t i = 0; i < input->source_file_count; i++) if (path == input->source_files[i]) return true;
  for (size_t i = 0; i < input->source_line_count; i++) if (path == input->source_line_paths[i]) return true;
  for (size_t i = 0; i < input->module_count; i++) if (path == input->module_paths[i]) return true;
  for (size_t i = 0; i < input->import_edge_count; i++) {
    if (path == input->import_paths[i] || path == input->import_source_paths[i]) return true;
  }
  for (size_t i = 0; i < input->dependency_count; i++) {
    if (path == input->dependencies[i].path || path == input->dependencies[i].resolved_manifest) return true;
  }
  for (size_t i = 0; i < input->direct_c_import_header_count; i++) {
    if (path == input->direct_c_import_headers[i] || path == input->direct_c_import_resolved_headers[i]) return true;
  }
  return false;
}

static void free_diag_path_if_owned(ZDiag *diag, const SourceInput *input, const char *command_input) {
  if (!diag || !diag->path) return;
  if (diag->path == command_input || source_input_contains_path_pointer(input, diag->path)) {
    diag->path = NULL;
    return;
  }
  free((char *)diag->path);
  diag->path = NULL;
}

static bool direct_buildability_preflight(const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const IrProgram *ir, ZDiag *diag) {
  if (ir && !ir->mir_valid) {
    init_lowering_backend_diag(diag, input, target, command, ir);
    return false;
  }
  return z_direct_buildability_check(ir, target, emit_kind, diag);
}

static void format_file_size(long long bytes, char *out, size_t out_len) {
  const char *units[] = {"B", "KiB", "MiB", "GiB"};
  double size = (double)bytes;
  size_t unit = 0;
  while (size >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
    size /= 1024.0;
    unit++;
  }
  if (unit == 0) snprintf(out, out_len, "%lld %s", bytes, units[unit]);
  else snprintf(out, out_len, "%.1f %s", size, units[unit]);
}

static long long now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void format_duration(long long elapsed_ms, char *out, size_t out_len) {
  if (elapsed_ms < 1000) snprintf(out, out_len, "%lld ms", elapsed_ms);
  else snprintf(out, out_len, "%.1f s", (double)elapsed_ms / 1000.0);
}

static void print_artifact(const char *path, long long elapsed_ms) {
  char duration[32];
  format_duration(elapsed_ms, duration, sizeof(duration));

  struct stat st;
  if (stat(path, &st) == 0) {
    char size[32];
    format_file_size((long long)st.st_size, size, sizeof(size));
    printf("%s (%s, %s)\n", path, size, duration);
  } else {
    printf("%s (%s)\n", path, duration);
  }
}

static void init_executable_finalize_diag(ZDiag *diag, const char *path) {
  if (!diag) return;
  diag->code = 2003;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  diag->path = path;
  snprintf(diag->message, sizeof(diag->message), "failed to finalize executable artifact");
  snprintf(diag->expected, sizeof(diag->expected), "regular non-empty executable artifact with executable permissions");
  snprintf(diag->actual, sizeof(diag->actual), "%s", path && path[0] ? path : "missing artifact path");
  snprintf(diag->help, sizeof(diag->help), "check output path permissions, ensure the artifact path is not a directory or symlink, and pass --out <path> when running builds concurrently");
}

static int run_executable_artifact_as(const char *exe_file, const char *argv0, const Command *command) {
  if (!z_process_executable_file_ready(exe_file)) {
    fprintf(stderr, "zero run: executable artifact is not a regular executable file: %s\n", exe_file ? exe_file : "<missing>");
    return 1;
  }
  int run_argc = command ? command->run_argc : 0; if (run_argc < 0) run_argc = 0;
  char **child_argv = (char **)z_checked_calloc((size_t)run_argc + 2, sizeof(char *));
  child_argv[0] = (char *)(argv0 && argv0[0] ? argv0 : exe_file);
  for (int i = 0; i < run_argc; i++) child_argv[i + 1] = command->run_argv[i];
  child_argv[run_argc + 1] = NULL;
  fflush(NULL);
#if defined(_WIN32)
  intptr_t status = _spawnv(_P_WAIT, exe_file, (const char *const *)child_argv);
  free(child_argv);
  if (status < 0) {
    perror("zero run");
    return 1;
  }
  return (int)status;
#else
  pid_t pid = fork();
  if (pid == 0) {
    execv(exe_file, child_argv);
    perror("zero run");
    _exit(127);
  }
  free(child_argv);
  if (pid < 0) {
    perror("zero run");
    return 1;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    perror("zero run");
    return 1;
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status); if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 1;
#endif
}

static int exec_cached_executable_artifact(const char *exe_file, const char *argv0, const Command *command) {
#if defined(_WIN32)
  return run_executable_artifact_as(exe_file, argv0, command);
#else
  if (!z_process_executable_file_ready(exe_file)) {
    fprintf(stderr, "zero run: executable artifact is not a regular executable file: %s\n", exe_file ? exe_file : "<missing>");
    return 1;
  }
  int run_argc = command ? command->run_argc : 0; if (run_argc < 0) run_argc = 0;
  char **child_argv = (char **)z_checked_calloc((size_t)run_argc + 2, sizeof(char *));
  child_argv[0] = (char *)(argv0 && argv0[0] ? argv0 : exe_file);
  for (int i = 0; i < run_argc; i++) child_argv[i + 1] = command->run_argv[i];
  child_argv[run_argc + 1] = NULL;
  fflush(NULL);
  execv(exe_file, child_argv);
  perror("zero run");
  free(child_argv);
  return 127;
#endif
}

static const char *build_compiler_label(const Command *command, const ZTargetInfo *target) {
  ZToolchainPlan plan = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
  return plan.compiler;
}

static const char *profile_canonical_name(const char *profile) {
  if (!profile || strcmp(profile, "release") == 0 || strcmp(profile, "release-small") == 0 || strcmp(profile, "small") == 0) return "release-small";
  if (strcmp(profile, "release-fast") == 0 || strcmp(profile, "fast") == 0) return "release-fast";
  if (strcmp(profile, "tiny") == 0) return "tiny";
  if (strcmp(profile, "debug") == 0) return "debug";
  if (strcmp(profile, "dev") == 0) return "dev";
  if (strcmp(profile, "audit") == 0) return "audit";
  return "release-small";
}

static const char *profile_overflow_policy(const char *profile) {
  (void)profile;
  return "literal-range-checked-runtime-unchecked";
}

static const char *profile_bounds_policy(const char *profile) {
  (void)profile;
  return "checked";
}

static const char *profile_panic_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0 || strcmp(canonical, "audit") == 0) return "diagnostic-trap";
  return "abort";
}

static bool profile_debug_info(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  return strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0 || strcmp(canonical, "audit") == 0;
}

static const char *profile_runtime_metadata(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "audit") == 0) return "maximum";
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "debug";
  if (strcmp(canonical, "tiny") == 0) return "minimum";
  return "pay-as-used";
}

static const char *profile_key_name(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "release-fast") == 0) return "fast";
  if (strcmp(canonical, "release-small") == 0) return "small";
  return canonical;
}

static const char *profile_optimization_goal(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0) return "observability";
  if (strcmp(canonical, "dev") == 0) return "edit-latency";
  if (strcmp(canonical, "release-fast") == 0) return "throughput";
  if (strcmp(canonical, "tiny") == 0) return "minimum-binary-size";
  if (strcmp(canonical, "audit") == 0) return "release-auditability";
  return "small-binary-size";
}

static const char *profile_codegen_optimization(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "none";
  if (strcmp(canonical, "release-fast") == 0) return "speed";
  if (strcmp(canonical, "tiny") == 0) return "size-min";
  if (strcmp(canonical, "audit") == 0) return "size-with-audit-metadata";
  return "size";
}

static const char *profile_link_optimization(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "keep-debug-names";
  if (strcmp(canonical, "tiny") == 0) return "section-gc-strip-minimal-metadata";
  if (strcmp(canonical, "audit") == 0) return "section-gc-keep-audit-metadata";
  return "section-gc-strip";
}

static const char *profile_unwind_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0 || strcmp(canonical, "audit") == 0) return "no-unwind-diagnostic-trap";
  return "no-unwind-abort";
}

static const char *profile_symbol_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "keep-local-and-public-names";
  if (strcmp(canonical, "audit") == 0) return "keep-public-and-audit-names";
  if (strcmp(canonical, "tiny") == 0) return "strip-all-unrequested-names";
  return "keep-public-names";
}

static void append_safety_facts_json(ZBuf *buf, const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  ZSafetyFactsProfile facts = {
    .canonical_profile = canonical,
    .profile_key = profile_key_name(profile),
  };
  z_append_safety_facts_json(buf, &facts);
}

static int profile_max_hello_bytes(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "audit") == 0) return 65536;
  if (strcmp(canonical, "dev") == 0) return 32768;
  if (strcmp(canonical, "release-fast") == 0) return 24576;
  if (strcmp(canonical, "tiny") == 0) return 10240;
  return 12288;
}

static const char *profile_helper_budget_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "pay-as-used-debug-metadata-allowed";
  if (strcmp(canonical, "audit") == 0) return "pay-as-used-audit-metadata-allowed";
  if (strcmp(canonical, "tiny") == 0) return "pay-as-used-minimum-runtime";
  return "pay-as-used";
}

static void append_profile_aliases_json(ZBuf *buf, const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  zbuf_append(buf, "[");
  if (strcmp(canonical, "release-small") == 0) {
    append_json_string(buf, "small");
    zbuf_append(buf, ", ");
    append_json_string(buf, "release-small");
    zbuf_append(buf, ", ");
    append_json_string(buf, "release");
  } else if (strcmp(canonical, "release-fast") == 0) {
    append_json_string(buf, "fast");
    zbuf_append(buf, ", ");
    append_json_string(buf, "release-fast");
  } else {
    append_json_string(buf, canonical);
  }
  zbuf_append(buf, "]");
}

static void append_profile_budget_json(ZBuf *buf, const char *profile) {
  zbuf_append(buf, "{\"maxHelloArtifactBytes\":");
  zbuf_appendf(buf, "%d", profile_max_hello_bytes(profile));
  zbuf_append(buf, ",\"helperBudgetPolicy\":");
  append_json_string(buf, profile_helper_budget_policy(profile));
  zbuf_append(buf, ",\"debugMetadataAllowed\":");
  zbuf_append(buf, profile_debug_info(profile) ? "true" : "false");
  zbuf_append(buf, ",\"metadataRetention\":");
  append_json_string(buf, profile_runtime_metadata(profile));
  zbuf_append(buf, ",\"deterministicArtifacts\":true,\"generatedCBytes\":0,\"cBridgeFallback\":false}");
}

static void append_profile_semantics_json(ZBuf *buf, const char *profile) {
  zbuf_append(buf, "{\"requested\":");
  append_json_string(buf, profile ? profile : "release");
  zbuf_append(buf, ",\"canonical\":");
  append_json_string(buf, profile_canonical_name(profile));
  zbuf_append(buf, ",\"profileKey\":");
  append_json_string(buf, profile_key_name(profile));
  zbuf_append(buf, ",\"aliases\":");
  append_profile_aliases_json(buf, profile);
  zbuf_append(buf, ",\"optimizationGoal\":");
  append_json_string(buf, profile_optimization_goal(profile));
  zbuf_append(buf, ",\"codegenOptimization\":");
  append_json_string(buf, profile_codegen_optimization(profile));
  zbuf_append(buf, ",\"linkOptimization\":");
  append_json_string(buf, profile_link_optimization(profile));
  zbuf_append(buf, ",\"overflowPolicy\":");
  append_json_string(buf, profile_overflow_policy(profile));
  zbuf_append(buf, ",\"boundsPolicy\":");
  append_json_string(buf, profile_bounds_policy(profile));
  zbuf_append(buf, ",\"panicPolicy\":");
  append_json_string(buf, profile_panic_policy(profile));
  zbuf_append(buf, ",\"unwindPolicy\":");
  append_json_string(buf, profile_unwind_policy(profile));
  zbuf_appendf(buf, ",\"debugInfo\":%s", profile_debug_info(profile) ? "true" : "false");
  zbuf_append(buf, ",\"runtimeMetadataPolicy\":");
  append_json_string(buf, profile_runtime_metadata(profile));
  zbuf_append(buf, ",\"symbolPolicy\":");
  append_json_string(buf, profile_symbol_policy(profile));
  zbuf_append(buf, ",\"panicFormatting\":");
  zbuf_append(buf, profile_debug_info(profile) ? "true" : "false");
  zbuf_append(buf, ",\"profileBudget\":");
  append_profile_budget_json(buf, profile);
  zbuf_append(buf, "}");
}

static void append_profile_catalog_json(ZBuf *buf) {
  const char *profiles[] = {"debug", "dev", "fast", "small", "tiny", "audit", NULL};
  zbuf_append(buf, "[");
  for (int i = 0; profiles[i]; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_profile_semantics_json(buf, profiles[i]);
  }
  zbuf_append(buf, "]");
}

static const char *public_driver_kind(const char *driver_kind) {
  if (driver_kind && strcmp(driver_kind, "zig-cc") == 0) return "target-cc";
  return driver_kind ? driver_kind : "";
}

static const char *public_compiler_label(const ZToolchainPlan *plan) {
  if (plan && strcmp(plan->driver_kind, "zig-cc") == 0) return "target-capable C compiler";
  return plan && plan->compiler ? plan->compiler : "";
}

static const char *public_linker_label(const char *linker) {
  if (linker && strcmp(linker, "zig cc") == 0) return "target-cc";
  return linker ? linker : "";
}

static void append_toolchain_plan_value_json(ZBuf *buf, const ZToolchainPlan *plan) {
  zbuf_append(buf, "{\"driverKind\":");
  append_json_string(buf, public_driver_kind(plan ? plan->driver_kind : ""));
  zbuf_append(buf, ",\"selectionSource\":");
  append_json_string(buf, plan ? plan->selection_source : "");
  zbuf_append(buf, ",\"compiler\":");
  append_json_string(buf, public_compiler_label(plan));
  zbuf_append(buf, ",\"targetTriple\":");
  append_json_string(buf, plan ? plan->target_triple : "");
  zbuf_append(buf, ",\"linkerFlavor\":");
  append_json_string(buf, public_linker_label(plan ? plan->linker_flavor : ""));
  zbuf_append(buf, ",\"libcMode\":");
  append_json_string(buf, plan ? plan->libc_mode : "");
  zbuf_appendf(buf, ",\"requiresSysroot\":%s", plan && plan->requires_sysroot ? "true" : "false");
  zbuf_append(buf, ",\"sysrootEnv\":");
  append_json_string(buf, plan ? plan->sysroot_env : "");
  zbuf_append(buf, ",\"sysrootStatus\":");
  append_json_string(buf, plan ? plan->sysroot_status : "");
  zbuf_appendf(buf, ",\"usesTargetFlag\":%s", plan && plan->uses_target_flag ? "true" : "false");
  zbuf_appendf(buf, ",\"usesToolchainCache\":%s", plan && plan->uses_zig_cache ? "true" : "false");
  zbuf_appendf(buf, ",\"stripArtifact\":%s", plan && plan->strip_artifact ? "true" : "false");
  zbuf_append(buf, "}");
}

static void append_toolchain_plan_json(ZBuf *buf, const Command *command, const ZTargetInfo *target) {
  ZToolchainPlan plan = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
  append_toolchain_plan_value_json(buf, &plan);
}

static bool command_uses_llvm_native_exe(const Command *command, const char *emit_kind) {
  return command && command->emit == EMIT_EXE && z_backend_request_is_llvm(command->backend, emit_kind ? emit_kind : "exe");
}

static void append_direct_backend_facts_json(ZBuf *buf, const SourceInput *input) {
  bool uses_allocator = input && input->direct_allocator_helper_count > 0;
  bool uses_buffers = input && input->direct_buffer_helper_count > 0;
  bool exports_memory = input && (input->direct_stack_bytes > 0 || input->direct_readonly_data_bytes > 0 || uses_allocator);
  size_t actual_export_count = input ? input->direct_export_count + (exports_memory ? 1 : 0) : 0;
  zbuf_appendf(buf, ",\"directFacts\":{\"functionCount\":%zu,\"exportCount\":%zu,\"stackBytes\":%zu,\"maxFrameBytes\":%zu,\"readonlyDataBytes\":%zu,\"allocatorHelperCount\":%zu,\"bufferHelperCount\":%zu,\"runtimeHelperCount\":%zu,\"sourceFileCount\":%zu,\"moduleCount\":%zu,\"runtime\":{\"linearMemory\":%s,\"memoryPageBytes\":65536,\"stackBase\":65536,\"stackBytes\":%zu,\"readonlyDataBytes\":%zu,\"heapStart\":null,\"heapEnd\":null,\"heapPolicy\":\"%s\",\"allocator\":\"%s\",\"buffer\":\"%s\",\"boundsTraps\":\"pay-as-used\"}}",
              input ? input->direct_function_count : 0,
              actual_export_count,
              input ? input->direct_stack_bytes : 0,
              input ? input->direct_max_frame_bytes : 0,
              input ? input->direct_readonly_data_bytes : 0,
              input ? input->direct_allocator_helper_count : 0,
              input ? input->direct_buffer_helper_count : 0,
              input ? input->direct_runtime_helper_count : 0,
              input ? input->source_file_count : 0,
              input ? input->module_count : 0,
              exports_memory ? "true" : "false",
              input ? input->direct_stack_bytes : 0,
              input ? input->direct_readonly_data_bytes : 0,
              uses_allocator ? "explicit-fixed-buffer-bump" : "not-yet-enabled",
              uses_allocator ? "FixedBufAlloc" : "none",
              uses_buffers ? "Vec<u8>" : "none");
}

typedef struct {
  bool fd_write;
  bool fd_read;
  bool fd_close;
  bool path_open;
  bool args_sizes_get;
  bool args_get;
  bool environ_sizes_get;
  bool environ_get;
  bool clock_time_get;
  bool random_get;
  bool fd_filestat_get;
  bool fd_readdir;
  bool path_create_directory;
  bool path_remove_directory;
  bool path_unlink_file;
  bool path_rename;
  bool zero_json_parse_bytes;
  bool zero_json_diagnostic;
  bool zero_json_field;
  bool zero_json_lookup_scalar;
  bool zero_json_string_decode;
  bool zero_json_string_field;
  bool zero_json_write_string;
  bool zero_json_write_field_raw;
  bool zero_json_write_field_string;
  bool zero_json_write_field_u32;
  bool zero_json_write_field_bool;
  bool zero_json_write_object1_string;
  bool zero_json_write_object1_u32;
  bool zero_json_write_object1_bool;
  bool zero_json_write_object2_fields;
  bool zero_json_write_object2_string_field;
  bool zero_json_write_object2_u32_field;
  bool zero_json_write_object2_bool_field;
  bool zero_json_write_array2_strings;
  bool zero_json_write_array2_u32;
  bool zero_json_write_array2_bools;
  bool zero_ascii_op;
  bool zero_text_op;
  bool zero_time_op;
  bool zero_term_op;
  bool zero_term_read_input;
  bool zero_math_op;
  bool zero_math_usize_op;
  bool zero_search_op;
  bool zero_sort_op;
  bool zero_sort_is_sorted_op;
  bool zero_str_contains;
  bool zero_str_buffer_op;
  bool zero_str_concat;
  bool zero_str_repeat;
  bool zero_str_trim_op;
  bool zero_str_pair_op;
  bool zero_str_count_byte;
  bool zero_str_word_count_ascii;
  bool zero_crypto_digest;
  bool zero_crypto_hmac_sha256;
  bool zero_crypto_hmac_sha256_hex;
  bool zero_args_find;
  bool zero_parse_op;
  bool zero_parse_usize;
  bool zero_parse_i32;
  bool zero_parse_u32;
  bool zero_fmt_bool;
  bool zero_fmt_hex_lower_u32;
  bool zero_fmt_i32;
  bool zero_fmt_u32;
  bool zero_fmt_usize;
  bool zero_http_fetch_result;
  bool zero_http_result_ok;
  bool zero_http_result_status;
  bool zero_http_result_body_len;
  bool zero_http_result_error;
  bool zero_http_response_len;
  bool zero_http_response_headers_len;
  bool zero_http_response_body_offset;
  bool zero_http_header_value;
  bool zero_http_header_found;
  bool zero_http_header_offset;
  bool zero_http_header_len;
  bool zero_http_write_json_response;
  bool zero_http_request_method_name;
  bool zero_http_request_path;
  bool zero_http_request_matches;
  bool zero_http_request_body_within;
  bool zero_proc_spawn_inherit, zero_proc_spawn_inherit_args;
  bool zero_proc_capture, zero_proc_capture_args;
  bool zero_proc_capture_files, zero_proc_capture_files_args;
  bool zero_proc_spawn_child;
  bool zero_proc_spawn_child_in;
  bool zero_proc_spawn_child_in_env;
  bool zero_proc_spawn_child_args;
  bool zero_proc_child_op;
  bool zero_proc_child_io;
  bool zero_pty_spawn;
  bool zero_pty_spawn_in;
  bool zero_pty_spawn_in_env;
  bool zero_pty_spawn_args;
  bool zero_pty_resize;
} RuntimeImportAudit;

static void runtime_import_audit_mark_fs_base(RuntimeImportAudit *audit) {
  if (!audit) return;
  audit->fd_write = true;
  audit->fd_read = true;
  audit->fd_close = true;
  audit->path_open = true;
}

static void runtime_import_audit_mark_str_runtime(RuntimeImportAudit *audit, const IrValue *value) {
  if (!audit || !value) return;
  switch ((IrStrOp)value->int_value) {
    case IR_STR_OP_REVERSE:
    case IR_STR_OP_COPY:
    case IR_STR_OP_TO_LOWER_ASCII:
    case IR_STR_OP_TO_UPPER_ASCII:
      audit->zero_str_buffer_op = true;
      break;
    case IR_STR_OP_CRYPTO_SHA256:
    case IR_STR_OP_CRYPTO_SHA256_HEX:
      audit->zero_crypto_digest = true;
      break;
    case IR_STR_OP_CRYPTO_HMAC_SHA256:
      audit->zero_crypto_hmac_sha256 = true;
      break;
    case IR_STR_OP_CRYPTO_HMAC_SHA256_HEX:
      audit->zero_crypto_hmac_sha256_hex = true;
      break;
    case IR_STR_OP_CONCAT:
      audit->zero_str_concat = true;
      break;
    case IR_STR_OP_REPEAT:
      audit->zero_str_repeat = true;
      break;
    case IR_STR_OP_TRIM_ASCII:
    case IR_STR_OP_TRIM_START_ASCII:
    case IR_STR_OP_TRIM_END_ASCII:
    case IR_STR_OP_PATH_BASENAME:
    case IR_STR_OP_PATH_DIRNAME:
    case IR_STR_OP_PATH_EXTENSION:
    case IR_STR_OP_PARSE_TOKEN_ASCII:
      audit->zero_str_trim_op = true;
      break;
    case IR_STR_OP_COUNT_BYTE:
      audit->zero_str_count_byte = true;
      break;
    case IR_STR_OP_STARTS_WITH:
    case IR_STR_OP_ENDS_WITH:
    case IR_STR_OP_CONTAINS:
    case IR_STR_OP_COUNT:
    case IR_STR_OP_INDEX_OF:
    case IR_STR_OP_LAST_INDEX_OF:
    case IR_STR_OP_EQL_IGNORE_ASCII_CASE:
      audit->zero_str_pair_op = true;
      break;
    case IR_STR_OP_WORD_COUNT_ASCII:
      audit->zero_str_word_count_ascii = true;
      break;
  }
}

static void runtime_import_audit_value(const IrValue *value, RuntimeImportAudit *audit) {
  if (!value || !audit) return;
  switch (value->kind) {
    case IR_VALUE_ARGS_LEN:
    case IR_VALUE_ARGS_GET:
      audit->args_sizes_get = true;
      audit->args_get = true;
      break;
    case IR_VALUE_ENV_GET:
      audit->environ_sizes_get = true;
      audit->environ_get = true;
      break;
    case IR_VALUE_TIME_WALL_SECONDS:
    case IR_VALUE_TIME_MONOTONIC:
      audit->clock_time_get = true;
      break;
    case IR_VALUE_RAND_ENTROPY_U32:
      audit->random_get = true;
      break;
    case IR_VALUE_FS_OPEN:
    case IR_VALUE_FS_CREATE:
    case IR_VALUE_FS_READ_PATH:
    case IR_VALUE_FS_WRITE_PATH:
    case IR_VALUE_FS_READ_BYTES_PATH:
    case IR_VALUE_FS_READ_BYTES_AT_PATH:
    case IR_VALUE_FS_WRITE_BYTES_PATH:
    case IR_VALUE_FS_APPEND_BYTES_PATH:
    case IR_VALUE_FS_READ_ALL:
    case IR_VALUE_FS_READ_FILE:
    case IR_VALUE_FS_WRITE_ALL_FILE:
    case IR_VALUE_FS_CLOSE_FILE:
    case IR_VALUE_FS_EXISTS:
    case IR_VALUE_FS_REMOVE:
    case IR_VALUE_FS_RENAME:
    case IR_VALUE_FS_FILE_LEN:
    case IR_VALUE_FS_MAKE_DIR:
    case IR_VALUE_FS_REMOVE_DIR:
    case IR_VALUE_FS_IS_DIR:
    case IR_VALUE_FS_DIR_ENTRY_COUNT:
    case IR_VALUE_FS_DIR_ENTRY_NAME:
    case IR_VALUE_FS_TEMP_NAME:
    case IR_VALUE_FS_ATOMIC_WRITE:
      runtime_import_audit_mark_fs_base(audit);
      break;
    case IR_VALUE_PROC_SPAWN_INHERIT:
      if (value->arg_len == 4) audit->zero_proc_spawn_inherit_args = true;
      else audit->zero_proc_spawn_inherit = true;
      break;
    case IR_VALUE_PROC_CAPTURE:
      if (value->arg_len == 2) audit->zero_proc_capture_args = true;
      else audit->zero_proc_capture = true;
      break;
    case IR_VALUE_PROC_CAPTURE_FILES:
      if (value->arg_len == 2) audit->zero_proc_capture_files_args = true;
      else audit->zero_proc_capture_files = true;
      break;
    case IR_VALUE_PROC_CHILD_SPAWN:
      if (value->int_value) {
        if (value->arg_len == 4) audit->zero_pty_spawn_args = true;
        else if (value->index) audit->zero_pty_spawn_in_env = true;
        else if (value->right) audit->zero_pty_spawn_in = true;
        else audit->zero_pty_spawn = true;
      } else {
        if (value->arg_len == 4) audit->zero_proc_spawn_child_args = true;
        else if (value->index) audit->zero_proc_spawn_child_in_env = true;
        else if (value->right) audit->zero_proc_spawn_child_in = true;
        else audit->zero_proc_spawn_child = true;
      }
      break;
    case IR_VALUE_PROC_CHILD_OP:
      audit->zero_proc_child_op = true;
      break;
    case IR_VALUE_PROC_CHILD_IO:
      audit->zero_proc_child_io = true;
      break;
    case IR_VALUE_PROC_PTY_RESIZE:
      audit->zero_pty_resize = true;
      break;
    case IR_VALUE_JSON_PARSE_BYTES:
    case IR_VALUE_JSON_VALIDATE_BYTES:
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
      audit->zero_json_parse_bytes = true;
      break;
    case IR_VALUE_JSON_DIAGNOSTIC_BYTES:
      audit->zero_json_diagnostic = true;
      break;
    case IR_VALUE_JSON_FIELD:
      audit->zero_json_field = true;
      break;
    case IR_VALUE_JSON_LOOKUP_SCALAR:
      audit->zero_json_lookup_scalar = true;
      break;
    case IR_VALUE_JSON_STRING_DECODE:
      audit->zero_json_string_decode = true;
      break;
    case IR_VALUE_JSON_STRING_FIELD:
      audit->zero_json_string_field = true;
      break;
    case IR_VALUE_JSON_WRITE_STRING:
      audit->zero_json_write_string = true;
      break;
    case IR_VALUE_JSON_WRITE_RUNTIME:
      switch ((IrJsonWriteOp)value->int_value) {
        case IR_JSON_WRITE_FIELD_RAW: audit->zero_json_write_field_raw = true; break;
        case IR_JSON_WRITE_FIELD_STRING: audit->zero_json_write_field_string = true; break;
        case IR_JSON_WRITE_FIELD_U32: audit->zero_json_write_field_u32 = true; break;
        case IR_JSON_WRITE_FIELD_BOOL: audit->zero_json_write_field_bool = true; break;
        case IR_JSON_WRITE_OBJECT1_STRING: audit->zero_json_write_object1_string = true; break;
        case IR_JSON_WRITE_OBJECT1_U32: audit->zero_json_write_object1_u32 = true; break;
        case IR_JSON_WRITE_OBJECT1_BOOL: audit->zero_json_write_object1_bool = true; break;
        case IR_JSON_WRITE_OBJECT2_FIELDS: audit->zero_json_write_object2_fields = true; break;
        case IR_JSON_WRITE_OBJECT2_STRING_FIELD: audit->zero_json_write_object2_string_field = true; break;
        case IR_JSON_WRITE_OBJECT2_U32_FIELD: audit->zero_json_write_object2_u32_field = true; break;
        case IR_JSON_WRITE_OBJECT2_BOOL_FIELD: audit->zero_json_write_object2_bool_field = true; break;
        case IR_JSON_WRITE_ARRAY2_STRINGS: audit->zero_json_write_array2_strings = true; break;
        case IR_JSON_WRITE_ARRAY2_U32: audit->zero_json_write_array2_u32 = true; break;
        case IR_JSON_WRITE_ARRAY2_BOOLS: audit->zero_json_write_array2_bools = true; break;
      }
      break;
    case IR_VALUE_ASCII_RUNTIME:
      audit->zero_ascii_op = true;
      break;
    case IR_VALUE_TEXT_RUNTIME:
      audit->zero_text_op = true;
      break;
    case IR_VALUE_TIME_RUNTIME:
      audit->zero_time_op = true;
      break;
    case IR_VALUE_TERM_RUNTIME: if ((IrTermOp)value->int_value == IR_TERM_OP_READ_INPUT) audit->zero_term_read_input = true; else audit->zero_term_op = true; break;
    case IR_VALUE_MATH_RUNTIME:
      if (value->type == IR_TYPE_MAYBE_SCALAR && value->element_type == IR_TYPE_USIZE) audit->zero_math_usize_op = true;
      else audit->zero_math_op = true;
      break;
    case IR_VALUE_SEARCH_RUNTIME:
      audit->zero_search_op = true;
      break;
    case IR_VALUE_SORT_RUNTIME:
      if (value->type == IR_TYPE_BOOL) audit->zero_sort_is_sorted_op = true;
      else audit->zero_sort_op = true;
      break;
    case IR_VALUE_STR_CONTAINS:
      audit->zero_str_contains = true;
      break;
    case IR_VALUE_STR_RUNTIME:
      runtime_import_audit_mark_str_runtime(audit, value);
      break;
    case IR_VALUE_PARSE_RUNTIME:
      if (value->type == IR_TYPE_MAYBE_SCALAR && value->element_type == IR_TYPE_USIZE) audit->zero_parse_usize = true;
      else audit->zero_parse_op = true;
      break;
    case IR_VALUE_PARSE_I32:
      audit->zero_parse_i32 = true;
      break;
    case IR_VALUE_PARSE_U32:
    case IR_VALUE_ARGS_PARSE_U32:
      audit->zero_parse_u32 = true;
      break;
    case IR_VALUE_ARGS_FIND:
    case IR_VALUE_ARGS_CONTAINS:
    case IR_VALUE_ARGS_VALUE_AFTER:
    case IR_VALUE_ARGS_VALUE_AFTER_OR:
      audit->zero_args_find = true;
      break;
    case IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32:
      audit->zero_args_find = true;
      audit->zero_parse_u32 = true;
      break;
    case IR_VALUE_FMT_BOOL:
      audit->zero_fmt_bool = true;
      break;
    case IR_VALUE_FMT_HEX_U32:
      audit->zero_fmt_hex_lower_u32 = true;
      break;
    case IR_VALUE_FMT_I32:
      audit->zero_fmt_i32 = true;
      break;
    case IR_VALUE_FMT_U32:
      audit->zero_fmt_u32 = true;
      break;
    case IR_VALUE_FMT_USIZE:
      audit->zero_fmt_usize = true;
      break;
    case IR_VALUE_HTTP_FETCH:
      audit->zero_http_fetch_result = true;
      break;
    case IR_VALUE_HTTP_RESULT_OK:
      audit->zero_http_result_ok = true;
      break;
    case IR_VALUE_HTTP_RESULT_STATUS:
      audit->zero_http_result_status = true;
      break;
    case IR_VALUE_HTTP_RESULT_BODY_LEN:
      audit->zero_http_result_body_len = true;
      break;
    case IR_VALUE_HTTP_RESULT_ERROR:
      audit->zero_http_result_error = true;
      break;
    case IR_VALUE_HTTP_RESPONSE_LEN:
      audit->zero_http_response_len = true;
      break;
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
      audit->zero_http_response_headers_len = true;
      break;
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET:
      audit->zero_http_response_body_offset = true;
      break;
    case IR_VALUE_HTTP_HEADER_VALUE:
      audit->zero_http_header_value = true;
      break;
    case IR_VALUE_HTTP_HEADER_FOUND:
      audit->zero_http_header_found = true;
      break;
    case IR_VALUE_HTTP_HEADER_OFFSET:
      audit->zero_http_header_offset = true;
      break;
    case IR_VALUE_HTTP_HEADER_LEN:
      audit->zero_http_header_len = true;
      break;
    case IR_VALUE_HTTP_WRITE_JSON_RESPONSE:
      audit->zero_http_write_json_response = true;
      break;
    case IR_VALUE_HTTP_REQUEST_METHOD_NAME: audit->zero_http_request_method_name = true; break;
    case IR_VALUE_HTTP_REQUEST_PATH: audit->zero_http_request_path = true; break;
    case IR_VALUE_HTTP_REQUEST_MATCHES: audit->zero_http_request_matches = true; break;
    case IR_VALUE_HTTP_REQUEST_BODY_WITHIN: audit->zero_http_request_body_within = true; break;
    default:
      break;
  }
  if (value->kind == IR_VALUE_FS_FILE_LEN ||
      (value->kind == IR_VALUE_FS_READ_ALL && value->type == IR_TYPE_I64)) {
    audit->fd_filestat_get = true;
  }
  if (value->kind == IR_VALUE_FS_DIR_ENTRY_COUNT || value->kind == IR_VALUE_FS_DIR_ENTRY_NAME) audit->fd_readdir = true;
  if (value->kind == IR_VALUE_FS_MAKE_DIR) audit->path_create_directory = true;
  if (value->kind == IR_VALUE_FS_REMOVE_DIR) audit->path_remove_directory = true;
  if (value->kind == IR_VALUE_FS_REMOVE) audit->path_unlink_file = true;
  if (value->kind == IR_VALUE_FS_RENAME || value->kind == IR_VALUE_FS_ATOMIC_WRITE) audit->path_rename = true;

  runtime_import_audit_value(value->index, audit);
  runtime_import_audit_value(value->left, audit);
  runtime_import_audit_value(value->right, audit);
  for (size_t i = 0; i < value->arg_len; i++) runtime_import_audit_value(value->args[i], audit);
}

static void runtime_import_audit_instrs(const IrInstr *instrs, size_t len, RuntimeImportAudit *audit) {
  if (!instrs || !audit) return;
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (instr->kind == IR_INSTR_WORLD_WRITE) audit->fd_write = true;
    runtime_import_audit_value(instr->value, audit);
    runtime_import_audit_value(instr->index, audit);
    runtime_import_audit_instrs(instr->then_instrs, instr->then_len, audit);
    runtime_import_audit_instrs(instr->else_instrs, instr->else_len, audit);
  }
}

static RuntimeImportAudit runtime_import_audit_from_ir(const IrProgram *ir) {
  RuntimeImportAudit audit = {0};
  if (!ir) return audit;
  for (size_t i = 0; i < ir->function_len; i++) {
    runtime_import_audit_instrs(ir->functions[i].instrs, ir->functions[i].instr_len, &audit);
  }
  return audit;
}

bool z_ir_needs_zero_runtime_object(const IrProgram *ir);

static bool ir_needs_linked_executable_object(const IrProgram *ir) { return z_ir_needs_zero_runtime_object(ir) || (ir && ir->direct_c_import_call_count > 0); }
static bool ir_linked_executable_needs_zero_runtime_object(const IrProgram *ir) { return z_ir_needs_zero_runtime_object(ir) || (ir && ir->direct_c_import_call_count > 0 && ir->direct_runtime_helper_count > 0); }

static size_t native_zero_runtime_import_count(const RuntimeImportAudit *audit) {
  if (!audit) return 0;
  size_t count = 0;
  if (audit->zero_json_parse_bytes) count++;
  if (audit->zero_json_diagnostic) count++;
  if (audit->zero_json_field) count++;
  if (audit->zero_json_lookup_scalar) count++;
  if (audit->zero_json_string_decode) count++;
  if (audit->zero_json_string_field) count++;
  if (audit->zero_json_write_string) count++;
  if (audit->zero_json_write_field_raw) count++;
  if (audit->zero_json_write_field_string) count++;
  if (audit->zero_json_write_field_u32) count++;
  if (audit->zero_json_write_field_bool) count++;
  if (audit->zero_json_write_object1_string) count++;
  if (audit->zero_json_write_object1_u32) count++;
  if (audit->zero_json_write_object1_bool) count++;
  if (audit->zero_json_write_object2_fields) count++;
  if (audit->zero_json_write_object2_string_field) count++;
  if (audit->zero_json_write_object2_u32_field) count++;
  if (audit->zero_json_write_object2_bool_field) count++;
  if (audit->zero_json_write_array2_strings) count++;
  if (audit->zero_json_write_array2_u32) count++;
  if (audit->zero_json_write_array2_bools) count++;
  if (audit->zero_ascii_op) count++;
  if (audit->zero_text_op) count++;
  if (audit->zero_time_op) count++;
  if (audit->zero_term_op) count++;
  if (audit->zero_term_read_input) count++;
  if (audit->zero_math_op) count++;
  if (audit->zero_math_usize_op) count++;
  if (audit->zero_search_op) count++;
  if (audit->zero_sort_op) count++;
  if (audit->zero_sort_is_sorted_op) count++;
  if (audit->zero_str_contains) count++;
  if (audit->zero_str_buffer_op) count++;
  if (audit->zero_str_concat) count++;
  if (audit->zero_str_repeat) count++;
  if (audit->zero_str_trim_op) count++;
  if (audit->zero_str_pair_op) count++;
  if (audit->zero_str_count_byte) count++;
  if (audit->zero_str_word_count_ascii) count++;
  if (audit->zero_crypto_digest) count++;
  if (audit->zero_crypto_hmac_sha256) count++;
  if (audit->zero_crypto_hmac_sha256_hex) count++;
  if (audit->zero_args_find) count++;
  if (audit->zero_parse_op) count++;
  if (audit->zero_parse_usize) count++;
  if (audit->zero_parse_i32) count++;
  if (audit->zero_parse_u32) count++;
  if (audit->zero_fmt_bool) count++;
  if (audit->zero_fmt_hex_lower_u32) count++;
  if (audit->zero_fmt_i32) count++;
  if (audit->zero_fmt_u32) count++;
  if (audit->zero_fmt_usize) count++;
  if (audit->zero_http_fetch_result) count++;
  if (audit->zero_http_result_ok) count++;
  if (audit->zero_http_result_status) count++;
  if (audit->zero_http_result_body_len) count++;
  if (audit->zero_http_result_error) count++;
  if (audit->zero_http_response_len) count++;
  if (audit->zero_http_response_headers_len) count++;
  if (audit->zero_http_response_body_offset) count++;
  if (audit->zero_http_header_value) count++;
  if (audit->zero_http_header_found) count++;
  if (audit->zero_http_header_offset) count++;
  if (audit->zero_http_header_len) count++;
  if (audit->zero_http_write_json_response) count++;
  if (audit->zero_http_request_method_name) count++;
  if (audit->zero_http_request_path) count++;
  if (audit->zero_http_request_matches) count++;
  if (audit->zero_http_request_body_within) count++;
  if (audit->zero_proc_spawn_inherit) count++;
  if (audit->zero_proc_spawn_inherit_args) count++;
  count += (size_t)audit->zero_proc_capture + (size_t)audit->zero_proc_capture_args + (size_t)audit->zero_proc_capture_files + (size_t)audit->zero_proc_capture_files_args;
  if (audit->zero_proc_spawn_child) count++;
  if (audit->zero_proc_spawn_child_in) count++;
  if (audit->zero_proc_spawn_child_in_env) count++;
  if (audit->zero_proc_spawn_child_args) count++;
  if (audit->zero_proc_child_op) count++;
  if (audit->zero_proc_child_io) count++;
  if (audit->zero_pty_spawn) count++;
  if (audit->zero_pty_spawn_in) count++;
  if (audit->zero_pty_spawn_in_env) count++;
  if (audit->zero_pty_spawn_args) count++;
  if (audit->zero_pty_resize) count++;
  return count;
}

static bool runtime_import_audit_uses_zero_runtime(const RuntimeImportAudit *audit) {
  return native_zero_runtime_import_count(audit) > 0;
}

static void append_runtime_import_module_json(ZBuf *buf, const RuntimeImportAudit *audit, size_t import_count) {
  if (import_count == 0) {
    zbuf_append(buf, "null");
    return;
  }
  bool uses_zero_runtime = runtime_import_audit_uses_zero_runtime(audit);
  append_json_string(buf, uses_zero_runtime ? "zero_runtime" : "native-target-runtime");
}

static bool append_runtime_import_functions_json(ZBuf *buf, const RuntimeImportAudit *audit) {
  bool first = true;
  zbuf_append(buf, "[");
  if (audit && audit->zero_json_parse_bytes) append_json_string_array_item(buf, &first, "zero_json_parse_bytes");
  if (audit && audit->zero_json_diagnostic) append_json_string_array_item(buf, &first, "zero_json_diagnostic");
  if (audit && audit->zero_json_field) append_json_string_array_item(buf, &first, "zero_json_field");
  if (audit && audit->zero_json_lookup_scalar) append_json_string_array_item(buf, &first, "zero_json_lookup_scalar");
  if (audit && audit->zero_json_string_decode) append_json_string_array_item(buf, &first, "zero_json_string_decode");
  if (audit && audit->zero_json_string_field) append_json_string_array_item(buf, &first, "zero_json_string_field");
  if (audit && audit->zero_json_write_string) append_json_string_array_item(buf, &first, "zero_json_write_string");
  if (audit && audit->zero_json_write_field_raw) append_json_string_array_item(buf, &first, "zero_json_write_field_raw");
  if (audit && audit->zero_json_write_field_string) append_json_string_array_item(buf, &first, "zero_json_write_field_string");
  if (audit && audit->zero_json_write_field_u32) append_json_string_array_item(buf, &first, "zero_json_write_field_u32");
  if (audit && audit->zero_json_write_field_bool) append_json_string_array_item(buf, &first, "zero_json_write_field_bool");
  if (audit && audit->zero_json_write_object1_string) append_json_string_array_item(buf, &first, "zero_json_write_object1_string");
  if (audit && audit->zero_json_write_object1_u32) append_json_string_array_item(buf, &first, "zero_json_write_object1_u32");
  if (audit && audit->zero_json_write_object1_bool) append_json_string_array_item(buf, &first, "zero_json_write_object1_bool");
  if (audit && audit->zero_json_write_object2_fields) append_json_string_array_item(buf, &first, "zero_json_write_object2_fields");
  if (audit && audit->zero_json_write_object2_string_field) append_json_string_array_item(buf, &first, "zero_json_write_object2_string_field");
  if (audit && audit->zero_json_write_object2_u32_field) append_json_string_array_item(buf, &first, "zero_json_write_object2_u32_field");
  if (audit && audit->zero_json_write_object2_bool_field) append_json_string_array_item(buf, &first, "zero_json_write_object2_bool_field");
  if (audit && audit->zero_json_write_array2_strings) append_json_string_array_item(buf, &first, "zero_json_write_array2_strings");
  if (audit && audit->zero_json_write_array2_u32) append_json_string_array_item(buf, &first, "zero_json_write_array2_u32");
  if (audit && audit->zero_json_write_array2_bools) append_json_string_array_item(buf, &first, "zero_json_write_array2_bools");
  if (audit && audit->zero_ascii_op) append_json_string_array_item(buf, &first, "zero_ascii_op");
  if (audit && audit->zero_text_op) append_json_string_array_item(buf, &first, "zero_text_op");
  if (audit && audit->zero_time_op) append_json_string_array_item(buf, &first, "zero_time_op");
  if (audit && audit->zero_term_op) append_json_string_array_item(buf, &first, "zero_term_op");
  if (audit && audit->zero_term_read_input) append_json_string_array_item(buf, &first, "zero_term_read_input");
  if (audit && audit->zero_math_op) append_json_string_array_item(buf, &first, "zero_math_op");
  if (audit && audit->zero_math_usize_op) append_json_string_array_item(buf, &first, "zero_math_usize_op");
  if (audit && audit->zero_search_op) append_json_string_array_item(buf, &first, "zero_search_op");
  if (audit && audit->zero_sort_op) append_json_string_array_item(buf, &first, "zero_sort_op");
  if (audit && audit->zero_sort_is_sorted_op) append_json_string_array_item(buf, &first, "zero_sort_is_sorted_op");
  if (audit && audit->zero_str_contains) append_json_string_array_item(buf, &first, "zero_str_contains");
  if (audit && audit->zero_str_buffer_op) append_json_string_array_item(buf, &first, "zero_str_buffer_op");
  if (audit && audit->zero_str_concat) append_json_string_array_item(buf, &first, "zero_str_concat");
  if (audit && audit->zero_str_repeat) append_json_string_array_item(buf, &first, "zero_str_repeat");
  if (audit && audit->zero_str_trim_op) append_json_string_array_item(buf, &first, "zero_str_trim_op");
  if (audit && audit->zero_str_pair_op) append_json_string_array_item(buf, &first, "zero_str_pair_op");
  if (audit && audit->zero_str_count_byte) append_json_string_array_item(buf, &first, "zero_str_count_byte");
  if (audit && audit->zero_str_word_count_ascii) append_json_string_array_item(buf, &first, "zero_str_word_count_ascii");
  if (audit && audit->zero_crypto_digest) append_json_string_array_item(buf, &first, "zero_crypto_digest");
  if (audit && audit->zero_crypto_hmac_sha256) append_json_string_array_item(buf, &first, "zero_crypto_hmac_sha256");
  if (audit && audit->zero_crypto_hmac_sha256_hex) append_json_string_array_item(buf, &first, "zero_crypto_hmac_sha256_hex");
  if (audit && audit->zero_args_find) append_json_string_array_item(buf, &first, "zero_args_find");
  if (audit && audit->zero_parse_op) append_json_string_array_item(buf, &first, "zero_parse_op");
  if (audit && audit->zero_parse_usize) append_json_string_array_item(buf, &first, "zero_parse_usize");
  if (audit && audit->zero_parse_i32) append_json_string_array_item(buf, &first, "zero_parse_i32");
  if (audit && audit->zero_parse_u32) append_json_string_array_item(buf, &first, "zero_parse_u32");
  if (audit && audit->zero_fmt_bool) append_json_string_array_item(buf, &first, "zero_fmt_bool");
  if (audit && audit->zero_fmt_hex_lower_u32) append_json_string_array_item(buf, &first, "zero_fmt_hex_lower_u32");
  if (audit && audit->zero_fmt_i32) append_json_string_array_item(buf, &first, "zero_fmt_i32");
  if (audit && audit->zero_fmt_u32) append_json_string_array_item(buf, &first, "zero_fmt_u32");
  if (audit && audit->zero_fmt_usize) append_json_string_array_item(buf, &first, "zero_fmt_usize");
  if (audit && audit->zero_http_fetch_result) append_json_string_array_item(buf, &first, "zero_http_fetch_result");
  if (audit && audit->zero_http_result_ok) append_json_string_array_item(buf, &first, "zero_http_result_ok");
  if (audit && audit->zero_http_result_status) append_json_string_array_item(buf, &first, "zero_http_result_status");
  if (audit && audit->zero_http_result_body_len) append_json_string_array_item(buf, &first, "zero_http_result_body_len");
  if (audit && audit->zero_http_result_error) append_json_string_array_item(buf, &first, "zero_http_result_error");
  if (audit && audit->zero_http_response_len) append_json_string_array_item(buf, &first, "zero_http_response_len");
  if (audit && audit->zero_http_response_headers_len) append_json_string_array_item(buf, &first, "zero_http_response_headers_len");
  if (audit && audit->zero_http_response_body_offset) append_json_string_array_item(buf, &first, "zero_http_response_body_offset");
  if (audit && audit->zero_http_header_value) append_json_string_array_item(buf, &first, "zero_http_header_value");
  if (audit && audit->zero_http_header_found) append_json_string_array_item(buf, &first, "zero_http_header_found");
  if (audit && audit->zero_http_header_offset) append_json_string_array_item(buf, &first, "zero_http_header_offset");
  if (audit && audit->zero_http_header_len) append_json_string_array_item(buf, &first, "zero_http_header_len");
  if (audit && audit->zero_http_write_json_response) append_json_string_array_item(buf, &first, "zero_http_write_json_response");
  if (audit && audit->zero_http_request_method_name) append_json_string_array_item(buf, &first, "zero_http_request_method_name");
  if (audit && audit->zero_http_request_path) append_json_string_array_item(buf, &first, "zero_http_request_path");
  if (audit && audit->zero_http_request_matches) append_json_string_array_item(buf, &first, "zero_http_request_matches");
  if (audit && audit->zero_http_request_body_within) append_json_string_array_item(buf, &first, "zero_http_request_body_within");
  if (audit && audit->zero_proc_spawn_inherit) append_json_string_array_item(buf, &first, "zero_proc_spawn_inherit");
  if (audit && audit->zero_proc_spawn_inherit_args) append_json_string_array_item(buf, &first, "zero_proc_spawn_inherit_args");
  if (audit && audit->zero_proc_capture) append_json_string_array_item(buf, &first, "zero_proc_capture");
  if (audit && audit->zero_proc_capture_args) append_json_string_array_item(buf, &first, "zero_proc_capture_args");
  if (audit && audit->zero_proc_capture_files) append_json_string_array_item(buf, &first, "zero_proc_capture_files");
  if (audit && audit->zero_proc_capture_files_args) append_json_string_array_item(buf, &first, "zero_proc_capture_files_args");
  if (audit && audit->zero_proc_spawn_child) append_json_string_array_item(buf, &first, "zero_proc_spawn_child");
  if (audit && audit->zero_proc_spawn_child_in) append_json_string_array_item(buf, &first, "zero_proc_spawn_child_in");
  if (audit && audit->zero_proc_spawn_child_in_env) append_json_string_array_item(buf, &first, "zero_proc_spawn_child_in_env");
  if (audit && audit->zero_proc_spawn_child_args) append_json_string_array_item(buf, &first, "zero_proc_spawn_child_args");
  if (audit && audit->zero_proc_child_op) append_json_string_array_item(buf, &first, "zero_proc_child_op");
  if (audit && audit->zero_proc_child_io) append_json_string_array_item(buf, &first, "zero_proc_child_io");
  if (audit && audit->zero_pty_spawn) append_json_string_array_item(buf, &first, "zero_pty_spawn");
  if (audit && audit->zero_pty_spawn_in) append_json_string_array_item(buf, &first, "zero_pty_spawn_in");
  if (audit && audit->zero_pty_spawn_in_env) append_json_string_array_item(buf, &first, "zero_pty_spawn_in_env");
  if (audit && audit->zero_pty_spawn_args) append_json_string_array_item(buf, &first, "zero_pty_spawn_args");
  if (audit && audit->zero_pty_resize) append_json_string_array_item(buf, &first, "zero_pty_resize");
  zbuf_append(buf, "]");
  return !first;
}

static void append_memory_floor_json(ZBuf *buf, const SourceInput *input) {
  zbuf_append(buf, "{\"linearMemory\":");
  zbuf_append(buf, "false");
  zbuf_appendf(buf, ",\"pageBytes\":0,\"minimumPages\":0,\"floorBytes\":0,\"stackBase\":0,\"stackBytes\":%zu,\"readonlyDataBytes\":%zu}",
               input ? input->direct_stack_bytes : 0,
               input ? input->direct_readonly_data_bytes : 0);
}

static void append_runtime_import_audit_json(ZBuf *buf, const IrProgram *ir, const SourceInput *input, const ZTargetInfo *target) {
  RuntimeImportAudit audit = runtime_import_audit_from_ir(ir);
  size_t import_count = native_zero_runtime_import_count(&audit);
  zbuf_append(buf, "{\"source\":\"direct-ir-scan\",\"module\":");
  append_runtime_import_module_json(buf, &audit, import_count);
  zbuf_append(buf, ",\"functions\":");
  append_runtime_import_functions_json(buf, &audit);
  zbuf_appendf(buf, ",\"functionCount\":%zu", import_count);
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target ? target->name : "host");
  zbuf_append(buf, ",\"memoryFloor\":");
  append_memory_floor_json(buf, input);
  zbuf_append(buf, "}");
}

static void append_portable_runtime_json(ZBuf *buf, const IrProgram *ir, const SourceInput *input, const ZTargetInfo *target, const CapabilitySummary *caps) {
  RuntimeImportAudit audit = runtime_import_audit_from_ir(ir);
  bool uses_zero_runtime = runtime_import_audit_uses_zero_runtime(&audit);
  size_t import_count = native_zero_runtime_import_count(&audit);
  zbuf_append(buf, "{\"schemaVersion\":1,\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"runtimeKind\":");
  append_json_string(buf, portable_runtime_kind(target));
  zbuf_append(buf, ",\"portable\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"providerSpecificDeployment\":false,\"hostedDeployment\":\"out-of-scope\"");
  zbuf_append(buf, ",\"imports\":{\"source\":\"direct-ir-scan\",\"explicit\":");
  zbuf_append(buf, uses_zero_runtime ? "true" : "false");
  zbuf_append(buf, ",\"module\":");
  append_runtime_import_module_json(buf, &audit, import_count);
  zbuf_append(buf, ",\"functions\":");
  append_runtime_import_functions_json(buf, &audit);
  zbuf_appendf(buf, ",\"functionCount\":%zu", import_count);
  zbuf_append(buf, ",\"adapter\":");
  if (uses_zero_runtime) append_json_string(buf, "native-zero-runtime-object");
  else append_json_string(buf, "native-target-runtime");
  zbuf_append(buf, "},\"localRunner\":{\"covered\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"command\":");
  append_json_string(buf, "zero dev");
  zbuf_append(buf, ",\"productionLikeImports\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, "},\"capabilityRestrictions\":");
  append_portable_capability_restrictions_json(buf, target);
  zbuf_append(buf, ",\"capabilities\":");
  append_capability_json_array(buf, caps);
  zbuf_append(buf, ",\"memoryFloor\":");
  append_memory_floor_json(buf, input);
  zbuf_append(buf, ",\"frameworkTaxBytes\":0");
  zbuf_append(buf, "}");
}

static void append_target_capability_contract_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *capabilities[] = {"memory", "stdio", "args", "env", "fs", "net", "proc", "time", "rand", "web", NULL};
  zbuf_append(buf, "[");
  for (int i = 0; capabilities[i]; i++) {
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, capabilities[i]);
    zbuf_appendf(buf, ",\"available\":%s,\"source\":", z_target_has_capability(target, capabilities[i]) ? "true" : "false");
    append_json_string(buf, z_target_has_capability(target, capabilities[i]) ? "target-manifest" : "unavailable");
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]");
}

static void append_target_capability_names_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *capabilities[] = {"memory", "stdio", "args", "env", "fs", "net", "proc", "time", "rand", "web", NULL};
  bool first = true;
  zbuf_append(buf, "[");
  for (int i = 0; capabilities[i]; i++) {
    if (!z_target_has_capability(target, capabilities[i])) continue;
    if (!first) zbuf_append(buf, ",");
    append_json_string(buf, capabilities[i]);
    first = false;
  }
  zbuf_append(buf, "]");
}

static bool source_uses_linked_executable_path(const SourceInput *input, const char *emit_kind) {
  return emit_kind && strcmp(emit_kind, "exe") == 0 && input &&
         (input->direct_host_runtime_import_count > 0 || input->direct_c_import_call_count > 0);
}

static void append_release_target_contract_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const Command *command, const char *emit_kind) {
  const char *object_format = target && target->object_format ? target->object_format : "unknown";
  bool llvm_ir_output = command && command->emit == EMIT_LLVM_IR && z_backend_request_is_llvm(command->backend, emit_kind);
  bool llvm_native_output = command_uses_llvm_native_exe(command, emit_kind);
  ZToolchainPlan plan = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
  ZLlvmToolchainPlan llvm_plan = z_llvm_toolchain_plan(target);
  bool linked_executable = source_uses_linked_executable_path(input, emit_kind);
  ZDirectReleaseTargetFacts release = z_direct_release_target_facts(target, emit_kind, z_backend_direct_request_name(command ? command->backend : NULL), &plan, linked_executable);
  if (llvm_ir_output) {
    release.selected_emitter = "llvm-ir"; release.artifact_kind = "llvm-ir"; release.linker_flavor = "none"; release.artifact_libc_mode = "none";
    release.artifact_requires_sysroot = false; release.direct_selected = false;
    release.sysroot_status = release.target_requires_sysroot ? "not-used-by-llvm-ir" : "not-required";
  } else if (llvm_native_output) {
    release.selected_emitter = "llvm-clang-exe"; release.artifact_kind = "native-executable"; release.linker_flavor = "clang"; release.artifact_libc_mode = z_target_libc_mode(target);
    release.artifact_requires_sysroot = false; release.direct_selected = false; release.sysroot_status = "not-required";
  }
  bool release_supported = llvm_ir_output || (llvm_native_output && llvm_plan.native_executable) || release.direct_selected;
  bool missing_sysroot = release.artifact_requires_sysroot && strcmp(plan.sysroot_status, "missing") == 0;

  zbuf_append(buf, "{\"schemaVersion\":1,\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
#define APPEND_FIELD(name, value) do { zbuf_append(buf, ",\"" name "\":"); append_json_string(buf, value); } while (0)
  APPEND_FIELD("hostTarget", z_host_target());
  zbuf_appendf(buf, ",\"crossCompilation\":%s", target && strcmp(target->name, z_host_target()) != 0 ? "true" : "false");
  APPEND_FIELD("emit", emit_kind ? emit_kind : "exe");
  if (llvm_ir_output || llvm_native_output) {
    zbuf_append(buf, ",\"backendFamily\":\"llvm\"");
    z_append_llvm_backend_lifecycle_field_json(buf);
  }
  APPEND_FIELD("artifactKind", release.artifact_kind);
  APPEND_FIELD("objectFormat", object_format);
  APPEND_FIELD("os", target && target->os ? target->os : "unknown");
  APPEND_FIELD("arch", target && target->arch ? target->arch : "unknown");
  APPEND_FIELD("abi", target && target->abi ? target->abi : "");
  APPEND_FIELD("linkerFlavor", release.linker_flavor);
  APPEND_FIELD("targetLinker", llvm_ir_output ? "none" : (llvm_native_output ? "clang" : (target && target->linker ? target->linker : "")));
  APPEND_FIELD("selectedEmitter", release.selected_emitter);
  APPEND_FIELD("directObjectEmitter", z_direct_object_emitter(target));
  APPEND_FIELD("directExeEmitter", z_direct_exe_emitter(target));
  APPEND_FIELD("directStatus", z_direct_backend_status(target));
  APPEND_FIELD("fallbackPolicy", (llvm_ir_output || llvm_native_output) ? "none" : "explicit-direct-never-c-bridge");
  zbuf_append(buf, ",\"generatedCBytes\":0,\"cBridgeFallback\":false");
  zbuf_append(buf, ",\"libc\":{\"name\":");
  append_json_string(buf, target && target->libc ? target->libc : "default");
  APPEND_FIELD("targetMode", z_target_libc_mode(target));
  APPEND_FIELD("artifactMode", release.artifact_libc_mode);
  zbuf_appendf(buf, ",\"hostReusable\":%s}", target && z_target_is_host(target) ? "true" : "false");
  zbuf_appendf(buf, ",\"sysroot\":{\"requiredByTarget\":%s,\"requiredByArtifact\":%s,\"env\":",
               release.target_requires_sysroot ? "true" : "false",
               release.artifact_requires_sysroot ? "true" : "false");
  append_json_string(buf, release.target_requires_sysroot || release.artifact_requires_sysroot ? plan.sysroot_env : "");
  APPEND_FIELD("status", release.sysroot_status);
  zbuf_appendf(buf, ",\"missing\":%s}", missing_sysroot ? "true" : "false");
  zbuf_append(buf, ",\"capabilities\":");
  append_target_capability_names_json(buf, target);
  zbuf_append(buf, ",\"capabilityFacts\":");
  append_target_capability_contract_json(buf, target);
  zbuf_append(buf, ",\"readiness\":{\"status\":");
  append_json_string(buf, release_supported ? "supported" : "unsupported");
  zbuf_appendf(buf, ",\"directArtifact\":%s", release.direct_selected ? "true" : "false");
  if (llvm_ir_output || llvm_native_output) zbuf_append(buf, ",\"llvmArtifact\":true");
  if (llvm_ir_output || llvm_native_output) zbuf_append(buf, ",\"releaseEligible\":false");
  zbuf_appendf(buf, ",\"missingSysroot\":%s,\"unsupportedReason\":", missing_sysroot ? "true" : "false");
  append_json_string(buf, release_supported ? "" : (llvm_native_output ? llvm_plan.reason : z_direct_backend_reason(target)));
#undef APPEND_FIELD
  zbuf_append(buf, "},\"determinism\":{\"reproducible\":");
  zbuf_append(buf, llvm_native_output ? "false" : "true");
  zbuf_append(buf, ",\"stableArtifactNames\":true,\"repeatBuildHash\":");
  append_json_string(buf, llvm_native_output ? "external-toolchain-not-claimed" : "checked-by-command-contracts");
  zbuf_append(buf, "}");
  zbuf_appendf(buf, ",\"sourceFileCount\":%zu,\"moduleCount\":%zu}", input ? input->source_file_count : 0, input ? input->module_count : 0);
}

static void append_target_libraries_label_json(ZBuf *buf, bool links_zero_runtime, bool links_http_runtime, bool links_c_imports) {
  if (!links_zero_runtime && !links_http_runtime && !links_c_imports) {
    append_json_string(buf, "none");
    return;
  }
  ZBuf label;
  zbuf_init(&label);
  if (links_zero_runtime) zbuf_append(&label, "zero-runtime");
  if (links_http_runtime) {
    if (label.len > 0) zbuf_append_char(&label, ',');
    zbuf_append(&label, "curl");
  }
  if (links_c_imports) {
    if (label.len > 0) zbuf_append_char(&label, ',');
    zbuf_append(&label, "c-imports");
  }
  append_json_string(buf, label.data ? label.data : "none");
  zbuf_free(&label);
}

static void append_object_linker_plan_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const ZDirectObjectBackendFacts *direct, const ZToolchainPlan *runtime_toolchain, const char *object_format, const char *runtime_external_toolchain, bool uses_external_toolchain, bool links_zero_runtime, bool links_http_runtime, bool uses_c_imports) {
  ZBuf static_libraries;
  ZBuf system_libraries;
  zbuf_init(&static_libraries);
  zbuf_init(&system_libraries);
  zbuf_append(&static_libraries, "[");
  zbuf_append(&system_libraries, "[");
  bool static_first = true;
  bool system_first = true;
  if (links_zero_runtime) append_json_array_item_string(&static_libraries, &static_first, "zero_runtime.o");
  if (links_http_runtime) append_json_array_item_string(&static_libraries, &static_first, "zero_http_curl.o");
  if (links_http_runtime) append_json_array_item_string(&system_libraries, &system_first, "curl");
  if (uses_c_imports) append_manifest_c_link_plan_items_json(&static_libraries, &static_first, &system_libraries, &system_first, input);
  zbuf_append(&static_libraries, "]");
  zbuf_append(&system_libraries, "]");
  zbuf_append(buf, ",\"linkerPlan\":{\"format\":");
  append_json_string(buf, object_format);
  zbuf_append(buf, ",\"flavor\":");
  append_json_string(buf, direct ? direct->linker_flavor : "none");
  zbuf_append(buf, ",\"archives\":[],\"staticLibraries\":");
  zbuf_append(buf, static_libraries.data ? static_libraries.data : "[]");
  zbuf_append(buf, ",\"importLibraries\":[],\"systemLibraries\":");
  zbuf_append(buf, system_libraries.data ? system_libraries.data : "[]");
  zbuf_append(buf, ",\"rpaths\":[],\"loadPaths\":[],\"visibility\":\"exported-c-and-main-only\",\"crossLinking\":true,\"externalToolchain\":");
  append_json_string(buf, runtime_external_toolchain);
  zbuf_append(buf, ",\"reproducible\":true,\"libcMode\":");
  append_json_string(buf, uses_external_toolchain && runtime_toolchain ? runtime_toolchain->libc_mode : "none");
  zbuf_append(buf, ",\"targetLibcMode\":");
  append_json_string(buf, z_target_libc_mode(target));
  bool artifact_requires_sysroot = uses_external_toolchain && runtime_toolchain && runtime_toolchain->requires_sysroot;
  zbuf_appendf(buf, ",\"requiresSysroot\":%s,\"targetRequiresSysroot\":%s,\"sysrootStatus\":",
               artifact_requires_sysroot ? "true" : "false",
               z_target_requires_sysroot(target) ? "true" : "false");
  append_json_string(buf, artifact_requires_sysroot && runtime_toolchain ? runtime_toolchain->sysroot_status : (z_target_requires_sysroot(target) ? "not-used-by-direct-artifact" : "not-required"));
  zbuf_append(buf, "}");
  zbuf_free(&static_libraries);
  zbuf_free(&system_libraries);
}

static void append_object_backend_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const Command *command, const char *emit_kind) {
  bool uses_host_runtime = input && input->direct_host_runtime_import_count > 0;
  bool uses_http_runtime = input && input->direct_http_runtime_import_count > 0;
  bool uses_c_imports = input && input->direct_c_import_call_count > 0;
  bool linked_executable = source_uses_linked_executable_path(input, emit_kind);
  ZDirectObjectBackendFacts direct = z_direct_object_backend_facts(target, emit_kind, z_backend_direct_request_name(command ? command->backend : NULL), linked_executable);
  if (direct.active) {
    const char *object_format = target && target->object_format ? target->object_format : "unknown";
    const char *arch = target && target->arch ? target->arch : "unknown";
    size_t direct_symbol_count = input ? input->direct_function_count : 0;
    bool links_zero_runtime = uses_host_runtime || (linked_executable && input && input->direct_runtime_helper_count > 0);
    bool links_http_runtime = uses_http_runtime;
    ZToolchainPlan runtime_toolchain = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
    const char *runtime_external_toolchain = linked_executable ? public_compiler_label(&runtime_toolchain) : "none";
    const char *toolchain_source = links_zero_runtime && uses_c_imports
      ? "direct-backend-runtime-and-c-import-link-plan"
      : (uses_c_imports ? "direct-backend-c-import-link-plan" : (links_zero_runtime ? "direct-backend-runtime-link-plan" : "direct-backend"));
    const char *relocations = uses_c_imports && links_zero_runtime
      ? "patched-runtime-and-external-c-relocations"
      : (uses_c_imports ? "patched-external-c-call-relocations" : (links_zero_runtime ? "patched-runtime-import-relocations" : NULL));
    zbuf_append(buf, "{\"internalIr\":{\"typeRepresentation\":\"MIR primitive value types\",\"controlFlowRepresentation\":\"MIR instruction stream lowered to target machine/module code\",\"callRepresentation\":\"same-object direct calls for supported direct subsets\",\"functionIdentity\":\"module-qualified-stable-sorted\",\"debugRepresentation\":\"source spans retained on MIR nodes\"}");
    bool direct_has_data = input && input->direct_readonly_data_bytes > 0;
    direct_symbol_count += input ? input->direct_runtime_helper_count : 0;
    direct_symbol_count += input ? input->direct_c_import_symbol_count : 0;
    direct_symbol_count += z_direct_backend_symbol_overhead(direct.backend, direct_has_data);
    zbuf_appendf(buf, ",\"objectEmission\":{\"path\":\"%s\",\"functions\":true,\"dataSections\":%s,\"symbols\":%s,\"relocations\":\"%s\",\"symbolCount\":%zu,\"internalHelperCount\":%zu}",
                 direct.artifact_path,
                 direct_has_data ? "true" : "false",
                 "true",
                 relocations ? relocations : (direct_has_data ? "patched-internal-calls-and-data-relocations" : "patched-internal-calls-or-none-in-mvp"),
                 direct_symbol_count,
                 input && input->direct_function_count > input->direct_export_count ? input->direct_function_count - input->direct_export_count : 0);
    zbuf_append(buf, ",\"linking\":{\"linkerFlavor\":");
    append_json_string(buf, direct.linker_flavor);
    zbuf_append(buf, ",\"objectFormat\":");
    append_json_string(buf, object_format);
    zbuf_append(buf, ",\"targetLibraries\":");
    append_target_libraries_label_json(buf, links_zero_runtime, links_http_runtime, uses_c_imports);
    zbuf_append(buf, ",\"symbolMap\":");
    append_json_string(buf, "object-symbol-table");
    zbuf_append(buf, ",\"externalToolchain\":");
    append_json_string(buf, runtime_external_toolchain);
    zbuf_append(buf, ",\"toolchainSource\":");
    append_json_string(buf, toolchain_source);
    zbuf_append(buf, ",\"stripArtifacts\":false}");
    if (links_http_runtime) {
      zbuf_append(buf, ",\"httpRuntime\":");
      z_append_http_runtime_json(buf, target);
    }
    append_object_linker_plan_json(buf, input, target, &direct, &runtime_toolchain, object_format, runtime_external_toolchain, linked_executable, links_zero_runtime, links_http_runtime, uses_c_imports);
    zbuf_append(buf, ",\"targetFacts\":{\"directAvailable\":true,\"status\":");
    append_json_string(buf, z_direct_backend_status(target));
    zbuf_append(buf, ",\"selectedEmitter\":");
    append_json_string(buf, direct.selected_emitter);
    zbuf_append(buf, ",\"objectFormat\":");
    append_json_string(buf, object_format);
    zbuf_append(buf, ",\"arch\":");
    append_json_string(buf, arch);
    zbuf_append(buf, ",\"abi\":");
    append_json_string(buf, target && target->abi ? target->abi : "");
    zbuf_append(buf, ",\"libcMode\":");
    append_json_string(buf, z_target_libc_mode(target));
    zbuf_appendf(buf, ",\"requiresSysroot\":%s", z_target_requires_sysroot(target) ? "true" : "false");
    zbuf_append(buf, ",\"capabilities\":");
    append_target_capability_names_json(buf, target);
    zbuf_append(buf, ",\"fallbackPolicy\":\"explicit-direct-never-c-bridge\",\"reason\":");
    append_json_string(buf, z_direct_backend_reason(target));
    zbuf_append(buf, "}");
    zbuf_appendf(buf, ",\"moduleCount\":%zu,\"emitKind\":", input ? input->module_count : 0);
    append_json_string(buf, emit_kind ? emit_kind : "exe");
    append_direct_backend_facts_json(buf, input);
    zbuf_append(buf, "}");
    return;
  }
  zbuf_append(buf, "{\"internalIr\":{\"typeRepresentation\":\"MIR primitive value types\",\"controlFlowRepresentation\":\"direct-backend-only\",\"callRepresentation\":\"unsupported-direct-request\",\"debugRepresentation\":\"source spans retained on diagnostics\"}");
  zbuf_append(buf, ",\"objectEmission\":{\"path\":\"none\",\"functions\":false,\"dataSections\":false,\"symbols\":false,\"relocations\":\"none\"}");
  zbuf_append(buf, ",\"linking\":{\"linkerFlavor\":\"none\",\"objectFormat\":");
  append_json_string(buf, target && target->object_format ? target->object_format : "native");
  zbuf_append(buf, ",\"targetLibraries\":\"none\",\"symbolMap\":\"none\",\"externalToolchain\":\"none\",\"toolchainSource\":\"direct-backend-only\",\"stripArtifacts\":false}");
  zbuf_append(buf, ",\"linkerPlan\":{\"format\":");
  append_json_string(buf, target && target->object_format ? target->object_format : "native");
  ZToolchainPlan plan = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
  zbuf_append(buf, ",\"flavor\":\"none\",\"archives\":[],\"staticLibraries\":[],\"importLibraries\":[],\"systemLibraries\":[],\"rpaths\":[],\"loadPaths\":[],\"visibility\":\"none\",\"crossLinking\":false,\"externalToolchain\":\"none\",\"reproducible\":true,\"libcMode\":");
  append_json_string(buf, plan.libc_mode);
  zbuf_appendf(buf, ",\"requiresSysroot\":%s,\"sysrootStatus\":", plan.requires_sysroot ? "true" : "false");
  append_json_string(buf, plan.sysroot_status);
  zbuf_append(buf, "}");
  zbuf_append(buf, ",\"targetFacts\":{\"directAvailable\":false,\"fallbackPolicy\":\"removed\",\"reason\":");
  append_json_string(buf, z_direct_backend_reason(target));
  zbuf_append(buf, "}");
  zbuf_appendf(buf, ",\"moduleCount\":%zu,\"emitKind\":", input ? input->module_count : 0);
  append_json_string(buf, emit_kind ? emit_kind : "exe");
  zbuf_append(buf, "}");
}
static size_t z_max_size(size_t a, size_t b) {
  return a > b ? a : b;
}

static size_t memory_budget_stack_bytes(const SourceInput *input, const MemoryModelSummary *summary) {
  return z_max_size(input ? input->direct_stack_bytes : 0, summary ? summary->stack_estimate_bytes : 0);
}

static size_t memory_budget_max_frame_bytes(const SourceInput *input, const MemoryModelSummary *summary) {
  return z_max_size(input ? input->direct_max_frame_bytes : 0, summary ? summary->stack_estimate_bytes : 0);
}

static size_t memory_budget_static_bytes(const SourceInput *input, const MemoryModelSummary *summary) {
  return z_max_size(input ? input->direct_readonly_data_bytes : 0, summary ? summary->readonly_literal_bytes : 0);
}

static bool memory_summary_uses_allocator(const MemoryModelSummary *summary) {
  return summary && (summary->fixed_allocator_count > 0 ||
                     summary->arena_count > 0 ||
                     summary->null_allocator_count > 0 ||
                     summary->page_allocator_count > 0 ||
                     summary->general_allocator_count > 0 ||
                     summary->alloc_bytes_calls > 0 ||
                     summary->byte_buf_calls > 0 ||
                     summary->reset_calls > 0 ||
                     summary->capacity_calls > 0);
}

static bool memory_summary_uses_collections(const MemoryModelSummary *summary) {
  return summary && (summary->vec_count > 0 ||
                     summary->vec_push_calls > 0 ||
                     summary->vec_set_calls > 0 ||
                     summary->vec_view_calls > 0 ||
                     summary->vec_clear_calls > 0 ||
                     summary->vec_pop_calls > 0 ||
                     summary->vec_truncate_calls > 0 ||
                     summary->vec_remove_swap_calls > 0 ||
                     summary->vec_len_calls > 0 ||
                     summary->vec_capacity_calls > 0 ||
                     summary->collection_storage_count > 0 ||
                     summary->collection_push_calls > 0 ||
                     summary->collection_append_calls > 0 ||
                     summary->collection_view_calls > 0 ||
                     summary->collection_query_calls > 0 ||
                     summary->collection_mutation_calls > 0 ||
                     summary->byte_buf_calls > 0);
}

static void append_memory_budgets_json(ZBuf *buf, const SourceInput *input, const MemoryModelSummary *summary, bool linear_memory, const char *source) {
  size_t stack_bytes = memory_budget_stack_bytes(input, summary);
  size_t max_frame_bytes = memory_budget_max_frame_bytes(input, summary);
  size_t static_bytes = memory_budget_static_bytes(input, summary);
  size_t fixed_capacity = summary ? summary->fixed_allocator_capacity_bytes : 0;
  size_t arena_capacity = summary ? summary->arena_capacity_bytes : 0;
  size_t collection_capacity = summary ? summary->vec_capacity_bytes + summary->fixed_collection_capacity_bytes : 0;
  zbuf_appendf(buf, "{\"stackBytes\":%zu,\"maxFrameBytes\":%zu,\"staticBytes\":%zu,\"heapBytes\":0,\"arenaBytes\":%zu,\"fixedBufferBytes\":%zu,\"collectionCapacityBytes\":%zu,\"allocatorCapacityBytes\":%zu,\"allocationRequestedBytes\":%zu,\"globalHeapBytes\":0,\"linearMemoryFloorBytes\":%d,\"hiddenHeapAllocation\":false,\"unknownCapacitySites\":%zu,\"source\":",
               stack_bytes,
               max_frame_bytes,
               static_bytes,
               arena_capacity,
               fixed_capacity,
               collection_capacity,
               fixed_capacity + arena_capacity,
               summary ? summary->allocation_requested_bytes : 0,
               linear_memory ? 65536 : 0,
               summary ? summary->unknown_capacity_sites : 0);
  append_json_string(buf, source ? source : "direct-mir-and-checked-ast");
  zbuf_append(buf, "}");
}

static void append_allocator_facts_json(ZBuf *buf, const MemoryModelSummary *summary) {
  size_t fixed_capacity = summary ? summary->fixed_allocator_capacity_bytes : 0;
  size_t arena_capacity = summary ? summary->arena_capacity_bytes : 0;
  zbuf_append(buf, "[");
  zbuf_appendf(buf, "{\"kind\":\"NullAlloc\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":0,\"failure\":\"always-none\",\"hiddenGlobalAllocator\":false}",
               summary && summary->null_allocator_count > 0 ? "true" : "false",
               summary ? summary->null_allocator_count : 0);
  zbuf_appendf(buf, ", {\"kind\":\"FixedBufAlloc\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":%zu,\"storage\":\"caller-owned MutSpan<u8>\",\"failure\":\"Maybe.none on overflow\",\"resettable\":true,\"hiddenGlobalAllocator\":false}",
               summary && summary->fixed_allocator_count > 0 ? "true" : "false",
               summary ? summary->fixed_allocator_count : 0,
               fixed_capacity);
  zbuf_appendf(buf, ", {\"kind\":\"Arena\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":%zu,\"storage\":\"caller-owned MutSpan<u8>\",\"failure\":\"Maybe.none on overflow\",\"resettable\":true,\"hiddenGlobalAllocator\":false}",
               summary && summary->arena_count > 0 ? "true" : "false",
               summary ? summary->arena_count : 0,
               arena_capacity);
  zbuf_appendf(buf, ", {\"kind\":\"PageAlloc\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":0,\"status\":\"explicit-host-handle\",\"failure\":\"Maybe.none or target diagnostic before codegen\",\"hiddenGlobalAllocator\":false}",
               summary && summary->page_allocator_count > 0 ? "true" : "false",
               summary ? summary->page_allocator_count : 0);
  zbuf_appendf(buf, ", {\"kind\":\"GeneralAlloc\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":0,\"status\":\"explicit-handle-no-default-global\",\"failure\":\"Maybe.none on failure\",\"hiddenGlobalAllocator\":false}",
               summary && summary->general_allocator_count > 0 ? "true" : "false",
               summary ? summary->general_allocator_count : 0);
  zbuf_append(buf, "]");
}

static void append_allocation_instrumentation_json(ZBuf *buf, const MemoryModelSummary *summary) {
  size_t sites = summary ? summary->alloc_bytes_calls + summary->byte_buf_calls : 0;
  zbuf_appendf(buf, "{\"source\":\"checked-ast\",\"allocationSites\":%zu,\"allocBytesCalls\":%zu,\"byteBufCalls\":%zu,\"bytesRequestedByLiteralCalls\":%zu,\"failureSignal\":\"Maybe.none\",\"hiddenGlobalAllocator\":false,\"runtimeCountersPayAsUsed\":true,\"hooks\":[\"attempts\",\"successes\",\"failures\",\"bytesRequested\",\"bytesGranted\",\"peakLiveBytes\"]}",
               sites,
               summary ? summary->alloc_bytes_calls : 0,
               summary ? summary->byte_buf_calls : 0,
               summary ? summary->allocation_requested_bytes : 0);
}

static void append_collection_facts_json(ZBuf *buf, const MemoryModelSummary *summary) {
  zbuf_appendf(buf, "{\"Vec\":{\"used\":%s,\"instances\":%zu,\"pushCalls\":%zu,\"setCalls\":%zu,\"viewCalls\":%zu,\"clearCalls\":%zu,\"popCalls\":%zu,\"truncateCalls\":%zu,\"removeSwapCalls\":%zu,\"capacityCalls\":%zu,\"capacityBytes\":%zu,\"growth\":\"fixed-capacity caller storage\",\"pushFailure\":\"returns false\",\"setFailure\":\"returns false\",\"cleanup\":\"caller owns storage\",\"hiddenAllocation\":false}",
               summary && summary->vec_count > 0 ? "true" : "false",
               summary ? summary->vec_count : 0,
               summary ? summary->vec_push_calls : 0,
               summary ? summary->vec_set_calls : 0,
               summary ? summary->vec_view_calls : 0,
               summary ? summary->vec_clear_calls : 0,
               summary ? summary->vec_pop_calls : 0,
               summary ? summary->vec_truncate_calls : 0,
               summary ? summary->vec_remove_swap_calls : 0,
               summary ? summary->vec_capacity_calls : 0,
               summary ? summary->vec_capacity_bytes : 0);
  zbuf_appendf(buf, ",\"FixedStorage\":{\"used\":%s,\"storageSites\":%zu,\"pushCalls\":%zu,\"appendCalls\":%zu,\"viewCalls\":%zu,\"queryCalls\":%zu,\"mutationCalls\":%zu,\"capacityBytes\":%zu,\"lengthModel\":\"caller-tracked usize\",\"overflow\":\"returns unchanged length\",\"cleanup\":\"caller owns storage\",\"hiddenAllocation\":false}",
               summary && (summary->collection_storage_count > 0 ||
                            summary->collection_push_calls > 0 ||
                            summary->collection_append_calls > 0 ||
                            summary->collection_view_calls > 0 ||
                            summary->collection_query_calls > 0 ||
                            summary->collection_mutation_calls > 0) ? "true" : "false",
               summary ? summary->collection_storage_count : 0,
               summary ? summary->collection_push_calls : 0,
               summary ? summary->collection_append_calls : 0,
               summary ? summary->collection_view_calls : 0,
               summary ? summary->collection_query_calls : 0,
               summary ? summary->collection_mutation_calls : 0,
               summary ? summary->fixed_collection_capacity_bytes : 0);
  zbuf_appendf(buf, ",\"ByteBuf\":{\"used\":%s,\"allocationCalls\":%zu,\"ownership\":\"owned ByteBuf backed by explicit allocator storage\",\"cleanup\":\"owned value releases logical ownership; allocator storage remains explicit\",\"hiddenAllocation\":false}",
               summary && summary->byte_buf_calls > 0 ? "true" : "false",
               summary ? summary->byte_buf_calls : 0);
  zbuf_append(buf, "}");
}

static void append_memory_regions_json(ZBuf *buf, const SourceInput *input, const MemoryModelSummary *summary, bool linear_memory) {
  (void)linear_memory;
  zbuf_append(buf, "[");
  zbuf_appendf(buf, "{\"name\":\"stack\",\"kind\":\"stack\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"direct-mir-frame-layout\"}",
               memory_budget_stack_bytes(input, summary));
  zbuf_appendf(buf, ", {\"name\":\"max-frame\",\"kind\":\"stack-frame\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"largest-direct-function-frame\"}",
               memory_budget_max_frame_bytes(input, summary));
  zbuf_appendf(buf, ", {\"name\":\"readonly-data\",\"kind\":\"static\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"direct-data-segments\"}",
               memory_budget_static_bytes(input, summary));
  zbuf_append(buf, ", {\"name\":\"heap\",\"kind\":\"heap\",\"bytes\":0,\"payAsUsed\":true,\"source\":\"no hidden global heap\"}");
  zbuf_appendf(buf, ", {\"name\":\"arena-storage\",\"kind\":\"arena\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"caller-owned arena buffers\"}",
               summary ? summary->arena_capacity_bytes : 0);
  zbuf_appendf(buf, ", {\"name\":\"fixed-buffer-storage\",\"kind\":\"allocator-capacity\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"caller-owned FixedBufAlloc buffers\"}",
               summary ? summary->fixed_allocator_capacity_bytes : 0);
  zbuf_appendf(buf, ", {\"name\":\"collection-storage\",\"kind\":\"collection-capacity\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"caller-owned collection storage\"}",
               summary ? summary->vec_capacity_bytes + summary->fixed_collection_capacity_bytes : 0);
  zbuf_append(buf, "]");
}

static void append_memory_capability_facts_json(ZBuf *buf, const CapabilitySummary *caps, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"requiredCapabilities\":");
  append_capability_json_array(buf, caps);
  zbuf_append(buf, ",\"targetSupport\":");
  append_target_capability_facts_json(buf, target, caps);
  zbuf_append(buf, ",\"filesystem\":");
  append_json_string(buf, caps && caps->fs
                        ? (z_target_has_capability(target, "fs") ? "available" : "target-denied")
                        : "not-required");
  zbuf_append(buf, ",\"worldStdio\":");
  append_json_string(buf, caps && caps->world
                        ? (z_target_has_capability(target, "stdio") ? "available" : "target-denied")
                        : "not-required");
  zbuf_append(buf, "}");
}

static void append_direct_memory_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command, const IrProgram *ir) {
  bool graph_input = command && z_program_graph_artifact_source_present(&command->graph_source);
  CapabilitySummary caps = program_or_ir_capabilities(program, graph_input ? ir : NULL);
  HelperUseSummary used_helpers = program_used_helpers(program);
  MemoryModelSummary memory_summary = graph_input ? memory_model_summary_from_ir(ir, target) : memory_model_summary_from_program(program, target);
  bool uses_allocator = (input && input->direct_allocator_helper_count > 0) || memory_summary_uses_allocator(&memory_summary);
  bool uses_buffers = (input && input->direct_buffer_helper_count > 0) || memory_summary_uses_collections(&memory_summary);
  bool linear_memory = input && (input->direct_stack_bytes > 0 ||
                                 input->direct_readonly_data_bytes > 0 ||
                                 input->direct_runtime_helper_count > 0);
  linear_memory = linear_memory ||
                  memory_summary.stack_estimate_bytes > 0 ||
                  memory_summary.readonly_literal_bytes > 0 ||
                  uses_allocator ||
                  uses_buffers;
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : NULL);
  if (command) append_program_graph_artifact_source_json(buf, &command->graph_source);
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : "host");
  zbuf_append(buf, ",\n  \"hostTarget\": ");
  append_json_string(buf, z_host_target());
  zbuf_append(buf, ",\n  \"profile\": ");
  append_json_string(buf, command && command->profile ? command->profile : "release");
  zbuf_append(buf, ",\n  \"safetyFacts\": ");
  append_safety_facts_json(buf, command && command->profile ? command->profile : "release");
  zbuf_append(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"loweredIrBytes\": ");
  zbuf_appendf(buf, "%zu", input ? input->lowered_ir_bytes : 0);
  zbuf_append(buf, ",\n  \"directFacts\": {\"functionCount\":");
  zbuf_appendf(buf, "%zu,\"exportCount\":%zu,\"stackBytes\":%zu,\"maxFrameBytes\":%zu,\"readonlyDataBytes\":%zu,\"allocatorHelperCount\":%zu,\"bufferHelperCount\":%zu,\"runtimeHelperCount\":%zu,\"moduleCount\":%zu}",
               input ? input->direct_function_count : 0,
               input ? input->direct_export_count : 0,
               input ? input->direct_stack_bytes : 0,
               input ? input->direct_max_frame_bytes : 0,
               input ? input->direct_readonly_data_bytes : 0,
               input ? input->direct_allocator_helper_count : 0,
               input ? input->direct_buffer_helper_count : 0,
               input ? input->direct_runtime_helper_count : 0,
               input ? input->module_count : 0);
  zbuf_append(buf, ",\n  \"memory\": {\"linearMemory\":");
  zbuf_append(buf, linear_memory ? "true" : "false");
  zbuf_appendf(buf, ",\"memoryPageBytes\":65536,\"stackBase\":65536,\"stackBytes\":%zu,\"maxFrameBytes\":%zu,\"readonlyDataBytes\":%zu,\"heapStart\":null,\"heapEnd\":null,\"heapPolicy\":",
               memory_budget_stack_bytes(input, &memory_summary),
               memory_budget_max_frame_bytes(input, &memory_summary),
               memory_budget_static_bytes(input, &memory_summary));
  append_json_string(buf, uses_allocator ? "explicit-allocator-no-global-heap" : "not-required");
  zbuf_append(buf, ",\"allocator\":");
  append_json_string(buf, uses_allocator ? "explicit" : "none");
  zbuf_append(buf, ",\"buffer\":");
  append_json_string(buf, uses_buffers ? "explicit collections" : "none");
  zbuf_append(buf, ",\"hiddenHeapAllocation\":false,\"boundsTraps\":\"pay-as-used\"},\n  \"memoryBudgets\": ");
  append_memory_budgets_json(buf, input, &memory_summary, linear_memory, graph_input ? "typed-graph-and-direct-mir" : "direct-mir-and-checked-ast");
  zbuf_append(buf, ",\n  \"allocatorFacts\": ");
  append_allocator_facts_json(buf, &memory_summary);
  zbuf_append(buf, ",\n  \"allocationInstrumentation\": ");
  append_allocation_instrumentation_json(buf, &memory_summary);
  zbuf_append(buf, ",\n  \"collectionFacts\": ");
  append_collection_facts_json(buf, &memory_summary);
  zbuf_append(buf, ",\n  \"regions\": ");
  append_memory_regions_json(buf, input, &memory_summary, linear_memory);
  zbuf_append(buf, ",\n  \"capabilityFacts\": ");
  append_memory_capability_facts_json(buf, &caps, target);
  zbuf_append(buf, ",\n  \"usedStdlibHelpers\": ");
  append_used_stdlib_helpers_json(buf, &used_helpers);
  zbuf_append(buf, ",\n  \"runtimeShims\": ");
  append_runtime_shims_json(buf, NULL, &caps);
  zbuf_append(buf, ",\n  \"objectBackend\": ");
  append_object_backend_json(buf, input, target, command, "mem");
  zbuf_append(buf, ",\n  \"mir\": {\"valid\":");
  zbuf_append(buf, ir && ir->mir_valid ? "true" : "false");
  zbuf_append(buf, ",\"expected\":");
  append_json_string(buf, ir ? ir->mir_expected : "");
  zbuf_append(buf, ",\"actual\":");
  append_json_string(buf, ir ? ir->mir_actual : "");
  zbuf_append(buf, ",\"message\":");
  append_json_string(buf, ir ? ir->mir_message : "");
  zbuf_append(buf, "},\n  \"compilerPhases\": ");
  append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  if (graph_input) append_compiler_caches_json_ex(buf, input, target, command ? command->profile : "release", "program-graph", command->graph_source.graph_hash);
  else append_compiler_caches_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, ",\n  \"incrementalInvalidation\": ");
  if (graph_input) append_incremental_invalidations_json_ex(buf, input, target, command ? command->profile : "release", command->graph_source.artifact, command->graph_source.graph_hash, command->graph_source.lowering);
  else append_incremental_invalidations_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, "\n}\n");
  helper_summary_free(&used_helpers);
}

static void print_build_graph_source_json(const Command *command, const SourceInput *input) {
  if (!command || !z_program_graph_artifact_source_present(&command->graph_source)) return;
  printf(",\n  \"graph\": {\"artifact\": ");
  print_json_string(command->graph_source.artifact ? command->graph_source.artifact : input->source_file);
  printf(", \"canonicalSource\": %s, \"moduleIdentity\": ", command->graph_source.canonical_source ? "true" : "false");
  print_json_string(command->graph_source.module_identity ? command->graph_source.module_identity : "");
  printf(", \"graphHash\": ");
  print_json_string(command->graph_source.graph_hash);
  printf(", \"lowering\": ");
  print_json_string(command->graph_source.lowering ? command->graph_source.lowering : "direct-program-graph");
  if (command->graph_source.source_projection_state) {
    printf(", \"sourceProjectionState\": ");
    print_json_string(command->graph_source.source_projection_state);
  }
  printf("}");
}

static void print_build_json(const Command *command, const SourceInput *input, const Program *program, const IrProgram *ir, const ZTargetInfo *target, const char *emit_kind, const char *artifact_path, long long artifact_bytes, long long generated_c_bytes, long long elapsed_ms) {
  bool llvm_ir_output = command && command->emit == EMIT_LLVM_IR && z_backend_request_is_llvm(command->backend, emit_kind);
  bool llvm_native_output = command_uses_llvm_native_exe(command, emit_kind);
  printf("{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  print_json_string(input->source_file);
  print_build_graph_source_json(command, input);
  printf(",\n  \"emit\": ");
  print_json_string(emit_kind);
  printf(",\n  \"hostTarget\": ");
  print_json_string(z_host_target());
  printf(",\n  \"target\": ");
  print_json_string(target->name);
  printf(",\n  \"profile\": ");
  print_json_string(command->profile);
  printf(",\n  \"legacy\": false");
  printf(",\n  \"legacyBackend\": null");
  printf(",\n  \"warnings\": []");
  printf(",\n  \"compiler\": ");
  ZToolchainPlan direct_plan;
  bool linked_executable = source_uses_linked_executable_path(input, emit_kind);
  bool direct_toolchain = !linked_executable && z_direct_backend_toolchain_plan_for_emit_kind(target, emit_kind, z_backend_direct_request_name(command ? command->backend : NULL), &direct_plan);
  if (llvm_ir_output) print_json_string("zero-c-llvm-ir");
  else if (llvm_native_output) {
    ZLlvmToolchainPlan llvm_plan = z_llvm_toolchain_plan(target);
    print_json_string(llvm_plan.compiler);
  }
  else if (direct_toolchain) print_json_string(direct_plan.driver_kind);
  else print_json_string(build_compiler_label(command, target));
  printf(",\n  \"toolchain\": ");
  if (llvm_ir_output) {
    printf("{\"driverKind\":\"none\",\"selectionSource\":\"not-required\",\"compiler\":\"zero-c\",\"targetTriple\":");
    print_json_string(target && target->zig_target ? target->zig_target : "");
    printf(",\"linkerFlavor\":\"none\",\"libcMode\":\"none\",\"requiresSysroot\":false,\"sysrootEnv\":\"\",\"sysrootStatus\":\"not-required\",\"usesTargetFlag\":false,\"usesToolchainCache\":false,\"stripArtifact\":false}");
  } else if (llvm_native_output) {
    ZBuf llvm_toolchain_json;
    zbuf_init(&llvm_toolchain_json);
    z_append_llvm_toolchain_plan_json(&llvm_toolchain_json, target);
    fputs(llvm_toolchain_json.data, stdout);
    zbuf_free(&llvm_toolchain_json);
  } else if (direct_toolchain) {
    ZBuf direct_toolchain_json;
    zbuf_init(&direct_toolchain_json);
    append_toolchain_plan_value_json(&direct_toolchain_json, &direct_plan);
    fputs(direct_toolchain_json.data, stdout);
    zbuf_free(&direct_toolchain_json);
  } else {
    ZBuf toolchain_json;
    zbuf_init(&toolchain_json);
    append_toolchain_plan_json(&toolchain_json, command, target);
    fputs(toolchain_json.data, stdout);
    zbuf_free(&toolchain_json);
  }
  printf(",\n  \"artifactPath\": ");
  if (artifact_path) print_json_string(artifact_path);
  else printf("null");
  printf(",\n  \"artifactBytes\": ");
  if (artifact_bytes >= 0) printf("%lld", artifact_bytes);
  else printf("null");
  printf(",\n  \"generatedCBytes\": %lld,\n  \"loweredIrBytes\": %zu,\n  \"elapsedMs\": %lld,\n  \"targetSupport\": {\"fsAvailable\": %s, \"directStatus\": ", generated_c_bytes, input->lowered_ir_bytes, elapsed_ms, z_target_has_capability(target, "fs") ? "true" : "false");
  print_json_string(z_direct_backend_status(target));
  printf(", \"directObjectEmitter\": ");
  print_json_string(z_direct_object_emitter(target));
  printf(", \"directExeEmitter\": ");
  print_json_string(z_direct_exe_emitter(target));
  if (llvm_ir_output || llvm_native_output) {
    ZLlvmToolchainPlan llvm_plan = z_llvm_toolchain_plan(target);
    printf(", \"backendFamily\": \"llvm\", \"llvmStatus\": ");
    print_json_string(llvm_ir_output ? "ir-only" : llvm_plan.status);
    printf(", \"fallbackPolicy\": \"none\", \"backendLifecycle\": ");
    fputs(z_llvm_backend_lifecycle_json_text(), stdout);
    printf("}");
  } else {
    printf(", \"fallbackPolicy\": \"explicit-direct-never-c-bridge\"}");
  }
  ZBuf extra;
  zbuf_init(&extra);
  zbuf_append(&extra, ",\n  \"compilerPhases\": ");
  append_compiler_phases_json(&extra, input);
  if (command && z_program_graph_artifact_source_present(&command->graph_source)) {
    zbuf_append(&extra, ",\n  \"graphBuild\": ");
    append_graph_build_timing_facts_json(&extra, input);
  }
  zbuf_append(&extra, ",\n  \"compilerCaches\": ");
  if (command && z_program_graph_artifact_source_present(&command->graph_source)) append_compiler_caches_json_ex(&extra, input, target, command->profile, "program-graph", command->graph_source.graph_hash);
  else append_compiler_caches_json(&extra, input, target, command->profile);
  zbuf_append(&extra, ",\n  \"package\": ");
  append_package_metadata_json(&extra, input, target);
  zbuf_append(&extra, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(&extra, input, target, command->profile);
  zbuf_append(&extra, ",\n  \"incrementalInvalidation\": ");
  if (command && z_program_graph_artifact_source_present(&command->graph_source)) append_incremental_invalidations_json_ex(&extra, input, target, command->profile, command->graph_source.artifact, command->graph_source.graph_hash, command->graph_source.lowering);
  else append_incremental_invalidations_json(&extra, input, target, command->profile);
  zbuf_append(&extra, ",\n  \"profileSemantics\": ");
  append_profile_semantics_json(&extra, command->profile);
  zbuf_append(&extra, ",\n  \"safetyFacts\": ");
  append_safety_facts_json(&extra, command->profile);
  zbuf_append(&extra, ",\n  \"profileCatalog\": ");
  append_profile_catalog_json(&extra);
  zbuf_append(&extra, ",\n  \"profileBudget\": ");
  append_profile_budget_json(&extra, command->profile);
  zbuf_append(&extra, ",\n  \"releaseTargetContract\": ");
  append_release_target_contract_json(&extra, input, target, command, emit_kind);
  zbuf_append(&extra, ",\n  \"objectBackend\": ");
  if (llvm_ir_output) z_append_llvm_ir_backend_json(&extra, input, target, emit_kind);
  else if (llvm_native_output) z_append_llvm_native_backend_json(&extra, input, target, emit_kind);
  else append_object_backend_json(&extra, input, target, command, emit_kind);
  CapabilitySummary routing_caps = program_or_ir_capabilities(program, ir);
  zbuf_append(&extra, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(&extra, "build", emit_kind, program, &routing_caps, target);
  fputs(extra.data, stdout);
  zbuf_free(&extra);
  printf("\n}\n");
}

static long long file_size_or_negative(const char *path) {
  struct stat st;
  if (!path || stat(path, &st) != 0) return -1;
  return (long long)st.st_size;
}

static char *path_with_suffix(const char *path, const char *suffix) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, path ? path : "");
  zbuf_append(&buf, suffix ? suffix : "");
  return buf.data;
}

static char *repository_graph_default_artifact_source_path(const Command *command, const SourceInput *input) {
  const char *fallback = input && input->source_file ? input->source_file : "app";
  if (!command || !command->repository_graph_input) return z_strdup(fallback);
  const char *manifest_input = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
  char *manifest_path = z_manifest_path_for_input(manifest_input);
  if (!manifest_path) return z_strdup(fallback);
  ZDiag diag = {0};
  char *manifest = z_read_file(manifest_path, &diag);
  if (!manifest) {
    free(manifest_path);
    return z_strdup(fallback);
  }
  ZManifest parsed_manifest = {0};
  bool ok = z_parse_manifest_json(manifest, &parsed_manifest, &diag);
  char *root = ok && parsed_manifest.main_path && is_zero_source_path(parsed_manifest.main_path) ? direct_dirname_of(manifest_path) : NULL;
  char *source_path = root ? direct_join_path(root, parsed_manifest.main_path) : NULL;
  free(root);
  z_free_manifest(&parsed_manifest);
  free(manifest);
  free(manifest_path);
  return source_path ? source_path : z_strdup(fallback);
}

static char *command_default_out_path(const Command *command, const SourceInput *input) {
  char *source_path = repository_graph_default_artifact_source_path(command, input);
  char *out_path = z_default_out_path(source_path);
  free(source_path);
  return out_path;
}

static bool command_uses_ephemeral_run_artifact(const Command *command) {
  return command && cli_arg_is(command->command, "run") && !command->out;
}

static char *command_default_exe_base_path(const Command *command, const SourceInput *input) {
  char *base = command_default_out_path(command, input);
  if (!command_uses_ephemeral_run_artifact(command)) return base;
  ZBuf out;
  zbuf_init(&out);
#if defined(_WIN32)
  long pid = (long)_getpid();
#else
  long pid = (long)getpid();
#endif
  zbuf_appendf(&out, "%s.run-%ld", base ? base : ".zero/out/main", pid);
  free(base);
  return out.data ? out.data : z_strdup("");
}

static void command_remove_ephemeral_run_artifact(const Command *command, const char *exe_file) {
  if (command_uses_ephemeral_run_artifact(command)) (void)z_process_remove_regular_file(exe_file);
}

static int print_version_command(bool json) {
  if (!json) {
    printf("zero %s (build %s)\n", ZERO_VERSION, ZERO_BUILD_HASH);
    return 0;
  }

  const char *zero_cc = getenv("ZERO_CC");
  bool target_cc_override = zero_cc && zero_cc[0];
  bool bundled_target_cc_available = command_available("zig");
  bool target_cc_available = target_cc_override || bundled_target_cc_available;
  const char *const zig_version_argv[] = {"zig", "version", NULL};
  char *target_cc_version = target_cc_override ? z_strdup("configured via ZERO_CC") : (bundled_target_cc_available ? z_process_first_stdout_line(zig_version_argv, true) : z_strdup(""));

  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"version\": ");
  append_json_string(&buf, ZERO_VERSION);
  zbuf_append(&buf, ",\n  \"commit\": ");
  append_json_string(&buf, zero_commit());
  zbuf_append(&buf, ",\n  \"host\": ");
  append_json_string(&buf, z_host_target());
  zbuf_append(&buf, ",\n  \"backend\": \"zero-c\",\n  \"targets\": ");
  z_append_target_names_json(&buf);
  zbuf_append(&buf, ",\n  \"targetCompiler\": {\"available\": ");
  zbuf_append(&buf, target_cc_available ? "true" : "false");
  zbuf_append(&buf, ", \"kind\": \"target-capable C compiler\", \"version\": ");
  append_json_string(&buf, target_cc_version);
  zbuf_append(&buf, "},\n  \"crossCompilation\": {\"ready\": ");
  zbuf_append(&buf, target_cc_available ? "true" : "false");
  zbuf_append(&buf, "}\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
  free(target_cc_version);
  return 0;
}

static bool import_init_template_graph_store(const char *root, ZProgramGraphStoreFormat store_format, ZDiag *diag);

static char *graph_init_package_name(const char *path) {
  char cwd[4096];
  if (!path || !path[0] || strcmp(path, ".") == 0) {
    if (getcwd(cwd, sizeof(cwd))) path = cwd;
  }
  const char *end = path ? path + strlen(path) : NULL;
  while (end && end > path && end[-1] == '/') end--;
  const char *start = end;
  while (start && start > path && start[-1] != '/') start--;
  if (!start || start == end) return z_strdup("zero-app");
  ZBuf name;
  zbuf_init(&name);
  for (const unsigned char *cursor = (const unsigned char *)start; cursor < (const unsigned char *)end; cursor++) {
    if (isalnum(*cursor) || *cursor == '-' || *cursor == '_' || *cursor == '.') zbuf_append_char(&name, (char)*cursor);
    else zbuf_append_char(&name, '-');
  }
  if (name.len == 0) zbuf_append(&name, "zero-app");
  return name.data ? name.data : z_strdup("zero-app");
}

static bool graph_init_reject_existing(const char *root, ZDiag *diag) {
  char *toml_manifest = join_cli_path(root, "zero.toml");
  char *json_manifest = join_cli_path(root, "zero.json");
  char *store = join_cli_path(root, "zero.graph");
  bool exists = path_exists(toml_manifest) || path_exists(json_manifest) || path_exists(store);
  if (exists) {
    diag->code = 2002;
    z_diag_set_path_copy(diag, path_exists(toml_manifest) ? toml_manifest : (path_exists(json_manifest) ? json_manifest : store));
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "graph-first project already exists");
    snprintf(diag->expected, sizeof(diag->expected), "project path without zero.toml, zero.json, or zero.graph");
    snprintf(diag->actual, sizeof(diag->actual), "%s", diag->path);
    snprintf(diag->help, sizeof(diag->help), "choose a new project path or remove the existing graph project files");
    free(toml_manifest);
    free(json_manifest);
    free(store);
    return false;
  }
  free(toml_manifest);
  free(json_manifest);
  free(store);
  return true;
}

static void graph_init_append_module_graph(ZProgramGraph *graph, const char *package_name) {
  z_program_graph_init(graph);
  free(graph->module_identity);
  ZBuf identity;
  zbuf_init(&identity);
  zbuf_append(&identity, "package:");
  zbuf_append(&identity, package_name && package_name[0] ? package_name : "zero-app");
  zbuf_append(&identity, "@0.1.0");
  graph->module_identity = identity.data ? identity.data : z_strdup("package:zero-app@0.1.0");
  graph->canonical_source = true;
  graph->node_cap = 1;
  graph->node_len = 1;
  graph->nodes = z_checked_calloc(1, sizeof(ZProgramGraphNode));
  graph->nodes[0].id = z_strdup("#mod_main");
  graph->nodes[0].kind = Z_PROGRAM_GRAPH_NODE_MODULE;
  graph->nodes[0].name = z_strdup("main");
  graph->nodes[0].path = z_strdup("src/main.0");
  graph->nodes[0].line = 1;
  graph->nodes[0].column = 1;
  z_program_graph_finalize_identities(graph);
}

static bool command_repository_store_format(const Command *command, ZProgramGraphStoreFormat fallback, ZProgramGraphStoreFormat *out, ZDiag *diag) {
  if (!command || !command->store_format || !command->store_format[0]) {
    if (out) *out = fallback;
    return true;
  }
  if (z_program_graph_store_format_from_name(command->store_format, out)) return true;
  if (diag) {
    *diag = (ZDiag){0};
    diag->code = 2002;
    diag->path = command->input ? command->input : "zero.graph";
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "repository graph store format is not supported");
    snprintf(diag->expected, sizeof(diag->expected), "--format text|binary");
    snprintf(diag->actual, sizeof(diag->actual), "%s", command->store_format);
    snprintf(diag->help, sizeof(diag->help), "omit --format for the binary zero.graph default, pass --format text for readable debug stores, or pass --format binary for explicit binary graph artifacts");
  }
  return false;
}

static int run_graph_init_template_command(const Command *command, ZDiag *diag) {
  const char *kind = command->init_template;
  if (!z_init_template_kind_is_known(kind)) {
    diag->code = 2002;
    diag->path = command->input ? command->input : ".";
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "init template is not supported");
    snprintf(diag->expected, sizeof(diag->expected), "--template cli|lib|package");
    snprintf(diag->actual, sizeof(diag->actual), "--template %s", kind ? kind : "");
    snprintf(diag->help, sizeof(diag->help), "omit --template for an empty graph-first package, or choose cli, lib, or package");
    print_command_diag(command, diag->path, diag);
    return 1;
  }
  if (!graph_init_reject_existing(command->input, diag) || !z_init_template_reject_existing_outputs(command->input, kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }
  ZProgramGraphStoreFormat store_format = Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY;
  if (!command_repository_store_format(command, Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY, &store_format, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }
  char *package_name = graph_init_package_name(command->input);
  const char *manifest_file_name = NULL;
  bool ok = z_init_template_write_files(command->input, kind, package_name, command->manifest_format, &manifest_file_name, diag);
  if (ok) ok = import_init_template_graph_store(command->input, store_format, diag);
  if (ok && command->json) {
    ZBuf json;
    zbuf_init(&json);
    char *manifest_path = join_cli_path(command->input, manifest_file_name ? manifest_file_name : "zero.toml");
    char *store_path = join_cli_path(command->input, "zero.graph");
    zbuf_append(&json, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"project\": ");
    append_json_string(&json, command->input);
    zbuf_append(&json, ",\n  \"template\": ");
    append_json_string(&json, kind);
    zbuf_append(&json, ",\n  \"compilerInput\": \"repository-graph\",\n  \"writes\": [");
    append_json_string(&json, manifest_path);
    zbuf_append(&json, ", ");
    append_json_string(&json, store_path);
    zbuf_append(&json, "],\n  \"sourceProjection\": {\"path\": ");
    append_json_string(&json, z_init_template_projection_path(kind));
    zbuf_append(&json, ", \"materialized\": true},\n  \"next\": [");
    ZBuf next;
    zbuf_init(&next);
    if (strcmp(command->input, ".") != 0) {
      zbuf_append(&next, "cd ");
      zbuf_append(&next, command->input);
      zbuf_append(&next, " && ");
    }
    if (strcmp(kind, "lib") == 0) zbuf_append(&next, "zero check && zero test");
    else zbuf_append(&next, "zero check && zero test && zero run");
    append_json_string(&json, next.data ? next.data : "");
    zbuf_append(&json, "]\n}\n");
    fputs(json.data, stdout);
    zbuf_free(&next);
    zbuf_free(&json);
    free(manifest_path);
    free(store_path);
  } else if (ok) {
    printf("graph template init ok\n");
    printf("template: %s\n", kind);
    printf("wrote: %s/%s\n", command->input, manifest_file_name ? manifest_file_name : "zero.toml");
    printf("wrote: %s/zero.graph\n", command->input);
    if (strcmp(command->input, ".") == 0) {
      if (strcmp(kind, "lib") == 0) printf("next: zero check && zero test\n");
      else printf("next: zero check && zero test && zero run\n");
    } else {
      if (strcmp(kind, "lib") == 0) printf("next: cd %s && zero check && zero test\n", command->input);
      else printf("next: cd %s && zero check && zero test && zero run\n", command->input);
    }
  }
  if (!ok) print_command_diag(command, diag->path ? diag->path : command->input, diag);
  free(package_name);
  return ok ? 0 : 1;
}

static int run_graph_init_command(const Command *command, ZDiag *diag) {
  if (command->out) {
    diag->code = 2002;
    diag->path = command->out;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "graph init writes repository files and does not support --out");
    snprintf(diag->expected, sizeof(diag->expected), "zero init [--manifest toml|json] [--format text|binary] [--template cli|lib|package] [project-path]");
    snprintf(diag->actual, sizeof(diag->actual), "zero init --out");
    snprintf(diag->help, sizeof(diag->help), "remove --out; init writes zero.toml or zero.json plus zero.graph in the selected project");
    print_command_diag(command, command->out, diag);
    return 1;
  }
  if (command->init_template) return run_graph_init_template_command(command, diag);
  if (!command->input || !command->input[0]) {
    diag->code = 2002;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "graph init requires an input directory");
    snprintf(diag->expected, sizeof(diag->expected), "zero init [--manifest toml|json] [--format text|binary] [--template cli|lib|package] [project-path]");
    snprintf(diag->actual, sizeof(diag->actual), "missing input directory");
    print_command_diag(command, "<graph-init>", diag);
    return 1;
  }
  struct stat st;
  if (zero_lstat(command->input, &st) == 0 && !S_ISDIR(st.st_mode)) {
    diag->code = 2002;
    diag->path = command->input;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "graph init path is not a directory");
    snprintf(diag->expected, sizeof(diag->expected), "directory project path");
    snprintf(diag->actual, sizeof(diag->actual), "%s", command->input);
    print_command_diag(command, command->input, diag);
    return 1;
  }
  if (!graph_init_reject_existing(command->input, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }
  ZProgramGraphStoreFormat store_format = Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY;
  if (!command_repository_store_format(command, Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY, &store_format, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }
  char *package_name = graph_init_package_name(command->input);
  const char *manifest_file_name = NULL;
  if (!z_init_write_manifest(command->input, package_name, "src/main.0", command->manifest_format, false, &manifest_file_name, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    free(package_name);
    return 1;
  }
  bool ok = true;
  if (ok) {
    ZProgramGraph graph = {0};
    graph_init_append_module_graph(&graph, package_name);
    char *store_path = join_cli_path(command->input, "zero.graph");
    ok = z_program_graph_store_write_generated_path_format(store_path, &graph, store_format, NULL, diag);
    z_program_graph_free(&graph);
    if (ok && command->json) {
      ZBuf json;
      zbuf_init(&json);
      ZBuf patch_command;
      zbuf_init(&patch_command);
      zbuf_append(&patch_command, "cd ");
      zbuf_append(&patch_command, command->input);
      zbuf_append(&patch_command, " && zero patch --op 'addMain'");
      zbuf_append(&json, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"project\": ");
      append_json_string(&json, command->input);
      zbuf_append(&json, ",\n  \"compilerInput\": \"repository-graph\",\n  \"writes\": [");
      char *manifest_path = join_cli_path(command->input, manifest_file_name);
      append_json_string(&json, manifest_path);
      zbuf_append(&json, ", ");
      append_json_string(&json, store_path);
      zbuf_append(&json, "],\n  \"sourceProjection\": {\"path\": \"src/main.0\", \"materialized\": false},\n  \"next\": [");
      append_json_string(&json, patch_command.data ? patch_command.data : "");
      zbuf_append(&json, "]\n}\n");
      fputs(json.data, stdout);
      zbuf_free(&json);
      zbuf_free(&patch_command);
      free(manifest_path);
    } else if (ok) {
      printf("graph project init ok\n");
      printf("wrote: %s/%s\n", command->input, manifest_file_name);
      printf("wrote: %s/zero.graph\n", command->input);
      if (strcmp(command->input, ".") == 0) printf("next: zero patch --op 'addMain'\n");
      else printf("next: cd %s && zero patch --op 'addMain'\n", command->input);
    }
    free(store_path);
  }
  if (!ok) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
  }
  free(package_name);
  return ok ? 0 : 1;
}

static int clean_command(const Command *command) {
  const char *default_paths[] = {".zero/out", NULL};
  const char *all_paths[] = {
    ".zero/out",
    ".zero/native-test",
    ".zero/conformance",
    ".zero/command-contracts",
    ".zero/generated-c-audit",
    ".zero/bench",
    NULL
  };
  const char **paths = command->all ? all_paths : default_paths;
  ZBuf deleted;
  zbuf_init(&deleted);
  for (size_t i = 0; paths[i]; i++) {
    if (!remove_tree(paths[i], &deleted)) {
      fprintf(stderr, "zero clean: failed to remove %s: %s\n", paths[i], strerror(errno));
      zbuf_free(&deleted);
      return 1;
    }
  }
  if (deleted.len == 0) {
    printf("nothing to clean\n");
  } else {
    printf("removed:\n%s\n", deleted.data);
    if (command->all) printf("preserved .zero/bin so the local zero wrapper still works\n");
  }
  zbuf_free(&deleted);
  return 0;
}

static const char *worse_status(const char *current, const char *next) {
  if (strcmp(current, "error") == 0 || strcmp(next, "error") == 0) return "error";
  if (strcmp(current, "warning") == 0 || strcmp(next, "warning") == 0) return "warning";
  return "ok";
}

static void append_doctor_check_json(ZBuf *buf, bool *first, const char *name, const char *status, const char *message) {
  if (!*first) zbuf_append(buf, ",\n");
  *first = false;
  zbuf_append(buf, "    {\"name\": ");
  append_json_string(buf, name);
  zbuf_append(buf, ", \"status\": ");
  append_json_string(buf, status);
  zbuf_append(buf, ", \"message\": ");
  append_json_string(buf, message);
  zbuf_append(buf, "}");
}

static char *doctor_path_message(bool *ok) {
  const char *path = getenv("PATH");
  if (!path || !path[0]) {
    *ok = false;
    return z_strdup("PATH is empty");
  }
  char separator =
#ifdef _WIN32
    ';';
#else
    ':';
#endif
  size_t bad = 0;
  size_t entries = 0;
  const char *cursor = path;
  while (true) {
    const char *end = strchr(cursor, separator);
    size_t len = end ? (size_t)(end - cursor) : strlen(cursor);
    entries++;
    if (len == 0) {
      bad++;
    } else {
      char *entry = z_strndup(cursor, len);
      struct stat st;
      if (stat(entry, &st) != 0 || !S_ISDIR(st.st_mode)) bad++;
      free(entry);
    }
    if (!end) break;
    cursor = end + 1;
  }
  *ok = bad == 0;
  ZBuf message;
  zbuf_init(&message);
  if (bad == 0) zbuf_appendf(&message, "PATH has %zu usable entries", entries);
  else zbuf_appendf(&message, "PATH has %zu bad or empty entries out of %zu", bad, entries);
  return message.data;
}

static void doctor_target_toolchain_status(const ZTargetInfo *target, const ZToolchainPlan *plan, bool cc_ok, bool target_cc_ok, const char **status, ZBuf *message) {
  if (plan->requires_sysroot && strcmp(plan->sysroot_status, "host-leakage") == 0) {
    *status = "error";
    zbuf_appendf(message, "target sysroot for %s points at host SDK paths; set %s to a target SDK/sysroot", target->name, plan->sysroot_env);
    return;
  }

  if (strcmp(plan->driver_kind, "host-cc") == 0 && !cc_ok) {
    *status = "error";
    zbuf_append(message, "host cc is missing; install a native C compiler or pass --cc/ZERO_CC");
    return;
  }

  if (strcmp(plan->driver_kind, "zig-cc") == 0 && !target_cc_ok) {
    *status = "warning";
    zbuf_appendf(message, "target-capable C compiler is required for %s; pass --cc/ZERO_CC or install the bundled-toolchain default", target->name);
    return;
  }

  if (plan->requires_sysroot && strcmp(plan->sysroot_status, "missing") == 0) {
    *status = "warning";
    zbuf_appendf(message, "target %s requires sysroot mode; set %s to the target SDK/sysroot", target->name, plan->sysroot_env);
    return;
  }

  *status = "ok";
  zbuf_appendf(message, "%s ready for %s", public_compiler_label(plan), target->name);
}

static void append_doctor_target_toolchains_json(ZBuf *buf, size_t *ok_count, size_t *warning_count, size_t *error_count) {
  bool cc_ok = command_available("cc");
  bool target_cc_ok = command_available("zig");
  zbuf_append(buf, ",\n  \"targetToolchains\": [\n");
  for (size_t i = 0; i < z_target_count(); i++) {
    const ZTargetInfo *target = z_target_at(i);
    ZToolchainPlan plan = z_plan_toolchain(NULL, "release", target);
    const char *status = "ok";
    ZBuf message;
    zbuf_init(&message);
    doctor_target_toolchain_status(target, &plan, cc_ok, target_cc_ok, &status, &message);
    if (strcmp(status, "error") == 0) (*error_count)++;
    else if (strcmp(status, "warning") == 0) (*warning_count)++;
    else (*ok_count)++;

    zbuf_append(buf, "    {\"target\": ");
    append_json_string(buf, target->name);
    zbuf_append(buf, ", \"status\": ");
    append_json_string(buf, status);
    zbuf_append(buf, ", \"message\": ");
    append_json_string(buf, message.data ? message.data : "");
    zbuf_append(buf, ", \"driverKind\": ");
    append_json_string(buf, public_driver_kind(plan.driver_kind));
    zbuf_append(buf, ", \"compiler\": ");
    append_json_string(buf, public_compiler_label(&plan));
    zbuf_append(buf, ", \"selectionSource\": ");
    append_json_string(buf, plan.selection_source);
    zbuf_append(buf, ", \"targetTriple\": ");
    append_json_string(buf, plan.target_triple);
    zbuf_append(buf, ", \"linkerFlavor\": ");
    append_json_string(buf, public_linker_label(plan.linker_flavor));
    zbuf_append(buf, ", \"libcMode\": ");
    append_json_string(buf, plan.libc_mode);
    zbuf_appendf(buf, ", \"requiresSysroot\": %s", plan.requires_sysroot ? "true" : "false");
    zbuf_append(buf, ", \"sysrootEnv\": ");
    append_json_string(buf, plan.sysroot_env);
    zbuf_append(buf, ", \"sysrootStatus\": ");
    append_json_string(buf, plan.sysroot_status);
    zbuf_appendf(buf, "}%s\n", i + 1 < z_target_count() ? "," : "");
    zbuf_free(&message);
  }
  zbuf_append(buf, "  ]");
}

static void doctor_target_toolchain_counts(size_t *ok_count, size_t *warning_count, size_t *error_count) {
  ZBuf unused;
  zbuf_init(&unused);
  append_doctor_target_toolchains_json(&unused, ok_count, warning_count, error_count);
  zbuf_free(&unused);
}

static int doctor_command(bool json) {
  const char *overall = "ok";
  const char *host_status = strcmp(z_host_target(), "unknown") == 0 ? "warning" : "ok";
  const ZTargetInfo *host_target = z_find_target(z_host_target());
  ZLlvmToolchainPlan llvm_plan = z_llvm_toolchain_plan(host_target);
  const char *llvm_status = llvm_plan.native_executable ? "ok" : "warning";
  bool cc_ok = command_available("cc") || command_available("clang") || command_available("gcc");
  const char *zero_cc = getenv("ZERO_CC");
  bool target_cc_override = zero_cc && zero_cc[0];
  bool bundled_target_cc_ok = command_available("zig");
  bool target_cc_ok = target_cc_override || bundled_target_cc_ok;
  bool zero_dir_ok = zero_mkdir(".zero") == 0 || errno == EEXIST;
  bool write_ok = false;
  if (zero_dir_ok) {
    ZDiag diag = {0};
    write_ok = z_write_file(".zero/doctor.tmp", "ok\n", &diag);
    unlink(".zero/doctor.tmp");
  }
  bool docs_ok = path_exists("examples/hello.0") && path_exists("docs/articles/getting-started.md");
  bool path_ok = false;
  char *path_message = doctor_path_message(&path_ok);
  bool target_ok = z_find_target(z_host_target()) != NULL;
  size_t target_toolchains_ok = 0;
  size_t target_toolchains_warning = 0;
  size_t target_toolchains_error = 0;
  doctor_target_toolchain_counts(&target_toolchains_ok, &target_toolchains_warning, &target_toolchains_error);
  const char *target_toolchains_status = target_toolchains_error > 0 ? "error" : (target_toolchains_warning > 0 ? "warning" : "ok");
  const char *sdk_status = target_cc_ok ? "ok" : "warning";
  const char *sdk_message = target_cc_ok ? "target-capable C compiler available for non-host linker/sysroot support" : "configure a target-capable C compiler/sysroot before building non-host executables";
  overall = worse_status(overall, host_status);
  overall = worse_status(overall, cc_ok ? "ok" : "error");
  overall = worse_status(overall, target_cc_ok ? "ok" : "warning");
  overall = worse_status(overall, path_ok ? "ok" : "warning");
  overall = worse_status(overall, target_ok ? "ok" : "warning");
  overall = worse_status(overall, sdk_status);
  overall = worse_status(overall, target_toolchains_status);
  overall = worse_status(overall, llvm_status);
  overall = worse_status(overall, (zero_dir_ok && write_ok) ? "ok" : "error");
  overall = worse_status(overall, docs_ok ? "ok" : "warning");

  const char *const cc_version_argv[] = {"cc", "--version", NULL};
  const char *const zig_version_argv[] = {"zig", "version", NULL};
  char *cc_message = cc_ok ? z_process_first_stdout_line(cc_version_argv, true) : z_strdup("no native C compiler found on PATH");
  char *target_cc_message = target_cc_override ? z_strdup("configured via ZERO_CC") : (bundled_target_cc_ok ? z_process_first_stdout_line(zig_version_argv, true) : z_strdup("target-capable C compiler not found; cross-target executable builds may be unavailable"));

  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"status\": ");
    append_json_string(&buf, overall);
    zbuf_append(&buf, ",\n  \"host\": ");
    append_json_string(&buf, z_host_target());
    zbuf_append(&buf, ",\n  \"checks\": [\n");
    bool first = true;
    append_doctor_check_json(&buf, &first, "host", host_status, z_host_target());
    append_doctor_check_json(&buf, &first, "native-c-compiler", cc_ok ? "ok" : "error", cc_message);
    append_doctor_check_json(&buf, &first, "target-c-compiler", target_cc_ok ? "ok" : "warning", target_cc_message);
    append_doctor_check_json(&buf, &first, "cross-executable-builds", target_cc_ok ? "ok" : "warning", target_cc_ok ? "target-capable C compiler available for non-host executable builds" : "pass --cc/ZERO_CC or install a target-capable C compiler for non-host executable builds");
    append_doctor_check_json(&buf, &first, "path", path_ok ? "ok" : "warning", path_message);
    append_doctor_check_json(&buf, &first, "host-target", target_ok ? "ok" : "warning", target_ok ? "host target is supported" : "host target is not in the bundled target list");
    append_doctor_check_json(&buf, &first, "target-sdk-sysroot", sdk_status, sdk_message);
    append_doctor_check_json(&buf, &first, "llvm-toolchain", llvm_status, llvm_plan.reason);
    append_doctor_check_json(&buf, &first, ".zero-write", (zero_dir_ok && write_ok) ? "ok" : "error", (zero_dir_ok && write_ok) ? ".zero is writable" : ".zero is not writable");
    append_doctor_check_json(&buf, &first, "targets", target_ok ? "ok" : "warning", "run `zero targets` for the supported target list");
    append_doctor_check_json(&buf, &first, "docs-examples", docs_ok ? "ok" : "warning", docs_ok ? "docs and examples found" : "docs or examples are missing from this checkout");
    zbuf_append(&buf, "\n  ]");
    target_toolchains_ok = 0;
    target_toolchains_warning = 0;
    target_toolchains_error = 0;
    append_doctor_target_toolchains_json(&buf, &target_toolchains_ok, &target_toolchains_warning, &target_toolchains_error);
    z_append_doctor_llvm_toolchain_json(&buf, host_target, &llvm_plan);
    zbuf_append(&buf, "\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    ZBuf target_toolchain_message;
    zbuf_init(&target_toolchain_message);
    zbuf_appendf(&target_toolchain_message, "%zu ready, %zu warnings, %zu errors; run `zero doctor --json` for per-target details", target_toolchains_ok, target_toolchains_warning, target_toolchains_error);
    printf("zero doctor: %s\n", overall);
    printf("host: %s (%s)\n", host_status, z_host_target());
    printf("native C compiler: %s (%s)\n", cc_ok ? "ok" : "error", cc_message);
    printf("target C compiler: %s (%s)\n", target_cc_ok ? "ok" : "warning", target_cc_message);
    printf("cross executable builds: %s (%s)\n", target_cc_ok ? "ok" : "warning", target_cc_ok ? "target-capable C compiler available" : "pass --cc/ZERO_CC or install a target-capable C compiler");
    printf("PATH: %s (%s)\n", path_ok ? "ok" : "warning", path_message);
    printf("host target: %s (%s)\n", target_ok ? "ok" : "warning", target_ok ? "supported" : "unsupported host/target combination");
    printf("target SDK/sysroot: %s (%s)\n", sdk_status, sdk_message);
    printf("LLVM toolchain: %s (%s)\n", llvm_status, llvm_plan.reason);
    printf("target toolchains: %s (%s)\n", target_toolchains_status, target_toolchain_message.data ? target_toolchain_message.data : "no target data");
    printf(".zero write: %s\n", (zero_dir_ok && write_ok) ? "ok" : "error");
    printf("targets: %s (run `zero targets`)\n", target_ok ? "ok" : "warning");
    printf("docs/examples: %s\n", docs_ok ? "ok" : "warning");
    zbuf_free(&target_toolchain_message);
  }

  free(cc_message);
  free(target_cc_message);
  free(path_message);
  return strcmp(overall, "error") == 0 ? 1 : 0;
}

static void append_function_effects_json(ZBuf *buf, const Function *fun) {
  CapabilitySummary caps = function_capabilities(fun);
  append_capability_json_array(buf, &caps);
}

static void append_function_error_json(ZBuf *buf, const Function *fun) {
  zbuf_append(buf, "\"errorSetKind\":");
  append_json_string(buf, !fun || !fun->raises ? "none" : (fun->has_error_set ? "explicit" : (fun->is_public ? "open" : "inferred")));
  zbuf_append(buf, ",\"errorNames\":[");
  if (fun && fun->has_error_set) {
    for (size_t i = 0; i < fun->errors.len; i++) {
      if (i > 0) zbuf_append(buf, ",");
      append_json_string(buf, fun->errors.items[i].name);
    }
  }
  zbuf_append(buf, "]");
}

static bool text_contains(const char *text, const char *needle) {
  return text && needle && strstr(text, needle) != NULL;
}

static const char *type_ownership_kind(const char *type) {
  if (!type) return "unknown";
  if (text_contains(type, "owned<")) return "owned";
  if (text_contains(type, "mutref<") || text_contains(type, "MutSpan<")) return "mutable-borrow";
  if (text_contains(type, "ref<") || text_contains(type, "Span<") || strcmp(type, "String") == 0) return "borrow";
  return "value";
}

static const char *function_allocation_behavior(const Function *fun) {
  CapabilitySummary caps = function_capabilities(fun);
  if (caps.alloc) return "uses explicit allocator";
  return "no heap allocation";
}

static bool capability_available_on_target(const ZTargetInfo *target, const char *capability) {
  if (!capability) return true;
  if (strcmp(capability, "alloc") == 0 || strcmp(capability, "path") == 0 || strcmp(capability, "codec") == 0 ||
      strcmp(capability, "parse") == 0) {
    return true;
  }
  if (strcmp(capability, "world") == 0) return z_target_has_capability(target, "stdio");
  return z_target_has_capability(target, capability);
}

static bool append_missing_capabilities_json(ZBuf *buf, const CapabilitySummary *caps, const ZTargetInfo *target) {
  bool wrote = false;
  zbuf_append(buf, "[");
#define APPEND_MISSING_CAP(name, enabled) do { \
    if ((enabled) && !capability_available_on_target(target, name)) { \
      if (wrote) zbuf_append(buf, ","); \
      append_json_string(buf, name); \
      wrote = true; \
    } \
  } while (0)
  APPEND_MISSING_CAP("args", caps && caps->args);
  APPEND_MISSING_CAP("env", caps && caps->env);
  APPEND_MISSING_CAP("fs", caps && caps->fs);
  APPEND_MISSING_CAP("memory", caps && caps->memory);
  APPEND_MISSING_CAP("alloc", caps && caps->alloc);
  APPEND_MISSING_CAP("path", caps && caps->path);
  APPEND_MISSING_CAP("codec", caps && caps->codec);
  APPEND_MISSING_CAP("parse", caps && caps->parse);
  APPEND_MISSING_CAP("time", caps && caps->time);
  APPEND_MISSING_CAP("rand", caps && caps->rand);
  APPEND_MISSING_CAP("net", caps && caps->net);
  APPEND_MISSING_CAP("proc", caps && caps->proc);
  APPEND_MISSING_CAP("web", caps && caps->web);
  APPEND_MISSING_CAP("world", caps && caps->world);
#undef APPEND_MISSING_CAP
  zbuf_append(buf, "]");
  return wrote;
}

static void append_target_capability_facts_json(ZBuf *buf, const ZTargetInfo *target, const CapabilitySummary *caps) {
  zbuf_append(buf, "{\"status\":");
  ZBuf missing;
  zbuf_init(&missing);
  bool has_missing = append_missing_capabilities_json(&missing, caps, target);
  append_json_string(buf, has_missing ? "unsupported" : "supported");
  zbuf_append(buf, ",\"missingCapabilities\":");
  zbuf_append(buf, missing.data ? missing.data : "[]");
  zbuf_append(buf, "}");
  zbuf_free(&missing);
}

static void append_function_ownership_json(ZBuf *buf, const Function *fun) {
  zbuf_append(buf, "{\"params\":[");
  bool moves = false;
  for (size_t i = 0; fun && i < fun->params.len; i++) {
    Param *param = &fun->params.items[i];
    if (i > 0) zbuf_append(buf, ",");
    const char *ownership = type_ownership_kind(param->type);
    if (strcmp(ownership, "owned") == 0) moves = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, param->name);
    zbuf_append(buf, ",\"type\":");
    append_json_string(buf, param->type);
    zbuf_append(buf, ",\"ownership\":");
    append_json_string(buf, ownership);
    zbuf_append(buf, "}");
  }
  const char *return_ownership = type_ownership_kind(fun ? fun->return_type : NULL);
  if (strcmp(return_ownership, "owned") == 0) moves = true;
  zbuf_append(buf, "],\"returnOwnership\":");
  append_json_string(buf, return_ownership);
  zbuf_appendf(buf, ",\"movesOwnership\":%s}", moves ? "true" : "false");
}

static const Function *find_program_function(const Program *program, const char *name) {
  if (!program || !name) return NULL;
  for (size_t i = 0; i < program->functions.len; i++) {
    if (strcmp(program->functions.items[i].name, name) == 0) return &program->functions.items[i];
  }
  return NULL;
}

typedef struct {
  bool present;
  bool auto_increment_port;
  uint16_t port;
  const char *handler;
  int line;
  int column;
} HttpListenSpec;

static bool parse_u16_literal_text(const char *text, uint16_t *out);

static bool parse_u16_number_literal(const Expr *expr, uint16_t *out) {
  if (out) *out = 0;
  if (!expr || expr->kind != EXPR_NUMBER || !expr->text || !expr->text[0]) return false;
  return parse_u16_literal_text(expr->text, out);
}

static bool parse_u16_literal_text(const char *text, uint16_t *out) {
  if (out) *out = 0;
  if (!text || !text[0]) return false;
  unsigned long value = 0;
  size_t digits = 0;
  for (const char *p = text; *p; p++) {
    if (*p == '_') break;
    if (*p < '0' || *p > '9') return false;
    value = value * 10ul + (unsigned long)(*p - '0');
    if (value > 65535ul) return false;
    digits++;
  }
  if (digits == 0) return false;
  if (out) *out = (uint16_t)value;
  return true;
}

static bool find_http_listen_expr(const Expr *expr, HttpListenSpec *spec, ZDiag *diag) {
  if (!expr || !spec) return false;
  if (expr->kind == EXPR_CALL && expr->left) {
    char *callee = expr_callee_name(expr->left);
    bool is_listen = strcmp(callee, "std.http.listen") == 0;
    free(callee);
    if (is_listen) {
      spec->present = true;
      spec->handler = "handle";
      spec->line = expr->line > 0 ? expr->line : 1;
      spec->column = expr->column > 0 ? expr->column : 1;
      if (expr->args.len == 1) {
        spec->auto_increment_port = true;
        spec->port = 3000;
      } else if (expr->args.len != 2 || !parse_u16_number_literal(expr->args.items[1], &spec->port)) {
        if (diag) {
          diag->code = 2002;
          diag->line = spec->line;
          diag->column = spec->column;
          snprintf(diag->message, sizeof(diag->message), "std.http.listen expects World plus an optional literal u16 port");
          snprintf(diag->expected, sizeof(diag->expected), "std.http.listen(world) or std.http.listen(world, 3000_u16)");
          snprintf(diag->actual, sizeof(diag->actual), "non-literal or invalid port");
          snprintf(diag->help, sizeof(diag->help), "use a literal port and define handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>>");
        }
      }
      return true;
    }
  }
  if (find_http_listen_expr(expr->left, spec, diag) || find_http_listen_expr(expr->right, spec, diag)) return true;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (find_http_listen_expr(expr->args.items[i], spec, diag)) return true;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (find_http_listen_expr(expr->fields.items[i].value, spec, diag)) return true;
  }
  return false;
}

static bool find_http_listen_stmt_vec(const StmtVec *body, HttpListenSpec *spec, ZDiag *diag) {
  if (!body || !spec) return false;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    if (!stmt) continue;
    if (find_http_listen_expr(stmt->expr, spec, diag) ||
        find_http_listen_expr(stmt->target, spec, diag) ||
        find_http_listen_expr(stmt->range_end, spec, diag)) {
      return true;
    }
    if (find_http_listen_stmt_vec(&stmt->then_body, spec, diag) ||
        find_http_listen_stmt_vec(&stmt->else_body, spec, diag)) {
      return true;
    }
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      if (find_http_listen_expr(stmt->match_arms.items[arm_index].guard, spec, diag) ||
          find_http_listen_stmt_vec(&stmt->match_arms.items[arm_index].body, spec, diag)) {
        return true;
      }
    }
  }
  return false;
}

static bool find_http_listen_program(const Program *program, HttpListenSpec *spec, ZDiag *diag) {
  if (spec) *spec = (HttpListenSpec){0};
  const Function *main_fun = find_program_function(program, "main");
  return main_fun && find_http_listen_stmt_vec(&main_fun->body, spec, diag);
}

static bool validate_http_listen_handler(const Program *program, const HttpListenSpec *spec, ZDiag *diag) {
  const char *handler = spec && spec->handler ? spec->handler : "handle";
  const Function *fun = find_program_function(program, handler);
  if (fun && fun->params.len == 2 &&
      fun->params.items[0].type && strcmp(fun->params.items[0].type, "Span<u8>") == 0 &&
      fun->params.items[1].type && strcmp(fun->params.items[1].type, "MutSpan<u8>") == 0 &&
      fun->return_type && strcmp(fun->return_type, "Maybe<Span<u8>>") == 0) {
    return true;
  }
  if (diag) {
    diag->code = 2002;
    diag->line = spec && spec->line > 0 ? spec->line : 1;
    diag->column = spec && spec->column > 0 ? spec->column : 1;
    snprintf(diag->message, sizeof(diag->message), "std.http.listen requires a same-module HTTP handler");
    snprintf(diag->expected, sizeof(diag->expected), "fn handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>>");
    snprintf(diag->actual, sizeof(diag->actual), "%s", fun ? "handler signature mismatch" : "missing handle function");
    snprintf(diag->help, sizeof(diag->help), "define handle with the expected request/response envelope signature");
  }
  return false;
}

static const ZProgramGraphNode *graph_find_node_id(const ZProgramGraph *graph, const char *id) {
  if (!graph || !id) return NULL;
  for (size_t i = 0; i < graph->node_len; i++) {
    if (graph->nodes[i].id && strcmp(graph->nodes[i].id, id) == 0) return &graph->nodes[i];
  }
  return NULL;
}

static const ZProgramGraphNode *graph_child_node(const ZProgramGraph *graph, const ZProgramGraphNode *parent, const char *edge, size_t order) {
  if (!graph || !parent || !parent->id || !edge) return NULL;
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *candidate = &graph->edges[i];
    if (candidate->target == Z_PROGRAM_GRAPH_EDGE_TARGET_NODE &&
        candidate->from && strcmp(candidate->from, parent->id) == 0 &&
        candidate->kind && strcmp(candidate->kind, edge) == 0 &&
        candidate->order == order) {
      return graph_find_node_id(graph, candidate->to);
    }
  }
  return NULL;
}

static const ZProgramGraphNode *graph_find_function_node(const ZProgramGraph *graph, const char *name) {
  if (!graph || !name) return NULL;
  for (size_t i = 0; i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && node->name && strcmp(node->name, name) == 0) return node;
  }
  return NULL;
}

static void graph_append_expr_qualified_name(const ZProgramGraph *graph, const ZProgramGraphNode *node, ZBuf *buf) {
  if (!node || !buf) return;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_IDENTIFIER) {
    zbuf_append(buf, node->name ? node->name : "");
    return;
  }
  if (node->kind == Z_PROGRAM_GRAPH_NODE_FIELD_ACCESS) {
    graph_append_expr_qualified_name(graph, graph_child_node(graph, node, "left", 0), buf);
    if (buf->len > 0) zbuf_append_char(buf, '.');
    zbuf_append(buf, node->name ? node->name : "");
  }
}

static char *graph_expr_qualified_name(const ZProgramGraph *graph, const ZProgramGraphNode *node) {
  ZBuf buf;
  zbuf_init(&buf);
  graph_append_expr_qualified_name(graph, node, &buf);
  return buf.data ? buf.data : z_strdup("");
}

static bool find_http_listen_graph_node(const ZProgramGraph *graph, const ZProgramGraphNode *node, HttpListenSpec *spec, ZDiag *diag) {
  if (!graph || !node || !spec) return false;
  if (node->kind == Z_PROGRAM_GRAPH_NODE_CALL || node->kind == Z_PROGRAM_GRAPH_NODE_METHOD_CALL) {
    char *callee = graph_expr_qualified_name(graph, graph_child_node(graph, node, "left", 0));
    bool is_listen = callee && strcmp(callee, "std.http.listen") == 0;
    free(callee);
    if (is_listen) {
      const ZProgramGraphNode *world = graph_child_node(graph, node, "arg", 0);
      const ZProgramGraphNode *port = graph_child_node(graph, node, "arg", 1);
      const ZProgramGraphNode *extra = graph_child_node(graph, node, "arg", 2);
      spec->present = true;
      spec->handler = "handle";
      spec->line = node->line > 0 ? node->line : 1;
      spec->column = node->column > 0 ? node->column : 1;
      if (world && !port && !extra) {
        spec->auto_increment_port = true;
        spec->port = 3000;
      } else if (!world || extra || !port || port->kind != Z_PROGRAM_GRAPH_NODE_LITERAL || !parse_u16_literal_text(port->value, &spec->port)) {
        if (diag) {
          diag->code = 2002;
          diag->line = spec->line;
          diag->column = spec->column;
          snprintf(diag->message, sizeof(diag->message), "std.http.listen expects World plus an optional literal u16 port");
          snprintf(diag->expected, sizeof(diag->expected), "std.http.listen(world) or std.http.listen(world, 3000_u16)");
          snprintf(diag->actual, sizeof(diag->actual), "missing world, extra argument, or invalid port");
          snprintf(diag->help, sizeof(diag->help), "use a literal port and define handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>>");
        }
      }
      return true;
    }
  }
  for (size_t i = 0; i < graph->edge_len; i++) {
    const ZProgramGraphEdge *edge = &graph->edges[i];
    if (edge->target != Z_PROGRAM_GRAPH_EDGE_TARGET_NODE || !edge->from || !node->id || strcmp(edge->from, node->id) != 0) continue;
    const ZProgramGraphNode *child = graph_find_node_id(graph, edge->to);
    if (find_http_listen_graph_node(graph, child, spec, diag)) return true;
  }
  return false;
}

static bool graph_may_have_http_listen(const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (c_import_header_text_equal(node->name, "listen") ||
        c_import_header_text_equal(node->value, "listen")) {
      return true;
    }
  }
  return false;
}

static bool find_http_listen_graph_program(const ZProgramGraph *graph, HttpListenSpec *spec, ZDiag *diag) {
  if (spec) *spec = (HttpListenSpec){0};
  if (!graph_may_have_http_listen(graph)) return false;
  const ZProgramGraphNode *main_fun = graph_find_function_node(graph, "main");
  const ZProgramGraphNode *body = graph_child_node(graph, main_fun, "body", 0);
  return find_http_listen_graph_node(graph, body, spec, diag);
}

static bool validate_http_listen_handler_graph(const ZProgramGraph *graph, const HttpListenSpec *spec, ZDiag *diag) {
  const char *handler = spec && spec->handler ? spec->handler : "handle";
  const ZProgramGraphNode *fun = graph_find_function_node(graph, handler);
  const ZProgramGraphNode *request = graph_child_node(graph, fun, "param", 0);
  const ZProgramGraphNode *response = graph_child_node(graph, fun, "param", 1);
  if (fun && request && response &&
      request->type && strcmp(request->type, "Span<u8>") == 0 &&
      response->type && strcmp(response->type, "MutSpan<u8>") == 0 &&
      fun->type && strcmp(fun->type, "Maybe<Span<u8>>") == 0) {
    return true;
  }
  if (diag) {
    diag->code = 2002;
    diag->line = spec && spec->line > 0 ? spec->line : 1;
    diag->column = spec && spec->column > 0 ? spec->column : 1;
    snprintf(diag->message, sizeof(diag->message), "std.http.listen requires a same-module HTTP handler");
    snprintf(diag->expected, sizeof(diag->expected), "fn handle(request: Span<u8>, response: MutSpan<u8>) -> Maybe<Span<u8>>");
    snprintf(diag->actual, sizeof(diag->actual), "%s", fun ? "handler signature mismatch" : "missing handle function");
    snprintf(diag->help, sizeof(diag->help), "define handle with the expected request/response envelope signature");
  }
  return false;
}

static int run_http_listen_or_diag(const Command *command, const ZTargetInfo *target, const HttpListenSpec *listen, const char *zero_exe, ZDiag *diag) {
  (void)target;
  if (!command || !listen) return 1;
  if (strcmp(command->command, "build") == 0) {
    diag->code = 2002;
    diag->line = listen->line > 0 ? listen->line : 1;
    diag->column = listen->column > 0 ? listen->column : 1;
    snprintf(diag->message, sizeof(diag->message), "std.http.listen standalone artifacts are not implemented yet");
    snprintf(diag->expected, sizeof(diag->expected), "zero run starts the host listener");
    snprintf(diag->actual, sizeof(diag->actual), "zero %s requested a listen artifact", command->command);
    snprintf(diag->help, sizeof(diag->help), "run the graph package with zero run, or keep build targets to request/response handler functions for now");
    return 1;
  }
  ZHttpListenRunConfig listen_config = {
    .zero_exe = zero_exe,
    .input = command->repository_graph_source_input ? command->repository_graph_source_input : command->input,
    .target = command->target,
    .profile = command->profile,
    .backend = command->backend,
    .cc = command->cc,
    .handler = listen->handler,
    .port = listen->port,
    .auto_increment_port = listen->auto_increment_port,
  };
  return z_http_listen_run(&listen_config, diag);
}

typedef struct TestValue TestValue;

typedef struct {
  char *name;
  TestValue *value;
} TestFieldValue;

struct TestValue {
  enum { TEST_VALUE_VOID, TEST_VALUE_BOOL, TEST_VALUE_INT, TEST_VALUE_STRING, TEST_VALUE_SHAPE } kind;
  bool bool_value;
  long long int_value;
  char *string_value;
  TestFieldValue *fields;
  size_t field_len;
};

typedef struct {
  char *name;
  TestValue value;
} TestBinding;

typedef struct {
  TestBinding *items;
  size_t len;
  size_t cap;
} TestEnv;

typedef struct {
  const char *current_test;
  int line;
  int column;
  char message[256];
} TestRunFailure;

static void test_value_free(TestValue *value) {
  if (!value) return;
  free(value->string_value);
  for (size_t i = 0; i < value->field_len; i++) {
    free(value->fields[i].name);
    test_value_free(value->fields[i].value);
    free(value->fields[i].value);
  }
  free(value->fields);
  memset(value, 0, sizeof(*value));
}

static TestValue test_value_copy(const TestValue *value) {
  TestValue copy = {0};
  if (!value) return copy;
  copy.kind = value->kind;
  copy.bool_value = value->bool_value;
  copy.int_value = value->int_value;
  copy.string_value = value->string_value ? z_strdup(value->string_value) : NULL;
  copy.field_len = value->field_len;
  if (copy.field_len > 0) {
    copy.fields = z_checked_calloc(copy.field_len, sizeof(TestFieldValue));
    for (size_t i = 0; i < copy.field_len; i++) {
      copy.fields[i].name = z_strdup(value->fields[i].name);
      copy.fields[i].value = z_checked_calloc(1, sizeof(TestValue));
      *copy.fields[i].value = test_value_copy(value->fields[i].value);
    }
  }
  return copy;
}

static void test_env_free(TestEnv *env) {
  for (size_t i = 0; env && i < env->len; i++) {
    free(env->items[i].name);
    test_value_free(&env->items[i].value);
  }
  free(env ? env->items : NULL);
  if (env) memset(env, 0, sizeof(*env));
}

static bool test_env_set(TestEnv *env, const char *name, const TestValue *value) {
  if (!env || !name || !value) return false;
  for (size_t i = 0; i < env->len; i++) {
    if (strcmp(env->items[i].name, name) == 0) {
      test_value_free(&env->items[i].value);
      env->items[i].value = test_value_copy(value);
      return true;
    }
  }
  if (env->len + 1 > env->cap) {
    env->cap = z_grow_capacity(env->cap, env->len + 1, 8);
    env->items = z_checked_reallocarray(env->items, env->cap, sizeof(TestBinding));
  }
  env->items[env->len].name = z_strdup(name);
  env->items[env->len].value = test_value_copy(value);
  env->len++;
  return true;
}

static bool test_env_get(const TestEnv *env, const char *name, TestValue *out) {
  for (size_t i = 0; env && i < env->len; i++) {
    if (strcmp(env->items[i].name, name) == 0) {
      *out = test_value_copy(&env->items[i].value);
      return true;
    }
  }
  return false;
}

static void test_fail(TestRunFailure *failure, const Expr *expr, const char *message) {
  if (!failure || failure->message[0]) return;
  if (expr) {
    failure->line = expr->line;
    failure->column = expr->column;
  }
  snprintf(failure->message, sizeof(failure->message), "%s%s%s",
           message ? message : "zero test expectation failed",
           failure->current_test ? ": " : "",
           failure->current_test ? failure->current_test : "");
  (void)expr;
}

static bool test_expected_failure(const Function *fun) {
  const char *name = fun && fun->test_name ? fun->test_name : "";
  return strncmp(name, "xfail:", strlen("xfail:")) == 0 ||
         strncmp(name, "expected fail:", strlen("expected fail:")) == 0 ||
         strstr(name, "[xfail]") != NULL;
}

static bool test_expr_name(const Expr *expr, ZBuf *out) {
  if (!expr) return false;
  if (expr->kind == EXPR_IDENT) {
    zbuf_append(out, expr->text ? expr->text : "");
    return true;
  }
  if (expr->kind == EXPR_MEMBER) {
    if (!test_expr_name(expr->left, out)) return false;
    zbuf_append_char(out, '.');
    zbuf_append(out, expr->text ? expr->text : "");
    return true;
  }
  return false;
}

static bool test_value_truthy(const TestValue *value) {
  if (!value) return false;
  if (value->kind == TEST_VALUE_BOOL) return value->bool_value;
  if (value->kind == TEST_VALUE_INT) return value->int_value != 0;
  return false;
}

static bool test_string_equal(const char *left, const char *right) {
  const char *a = left ? left : "";
  const char *b = right ? right : "";
  size_t a_len = strlen(a);
  size_t b_len = strlen(b);
  return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static bool test_value_equals(const TestValue *left, const TestValue *right) {
  if (!left || !right) return false;
  if (left->kind == TEST_VALUE_STRING && right->kind == TEST_VALUE_STRING) {
    return test_string_equal(left->string_value, right->string_value);
  }
  if (left->kind == TEST_VALUE_BOOL && right->kind == TEST_VALUE_BOOL) return left->bool_value == right->bool_value;
  if ((left->kind == TEST_VALUE_INT || left->kind == TEST_VALUE_BOOL) &&
      (right->kind == TEST_VALUE_INT || right->kind == TEST_VALUE_BOOL)) {
    long long a = left->kind == TEST_VALUE_BOOL ? (left->bool_value ? 1 : 0) : left->int_value;
    long long b = right->kind == TEST_VALUE_BOOL ? (right->bool_value ? 1 : 0) : right->int_value;
    return a == b;
  }
  return false;
}

static bool test_string_starts_with(const char *text, const char *prefix) {
  if (!text || !prefix) return false;
  size_t text_len = strlen(text);
  size_t prefix_len = strlen(prefix);
  return prefix_len <= text_len && strncmp(text, prefix, prefix_len) == 0;
}

static bool test_string_ends_with(const char *text, const char *suffix) {
  if (!text || !suffix) return false;
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);
  return suffix_len <= text_len && memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

typedef enum {
  TEST_STD_CALL_EQUALS,
  TEST_STD_CALL_TRUTHY,
  TEST_STD_CALL_FALSEY,
  TEST_STD_CALL_CONTAINS,
  TEST_STD_CALL_STARTS_WITH,
  TEST_STD_CALL_ENDS_WITH,
} TestStdCallKind;

typedef struct {
  const char *name;
  size_t arg_count;
  TestStdCallKind kind;
} TestStdCallSpec;

static const TestStdCallSpec test_std_calls[] = {
  {"std.mem.eql", 2, TEST_STD_CALL_EQUALS},
  {"std.testing.isTrue", 1, TEST_STD_CALL_TRUTHY},
  {"std.testing.isFalse", 1, TEST_STD_CALL_FALSEY},
  {"std.testing.equalBool", 2, TEST_STD_CALL_EQUALS},
  {"std.testing.equalUsize", 2, TEST_STD_CALL_EQUALS},
  {"std.testing.equalU32", 2, TEST_STD_CALL_EQUALS},
  {"std.testing.equalI32", 2, TEST_STD_CALL_EQUALS},
  {"std.testing.equalBytes", 2, TEST_STD_CALL_EQUALS},
  {"std.testing.containsBytes", 2, TEST_STD_CALL_CONTAINS},
  {"std.testing.startsWith", 2, TEST_STD_CALL_STARTS_WITH},
  {"std.testing.endsWith", 2, TEST_STD_CALL_ENDS_WITH},
};

static const TestStdCallSpec *test_std_call_find(const char *name) {
  for (size_t i = 0; i < sizeof(test_std_calls) / sizeof(test_std_calls[0]); i++) {
    if (test_string_equal(test_std_calls[i].name, name)) return &test_std_calls[i];
  }
  return NULL;
}

static bool test_std_call_eval(const TestStdCallSpec *spec, const TestValue *args, TestValue *out) {
  if (!spec || !args || !out) return false;
  out->kind = TEST_VALUE_BOOL;
  switch (spec->kind) {
    case TEST_STD_CALL_EQUALS:
      out->bool_value = test_value_equals(&args[0], &args[1]);
      return true;
    case TEST_STD_CALL_TRUTHY:
      out->bool_value = test_value_truthy(&args[0]);
      return true;
    case TEST_STD_CALL_FALSEY:
      out->bool_value = !test_value_truthy(&args[0]);
      return true;
    case TEST_STD_CALL_CONTAINS:
      out->bool_value = args[0].kind == TEST_VALUE_STRING && args[1].kind == TEST_VALUE_STRING &&
                        strstr(args[0].string_value ? args[0].string_value : "", args[1].string_value ? args[1].string_value : "") != NULL;
      return true;
    case TEST_STD_CALL_STARTS_WITH:
      out->bool_value = args[0].kind == TEST_VALUE_STRING && args[1].kind == TEST_VALUE_STRING &&
                        test_string_starts_with(args[0].string_value, args[1].string_value);
      return true;
    case TEST_STD_CALL_ENDS_WITH:
      out->bool_value = args[0].kind == TEST_VALUE_STRING && args[1].kind == TEST_VALUE_STRING &&
                        test_string_ends_with(args[0].string_value, args[1].string_value);
      return true;
  }
  return false;
}

static bool test_eval_expr(const Program *program, TestEnv *env, const Expr *expr, TestValue *out, TestRunFailure *failure);

static bool test_eval_stmt_vec(const Program *program, TestEnv *env, const StmtVec *body, TestValue *return_value, bool *returned, TestRunFailure *failure) {
  for (size_t i = 0; body && i < body->len; i++) {
    Stmt *stmt = body->items[i];
    if (!stmt) continue;
    if (stmt->kind == STMT_LET) {
      TestValue value = {0};
      if (!test_eval_expr(program, env, stmt->expr, &value, failure)) return false;
      test_env_set(env, stmt->name, &value);
      test_value_free(&value);
    } else if (stmt->kind == STMT_RETURN) {
      if (stmt->expr) {
        if (!test_eval_expr(program, env, stmt->expr, return_value, failure)) return false;
      } else {
        return_value->kind = TEST_VALUE_VOID;
      }
      *returned = true;
      return true;
    } else if (stmt->kind == STMT_EXPR) {
      if (stmt->expr && stmt->expr->kind == EXPR_CALL) {
        ZBuf name;
        zbuf_init(&name);
        bool named = test_expr_name(stmt->expr->left, &name);
        if (named && strcmp(name.data ? name.data : "", "expect") == 0 && stmt->expr->args.len == 1) {
          TestValue condition = {0};
          bool ok = test_eval_expr(program, env, stmt->expr->args.items[0], &condition, failure);
          if (!ok) {
            zbuf_free(&name);
            return false;
          }
          bool passed = test_value_truthy(&condition);
          test_value_free(&condition);
          zbuf_free(&name);
          if (!passed) {
            test_fail(failure, stmt->expr, "zero test expectation failed");
            return false;
          }
          continue;
        }
        zbuf_free(&name);
      }
      TestValue ignored = {0};
      bool ok = test_eval_expr(program, env, stmt->expr, &ignored, failure);
      test_value_free(&ignored);
      if (!ok) return false;
    } else if (stmt->kind == STMT_IF) {
      TestValue condition = {0};
      if (!test_eval_expr(program, env, stmt->expr, &condition, failure)) return false;
      bool branch = test_value_truthy(&condition);
      test_value_free(&condition);
      if (!test_eval_stmt_vec(program, env, branch ? &stmt->then_body : &stmt->else_body, return_value, returned, failure)) return false;
      if (*returned) return true;
    } else {
      test_fail(failure, NULL, "zero test direct runner does not support this statement yet");
      return false;
    }
  }
  return true;
}

static bool test_eval_function(const Program *program, const Function *fun, const TestValue *args, size_t arg_len, TestValue *out, TestRunFailure *failure) {
  if (!fun) {
    test_fail(failure, NULL, "zero test unknown function");
    return false;
  }
  if (fun->params.len != arg_len) {
    test_fail(failure, NULL, "zero test function argument count mismatch");
    return false;
  }
  TestEnv local = {0};
  for (size_t i = 0; i < arg_len; i++) test_env_set(&local, fun->params.items[i].name, &args[i]);
  bool returned = false;
  bool ok = test_eval_stmt_vec(program, &local, &fun->body, out, &returned, failure);
  test_env_free(&local);
  if (!ok) return false;
  if (!returned) out->kind = TEST_VALUE_VOID;
  return true;
}

static bool test_eval_binary_expr(const Program *program, TestEnv *env, const Expr *expr, TestValue *out, TestRunFailure *failure) {
  TestValue left = {0}, right = {0};
  if (!test_eval_expr(program, env, expr->left, &left, failure) ||
      !test_eval_expr(program, env, expr->right, &right, failure)) {
    test_value_free(&left); test_value_free(&right);
    return false;
  }
  const char *op = expr->text ? expr->text : "";
  if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
    out->kind = TEST_VALUE_BOOL;
    out->bool_value = test_value_equals(&left, &right);
    if (strcmp(op, "!=") == 0) out->bool_value = !out->bool_value;
  } else if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
    out->kind = TEST_VALUE_BOOL;
    out->bool_value = strcmp(op, "&&") == 0 ? (test_value_truthy(&left) && test_value_truthy(&right)) : (test_value_truthy(&left) || test_value_truthy(&right));
  } else {
    long long a = left.kind == TEST_VALUE_BOOL ? (left.bool_value ? 1 : 0) : left.int_value;
    long long b = right.kind == TEST_VALUE_BOOL ? (right.bool_value ? 1 : 0) : right.int_value;
    out->kind = TEST_VALUE_INT;
    if (strcmp(op, "+") == 0) out->int_value = a + b;
    else if (strcmp(op, "-") == 0) out->int_value = a - b;
    else if (strcmp(op, "*") == 0) out->int_value = a * b;
    else if (strcmp(op, "/") == 0) out->int_value = b == 0 ? 0 : a / b;
    else if (strcmp(op, "%") == 0) out->int_value = b == 0 ? 0 : a % b;
    else if (strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
      out->kind = TEST_VALUE_BOOL;
      if (strcmp(op, "<") == 0) out->bool_value = a < b;
      else if (strcmp(op, "<=") == 0) out->bool_value = a <= b;
      else if (strcmp(op, ">") == 0) out->bool_value = a > b;
      else out->bool_value = a >= b;
    } else {
      test_fail(failure, expr, "zero test unsupported operator");
      test_value_free(&left);
      test_value_free(&right);
      return false;
    }
  }
  test_value_free(&left); test_value_free(&right);
  return true;
}

static bool test_eval_call_expr(const Program *program, TestEnv *env, const Expr *expr, TestValue *out, TestRunFailure *failure) {
  ZBuf name;
  zbuf_init(&name);
  if (!test_expr_name(expr->left, &name)) {
    zbuf_free(&name);
    test_fail(failure, expr, "zero test unsupported callee");
    return false;
  }
  const char *callee_name = name.data ? name.data : "";
  TestValue *args = z_checked_calloc(expr->args.len ? expr->args.len : 1, sizeof(TestValue));
  for (size_t i = 0; i < expr->args.len; i++) {
    if (!test_eval_expr(program, env, expr->args.items[i], &args[i], failure)) {
      for (size_t j = 0; j <= i; j++) test_value_free(&args[j]);
      free(args); zbuf_free(&name);
      return false;
    }
  }
  bool ok = true;
  const TestStdCallSpec *std_call = test_std_call_find(callee_name);
  if (std_call) {
    if (expr->args.len == std_call->arg_count) {
      ok = test_std_call_eval(std_call, args, out);
    } else {
      ok = false;
      test_fail(failure, expr, "zero test std helper argument count mismatch");
    }
  } else {
    const Function *fun = find_program_function(program, callee_name);
    ok = test_eval_function(program, fun, args, expr->args.len, out, failure);
  }
  for (size_t i = 0; i < expr->args.len; i++) test_value_free(&args[i]);
  free(args); zbuf_free(&name);
  return ok;
}

static bool test_eval_expr(const Program *program, TestEnv *env, const Expr *expr, TestValue *out, TestRunFailure *failure) {
  if (!expr || !out) return false;
  switch (expr->kind) {
    case EXPR_BOOL:
      out->kind = TEST_VALUE_BOOL;
      out->bool_value = expr->bool_value;
      return true;
    case EXPR_NUMBER:
      out->kind = TEST_VALUE_INT;
      out->int_value = strtoll(expr->text ? expr->text : "0", NULL, 0);
      return true;
    case EXPR_STRING:
      out->kind = TEST_VALUE_STRING;
      out->string_value = z_strdup(expr->text ? expr->text : "");
      return true;
    case EXPR_IDENT:
      if (test_env_get(env, expr->text, out)) return true;
      test_fail(failure, expr, "zero test unknown identifier");
      return false;
    case EXPR_SHAPE_LITERAL:
      out->kind = TEST_VALUE_SHAPE;
      out->field_len = expr->fields.len;
      out->fields = z_checked_calloc(out->field_len ? out->field_len : 1, sizeof(TestFieldValue));
      for (size_t i = 0; i < expr->fields.len; i++) {
        out->fields[i].name = z_strdup(expr->fields.items[i].name);
        out->fields[i].value = z_checked_calloc(1, sizeof(TestValue));
        if (!test_eval_expr(program, env, expr->fields.items[i].value, out->fields[i].value, failure)) return false;
      }
      return true;
    case EXPR_MEMBER: {
      TestValue base = {0};
      if (!test_eval_expr(program, env, expr->left, &base, failure)) return false;
      if (base.kind == TEST_VALUE_SHAPE) {
        for (size_t i = 0; i < base.field_len; i++) {
          if (strcmp(base.fields[i].name, expr->text ? expr->text : "") == 0) {
            *out = test_value_copy(base.fields[i].value);
            test_value_free(&base);
            return true;
          }
        }
      }
      test_value_free(&base);
      test_fail(failure, expr, "zero test unknown field");
      return false;
    }
    case EXPR_BINARY:
      return test_eval_binary_expr(program, env, expr, out, failure);
    case EXPR_CALL:
      return test_eval_call_expr(program, env, expr, out, failure);
    default:
      test_fail(failure, expr, "zero test direct runner does not support this expression yet");
      return false;
  }
}

static int run_tests_direct(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target) {
  long long started_ms = now_ms();
  size_t discovered = program_test_count(program, NULL);
  size_t selected = 0;
  size_t passed = 0;
  size_t failed = 0;
  size_t expected_failures = 0;
  size_t unexpected_passes = 0;
  TestRunFailure first_failure = {0};
  ZBuf results;
  zbuf_init(&results);
  zbuf_append(&results, "[");
  bool wrote_result = false;
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (!fun->is_test) continue;
    if (command && command->filter && command->filter[0] && (!fun->test_name || !strstr(fun->test_name, command->filter))) continue;
    selected++;
    TestRunFailure failure = {0};
    failure.current_test = fun->test_name ? fun->test_name : fun->name;
    failure.line = fun->line;
    failure.column = fun->column;
    bool expected_failure = test_expected_failure(fun);
    long long test_started_ms = now_ms();
    TestValue ignored = {0};
    bool ok = test_eval_function(program, fun, NULL, 0, &ignored, &failure);
    long long test_duration_ms = now_ms() - test_started_ms;
    test_value_free(&ignored);

    const char *status = "passed";
    const char *failure_message = NULL;
    if (expected_failure && ok) {
      status = "unexpected-pass";
      failure_message = "expected failure unexpectedly passed";
      failed++;
      unexpected_passes++;
      if (!first_failure.message[0]) {
        first_failure.current_test = failure.current_test;
        first_failure.line = fun->line;
        first_failure.column = fun->column;
        snprintf(first_failure.message, sizeof(first_failure.message), "zero test expected failure unexpectedly passed: %s", failure.current_test ? failure.current_test : fun->name);
      }
    } else if (expected_failure && !ok) {
      status = "expected-fail";
      expected_failures++;
      failure_message = failure.message[0] ? failure.message : "expected failure";
    } else if (ok) {
      passed++;
    } else {
      status = "failed";
      failed++;
      failure_message = failure.message[0] ? failure.message : "zero test failed";
      if (!first_failure.message[0]) first_failure = failure;
    }

    if (wrote_result) zbuf_append(&results, ", ");
    wrote_result = true;
    zbuf_append(&results, "{\"name\":");
    append_json_string(&results, failure.current_test ? failure.current_test : fun->name);
    zbuf_append(&results, ",\"status\":");
    append_json_string(&results, status);
    zbuf_appendf(&results, ",\"expectedFailure\":%s,\"durationMs\":%lld,\"location\":", expected_failure ? "true" : "false", test_duration_ms);
    append_test_json_location(&results, input, fun->line, fun->column);
    zbuf_append(&results, ",\"failure\":");
    if (failure_message) {
      zbuf_append(&results, "{\"message\":");
      append_json_string(&results, failure_message);
      zbuf_append(&results, ",\"sourceFile\":");
      append_json_string(&results, test_json_source_path_for_line(input, failure.line ? failure.line : fun->line));
      zbuf_appendf(&results, ",\"line\":%d,\"column\":%d}", test_json_source_line_for_line(input, failure.line ? failure.line : fun->line), failure.column ? failure.column : fun->column);
    } else {
      zbuf_append(&results, "null");
    }
    zbuf_append(&results, "}");
  }
  zbuf_append(&results, "]");

  char stdout_text[64];
  snprintf(stdout_text, sizeof(stdout_text), "%zu test(s) ok\n", selected);
  const char *stderr_text = first_failure.message[0] ? first_failure.message : "";
  bool ok = failed == 0;
  long long duration_ms = now_ms() - started_ms;
  if (command && command->json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
    zbuf_append(&buf, ok ? "true" : "false");
    append_test_source_json(&buf, input, command);
    zbuf_append(&buf, ",\n  \"target\": ");
    append_json_string(&buf, target ? target->name : z_host_target());
    zbuf_append(&buf, ",\n  \"testBackend\": \"direct-frontend\",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"selectedTests\": ");
    zbuf_appendf(&buf, "%zu", selected);
    zbuf_append(&buf, ",\n  \"discoveredTests\": ");
    zbuf_appendf(&buf, "%zu", discovered);
    zbuf_append(&buf, ",\n  \"passedTests\": ");
    zbuf_appendf(&buf, "%zu", passed);
    zbuf_append(&buf, ",\n  \"failedTests\": ");
    zbuf_appendf(&buf, "%zu", failed);
    zbuf_append(&buf, ",\n  \"expectedFailures\": ");
    zbuf_appendf(&buf, "%zu", expected_failures);
    zbuf_append(&buf, ",\n  \"unexpectedPasses\": ");
    zbuf_appendf(&buf, "%zu", unexpected_passes);
    zbuf_append(&buf, ",\n  \"durationMs\": ");
    zbuf_appendf(&buf, "%lld", duration_ms);
    zbuf_append(&buf, ",\n  \"exitCode\": ");
    zbuf_append(&buf, ok ? "0" : "1");
    zbuf_append(&buf, ",\n  \"stdout\": ");
    append_json_string(&buf, ok ? stdout_text : "");
    zbuf_append(&buf, ",\n  \"stderr\": ");
    append_json_string(&buf, stderr_text);
    zbuf_append(&buf, ",\n  \"testDiscovery\": {\"mode\":");
    append_json_string(&buf, test_discovery_mode(input, command));
    zbuf_append(&buf, ",\"filter\":");
    if (command && command->filter) append_json_string(&buf, command->filter);
    else zbuf_append(&buf, "null");
    zbuf_append(&buf, ",\"packageRoot\":");
    append_json_string(&buf, input && input->package_root ? input->package_root : "");
    zbuf_append(&buf, ",\"manifestPath\":");
    append_json_string(&buf, input && input->manifest_path ? input->manifest_path : "");
    zbuf_appendf(&buf, ",\"sourceFileCount\":%zu,\"moduleCount\":%zu,\"discoveredTests\":%zu,\"selectedTests\":%zu}",
                 input ? input->source_file_count : 0,
                 input ? input->module_count : 0,
                 discovered,
                 selected);
    zbuf_append(&buf, ",\n  \"fixtures\": {\"sourceFiles\":[");
    for (size_t i = 0; input && i < input->source_file_count; i++) {
      if (i > 0) zbuf_append(&buf, ", ");
      append_json_string(&buf, input->source_files[i]);
    }
    zbuf_append(&buf, "],\"goldenOutput\":");
    append_json_string(&buf, stdout_text);
    zbuf_append(&buf, ",\"snapshotKey\":\"zero-test-direct-frontend-v1\"}");
    CapabilitySummary caps = program_capabilities(program);
    zbuf_append(&buf, ",\n  \"targetFacts\": {\"hostTarget\":");
    append_json_string(&buf, z_host_target());
    zbuf_append(&buf, ",\"capabilitySupport\":");
    append_target_capability_facts_json(&buf, target, &caps);
    zbuf_append(&buf, "}");
    zbuf_append(&buf, ",\n  \"results\": ");
    zbuf_append(&buf, results.data ? results.data : "[]");
    zbuf_append(&buf, "\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else if (!ok) {
    fprintf(stderr, "%s\n", first_failure.message);
  } else {
    fputs(stdout_text, stdout);
  }
  zbuf_free(&results);
  return ok ? 0 : 1;
}

static void append_std_helper_object_json(ZBuf *buf, const ZStdHelperInfo *helper, bool include_estimate);

static void append_stdlib_helpers_json(ZBuf *buf) {
  zbuf_append(buf, "[");
  for (size_t i = 0; z_std_helpers[i].name; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_std_helper_object_json(buf, &z_std_helpers[i], false);
  }
  zbuf_append(buf, "]");
}

static int helper_estimated_direct_bytes(const ZStdHelperInfo *helper) {
  if (!helper || !helper->emits_runtime_helper) return 0;
  return 96 + (int)strlen(helper->name) * 4 + helper->arg_count * 16;
}

static const char *helper_module_name(const ZStdHelperInfo *helper) {
  const char *name = helper && helper->name ? helper->name : "";
  if (strncmp(name, "std.args.", strlen("std.args.")) == 0) return "std.args";
  if (strncmp(name, "std.ascii.", strlen("std.ascii.")) == 0) return "std.ascii";
  if (strncmp(name, "std.collections.", strlen("std.collections.")) == 0) return "std.collections";
  if (strncmp(name, "std.env.", strlen("std.env.")) == 0) return "std.env";
  if (strncmp(name, "std.fmt.", strlen("std.fmt.")) == 0) return "std.fmt";
  if (strncmp(name, "std.math.", strlen("std.math.")) == 0) return "std.math";
  if (strncmp(name, "std.path.", strlen("std.path.")) == 0) return "std.path";
  if (strncmp(name, "std.search.", strlen("std.search.")) == 0) return "std.search";
  if (strncmp(name, "std.sort.", strlen("std.sort.")) == 0) return "std.sort";
  if (strncmp(name, "std.str.", strlen("std.str.")) == 0) return "std.str";
  if (strncmp(name, "std.testing.", strlen("std.testing.")) == 0) return "std.testing";
  if (strncmp(name, "std.term.", strlen("std.term.")) == 0) return "std.term";
  if (strncmp(name, "std.text.", strlen("std.text.")) == 0) return "std.text";
  if (strncmp(name, "std.unicode.", strlen("std.unicode.")) == 0) return "std.unicode";
  if (strncmp(name, "std.io.", strlen("std.io.")) == 0) return "std.io";
  if (strncmp(name, "std.codec.", strlen("std.codec.")) == 0) return "std.codec";
  if (strncmp(name, "std.csv.", strlen("std.csv.")) == 0) return "std.csv";
  if (strncmp(name, "std.mem.", strlen("std.mem.")) == 0) return "std.mem";
  if (strncmp(name, "std.parse.", strlen("std.parse.")) == 0) return "std.parse";
  if (strncmp(name, "std.json.", strlen("std.json.")) == 0) return "std.json";
  if (strncmp(name, "std.log.", strlen("std.log.")) == 0) return "std.log";
  if (strncmp(name, "std.url.", strlen("std.url.")) == 0) return "std.url";
  if (strncmp(name, "std.time.", strlen("std.time.")) == 0) return "std.time";
  if (strncmp(name, "std.rand.", strlen("std.rand.")) == 0) return "std.rand";
  if (strncmp(name, "std.regex.", strlen("std.regex.")) == 0) return "std.regex";
  if (strncmp(name, "std.proc.", strlen("std.proc.")) == 0) return "std.proc";
  if (strncmp(name, "std.crypto.", strlen("std.crypto.")) == 0) return "std.crypto";
  if (strncmp(name, "std.inet.", strlen("std.inet.")) == 0) return "std.inet";
  if (strncmp(name, "std.net.", strlen("std.net.")) == 0) return "std.net";
  if (strncmp(name, "std.http.", strlen("std.http.")) == 0) return "std.http";
  if (strncmp(name, "std.fs.", strlen("std.fs.")) == 0) return "std.fs";
  return "std";
}

static const char *helper_error_behavior(const ZStdHelperInfo *helper) {
  if (!helper) return "unknown";
  if (z_std_helper_is_fallible(helper)) {
    static char behavior[160];
    z_std_helper_error_set_text(helper, behavior, sizeof(behavior));
    return behavior;
  }
  if (helper->return_type && strncmp(helper->return_type, "Maybe<", strlen("Maybe<")) == 0) return "returns null on failure";
  if (strcmp(helper->name, "std.proc.spawn") == 0) return "returns ProcStatus";
  return "infallible";
}

static const char *helper_ownership_notes(const ZStdHelperInfo *helper) {
  if (!helper) return "unknown";
  if (helper->return_type && strstr(helper->return_type, "owned<File>")) return "returns owned file handle; close or deterministic cleanup";
  if (helper->return_type && strstr(helper->return_type, "owned<ByteBuf>")) return "returns owned byte buffer backed by the explicit allocator";
  if (helper->allocation_behavior && strstr(helper->allocation_behavior, "caller")) return "borrows or writes caller-owned storage";
  if (helper->allocation_behavior && strstr(helper->allocation_behavior, "allocator")) return "uses explicit allocator state only";
  if (helper->allocation_behavior && strstr(helper->allocation_behavior, "borrows")) return helper->allocation_behavior;
  return "no ownership transfer";
}

static const char *helper_example_path(const ZStdHelperInfo *helper) {
  const char *module = helper_module_name(helper);
  if (strcmp(module, "std.ascii") == 0) return "conformance/native/pass/std-ascii.0";
  if (strcmp(module, "std.collections") == 0) return "conformance/native/pass/std-collections-algorithms.0";
  if (strcmp(module, "std.fmt") == 0) return "conformance/native/pass/std-fmt.0";
  if (strcmp(module, "std.search") == 0 || strcmp(module, "std.sort") == 0) return "conformance/native/pass/std-search-sort-widths.0";
  if (strcmp(module, "std.mem") == 0) return "examples/memory-primitives.0";
  if (helper && helper->name && strncmp(helper->name, "std.str.", strlen("std.str.")) == 0) return "examples/std-str.0";
  if (strcmp(module, "std.testing") == 0 || strcmp(module, "std.log") == 0) return "examples/std-testing-log.0";
  if (strcmp(module, "std.text") == 0) return "conformance/native/pass/std-text.0";
  if (strcmp(module, "std.unicode") == 0) return "conformance/native/pass/std-unicode.0";
  if (helper && helper->name && strncmp(helper->name, "std.math.", strlen("std.math.")) == 0) return "examples/std-math.0";
  if (strcmp(module, "std.io") == 0 || strcmp(module, "std.path") == 0) return "examples/std-path-io.0";
  if (strcmp(module, "std.args") == 0 || strcmp(module, "std.env") == 0) return "examples/cli-file.0";
  if (strcmp(module, "std.fs") == 0) return "examples/zero-hash/";
  if (strcmp(module, "std.codec") == 0 || strcmp(module, "std.json") == 0) return "examples/std-data-formats.0";
  if (strcmp(module, "std.csv") == 0) return "conformance/native/pass/std-csv.0";
  if (helper && helper->name && strncmp(helper->name, "std.url.", strlen("std.url.")) == 0) return "conformance/native/pass/std-codec-json-url.0";
  if (strcmp(module, "std.parse") == 0) return "examples/parse-cursor.0";
  if (strcmp(module, "std.regex") == 0) return "conformance/native/pass/std-regex.0";
  if (strcmp(module, "std.time") == 0 || strcmp(module, "std.rand") == 0 || strcmp(module, "std.proc") == 0 || strcmp(module, "std.crypto") == 0) return "examples/std-platform.0";
  if (strcmp(module, "std.inet") == 0) return "conformance/native/pass/std-inet.0";
  if (strcmp(module, "std.net") == 0) return "conformance/native/pass/std-net-http-breadth.0";
  if (strcmp(module, "std.http") == 0) return "conformance/native/pass/std-http-fetch.0";
  return "examples/README.md";
}

static void append_std_helper_object_json(ZBuf *buf, const ZStdHelperInfo *helper, bool include_estimate) {
  zbuf_append(buf, "{\"name\":");
  append_json_string(buf, helper->name);
  zbuf_append(buf, ",\"module\":");
  append_json_string(buf, helper_module_name(helper));
  zbuf_append(buf, ",\"returnType\":");
  append_json_string(buf, helper->return_type);
  zbuf_appendf(buf, ",\"args\":%d,\"capability\":", helper->arg_count);
  append_json_string(buf, helper->capability);
  zbuf_append(buf, ",\"effects\":");
  if (strcmp(helper->capability, "none") == 0) {
    zbuf_append(buf, "[]");
  } else {
    zbuf_append(buf, "[");
    append_json_string(buf, helper->capability);
    zbuf_append(buf, "]");
  }
  zbuf_append(buf, ",\"targetSupport\":");
  append_json_string(buf, helper->target_support);
  zbuf_append(buf, ",\"allocationBehavior\":");
  append_json_string(buf, helper->allocation_behavior);
  zbuf_append(buf, ",\"errorBehavior\":");
  append_json_string(buf, helper_error_behavior(helper));
  zbuf_append(buf, ",\"ownershipNotes\":");
  append_json_string(buf, helper_ownership_notes(helper));
  zbuf_append(buf, ",\"example\":");
  append_json_string(buf, helper_example_path(helper));
  zbuf_append(buf, ",\"apiStability\":\"bootstrap-stable\"");
  zbuf_appendf(buf, ",\"emitsRuntimeHelper\":%s", helper->emits_runtime_helper ? "true" : "false");
  if (include_estimate) zbuf_appendf(buf, ",\"estimatedDirectBytes\":%d", helper_estimated_direct_bytes(helper));
  zbuf_append(buf, "}");
}

static void append_used_stdlib_helpers_json(ZBuf *buf, const HelperUseSummary *helpers) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; z_std_helpers[i].name && helpers && i < helpers->len; i++) {
    if (!helper_summary_used(helpers, i)) continue;
    if (wrote) zbuf_append(buf, ", ");
    append_std_helper_object_json(buf, &z_std_helpers[i], true);
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static void append_top_emitted_helpers_json(ZBuf *buf, const HelperUseSummary *helpers) {
  zbuf_append(buf, "[");
  bool *emitted = z_checked_calloc(helpers && helpers->len ? helpers->len : 1, sizeof(bool));
  bool wrote = false;
  for (int rank = 0; rank < 5; rank++) {
    int best_index = -1;
    int best_bytes = -1;
    for (size_t i = 0; z_std_helpers[i].name && helpers && i < helpers->len; i++) {
      if (!helper_summary_used(helpers, i) || emitted[i] || !z_std_helpers[i].emits_runtime_helper) continue;
      int bytes = helper_estimated_direct_bytes(&z_std_helpers[i]);
      if (bytes > best_bytes) {
        best_bytes = bytes;
        best_index = (int)i;
      }
    }
    if (best_index < 0) break;
    emitted[best_index] = true;
    if (wrote) zbuf_append(buf, ", ");
    append_std_helper_object_json(buf, &z_std_helpers[best_index], true);
    wrote = true;
  }
  free(emitted);
  zbuf_append(buf, "]");
}

static bool program_uses_bounds_checked_access(const Program *program);
static void append_runtime_shims_json_ex(ZBuf *buf, const char *emitted_symbol_text, const CapabilitySummary *caps, bool direct_bounds_checks);

static size_t estimated_function_bytes(const Function *fun) {
  if (!fun) return 0;
  return 48 + fun->params.len * 12 + fun->body.len * 32 + (fun->raises ? 16 : 0);
}

static const char *function_retention_reason(const Function *fun) {
  if (!fun) return "unknown";
  if (fun->is_test) return "zero test discovery";
  if (fun->export_c) return "exported C ABI";
  if (fun->name && strcmp(fun->name, "main") == 0) return "entry point";
  if (fun->is_public) return "public API";
  return "reachable from checked module";
}

static size_t profile_debug_metadata_bytes(const Program *program, const char *profile) {
  if (!profile_debug_info(profile)) return 0;
  size_t functions = program ? program->functions.len : 0;
  return 64 + functions * 24;
}

static void append_size_functions_json(ZBuf *buf, const Program *program) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, fun->is_test ? (fun->test_name ? fun->test_name : "test") : fun->name);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, fun->is_test ? "test" : "function");
    zbuf_appendf(buf, ",\"public\":%s,\"exportC\":%s,\"raises\":%s,\"estimatedDirectBytes\":%zu,\"retained\":true,\"retainedBy\":",
                 fun->is_public ? "true" : "false",
                 fun->export_c ? "true" : "false",
                 fun->raises ? "true" : "false",
                 estimated_function_bytes(fun));
    append_json_string(buf, function_retention_reason(fun));
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]");
}

static void append_size_sections_json(ZBuf *buf, const SourceInput *input, const Program *program, const char *profile, long long artifact_bytes) {
  size_t function_bytes = 0;
  for (size_t i = 0; program && i < program->functions.len; i++) function_bytes += estimated_function_bytes(&program->functions.items[i]);
  size_t readonly = input ? input->direct_readonly_data_bytes : 0;
  size_t stack = input ? input->direct_stack_bytes : 0;
  size_t helper_bytes = 0;
  if (input) {
    helper_bytes += input->direct_runtime_helper_count * 96;
    helper_bytes += input->direct_allocator_helper_count * 128;
    helper_bytes += input->direct_buffer_helper_count * 112;
  }
  size_t debug_bytes = profile_debug_metadata_bytes(program, profile);
  zbuf_append(buf, "[");
  zbuf_appendf(buf, "{\"name\":\"text\",\"kind\":\"code\",\"bytes\":%zu,\"retainedBy\":\"retained functions and direct runtime helpers\"}", function_bytes + helper_bytes);
  zbuf_appendf(buf, ", {\"name\":\"rodata\",\"kind\":\"literals\",\"bytes\":%zu,\"retainedBy\":\"string and byte literals referenced by code\"}", readonly);
  zbuf_appendf(buf, ", {\"name\":\"stack\",\"kind\":\"memory\",\"bytes\":%zu,\"retainedBy\":\"fixed locals and direct stack layout\"}", stack);
  zbuf_appendf(buf, ", {\"name\":\"debug-metadata\",\"kind\":\"debug\",\"bytes\":%zu,\"retainedBy\":", debug_bytes);
  append_json_string(buf, debug_bytes ? "profile keeps debug metadata" : "profile strips unrequested debug metadata");
  zbuf_append(buf, "}");
  if (artifact_bytes >= 0) zbuf_appendf(buf, ", {\"name\":\"artifact\",\"kind\":\"output\",\"bytes\":%lld,\"retainedBy\":\"written output artifact\"}", artifact_bytes);
  zbuf_append(buf, "]");
}

static void append_size_literals_json(ZBuf *buf, const SourceInput *input) {
  size_t readonly = input ? input->direct_readonly_data_bytes : 0;
  zbuf_append(buf, "{\"readonlyDataBytes\":");
  zbuf_appendf(buf, "%zu", readonly);
  zbuf_append(buf, ",\"items\":[");
  if (readonly > 0) {
    zbuf_append(buf, "{\"kind\":\"string-or-byte-literal\",\"bytes\":");
    zbuf_appendf(buf, "%zu", readonly);
    zbuf_append(buf, ",\"retainedBy\":\"referenced by retained function bodies\"}");
  }
  zbuf_append(buf, "]}");
}

static void append_size_helper_breakdown_json(ZBuf *buf, const HelperUseSummary *helpers) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; z_std_helpers[i].name && helpers && i < helpers->len; i++) {
    if (!helper_summary_used(helpers, i)) continue;
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, z_std_helpers[i].name);
    zbuf_append(buf, ",\"module\":");
    append_json_string(buf, helper_module_name(&z_std_helpers[i]));
    zbuf_appendf(buf, ",\"estimatedDirectBytes\":%d,\"emitsRuntimeHelper\":%s,\"retainedBy\":\"stdlib helper call\",\"capability\":",
                 helper_estimated_direct_bytes(&z_std_helpers[i]),
                 z_std_helpers[i].emits_runtime_helper ? "true" : "false");
    append_json_string(buf, z_std_helpers[i].capability);
    zbuf_append(buf, "}");
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static void append_size_imports_json(ZBuf *buf, const CapabilitySummary *caps) {
  typedef struct { const char *name; bool enabled; const char *reason; } ImportFact;
  const ImportFact facts[] = {
    {"world.out", caps && caps->world, "World capability requested by entry point"},
    {"std.args", caps && caps->args, "argument capability helper used"},
    {"std.env", caps && caps->env, "environment capability helper used"},
    {"std.fs", caps && caps->fs, "filesystem helper used"},
    {"std.net", caps && caps->net, "network helper used"},
    {"std.proc", caps && caps->proc, "process helper used"},
    {NULL, false, NULL},
  };
  zbuf_append(buf, "[");
  bool wrote = false;
  for (int i = 0; facts[i].name; i++) {
    if (!facts[i].enabled) continue;
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, facts[i].name);
    zbuf_append(buf, ",\"retainedBy\":");
    append_json_string(buf, facts[i].reason);
    zbuf_append(buf, "}");
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static void append_retention_reasons_json(ZBuf *buf, const SourceInput *input, const Program *program, const HelperUseSummary *helpers, const char *profile) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"kind\":\"function\",\"name\":");
    append_json_string(buf, fun->is_test ? (fun->test_name ? fun->test_name : "test") : fun->name);
    zbuf_append(buf, ",\"reason\":");
    append_json_string(buf, function_retention_reason(fun));
    zbuf_append(buf, "}");
    wrote = true;
  }
  for (size_t i = 0; z_std_helpers[i].name && helpers && i < helpers->len; i++) {
    if (!helper_summary_used(helpers, i)) continue;
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"kind\":\"stdlibHelper\",\"name\":");
    append_json_string(buf, z_std_helpers[i].name);
    zbuf_append(buf, ",\"reason\":\"stdlib helper call\",\"estimatedDirectBytes\":");
    zbuf_appendf(buf, "%d}", helper_estimated_direct_bytes(&z_std_helpers[i]));
    wrote = true;
  }
  if (input && input->direct_readonly_data_bytes > 0) {
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"kind\":\"literal\",\"name\":\"readonly-data\",\"reason\":\"referenced by retained code\",\"bytes\":");
    zbuf_appendf(buf, "%zu}", input->direct_readonly_data_bytes);
    wrote = true;
  }
  if (profile_debug_metadata_bytes(program, profile) > 0) {
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"kind\":\"debugMetadata\",\"name\":\"profile-debug-metadata\",\"reason\":\"profile keeps debug or audit metadata\",\"bytes\":");
    zbuf_appendf(buf, "%zu}", profile_debug_metadata_bytes(program, profile));
  }
  zbuf_append(buf, "]");
}

static void append_optimization_hints_json(ZBuf *buf, const SourceInput *input, const HelperUseSummary *helpers, const CapabilitySummary *caps, const char *profile) {
  zbuf_append(buf, "[");
  bool wrote = false;
#define APPEND_HINT(ID, MESSAGE) do { \
  if (wrote) zbuf_append(buf, ", "); \
  zbuf_append(buf, "{\"id\":"); \
  append_json_string(buf, ID); \
  zbuf_append(buf, ",\"message\":"); \
  append_json_string(buf, MESSAGE); \
  zbuf_append(buf, "}"); \
  wrote = true; \
} while (0)
  if (profile_debug_info(profile)) APPEND_HINT("profile-debug-metadata", "debug/dev/audit profiles retain extra metadata; use --profile small or --profile tiny for size work");
  if (caps && caps->fs) APPEND_HINT("hosted-fs-runtime", "std.fs retains hosted filesystem capability shims; move file I/O out of target-neutral paths for smaller cross artifacts");
  if (input && input->direct_readonly_data_bytes > 0) APPEND_HINT("readonly-literals", "string and byte literals are retained by referenced code; inspect literal-heavy output before optimizing code");
  if (helpers) {
    for (size_t i = 0; z_std_helpers[i].name && i < helpers->len; i++) {
      if (helper_summary_used(helpers, i) && z_std_helpers[i].emits_runtime_helper) {
        APPEND_HINT("pay-as-used-helper", "topLargestEmittedHelpers identifies the retained stdlib helper budget");
        break;
      }
    }
  }
  if (!wrote) APPEND_HINT("no-action", "no size-specific hints for this direct subset");
#undef APPEND_HINT
  zbuf_append(buf, "]");
}

static size_t helper_count_used(const HelperUseSummary *helpers) {
  size_t count = 0;
  for (size_t i = 0; helpers && z_std_helpers[i].name && i < helpers->len; i++) {
    if (helper_summary_used(helpers, i)) count++;
  }
  return count;
}

static bool size_report_uses_llvm_backend(const Command *command) {
  return command && command->emit == EMIT_EXE && z_backend_request_is_llvm(command->backend, "exe");
}

static long long input_compile_time_ms(const SourceInput *input) {
  if (!input) return 0;
  return input->resolve_ms + input->parse_ms + input->interface_ms + input->check_ms + input->lower_ms + input->codegen_ms + input->object_ms + input->link_ms;
}

static const char *direct_size_selected_emitter(const ZTargetInfo *target, const Command *command) {
  const char *name = z_direct_backend_emitter_for_emit_kind(target, "exe", z_backend_direct_request_name(command ? command->backend : NULL));
  return name && name[0] ? name : z_direct_backend_status(target);
}

static void append_backend_retention_json(ZBuf *buf, const SourceInput *input, const HelperUseSummary *helpers, const CapabilitySummary *caps) {
  zbuf_appendf(buf, "{\"functionCount\":%zu,\"usedStdlibHelperCount\":%zu,\"runtimeHelperCount\":%zu,\"readonlyDataBytes\":%zu,\"stackBytes\":%zu,\"worldRuntimeShim\":%s,\"hostFsRuntimeShim\":%s}",
               input ? input->direct_function_count : 0,
               helper_count_used(helpers),
               input ? input->direct_runtime_helper_count : 0,
               input ? input->direct_readonly_data_bytes : 0,
               input ? input->direct_stack_bytes : 0,
               caps && caps->world ? "true" : "false",
               caps && caps->fs ? "true" : "false");
}

static void append_llvm_lowering_status_json(ZBuf *buf, const IrProgram *ir, long long *llvm_ir_bytes_out) {
  if (llvm_ir_bytes_out) *llvm_ir_bytes_out = -1;
  ZBuf llvm_ir;
  ZDiag diag = {0};
  bool ok = ir && ir->mir_valid && z_emit_llvm_ir_from_ir(ir, &llvm_ir, &diag);
  zbuf_appendf(buf, "{\"ok\":%s", ok ? "true" : "false");
  if (ok) {
    if (llvm_ir_bytes_out) *llvm_ir_bytes_out = (long long)llvm_ir.len;
    zbuf_appendf(buf, ",\"llvmIrBytes\":%zu,\"stage\":\"ready\",\"unsupportedFeature\":\"\"", llvm_ir.len);
    zbuf_free(&llvm_ir);
  } else {
    zbuf_append(buf, ",\"llvmIrBytes\":null,\"stage\":\"lower\",\"unsupportedFeature\":");
    append_json_string(buf, ir && ir->backend_blocker.unsupported_feature[0] ? ir->backend_blocker.unsupported_feature : (diag.backend_blocker.unsupported_feature[0] ? diag.backend_blocker.unsupported_feature : "unsupported MIR"));
  }
  zbuf_append(buf, "}");
}

static void append_backend_profile_json(ZBuf *buf, const Command *command, const SourceInput *input, const ZTargetInfo *target, const IrProgram *ir, const HelperUseSummary *helpers, const CapabilitySummary *caps) {
  const char *profile = command && command->profile ? command->profile : "release";
  bool llvm = size_report_uses_llvm_backend(command);
  zbuf_append(buf, "{\"schemaVersion\":1,\"backendFamily\":");
  append_json_string(buf, llvm ? "llvm" : "direct");
  zbuf_append(buf, ",\"emit\":\"exe\",\"profile\":");
  append_json_string(buf, profile);
  zbuf_append(buf, ",\"profileKey\":");
  append_json_string(buf, profile_key_name(profile));
  zbuf_append(buf, ",\"selectedEmitter\":");
  append_json_string(buf, llvm ? "llvm-clang-exe" : direct_size_selected_emitter(target, command));
  zbuf_append(buf, ",\"targetTriple\":");
  append_json_string(buf, llvm ? z_llvm_target_triple(target) : (target && target->zig_target ? target->zig_target : ""));
  zbuf_append(buf, ",\"optimizationLevel\":");
  append_json_string(buf, llvm ? z_llvm_optimization_level(profile) : profile_codegen_optimization(profile));
  zbuf_append(buf, ",\"fallbackPolicy\":");
  append_json_string(buf, llvm ? "none" : "explicit-direct-never-c-bridge");
  zbuf_append(buf, ",\"retained\":");
  append_backend_retention_json(buf, input, helpers, caps);
  zbuf_append(buf, ",\"compileTimeMs\":");
  zbuf_appendf(buf, "%lld", input_compile_time_ms(input));
  if (llvm) {
    ZLlvmToolchainPlan plan = z_llvm_toolchain_plan(target);
    long long llvm_ir_bytes = -1;
    z_append_llvm_backend_lifecycle_field_json(buf);
    zbuf_append(buf, ",\"toolchain\":");
    z_append_llvm_toolchain_plan_json(buf, target);
    zbuf_appendf(buf, ",\"readiness\":{\"nativeExecutable\":%s,\"toolchainStatus\":", plan.native_executable ? "true" : "false");
    append_json_string(buf, plan.status);
    zbuf_append(buf, ",\"mir\":");
    append_llvm_lowering_status_json(buf, ir, &llvm_ir_bytes);
    zbuf_append(buf, "}");
    zbuf_append(buf, ",\"llvmIrBytes\":");
    if (llvm_ir_bytes >= 0) zbuf_appendf(buf, "%lld", llvm_ir_bytes);
    else zbuf_append(buf, "null");
  } else {
    zbuf_append(buf, ",\"toolchain\":{\"driverKind\":\"none\",\"status\":\"direct-emitter\"},\"readiness\":{\"nativeExecutable\":true,\"toolchainStatus\":\"not-required\",\"mir\":{\"ok\":");
    zbuf_append(buf, ir && ir->mir_valid ? "true" : "false");
    zbuf_append(buf, ",\"stage\":\"ready\",\"unsupportedFeature\":\"\"}},\"llvmIrBytes\":null");
  }
  zbuf_append(buf, "}");
}

static void append_backend_comparison_row_json(ZBuf *buf, const char *id, const char *backend, const char *profile, const char *optimization, bool available, const char *status, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"id\":");
  append_json_string(buf, id);
  zbuf_append(buf, ",\"backendFamily\":");
  append_json_string(buf, backend);
  zbuf_append(buf, ",\"profile\":");
  append_json_string(buf, profile);
  zbuf_append(buf, ",\"optimizationLevel\":");
  append_json_string(buf, optimization);
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target && target->name ? target->name : z_host_target());
  zbuf_appendf(buf, ",\"available\":%s,\"status\":", available ? "true" : "false");
  append_json_string(buf, status);
  zbuf_append(buf, ",\"compileTimeMs\":null,\"artifactBytes\":null}");
}

static void append_backend_comparison_json(ZBuf *buf, const Command *command, const ZTargetInfo *target) {
  (void)command;
  ZLlvmToolchainPlan llvm = z_llvm_toolchain_plan(target);
  zbuf_append(buf, "{\"schemaVersion\":1,\"source\":\"size-profile-metadata\",\"note\":\"run pnpm run llvm:profile for measured direct and LLVM build rows\",\"rows\":[");
  append_backend_comparison_row_json(buf, "direct-debug", "direct", "debug", "none", true, "available", target);
  zbuf_append(buf, ", ");
  append_backend_comparison_row_json(buf, "direct-small", "direct", "small", "size", true, "available", target);
  zbuf_append(buf, ", ");
  append_backend_comparison_row_json(buf, "llvm-no-opt", "llvm", "debug", z_llvm_optimization_level("debug"), llvm.native_executable, llvm.status, target);
  zbuf_append(buf, ", ");
  append_backend_comparison_row_json(buf, "llvm-optimized", "llvm", "small", z_llvm_optimization_level("small"), llvm.native_executable, llvm.status, target);
  zbuf_append(buf, "]}");
}

static void append_size_breakdown_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command, const HelperUseSummary *helpers, const CapabilitySummary *caps, long long artifact_bytes) {
  const char *profile = command && command->profile ? command->profile : "release";
  size_t function_count = program && program->functions.len > 0 ? program->functions.len : (input ? input->direct_function_count : 0);
  zbuf_append(buf, "{\"schemaVersion\":1,\"profile\":");
  append_json_string(buf, profile);
  zbuf_append(buf, ",\"profileKey\":");
  append_json_string(buf, profile_key_name(profile));
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_appendf(buf, ",\"summary\":{\"functionCount\":%zu,\"stdlibHelperCount\":%zu,\"runtimeImportCount\":%d,\"debugMetadataBytes\":%zu},\"functions\":",
               function_count,
               helpers ? helper_count_used(helpers) : 0,
               caps ? ((caps->world ? 1 : 0) + (caps->args ? 1 : 0) + (caps->env ? 1 : 0) + (caps->fs ? 1 : 0) + (caps->net ? 1 : 0) + (caps->proc ? 1 : 0)) : 0,
               profile_debug_metadata_bytes(program, profile));
  append_size_functions_json(buf, program);
  zbuf_append(buf, ",\"sections\":");
  append_size_sections_json(buf, input, program, profile, artifact_bytes);
  zbuf_append(buf, ",\"literals\":");
  append_size_literals_json(buf, input);
  zbuf_append(buf, ",\"stdlibHelpers\":");
  append_size_helper_breakdown_json(buf, helpers);
  zbuf_append(buf, ",\"imports\":");
  append_size_imports_json(buf, caps);
  zbuf_append(buf, ",\"runtimeShims\":");
  append_runtime_shims_json_ex(buf, NULL, caps, program_uses_bounds_checked_access(program));
  zbuf_append(buf, ",\"debugMetadata\":{\"bytes\":");
  zbuf_appendf(buf, "%zu", profile_debug_metadata_bytes(program, profile));
  zbuf_append(buf, ",\"policy\":");
  append_json_string(buf, profile_runtime_metadata(profile));
  zbuf_append(buf, ",\"retainedBy\":");
  append_json_string(buf, profile_debug_info(profile) ? "profile metadata policy" : "stripped by profile");
  zbuf_append(buf, "}}");
}

static void append_compiler_runtime_helpers_json_ex(ZBuf *buf, const char *emitted_symbol_text, bool seed_compiler_runtime) {
  zbuf_append(buf, "[");
  for (size_t i = 0; compiler_runtime_helpers[i].name; i++) {
    const CompilerRuntimeHelperInfo *helper = &compiler_runtime_helpers[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, helper->name);
    zbuf_append(buf, ",\"symbol\":");
    append_json_string(buf, helper->symbol);
    zbuf_append(buf, ",\"category\":");
    append_json_string(buf, helper->category);
    zbuf_appendf(buf, ",\"estimatedDirectBytes\":%d,\"emitted\":%s,\"payAsUsed\":true}",
                 helper->estimated_direct_bytes,
                 (emitted_symbol_text && strstr(emitted_symbol_text, helper->symbol)) || seed_compiler_runtime ? "true" : "false");
  }
  zbuf_append(buf, "]");
}

typedef struct {
  char names[128][192];
  size_t len;
  bool wrote;
} DirectGenericJsonState;

typedef struct {
  const char *name;
  char *type;
  bool is_static;
} DirectGenericBinding;

static bool append_direct_generic_specialization_item(ZBuf *buf, DirectGenericJsonState *state, const char *name) {
  if (!buf || !state || !name || !name[0]) return false;
  for (size_t i = 0; i < state->len; i++) {
    if (strcmp(state->names[i], name) == 0) return false;
  }
  if (state->len >= sizeof(state->names) / sizeof(state->names[0])) return false;
  snprintf(state->names[state->len++], sizeof(state->names[0]), "%s", name);
  if (state->wrote) zbuf_append(buf, ", ");
  zbuf_append(buf, "{\"name\":");
  append_json_string(buf, name);
  zbuf_append(buf, ",\"kind\":\"generic-specialization\",\"estimatedDirectBytes\":128,\"source\":\"direct-size-metadata\"}");
  state->wrote = true;
  return true;
}

static char *direct_trim_copy(const char *text) {
  if (!text) return z_strdup("");
  const char *start = text;
  while (*start && isspace((unsigned char)*start)) start++;
  const char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

static const char *direct_resolve_alias_type(const Program *program, const char *type) {
  if (!program || !type) return type;
  const char *current = type;
  for (size_t depth = 0; depth < program->aliases.len; depth++) {
    const TypeAlias *next = NULL;
    for (size_t i = 0; i < program->aliases.len; i++) {
      if (strcmp(program->aliases.items[i].name, current) == 0) {
        next = &program->aliases.items[i];
        break;
      }
    }
    if (!next || !next->target) return current;
    current = next->target;
  }
  return current;
}

static char *direct_resolved_type_copy(const Program *program, const char *type) {
  char *trimmed = direct_trim_copy(type);
  const char *resolved = direct_resolve_alias_type(program, trimmed);
  if (resolved == trimmed) return trimmed;
  char *copy = direct_trim_copy(resolved);
  free(trimmed);
  return copy;
}

static bool direct_split_generic_args(const char *inner, size_t inner_len, char ***out_items, size_t *out_len) {
  char **items = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t start = 0;
  int depth = 0;
  for (size_t i = 0; i <= inner_len; i++) {
    char c = i < inner_len ? inner[i] : ',';
    if (c == '<') depth++;
    else if (c == '>') depth--;
    if ((c == ',' && depth == 0) || i == inner_len) {
      size_t end = i;
      while (start < end && isspace((unsigned char)inner[start])) start++;
      while (end > start && isspace((unsigned char)inner[end - 1])) end--;
      if (end == start) {
        for (size_t j = 0; j < len; j++) free(items[j]);
        free(items);
        return false;
      }
      if (len + 1 > cap) {
        cap = z_grow_capacity(cap, len + 1, 4);
        items = z_checked_reallocarray(items, cap, sizeof(char *));
      }
      items[len++] = z_strndup(inner + start, end - start);
      start = i + 1;
    }
    if (depth < 0) {
      for (size_t j = 0; j < len; j++) free(items[j]);
      free(items);
      return false;
    }
  }
  if (depth != 0 || len == 0) {
    for (size_t j = 0; j < len; j++) free(items[j]);
    free(items);
    return false;
  }
  *out_items = items;
  *out_len = len;
  return true;
}

static bool direct_type_generic_arg_list(const char *type, const char *name, char ***out_items, size_t *out_len) {
  if (!type || !name) return false;
  size_t name_len = strlen(name);
  size_t type_len = strlen(type);
  if (type_len <= name_len + 2 || strncmp(type, name, name_len) != 0 || type[name_len] != '<' || type[type_len - 1] != '>') return false;
  return direct_split_generic_args(type + name_len + 1, type_len - name_len - 2, out_items, out_len);
}

static void direct_free_type_arg_list(char **items, size_t len) {
  for (size_t i = 0; i < len; i++) free(items[i]);
  free(items);
}

static bool direct_parse_static_uint_text(const char *text, unsigned long long *out) {
  if (!text || !text[0]) return false;
  size_t text_len = strlen(text);
  size_t body_len = text_len;
  const char *last_underscore = strrchr(text, '_');
  if (last_underscore && isalpha((unsigned char)last_underscore[1])) body_len = (size_t)(last_underscore - text);
  if (body_len == 0) return false;
  unsigned radix = 10;
  size_t index = 0;
  if (body_len > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    radix = 16;
    index = 2;
  } else if (body_len > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
    radix = 2;
    index = 2;
  } else if (body_len > 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) {
    radix = 8;
    index = 2;
  }
  unsigned long long value = 0;
  bool saw_digit = false;
  for (; index < body_len; index++) {
    char ch = text[index];
    if (ch == '_') continue;
    int digit = -1;
    if (ch >= '0' && ch <= '9') digit = ch - '0';
    else if (ch >= 'a' && ch <= 'f') digit = 10 + (ch - 'a');
    else if (ch >= 'A' && ch <= 'F') digit = 10 + (ch - 'A');
    if (digit < 0 || (unsigned)digit >= radix) return false;
    if (value > (ULLONG_MAX - (unsigned)digit) / radix) return false;
    value = value * radix + (unsigned)digit;
    saw_digit = true;
  }
  if (!saw_digit) return false;
  if (out) *out = value;
  return true;
}

static bool direct_eval_static_const_expr(const Program *program, const Expr *expr, unsigned long long *out, size_t depth) {
  if (!expr || depth > 32) return false;
  switch (expr->kind) {
    case EXPR_NUMBER:
      return direct_parse_static_uint_text(expr->text, out);
    case EXPR_IDENT:
      for (size_t i = 0; program && i < program->consts.len; i++) {
        if (strcmp(program->consts.items[i].name, expr->text) == 0) {
          return direct_eval_static_const_expr(program, program->consts.items[i].expr, out, depth + 1);
        }
      }
      return false;
    case EXPR_BINARY: {
      unsigned long long left = 0;
      unsigned long long right = 0;
      if (!direct_eval_static_const_expr(program, expr->left, &left, depth + 1) ||
          !direct_eval_static_const_expr(program, expr->right, &right, depth + 1)) return false;
      if (strcmp(expr->text, "+") == 0) *out = left + right;
      else if (strcmp(expr->text, "-") == 0) {
        if (right > left) return false;
        *out = left - right;
      } else if (strcmp(expr->text, "*") == 0) *out = left * right;
      else if (strcmp(expr->text, "/") == 0) {
        if (right == 0) return false;
        *out = left / right;
      } else if (strcmp(expr->text, "%") == 0) {
        if (right == 0) return false;
        *out = left % right;
      } else return false;
      return true;
    }
    default:
      return false;
  }
}

static char *direct_canonical_static_arg(const Program *program, const char *text) {
  unsigned long long value = 0;
  if (direct_parse_static_uint_text(text, &value)) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_appendf(&buf, "%llu", value);
    return buf.data;
  }
  for (size_t i = 0; program && i < program->consts.len; i++) {
    if (strcmp(program->consts.items[i].name, text) == 0 &&
        direct_eval_static_const_expr(program, program->consts.items[i].expr, &value, 0)) {
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_appendf(&buf, "%llu", value);
      return buf.data;
    }
  }
  return NULL;
}

static const char *direct_lookup_binding(const DirectGenericBinding *bindings, size_t binding_len, const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; i < binding_len; i++) {
    if (bindings[i].name && bindings[i].type && strcmp(bindings[i].name, name) == 0) return bindings[i].type;
  }
  return NULL;
}

static int direct_binding_index(DirectGenericBinding *bindings, size_t binding_len, const char *name) {
  if (!name) return -1;
  for (size_t i = 0; i < binding_len; i++) {
    if (bindings[i].name && strcmp(bindings[i].name, name) == 0) return (int)i;
  }
  return -1;
}

static void direct_set_binding(const Program *program, DirectGenericBinding *bindings, size_t binding_len, const char *name, const char *type) {
  int index = direct_binding_index(bindings, binding_len, name);
  if (index < 0 || !type || !type[0]) return;
  char *resolved = bindings[index].is_static ? direct_canonical_static_arg(program, type) : NULL;
  if (!resolved) resolved = direct_resolved_type_copy(program, type);
  if (!bindings[index].type) {
    bindings[index].type = resolved;
    return;
  }
  if (strcmp(bindings[index].type, resolved) == 0) {
    free(resolved);
    return;
  }
  free(resolved);
}

static char *direct_substitute_type(const Program *program, const char *type, const DirectGenericBinding *bindings, size_t binding_len) {
  char *base = direct_resolved_type_copy(program, type);
  const char *bound = direct_lookup_binding(bindings, binding_len, base);
  if (bound) {
    free(base);
    return z_strdup(bound);
  }
  if (strncmp(base, "const ", strlen("const ")) == 0) {
    char *inner = direct_substitute_type(program, base + strlen("const "), bindings, binding_len);
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "const ");
    zbuf_append(&buf, inner);
    free(inner);
    free(base);
    return buf.data;
  }
  if (base[0] == '[') {
    const char *close = strchr(base, ']');
    if (close && close[1]) {
      char *length = z_strndup(base + 1, (size_t)(close - base - 1));
      const char *bound_len = direct_lookup_binding(bindings, binding_len, length);
      char *inner = direct_substitute_type(program, close + 1, bindings, binding_len);
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_append_char(&buf, '[');
      zbuf_append(&buf, bound_len ? bound_len : length);
      zbuf_append_char(&buf, ']');
      zbuf_append(&buf, inner);
      free(length);
      free(inner);
      free(base);
      return buf.data;
    }
  }
  const char *open = strchr(base, '<');
  const char *close = strrchr(base, '>');
  if (open && close && close[1] == 0 && open > base) {
    char *name = z_strndup(base, (size_t)(open - base));
    char **args = NULL;
    size_t arg_len = 0;
    if (direct_type_generic_arg_list(base, name, &args, &arg_len)) {
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_append(&buf, name);
      zbuf_append_char(&buf, '<');
      for (size_t i = 0; i < arg_len; i++) {
        if (i > 0) zbuf_append(&buf, ",");
        char *arg = direct_substitute_type(program, args[i], bindings, binding_len);
        zbuf_append(&buf, arg);
        free(arg);
      }
      zbuf_append_char(&buf, '>');
      direct_free_type_arg_list(args, arg_len);
      free(name);
      free(base);
      return buf.data;
    }
    free(name);
  }
  return base;
}

static void direct_append_mangled_component(ZBuf *buf, const char *text) {
  if (!text || !text[0]) {
    zbuf_append(buf, "Unknown");
    return;
  }
  bool wrote = false;
  bool last_separator = false;
  for (const char *cursor = text; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (isalnum(ch) || ch == '_') {
      zbuf_append_char(buf, (char)ch);
      wrote = true;
      last_separator = false;
    } else if (!last_separator && wrote) {
      zbuf_append_char(buf, '_');
      last_separator = true;
    }
  }
  if (!wrote) zbuf_append(buf, "Unknown");
}

static char *direct_shape_specialized_name(const Shape *shape, char **args, size_t arg_len) {
  ZBuf name;
  zbuf_init(&name);
  zbuf_append(&name, "z_");
  direct_append_mangled_component(&name, shape ? shape->name : "Unknown");
  zbuf_append_char(&name, '_');
  for (size_t i = 0; i < arg_len; i++) {
    direct_append_mangled_component(&name, args[i]);
    zbuf_append_char(&name, '_');
  }
  return name.data;
}

static char *direct_function_specialized_name(const Function *fun, const DirectGenericBinding *bindings, size_t binding_len) {
  ZBuf name;
  zbuf_init(&name);
  zbuf_append(&name, "z_");
  direct_append_mangled_component(&name, fun ? fun->name : "unknown");
  for (size_t i = 0; i < binding_len; i++) {
    zbuf_append(&name, "__");
    direct_append_mangled_component(&name, bindings[i].type ? bindings[i].type : "Unknown");
  }
  return name.data;
}

static void direct_bind_type_pattern(const Program *program, const char *pattern, const char *actual, DirectGenericBinding *bindings, size_t binding_len) {
  char *pattern_type = direct_resolved_type_copy(program, pattern);
  char *actual_type = direct_resolved_type_copy(program, actual);
  if (!pattern_type[0] || !actual_type[0]) {
    free(pattern_type);
    free(actual_type);
    return;
  }
  if (direct_binding_index(bindings, binding_len, pattern_type) >= 0) {
    direct_set_binding(program, bindings, binding_len, pattern_type, actual_type);
    free(pattern_type);
    free(actual_type);
    return;
  }
  if (strncmp(pattern_type, "const ", strlen("const ")) == 0) {
    const char *actual_inner = strncmp(actual_type, "const ", strlen("const ")) == 0 ? actual_type + strlen("const ") : actual_type;
    direct_bind_type_pattern(program, pattern_type + strlen("const "), actual_inner, bindings, binding_len);
    free(pattern_type);
    free(actual_type);
    return;
  }
  if (pattern_type[0] == '[') {
    const char *pattern_close = strchr(pattern_type, ']');
    const char *actual_close = actual_type[0] == '[' ? strchr(actual_type, ']') : NULL;
    if (pattern_close && actual_close && pattern_close[1] && actual_close[1]) {
      char *pattern_len = z_strndup(pattern_type + 1, (size_t)(pattern_close - pattern_type - 1));
      char *actual_len = z_strndup(actual_type + 1, (size_t)(actual_close - actual_type - 1));
      if (direct_binding_index(bindings, binding_len, pattern_len) >= 0) {
        direct_set_binding(program, bindings, binding_len, pattern_len, actual_len);
      }
      direct_bind_type_pattern(program, pattern_close + 1, actual_close + 1, bindings, binding_len);
      free(pattern_len);
      free(actual_len);
    }
    free(pattern_type);
    free(actual_type);
    return;
  }
  const char *pattern_open = strchr(pattern_type, '<');
  const char *pattern_close = strrchr(pattern_type, '>');
  const char *actual_open = strchr(actual_type, '<');
  const char *actual_close = strrchr(actual_type, '>');
  if (pattern_open && pattern_close && pattern_close[1] == 0 &&
      actual_open && actual_close && actual_close[1] == 0 &&
      pattern_open > pattern_type && actual_open > actual_type) {
    char *pattern_name = z_strndup(pattern_type, (size_t)(pattern_open - pattern_type));
    char *actual_name = z_strndup(actual_type, (size_t)(actual_open - actual_type));
    char **pattern_args = NULL;
    char **actual_args = NULL;
    size_t pattern_arg_len = 0;
    size_t actual_arg_len = 0;
    bool same_owner = strcmp(pattern_name, actual_name) == 0;
    bool pattern_ok = direct_type_generic_arg_list(pattern_type, pattern_name, &pattern_args, &pattern_arg_len);
    bool actual_ok = direct_type_generic_arg_list(actual_type, actual_name, &actual_args, &actual_arg_len);
    if (same_owner && pattern_ok && actual_ok && pattern_arg_len == actual_arg_len) {
      for (size_t i = 0; i < pattern_arg_len; i++) {
        direct_bind_type_pattern(program, pattern_args[i], actual_args[i], bindings, binding_len);
      }
    }
    direct_free_type_arg_list(pattern_args, pattern_arg_len);
    direct_free_type_arg_list(actual_args, actual_arg_len);
    free(pattern_name);
    free(actual_name);
  }
  free(pattern_type);
  free(actual_type);
}

static void direct_free_bindings(DirectGenericBinding *bindings, size_t binding_len) {
  for (size_t i = 0; i < binding_len; i++) free(bindings[i].type);
}

static bool direct_bindings_complete(const DirectGenericBinding *bindings, size_t binding_len) {
  for (size_t i = 0; i < binding_len; i++) {
    if (!bindings[i].type || !bindings[i].type[0]) return false;
  }
  return true;
}

static const TypeArgVec *direct_call_type_args(const Expr *call) {
  if (!call) return NULL;
  if (call->checked_type_args.len > 0) return &call->checked_type_args;
  if (call->type_args.len > 0) return &call->type_args;
  if (call->kind == EXPR_CALL && call->left && call->left->checked_type_args.len > 0) return &call->left->checked_type_args;
  if (call->left && call->left->type_args.len > 0) return &call->left->type_args;
  return NULL;
}

static void direct_collect_shape_specializations_from_type(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const char *type, const DirectGenericBinding *bindings, size_t binding_len);
static void direct_collect_shape_specializations_from_expr(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const Expr *expr, const DirectGenericBinding *bindings, size_t binding_len);
static void direct_collect_shape_specializations_from_stmt_vec(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const StmtVec *body, const DirectGenericBinding *bindings, size_t binding_len);
static void direct_collect_generic_function_specializations_from_stmt_vec(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const StmtVec *body, const DirectGenericBinding *bindings, size_t binding_len);

static void direct_collect_shape_specializations_from_type(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const char *type, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!program || !type || !type[0]) return;
  char *resolved = direct_substitute_type(program, type, bindings, binding_len);
  for (size_t i = 0; i < program->shapes.len; i++) {
    const Shape *shape = &program->shapes.items[i];
    if (shape->type_params.len == 0) continue;
    char **args = NULL;
    size_t arg_len = 0;
    if (direct_type_generic_arg_list(resolved, shape->name, &args, &arg_len) && arg_len == shape->type_params.len) {
      char **specialized_args = z_checked_calloc(arg_len, sizeof(char *));
      for (size_t arg_index = 0; arg_index < arg_len; arg_index++) {
        specialized_args[arg_index] = shape->type_params.items[arg_index].is_static
          ? direct_canonical_static_arg(program, args[arg_index])
          : NULL;
        if (!specialized_args[arg_index]) specialized_args[arg_index] = z_strdup(args[arg_index]);
        direct_collect_shape_specializations_from_type(buf, state, program, specialized_args[arg_index], NULL, 0);
      }
      char *name = direct_shape_specialized_name(shape, specialized_args, arg_len);
      append_direct_generic_specialization_item(buf, state, name);
      free(name);
      direct_free_type_arg_list(specialized_args, arg_len);
    }
    direct_free_type_arg_list(args, arg_len);
  }
  const char *open = strchr(resolved, '<');
  const char *close = strrchr(resolved, '>');
  if (open && close && close[1] == 0 && open > resolved) {
    char *name = z_strndup(resolved, (size_t)(open - resolved));
    char **args = NULL;
    size_t arg_len = 0;
    if (direct_type_generic_arg_list(resolved, name, &args, &arg_len)) {
      for (size_t i = 0; i < arg_len; i++) {
        direct_collect_shape_specializations_from_type(buf, state, program, args[i], NULL, 0);
      }
    }
    direct_free_type_arg_list(args, arg_len);
    free(name);
  } else if (resolved[0] == '[') {
    const char *close_bracket = strchr(resolved, ']');
    if (close_bracket && close_bracket[1]) {
      direct_collect_shape_specializations_from_type(buf, state, program, close_bracket + 1, NULL, 0);
    }
  }
  free(resolved);
}

static void direct_collect_shape_specializations_from_expr(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const Expr *expr, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!expr) return;
  direct_collect_shape_specializations_from_type(buf, state, program, expr->resolved_type, bindings, binding_len);
  direct_collect_shape_specializations_from_expr(buf, state, program, expr->left, bindings, binding_len);
  direct_collect_shape_specializations_from_expr(buf, state, program, expr->right, bindings, binding_len);
  for (size_t i = 0; i < expr->args.len; i++) {
    direct_collect_shape_specializations_from_expr(buf, state, program, expr->args.items[i], bindings, binding_len);
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    direct_collect_shape_specializations_from_expr(buf, state, program, expr->fields.items[i].value, bindings, binding_len);
  }
}

static void direct_collect_shape_specializations_from_stmt_vec(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const StmtVec *body, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!body) return;
  for (size_t i = 0; i < body->len; i++) {
    Stmt *stmt = body->items[i];
    direct_collect_shape_specializations_from_type(buf, state, program, stmt->type, bindings, binding_len);
    direct_collect_shape_specializations_from_type(buf, state, program, stmt->resolved_type, bindings, binding_len);
    direct_collect_shape_specializations_from_expr(buf, state, program, stmt->target, bindings, binding_len);
    direct_collect_shape_specializations_from_expr(buf, state, program, stmt->expr, bindings, binding_len);
    direct_collect_shape_specializations_from_expr(buf, state, program, stmt->range_end, bindings, binding_len);
    direct_collect_shape_specializations_from_stmt_vec(buf, state, program, &stmt->then_body, bindings, binding_len);
    direct_collect_shape_specializations_from_stmt_vec(buf, state, program, &stmt->else_body, bindings, binding_len);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      direct_collect_shape_specializations_from_expr(buf, state, program, stmt->match_arms.items[arm_index].guard, bindings, binding_len);
      direct_collect_shape_specializations_from_stmt_vec(buf, state, program, &stmt->match_arms.items[arm_index].body, bindings, binding_len);
    }
  }
}

static void direct_collect_generic_function_specializations_from_expr(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const Expr *expr, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!expr) return;
  if (expr->kind == EXPR_CALL && expr->left && expr->left->kind == EXPR_IDENT) {
    const Function *fun = find_program_function(program, expr->left->text);
    if (fun && fun->type_params.len > 0) {
      size_t specialized_len = fun->type_params.len;
      DirectGenericBinding *specialized = z_checked_calloc(specialized_len, sizeof(DirectGenericBinding));
      for (size_t i = 0; i < specialized_len; i++) {
        specialized[i].name = fun->type_params.items[i].name;
        specialized[i].is_static = fun->type_params.items[i].is_static;
      }
      const TypeArgVec *type_args = direct_call_type_args(expr);
      if (type_args && type_args->len == specialized_len) {
        for (size_t i = 0; i < specialized_len; i++) {
          char *arg = direct_substitute_type(program, type_args->items[i].type, bindings, binding_len);
          direct_set_binding(program, specialized, specialized_len, specialized[i].name, arg);
          free(arg);
        }
      } else {
        size_t arg_len = expr->args.len < fun->params.len ? expr->args.len : fun->params.len;
        for (size_t i = 0; i < arg_len; i++) {
          char *actual = direct_substitute_type(program, expr->args.items[i]->resolved_type, bindings, binding_len);
          direct_bind_type_pattern(program, fun->params.items[i].type, actual, specialized, specialized_len);
          free(actual);
        }
      }
      if (expr->resolved_type && fun->return_type) {
        char *actual_return = direct_substitute_type(program, expr->resolved_type, bindings, binding_len);
        direct_bind_type_pattern(program, fun->return_type, actual_return, specialized, specialized_len);
        free(actual_return);
      }
      if (direct_bindings_complete(specialized, specialized_len)) {
        char *name = direct_function_specialized_name(fun, specialized, specialized_len);
        bool wrote = append_direct_generic_specialization_item(buf, state, name);
        if (wrote) {
          direct_collect_shape_specializations_from_stmt_vec(buf, state, program, &fun->body, specialized, specialized_len);
          direct_collect_generic_function_specializations_from_stmt_vec(buf, state, program, &fun->body, specialized, specialized_len);
        }
        free(name);
      }
      direct_free_bindings(specialized, specialized_len);
      free(specialized);
    }
  }
  direct_collect_generic_function_specializations_from_expr(buf, state, program, expr->left, bindings, binding_len);
  direct_collect_generic_function_specializations_from_expr(buf, state, program, expr->right, bindings, binding_len);
  for (size_t i = 0; i < expr->args.len; i++) {
    direct_collect_generic_function_specializations_from_expr(buf, state, program, expr->args.items[i], bindings, binding_len);
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    direct_collect_generic_function_specializations_from_expr(buf, state, program, expr->fields.items[i].value, bindings, binding_len);
  }
}

static void direct_collect_generic_function_specializations_from_stmt_vec(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const StmtVec *body, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!body) return;
  for (size_t i = 0; i < body->len; i++) {
    Stmt *stmt = body->items[i];
    direct_collect_generic_function_specializations_from_expr(buf, state, program, stmt->target, bindings, binding_len);
    direct_collect_generic_function_specializations_from_expr(buf, state, program, stmt->expr, bindings, binding_len);
    direct_collect_generic_function_specializations_from_expr(buf, state, program, stmt->range_end, bindings, binding_len);
    direct_collect_generic_function_specializations_from_stmt_vec(buf, state, program, &stmt->then_body, bindings, binding_len);
    direct_collect_generic_function_specializations_from_stmt_vec(buf, state, program, &stmt->else_body, bindings, binding_len);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      direct_collect_generic_function_specializations_from_expr(buf, state, program, stmt->match_arms.items[arm_index].guard, bindings, binding_len);
      direct_collect_generic_function_specializations_from_stmt_vec(buf, state, program, &stmt->match_arms.items[arm_index].body, bindings, binding_len);
    }
  }
}

static void append_direct_generic_specializations_json(ZBuf *buf, const Program *program) {
  DirectGenericJsonState state = {0};
  zbuf_append(buf, "[");
  if (program) {
    for (size_t i = 0; i < program->shapes.len; i++) {
      const Shape *shape = &program->shapes.items[i];
      for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
        direct_collect_shape_specializations_from_expr(buf, &state, program, shape->fields.items[field_index].default_value, NULL, 0);
      }
    }
    for (size_t i = 0; i < program->functions.len; i++) {
      const Function *fun = &program->functions.items[i];
      if (fun->is_test) continue;
      if (fun->type_params.len == 0) {
        direct_collect_shape_specializations_from_type(buf, &state, program, fun->return_type, NULL, 0);
        for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
          direct_collect_shape_specializations_from_type(buf, &state, program, fun->params.items[param_index].type, NULL, 0);
        }
        direct_collect_shape_specializations_from_stmt_vec(buf, &state, program, &fun->body, NULL, 0);
        direct_collect_generic_function_specializations_from_stmt_vec(buf, &state, program, &fun->body, NULL, 0);
      }
    }
  }
  zbuf_append(buf, "]");
}

static bool expr_uses_bounds_checked_access(const Expr *expr) {
  if (!expr) return false;
  if (expr->kind == EXPR_INDEX || expr->kind == EXPR_SLICE) return true;
  if (expr_uses_bounds_checked_access(expr->left) || expr_uses_bounds_checked_access(expr->right)) return true;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (expr_uses_bounds_checked_access(expr->args.items[i])) return true;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (expr_uses_bounds_checked_access(expr->fields.items[i].value)) return true;
  }
  return false;
}

static bool stmt_vec_uses_bounds_checked_access(const StmtVec *body) {
  if (!body) return false;
  for (size_t i = 0; i < body->len; i++) {
    Stmt *stmt = body->items[i];
    if (expr_uses_bounds_checked_access(stmt->target) ||
        expr_uses_bounds_checked_access(stmt->expr) ||
        expr_uses_bounds_checked_access(stmt->range_end)) return true;
    if (stmt_vec_uses_bounds_checked_access(&stmt->then_body) ||
        stmt_vec_uses_bounds_checked_access(&stmt->else_body)) return true;
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      if (expr_uses_bounds_checked_access(stmt->match_arms.items[arm_index].guard) ||
          stmt_vec_uses_bounds_checked_access(&stmt->match_arms.items[arm_index].body)) return true;
    }
  }
  return false;
}

static bool program_uses_bounds_checked_access(const Program *program) {
  if (!program) return false;
  for (size_t i = 0; i < program->shapes.len; i++) {
    const Shape *shape = &program->shapes.items[i];
    for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
      if (expr_uses_bounds_checked_access(shape->fields.items[field_index].default_value)) return true;
    }
    for (size_t method_index = 0; method_index < shape->methods.len; method_index++) {
      if (stmt_vec_uses_bounds_checked_access(&shape->methods.items[method_index].body)) return true;
    }
  }
  for (size_t i = 0; i < program->functions.len; i++) {
    if (stmt_vec_uses_bounds_checked_access(&program->functions.items[i].body)) return true;
  }
  return false;
}

static void append_runtime_shims_json_ex(ZBuf *buf, const char *emitted_symbol_text, const CapabilitySummary *caps, bool direct_bounds_checks) {
  zbuf_append(buf, "[");
  bool wrote = false;
#define APPEND_SHIM(name, reason, enabled) do { \
    if (enabled) { \
      if (wrote) zbuf_append(buf, ", "); \
      zbuf_append(buf, "{\"name\":"); \
      append_json_string(buf, name); \
      zbuf_append(buf, ",\"reason\":"); \
      append_json_string(buf, reason); \
      zbuf_append(buf, ",\"payAsUsed\":true}"); \
      wrote = true; \
    } \
  } while (0)
  APPEND_SHIM("bounds-checks", "indexed and sliced access traps instead of silent memory corruption", (emitted_symbol_text && strstr(emitted_symbol_text, "z_bounds_fail")) || direct_bounds_checks);
  APPEND_SHIM("stdio-world", "World output lowers to hosted stdout/stderr calls", (caps && caps->world) || (emitted_symbol_text && strstr(emitted_symbol_text, "fputs")));
  APPEND_SHIM("hosted-fs", "host filesystem helpers are emitted only for std.fs users", caps && caps->fs);
#undef APPEND_SHIM
  zbuf_append(buf, "]");
}

static void append_runtime_shims_json(ZBuf *buf, const char *emitted_symbol_text, const CapabilitySummary *caps) { append_runtime_shims_json_ex(buf, emitted_symbol_text, caps, false); }

typedef struct { const ZProgramGraph *graph; const ZProgramGraphArtifactSource *artifact_source; const char *artifact; const char *lowering; } GraphSizeSource;

static bool write_size_metadata_artifact(const Command *command, SourceInput *input, const ZTargetInfo *target, char **artifact_path, long long *artifact_bytes, ZDiag *diag) {
  if (artifact_path) *artifact_path = NULL;
  if (artifact_bytes) *artifact_bytes = -1;
  if (!command || !command->out) return true;
  bool llvm_report = size_report_uses_llvm_backend(command);

  char *base_artifact_path = z_strdup(command->out); char *path = apply_target_suffix(base_artifact_path, target);
  free(base_artifact_path);

  ZBuf artifact; zbuf_init(&artifact);
  zbuf_append(&artifact, "{\n  \"schemaVersion\": 1,\n  \"kind\": \"zero-size-metadata\",\n  \"sourceFile\": "); append_json_string(&artifact, input && input->source_file ? input->source_file : "");
  zbuf_append(&artifact, ",\n  \"target\": "); append_json_string(&artifact, target ? target->name : z_host_target());
  zbuf_append(&artifact, ",\n  \"backendFamily\": "); append_json_string(&artifact, llvm_report ? "llvm" : "direct");
  if (llvm_report) {
    zbuf_append(&artifact, ",\n  \"backendLifecycle\": "); z_append_llvm_backend_lifecycle_json(&artifact);
    zbuf_append(&artifact, ",\n  \"llvmTargetTriple\": "); append_json_string(&artifact, z_llvm_target_triple(target));
    zbuf_append(&artifact, ",\n  \"llvmOptimizationLevel\": "); append_json_string(&artifact, z_llvm_optimization_level(command && command->profile ? command->profile : "release"));
  }
  zbuf_appendf(&artifact, ",\n  \"generatedCBytes\": 0,\n  \"loweredIrBytes\": %zu\n}\n", input ? input->lowered_ir_bytes : 0);

  long long phase_started = now_ms();
  if (input) input->emitted_object_cache_hit = compiler_cache_touch("emitted-object", command_compile_cache_key(command, input, target, command ? command->profile : "release", llvm_report ? "llvm-size-metadata" : "direct-size-metadata"));
  bool wrote = z_write_file(path, artifact.data, diag);
  if (input) { input->object_ms = now_ms() - phase_started; input->link_ms = 0; }
  if (wrote && artifact_bytes) *artifact_bytes = file_size_or_negative(path);
  zbuf_free(&artifact);

  if (!wrote) {
    if (artifact_path) *artifact_path = path;
    else free(path);
    return false;
  }
  if (artifact_path) *artifact_path = path;
  else free(path);
  return true;
}

static void append_size_graph_source_json(ZBuf *buf, const GraphSizeSource *graph_source) {
  if (!graph_source || (!graph_source->graph && !z_program_graph_artifact_source_present(graph_source->artifact_source))) return;
  const char *artifact = graph_source->artifact ? graph_source->artifact : (graph_source->artifact_source ? graph_source->artifact_source->artifact : "");
  const char *module_identity = graph_source->graph ? graph_source->graph->module_identity : graph_source->artifact_source->module_identity;
  const char *graph_hash = graph_source->graph ? graph_source->graph->graph_hash : graph_source->artifact_source->graph_hash;
  const char *lowering = graph_source->lowering ? graph_source->lowering : (graph_source->artifact_source ? graph_source->artifact_source->lowering : "direct-program-graph");
  bool canonical_source = graph_source->graph ? graph_source->graph->canonical_source : graph_source->artifact_source->canonical_source;
  zbuf_append(buf, ",\n  \"graph\": {\"artifact\": "); append_json_string(buf, artifact ? artifact : "");
  zbuf_appendf(buf, ", \"canonicalSource\": %s, \"moduleIdentity\": ", canonical_source ? "true" : "false"); append_json_string(buf, module_identity ? module_identity : "");
  zbuf_append(buf, ", \"graphHash\": "); append_json_string(buf, graph_hash ? graph_hash : "");
  zbuf_append(buf, ", \"lowering\": "); append_json_string(buf, lowering ? lowering : "direct-program-graph");
  if (graph_source->artifact_source && graph_source->artifact_source->source_projection_state) { zbuf_append(buf, ", \"sourceProjectionState\": "); append_json_string(buf, graph_source->artifact_source->source_projection_state); }
  zbuf_append(buf, "}");
}

static void append_size_report_front_json(ZBuf *buf, const Command *command, SourceInput *input, const Program *program, const ZTargetInfo *target, const IrProgram *ir, const CapabilitySummary *caps, const HelperUseSummary *used_helpers, const GraphSizeSource *graph_source) {
  const char *profile = command && command->profile ? command->profile : "release";
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": "); append_json_string(buf, input && input->source_file ? input->source_file : "");
  append_size_graph_source_json(buf, graph_source);
  zbuf_append(buf, ",\n  \"package\": "); append_package_metadata_json(buf, input, target);
  zbuf_append(buf, ",\n  \"packageCache\": "); append_package_cache_audit_json(buf, input, target, profile);
  zbuf_append(buf, ",\n  \"target\": "); append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\n  \"hostTarget\": "); append_json_string(buf, z_host_target());
  zbuf_append(buf, ",\n  \"profile\": "); append_json_string(buf, profile);
  bool llvm_report = size_report_uses_llvm_backend(command);
  ZLlvmToolchainPlan llvm_plan = z_llvm_toolchain_plan(target);
  zbuf_appendf(buf, ",\n  \"targetSupport\": {\"fsAvailable\": %s, \"backendFamily\": ", z_target_has_capability(target, "fs") ? "true" : "false");
  append_json_string(buf, llvm_report ? "llvm" : "direct");
  zbuf_append(buf, ", \"fallbackPolicy\": ");
  append_json_string(buf, llvm_report ? "none" : "explicit-direct-never-c-bridge");
  if (llvm_report) {
    zbuf_append(buf, ", \"llvmStatus\": ");
    append_json_string(buf, llvm_plan.status);
    zbuf_append(buf, ", \"llvmTargetTriple\": ");
    append_json_string(buf, llvm_plan.target_triple);
    zbuf_append(buf, ", \"backendLifecycle\": ");
    z_append_llvm_backend_lifecycle_json(buf);
  }
  zbuf_append(buf, "},\n  \"requiresCapabilities\": ");
  append_capability_json_array(buf, caps);
  zbuf_append(buf, ",\n  \"runtimeImportAudit\": "); append_runtime_import_audit_json(buf, ir, input, target);
  zbuf_append(buf, ",\n  \"portableRuntime\": "); append_portable_runtime_json(buf, ir, input, target, caps);
  zbuf_append(buf, ",\n  \"stdlibHelpers\": "); append_stdlib_helpers_json(buf);
  zbuf_append(buf, ",\n  \"usedStdlibHelpers\": "); append_used_stdlib_helpers_json(buf, used_helpers);
  zbuf_append(buf, ",\n  \"stdlibHelperAttribution\": "); append_used_stdlib_helpers_json(buf, used_helpers);
  zbuf_append(buf, ",\n  \"selfHostRouting\": "); append_self_host_routing_json(buf, "size", NULL, program, caps, target);
  zbuf_append(buf, ",\n  \"compilerRuntimeHelpers\": "); append_compiler_runtime_helpers_json_ex(buf, NULL, false);
  zbuf_append(buf, ",\n  \"genericSpecializations\": "); append_direct_generic_specializations_json(buf, program);
  zbuf_append(buf, ",\n  \"runtimeShims\": "); append_runtime_shims_json_ex(buf, NULL, caps, program_uses_bounds_checked_access(program));
}

static void append_size_report_back_json(ZBuf *buf, const Command *command, SourceInput *input, const Program *program, const ZTargetInfo *target, const IrProgram *ir, const HelperUseSummary *used_helpers, const CapabilitySummary *caps, const GraphSizeSource *graph_source, const char *artifact_path, long long artifact_bytes) {
  const char *profile = command && command->profile ? command->profile : "release";
  const char *graph_hash = graph_source && graph_source->graph ? graph_source->graph->graph_hash : (graph_source && graph_source->artifact_source ? graph_source->artifact_source->graph_hash : NULL);
  const char *graph_artifact = graph_source && graph_source->artifact ? graph_source->artifact : (graph_source && graph_source->artifact_source ? graph_source->artifact_source->artifact : NULL);
  const char *graph_lowering = graph_source && graph_source->lowering ? graph_source->lowering : (graph_source && graph_source->artifact_source ? graph_source->artifact_source->lowering : NULL);
  zbuf_appendf(buf, ",\n  \"sections\": [{\"name\":\"lowered-ir\",\"kind\":\"ir\",\"bytes\":%zu}, {\"name\":\"%s-size-metadata\",\"kind\":\"metadata\",\"bytes\":0}", input ? input->lowered_ir_bytes : 0, size_report_uses_llvm_backend(command) ? "llvm" : "direct");
  if (artifact_bytes >= 0) zbuf_appendf(buf, ", {\"name\":\"artifact\",\"kind\":\"metadata\",\"bytes\":%lld}", artifact_bytes);
  zbuf_append(buf, "],\n  \"topLargestEmittedHelpers\": "); append_top_emitted_helpers_json(buf, used_helpers);
  zbuf_append(buf, ",\n  \"backendProfile\": "); append_backend_profile_json(buf, command, input, target, ir, used_helpers, caps);
  zbuf_append(buf, ",\n  \"backendComparison\": "); append_backend_comparison_json(buf, command, target);
  zbuf_append(buf, ",\n  \"sizeBreakdown\": "); append_size_breakdown_json(buf, input, program, target, command, used_helpers, caps, artifact_bytes);
  zbuf_append(buf, ",\n  \"retentionReasons\": "); append_retention_reasons_json(buf, input, program, used_helpers, profile);
  zbuf_append(buf, ",\n  \"optimizationHints\": "); append_optimization_hints_json(buf, input, used_helpers, caps, profile);
  zbuf_append(buf, ",\n  \"profileBudget\": "); append_profile_budget_json(buf, profile);
  zbuf_appendf(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"loweredIrBytes\": %zu,\n  \"artifactPath\": ", input ? input->lowered_ir_bytes : 0);
  append_json_nullable_string(buf, artifact_path);
  zbuf_append(buf, ",\n  \"artifactBytes\": ");
  if (artifact_bytes >= 0) zbuf_appendf(buf, "%lld", artifact_bytes);
  else zbuf_append(buf, "null");
  zbuf_append(buf, ",\n  \"compilerPhases\": "); append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": "); append_compiler_caches_json_ex(buf, input, target, profile, graph_hash && graph_hash[0] ? "program-graph" : NULL, graph_hash);
  zbuf_append(buf, ",\n  \"incrementalInvalidation\": "); append_incremental_invalidations_json_ex(buf, input, target, profile, graph_artifact, graph_hash, graph_lowering);
  zbuf_append(buf, ",\n  \"profileSemantics\": "); append_profile_semantics_json(buf, profile);
  zbuf_append(buf, ",\n  \"safetyFacts\": "); append_safety_facts_json(buf, profile);
  zbuf_append(buf, ",\n  \"profileCatalog\": "); append_profile_catalog_json(buf);
  zbuf_append(buf, ",\n  \"objectBackend\": ");
  if (size_report_uses_llvm_backend(command)) z_append_llvm_native_backend_json(buf, input, target, "exe");
  else append_object_backend_json(buf, input, target, command, "size");
  zbuf_append(buf, "\n}\n");
}

static int run_size_report_command(const Command *command, SourceInput *input, Program *program, const ZTargetInfo *target, IrProgram *ir, const GraphSizeSource *graph_source, ZDiag *diag) {
  ZProgramGraphReportLoad size_graph = {0};
  const char *graph_artifact = graph_source ? (graph_source->artifact ? graph_source->artifact : (graph_source->artifact_source ? graph_source->artifact_source->artifact : NULL)) : NULL;
  const ZProgramGraph *report_graph = graph_source && graph_source->graph ? graph_source->graph : z_program_graph_report_load_source(graph_artifact, command && command->repository_graph_input, &size_graph);
  CapabilitySummary caps = program_or_ir_capabilities(program, ir);
  if (report_graph) { CapabilitySummary graph_caps = z_program_graph_report_capabilities(report_graph); z_capability_summary_merge(&caps, &graph_caps); }
  HelperUseSummary used_helpers = program_used_helpers(program);
  if (report_graph) z_program_graph_report_mark_used_std_helpers(report_graph, used_helpers.used, used_helpers.len);
  long long artifact_bytes = -1;
  char *artifact_path = NULL;
  if (!write_size_metadata_artifact(command, input, target, &artifact_path, &artifact_bytes, diag)) {
    const char *path = diag && diag->path ? diag->path : (command && command->out ? command->out : artifact_path);
    if (command && command->json) print_diag_json(path, diag);
    else print_diag(path, diag);
    free(artifact_path); z_program_graph_report_load_free(&size_graph); helper_summary_free(&used_helpers); return 1;
  }

  ZBuf json;
  zbuf_init(&json);
  append_size_report_front_json(&json, command, input, program, target, ir, &caps, &used_helpers, graph_source);
  append_size_report_back_json(&json, command, input, program, target, ir, &used_helpers, &caps, graph_source, artifact_path, artifact_bytes);
  fputs(json.data, stdout);
  zbuf_free(&json);
  free(artifact_path); z_program_graph_report_load_free(&size_graph); helper_summary_free(&used_helpers);
  return 0;
}

static size_t count_text_occurrences(const char *text, const char *needle) {
  size_t count = 0;
  const char *cursor = text ? text : "";
  while ((cursor = strstr(cursor, needle)) != NULL) {
    count++;
    cursor += strlen(needle);
  }
  return count;
}

static const char *skip_c_space(const char *cursor) {
  while (cursor && *cursor && isspace((unsigned char)*cursor)) cursor++;
  return cursor;
}

static bool c_ident_char(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
}

static char *trim_span_copy(const char *start, const char *end) {
  if (!start || !end || end < start) return z_strdup("");
  while (start < end && isspace((unsigned char)*start)) start++;
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

static const char *last_ident_start_before(const char *start, const char *end) {
  const char *cursor = end;
  while (cursor > start && !c_ident_char(cursor[-1])) cursor--;
  const char *ident_end = cursor;
  while (cursor > start && c_ident_char(cursor[-1])) cursor--;
  return ident_end > cursor ? cursor : NULL;
}

static void append_c_import_function_model_json(ZBuf *buf, const ZCImportFunction *function) {
  zbuf_append(buf, "{\"name\":");
  append_json_string(buf, function ? function->name : "");
  zbuf_append(buf, ",\"returnType\":");
  append_json_string(buf, function ? function->return_c_type : "");
  zbuf_append(buf, ",\"params\":");
  zbuf_append(buf, "[");
  for (size_t i = 0; function && i < function->param_len; i++) {
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, function->params[i].name);
    zbuf_append(buf, ",\"type\":");
    append_json_string(buf, function->params[i].c_type);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]");
  zbuf_append(buf, "}");
}

static void append_c_header_functions_json(ZBuf *buf, const char *header, const ZTargetInfo *target) {
  ZCImportFunctionVec functions = {0};
  z_c_header_parse_functions_for_target(header, target, &functions);
  zbuf_append(buf, "[");
  for (size_t i = 0; i < functions.len; i++) {
    if (i > 0) zbuf_append(buf, ",");
    append_c_import_function_model_json(buf, &functions.items[i]);
  }
  zbuf_append(buf, "]");
  z_c_import_function_vec_free(&functions);
}

static void append_c_header_constants_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "#define")) != NULL) {
    cursor += strlen("#define");
    cursor = skip_c_space(cursor);
    const char *name_start = cursor;
    while (c_ident_char(*cursor)) cursor++;
    const char *name_end = cursor;
    const char *line_end = strchr(cursor, '\n');
    if (!line_end) line_end = cursor + strlen(cursor);
    char *name = z_strndup(name_start, (size_t)(name_end - name_start));
    char *value = trim_span_copy(cursor, line_end);
    if (name[0]) {
      if (wrote) zbuf_append(buf, ",");
      wrote = true;
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, name);
      zbuf_append(buf, ",\"value\":");
      append_json_string(buf, value);
      zbuf_append(buf, ",\"type\":\"int\"}");
    }
    free(name);
    free(value);
  }
  zbuf_append(buf, "]");
}

static void append_c_header_typedefs_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "typedef ")) != NULL) {
    const char *line_end = strchr(cursor, ';');
    if (!line_end) break;
    char *line = trim_span_copy(cursor + strlen("typedef "), line_end);
    if (strncmp(line, "struct ", 7) != 0) {
      const char *line_text_end = line + strlen(line);
      const char *name_start = last_ident_start_before(line, line_text_end);
      if (name_start) {
        const char *name_end = name_start;
        while (*name_end && c_ident_char(*name_end)) name_end++;
        char *name = z_strndup(name_start, (size_t)(name_end - name_start));
        char *target = trim_span_copy(line, name_start);
        if (wrote) zbuf_append(buf, ",");
        wrote = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, name);
        zbuf_append(buf, ",\"target\":");
        append_json_string(buf, target);
        zbuf_append(buf, "}");
        free(name);
        free(target);
      }
    }
    free(line);
    cursor = line_end + 1;
  }
  zbuf_append(buf, "]");
}

static void append_c_struct_fields_json(ZBuf *buf, const char *start, const char *end) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = start;
  while (cursor && cursor < end) {
    const char *semi = cursor;
    while (semi < end && *semi != ';') semi++;
    char *field = trim_span_copy(cursor, semi);
    if (field[0]) {
      const char *field_end = field + strlen(field);
      const char *name_start = last_ident_start_before(field, field_end);
      if (name_start) {
        const char *name_end = name_start;
        while (*name_end && c_ident_char(*name_end)) name_end++;
        char *name = z_strndup(name_start, (size_t)(name_end - name_start));
        char *type = trim_span_copy(field, name_start);
        if (wrote) zbuf_append(buf, ",");
        wrote = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, name);
        zbuf_append(buf, ",\"type\":");
        append_json_string(buf, type);
        zbuf_append(buf, "}");
        free(name);
        free(type);
      }
    }
    free(field);
    cursor = semi < end ? semi + 1 : end;
  }
  zbuf_append(buf, "]");
}

static void append_c_header_structs_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "typedef struct")) != NULL) {
    const char *tag_start = skip_c_space(cursor + strlen("typedef struct"));
    const char *tag_end = tag_start;
    while (c_ident_char(*tag_end)) tag_end++;
    const char *open = strchr(tag_end, '{');
    const char *close = open ? strchr(open, '}') : NULL;
    const char *semi = close ? strchr(close, ';') : NULL;
    if (!open || !close || !semi) break;
    char *tag = trim_span_copy(tag_start, tag_end);
    char *alias = trim_span_copy(close + 1, semi);
    if (wrote) zbuf_append(buf, ",");
    wrote = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, alias[0] ? alias : tag);
    zbuf_append(buf, ",\"tag\":");
    append_json_string(buf, tag);
    zbuf_append(buf, ",\"fields\":");
    append_c_struct_fields_json(buf, open + 1, close);
    zbuf_append(buf, "}");
    free(tag);
    free(alias);
    cursor = semi + 1;
  }
  zbuf_append(buf, "]");
}

static void append_c_enum_cases_json(ZBuf *buf, const char *start, const char *end) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = start;
  while (cursor && cursor < end) {
    const char *comma = cursor;
    while (comma < end && *comma != ',') comma++;
    char *item = trim_span_copy(cursor, comma);
    if (item[0]) {
      char *eq = strchr(item, '=');
      char *name = eq ? trim_span_copy(item, eq) : trim_span_copy(item, item + strlen(item));
      char *value = eq ? trim_span_copy(eq + 1, item + strlen(item)) : z_strdup("");
      if (name[0]) {
        if (wrote) zbuf_append(buf, ",");
        wrote = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, name);
        zbuf_append(buf, ",\"value\":");
        append_json_string(buf, value);
        zbuf_append(buf, "}");
      }
      free(name);
      free(value);
    }
    free(item);
    cursor = comma < end ? comma + 1 : end;
  }
  zbuf_append(buf, "]");
}

static void append_c_header_enums_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "enum ")) != NULL) {
    const char *name_start = skip_c_space(cursor + strlen("enum "));
    const char *name_end = name_start;
    while (c_ident_char(*name_end)) name_end++;
    const char *open = strchr(name_end, '{');
    const char *close = open ? strchr(open, '}') : NULL;
    if (!open || !close) break;
    char *name = trim_span_copy(name_start, name_end);
    if (wrote) zbuf_append(buf, ",");
    wrote = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, name);
    zbuf_append(buf, ",\"backing\":\"int\",\"cases\":");
    append_c_enum_cases_json(buf, open + 1, close);
    zbuf_append(buf, "}");
    free(name);
    cursor = close + 1;
  }
  zbuf_append(buf, "]");
}

static void append_c_header_model_json(ZBuf *buf, const char *header, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"functions\":");
  append_c_header_functions_json(buf, header, target);
  zbuf_append(buf, ",\"constants\":");
  append_c_header_constants_json(buf, header);
  zbuf_append(buf, ",\"structs\":");
  append_c_header_structs_json(buf, header);
  zbuf_append(buf, ",\"enums\":");
  append_c_header_enums_json(buf, header);
  zbuf_append(buf, ",\"typedefs\":");
  append_c_header_typedefs_json(buf, header);
  zbuf_append(buf, "}");
}

static void append_c_imports_json(ZBuf *buf, const Program *program, const ZTargetInfo *target) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->c_imports.len; i++) {
    const CImport *import = &program->c_imports.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    ZDiag read_diag = {0};
    const char *read_path = import->resolved_header && import->resolved_header[0] ? import->resolved_header : import->header;
    char *header = z_read_file(read_path, &read_diag);
    uint64_t header_hash = fnv1a_text(header ? header : "");
    zbuf_append(buf, "{\"header\":");
    append_json_string(buf, import->header);
    zbuf_append(buf, ",\"alias\":");
    append_json_string(buf, import->alias);
    zbuf_appendf(buf, ",\"cacheKey\":\"%016llx-%s\"", (unsigned long long)header_hash, target && target->zig_target ? target->zig_target : "host");
    zbuf_append(buf, ",\"cache\":{\"headerHash\":\"");
    zbuf_appendf(buf, "%016llx", (unsigned long long)header_hash);
    zbuf_append(buf, "\",\"target\":");
    append_json_string(buf, target && target->name ? target->name : z_host_target());
    zbuf_append(buf, ",\"abi\":");
    append_json_string(buf, target && target->abi ? target->abi : "host");
    zbuf_appendf(buf, ",\"flagsHash\":\"%016llx\",\"sysrootFingerprint\":\"%016llx\"}", (unsigned long long)fnv1a_text(""), (unsigned long long)fnv1a_text(target && z_target_requires_sysroot(target) ? z_target_sysroot_env_name(target) : "not-required"));
    zbuf_append(buf, ",\"imports\":{\"functions\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, ");"));
    zbuf_append(buf, ",\"constants\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, "#define"));
    zbuf_append(buf, ",\"structs\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, "struct "));
    zbuf_append(buf, ",\"enums\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, "enum "));
    zbuf_append(buf, ",\"typedefs\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, "typedef "));
    zbuf_append(buf, "},\"typedModel\":");
    append_c_header_model_json(buf, header, target);
    zbuf_appendf(buf, ",\"target\":\"%s\"}", target ? target->name : "host");
    free(header);
  }
  zbuf_append(buf, "]");
}

static int run_abi_command(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target) {
  const char *mode = command && command->kind ? command->kind : "check";
  if (strcmp(mode, "dump") == 0) {
    if (command && command->json) {
      ZProgramGraphReportLoad abi_graph_load = {0};
      const ZProgramGraph *abi_graph = NULL;
      if (z_program_graph_artifact_source_present(&command->graph_source)) {
        abi_graph = z_program_graph_report_load_source(command->graph_source.artifact, command->repository_graph_input, &abi_graph_load);
      }
      ZBuf abi;
      zbuf_init(&abi);
      z_append_abi_dump_json(&abi, input, program, target, abi_graph, append_c_imports_json);
      fputs(abi.data, stdout);
      zbuf_free(&abi);
      z_program_graph_report_load_free(&abi_graph_load);
    } else {
      printf("abi dump ok\n");
    }
    return 0;
  }
  if (strcmp(mode, "check") == 0) {
    if (command && command->json) {
      printf("{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"sourceFile\": ");
      print_json_string(input ? input->source_file : "");
      printf(",\n  \"target\": ");
      print_json_string(target ? target->name : "host");
      printf(",\n  \"diagnostics\": []\n}\n");
    } else {
      printf("abi ok\n");
    }
    return 0;
  }
  fprintf(stderr, "unknown abi mode: %s\n", mode);
  return 1;
}

static bool json_array_has_entries(const char *json) {
  return json && strcmp(json, "[]") != 0 && strchr(json, '"') != NULL;
}

static bool c_lib_has_host_paths(const ZManifestCLib *lib) {
  const char *include_json = lib && lib->include_json ? lib->include_json : "";
  const char *lib_json = lib && lib->lib_json ? lib->lib_json : "";
  return strstr(include_json, "/usr/include") || strstr(include_json, "/usr/local/include") ||
         strstr(lib_json, "/usr/lib") || strstr(lib_json, "/usr/local/lib");
}

static bool c_lib_has_vendored_headers(const ZManifestCLib *lib) {
  return lib && json_array_has_entries(lib->headers_json) && json_array_has_entries(lib->include_json) && !c_lib_has_host_paths(lib);
}

static bool c_lib_has_vendored_libraries(const ZManifestCLib *lib) {
  return lib && json_array_has_entries(lib->lib_json) && !c_lib_has_host_paths(lib);
}

static bool c_lib_uses_unsafe_foreign_discovery(const ZManifestCLib *lib, const ZTargetInfo *target) {
  if (!target || z_target_is_host(target)) return false;
  if (c_lib_has_host_paths(lib)) return true;
  bool uses_pkg_config = lib && lib->pkg_config && lib->pkg_config[0];
  return uses_pkg_config && (!c_lib_has_vendored_headers(lib) || !c_lib_has_vendored_libraries(lib));
}

static bool c_import_link_plan_required(const SourceInput *input, const Command *command) {
  return input && input->direct_c_import_call_count > 0 &&
         (!command || command->emit == EMIT_EXE);
}

static void set_c_import_link_plan_diag(ZDiag *diag, const char *manifest_path, const char *message, const char *actual) {
  if (!diag) return;
  diag->code = 8005;
  diag->path = z_strdup(manifest_path ? manifest_path : "zero.toml or zero.json");
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "extern C link plan is not configured");
  snprintf(diag->expected, sizeof(diag->expected), "matching c.libs.*.headers with package-relative lib entries or safe link names");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "missing C link metadata");
  snprintf(diag->help, sizeof(diag->help), "add the imported header to c.libs.*.headers, then add vendored object/archive paths under lib or safe system names under link");
}

static void manifest_path_for_input(const SourceInput *input, char *manifest_path, size_t manifest_path_len) {
  if (!manifest_path || manifest_path_len == 0) return;
  manifest_path[0] = 0;
  if (input && input->manifest_path && input->manifest_path[0]) {
    snprintf(manifest_path, manifest_path_len, "%s", input->manifest_path);
    return;
  }
  const char *src = input && input->source_file ? input->source_file : "";
  const char *src_dir = strstr(src, "/src/");
  if (src_dir) {
    char *root = z_strndup(src, (size_t)(src_dir - src));
    char *found = z_manifest_path_for_root(root);
    snprintf(manifest_path, manifest_path_len, "%s", found ? found : "");
    free(found);
    free(root);
    return;
  }
  char *found = z_manifest_path_for_root(".");
  snprintf(manifest_path, manifest_path_len, "%s", found ? found : "");
  free(found);
}

static void append_c_libraries_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target) {
  char manifest_path[512];
  manifest_path_for_input(input, manifest_path, sizeof(manifest_path));
  ZDiag read_diag = {0};
  char *manifest = z_read_file(manifest_path, &read_diag);
  if (!manifest) {
    zbuf_append(buf, "[]");
    return;
  }
  ZManifest parsed_manifest = {0};
  if (!z_parse_manifest_json(manifest, &parsed_manifest, &read_diag)) {
    free(manifest);
    zbuf_append(buf, "[]");
    return;
  }
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; i < parsed_manifest.c_lib_count; i++) {
    ZManifestCLib *lib = &parsed_manifest.c_libs[i];
    if (wrote) zbuf_append(buf, ", ");
    wrote = true;
    bool host_leakage = c_lib_has_host_paths(lib);
    bool vendored_headers = c_lib_has_vendored_headers(lib);
    bool vendored_libraries = c_lib_has_vendored_libraries(lib);
    bool pkg_unsafe = lib->pkg_config && lib->pkg_config[0] && !z_target_is_host(target) && (!vendored_headers || !vendored_libraries);
    bool implicit_discovery = c_lib_uses_unsafe_foreign_discovery(lib, target);
    bool windows_import_ready = !(target && target->abi && strcmp(target->abi, "msvc") == 0) || strstr(lib->lib_json ? lib->lib_json : "", ".lib") != NULL;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, lib->name);
    zbuf_append(buf, ",\"headers\":");
    zbuf_append(buf, lib->headers_json ? lib->headers_json : "[]");
    zbuf_append(buf, ",\"includePaths\":");
    zbuf_append(buf, lib->include_json ? lib->include_json : "[]");
    zbuf_append(buf, ",\"libraryPaths\":");
    zbuf_append(buf, lib->lib_json ? lib->lib_json : "[]");
    zbuf_append(buf, ",\"link\":");
    zbuf_append(buf, lib->link_json ? lib->link_json : "[]");
    zbuf_append(buf, ",\"linkMode\":");
    append_json_string(buf, lib->mode && strstr(lib->mode, "dynamic") ? "dynamic" : "static");
    zbuf_append(buf, ",\"pkgConfig\":");
    append_json_string(buf, lib->pkg_config ? lib->pkg_config : "");
    zbuf_append(buf, ",\"linkPlan\":{\"target\":");
    append_json_string(buf, target && target->name ? target->name : z_host_target());
    zbuf_append(buf, ",\"sysrootEnv\":");
    append_json_string(buf, target && z_target_requires_sysroot(target) ? z_target_sysroot_env_name(target) : "");
    zbuf_append(buf, ",\"includePaths\":");
    zbuf_append(buf, lib->include_json ? lib->include_json : "[]");
    zbuf_append(buf, ",\"libraryPaths\":");
    zbuf_append(buf, lib->lib_json ? lib->lib_json : "[]");
    zbuf_append(buf, ",\"hostDiscovery\":");
    append_json_string(buf, implicit_discovery ? "blocked" : "none");
    zbuf_appendf(buf, "},\"targetValidation\":{\"pkgConfigTargetSafe\":%s,\"vendoredHeaders\":%s,\"vendoredLibraries\":%s,\"windowsImportLibraries\":%s,\"hostHeaderLeakage\":%s,\"implicitHostDiscovery\":%s,\"status\":",
                 pkg_unsafe ? "false" : "true",
                 vendored_headers ? "true" : "false",
                 vendored_libraries ? "true" : "false",
                 windows_import_ready ? "true" : "false",
                 host_leakage ? "true" : "false",
                 implicit_discovery ? "true" : "false");
    append_json_string(buf, implicit_discovery ? "blocked" : "ok");
    zbuf_append(buf, "}}");
  }
  zbuf_append(buf, "]");
  z_free_manifest(&parsed_manifest);
  free(manifest);
}

static bool validate_c_libraries_for_target(const SourceInput *input, const ZTargetInfo *target, const Command *command, ZDiag *diag) {
  bool require_link_plan = c_import_link_plan_required(input, command);
  bool ok = true;
  char manifest_path[512];
  manifest_path_for_input(input, manifest_path, sizeof(manifest_path));
  ZDiag read_diag = {0}; char *manifest = z_read_file(manifest_path, &read_diag);
  free((char *)read_diag.path); read_diag.path = NULL;
  if (!manifest && !require_link_plan) return true;
  if (!manifest) {
    set_c_import_link_plan_diag(diag, manifest_path, "extern C calls require C link metadata", "zero.toml or zero.json was not found");
    return false;
  }
  ZManifest parsed_manifest = {0};
  if (!z_parse_manifest_json(manifest, &parsed_manifest, &read_diag)) { free((char *)read_diag.path); free(manifest); return true; }
  free((char *)read_diag.path); read_diag.path = NULL;
  bool *direct_import_has_link_inputs = NULL;
  if (require_link_plan) direct_import_has_link_inputs = z_checked_calloc(input->direct_c_import_header_count ? input->direct_c_import_header_count : 1, sizeof(bool));
  if (require_link_plan && input->direct_c_import_header_count == 0) { set_c_import_link_plan_diag(diag, manifest_path, "extern C calls require C link metadata", "no direct C import headers were recorded"); ok = false; goto done; }
  for (size_t i = 0; i < parsed_manifest.c_lib_count; i++) {
    ZManifestCLib *lib = &parsed_manifest.c_libs[i];
    if (c_lib_uses_unsafe_foreign_discovery(lib, target)) {
      diag->code = 8003; diag->path = z_strdup(manifest_path);
      diag->line = diag->column = diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "foreign target C dependency would use host discovery");
      snprintf(diag->expected, sizeof(diag->expected), "package-relative vendored headers/libraries or target sysroot");
      snprintf(diag->actual, sizeof(diag->actual), "c.libs.%s uses %s%s", lib->name ? lib->name : "<unknown>", c_lib_has_host_paths(lib) ? "host include/lib paths" : "", (lib->pkg_config && lib->pkg_config[0]) ? " pkg-config" : "");
      snprintf(diag->help, sizeof(diag->help), "add target-tagged vendored include/lib entries or set the target sysroot; host /usr paths and host pkg-config are not reused for foreign targets");
      ok = false; goto done;
    }
    bool lib_has_link_inputs = false;
    const char *lib_cursor = lib->lib_json ? lib->lib_json : ""; char lib_item[512];
    while (json_array_next_string(&lib_cursor, lib_item, sizeof(lib_item))) {
      lib_has_link_inputs = true;
      if (!c_link_path_is_safe(lib_item)) {
        char actual[512];
        snprintf(actual, sizeof(actual), "c.libs.%s.lib contains an empty or control-character path", lib->name ? lib->name : "<unknown>");
        set_c_import_link_plan_diag(diag, manifest_path, "extern C library path is unsafe", actual);
        ok = false; goto done;
      }
    }
    const char *link_cursor = lib->link_json ? lib->link_json : ""; char link_item[256];
    while (json_array_next_string(&link_cursor, link_item, sizeof(link_item))) {
      lib_has_link_inputs = true;
      if (!c_link_name_is_safe(link_item)) {
        char actual[512];
        snprintf(actual, sizeof(actual), "c.libs.%s.link contains unsafe library name '%s'", lib->name ? lib->name : "<unknown>", link_item);
        set_c_import_link_plan_diag(diag, manifest_path, "extern C system library name is unsafe", actual);
        ok = false; goto done;
      }
    }
    if (require_link_plan && c_lib_matches_direct_c_imports(input, lib)) {
      if (!lib_has_link_inputs) continue;
      for (size_t header_index = 0; header_index < input->direct_c_import_header_count; header_index++) {
        SourceInput one_header = *input;
        one_header.direct_c_import_header_count = 1; one_header.direct_c_import_headers = &input->direct_c_import_headers[header_index];
        one_header.direct_c_import_resolved_headers = &input->direct_c_import_resolved_headers[header_index];
        if (!direct_import_has_link_inputs[header_index] && c_lib_matches_direct_c_imports(&one_header, lib)) direct_import_has_link_inputs[header_index] = true;
      }
    }
  }
  if (require_link_plan) {
    for (size_t header_index = 0; header_index < input->direct_c_import_header_count; header_index++) {
      if (direct_import_has_link_inputs[header_index]) continue;
      char actual[128];
      const char *header = input->direct_c_import_headers[header_index] && input->direct_c_import_headers[header_index][0] ? input->direct_c_import_headers[header_index] : (input->direct_c_import_resolved_headers[header_index] ? input->direct_c_import_resolved_headers[header_index] : "<unknown>");
      snprintf(actual, sizeof(actual), "no lib/link inputs for imported header %.72s", header);
      set_c_import_link_plan_diag(diag, manifest_path, "extern C calls require C link metadata", actual);
      ok = false; goto done;
    }
  }
done:
  free(direct_import_has_link_inputs);
  z_free_manifest(&parsed_manifest);
  free(manifest);
  return ok;
}

static bool validate_package_dependencies_for_target(const SourceInput *input, const ZTargetInfo *target, ZDiag *diag) {
  for (size_t i = 0; input && i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    if (package_dependency_target_compatible(dep, target)) continue;
    diag->code = 9004;
    diag->path = input->manifest_path ? input->manifest_path : input->source_file;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "package dependency is not compatible with target");
    snprintf(diag->expected, sizeof(diag->expected), "dependency target list including %s", target ? target->name : z_host_target());
    snprintf(diag->actual, sizeof(diag->actual), "%s targets %s", dep->name ? dep->name : "<dependency>", dep->targets_json ? dep->targets_json : "[]");
    snprintf(diag->help, sizeof(diag->help), "choose a supported target or gate the dependency behind target-specific package metadata");
    return false;
  }
  return true;
}

static bool self_host_caps_allowed(const CapabilitySummary *caps) {
  if (!caps) return true;
  return !caps->web;
}

static bool self_host_subset_compatible(const Program *program, const CapabilitySummary *caps) {
  bool caps_allowed = self_host_caps_allowed(caps);
  (void)program;
  return caps_allowed;
}

static void append_self_host_subset_json(ZBuf *buf, const Program *program, const CapabilitySummary *caps, const ZTargetInfo *target) {
  bool compatible = self_host_subset_compatible(program, caps);
  zbuf_append(buf, "{\"contractVersion\":1,\"stage\":\"native-bootstrap\"");
  zbuf_appendf(buf, ",\"compatible\":%s", compatible ? "true" : "false");
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target && target->name ? target->name : "host");
  zbuf_append(buf, ",\"backend\":\"native-direct\"");
  zbuf_append(buf, ",\"selectedTarget\":");
  append_json_string(buf, target && target->name ? target->name : "host");
  zbuf_append(buf, ",\"allowedCapabilities\":[\"memory\",\"alloc\",\"path\",\"codec\",\"parse\",\"args\",\"env\",\"fs\",\"time\",\"rand\",\"net\",\"proc\",\"World.stdout\",\"World.stderr\"]");
  zbuf_appendf(buf, ",\"cInterop\":{\"headerImports\":%s,\"typedBindings\":%s,\"generatedCRequired\":false,\"externalCallBoundary\":\"direct-abi-audit\"}", program && program->c_imports.len > 0 ? "true" : "false", program && program->c_imports.len > 0 ? "true" : "false");
  zbuf_append(buf, ",\"blockedReasons\":[");
  bool wrote = false;
#define APPEND_BLOCKED_CAP(field, label) do { \
    if (caps && caps->field) { \
      if (wrote) zbuf_append(buf, ","); \
      append_json_string(buf, label); \
      wrote = true; \
    } \
  } while (0)
  APPEND_BLOCKED_CAP(web, "web");
#undef APPEND_BLOCKED_CAP
  zbuf_append(buf, "],\"sourceForms\":[\"package-local-modules\",\"functions\",\"while\",\"if\",\"return\",\"fixed-arrays\",\"primitive-locals\",\"shape\",\"enum\",\"minimal-choice\",\"strings\",\"byte-spans\",\"fallibility\",\"explicit-alloc\"]");
  zbuf_append(buf, ",\"featureFacts\":{");
  zbuf_append(buf, "\"strings\":{\"status\":\"native-direct-lowered\",\"representation\":\"readonly-data-ptr-len\",\"indexing\":\"bounds-checked-u8\",\"slicing\":\"bounds-checked-byte-view\",\"dataSegments\":true}");
  zbuf_append(buf, ",\"spans\":{\"status\":\"native-direct-lowered\",\"readonlyRepresentation\":\"ptr-len\",\"mutableRepresentation\":\"ptr-len-mutspan-u8\",\"helpers\":[\"std.mem.len\",\"std.mem.eqlBytes\",\"std.mem.copy\",\"std.mem.fill\"]}");
  zbuf_append(buf, ",\"aggregates\":{\"shape\":\"staged\",\"enum\":\"staged\",\"choice\":\"staged\",\"directLowering\":false}");
  zbuf_append(buf, ",\"fallibility\":{\"status\":\"staged\",\"directLowering\":false}");
  zbuf_append(buf, ",\"containers\":{\"status\":\"staged-explicit-storage\",\"directLowering\":false}");
  zbuf_append(buf, ",\"generics\":{\"status\":\"staged-concrete-specializations-only\",\"runtimeMetadata\":false}");
  zbuf_append(buf, "},\"targetLimitations\":[\"hosted-runtime-checked-per-mir\",\"external-c-calls-require-target-library-audit\"]}");
}

static void append_self_host_routing_json(ZBuf *buf, const char *command_name, const char *emit_kind, const Program *program, const CapabilitySummary *caps, const ZTargetInfo *target) {
  bool compatible = self_host_subset_compatible(program, caps);
  (void)command_name;
  (void)emit_kind;
  (void)target;
  zbuf_append(buf, "{\"contractVersion\":1,\"subsetCompatible\":");
  zbuf_append(buf, compatible ? "true" : "false");
  zbuf_append(buf, ",\"mode\":\"native-bootstrap\"");
  zbuf_append(buf, ",\"phases\":{\"parse\":\"zero-c\",\"check\":\"zero-c\",\"lower\":\"zero-c\",\"emit\":\"zero-c\"}");
  zbuf_append(buf, ",\"removed\":{\"seedCompiler\":true,\"browserCompiler\":true,\"portableEmitter\":true}");
  zbuf_append(buf, ",\"metadata\":{\"graphJson\":");
  zbuf_append(buf, compatible ? "true" : "false");
  zbuf_append(buf, ",\"sizeJson\":");
  zbuf_append(buf, compatible ? "true" : "false");
  zbuf_append(buf, "},\"cBridge\":{\"required\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"policy\":\"removed\",\"explicitDirectFallback\":\"never-c-bridge\"}}");
}

static void apply_ir_metrics_to_input(SourceInput *input, const IrProgram *ir, const ZTargetInfo *target) {
  if (!input || !ir) return;
  source_input_clear_direct_c_import_headers(input);
  input->lowered_ir_bytes = ir->mir_bytes;
  input->direct_function_count = ir->direct_function_count;
  input->direct_export_count = ir->direct_export_count;
  input->direct_stack_bytes = z_direct_target_stack_bytes(target, ir);
  input->direct_max_frame_bytes = z_direct_target_max_frame_bytes(target, ir);
  input->direct_readonly_data_bytes = ir->direct_readonly_data_bytes;
  input->direct_allocator_helper_count = ir->direct_allocator_helper_count;
  input->direct_buffer_helper_count = ir->direct_buffer_helper_count;
  input->direct_runtime_helper_count = ir->direct_runtime_helper_count;
  input->direct_host_runtime_import_count = ir->direct_host_runtime_import_count;
  input->direct_http_runtime_import_count = ir->direct_http_runtime_import_count;
  input->direct_c_import_call_count = ir->direct_c_import_call_count;
  input->direct_c_import_symbol_count = ir->external_function_len;
  for (size_t i = 0; i < ir->external_function_len; i++) {
    const IrExternalFunction *external = &ir->external_functions[i];
    source_input_add_direct_c_import_header(input, external->import_header, external->import_resolved_header);
  }
}

static void init_lowering_backend_diag(ZDiag *diag, const SourceInput *input, const ZTargetInfo *target, const Command *command, const IrProgram *ir) {
  const char *emit_kind = emit_kind_name(command ? command->emit : EMIT_EXE);
  bool llvm_request = command &&
                      ((command->emit == EMIT_LLVM_IR && z_backend_request_is_llvm(command->backend, emit_kind)) ||
                       command_uses_llvm_native_exe(command, emit_kind));
  bool typed_graph_mir_failure = ir && memcmp(ir->mir_expected, "typed program graph MIR subset", sizeof("typed program graph MIR subset")) == 0;
  memset(diag, 0, sizeof(*diag));
  diag->code = llvm_request || !ir || strcmp(ir->mir_expected, "direct backend MIR contract") != 0 ? 2004 : 4004;
  diag->path = typed_graph_mir_failure && ir->mir_path && ir->mir_path[0] ? ir->mir_path : (input ? input->source_file : NULL);
  diag->line = ir && ir->mir_line > 0 ? ir->mir_line : 1;
  diag->column = ir && ir->mir_column > 0 ? ir->mir_column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s",
           typed_graph_mir_failure && ir->mir_message[0] ? ir->mir_message :
           llvm_request ? "LLVM IR backend cannot lower this MIR program yet" :
           (ir && ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed"));
  snprintf(diag->expected, sizeof(diag->expected), "%s",
           typed_graph_mir_failure && ir->mir_expected[0] ? ir->mir_expected :
           llvm_request ? "LLVM IR scalar, fixed-array, and byte-view MIR subset" : z_direct_backend_expected(target));
  snprintf(diag->actual, sizeof(diag->actual), "%s", ir && ir->mir_actual[0] ? ir->mir_actual : "unsupported construct");
  snprintf(diag->help, sizeof(diag->help), "%s",
           typed_graph_mir_failure && ir->mir_help[0] ? ir->mir_help :
           llvm_request
             ? "use --backend llvm --emit llvm-ir for scalar code, fixed arrays, byte views, readonly strings, and primitive std.mem helpers"
             : z_direct_backend_help(target));
  if (ir) z_diag_set_backend_blocker(diag, &ir->backend_blocker);
  complete_backend_blocker_diag(diag, target, command, emit_kind, "lower");
}

static bool target_readiness_select_emit_target(const Command *command, const SourceInput *input, const ZTargetInfo *target, ZDiag *diag) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  const char *emit_kind = emit_kind_name(emit);
  if (emit == EMIT_LLVM_IR) {
    if (z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind)) return true;
    init_direct_llvm_ir_unavailable_diag(diag, command, target, input ? input->source_file : NULL);
    return false;
  }
  if (z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind)) {
    if (emit == EMIT_EXE) return true;
    z_backend_init_llvm_unavailable_diag(diag, target, emit_kind, input ? input->source_file : NULL);
    return false;
  }
  if (emit == EMIT_OBJ) {
    ZDirectObjectTargetFacts direct_obj = z_direct_object_target_facts(target);
    const char *direct_request = z_backend_direct_request_name(command ? command->backend : NULL);
    if (direct_request && !z_direct_requested_backend_matches(direct_request, direct_obj.backend)) {
      init_direct_backend_request_mismatch_diag(diag, command, input, target, emit_kind);
      return false;
    }
    if (direct_obj.available) return true;
    init_direct_backend_diag(diag, command, input, target, emit_kind, direct_obj.unsupported_reason);
    return false;
  }
  if (emit == EMIT_C) {
    init_direct_backend_diag(diag, command, input, target, emit_kind, "use --emit exe or --emit obj for target readiness");
    return false;
  }
  return true;
}

static bool target_readiness_buildability_check(const Command *command, const ZTargetInfo *target, const IrProgram *ir, ZDiag *diag) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  const char *emit_kind = emit_kind_name(emit);
  if (z_direct_buildability_check(ir, target, emit_kind, diag)) return true;
  complete_backend_blocker_diag(diag, target, command, emit_kind, diag && diag->backend_blocker.present ? diag->backend_blocker.stage : "buildability");
  return false;
}

static bool target_readiness_select_diag(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target, const IrProgram *ir, ZDiag *diag) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  const char *emit_kind = emit_kind_name(emit);
  if (emit == EMIT_LLVM_IR && z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind)) {
    ZBuf scratch;
    bool ok = z_emit_llvm_ir_from_ir(ir, &scratch, diag);
    if (ok) zbuf_free(&scratch);
    return ok;
  }
  if (command_uses_llvm_native_exe(command, emit_kind)) {
    if (!z_llvm_native_executable_ready(target, input ? input->source_file : NULL, diag)) return false;
    ZBuf scratch;
    bool ok = z_emit_llvm_ir_from_ir(ir, &scratch, diag);
    if (ok) zbuf_free(&scratch);
    return ok;
  }
  if (emit == EMIT_OBJ) return target_readiness_buildability_check(command, target, ir, diag);
  if (emit == EMIT_C) {
    init_direct_backend_diag(diag, command, input, target, emit_kind, "use --emit exe or --emit obj for target readiness");
    return false;
  }

  const char *direct_request = z_backend_direct_request_name(command ? command->backend : NULL);
  bool needs_zero_runtime = ir && ir_linked_executable_needs_zero_runtime_object(ir);
  bool needs_linked_executable = ir && ir_needs_linked_executable_object(ir);
  if (needs_linked_executable) {
    ZDirectObjectTargetFacts direct_obj = z_direct_object_target_facts(target); if (direct_request && !z_direct_requested_backend_matches(direct_request, direct_obj.backend)) { init_direct_backend_request_mismatch_diag(diag, command, input, target, emit_kind); return false; }
    if (!target_readiness_buildability_check(command, target, ir, diag)) return false;
    bool needs_http_runtime = ir && ir->direct_http_runtime_import_count > 0;
    ZDirectRuntimeObjectFacts runtime_object = z_direct_runtime_object_facts(target, needs_http_runtime);
    if (needs_zero_runtime && !runtime_object.supported) {
      init_direct_backend_diag(diag, command, input, target, emit_kind, runtime_object.blocker);
      return false;
    }
    return true;
  }
  ZDirectExecutableTargetFacts direct_exe = z_direct_executable_target_facts(target, direct_request);
  CapabilitySummary caps = program_capabilities(program);
  if (direct_request && !direct_exe.request_supported) {
    init_direct_backend_request_mismatch_diag(diag, command, input, target, emit_kind);
    return false;
  }
  bool default_direct_exe = direct_exe.request_supported && !direct_request && self_host_subset_compatible(program, &caps);
  bool requested_direct_exe = direct_exe.request_supported && direct_exe.requested;
  if (default_direct_exe || requested_direct_exe) return target_readiness_buildability_check(command, target, ir, diag);
  init_direct_backend_diag(diag, command, input, target, emit_kind, "direct executable backend is not implemented for this target/backend pair; use --emit obj for direct target objects or choose a supported direct executable target");
  return false;
}

static void append_target_readiness_diagnostic_json(ZBuf *buf, const char *path, const ZDiag *diag) {
  zbuf_append(buf, "{\"severity\":\"error\",\"code\":");
  append_json_string(buf, diag_code(diag->code));
  zbuf_append(buf, ",\"message\":");
  append_json_string(buf, diag->message);
  zbuf_append(buf, ",\"path\":");
  append_json_string(buf, diag->path ? diag->path : path);
  zbuf_appendf(buf, ",\"line\":%d,\"column\":%d,\"length\":%d", diag->line, diag->column, diag->length > 0 ? diag->length : 1);
  zbuf_append(buf, ",\"expected\":");
  append_json_string(buf, diag->expected);
  zbuf_append(buf, ",\"actual\":");
  append_json_string(buf, diag->actual);
  zbuf_append(buf, ",\"help\":");
  append_json_string(buf, diag->help);
  zbuf_append(buf, ",\"fixSafety\":");
  append_json_string(buf, diag_fix_safety(diag->code));
  zbuf_append(buf, ",\"repair\":{\"id\":");
  append_json_string(buf, diag_repair_id(diag->code));
  zbuf_append(buf, ",\"summary\":");
  append_json_string(buf, diag_repair_summary(diag->code));
  zbuf_append(buf, "}");
  if (diag->backend_blocker.present) {
    zbuf_append(buf, ",\"backendBlocker\":");
    append_backend_blocker_json(buf, &diag->backend_blocker);
  }
  zbuf_append(buf, ",\"related\":[]}");
}

static void append_target_readiness_json(ZBuf *buf, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command);

static void append_target_readiness_result_json(ZBuf *buf, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command, const IrProgram *ir, const ZDiag *diag, bool ready) {
  const char *emit_kind = emit_kind_name(command ? command->emit : EMIT_EXE);
  zbuf_append(buf, "{\"schemaVersion\":1,\"ok\":");
  zbuf_append(buf, ready ? "true" : "false");
  zbuf_append(buf, ",\"languageOk\":true,\"buildable\":");
  zbuf_append(buf, ready ? "true" : "false");
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target && target->name ? target->name : z_host_target());
  zbuf_append(buf, ",\"emit\":");
  append_json_string(buf, emit_kind);
  zbuf_append(buf, ",\"objectFormat\":");
  append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_append(buf, ",\"backend\":");
  bool requested_llvm_backend = z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind);
  const char *ready_backend = requested_llvm_backend
    ? "llvm"
    : z_direct_backend_name_for_emit_kind(target, emit_kind, z_backend_direct_request_name(command ? command->backend : NULL));
  append_json_string(buf, ready || !diag || !diag->backend_blocker.present ? ready_backend : diag->backend_blocker.backend);
  if (requested_llvm_backend) {
    z_append_llvm_backend_lifecycle_field_json(buf);
  }
  zbuf_append(buf, ",\"stage\":");
  append_json_string(buf, ready ? "ready" : (diag && diag->backend_blocker.present && diag->backend_blocker.stage[0] ? diag->backend_blocker.stage : "select"));
  zbuf_append(buf, ",\"diagnostics\":[");
  if (!ready && diag) append_target_readiness_diagnostic_json(buf, input ? input->source_file : NULL, diag);
  zbuf_append(buf, "]}");
  (void)program;
  (void)ir;
}

static void append_target_readiness_from_ir_json(ZBuf *buf, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command, const IrProgram *ir) {
  ZDiag diag = {0};
  bool ready = true;
  if (!ir) { append_target_readiness_json(buf, input, program, target, command); return; }
  if (!validate_c_libraries_for_target(input, target, command, &diag)) {
    ready = false;
  }
  if (ready) {
    if (!target_readiness_select_emit_target(command, input, target, &diag)) {
      ready = false;
    } else if (!ir->mir_valid) {
      init_lowering_backend_diag(&diag, input, target, command, ir);
      ready = false;
    } else if (!repository_graph_target_readiness_select_diag(command, input, target, ir, &diag)) {
      ready = false;
    }
  }
  bool mapped = false;
  if (!ready && input) {
    if (diag.code != 8003 && diag.code != 8005) mapped = z_map_source_diag(input, &diag);
    if (!diag.path) diag.path = input->source_file;
  }
  append_target_readiness_result_json(buf, input, program, target, command, ir, &diag, ready);
  /* source mapping replaces the path with a fresh copy it owns */
  if (mapped) free((char *)diag.path);
}

static void append_target_readiness_json(ZBuf *buf, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command) {
  ZDiag diag = {0};
  IrProgram ir = {0};
  bool ready = true;
  long long phase_started = now_ms();
  ir = z_lower_program_with_source(program, input, target);
  if (input) input->lower_ms = now_ms() - phase_started;
  apply_ir_metrics_to_input(input, &ir, target);
  if (!validate_c_libraries_for_target(input, target, command, &diag)) {
    ready = false;
  }
  if (ready) {
    if (!target_readiness_select_emit_target(command, input, target, &diag)) {
      ready = false;
    } else if (!ir.mir_valid) {
      bool graph_ready = false;
      if (command && z_program_graph_artifact_source_present(&command->graph_source)) {
        SourceInput graph_input = {0}; Program graph_program = {0}; IrProgram graph_ir = {0}; ZDiag graph_diag = {0};
        graph_ready =
          z_program_graph_prepare_artifact_mir_input(command->graph_source.artifact, target, emit_kind_name(command->emit), command->backend, &graph_program, &graph_input, &graph_ir, NULL, &graph_diag) &&
          validate_c_libraries_for_target(&graph_input, target, command, &graph_diag) &&
          target_readiness_select_emit_target(command, &graph_input, target, &graph_diag) &&
          graph_ir.mir_valid &&
          repository_graph_target_readiness_select_diag(command, &graph_input, target, &graph_ir, &graph_diag);
        if (graph_ready) { z_free_ir_program(&ir); ir = graph_ir; graph_ir = (IrProgram){0}; apply_ir_metrics_to_input(input, &ir, target); }
        z_free_ir_program(&graph_ir); z_free_program(&graph_program); z_free_source(&graph_input);
      }
      if (!graph_ready) { init_lowering_backend_diag(&diag, input, target, command, &ir); ready = false; }
    } else if (!target_readiness_select_diag(command, input, program, target, &ir, &diag)) {
      ready = false;
    }
  }
  bool mapped = false;
  if (!ready && input) {
    /* C library validation points at the package manifest; source mapping would erase that. */
    if (diag.code != 8003 && diag.code != 8005) mapped = z_map_source_diag(input, &diag);
    if (!diag.path) diag.path = input->source_file;
  }
  append_target_readiness_result_json(buf, input, program, target, command, &ir, &diag, ready);
  /* source mapping replaces the path with a fresh copy it owns */
  if (mapped) free((char *)diag.path);
  z_free_ir_program(&ir);
}
static bool repository_graph_target_readiness_select_diag(const Command *command, const SourceInput *input, const ZTargetInfo *target, const IrProgram *ir, ZDiag *diag) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  const char *emit_kind = emit_kind_name(emit);
  if (emit == EMIT_LLVM_IR && z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind)) {
    ZBuf scratch;
    bool ok = z_emit_llvm_ir_from_ir(ir, &scratch, diag);
    if (ok) zbuf_free(&scratch);
    return ok;
  }
  if (command_uses_llvm_native_exe(command, emit_kind)) {
    if (!z_llvm_native_executable_ready(target, input ? input->source_file : NULL, diag)) return false;
    ZBuf scratch;
    bool ok = z_emit_llvm_ir_from_ir(ir, &scratch, diag);
    if (ok) zbuf_free(&scratch);
    return ok;
  }
  if (emit == EMIT_OBJ) return target_readiness_buildability_check(command, target, ir, diag);
  if (emit == EMIT_C) {
    init_direct_backend_diag(diag, command, input, target, emit_kind, "use --emit exe or --emit obj for target readiness");
    return false;
  }
  const char *direct_request = z_backend_direct_request_name(command ? command->backend : NULL);
  bool needs_zero_runtime = ir && ir_linked_executable_needs_zero_runtime_object(ir);
  bool needs_linked_executable = ir && ir_needs_linked_executable_object(ir);
  if (needs_linked_executable) {
    ZDirectObjectTargetFacts direct_obj = z_direct_object_target_facts(target); if (direct_request && !z_direct_requested_backend_matches(direct_request, direct_obj.backend)) { init_direct_backend_request_mismatch_diag(diag, command, input, target, emit_kind); return false; }
    if (!target_readiness_buildability_check(command, target, ir, diag)) return false;
    bool needs_http_runtime = ir && ir->direct_http_runtime_import_count > 0;
    ZDirectRuntimeObjectFacts runtime_object = z_direct_runtime_object_facts(target, needs_http_runtime);
    if (needs_zero_runtime && !runtime_object.supported) {
      init_direct_backend_diag(diag, command, input, target, emit_kind, runtime_object.blocker);
      return false;
    }
    return true;
  }

  ZDirectExecutableTargetFacts direct_exe = z_direct_executable_target_facts(target, direct_request);
  if (direct_request && !direct_exe.request_supported) {
    init_direct_backend_request_mismatch_diag(diag, command, input, target, emit_kind);
    return false;
  }
  if (direct_exe.request_supported) return target_readiness_buildability_check(command, target, ir, diag);
  init_direct_backend_diag(diag, command, input, target, emit_kind, "direct executable backend is not implemented for this target/backend pair; use --emit obj for direct target objects or choose a supported direct executable target");
  return false;
}

static bool graph_check_buildability_gate_applies(const ZProgramGraph *graph) {
  if (!z_program_graph_has_direct_entry_function(graph)) return false;
  HttpListenSpec listen = {0};
  ZDiag listen_diag = {0};
  if (find_http_listen_graph_program(graph, &listen, &listen_diag)) return false;
  return z_check_gate_scan_finds_known_construct(graph);
}

static bool check_gate_buildability_blocked(const Command *command, const SourceInput *input, const ZTargetInfo *target, const ZProgramGraph *graph, const IrProgram *ir, ZDiag *out_diag) {
  if (!ir || !graph || ir->mir_valid || !graph_check_buildability_gate_applies(graph)) return false;
  ZDiag diag = {0};
  init_lowering_backend_diag(&diag, input, target, command, ir);
  if (!z_check_gate_diag_is_buildability_blocker(&diag)) return false;
  if (!z_check_gate_diag_is_known_construct(graph, &diag)) return false;
  if (input) {
    if (diag.code != 8003 && diag.code != 8005) z_map_source_diag(input, &diag);
    if (!diag.path) diag.path = input->source_file;
  }
  /* The diagnostic path may point into the transient MIR program; own it. */
  z_diag_set_path_copy(&diag, diag.path);
  if (out_diag) *out_diag = diag;
  return true;
}

/*
 * Build parity for unmerged graph inputs: graph artifact check and import
 * gate graphs have not merged embedded std modules yet, while build's MIR
 * preparation merges them before lowering. Lower a merged clone so the gate
 * only blocks programs zero build would also reject.
 */
static bool check_gate_merged_graph_blocked(const Command *command, const SourceInput *input, const ZTargetInfo *target, const ZProgramGraph *graph, ZDiag *out_diag) {
  if (!graph || !graph_check_buildability_gate_applies(graph)) return false;
  ZProgramGraph merged = {0};
  if (!z_program_graph_clone(graph, &merged)) return false;
  ZDiag merge_diag = {0};
  bool blocked = false;
  if (z_program_graph_merge_embedded_std_graph_modules(&merged, input, &merge_diag)) {
    IrProgram merged_ir = z_lower_program_graph_with_source(&merged, input, target);
    blocked = check_gate_buildability_blocked(command, input, target, &merged, &merged_ir, out_diag);
    z_free_ir_program(&merged_ir);
  }
  z_program_graph_free(&merged);
  return blocked;
}

/*
 * Plain-mode check gate: reuse the final-MIR cache before paying the stdlib
 * graph merge, then lower once and memoize the result for the next check or
 * build of the same graph hash.
 */
static bool repository_graph_check_gate_blocked(const Command *command, SourceInput *input, ZProgramGraphStore *store, const ZTargetInfo *target, long long *lower_ms_out, ZDiag *diag) {
  if (!store || !store->path) return false;
  const char *emit_kind = emit_kind_name(command ? command->emit : EMIT_EXE);
  const char *requested_backend = command ? command->backend : NULL;
  long long phase_started = now_ms();
  IrProgram ir = {0};
  bool have_ir = false;
  char *cache_path = z_mir_binary_cache_path_for_graph_store(store->path, store->graph.graph_hash, target, emit_kind, requested_backend);
  ZMirBinaryCacheFacts cache_facts = {0};
  if (cache_path && z_mir_binary_load_path(cache_path, store->graph.graph_hash, target, emit_kind, requested_backend, &ir, &cache_facts, NULL)) {
    have_ir = true;
  } else {
    ZDiag merge_diag = {0};
    if (z_program_graph_merge_embedded_std_graph_modules_timed(&store->graph, input, &merge_diag)) {
      ir = z_lower_program_graph_with_source(&store->graph, input, target);
      have_ir = true;
      if (ir.mir_valid && cache_path) {
        ZDiag cache_diag = {0};
        (void)z_mir_binary_write_path(cache_path, &ir, store->graph.graph_hash, target, emit_kind, requested_backend, &cache_diag);
      }
    }
  }
  free(cache_path);
  if (lower_ms_out) *lower_ms_out = now_ms() - phase_started;
  if (input) input->lower_ms = lower_ms_out ? *lower_ms_out : input->lower_ms;
  bool blocked = have_ir && check_gate_buildability_blocked(command, input, target, &store->graph, &ir, diag);
  if (have_ir) z_free_ir_program(&ir);
  return blocked;
}

static void init_repository_graph_missing_store_diag(ZDiag *diag, SourceInput *input) {
  memset(diag, 0, sizeof(*diag));
  diag->code = 2002;
  diag->path = input ? input->source_file : NULL;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "repository graph target readiness requires a graph store");
  snprintf(diag->expected, sizeof(diag->expected), "loaded repository ProgramGraph");
  snprintf(diag->actual, sizeof(diag->actual), "missing graph");
  snprintf(diag->help, sizeof(diag->help), "run zero status to inspect the repository graph store");
}

struct RepositoryGraphCheckReadiness {
  bool ran;
  bool ready;
  bool prepared;
  bool graph_mir_used;
  long long lower_ms;
  ZDiag diag;
  IrProgram ir;
  Program program;
};

static void repository_graph_check_readiness_free(RepositoryGraphCheckReadiness *readiness) {
  if (!readiness) return;
  z_free_ir_program(&readiness->ir);
  z_free_program(&readiness->program);
}

static bool repository_graph_check_readiness_compute(const Command *command, SourceInput *input, const ZProgramGraphStore *store, const ZTargetInfo *target, RepositoryGraphCheckReadiness *out) {
  out->ran = true;
  out->ready = true;
  const char *emit_kind = emit_kind_name(command ? command->emit : EMIT_EXE);

  if (!store || !store->path) {
    out->ready = false;
    init_repository_graph_missing_store_diag(&out->diag, input);
  } else if (!validate_c_libraries_for_target(input, target, command, &out->diag)) {
    out->ready = false;
  } else {
    long long phase_started = now_ms();
    out->prepared = z_program_graph_prepare_repository_store_mir_input(store->path,
                                                                       target,
                                                                       emit_kind,
                                                                       command ? command->backend : NULL,
                                                                       false,
                                                                       true,
                                                                       &out->program,
                                                                       input,
                                                                       &out->ir,
                                                                       NULL,
                                                                       &out->diag);
    out->lower_ms = now_ms() - phase_started;
    out->graph_mir_used = true;
    if (input) input->lower_ms = out->lower_ms;
    if (out->prepared) apply_ir_metrics_to_input(input, &out->ir, target);

    if (!out->prepared) {
      out->ready = false;
    } else if (!target_readiness_select_emit_target(command, input, target, &out->diag)) {
      out->ready = false;
    } else if (!out->ir.mir_valid) {
      init_lowering_backend_diag(&out->diag, input, target, command, &out->ir);
      out->ready = false;
    } else if (!repository_graph_target_readiness_select_diag(command, input, target, &out->ir, &out->diag)) {
      out->ready = false;
    }
  }

  if (!out->ready && input) {
    if (out->diag.code != 8003 && out->diag.code != 8005) z_map_source_diag(input, &out->diag);
    if (!out->diag.path) out->diag.path = input->source_file;
  }
  return out->ready;
}

static bool append_repository_graph_target_readiness_json(ZBuf *buf, SourceInput *input, const ZProgramGraphStore *store, const ZProgramGraphResolutionFacts *resolution, const ZTargetInfo *target, const Command *command, RepositoryGraphCheckReadiness *readiness, long long *lower_ms_out, bool *graph_mir_used_out) {
  (void)resolution;
  if (!readiness->ran) repository_graph_check_readiness_compute(command, input, store, target, readiness);
  bool ready = readiness->ready;
  const ZDiag *diag = &readiness->diag;
  if (lower_ms_out) *lower_ms_out = readiness->lower_ms;
  if (graph_mir_used_out) *graph_mir_used_out = readiness->graph_mir_used;
  const char *emit_kind = emit_kind_name(command ? command->emit : EMIT_EXE);

  zbuf_append(buf, "{\"schemaVersion\":1,\"ok\":");
  zbuf_append(buf, ready ? "true" : "false");
  zbuf_append(buf, ",\"languageOk\":true,\"buildable\":");
  zbuf_append(buf, ready ? "true" : "false");
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target && target->name ? target->name : z_host_target());
  zbuf_append(buf, ",\"emit\":");
  append_json_string(buf, emit_kind);
  zbuf_append(buf, ",\"objectFormat\":");
  append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_append(buf, ",\"backend\":");
  bool requested_llvm_backend = z_backend_request_is_llvm(command ? command->backend : NULL, emit_kind);
  const char *ready_backend = requested_llvm_backend
    ? "llvm"
    : z_direct_backend_name_for_emit_kind(target, emit_kind, z_backend_direct_request_name(command ? command->backend : NULL));
  append_json_string(buf, ready || !diag->backend_blocker.present ? ready_backend : diag->backend_blocker.backend);
  if (requested_llvm_backend) {
    z_append_llvm_backend_lifecycle_field_json(buf);
  }
  zbuf_append(buf, ",\"stage\":");
  append_json_string(buf, ready ? "ready" : (diag->backend_blocker.present && diag->backend_blocker.stage[0] ? diag->backend_blocker.stage : "select"));
  zbuf_append(buf, ",\"diagnostics\":[");
  if (!ready) append_target_readiness_diagnostic_json(buf, input ? input->source_file : NULL, diag);
  zbuf_append(buf, "]}");
  return ready;
}

static void append_release_matrix_target_support_json(ZBuf *buf) {
  const char *targets[] = {"darwin-arm64", "darwin-x64", "linux-arm64", "linux-musl-arm64", "linux-musl-x64", "linux-x64", "win32-arm64.exe", "win32-x64.exe", NULL};
  zbuf_append(buf, "[");
  for (size_t i = 0; targets[i]; i++) {
    if (i > 0) zbuf_append(buf, ",");
    const ZTargetInfo *matrix_target = z_find_target(targets[i]);
    zbuf_append(buf, "{\"target\":");
    append_json_string(buf, targets[i]);
    zbuf_append(buf, ",\"directStatus\":");
    append_json_string(buf, z_direct_backend_status(matrix_target));
    zbuf_append(buf, ",\"directObjectEmitter\":");
    append_json_string(buf, z_direct_object_emitter(matrix_target));
    zbuf_append(buf, ",\"directExeEmitter\":");
    append_json_string(buf, z_direct_exe_emitter(matrix_target));
    zbuf_append(buf, ",\"fallbackPolicy\":\"explicit-direct-never-c-bridge\"}");
  }
  zbuf_append(buf, "]");
}

static size_t source_line_count(const char *source) {
  if (!source || !source[0]) return 0;
  size_t lines = 1;
  for (size_t i = 0; source[i]; i++) {
    if (source[i] == '\n' && source[i + 1]) lines++;
  }
  return lines;
}

static const char *source_path_for_parser_line(const SourceInput *input, int parser_line) {
  if (!input || parser_line <= 0) return NULL;
  size_t index = (size_t)parser_line - 1;
  if (index >= input->source_line_count) return NULL;
  return input->source_line_paths[index];
}

static int source_original_line_for_parser_line(const SourceInput *input, int parser_line) {
  if (!input || parser_line <= 0) return parser_line > 0 ? parser_line : 1;
  size_t index = (size_t)parser_line - 1;
  if (index >= input->source_line_count) return parser_line;
  return input->source_line_numbers[index] > 0 ? input->source_line_numbers[index] : parser_line;
}

static const char *module_name_for_source_path(const SourceInput *input, const char *path) {
  if (!input || !path) return (input && input->module_count == 1) ? input->module_names[0] : "main";
  for (size_t i = 0; i < input->module_count; i++) {
    if (strcmp(input->module_paths[i], path) == 0) return input->module_names[i];
  }
  return input->module_count == 1 ? input->module_names[0] : "main";
}

static bool import_module_is_stdlib(const char *module) {
  return module && strncmp(module, "std.", 4) == 0;
}

static const char *resolved_path_for_import(const SourceInput *input, const char *from, const char *to) {
  if (!input || !from || !to) return NULL;
  for (size_t i = 0; i < input->import_edge_count; i++) {
    if (strcmp(input->import_from[i], from) == 0 && strcmp(input->import_to[i], to) == 0) {
      return input->import_paths[i];
    }
  }
  return NULL;
}

static void append_source_range_json(ZBuf *buf, const char *path, int line, int column, int length) {
  int start_line = line > 0 ? line : 1;
  int start_column = column > 0 ? column : 1;
  int end_column = start_column + (length > 0 ? length : 1);
  zbuf_append(buf, "{\"path\":");
  append_json_string(buf, path ? path : "");
  zbuf_appendf(buf, ",\"start\":{\"line\":%d,\"column\":%d},\"end\":{\"line\":%d,\"column\":%d},\"columnUnit\":\"utf8-byte\"}",
               start_line,
               start_column,
               start_line,
               end_column);
}

static void append_use_imports_json(ZBuf *buf, const SourceInput *input, const Program *program) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->use_imports.len; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    UseImport *item = &program->use_imports.items[i];
    const char *path = source_path_for_parser_line(input, item->line);
    const char *from = module_name_for_source_path(input, path);
    int line = source_original_line_for_parser_line(input, item->line);
    const char *kind = import_module_is_stdlib(item->module) ? "stdlib" : "package-local";
    const char *resolved_path = import_module_is_stdlib(item->module) ? NULL : resolved_path_for_import(input, from, item->module);
    zbuf_append(buf, "{\"from\":");
    append_json_string(buf, from);
    zbuf_append(buf, ",\"to\":");
    append_json_string(buf, item->module);
    zbuf_append(buf, ",\"alias\":");
    append_json_string_or_null(buf, item->alias);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, kind);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, path ? path : "");
    zbuf_appendf(buf, ",\"line\":%d,\"column\":%d", line, item->column);
    zbuf_append(buf, ",\"sourceRange\":{\"path\":");
    append_json_string(buf, path ? path : "");
    zbuf_appendf(buf, ",\"start\":{\"line\":%d,\"column\":%d},\"end\":{\"line\":%d,\"column\":%d},\"columnUnit\":\"utf8-byte\"}",
                 line,
                 item->column,
                 line,
                 item->end_column > 0 ? item->end_column : item->column);
    zbuf_append(buf, ",\"resolvedPath\":");
    append_json_string_or_null(buf, resolved_path);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]");
}

static void append_graph_readiness_json(ZBuf *buf, SourceInput *input, Program *program, const ZTargetInfo *target, const Command *command) {
  zbuf_append(buf, "  \"targetReadiness\": ");
  append_target_readiness_json(buf, input, program, target, command);
  zbuf_append(buf, ",\n  \"safetyFacts\": ");
  append_safety_facts_json(buf, command && command->profile ? command->profile : "release");
  zbuf_append(buf, ",\n");
}

static void append_graph_json(ZBuf *buf, SourceInput *input, Program *program, const ZTargetInfo *target, const Command *command) {
  CapabilitySummary caps = program_capabilities(program);
  const char *profile = command && command->profile ? command->profile : "release";
  bool graph_input = command && z_program_graph_artifact_source_present(&command->graph_source);
  size_t public_count = 0, private_count = 0;
  for (size_t i = 0; i < input->symbol_count; i++) {
    if (input->symbol_public[i]) public_count++;
    else private_count++;
  }
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n");
  zbuf_append(buf, "  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  if (graph_input) append_program_graph_artifact_source_json(buf, &command->graph_source);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"targets\": [{\"name\":\"cli\",\"kind\":\"exe\",\"main\":");
  append_json_string(buf, input->source_file);
  zbuf_append(buf, "}],\n");
  zbuf_append(buf, "  \"package\": "); append_package_metadata_json(buf, input, target); zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"packageCache\": ");
  append_package_cache_audit_json(buf, input, target, profile);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"targetSupport\": {\"target\":");
  append_json_string(buf, target ? target->name : "host");
  zbuf_append(buf, ",\"hostTarget\":");
  append_json_string(buf, z_host_target());
  zbuf_appendf(buf, ",\"hosted\":%s,\"fsAvailable\":%s,\"capabilities\":", target_is_host(target) ? "true" : "false", z_target_has_capability(target, "fs") ? "true" : "false");
  CapabilitySummary target_caps = {0};
  target_caps.memory = true;
  target_caps.args = z_target_has_capability(target, "args");
  target_caps.env = z_target_has_capability(target, "env");
  target_caps.fs = z_target_has_capability(target, "fs");
  target_caps.time = z_target_has_capability(target, "time");
  target_caps.rand = z_target_has_capability(target, "rand");
  target_caps.net = z_target_has_capability(target, "net");
  target_caps.proc = z_target_has_capability(target, "proc");
  target_caps.web = z_target_has_capability(target, "web");
  append_capability_json_array(buf, &target_caps);
  zbuf_append(buf, ",\"requiredCapabilitySupport\":");
  append_target_capability_facts_json(buf, target, &caps);
  zbuf_append(buf, ",\"httpRuntime\":");
  z_append_http_runtime_json(buf, target);
  zbuf_append(buf, "},\n");
  append_graph_readiness_json(buf, input, program, target, command);
  zbuf_append(buf, "  \"requiresCapabilities\": ");
  append_capability_json_array(buf, &caps);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"selfHostSubset\": ");
  append_self_host_subset_json(buf, program, &caps, target);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"selfHostRouting\": ");
  append_self_host_routing_json(buf, "graph", NULL, program, &caps, target);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"compileTime\": ");
  append_compile_time_json(buf, program, input, target);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"releaseMatrixTargetSupport\": ");
  append_release_matrix_target_support_json(buf);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"stdlibHelpers\": ");
  append_stdlib_helpers_json(buf);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"callResolution\": ");
  z_append_call_resolution_facts_json(buf, input, program);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"programGraph\": ");
  z_append_program_graph_json(buf, input, program);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"cImports\": ");
  append_c_imports_json(buf, program, target);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"cLibraries\": ");
  append_c_libraries_json(buf, input, target);
  zbuf_append(buf, ",\n");
  zbuf_appendf(buf, "  \"symbolCounts\": {\"public\": %zu, \"private\": %zu, \"total\": %zu},\n", public_count, private_count, input->symbol_count);
  zbuf_append(buf, "  \"sourceFiles\": [");
  for (size_t i = 0; i < input->source_file_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_json_string(buf, input->source_files[i]);
  }
  zbuf_append(buf, "],\n  \"sourceMaps\": [");
  for (size_t i = 0; i < input->source_file_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    char *map_source = read_optional_file(input->source_files[i]);
    const char *map_text = map_source ? map_source : ((strcmp(input->source_files[i], input->source_file) == 0 && input->source) ? input->source : input->source_files[i]);
    zbuf_append(buf, "{\"path\":");
    append_json_string(buf, input->source_files[i]);
    zbuf_appendf(buf, ",\"sourceHash\":\"%016llx\",\"lineCount\":%zu,\"columnUnit\":\"utf8-byte\"}", (unsigned long long)fnv1a_text(map_text), source_line_count(map_text));
    free(map_source);
  }
  zbuf_append(buf, "],\n  \"imports\": [");
  for (size_t i = 0; i < input->import_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_json_string(buf, input->imports[i]);
  }
  zbuf_append(buf, "],\n  \"useImports\": ");
  append_use_imports_json(buf, input, program);
  zbuf_append(buf, ",\n  \"modules\": [");
  for (size_t i = 0; i < input->module_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, input->module_names[i]);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->module_paths[i]);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"interfaceFingerprints\": ");
  if (graph_input) append_interface_fingerprints_json_ex(buf, input, target, command->graph_source.graph_hash);
  else append_interface_fingerprints_json(buf, input, target);
  zbuf_append(buf, ",\n  \"importEdges\": [");
  for (size_t i = 0; i < input->import_edge_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"from\":");
    append_json_string(buf, input->import_from[i]);
    zbuf_append(buf, ",\"to\":");
    append_json_string(buf, input->import_to[i]);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->import_paths[i]);
    zbuf_append(buf, ",\"sourceRange\":");
    append_source_range_json(buf,
                             input->import_source_paths ? input->import_source_paths[i] : "",
                             input->import_lines ? input->import_lines[i] : 1,
                             input->import_columns ? input->import_columns[i] : 1,
                             input->import_lengths ? input->import_lengths[i] : 1);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n");
  zbuf_append(buf, "  \"symbols\": [");
  for (size_t i = 0; i < input->symbol_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, input->symbol_names[i]);
    zbuf_append(buf, ",\"module\":");
    append_json_string(buf, input->symbol_modules[i]);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, input->symbol_kinds ? input->symbol_kinds[i] : "unknown");
    zbuf_appendf(buf, ",\"public\":%s", input->symbol_public[i] ? "true" : "false");
    const Function *symbol_fun = (input->symbol_kinds && strcmp(input->symbol_kinds[i], "function") == 0) ? find_program_function(program, input->symbol_names[i]) : NULL;
    if (symbol_fun) {
      zbuf_append(buf, ",\"effects\":");
      append_function_effects_json(buf, symbol_fun);
      zbuf_append(buf, ",\"allocationBehavior\":");
      append_json_string(buf, function_allocation_behavior(symbol_fun));
      zbuf_append(buf, ",\"targetSupport\":");
      CapabilitySummary symbol_caps = function_capabilities(symbol_fun);
      append_target_capability_facts_json(buf, target, &symbol_caps);
      zbuf_append(buf, ",\"ownership\":");
      append_function_ownership_json(buf, symbol_fun);
    }
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n");
  zbuf_append(buf, "  \"functions\": [");
  for (size_t i = 0; i < program->functions.len; i++) {
    Function *fun = &program->functions.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"kind\":\"function\",\"public\":%s,\"params\":%zu,\"typeParams\":[", fun->name, fun->is_public ? "true" : "false", fun->params.len);
    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
      if (type_param_index > 0) zbuf_append(buf, ",");
      append_json_string(buf, fun->type_params.items[type_param_index].name);
    }
    zbuf_append(buf, "],\"staticParams\":[");
    bool wrote_static_param = false;
    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
      Param *type_param = &fun->type_params.items[type_param_index];
      if (!type_param->is_static) continue;
      if (wrote_static_param) zbuf_append(buf, ",");
      wrote_static_param = true;
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, type_param->name);
      zbuf_append(buf, ",\"type\":");
      append_json_string(buf, type_param->type ? type_param->type : "usize");
      zbuf_append(buf, ",\"kind\":");
      append_json_string(buf, static_param_kind_json(program, type_param->type));
      zbuf_append(buf, ",\"staticDispatch\":true}");
    }
    zbuf_append(buf, "],\"constraints\":[");
    bool wrote_constraint = false;
    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
      Param *type_param = &fun->type_params.items[type_param_index];
      if (type_param->is_static) continue;
      if (!type_param->type || strcmp(type_param->type, "Type") == 0) continue;
      if (wrote_constraint) zbuf_append(buf, ",");
      wrote_constraint = true;
      zbuf_append(buf, "{\"typeParam\":");
      append_json_string(buf, type_param->name);
      zbuf_append(buf, ",\"interface\":");
      append_json_string(buf, type_param->type);
      zbuf_append(buf, ",\"staticDispatch\":true}");
    }
    zbuf_appendf(buf, "],\"generic\":%s,\"returnType\":\"%s\",\"raises\":%s,", fun->type_params.len > 0 ? "true" : "false", fun->return_type ? fun->return_type : "Void", fun->raises ? "true" : "false");
    append_function_error_json(buf, fun);
    zbuf_append(buf, ",\"requiresCapabilities\":");
    append_function_effects_json(buf, fun);
    zbuf_append(buf, ",\"effects\":");
    append_function_effects_json(buf, fun);
    zbuf_append(buf, ",\"allocationBehavior\":");
    append_json_string(buf, function_allocation_behavior(fun));
    zbuf_append(buf, ",\"targetSupport\":");
    CapabilitySummary fun_caps = function_capabilities(fun);
    append_target_capability_facts_json(buf, target, &fun_caps);
    zbuf_append(buf, ",\"ownership\":");
    append_function_ownership_json(buf, fun);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"shapes\": [");
  for (size_t i = 0; i < program->shapes.len; i++) {
    Shape *shape = &program->shapes.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"kind\":\"shape\",\"public\":%s,\"layout\":\"%s\",\"typeParams\":[", shape->name, shape->is_public ? "true" : "false", shape->layout);
    for (size_t type_param_index = 0; type_param_index < shape->type_params.len; type_param_index++) {
      if (type_param_index > 0) zbuf_append(buf, ",");
      append_json_string(buf, shape->type_params.items[type_param_index].name);
    }
    zbuf_append(buf, "],\"staticParams\":[");
    bool wrote_shape_static_param = false;
    for (size_t type_param_index = 0; type_param_index < shape->type_params.len; type_param_index++) {
      Param *type_param = &shape->type_params.items[type_param_index];
      if (!type_param->is_static) continue;
      if (wrote_shape_static_param) zbuf_append(buf, ",");
      wrote_shape_static_param = true;
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, type_param->name);
      zbuf_append(buf, ",\"type\":");
      append_json_string(buf, type_param->type ? type_param->type : "usize");
      zbuf_append(buf, ",\"kind\":");
      append_json_string(buf, static_param_kind_json(program, type_param->type));
      zbuf_append(buf, ",\"staticDispatch\":true}");
    }
    zbuf_append(buf, "],\"generic\":");
    zbuf_append(buf, shape->type_params.len > 0 ? "true" : "false");
    zbuf_append(buf, ",\"fields\":[");
    for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
      Param *field = &shape->fields.items[field_index];
      if (field_index > 0) zbuf_append(buf, ", ");
      zbuf_appendf(buf, "{\"name\":\"%s\",\"type\":\"%s\",\"hasDefault\":%s}", field->name, field->type, field->default_value ? "true" : "false");
    }
    zbuf_append(buf, "],\"methods\":[");
    for (size_t method_index = 0; method_index < shape->methods.len; method_index++) {
      Function *method = &shape->methods.items[method_index];
      if (method_index > 0) zbuf_append(buf, ", ");
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, method->name);
      zbuf_append(buf, ",\"doc\":\"\"");
      zbuf_appendf(buf, ",\"public\":%s,\"params\":%zu,\"typeParams\":[", method->is_public ? "true" : "false", method->params.len);
      for (size_t type_param_index = 0; type_param_index < method->type_params.len; type_param_index++) {
        if (type_param_index > 0) zbuf_append(buf, ",");
        append_json_string(buf, method->type_params.items[type_param_index].name);
      }
      zbuf_append(buf, "],\"staticParams\":");
      append_static_params_json(buf, program, &method->type_params);
      zbuf_append(buf, ",\"constraints\":");
      append_type_param_constraints_json(buf, &method->type_params);
      zbuf_append(buf, ",\"inheritedShapeParams\":");
      zbuf_append(buf, shape->type_params.len > 0 ? "true" : "false");
      zbuf_append(buf, ",\"shapeTypeParams\":[");
      for (size_t type_param_index = 0; type_param_index < shape->type_params.len; type_param_index++) {
        if (type_param_index > 0) zbuf_append(buf, ",");
        append_json_string(buf, shape->type_params.items[type_param_index].name);
      }
      zbuf_append(buf, "],\"shapeStaticParams\":[");
      bool wrote_method_shape_static_param = false;
      for (size_t type_param_index = 0; type_param_index < shape->type_params.len; type_param_index++) {
        Param *type_param = &shape->type_params.items[type_param_index];
        if (!type_param->is_static) continue;
        if (wrote_method_shape_static_param) zbuf_append(buf, ",");
        wrote_method_shape_static_param = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, type_param->name);
        zbuf_append(buf, ",\"type\":");
        append_json_string(buf, type_param->type ? type_param->type : "usize");
        zbuf_append(buf, ",\"kind\":");
        append_json_string(buf, static_param_kind_json(program, type_param->type));
        zbuf_append(buf, ",\"staticDispatch\":true}");
      }
      zbuf_append(buf, "],\"returnType\":");
      append_json_string(buf, method->return_type ? method->return_type : "Void");
      zbuf_appendf(buf, ",\"raises\":%s,", method->raises ? "true" : "false");
      append_function_error_json(buf, method);
      zbuf_append(buf, ",\"staticDispatch\":true,\"effects\":");
      append_function_effects_json(buf, method);
      zbuf_append(buf, ",\"allocationBehavior\":");
      append_json_string(buf, function_allocation_behavior(method));
      zbuf_append(buf, ",\"targetSupport\":");
      CapabilitySummary method_caps = function_capabilities(method);
      append_target_capability_facts_json(buf, target, &method_caps);
      zbuf_append(buf, ",\"ownership\":");
      append_function_ownership_json(buf, method);
      zbuf_append(buf, "}");
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "],\n  \"interfaces\": [");
  for (size_t i = 0; i < program->interfaces.len; i++) {
    InterfaceDecl *interface = &program->interfaces.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"kind\":\"interface\",\"public\":%s,\"typeParams\":[", interface->name, interface->is_public ? "true" : "false");
    for (size_t type_param_index = 0; type_param_index < interface->type_params.len; type_param_index++) {
      if (type_param_index > 0) zbuf_append(buf, ",");
      append_json_string(buf, interface->type_params.items[type_param_index].name);
    }
    zbuf_append(buf, "],\"staticOnly\":true,\"methods\":[");
    for (size_t method_index = 0; method_index < interface->methods.len; method_index++) {
      Function *method = &interface->methods.items[method_index];
      if (method_index > 0) zbuf_append(buf, ", ");
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, method->name);
      zbuf_append(buf, ",\"doc\":\"\"");
      zbuf_append(buf, ",\"typeParams\":");
      append_type_param_names_json(buf, &method->type_params);
      zbuf_append(buf, ",\"staticParams\":");
      append_static_params_json(buf, program, &method->type_params);
      zbuf_append(buf, ",\"constraints\":");
      append_type_param_constraints_json(buf, &method->type_params);
      zbuf_append(buf, ",\"params\":[");
      for (size_t param_index = 0; param_index < method->params.len; param_index++) {
        Param *param = &method->params.items[param_index];
        if (param_index > 0) zbuf_append(buf, ",");
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, param->name);
        zbuf_append(buf, ",\"type\":");
        append_json_string(buf, param->type);
        zbuf_append(buf, "}");
      }
      zbuf_append(buf, "],\"returnType\":");
      append_json_string(buf, method->return_type ? method->return_type : "Void");
      zbuf_appendf(buf, ",\"raises\":%s,", method->raises ? "true" : "false");
      append_function_error_json(buf, method);
      zbuf_append(buf, ",\"staticDispatch\":true}");
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "],\n  \"aliases\": [");
  for (size_t i = 0; i < program->aliases.len; i++) {
    TypeAlias *item = &program->aliases.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, item->name);
    zbuf_append(buf, ",\"target\":");
    append_json_string(buf, item->target);
    zbuf_appendf(buf, ",\"public\":%s}", item->is_public ? "true" : "false");
  }
  zbuf_append(buf, "],\n  \"consts\": [");
  for (size_t i = 0; i < program->consts.len; i++) {
    ConstDecl *item = &program->consts.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, item->name);
    zbuf_append(buf, ",\"type\":");
    append_json_string(buf, item->type ? item->type : (item->expr ? item->expr->resolved_type : "Unknown"));
    zbuf_appendf(buf, ",\"public\":%s}", item->is_public ? "true" : "false");
  }
  zbuf_append(buf, "],\n  \"enums\": [");
  for (size_t i = 0; i < program->enums.len; i++) {
    EnumDecl *item = &program->enums.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"public\":%s,\"cases\":[", item->name, item->is_public ? "true" : "false");
    for (size_t case_index = 0; case_index < item->cases.len; case_index++) {
      if (case_index > 0) zbuf_append(buf, ", ");
      zbuf_appendf(buf, "\"%s\"", item->cases.items[case_index].name);
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "],\n  \"choices\": [");
  for (size_t i = 0; i < program->choices.len; i++) {
    Choice *item = &program->choices.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"public\":%s,\"cases\":[", item->name, item->is_public ? "true" : "false");
    for (size_t case_index = 0; case_index < item->cases.len; case_index++) {
      if (case_index > 0) zbuf_append(buf, ", ");
      zbuf_appendf(buf, "{\"name\":\"%s\",\"type\":", item->cases.items[case_index].name);
      if (item->cases.items[case_index].type) zbuf_appendf(buf, "\"%s\"", item->cases.items[case_index].type);
      else zbuf_append(buf, "null");
      zbuf_append(buf, "}");
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "],\n  \"compilerPhases\": ");
  append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  if (graph_input) append_compiler_caches_json_ex(buf, input, target, profile, "program-graph", command->graph_source.graph_hash);
  else append_compiler_caches_json(buf, input, target, profile);
  zbuf_append(buf, ",\n  \"incrementalInvalidation\": ");
  if (graph_input) append_incremental_invalidations_json_ex(buf, input, target, profile, command->graph_source.artifact, command->graph_source.graph_hash, command->graph_source.lowering);
  else append_incremental_invalidations_json(buf, input, target, profile);
  zbuf_append(buf, "\n}\n");
}

static void print_capability_summary_text(const CapabilitySummary *caps) {
  bool wrote = false;
#define PRINT_CAP(name, enabled) do { \
    if (enabled) { \
      printf("%s%s", wrote ? ", " : "", name); \
      wrote = true; \
    } \
  } while (0)
  PRINT_CAP("args", caps && caps->args);
  PRINT_CAP("env", caps && caps->env);
  PRINT_CAP("fs", caps && caps->fs);
  PRINT_CAP("memory", caps && caps->memory);
  PRINT_CAP("alloc", caps && caps->alloc);
  PRINT_CAP("path", caps && caps->path);
  PRINT_CAP("codec", caps && caps->codec);
  PRINT_CAP("parse", caps && caps->parse);
  PRINT_CAP("time", caps && caps->time);
  PRINT_CAP("rand", caps && caps->rand);
  PRINT_CAP("net", caps && caps->net);
  PRINT_CAP("proc", caps && caps->proc);
  PRINT_CAP("web", caps && caps->web);
  PRINT_CAP("world", caps && caps->world);
#undef PRINT_CAP
  if (!wrote) printf("none");
}

static void print_graph_inspect_text(SourceInput *input, Program *program, const ZTargetInfo *target, const Command *command) {
  (void)command;
  CapabilitySummary caps = program_capabilities(program);
  size_t public_count = 0;
  size_t private_count = 0;
  for (size_t i = 0; input && i < input->symbol_count; i++) {
    if (input->symbol_public && input->symbol_public[i]) public_count++;
    else private_count++;
  }

  ZProgramGraph graph = {0};
  ZProgramGraphValidation validation = {0};
  bool has_graph = input && program && z_program_graph_from_program(input, program, &graph);
  if (has_graph && !z_program_graph_merge_embedded_std_graph_modules(&graph, input, NULL)) has_graph = false;
  if (has_graph) z_program_graph_validate(&graph, &validation);

  ZDiag target_diag = {0};
  bool target_ok = validate_target_capabilities(program, target, &target_diag, input ? input->source_file : "");

  printf("program graph inspect\n");
  printf("input: %s\n", input && input->source_file ? input->source_file : "");
  printf("target: %s\n", target && target->name ? target->name : z_host_target());
  if (input && input->package_name && input->package_name[0]) {
    printf("package: %s", input->package_name);
    if (input->package_version && input->package_version[0]) printf("@%s", input->package_version);
    printf("\n");
  }
  if (input && input->manifest_path && input->manifest_path[0]) printf("manifest: %s\n", input->manifest_path);
  if (has_graph) {
    printf("graph: %s %s (%zu nodes, %zu edges, %s)\n",
           graph.module_identity ? graph.module_identity : "",
           graph.graph_hash ? graph.graph_hash : "",
           graph.node_len,
           graph.edge_len,
           validation.ok ? "shape-valid" : "shape-invalid");
  }
  printf("symbols: %zu public, %zu private, %zu total\n", public_count, private_count, input ? input->symbol_count : 0);
  printf("capabilities: ");
  print_capability_summary_text(&caps);
  printf("\n");
  printf("target readiness: %s\n", target_ok ? "ok" : "blocked");
  if (!target_ok && target_diag.message[0]) {
    printf("  %s\n", target_diag.message);
    if (target_diag.help[0]) printf("  help: %s\n", target_diag.help);
  }

  printf("\nsource files:\n");
  for (size_t i = 0; input && i < input->source_file_count; i++) {
    printf("  %s\n", input->source_files && input->source_files[i] ? input->source_files[i] : "");
  }
  printf("\nmodules:\n");
  for (size_t i = 0; input && i < input->module_count; i++) {
    printf("  %s path:%s\n",
           input->module_names && input->module_names[i] ? input->module_names[i] : "",
           input->module_paths && input->module_paths[i] ? input->module_paths[i] : "");
  }
  printf("\nimports:\n");
  if (!input || input->import_edge_count == 0) {
    printf("  none\n");
  } else {
    for (size_t i = 0; i < input->import_edge_count; i++) {
      printf("  %s -> %s path:%s\n",
             input->import_from && input->import_from[i] ? input->import_from[i] : "",
             input->import_to && input->import_to[i] ? input->import_to[i] : "",
             input->import_paths && input->import_paths[i] ? input->import_paths[i] : "");
    }
  }

  printf("\nfunctions:\n");
  for (size_t i = 0; program && i < program->functions.len; i++) {
    Function *fun = &program->functions.items[i];
    printf("  %s%s%s -> %s%s\n",
           fun->is_public ? "pub " : "",
           fun->is_test ? "test " : "",
           fun->is_test ? (fun->test_name ? fun->test_name : "test") : (fun->name ? fun->name : ""),
           fun->return_type ? fun->return_type : "Void",
           fun->raises ? " raises" : "");
    for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
      Param *param = &fun->params.items[param_index];
      printf("    param[%zu] %s: %s\n",
             param_index,
             param->name ? param->name : "",
             param->type ? param->type : "Unknown");
    }
  }
  printf("\nnext: use `zero query %s` for patch-ready node handles\n", input && input->source_file ? input->source_file : "[graph-input]");

  z_program_graph_free(&graph);
}

static void append_graph_validate_json(ZBuf *buf, const Command *command, const ZProgramGraph *graph, const ZProgramGraphValidation *validation) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"artifact\": ");
  append_json_string(buf, command->input);
  zbuf_appendf(buf, ",\n  \"canonicalSource\": %s,\n  \"moduleIdentity\": ", graph && graph->canonical_source ? "true" : "false");
  append_json_string(buf, graph ? graph->module_identity : "");
  zbuf_append(buf, ",\n  \"graphHash\": ");
  append_json_string(buf, graph ? graph->graph_hash : "");
  zbuf_appendf(buf, ",\n  \"counts\": {\"nodes\": %zu, \"edges\": %zu}", graph ? graph->node_len : 0, graph ? graph->edge_len : 0);
  zbuf_append(buf, ",\n  \"validation\": {\"state\": ");
  append_json_string(buf, z_program_graph_validation_state_name(validation ? validation->state : Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID));
  zbuf_append(buf, ", \"ok\": true}");
  zbuf_append(buf, ",\n  \"saved\": ");
  if (command->out) {
    zbuf_append(buf, "{\"path\": ");
    append_json_string(buf, command->out);
    zbuf_append(buf, ", \"byteStable\": true}");
  } else {
    zbuf_append(buf, "null");
  }
  zbuf_append(buf, "\n}\n");
}

static void append_graph_view_json(ZBuf *buf, const Command *command, const ZProgramGraph *graph, const char *view) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"artifact\": ");
  append_json_string(buf, command->input);
  zbuf_appendf(buf, ",\n  \"canonicalSource\": %s,\n  \"moduleIdentity\": ", graph && graph->canonical_source ? "true" : "false");
  append_json_string(buf, graph ? graph->module_identity : "");
  zbuf_append(buf, ",\n  \"graphHash\": ");
  append_json_string(buf, graph ? graph->graph_hash : "");
  zbuf_append(buf, ",\n  \"saved\": ");
  if (command->out) {
    zbuf_append(buf, "{\"path\": ");
    append_json_string(buf, command->out);
    zbuf_append(buf, ", \"byteStable\": true}");
  } else {
    zbuf_append(buf, "null");
  }
  zbuf_append(buf, ",\n  \"view\": ");
  if (command->out || !view) zbuf_append(buf, "null");
  else append_json_string(buf, view);
  zbuf_append(buf, "\n}\n");
}

static void append_graph_saved_json(ZBuf *buf, const char *path) {
  if (path) {
    zbuf_append(buf, "{\"path\": ");
    append_json_string(buf, path);
    zbuf_append(buf, ", \"byteStable\": true}");
  } else {
    zbuf_append(buf, "null");
  }
}

static void append_graph_import_json(ZBuf *buf, const Command *command, const SourceInput *input, const ZProgramGraph *graph, const ZProgramGraphValidation *validation) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(buf, validation && validation->ok ? "true" : "false");
  zbuf_append(buf, ",\n  \"sourceFile\": ");
  append_json_string(buf, input && input->source_file ? input->source_file : command->input);
  zbuf_appendf(buf, ",\n  \"canonicalSource\": %s,\n  \"moduleIdentity\": ", graph && graph->canonical_source ? "true" : "false");
  append_json_string(buf, graph ? graph->module_identity : "");
  zbuf_append(buf, ",\n  \"graphHash\": ");
  append_json_string(buf, graph ? graph->graph_hash : "");
  zbuf_appendf(buf, ",\n  \"counts\": {\"nodes\": %zu, \"edges\": %zu}", graph ? graph->node_len : 0, graph ? graph->edge_len : 0);
  zbuf_append(buf, ",\n  \"validation\": {\"state\": ");
  append_json_string(buf, z_program_graph_validation_state_name(validation ? validation->state : Z_PROGRAM_GRAPH_VALIDATION_SHAPE_VALID));
  zbuf_append(buf, ", \"ok\": ");
  zbuf_append(buf, validation && validation->ok ? "true" : "false");
  zbuf_append(buf, "}");
  zbuf_append(buf, ",\n  \"saved\": ");
  append_graph_saved_json(buf, command->out);
  zbuf_append(buf, "\n}\n");
}

static const char *graph_check_diagnostic_path(const Command *command) {
  return command && command->input ? command->input : "<program-graph>";
}

static int reject_graph_unsupported_out(const Command *command, ZDiag *diag) {
  ZProgramGraphOutputContract contract = z_program_graph_command_output_contract(command ? command->kind : NULL);
  diag->code = 2002;
  diag->path = command && command->input ? command->input : NULL;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", contract.message ? contract.message : "graph command does not support --out");
  snprintf(diag->expected, sizeof(diag->expected), "%s", contract.expected ? contract.expected : "zero dump|validate|roundtrip --out <program-graph-artifact> [graph-input]");
  bool root_command = command && is_program_graph_root_command(command->command);
  snprintf(diag->actual, sizeof(diag->actual), root_command ? "zero %s --out" : "%s", root_command ? command->command : (contract.actual ? contract.actual : "zero --out"));
  snprintf(diag->help, sizeof(diag->help), "%s", contract.help ? contract.help : "remove --out or choose a graph subcommand that writes an artifact");
  if (command && command->json) print_diag_json(diag->path ? diag->path : (command->input ? command->input : "<graph>"), diag);
  else print_diag(diag->path ? diag->path : (command && command->input ? command->input : "<graph>"), diag);
  return 1;
}

static bool reject_graph_source_text_out(const Command *command, const char *expected, ZDiag *diag) {
  if (!command || !command->out || !z_program_graph_path_is_source_text(command->out)) return false;
  diag->code = 2002;
  diag->path = command->out;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "program graph output must not use source text extension");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "zero dump|validate|roundtrip --out <program-graph-artifact> [graph-input]");
  snprintf(diag->actual, sizeof(diag->actual), "%s", command->out);
  snprintf(diag->help, sizeof(diag->help), ".0 files are canonical source text; write derived ProgramGraph artifacts to a non-source path");
  print_command_diag(command, command->out, diag);
  return true;
}

typedef enum {
  GRAPH_INPUT_ARTIFACT,
  GRAPH_INPUT_CANONICAL_SOURCE,
  GRAPH_INPUT_CURRENT_SOURCE,
  GRAPH_INPUT_REPOSITORY_STORE,
} GraphInputKind;

static bool graph_input_is_source_path(const Command *command) {
  return command && command->input && is_zero_source_path(command->input);
}

static bool graph_source_or_artifact_command_prefers_package_source(const Command *command) {
  if (!command || !command->kind) return false;
  return strcmp(command->kind, "import") == 0;
}

static bool reject_graph_source_input_without_store(const Command *command, ZDiag *diag) {
  char *sidecar = command && is_zero_source_path(command->input)
    ? source_sidecar_graph_path(command->input)
    : NULL;
  set_graph_input_required_diag(command ? command->input : NULL, sidecar, diag);
  free(sidecar);
  return false;
}

static bool load_graph_from_repository_store(const Command *command, ZProgramGraph *graph, GraphInputKind *kind, ZDiag *diag) {
  ZProgramGraphStore store;
  if (!z_program_graph_store_load_path(command->input, &store, diag)) return false;
  *graph = store.graph;
  store.graph = (ZProgramGraph){0};
  z_program_graph_store_free(&store);
  if (kind) *kind = GRAPH_INPUT_REPOSITORY_STORE;
  return true;
}

static bool graph_build_from_source_program(const SourceInput *input, const Program *program, bool canonical_source, ZProgramGraph *graph, ZDiag *diag) {
  if (!z_program_graph_from_program(input, program, graph)) {
    if (diag) {
      diag->code = 2002;
      diag->path = input && input->source_file ? input->source_file : NULL;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "failed to build source program graph");
    }
    return false;
  }
  graph->canonical_source = canonical_source;
  if (!z_program_graph_merge_embedded_std_graph_modules(graph, input, diag)) {
    z_program_graph_free(graph);
    return false;
  }
  return true;
}

static bool parse_graph_read_source_input(SourceInput *input, Program *program, ZDiag *diag) {
  if (!input || !program) return false;
  if (diag) diag->path = input->source_file;
  bool ok = z_parse_canonical_text_program_source(input->source, program, diag);
  if (!ok && diag) z_map_source_diag(input, diag);
  return ok;
}

static bool load_source_program_for_graph_read(const char *input_path, SourceInput *input, Program *program, ZDiag *diag) {
  bool handled_package = false;
  if (resolve_direct_package_source(input_path, input, diag, &handled_package)) {
    return parse_graph_read_source_input(input, program, diag);
  }
  if (handled_package) return false;

  if (is_zero_source_path(input_path)) {
    if (!resolve_direct_canonical_source(input_path, input, diag)) return false;
    return parse_graph_read_source_input(input, program, diag);
  }

  set_source_input_diag(input_path, diag);
  return false;
}

static bool load_graph_from_current_source(const Command *command, const ZTargetInfo *target, SourceInput *input, Program *program, ZProgramGraph *graph, GraphInputKind *kind, ZDiag *diag) {
  (void)target;
  if (!load_source_program_for_graph_read(command->input, input, program, diag)) return false;
  if (!graph_build_from_source_program(input, program, input->canonical_text_source, graph, diag)) return false;
  if (kind) *kind = input->canonical_text_source ? GRAPH_INPUT_CANONICAL_SOURCE : GRAPH_INPUT_CURRENT_SOURCE;
  return true;
}

static bool import_init_template_graph_store(const char *root, ZProgramGraphStoreFormat store_format, ZDiag *diag) {
  Command graph_command = {0};
  graph_command.input = root;
  SourceInput input = {0};
  Program program = {0};
  ZProgramGraph graph = {0};
  ZProgramGraphStore saved = {0};
  bool ok = load_graph_from_current_source(&graph_command, z_find_target(z_host_target()), &input, &program, &graph, NULL, diag);
  if (ok) ok = z_program_graph_store_save_for_input_format(root, &graph, store_format, &saved, diag);
  z_program_graph_store_free(&saved);
  z_program_graph_free(&graph);
  z_free_program(&program);
  z_free_source(&input);
  return ok;
}

typedef struct {
  const Command *command;
  const ZTargetInfo *target;
} RepositoryGraphSourceGraphLoader;

static bool load_repository_graph_checked_source_graph(void *ctx, ZProgramGraph *graph, ZDiag *diag) {
  const RepositoryGraphSourceGraphLoader *loader = (const RepositoryGraphSourceGraphLoader *)ctx;
  if (!loader || !loader->command) return false;
  SourceInput input = {0};
  Program program = {0};
  bool ok = load_graph_from_current_source(loader->command, loader->target, &input, &program, graph, NULL, diag);
  z_free_program(&program);
  z_free_source(&input);
  if (!ok) z_program_graph_free(graph);
  return ok;
}

static bool repository_graph_command_loads_checked_source(const Command *command) {
  if (!command || !command->kind) return false;
  if (command->graph_export_from_graph) return false;
  if (strcmp(command->kind, "status") == 0 || strcmp(command->kind, "verify-projection") == 0) return false;
  return strcmp(command->kind, "import") == 0;
}

static bool load_graph_input_for_read(const Command *command, const ZTargetInfo *target, SourceInput *input, Program *program, ZProgramGraph *graph, GraphInputKind *kind, ZDiag *diag) {
  memset(input, 0, sizeof(*input));
  memset(program, 0, sizeof(*program));
  memset(graph, 0, sizeof(*graph));
  if (kind) *kind = GRAPH_INPUT_ARTIFACT;
  if (command->repository_graph_input) return load_graph_from_repository_store(command, graph, kind, diag);
  if (graph_input_is_source_path(command)) {
    if (graph_source_or_artifact_command_prefers_package_source(command)) return load_graph_from_current_source(command, target, input, program, graph, kind, diag);
    return reject_graph_source_input_without_store(command, diag);
  }

  char *manifest_path = z_manifest_path_for_input(command->input);
  if (manifest_path) {
    free(manifest_path);
    if (graph_source_or_artifact_command_prefers_package_source(command)) return load_graph_from_current_source(command, target, input, program, graph, kind, diag);
    return reject_graph_source_input_without_store(command, diag);
  }

  if (!z_program_graph_load(command->input, graph, diag)) return false;
  if (kind) *kind = GRAPH_INPUT_ARTIFACT;
  return true;
}

static bool load_graph_input_for_checked_read(const Command *command, const ZTargetInfo *target, SourceInput *input, Program *program, ZProgramGraph *graph, GraphInputKind *kind, ZDiag *diag) {
  memset(input, 0, sizeof(*input));
  memset(program, 0, sizeof(*program));
  memset(graph, 0, sizeof(*graph));
  if (kind) *kind = GRAPH_INPUT_ARTIFACT;
  if (command->repository_graph_input) return load_graph_from_repository_store(command, graph, kind, diag);
  bool source_input = graph_input_is_source_path(command);
  char *manifest_path = source_input ? NULL : z_manifest_path_for_input(command->input);
  if (source_input || manifest_path) {
    free(manifest_path);
    if (!graph_source_or_artifact_command_prefers_package_source(command)) return reject_graph_source_input_without_store(command, diag);
    if (!compile_input(command->input, target, "release", input, program, diag)) return false;
    if (!graph_build_from_source_program(input, program, input->canonical_text_source, graph, diag)) return false;
    if (kind) *kind = input->canonical_text_source ? GRAPH_INPUT_CANONICAL_SOURCE : GRAPH_INPUT_CURRENT_SOURCE;
    return true;
  }

  if (!z_program_graph_load(command->input, graph, diag)) return false;
  if (kind) *kind = GRAPH_INPUT_ARTIFACT;
  return true;
}

static bool load_graph_input_for_patch(const Command *command, SourceInput *input, Program *program, ZProgramGraph *graph, GraphInputKind *kind, ZDiag *diag) {
  memset(input, 0, sizeof(*input));
  memset(program, 0, sizeof(*program));
  memset(graph, 0, sizeof(*graph));
  if (kind) *kind = GRAPH_INPUT_ARTIFACT;
  if (command->repository_graph_input) return load_graph_from_repository_store(command, graph, kind, diag);
  if (!graph_input_is_source_path(command)) return z_program_graph_load(command->input, graph, diag);

  (void)input;
  (void)program;
  (void)kind;
  return reject_graph_source_input_without_store(command, diag);
}

/*
 * One diagnostic oracle for the repository graph sync verbs (friction #52).
 * zero patch revalidation, zero import, and the stale-store auto-refresh all
 * collect diagnostics through the same phases zero check runs on a package
 * store: name contracts, reference resolution, fixed-array length contracts,
 * target capabilities, package dependencies, and the check-time buildability
 * gate. The active phase reports every diagnostic it finds instead of only
 * the first, and callers diff the set against the pre-operation baseline so
 * pre-existing diagnostics never wall an unrelated operation.
 */
#define GRAPH_ORACLE_MAX_PHASE_DIAGS 64

typedef struct {
  ZDiag *items;
  char **keys;
  size_t len;
  size_t cap;
} GraphOracleDiags;

static void graph_oracle_diags_free(GraphOracleDiags *list) {
  if (!list) return;
  for (size_t i = 0; i < list->len; i++) {
    free((char *)list->items[i].path);
    free(list->keys[i]);
  }
  free(list->items);
  free(list->keys);
  *list = (GraphOracleDiags){0};
}

static const char *graph_oracle_path_tail(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

static void graph_oracle_diags_push(GraphOracleDiags *list, const ZDiag *diag, char *key) {
  if (list->len == list->cap) {
    size_t next = z_grow_capacity(list->cap, list->len + 1, 8);
    list->items = z_checked_reallocarray(list->items, next, sizeof(ZDiag));
    list->keys = z_checked_reallocarray(list->keys, next, sizeof(char *));
    list->cap = next;
  }
  list->items[list->len] = *diag;
  list->keys[list->len] = key;
  list->len++;
}

/*
 * The match key deliberately omits line and column so edits elsewhere in a
 * file cannot turn a pre-existing diagnostic into a falsely introduced one;
 * the salient detail (symbol name, capability, mismatch facts) plus code and
 * file name identify the diagnostic across graph rebuilds.
 */
static void graph_oracle_diags_add(GraphOracleDiags *list, const ZDiag *diag, const char *fallback_path, const char *salient) {
  ZDiag copy = *diag;
  const char *path = diag->path && diag->path[0] ? diag->path : fallback_path;
  copy.path = path ? z_strdup(path) : NULL;
  ZBuf key;
  zbuf_init(&key);
  zbuf_appendf(&key, "%s|%s|%s|%s", diag_code(diag->code), graph_oracle_path_tail(path), diag->message, salient && salient[0] ? salient : diag->actual);
  graph_oracle_diags_push(list, &copy, key.data ? key.data : z_strdup(""));
}

static void graph_oracle_collect_resolution(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *facts, const char *path, GraphOracleDiags *out) {
  if (!facts || facts->diagnostic_len == 0) return;
  for (size_t i = 0; i < facts->reference_len; i++) {
    const ZProgramGraphResolutionReference *ref = &facts->references[i];
    if (ref->resolved && !ref->ambiguous) continue;
    const ZProgramGraphNode *node = graph_check_node_by_id(graph, ref->node_id);
    ZDiag diag = {0};
    diag.code = ref->ambiguous ? 3004 : 3003;
    diag.path = node && node->path && node->path[0] ? node->path : path;
    diag.line = node && node->line > 0 ? node->line : 1;
    diag.column = node && node->column > 0 ? node->column : 1;
    diag.length = 1;
    snprintf(diag.message, sizeof(diag.message), "%s", ref->ambiguous ? "ambiguous graph reference" : "unresolved graph reference");
    snprintf(diag.expected, sizeof(diag.expected), "resolved graph symbol reference");
    snprintf(diag.actual, sizeof(diag.actual), "node %s name %s", ref->node_id ? ref->node_id : "<unknown>", ref->name ? ref->name : "<unknown>");
    snprintf(diag.help, sizeof(diag.help), "inspect zero query and zero source-map, then repair the referenced graph node or import binding");
    graph_oracle_diags_add(out, &diag, path, ref->name);
  }
}

static void graph_oracle_collect_capabilities(const ZProgramGraph *graph, const ZProgramGraphResolutionFacts *resolution, const ZTargetInfo *target, const char *path, GraphOracleDiags *out) {
  ZProgramGraphCapabilitySummary caps;
  z_program_graph_collect_capabilities(graph, resolution, &caps);
#define COLLECT_GRAPH_CAP(field, cap_name, label) do { \
    if (caps.field && !capability_available_on_target(target, cap_name)) { \
      ZDiag cap_diag = {0}; \
      graph_check_target_capability_diag(&cap_diag, caps.field##_node, target, cap_name, label); \
      graph_oracle_diags_add(out, &cap_diag, path, cap_name); \
    } \
  } while (0)
  COLLECT_GRAPH_CAP(fs, "fs", "Fs");
  COLLECT_GRAPH_CAP(args, "args", "Args");
  COLLECT_GRAPH_CAP(env, "env", "Env");
  COLLECT_GRAPH_CAP(time, "time", "Clock");
  COLLECT_GRAPH_CAP(rand, "rand", "Rand");
  COLLECT_GRAPH_CAP(net, "net", "Net");
  COLLECT_GRAPH_CAP(proc, "proc", "Proc");
  COLLECT_GRAPH_CAP(web, "web", "Web");
  COLLECT_GRAPH_CAP(world, "world", "World");
#undef COLLECT_GRAPH_CAP
}

static bool repository_graph_oracle_collect(const Command *command, const ZTargetInfo *target, ZProgramGraph *graph, const char *manifest_input, const char *diag_path, bool with_semantic_contracts, GraphOracleDiags *out, ZDiag *fatal) {
  SourceInput input = {.source_file = z_strdup(diag_path ? diag_path : "<repository-graph>")};
  z_program_graph_seed_source_metadata(&input, graph);
  ZDiag attach_diag = {0};
  if (manifest_input && !z_program_graph_manifest_attach_metadata_to_input(&input, manifest_input, &attach_diag)) {
    if (fatal) {
      const char *attach_path = attach_diag.path;
      *fatal = attach_diag;
      if (attach_path) z_diag_set_path_copy(fatal, attach_path);
      else z_diag_set_path_copy(fatal, diag_path);
      /* fatal now owns its own copy; release the attach diag's original */
      free((char *)attach_path);
    } else {
      free((char *)attach_diag.path);
    }
    z_free_source(&input);
    return false;
  }
  ZDiag *scratch = z_checked_calloc(GRAPH_ORACLE_MAX_PHASE_DIAGS, sizeof(ZDiag));
  size_t found = z_program_graph_name_contract_violations(graph, diag_path, scratch, GRAPH_ORACLE_MAX_PHASE_DIAGS);
  if (found > GRAPH_ORACLE_MAX_PHASE_DIAGS) found = GRAPH_ORACLE_MAX_PHASE_DIAGS;
  for (size_t i = 0; i < found; i++) graph_oracle_diags_add(out, &scratch[i], diag_path, scratch[i].actual);
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  if (out->len == 0) {
    if (!z_program_graph_collect_resolution_facts(graph, &resolution)) {
      ZDiag facts_diag = {0};
      facts_diag.code = 2002;
      facts_diag.path = diag_path;
      facts_diag.line = 1;
      facts_diag.column = 1;
      facts_diag.length = 1;
      snprintf(facts_diag.message, sizeof(facts_diag.message), "failed to collect graph resolution facts");
      graph_oracle_diags_add(out, &facts_diag, diag_path, "resolution-facts");
    } else {
      graph_oracle_collect_resolution(graph, &resolution, diag_path, out);
      if (out->len == 0) {
        memset(scratch, 0, GRAPH_ORACLE_MAX_PHASE_DIAGS * sizeof(ZDiag));
        found = z_program_graph_fixed_array_length_contract_violations(graph, &resolution, diag_path, scratch, GRAPH_ORACLE_MAX_PHASE_DIAGS);
        if (found > GRAPH_ORACLE_MAX_PHASE_DIAGS) found = GRAPH_ORACLE_MAX_PHASE_DIAGS;
        for (size_t i = 0; i < found; i++) graph_oracle_diags_add(out, &scratch[i], diag_path, scratch[i].actual);
      }
      if (out->len == 0) graph_oracle_collect_capabilities(graph, &resolution, target, diag_path, out);
      if (with_semantic_contracts && out->len == 0) {
        ZDiag dep_diag = {0};
        if (!validate_package_dependencies_for_target(&input, target, &dep_diag)) graph_oracle_diags_add(out, &dep_diag, diag_path, dep_diag.actual);
      }
      if (out->len == 0 && graph_check_buildability_gate_applies(graph)) {
        ZDiag gate_diag = {0};
        if (check_gate_merged_graph_blocked(command, &input, target, graph, &gate_diag)) {
          graph_oracle_diags_add(out, &gate_diag, diag_path, gate_diag.actual);
          free((char *)gate_diag.path);
        }
      }
      /*
       * Graph semantic contracts (return types, checked fallible calls,
       * Maybe guards, borrows) run last and only for patch revalidation:
       * they are stricter than zero check, so their findings only fail an
       * operation when the baseline diff proves the operation introduced
       * them. Import and refresh validate the same facts with the compiler
       * typecheck instead, which is much faster on large packages.
       */
      if (with_semantic_contracts && out->len == 0) {
        memset(scratch, 0, GRAPH_ORACLE_MAX_PHASE_DIAGS * sizeof(ZDiag));
        found = z_program_graph_effect_contract_violations(graph, &resolution, diag_path, scratch, GRAPH_ORACLE_MAX_PHASE_DIAGS);
        if (found > GRAPH_ORACLE_MAX_PHASE_DIAGS) found = GRAPH_ORACLE_MAX_PHASE_DIAGS;
        for (size_t i = 0; i < found; i++) graph_oracle_diags_add(out, &scratch[i], diag_path, scratch[i].actual);
      }
      if (with_semantic_contracts && out->len == 0) {
        memset(scratch, 0, GRAPH_ORACLE_MAX_PHASE_DIAGS * sizeof(ZDiag));
        found = z_program_graph_memory_contract_violations(graph, &resolution, diag_path, scratch, GRAPH_ORACLE_MAX_PHASE_DIAGS);
        if (found > GRAPH_ORACLE_MAX_PHASE_DIAGS) found = GRAPH_ORACLE_MAX_PHASE_DIAGS;
        for (size_t i = 0; i < found; i++) graph_oracle_diags_add(out, &scratch[i], diag_path, scratch[i].actual);
      }
      if (with_semantic_contracts && out->len == 0) {
        ZDiag borrow_diag = {0};
        if (!z_program_graph_borrow_contracts_ok(graph, diag_path, &borrow_diag)) graph_oracle_diags_add(out, &borrow_diag, diag_path, borrow_diag.actual);
      }
    }
  }
  free(scratch);
  z_program_graph_resolution_facts_free(&resolution);
  z_free_source(&input);
  return true;
}

static bool graph_oracle_key_present(const GraphOracleDiags *list, const char *key) {
  for (size_t i = 0; list && key && i < list->len; i++) {
    if (graph_check_text_eq(list->keys[i], key)) return true;
  }
  return false;
}

/* Moves every post entry into introduced or preexisting; post ends empty. */
static void repository_graph_oracle_diff(GraphOracleDiags *post, const GraphOracleDiags *pre, GraphOracleDiags *introduced, GraphOracleDiags *preexisting) {
  for (size_t i = 0; post && i < post->len; i++) {
    GraphOracleDiags *target_list = graph_oracle_key_present(pre, post->keys[i]) ? preexisting : introduced;
    graph_oracle_diags_push(target_list, &post->items[i], post->keys[i]);
  }
  if (post) {
    free(post->items);
    free(post->keys);
    *post = (GraphOracleDiags){0};
  }
}

static void print_command_diag_list(const Command *command, const char *fallback_path, const GraphOracleDiags *list) {
  if (!list || list->len == 0) return;
  if (command && command->json) {
    if (graph_check_text_eq(command->command, "check")) print_diag_json_list_ex(fallback_path, list->items, list->len, command->profile ? command->profile : "release");
    else print_diag_json_list_ex(fallback_path, list->items, list->len, NULL);
    return;
  }
  for (size_t i = 0; i < list->len; i++) {
    print_diag(list->items[i].path ? list->items[i].path : fallback_path, &list->items[i]);
  }
}

static void print_graph_patch_revalidation_hints(const Command *command, const GraphOracleDiags *list) {
  if ((command && command->json) || !list) return;
  bool saw_non_void_return = false;
  for (size_t i = 0; i < list->len; i++) {
    const ZDiag *diag = &list->items[i];
    if (strcmp(diag->code_text, "TYP003") == 0 && strstr(diag->message, "non-void function must return")) {
      saw_non_void_return = true;
    }
  }
  if (saw_non_void_return) {
    fprintf(stderr, "  help: new non-void helpers need a body in the same graph patch; use upsertFunction, or batch addFunction/addParam with replaceFunctionBody before revalidation\n");
  }
}

#define NOTE_DEDUP_SEED 1469598103934665603ull

static uint64_t note_dedup_fold(uint64_t hash, const char *text) {
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    hash ^= *cursor;
    hash *= 1099511628211ull;
  }
  hash ^= 0x1f;
  hash *= 1099511628211ull;
  return hash;
}

static char *note_dedup_marker_path(const char *input) {
  char *root = z_program_graph_store_root_for_input(input);
  if (!root) return NULL;
  ZBuf path;
  zbuf_init(&path);
  zbuf_append(&path, root);
  if (path.len > 0 && path.data[path.len - 1] != '/') zbuf_append_char(&path, '/');
  zbuf_append(&path, ".zero/cache/agent-notes.state");
  free(root);
  return path.data;
}

/*
 * Once-per-state stderr notes: a tiny marker under .zero/cache records the
 * context hash of the last printed note per note id, so an exact-duplicate
 * note stays suppressed until the store or source state changes the context.
 * The first print for any new state keeps its verbatim wording.
 */
static bool note_dedup_should_print(const char *input, const char *note_id, uint64_t context_hash) {
  char *marker = note_dedup_marker_path(input);
  if (!marker) return true;
  char line[160];
  snprintf(line, sizeof(line), "%s %016llx", note_id, (unsigned long long)context_hash);
  ZBuf next;
  zbuf_init(&next);
  bool seen = false;
  unsigned char *data = NULL;
  size_t len = 0;
  ZDiag read_diag = {0};
  if (z_read_binary_file(marker, &data, &len, &read_diag)) {
    char *text = z_strndup((const char *)data, len);
    free(data);
    char *cursor = text;
    while (*cursor) {
      char *end = strchr(cursor, '\n');
      if (end) *end = '\0';
      if (strcmp(cursor, line) == 0) seen = true;
      else {
        size_t id_len = strlen(note_id);
        bool same_id = strncmp(cursor, note_id, id_len) == 0 && cursor[id_len] == ' ';
        if (cursor[0] && !same_id) {
          zbuf_append(&next, cursor);
          zbuf_append_char(&next, '\n');
        }
      }
      if (!end) break;
      cursor = end + 1;
    }
    free(text);
  }
  if (seen) {
    zbuf_free(&next);
    free(marker);
    return false;
  }
  zbuf_append(&next, line);
  zbuf_append_char(&next, '\n');
  ZDiag write_diag = {0};
  (void)z_write_binary_file(marker, (const unsigned char *)(next.data ? next.data : ""), next.len, &write_diag);
  zbuf_free(&next);
  free(marker);
  return true;
}

static uint64_t preexisting_notes_context_hash(const GraphOracleDiags *list) {
  uint64_t hash = NOTE_DEDUP_SEED;
  for (size_t i = 0; list && i < list->len; i++) {
    const ZDiag *diag = &list->items[i];
    char facts[64];
    snprintf(facts, sizeof(facts), "%d:%d:%d", diag->code, diag->line, diag->column);
    hash = note_dedup_fold(hash, diag->path ? diag->path : "");
    hash = note_dedup_fold(hash, facts);
    hash = note_dedup_fold(hash, diag->message);
  }
  return hash;
}

static void print_preexisting_diag_notes(const char *verb, const char *input, const GraphOracleDiags *list) {
  if (!list || list->len == 0) return;
  if (!note_dedup_should_print(input, "preexisting", preexisting_notes_context_hash(list))) return;
  fprintf(stderr, "note: %zu pre-existing diagnostic%s predate%s this %s and did not block it; zero check reports the same set\n",
          list->len, list->len == 1 ? "" : "s", list->len == 1 ? "s" : "", verb);
  for (size_t i = 0; i < list->len; i++) {
    const ZDiag *diag = &list->items[i];
    fprintf(stderr, "note:   %s:%d:%d %s: %s\n", diag->path ? diag->path : "<package>", diag->line, diag->column, diag_code(diag->code), diag->message);
  }
}

/*
 * Whole-graph revalidation for zero patch with baseline-diff semantics: the
 * patched graph is validated with the shared oracle, and when diagnostics
 * surface they are compared against the unpatched store so only diagnostics
 * the patch introduced fail the operation. Returns false with fatal set when
 * validation infrastructure itself failed, false with introduced entries
 * when the patch added diagnostics, and true otherwise (preexisting carries
 * the diagnostics that predate the patch).
 */
static bool revalidate_repository_graph_patch_output(const Command *command, const ZTargetInfo *target, ZProgramGraph *patched, GraphOracleDiags *introduced, GraphOracleDiags *preexisting, ZDiag *fatal) {
  GraphOracleDiags post = {0};
  const char *manifest_input = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
  if (!repository_graph_oracle_collect(command, target, patched, manifest_input, command->input, true, &post, fatal)) {
    graph_oracle_diags_free(&post);
    return false;
  }
  if (post.len == 0) return true;
  GraphOracleDiags pre = {0};
  ZProgramGraphStore base;
  ZDiag base_diag = {0};
  if (z_program_graph_store_load_path(command->input, &base, &base_diag)) {
    ZDiag pre_fatal = {0};
    (void)repository_graph_oracle_collect(command, target, &base.graph, manifest_input, command->input, true, &pre, &pre_fatal);
    free((char *)pre_fatal.path);
    z_program_graph_store_free(&base);
  }
  repository_graph_oracle_diff(&post, &pre, introduced, preexisting);
  graph_oracle_diags_free(&pre);
  return introduced->len == 0;
}

/*
 * A compiler typecheck failure predates the operation when the file it
 * points at is byte-identical to the source projection recorded in the
 * package store: the diagnostic was already in the store at the last sync,
 * so the current operation did not introduce it.
 */
static bool repository_graph_checker_diag_is_preexisting(const char *input, const ZDiag *diag) {
  if (!diag || !diag->path || !diag->path[0]) return false;
  char *root = z_program_graph_store_root_for_input(input);
  char *store_path = root ? z_program_graph_store_path_for_root(root) : NULL;
  bool preexisting = false;
  ZProgramGraphStore store;
  ZDiag load_diag = {0};
  if (store_path && z_program_graph_store_path_exists(store_path) && z_program_graph_store_load_path(store_path, &store, &load_diag)) {
    const char *diag_file = diag->path;
    while (diag_file[0] == '.' && diag_file[1] == '/') diag_file += 2;
    size_t diag_len = strlen(diag_file);
    for (size_t i = 0; i < store.projection_len; i++) {
      const char *projection_path = store.projection_paths[i];
      size_t projection_len = strlen(projection_path);
      bool matches = graph_check_text_eq(projection_path, diag_file) ||
                     (diag_len > projection_len && strcmp(diag_file + diag_len - projection_len, projection_path) == 0 && diag_file[diag_len - projection_len - 1] == '/');
      if (!matches) continue;
      char *joined = direct_join_path(store.root && store.root[0] ? store.root : ".", projection_path);
      ZDiag read_diag = {0};
      char *current = z_read_file(joined, &read_diag);
      preexisting = current && graph_check_text_eq(current, store.projection_texts[i]);
      free(current);
      free(joined);
      break;
    }
    z_program_graph_store_free(&store);
  }
  free(store_path);
  free(root);
  return preexisting;
}

/*
 * Shared validation gate for zero import and the stale-store auto-refresh:
 * the source-projection graph is validated with the zero check oracle,
 * diffed against the existing package store so only diagnostics the edited
 * source introduces fail the operation, with every introduced diagnostic
 * reported in one shot. A compiler typecheck then guards type facts the
 * graph oracle does not model; its failure is skipped with a note when the
 * pointed-at file is unchanged since the last sync. Returns an exit code.
 */
/*
 * Diffs the zero check oracle findings for a source-projection graph
 * against the existing package store so callers see only the diagnostics
 * the edited source introduced; the rest are pre-existing.
 */
static bool repository_graph_source_oracle_diff(const Command *load_command, const ZTargetInfo *target, ZProgramGraph *source_graph, const char *input, GraphOracleDiags *introduced, GraphOracleDiags *preexisting, ZDiag *fatal) {
  GraphOracleDiags post = {0};
  if (!repository_graph_oracle_collect(load_command, target, source_graph, input, input, false, &post, fatal)) {
    graph_oracle_diags_free(&post);
    return false;
  }
  if (post.len == 0) {
    graph_oracle_diags_free(&post);
    return true;
  }
  GraphOracleDiags pre = {0};
  char *root = z_program_graph_store_root_for_input(input);
  char *store_path = root ? z_program_graph_store_path_for_root(root) : NULL;
  if (store_path && z_program_graph_store_path_exists(store_path)) {
    ZProgramGraphStore base;
    ZDiag base_diag = {0};
    if (z_program_graph_store_load_path(store_path, &base, &base_diag)) {
      ZDiag pre_fatal = {0};
      (void)repository_graph_oracle_collect(load_command, target, &base.graph, input, input, false, &pre, &pre_fatal);
      free((char *)pre_fatal.path);
      z_program_graph_store_free(&base);
    }
  }
  free(store_path);
  free(root);
  repository_graph_oracle_diff(&post, &pre, introduced, preexisting);
  graph_oracle_diags_free(&pre);
  graph_oracle_diags_free(&post);
  return true;
}

static int repository_graph_report_introduced(const Command *print_command, const char *input, const char *verb, const GraphOracleDiags *introduced) {
  print_command_diag_list(print_command, input, introduced);
  if (!(print_command && print_command->json)) {
    fprintf(stderr, "zero %s failed validation: %zu diagnostic%s introduced by the edited source projection\n", verb, introduced->len, introduced->len == 1 ? "" : "s");
  }
  return 1;
}

/*
 * Validation failure path for import/refresh when the compiler typecheck
 * rejected the edited source: report every diagnostic in one shot when the
 * edit introduced them, or skip with a note when the failure predates the
 * operation (the pointed-at file is unchanged since the last sync).
 */
static int repository_graph_handle_checker_failure(const Command *print_command, const Command *load_command, const ZTargetInfo *target, const char *verb, const char *input, const ZDiag *check_diag, ZProgramGraph *source_graph) {
  bool checker_preexisting = repository_graph_checker_diag_is_preexisting(input, check_diag);
  GraphOracleDiags introduced = {0};
  GraphOracleDiags preexisting = {0};
  ZDiag fatal = {0};
  if (!repository_graph_source_oracle_diff(load_command, target, source_graph, input, &introduced, &preexisting, &fatal)) {
    print_command_diag(print_command, fatal.path ? fatal.path : input, &fatal);
    free((char *)fatal.path);
    return 1;
  }
  int rc = 0;
  if (!checker_preexisting) {
    GraphOracleDiags combined = {0};
    graph_oracle_diags_add(&combined, check_diag, input, check_diag->actual);
    for (size_t i = 0; i < introduced.len; i++) {
      const ZDiag *diag = &introduced.items[i];
      bool duplicate = diag->code == check_diag->code && diag->line == check_diag->line &&
                       graph_check_text_eq(graph_oracle_path_tail(diag->path), graph_oracle_path_tail(check_diag->path));
      if (!duplicate) graph_oracle_diags_add(&combined, diag, input, NULL);
    }
    rc = repository_graph_report_introduced(print_command, input, verb, &combined);
    graph_oracle_diags_free(&combined);
  } else if (introduced.len > 0) {
    rc = repository_graph_report_introduced(print_command, input, verb, &introduced);
  } else {
    char facts[64];
    snprintf(facts, sizeof(facts), "%d:%d:%d", check_diag->code, check_diag->line, check_diag->column);
    uint64_t context = note_dedup_fold(note_dedup_fold(note_dedup_fold(NOTE_DEDUP_SEED, check_diag->path ? check_diag->path : ""), facts), check_diag->message);
    if (note_dedup_should_print(input, "preexisting-skip", context)) {
      fprintf(stderr, "note: a pre-existing diagnostic predates this %s and did not block it (the file is unchanged since the last sync): %s:%d:%d %s: %s\n",
              verb, check_diag->path ? check_diag->path : input, check_diag->line, check_diag->column, diag_code(check_diag->code), check_diag->message);
    }
  }
  print_preexisting_diag_notes(verb, input, &preexisting);
  graph_oracle_diags_free(&introduced);
  graph_oracle_diags_free(&preexisting);
  return rc;
}

/*
 * Loads and validates the source projection for zero import and the
 * stale-store auto-refresh. The compiler typecheck builds the checked
 * source graph (so stored graphs keep evaluated meta facts); its failure
 * is skipped with a note when it predates the operation, in which case the
 * graph comes from a parse-only load. The zero check oracle then runs with
 * baseline-diff semantics so only diagnostics the edited source introduced
 * fail the operation, all reported in one shot. Returns an exit code and
 * fills input/program/graph on success.
 */
static int load_and_validate_repository_graph_source(const Command *print_command, const Command *load_command, const ZTargetInfo *target, const char *verb, SourceInput *source_input, Program *source_program, ZProgramGraph *source_graph) {
  const char *input = load_command && load_command->input ? load_command->input : ".";
  memset(source_input, 0, sizeof(*source_input));
  memset(source_program, 0, sizeof(*source_program));
  memset(source_graph, 0, sizeof(*source_graph));
  ZDiag check_diag = {0};
  if (!compile_input(input, target, "release", source_input, source_program, &check_diag)) {
    z_free_program(source_program);
    z_free_source(source_input);
    memset(source_input, 0, sizeof(*source_input));
    memset(source_program, 0, sizeof(*source_program));
    ZDiag parse_diag = {0};
    if (!load_graph_from_current_source(load_command, target, source_input, source_program, source_graph, NULL, &parse_diag)) {
      print_command_diag(print_command, parse_diag.path ? parse_diag.path : input, &parse_diag);
      return 1;
    }
    return repository_graph_handle_checker_failure(print_command, load_command, target, verb, input, &check_diag, source_graph);
  }
  if (!graph_build_from_source_program(source_input, source_program, source_input->canonical_text_source, source_graph, &check_diag)) {
    print_command_diag(print_command, check_diag.path ? check_diag.path : input, &check_diag);
    return 1;
  }
  GraphOracleDiags introduced = {0};
  GraphOracleDiags preexisting = {0};
  ZDiag fatal = {0};
  if (!repository_graph_source_oracle_diff(load_command, target, source_graph, input, &introduced, &preexisting, &fatal)) {
    print_command_diag(print_command, fatal.path ? fatal.path : input, &fatal);
    free((char *)fatal.path);
    return 1;
  }
  int rc = 0;
  if (introduced.len > 0) rc = repository_graph_report_introduced(print_command, input, verb, &introduced);
  print_preexisting_diag_notes(verb, input, &preexisting);
  graph_oracle_diags_free(&introduced);
  graph_oracle_diags_free(&preexisting);
  return rc;
}

static void append_graph_check_json(ZBuf *buf, const Command *command, const ZTargetInfo *target, const SourceInput *input, const ZProgramGraph *graph, bool ok, const ZDiag *diag, const char *phase, const char *target_readiness_json, long long lower_ms, bool graph_mir_used) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(buf, ok ? "true" : "false");
  zbuf_append(buf, ",\n  \"artifact\": ");
  append_json_string(buf, command->input);
  zbuf_appendf(buf, ",\n  \"canonicalSource\": %s,\n  \"moduleIdentity\": ", graph && graph->canonical_source ? "true" : "false");
  append_json_string(buf, graph ? graph->module_identity : "");
  zbuf_append(buf, ",\n  \"graphHash\": ");
  append_json_string(buf, graph ? graph->graph_hash : "");
  zbuf_append(buf, ",\n  \"check\": {\"ok\": ");
  zbuf_append(buf, ok ? "true" : "false");
  zbuf_append(buf, ", \"phase\": ");
  append_json_string(buf, phase ? phase : (ok ? "typecheck" : "unknown"));
  zbuf_append(buf, ", \"target\": "); append_json_string(buf, target && target->name ? target->name : "unknown");
  zbuf_append(buf, ", \"lowering\": \"graph-native-check\", \"sourcePath\": null");
  zbuf_append(buf, "},\n  \"targetReadiness\": ");
  zbuf_append(buf, target_readiness_json ? target_readiness_json : "null");
  zbuf_append(buf, ",\n  \"graphCompiler\": {\"schemaVersion\":1,\"input\":\"program-graph-artifact\",\"graphNativeCheckerUsed\":true,\"graphHirToMirUsed\":");
  zbuf_append(buf, graph_mir_used ? "true" : "false"); zbuf_appendf(buf, ",\"timings\":{\"lowerMs\":%lld},\"semanticFacts\":", lower_ms);
  z_program_graph_append_semantics_json(buf, graph); zbuf_append(buf, "}");
  zbuf_append(buf, ",\n  \"safetyFacts\": ");
  append_safety_facts_json(buf, command && command->profile ? command->profile : "release");
  zbuf_append(buf, ",\n  \"compileTime\": ");
  append_compile_time_json(buf, NULL, input, target);
  zbuf_append(buf, ",\n  \"diagnostics\": [");
  if (!ok && diag) append_fix_plan_diagnostic(buf, diag->path ? diag->path : graph_check_diagnostic_path(command), diag);
  zbuf_append(buf, "],\n  \"saved\": "); append_graph_saved_json(buf, NULL); zbuf_append(buf, ",\n  \"view\": null\n}\n");
}

static void append_graph_patch_diagnostic_json(ZBuf *buf, const ZProgramGraphPatchResult *result) {
  zbuf_append(buf, "{\"code\": ");
  append_json_string(buf, result ? result->code : "GPH000");
  zbuf_append(buf, ", \"message\": ");
  append_json_string(buf, result ? result->message : "program graph patch failed");
  zbuf_append(buf, ", \"line\": ");
  if (result && result->line > 0) zbuf_appendf(buf, "%d", result->line);
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"expected\": ");
  append_json_nullable_string(buf, result ? result->expected : NULL);
  zbuf_append(buf, ", \"actual\": ");
  append_json_nullable_string(buf, result ? result->actual : NULL);
  zbuf_append(buf, "}");
}

static void append_graph_patch_operation_json(ZBuf *buf, const ZProgramGraphPatchOpResult *op) {
  zbuf_append(buf, "{\"index\": ");
  zbuf_appendf(buf, "%zu", op ? op->index : 0);
  zbuf_append(buf, ", \"line\": ");
  zbuf_appendf(buf, "%d", op ? op->line : 0);
  zbuf_append(buf, ", \"op\": ");
  append_json_string(buf, op ? op->op : "");
  zbuf_append(buf, ", \"ok\": ");
  zbuf_append(buf, op && op->ok ? "true" : "false");
  zbuf_append(buf, ", \"node\": ");
  append_json_nullable_string(buf, op ? op->node : NULL);
  zbuf_append(buf, ", \"parent\": ");
  append_json_nullable_string(buf, op ? op->parent : NULL);
  zbuf_append(buf, ", \"from\": ");
  append_json_nullable_string(buf, op ? op->from : NULL);
  zbuf_append(buf, ", \"to\": ");
  append_json_nullable_string(buf, op ? op->to : NULL);
  zbuf_append(buf, ", \"edge\": ");
  append_json_nullable_string(buf, op ? op->edge : NULL);
  zbuf_append(buf, ", \"kind\": ");
  append_json_nullable_string(buf, op ? op->kind : NULL);
  zbuf_append(buf, ", \"target\": ");
  append_json_nullable_string(buf, op ? op->target : NULL);
  zbuf_append(buf, ", \"order\": ");
  if (op && op->has_order) zbuf_appendf(buf, "%zu", op->order);
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"field\": ");
  append_json_nullable_string(buf, op ? op->field : NULL);
  zbuf_append(buf, ", \"expected\": ");
  append_json_nullable_string(buf, op && op->has_expected ? op->expected : NULL);
  zbuf_append(buf, ", \"actual\": ");
  append_json_nullable_string(buf, op ? op->actual : NULL);
  zbuf_append(buf, ", \"value\": ");
  append_json_nullable_string(buf, op ? op->value : NULL);
  zbuf_append(buf, ", \"name\": ");
  append_json_nullable_string(buf, op ? op->name : NULL);
  zbuf_append(buf, ", \"type\": ");
  append_json_nullable_string(buf, op ? op->type : NULL);
  zbuf_append(buf, ", \"path\": ");
  append_json_nullable_string(buf, op ? op->path : NULL);
  zbuf_append(buf, ", \"function\": ");
  append_json_nullable_string(buf, op ? op->function : NULL);
  zbuf_append(buf, ", \"left\": ");
  append_json_nullable_string(buf, op ? op->left : NULL);
  zbuf_append(buf, ", \"right\": ");
  append_json_nullable_string(buf, op ? op->right : NULL);
  zbuf_append(buf, ", \"arg0\": ");
  append_json_nullable_string(buf, op ? op->arg0 : NULL);
  zbuf_append(buf, ", \"arg1\": ");
  append_json_nullable_string(buf, op ? op->arg1 : NULL);
  zbuf_append(buf, ", \"call\": ");
  append_json_nullable_string(buf, op ? op->call : NULL);
  zbuf_append(buf, ", \"lineValue\": ");
  if (op && op->has_line_value) zbuf_appendf(buf, "%d", op->line_value);
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"columnValue\": ");
  if (op && op->has_column_value) zbuf_appendf(buf, "%d", op->column_value);
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"public\": ");
  if (op && op->has_public_value) zbuf_append(buf, op->public_value ? "true" : "false");
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"mutable\": ");
  if (op && op->has_mutable_value) zbuf_append(buf, op->mutable_value ? "true" : "false");
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"static\": ");
  if (op && op->has_static_value) zbuf_append(buf, op->static_value ? "true" : "false");
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"fallible\": ");
  if (op && op->has_fallible_value) zbuf_append(buf, op->fallible_value ? "true" : "false");
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"exportC\": ");
  if (op && op->has_export_c_value) zbuf_append(buf, op->export_c_value ? "true" : "false");
  else zbuf_append(buf, "null");
  zbuf_append(buf, ", \"callSites\": ");
  if (op && op->has_call_sites) zbuf_appendf(buf, "%zu", op->call_sites_updated);
  else zbuf_append(buf, "null");
  if (op && !op->ok && op->code[0]) {
    zbuf_append(buf, ", \"code\": ");
    append_json_string(buf, op->code);
    zbuf_append(buf, ", \"message\": ");
    append_json_string(buf, op->message);
  }
  zbuf_append(buf, "}");
}

static void append_graph_patch_symbols_json(ZBuf *buf, const ZProgramGraph *graph) {
  zbuf_append(buf, "{\"functions\":[");
  bool first_function = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || (node->value && node->value[0])) continue;
    if (!first_function) zbuf_append(buf, ",");
    append_json_string(buf, node->name ? node->name : "");
    first_function = false;
  }
  zbuf_append(buf, "],\"tests\":[");
  bool first_test = true;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    if (node->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || !node->value || !node->value[0]) continue;
    if (!first_test) zbuf_append(buf, ",");
    append_json_string(buf, node->value);
    first_test = false;
  }
  zbuf_append(buf, "]}");
}

typedef struct {
  char *id;
  char *hash;
  bool matched;
} GraphPatchNodeBaseline;

static int graph_patch_node_baseline_cmp(const void *left, const void *right) {
  return strcmp(((const GraphPatchNodeBaseline *)left)->id, ((const GraphPatchNodeBaseline *)right)->id);
}

static GraphPatchNodeBaseline *graph_patch_snapshot_baseline(const ZProgramGraph *graph, size_t *out_len) {
  size_t len = graph ? graph->node_len : 0;
  *out_len = len;
  if (len == 0) return NULL;
  GraphPatchNodeBaseline *items = z_checked_reallocarray(NULL, len, sizeof(GraphPatchNodeBaseline));
  for (size_t i = 0; i < len; i++) {
    items[i].id = z_strdup(graph->nodes[i].id ? graph->nodes[i].id : "");
    items[i].hash = z_strdup(graph->nodes[i].node_hash ? graph->nodes[i].node_hash : "");
    items[i].matched = false;
  }
  qsort(items, len, sizeof(GraphPatchNodeBaseline), graph_patch_node_baseline_cmp);
  return items;
}

static GraphPatchNodeBaseline *graph_patch_baseline_find(GraphPatchNodeBaseline *items, size_t len, const char *id) {
  size_t lo = 0;
  size_t hi = len;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int cmp = strcmp(items[mid].id, id);
    if (cmp == 0) return &items[mid];
    if (cmp < 0) lo = mid + 1;
    else hi = mid;
  }
  return NULL;
}

static size_t graph_patch_nodes_touched(GraphPatchNodeBaseline *baseline, size_t baseline_len, const ZProgramGraph *graph) {
  size_t touched = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *node = &graph->nodes[i];
    GraphPatchNodeBaseline *match = graph_patch_baseline_find(baseline, baseline_len, node->id ? node->id : "");
    if (!match) {
      touched++;
      continue;
    }
    match->matched = true;
    if (strcmp(match->hash, node->node_hash ? node->node_hash : "") != 0) touched++;
  }
  for (size_t i = 0; i < baseline_len; i++) {
    if (!baseline[i].matched) touched++;
  }
  return touched;
}

static void graph_patch_baseline_free(GraphPatchNodeBaseline *items, size_t len) {
  for (size_t i = 0; i < len; i++) {
    free(items[i].id);
    free(items[i].hash);
  }
  free(items);
}

static void print_graph_patch_summary_text(const ZProgramGraph *graph, const ZProgramGraphPatchResult *result, size_t nodes_touched) {
  printf("graphHash: %s\n", graph && graph->graph_hash ? graph->graph_hash : "");
  size_t ops = result ? result->operation_len : 0;
  printf("applied: %zu %s, %zu %s touched\n", ops, ops == 1 ? "op" : "ops", nodes_touched, nodes_touched == 1 ? "node" : "nodes");
  for (size_t i = 0; result && i < result->operation_len; i++) {
    const ZProgramGraphPatchOpResult *op = &result->operations[i];
    if (!op->has_call_sites) continue;
    printf("updated %zu call site%s\n", op->call_sites_updated, op->call_sites_updated == 1 ? "" : "s");
  }
}

static const char *graph_patch_source_label(const Command *command) {
  if (command && command->patch_file) return command->patch_file;
  if (command && command->patch_replace_in_fn) return "<replace-in-fn>";
  if (command && command->patch_body_file) return strcmp(command->patch_body_file, "-") == 0 ? "<stdin>" : command->patch_body_file;
  if (command && command->patch_text && strcmp(command->patch_text, "-") == 0) return "<stdin>";
  if (command && (command->patch_text || command->patch_op_len > 0)) return "<inline>";
  return "<patch>";
}

static bool graph_patch_expect_hash_valid(const char *hash) {
  if (!hash || strncmp(hash, "graph:", 6) != 0) return false;
  size_t hex_len = strlen(hash + 6);
  if (hex_len != 16) return false;
  for (const char *cursor = hash + 6; *cursor; cursor++) {
    if (!isxdigit((unsigned char)*cursor)) return false;
  }
  return true;
}

static bool graph_patch_expect_hash_ok(const Command *command, ZDiag *diag) {
  if (!command->patch_expect_graph_hash || graph_patch_expect_hash_valid(command->patch_expect_graph_hash)) return true;
  if (diag) {
    diag->code = 2002;
    diag->path = command->input;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "graph patch expected hash is invalid");
    snprintf(diag->expected, sizeof(diag->expected), "graph:<16 hex chars>");
    snprintf(diag->actual, sizeof(diag->actual), "%s", command->patch_expect_graph_hash ? command->patch_expect_graph_hash : "");
    snprintf(diag->help, sizeof(diag->help), "use the graph hash reported by zero dump or zero validate");
  }
  return false;
}

static char *graph_patch_build_inline_text(const Command *command, ZDiag *diag) {
  if (!graph_patch_expect_hash_ok(command, diag)) return NULL;
  ZBuf text;
  zbuf_init(&text);
  zbuf_append(&text, "zero-program-graph-patch v1\n");
  if (command->patch_expect_graph_hash) {
    zbuf_append(&text, "expect graphHash \"");
    zbuf_append(&text, command->patch_expect_graph_hash);
    zbuf_append(&text, "\"\n");
  }
  for (size_t i = 0; i < command->patch_op_len; i++) {
    zbuf_append(&text, command->patch_ops[i] ? command->patch_ops[i] : "");
    zbuf_append_char(&text, '\n');
  }
  return text.data ? text.data : z_strdup("");
}

static void append_graph_patch_json(
  ZBuf *buf,
  const Command *command,
  const ZProgramGraph *graph,
  const ZProgramGraphPatchResult *result,
  const char *original_hash,
  const char *saved_path
) {
  bool ok = result && result->ok;
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(buf, ok ? "true" : "false");
  zbuf_append(buf, ",\n  \"artifact\": ");
  append_json_string(buf, command->input);
  zbuf_append(buf, ",\n  \"patch\": ");
  append_json_string(buf, graph_patch_source_label(command));
  zbuf_appendf(buf, ",\n  \"checkOnly\": %s", command && command->graph_patch_check_only ? "true" : "false");
  zbuf_appendf(buf, ",\n  \"canonicalSource\": %s,\n  \"originalGraphHash\": ", graph && graph->canonical_source ? "true" : "false");
  append_json_string(buf, original_hash ? original_hash : "");
  zbuf_append(buf, ",\n  \"patchedGraphHash\": ");
  if (ok) append_json_string(buf, graph && graph->graph_hash ? graph->graph_hash : "");
  else zbuf_append(buf, "null");
  zbuf_appendf(buf, ",\n  \"operationCount\": %zu,\n  \"operations\": [", result ? result->operation_len : 0);
  for (size_t i = 0; result && i < result->operation_len; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_graph_patch_operation_json(buf, &result->operations[i]);
  }
  zbuf_append(buf, "],\n  \"summary\": ");
  if (ok) append_graph_patch_symbols_json(buf, graph);
  else zbuf_append(buf, "null");
  zbuf_append(buf, ",\n  \"diagnostic\": ");
  if (ok) zbuf_append(buf, "null");
  else append_graph_patch_diagnostic_json(buf, result);
  zbuf_append(buf, ",\n  \"saved\": ");
  if (ok && saved_path) {
    zbuf_append(buf, "{\"path\": ");
    append_json_string(buf, saved_path);
    zbuf_append(buf, ", \"byteStable\": true}");
  } else {
    zbuf_append(buf, "null");
  }
  zbuf_append(buf, "\n}\n");
}

static void append_graph_roundtrip_compare_json(ZBuf *buf, const ZProgramGraphCompare *comparison) {
  zbuf_append(buf, "{\"ok\":");
  zbuf_append(buf, comparison && comparison->ok ? "true" : "false");
  if (comparison && !comparison->ok) {
    zbuf_append(buf, ",\"code\":");
    append_json_string(buf, comparison->code);
    zbuf_append(buf, ",\"message\":");
    append_json_string(buf, comparison->message);
    zbuf_append(buf, ",\"field\":");
    append_json_string(buf, comparison->field);
    zbuf_appendf(buf, ",\"leftIndex\":%zu,\"rightIndex\":%zu,\"leftCount\":%zu,\"rightCount\":%zu",
                 comparison->left_index,
                 comparison->right_index,
                 comparison->left_count,
                 comparison->right_count);
  }
  zbuf_append_char(buf, '}');
}

static void append_graph_roundtrip_json(
  ZBuf *buf,
  const Command *command,
  const char *input_field,
  const char *input_value,
  const ZProgramGraph *original,
  const ZProgramGraph *roundtrip,
  const ZProgramGraphCompare *comparison,
  const char *lowering,
  const char *view,
  const char *saved_kind
) {
  bool ok = comparison && comparison->ok;
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(buf, ok ? "true" : "false");
  zbuf_append(buf, ",\n  \"");
  zbuf_append(buf, input_field && input_field[0] ? input_field : "sourceFile");
  zbuf_append(buf, "\": ");
  append_json_string(buf, input_value ? input_value : command->input);
  zbuf_appendf(buf, ",\n  \"canonicalSource\": %s,\n  \"semanticStable\": ", original && original->canonical_source ? "true" : "false");
  zbuf_append(buf, ok ? "true" : "false");
  zbuf_append(buf, ",\n  \"lowering\": ");
  append_json_string(buf, lowering && lowering[0] ? lowering : "direct-program-graph");
  zbuf_append(buf, ",\n  \"moduleIdentity\": ");
  append_json_string(buf, original ? original->module_identity : "");
  zbuf_append(buf, ",\n  \"roundtripModuleIdentity\": ");
  append_json_string(buf, roundtrip ? roundtrip->module_identity : "");
  zbuf_append(buf, ",\n  \"originalGraphHash\": ");
  append_json_string(buf, original ? original->graph_hash : "");
  zbuf_append(buf, ",\n  \"roundtripGraphHash\": ");
  append_json_string(buf, roundtrip ? roundtrip->graph_hash : "");
  zbuf_appendf(buf,
               ",\n  \"counts\": {\"original\": {\"nodes\": %zu, \"edges\": %zu}, \"roundtrip\": {\"nodes\": %zu, \"edges\": %zu}}",
               original ? original->node_len : 0,
               original ? original->edge_len : 0,
               roundtrip ? roundtrip->node_len : 0,
               roundtrip ? roundtrip->edge_len : 0);
  zbuf_appendf(buf,
               ",\n  \"semanticCounts\": {\"original\": {\"nodes\": %zu, \"edges\": %zu}, \"roundtrip\": {\"nodes\": %zu, \"edges\": %zu}}",
               comparison ? comparison->left_semantic_nodes : 0,
               comparison ? comparison->left_semantic_edges : 0,
               comparison ? comparison->right_semantic_nodes : 0,
               comparison ? comparison->right_semantic_edges : 0);
  zbuf_append(buf, ",\n  \"comparison\": ");
  append_graph_roundtrip_compare_json(buf, comparison);
  zbuf_append(buf, ",\n  \"saved\": ");
  if (command->out) {
    zbuf_append(buf, "{\"path\": ");
    append_json_string(buf, command->out);
    zbuf_append(buf, ", \"kind\": ");
    append_json_string(buf, saved_kind && saved_kind[0] ? saved_kind : "artifact");
    zbuf_append(buf, ", \"byteStable\": true}");
  } else {
    zbuf_append(buf, "null");
  }
  zbuf_append(buf, ",\n  \"view\": ");
  if (command->out || !view) zbuf_append(buf, "null");
  else append_json_string(buf, view);
  zbuf_append(buf, "\n}\n");
}

static int run_graph_validate_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  SourceInput input = {0};
  Program program = {0};
  ZProgramGraph graph = {0};
  GraphInputKind input_kind = GRAPH_INPUT_ARTIFACT;
  int rc = 1;
  if (!load_graph_input_for_read(command, target, &input, &program, &graph, &input_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    goto cleanup;
  }
  if (command->out) {
    if (reject_graph_source_text_out(command, "zero validate --out <program-graph-artifact> [graph-input]", diag)) {
      goto cleanup;
    }
    ZProgramGraphStoreFormat store_format = Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
    if (!command_repository_store_format(command, Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT, &store_format, diag)) {
      print_command_diag(command, diag->path ? diag->path : command->out, diag);
      goto cleanup;
    }
    if (!z_program_graph_save_format(command->out, &graph, store_format, diag)) {
      print_command_diag(command, diag->path ? diag->path : command->out, diag);
      goto cleanup;
    }
  }
  ZProgramGraphValidation validation = {0};
  z_program_graph_validate(&graph, &validation);
  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    append_graph_validate_json(&json, command, &graph, &validation);
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else {
    printf("program graph ok\n");
  }
  rc = 0;
cleanup:
  z_free_program(&program);
  z_free_source(&input);
  z_program_graph_free(&graph);
  (void)input_kind;
  return rc;
}

static int run_graph_dump_input_command(const Command *command, const ZTargetInfo *target, ZDiag *diag, bool graph_import) {
  if (command->out && reject_graph_source_text_out(command, graph_import ? "zero import --out <program-graph-artifact> [project|zero.toml|zero.json|file.0]" : "zero dump --out <program-graph-artifact> [graph-input]", diag)) {
    return 1;
  }
  SourceInput input = {0};
  Program program = {0};
  ZProgramGraph graph = {0};
  GraphInputKind input_kind = GRAPH_INPUT_ARTIFACT;
  if (!load_graph_input_for_read(command, target, &input, &program, &graph, &input_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }
  if (command->out) {
    ZProgramGraphStoreFormat store_format = Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
    if (!command_repository_store_format(command, Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT, &store_format, diag) ||
        !z_program_graph_save_format(command->out, &graph, store_format, diag)) {
      print_command_diag(command, diag->path ? diag->path : command->out, diag);
      z_free_program(&program);
      z_free_source(&input);
      z_program_graph_free(&graph);
      return 1;
    }
  }
  if (command->json) {
    ZProgramGraphValidation validation = {0};
    z_program_graph_validate(&graph, &validation);
    ZBuf json;
    zbuf_init(&json);
    if (graph_import) append_graph_import_json(&json, command, &input, &graph, &validation);
    else z_program_graph_append_json(&json, &graph, &validation);
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else if (!command->out) {
    ZBuf dump;
    zbuf_init(&dump);
    z_program_graph_append_dump(&dump, &graph, NULL);
    fputs(dump.data ? dump.data : "", stdout);
    zbuf_free(&dump);
  }
  z_free_program(&program);
  z_free_source(&input);
  z_program_graph_free(&graph);
  (void)input_kind;
  return 0;
}

static int run_graph_view_command(const Command *command, ZDiag *diag) {
  if (command->query_handles && (!command->query_function || command->view_around || command->view_outline)) {
    diag->code = 2002;
    diag->path = command->input;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "--handles requires --fn <name>%s", command->view_around || command->view_outline ? " without --around or --outline" : "");
    snprintf(diag->expected, sizeof(diag->expected), "zero view --fn <name> --handles [graph-input]");
    snprintf(diag->actual, sizeof(diag->actual), "--handles");
    snprintf(diag->help, sizeof(diag->help), "--handles prints one function's source with a trailing // #handle comment per statement; the handles are the node ids zero patch --op accepts");
    print_command_diag(command, command->input, diag);
    return 1;
  }
  if (command->view_around && !command->query_function) {
    diag->code = 2002;
    diag->path = command->input;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "--around requires --fn <name>");
    snprintf(diag->expected, sizeof(diag->expected), "zero view --fn <name> --around <text> [graph-input]");
    snprintf(diag->actual, sizeof(diag->actual), "--around %s", command->view_around);
    snprintf(diag->help, sizeof(diag->help), "--around scopes one function's source to the enclosing block that contains the text; name the function with --fn");
    print_command_diag(command, command->input, diag);
    return 1;
  }
  if (command->view_outline && (command->query_function || command->out)) {
    diag->code = 2002;
    diag->path = command->input;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "--outline does not combine with %s", command->query_function ? "--fn" : "--out");
    snprintf(diag->expected, sizeof(diag->expected), "zero view --outline <module-or-file> [graph-input]");
    snprintf(diag->actual, sizeof(diag->actual), "--outline %s", command->view_outline);
    snprintf(diag->help, sizeof(diag->help), "outline prints signatures on stdout; use zero view --fn <name> for one function's source");
    print_command_diag(command, command->input, diag);
    return 1;
  }
  SourceInput input = {0};
  Program program = {0};
  ZProgramGraph graph = {0};
  GraphInputKind input_kind = GRAPH_INPUT_ARTIFACT;
  const ZTargetInfo *host_target = z_find_target(z_host_target());
  if (!load_graph_input_for_read(command, host_target, &input, &program, &graph, &input_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }
  if (command->out && !has_suffix(command->out, ".0")) {
    diag->code = 2002;
    diag->path = command->out;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "graph view output must use .0 extension");
    snprintf(diag->expected, sizeof(diag->expected), "zero view --out <file.0> [graph-input]");
    snprintf(diag->actual, sizeof(diag->actual), "%s", command->out);
    snprintf(diag->help, sizeof(diag->help), "graph view writes canonical source text");
    print_command_diag(command, command->out, diag);
    z_free_program(&program);
    z_free_source(&input);
    z_program_graph_free(&graph);
    return 1;
  }
  ZBuf view;
  zbuf_init(&view);
  bool view_ok;
  if (command->view_outline) {
    view_ok = z_program_graph_append_view_outline(&view, &graph, command->input, command->view_outline, diag);
  } else if (command->query_function && command->view_around) {
    view_ok = z_program_graph_append_view_function_around(&view, &graph, command->input, command->query_function, command->view_around, diag);
  } else if (command->query_function && command->query_handles) {
    view_ok = z_program_graph_append_view_function_handles(&view, &graph, command->input, command->query_function, diag);
  } else if (command->query_function) {
    view_ok = z_program_graph_append_view_function(&view, &graph, command->input, command->query_function, diag);
  } else {
    view_ok = z_program_graph_append_view(&view, &graph, command->input, diag);
  }
  if (!view_ok) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    zbuf_free(&view);
    z_free_program(&program);
    z_free_source(&input);
    z_program_graph_free(&graph);
    return 1;
  }
  if (command->out && !z_write_file(command->out, view.data ? view.data : "", diag)) {
    print_command_diag(command, diag->path ? diag->path : command->out, diag);
    zbuf_free(&view);
    z_free_program(&program);
    z_free_source(&input);
    z_program_graph_free(&graph);
    return 1;
  }
  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    append_graph_view_json(&json, command, &graph, view.data ? view.data : "");
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else if (!command->out) {
    fputs(view.data ? view.data : "", stdout);
  }
  zbuf_free(&view);
  z_free_program(&program);
  z_free_source(&input);
  z_program_graph_free(&graph);
  (void)input_kind;
  return 0;
}

static const char *graph_input_kind_name(GraphInputKind kind) {
  switch (kind) {
    case GRAPH_INPUT_ARTIFACT: return "program-graph-artifact";
    case GRAPH_INPUT_CANONICAL_SOURCE: return "canonical-source";
    case GRAPH_INPUT_CURRENT_SOURCE: return "source";
    case GRAPH_INPUT_REPOSITORY_STORE: return "repository-graph";
  }
  return "unknown";
}

static bool parse_graph_query_depth(const Command *command, ZProgramGraphQueryRequest *request, ZDiag *diag) {
  if (!command->query_depth) return true;
  char *end = NULL;
  unsigned long value = strtoul(command->query_depth, &end, 10);
  if (command->query_depth[0] && end && !*end && value >= 1 && value <= 16) {
    request->node_depth = (size_t)value;
    return true;
  }
  diag->code = 2002;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "invalid query depth '%s'", command->query_depth);
  snprintf(diag->expected, sizeof(diag->expected), "--depth <1-16>");
  snprintf(diag->actual, sizeof(diag->actual), "--depth %s", command->query_depth);
  snprintf(diag->help, sizeof(diag->help), "use --depth 1 for immediate children of --node <id>, larger values for deeper subtrees");
  return false;
}

static int run_graph_query_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  if (command->out) return reject_graph_unsupported_out(command, diag);
  ZProgramGraphQueryRequest request;
  z_program_graph_query_request_init(&request);
  request.function = command->query_function;
  request.find = command->query_find;
  request.refs = command->query_refs;
  request.calls = command->query_calls;
  request.node = command->query_node;
  request.full_module = command->query_full;
  request.handles = command->query_handles;
  request.no_help = command->query_no_help;
  request.bare_argument = command->query_bare_argument;
  if (!parse_graph_query_depth(command, &request, diag)) {
    print_command_diag(command, command->input, diag);
    return 1;
  }
  SourceInput input = {0};
  Program program = {0};
  ZProgramGraph graph = {0};
  GraphInputKind input_kind = GRAPH_INPUT_ARTIFACT;
  if (!load_graph_input_for_read(command, target, &input, &program, &graph, &input_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }
  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    const char *input_name = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
    z_program_graph_append_query_json(&json, &graph, input_name, command->input, graph_input_kind_name(input_kind), &request);
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else {
    const char *input_name = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
    z_program_graph_print_query_text(&graph, input_name, command->input, graph_input_kind_name(input_kind), &request);
  }
  z_free_program(&program);
  z_free_source(&input);
  z_program_graph_free(&graph);
  return 0;
}

static int run_graph_source_map_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  if (command->out) return reject_graph_unsupported_out(command, diag);

  SourceInput input = {0};
  Program program = {0};
  ZProgramGraph graph = {0};
  GraphInputKind input_kind = GRAPH_INPUT_ARTIFACT;
  if (!load_graph_input_for_checked_read(command, target, &input, &program, &graph, &input_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }

  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    z_program_graph_append_source_map_json(&json, &graph, command->input);
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else {
    printf("program graph source map ok: %zu mappings\n", z_program_graph_source_map_count(&graph));
  }

  z_free_program(&program);
  z_free_source(&input);
  z_program_graph_free(&graph);
  (void)input_kind;
  return 0;
}

static int run_graph_inspect_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  if (command->out) return reject_graph_unsupported_out(command, diag);

  SourceInput input = {0};
  Program program = {0};
  ZProgramGraph graph = {0};
  GraphInputKind input_kind = GRAPH_INPUT_ARTIFACT;
  if (!load_graph_input_for_read(command, target, &input, &program, &graph, &input_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }

  if (input_kind == GRAPH_INPUT_ARTIFACT || input_kind == GRAPH_INPUT_REPOSITORY_STORE) {
    z_free_program(&program);
    z_free_source(&input);
    const char *display_input = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
    if (!z_program_graph_lower_to_program_with_source(&graph, display_input, &program, &input, diag)) {
      print_command_diag(command, diag->path ? diag->path : display_input, diag);
      z_program_graph_free(&graph);
      return 1;
    }
    if (command->repository_graph_source_input && !z_program_graph_manifest_attach_metadata_to_input(&input, command->repository_graph_source_input, diag)) {
      print_command_diag(command, diag->path ? diag->path : command->repository_graph_source_input, diag);
      z_free_program(&program);
      z_free_source(&input);
      z_program_graph_free(&graph);
      return 1;
    }
  }

  Command inspect_command = *command;
  if (input_kind == GRAPH_INPUT_ARTIFACT || input_kind == GRAPH_INPUT_REPOSITORY_STORE) {
    inspect_command.graph_source = (ZProgramGraphArtifactSource){
      .artifact = command->input ? command->input : "",
      .graph_hash = graph.graph_hash ? graph.graph_hash : "",
      .module_identity = graph.module_identity ? graph.module_identity : "",
      .lowering = "direct-program-graph",
      .source_projection_state = NULL,
      .canonical_source = graph.canonical_source,
    };
  }

  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    append_graph_json(&json, &input, &program, target, &inspect_command);
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else {
    print_graph_inspect_text(&input, &program, target, &inspect_command);
  }

  z_free_program(&program);
  z_free_source(&input);
  z_program_graph_free(&graph);
  return 0;
}

static int run_graph_reconcile_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  if (command->out) return reject_graph_unsupported_out(command, diag);
  if (!command->reconcile_source) {
    diag->code = 2002;
    diag->path = command->input;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "graph reconcile requires edited source input");
    snprintf(diag->expected, sizeof(diag->expected), "zero reconcile <base-graph-input> --source <edited-file.0|project|zero.toml|zero.json>");
    snprintf(diag->actual, sizeof(diag->actual), "missing --source");
    snprintf(diag->help, sizeof(diag->help), "pass the edited source or package that should be reconciled against the base graph");
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }

  SourceInput base_input = {0};
  Program base_program = {0};
  ZProgramGraph base_graph = {0};
  GraphInputKind base_kind = GRAPH_INPUT_ARTIFACT;
  if (!load_graph_input_for_read(command, target, &base_input, &base_program, &base_graph, &base_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }

  char *reconcile_package_input = NULL;
  if (is_zero_source_path(command->reconcile_source) && direct_file_exists(command->reconcile_source)) {
    char *package_root = z_program_graph_store_root_for_input(command->reconcile_source);
    char *package_manifest = package_root ? z_manifest_path_for_root(package_root) : NULL;
    if (package_manifest) {
      reconcile_package_input = package_root;
      package_root = NULL;
    }
    free(package_manifest);
    free(package_root);
  }
  Command edited_command = *command;
  edited_command.input = reconcile_package_input ? reconcile_package_input : command->reconcile_source;
  edited_command.out = NULL;
  SourceInput edited_input = {0};
  Program edited_program = {0};
  ZProgramGraph edited_graph = {0};
  GraphInputKind edited_kind = GRAPH_INPUT_ARTIFACT;
  if (!load_graph_from_current_source(&edited_command, target, &edited_input, &edited_program, &edited_graph, &edited_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->reconcile_source, diag);
    z_free_program(&base_program);
    z_free_source(&base_input);
    z_program_graph_free(&base_graph);
    free(reconcile_package_input);
    return 1;
  }

  char *edited_normalize_root = z_program_graph_store_root_for_input(edited_command.input);
  if (edited_normalize_root) {
    char *edited_normalize_manifest = z_manifest_path_for_root(edited_normalize_root);
    if (edited_normalize_manifest) z_program_graph_store_normalize_source_graph(&edited_graph, edited_normalize_root);
    free(edited_normalize_manifest);
  }
  free(edited_normalize_root);

  ZProgramGraphIdentityReconcile identity = {0};
  (void)z_program_graph_preserve_source_node_ids(&base_graph, &edited_graph, &identity);
  ZProgramGraphReconcileSummary summary = {0};
  z_program_graph_reconcile_summary(&base_graph, &edited_graph, &summary);
  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    z_program_graph_append_reconcile_json(&json, &base_graph, &edited_graph, command->input, command->reconcile_source, &summary);
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else if (summary.ok) {
    printf("program graph reconcile ok\n");
  } else {
    fprintf(stderr,
            "program graph reconcile failed: %zu ambiguous, %zu identity changed%s\n",
            summary.ambiguous,
            summary.identity_changed,
            summary.module_identity_changed ? ", module identity changed" : "");
  }

  z_free_program(&edited_program);
  z_free_source(&edited_input);
  z_program_graph_free(&edited_graph);
  z_free_program(&base_program);
  z_free_source(&base_input);
  z_program_graph_free(&base_graph);
  free(reconcile_package_input);
  (void)base_kind;
  (void)edited_kind;
  return summary.ok ? 0 : 1;
}

static bool validate_graph_patch_sources(const Command *command, bool *has_file, bool *has_patch_text, bool *has_ops, ZDiag *diag) {
  *has_file = command->patch_file != NULL;
  *has_patch_text = command->patch_text != NULL;
  *has_ops = command->patch_op_len > 0;
  bool has_body = command->patch_replace_fn || command->patch_body_file;
  bool has_in_fn = command->patch_replace_in_fn || command->patch_old_text || command->patch_old_file || command->patch_new_text || command->patch_new_file;
  bool has_rewrite = command->patch_rewrite || command->patch_rewrite_to;
  diag->code = 2002;
  diag->path = command->input;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  if (has_body && (!command->patch_replace_fn || !command->patch_body_file)) {
    snprintf(diag->message, sizeof(diag->message), "graph patch body replacement needs both --replace-fn and --body-file");
    snprintf(diag->expected, sizeof(diag->expected), "zero patch [graph-input] --replace-fn <function> --body-file <body-rows-file|->");
    snprintf(diag->actual, sizeof(diag->actual), "missing %s", command->patch_replace_fn ? "--body-file" : "--replace-fn");
    snprintf(diag->help, sizeof(diag->help), "the body file holds only the new body rows, exactly what zero view --fn prints between the signature braces; --body-file - reads them from stdin");
    return false;
  }
  if (has_rewrite && (!command->patch_rewrite || !command->patch_rewrite_to)) {
    snprintf(diag->message, sizeof(diag->message), "graph rewrite needs both --rewrite and --to");
    snprintf(diag->expected, sizeof(diag->expected), "zero patch [graph-input] --rewrite '<pattern>' --to '<template>' [--fn <name>] [--apply]");
    snprintf(diag->actual, sizeof(diag->actual), "missing %s", command->patch_rewrite ? "--to" : "--rewrite");
    snprintf(diag->help, sizeof(diag->help), "pattern and template are canonical projection expressions; metavariables $A, $B bind arbitrary subtrees");
    return false;
  }
  if (has_in_fn) {
    bool has_old = command->patch_old_text || command->patch_old_file;
    bool has_new = command->patch_new_text || command->patch_new_file;
    if (!command->patch_replace_in_fn || !has_old || !has_new) {
      snprintf(diag->message, sizeof(diag->message), "graph patch in-function replacement needs --replace-in-fn, --old, and --new");
      snprintf(diag->expected, sizeof(diag->expected), "zero patch [graph-input] --replace-in-fn <function> --old <text> --new <text>");
      snprintf(diag->actual, sizeof(diag->actual), "missing %s", !command->patch_replace_in_fn ? "--replace-in-fn" : (!has_old ? "--old" : "--new"));
      snprintf(diag->help, sizeof(diag->help), "--old must match the body text zero view --fn <name> prints exactly once; inline --old/--new accept \\n escapes, or pass --old-file/--new-file <file|-> for multi-line text");
      return false;
    }
    if ((command->patch_old_text && command->patch_old_file) || (command->patch_new_text && command->patch_new_file)) {
      snprintf(diag->message, sizeof(diag->message), "graph patch in-function replacement text is ambiguous");
      snprintf(diag->expected, sizeof(diag->expected), "one of --old or --old-file, and one of --new or --new-file");
      snprintf(diag->actual, sizeof(diag->actual), "%s", command->patch_old_text && command->patch_old_file ? "--old and --old-file" : "--new and --new-file");
      snprintf(diag->help, sizeof(diag->help), "pass the text inline or in a file, not both");
      return false;
    }
    if (command->patch_old_file && command->patch_new_file && strcmp(command->patch_old_file, "-") == 0 && strcmp(command->patch_new_file, "-") == 0) {
      snprintf(diag->message, sizeof(diag->message), "graph patch in-function replacement cannot read both texts from stdin");
      snprintf(diag->expected, sizeof(diag->expected), "at most one of --old-file and --new-file as -");
      snprintf(diag->actual, sizeof(diag->actual), "--old-file - --new-file -");
      snprintf(diag->help, sizeof(diag->help), "pass one of the texts inline or in a real file");
      return false;
    }
  }
  int source_count = (*has_file ? 1 : 0) + (*has_patch_text ? 1 : 0) + (*has_ops ? 1 : 0) + (has_body ? 1 : 0) + (has_in_fn ? 1 : 0) + (has_rewrite ? 1 : 0);
  if (source_count == 0) {
    snprintf(diag->message, sizeof(diag->message), "graph patch requires patch operations");
    snprintf(diag->expected, sizeof(diag->expected), "one patch source: file, --op, --patch-text, --replace-fn, --replace-in-fn, or --rewrite");
    snprintf(diag->actual, sizeof(diag->actual), "missing patch input");
    snprintf(diag->help, sizeof(diag->help), "pass a zero-program-graph-patch v1 file, one or more --op lines, --replace-fn with --body-file, or --replace-in-fn with --old and --new");
    return false;
  }
  if (source_count > 1 || (command->patch_expect_graph_hash && !*has_ops && !has_body && !has_in_fn)) {
    snprintf(diag->message, sizeof(diag->message), "graph patch source is ambiguous");
    snprintf(diag->expected, sizeof(diag->expected), "one patch source: <patch-file>, --patch-text, --op, --replace-fn with --body-file, or --replace-in-fn with --old and --new");
    snprintf(diag->actual, sizeof(diag->actual), "%s", graph_patch_source_label(command));
    snprintf(diag->help, sizeof(diag->help), "use --expect-graph-hash with --op, --replace-fn, or --replace-in-fn, or put the precondition in patch text");
    return false;
  }
  diag->code = 0;
  diag->path = NULL;
  return true;
}

static bool apply_graph_patch_source(const Command *command, bool has_file, bool has_patch_text, const char *rewrite_text, ZProgramGraph *graph, ZProgramGraphPatchResult *result, char **inline_text, ZDiag *diag) {
  if (rewrite_text) return z_program_graph_apply_patch_text("<rewrite>", rewrite_text, strlen(rewrite_text), graph, result, diag);
  if (has_file) return z_program_graph_apply_patch_file(command->patch_file, graph, result, diag);
  if (has_patch_text) {
    if (cli_arg_is(command->patch_text, "-")) {
      size_t text_len = 0;
      *inline_text = z_graph_patch_read_patch_text_source(&text_len, diag);
      return *inline_text && z_program_graph_apply_patch_text("<stdin>", *inline_text, text_len, graph, result, diag);
    }
    return z_program_graph_apply_patch_text("<inline>", command->patch_text, strlen(command->patch_text), graph, result, diag);
  }
  if (command->patch_replace_in_fn) {
    return graph_patch_expect_hash_ok(command, diag) &&
           z_program_graph_apply_replace_in_fn(command->patch_replace_in_fn, command->patch_old_text, command->patch_old_file, command->patch_new_text, command->patch_new_file, command->patch_expect_graph_hash, graph, result, diag);
  }
  if (command->patch_body_file) {
    return graph_patch_expect_hash_ok(command, diag) &&
           z_program_graph_apply_replace_fn_body_file(command->patch_replace_fn, command->patch_body_file, command->patch_expect_graph_hash, graph, result, diag);
  }
  *inline_text = graph_patch_build_inline_text(command, diag);
  return *inline_text && z_program_graph_apply_patch_text("<inline>", *inline_text, strlen(*inline_text), graph, result, diag);
}

static bool graph_patch_help_requested(const Command *command) {
  return command &&
         command->patch_op_len == 1 &&
         command->patch_ops &&
         (cli_arg_is(command->patch_ops[0], "help") || cli_arg_is(command->patch_ops[0], "?"));
}

static void print_graph_patch_help_json(void) {
  ZBuf json;
  zbuf_init(&json);
  zbuf_append(&json, "{\"schemaVersion\":1,\"ok\":true,\"command\":\"zero patch\",\"operations\":[");
  const char *const *ops = z_program_graph_patch_operation_examples();
  for (size_t i = 0; ops[i]; i++) {
    if (i > 0) zbuf_append(&json, ",");
    append_json_string(&json, ops[i]);
  }
  zbuf_append(&json, "]}\n");
  fputs(json.data, stdout);
  zbuf_free(&json);
}

static bool save_graph_patch_output(const Command *command, const ZTargetInfo *target, ZProgramGraph *graph, GraphInputKind input_kind, const SourceInput *input, const char **saved_path, ZDiag *diag) {
  (void)input;
  (void)target;
  *saved_path = NULL;
  bool repository_backed = input_kind == GRAPH_INPUT_REPOSITORY_STORE;
  if (repository_backed) {
    if (command->out) {
      diag->code = 2002;
      diag->path = command->out;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "repository graph patch writes the input zero.graph store");
      snprintf(diag->expected, sizeof(diag->expected), "zero patch <package|zero.toml|zero.json> (<patch-file>|--op <operation>)");
      snprintf(diag->actual, sizeof(diag->actual), "%s", command->out);
      snprintf(diag->help, sizeof(diag->help), "omit --out when patching a repository graph compiler input");
      return false;
    }
    if (command->graph_patch_check_only) return true;
    ZProgramGraphStoreFormat fallback_format = z_program_graph_store_path_is_binary(command->input)
      ? Z_PROGRAM_GRAPH_STORE_FORMAT_BINARY
      : Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
    ZProgramGraphStoreFormat store_format = fallback_format;
    if (!command_repository_store_format(command, fallback_format, &store_format, diag)) return false;
    if (!z_program_graph_store_write_generated_path_format(command->input, graph, store_format, NULL, diag)) return false;
    *saved_path = command->input;
    return true;
  }
  if (!command->out) return true;
  if (command->graph_patch_check_only) return true;
  if (command->out && z_program_graph_path_is_source_text(command->out)) {
    diag->code = 2002;
    diag->path = command->out;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "program graph output must not use source text extension");
    snprintf(diag->expected, sizeof(diag->expected), "zero patch --out <program-graph-artifact> <program-graph-artifact> (<patch-file>|--op <operation>)");
    snprintf(diag->actual, sizeof(diag->actual), "%s", command->out);
    snprintf(diag->help, sizeof(diag->help), ".0 files are canonical source text; write derived ProgramGraph artifacts to a non-source path");
    return false;
  }
  ZProgramGraphStoreFormat store_format = Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
  if (!command_repository_store_format(command, Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT, &store_format, diag)) return false;
  if (!z_program_graph_save_format(command->out, graph, store_format, diag)) return false;
  *saved_path = command->out;
  return true;
}

static void free_graph_patch_state(char *inline_text, ZProgramGraphPatchResult *result, char *original_hash, Program *program, SourceInput *input, ZProgramGraph *graph) {
  free(inline_text);
  z_program_graph_patch_result_free(result);
  free(original_hash);
  z_free_program(program);
  z_free_source(input);
  z_program_graph_free(graph);
}

static void append_json_escaped_or_null(ZBuf *buf, const char *value) {
  if (!value) {
    zbuf_append(buf, "null");
    return;
  }
  append_json_string(buf, value);
}

/* Dry-run (and pre-apply) report for zero patch --rewrite. */
static void print_graph_rewrite_report(const Command *command, const ZProgramGraphRewriteResult *rewrite) {
  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    zbuf_append(&json, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"rewrite\": {\"pattern\": ");
    append_json_escaped_or_null(&json, command->patch_rewrite);
    zbuf_append(&json, ", \"to\": ");
    append_json_escaped_or_null(&json, command->patch_rewrite_to);
    zbuf_append(&json, ", \"fn\": ");
    append_json_escaped_or_null(&json, command->query_function);
    zbuf_appendf(&json, ", \"apply\": %s},\n  \"matches\": [", command->apply ? "true" : "false");
    for (size_t i = 0; i < rewrite->len; i++) {
      const ZProgramGraphRewriteMatch *match = &rewrite->items[i];
      if (i > 0) zbuf_append(&json, ", ");
      zbuf_append(&json, "{\"node\": ");
      append_json_escaped_or_null(&json, match->node_id);
      zbuf_append(&json, ", \"handle\": ");
      append_json_escaped_or_null(&json, match->short_handle);
      zbuf_append(&json, ", \"fn\": ");
      append_json_escaped_or_null(&json, match->function_name);
      zbuf_append(&json, ", \"path\": ");
      append_json_escaped_or_null(&json, match->path);
      zbuf_append(&json, ", \"before\": ");
      append_json_escaped_or_null(&json, match->before);
      zbuf_append(&json, ", \"after\": ");
      append_json_escaped_or_null(&json, match->after);
      zbuf_append(&json, "}");
    }
    zbuf_appendf(&json, "],\n  \"matchCount\": %zu,\n  \"skippedSubtrees\": %zu\n}\n", rewrite->len, rewrite->skipped_unsupported);
    fputs(json.data, stdout);
    zbuf_free(&json);
    return;
  }
  printf("rewrite: %s -> %s\n", command->patch_rewrite ? command->patch_rewrite : "", command->patch_rewrite_to ? command->patch_rewrite_to : "");
  if (command->query_function) printf("scope: fn %s\n", command->query_function);
  printf("matches: %zu\n", rewrite->len);
  for (size_t i = 0; i < rewrite->len; i++) {
    const ZProgramGraphRewriteMatch *match = &rewrite->items[i];
    printf("  %s fn:%s %s\n", match->path && match->path[0] ? match->path : "<graph>", match->function_name && match->function_name[0] ? match->function_name : "?", match->short_handle);
    printf("    - %s\n", match->before);
    printf("    + %s\n", match->after);
  }
  if (rewrite->skipped_unsupported > 0) printf("%zu subtree%s skipped (unsupported kinds)\n", rewrite->skipped_unsupported, rewrite->skipped_unsupported == 1 ? "" : "s");
  if (!command->apply) {
    if (rewrite->len > 0) printf("dry run: pass --apply to rewrite %zu site%s in one batch\n", rewrite->len, rewrite->len == 1 ? "" : "s");
    else printf("dry run: no matches\n");
  }
}

static void print_graph_patch_failure_text(const Command *command, const ZProgramGraphPatchResult *result) {
  fprintf(stderr, "program graph patch failed: %s\n", result->message);
  if (result->line > 0 && result->actual && result->actual[0]) {
    fprintf(stderr, "  %s:%d: %s\n", graph_patch_source_label(command), result->line, result->actual);
  }
  if (result->expected && result->expected[0]) fprintf(stderr, "  expected: %s\n", result->expected);
  if (result->line <= 0 && result->actual && result->actual[0]) fprintf(stderr, "  actual: %s\n", result->actual);
  if (command->patch_replace_in_fn && (strcmp(result->code, "GPH003") == 0 || strcmp(result->code, "GPH004") == 0)) {
    fprintf(stderr, "  help: run zero view --fn %s to see the exact body text to match\n", command->patch_replace_in_fn);
  } else if (strcmp(result->code, "GPH003") == 0 || strcmp(result->code, "GPH004") == 0) {
    fprintf(stderr, "  help: run zero query --fn <name> --handles to list stmt and param patch handles, or zero query --find <text> for node ids\n");
  }
  if (result->format_error) {
    fprintf(stderr, "  help: a minimal complete patch file looks exactly like this:\n");
    fputs(z_program_graph_patch_minimal_file_example(), stderr);
  }
}

/* Runs --rewrite matching before the patch pipeline. Returns -1 to continue
 * with *rewrite_text set for apply, or an exit code when the command is done
 * (dry run, no matches, or pattern errors). */
static int prepare_graph_patch_rewrite(const Command *command, const ZProgramGraph *graph, char **rewrite_text, ZDiag *diag) {
  *rewrite_text = NULL;
  if (!command->patch_rewrite) return -1;
  ZProgramGraphRewriteResult rewrite = {0};
  if (!z_program_graph_rewrite_collect(graph, command->patch_rewrite, command->patch_rewrite_to, command->query_function, &rewrite, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    z_program_graph_rewrite_result_free(&rewrite);
    return 1;
  }
  if (!command->apply) {
    print_graph_rewrite_report(command, &rewrite);
    z_program_graph_rewrite_result_free(&rewrite);
    return 0;
  }
  if (rewrite.len == 0) {
    if (command->json) print_graph_rewrite_report(command, &rewrite);
    else fprintf(stderr, "graph rewrite matched 0 sites; store unchanged\n");
    z_program_graph_rewrite_result_free(&rewrite);
    return command->json ? 0 : 1;
  }
  if (!command->json) print_graph_rewrite_report(command, &rewrite);
  *rewrite_text = z_program_graph_rewrite_build_patch_text(&rewrite);
  z_program_graph_rewrite_result_free(&rewrite);
  return -1;
}

static int run_graph_patch_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  if (graph_patch_help_requested(command)) {
    if (command->json) print_graph_patch_help_json();
    else z_cli_print_graph_patch_help_text();
    return 0;
  }
  bool has_file = command->patch_file != NULL;
  bool has_patch_text = command->patch_text != NULL;
  bool has_ops = command->patch_op_len > 0;
  if (!validate_graph_patch_sources(command, &has_file, &has_patch_text, &has_ops, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }

  SourceInput input = {0};
  Program program = {0};
  ZProgramGraph graph = {0};
  GraphInputKind input_kind = GRAPH_INPUT_ARTIFACT;
  if (!load_graph_input_for_patch(command, &input, &program, &graph, &input_kind, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }

  char *rewrite_text = NULL;
  int rewrite_rc = prepare_graph_patch_rewrite(command, &graph, &rewrite_text, diag);
  if (rewrite_rc >= 0) {
    z_free_program(&program);
    z_free_source(&input);
    z_program_graph_free(&graph);
    return rewrite_rc;
  }

  char *original_hash = z_strdup(graph.graph_hash ? graph.graph_hash : "");
  size_t baseline_len = 0;
  GraphPatchNodeBaseline *baseline = command->json ? NULL : graph_patch_snapshot_baseline(&graph, &baseline_len);
  ZProgramGraphPatchResult result = {0};
  char *inline_text = NULL;
  bool ok = apply_graph_patch_source(command, has_file, has_patch_text, rewrite_text, &graph, &result, &inline_text, diag);
  free(rewrite_text);
  if (!ok && !result.message[0] && diag->code != 0) {
    print_command_diag(command, diag->path ? diag->path : graph_patch_source_label(command), diag);
    graph_patch_baseline_free(baseline, baseline_len);
    free_graph_patch_state(inline_text, &result, original_hash, &program, &input, &graph);
    return 1;
  }
  if (ok && input_kind == GRAPH_INPUT_REPOSITORY_STORE) {
    GraphOracleDiags introduced = {0};
    GraphOracleDiags preexisting = {0};
    ZDiag reval_fatal = {0};
    bool revalidated = revalidate_repository_graph_patch_output(command, target, &graph, &introduced, &preexisting, &reval_fatal);
    if (!revalidated && introduced.len == 0) {
      print_command_diag(command, reval_fatal.path ? reval_fatal.path : command->input, &reval_fatal);
      free((char *)reval_fatal.path);
      graph_oracle_diags_free(&introduced);
      graph_oracle_diags_free(&preexisting);
      graph_patch_baseline_free(baseline, baseline_len);
      free_graph_patch_state(inline_text, &result, original_hash, &program, &input, &graph);
      return 1;
    }
    if (!revalidated) {
      print_command_diag_list(command, command->input, &introduced);
      if (!command->json) {
        fprintf(stderr, "program graph patch failed revalidation: %zu diagnostic%s introduced by this patch\n", introduced.len, introduced.len == 1 ? "" : "s");
      }
      print_graph_patch_revalidation_hints(command, &introduced);
      print_preexisting_diag_notes("patch", command->input, &preexisting);
      graph_oracle_diags_free(&introduced);
      graph_oracle_diags_free(&preexisting);
      graph_patch_baseline_free(baseline, baseline_len);
      free_graph_patch_state(inline_text, &result, original_hash, &program, &input, &graph);
      return 1;
    }
    print_preexisting_diag_notes("patch", command->input, &preexisting);
    graph_oracle_diags_free(&introduced);
    graph_oracle_diags_free(&preexisting);
  }
  const char *saved_path = NULL;
  if (ok && !save_graph_patch_output(command, target, &graph, input_kind, &input, &saved_path, diag)) {
    print_command_diag(command, diag->path ? diag->path : (command->out ? command->out : command->input), diag);
    graph_patch_baseline_free(baseline, baseline_len);
    free_graph_patch_state(inline_text, &result, original_hash, &program, &input, &graph);
    return 1;
  }

  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    append_graph_patch_json(&json, command, &graph, &result, original_hash, saved_path);
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else if (ok && command->graph_patch_check_only) {
    printf("program graph patch ok (check-only)\n");
    print_graph_patch_summary_text(&graph, &result, graph_patch_nodes_touched(baseline, baseline_len, &graph));
    if (input_kind == GRAPH_INPUT_REPOSITORY_STORE) printf("validated: check-equivalent\n");
  } else if (ok && saved_path) {
    printf("program graph patch ok\n");
    printf("saved: %s\n", saved_path);
    print_graph_patch_summary_text(&graph, &result, graph_patch_nodes_touched(baseline, baseline_len, &graph));
    if (input_kind == GRAPH_INPUT_REPOSITORY_STORE) printf("validated: check-equivalent\n");
  } else if (ok) {
    ZProgramGraphValidation validation = {0};
    z_program_graph_validate(&graph, &validation);
    ZBuf dump;
    zbuf_init(&dump);
    z_program_graph_append_dump(&dump, &graph, &validation);
    fputs(dump.data ? dump.data : "", stdout);
    zbuf_free(&dump);
  } else if (result.message[0]) {
    print_graph_patch_failure_text(command, &result);
  } else {
    print_diag(diag->path ? diag->path : graph_patch_source_label(command), diag);
  }

  graph_patch_baseline_free(baseline, baseline_len);
  free_graph_patch_state(inline_text, &result, original_hash, &program, &input, &graph);
  return ok ? 0 : 1;
}

static void graph_check_map_diag_path(const Command *command, const SourceInput *input, ZDiag *diag) {
  if (!diag) return;
  if (diag->code != 8003 && diag->code != 8005 && input && input->source_file) z_map_source_diag(input, diag);
  if (!diag->path) diag->path = graph_check_diagnostic_path(command);
}

typedef enum {
  GRAPH_CHECK_PHASE_LOWER,
  GRAPH_CHECK_PHASE_TYPECHECK,
  GRAPH_CHECK_PHASE_TARGET_READINESS,
} GraphCheckPhase;

static const char *graph_check_phase_name(GraphCheckPhase phase) {
  switch (phase) {
    case GRAPH_CHECK_PHASE_LOWER: return "lower";
    case GRAPH_CHECK_PHASE_TYPECHECK: return "typecheck";
    case GRAPH_CHECK_PHASE_TARGET_READINESS: return "target-readiness";
  }
  return "unknown";
}

static bool append_graph_artifact_target_readiness_json(ZBuf *buf, SourceInput *input, const ZProgramGraph *graph, const ZTargetInfo *target, const Command *command, ZDiag *diag, long long *lower_ms_out, bool *graph_mir_used_out, ZDiag *blocked_diag, bool *blocked_out) {
  if (lower_ms_out) *lower_ms_out = 0;
  if (graph_mir_used_out) *graph_mir_used_out = false;
  if (blocked_out) *blocked_out = false;
  ZDiag readiness_diag = {0};
  long long phase_started = now_ms();
  IrProgram ir = z_lower_program_graph_with_source(graph, input, target);
  long long lower_ms = now_ms() - phase_started;
  if (lower_ms_out) *lower_ms_out = lower_ms;
  if (graph_mir_used_out) *graph_mir_used_out = true;
  if (input) input->lower_ms = lower_ms;
  apply_ir_metrics_to_input(input, &ir, target);
  bool ready = true;
  if (!validate_c_libraries_for_target(input, target, command, &readiness_diag)) ready = false;
  if (ready && !target_readiness_select_emit_target(command, input, target, &readiness_diag)) ready = false;
  if (ready && !ir.mir_valid) { init_lowering_backend_diag(&readiness_diag, input, target, command, &ir); ready = false; }
  if (ready && !repository_graph_target_readiness_select_diag(command, input, target, &ir, &readiness_diag)) ready = false;
  if (!ready && diag) *diag = readiness_diag;
  if (!ready && blocked_out && blocked_diag && !ir.mir_valid && !z_std_source_path_is_module_artifact(command ? command->input : NULL)) {
    *blocked_out = check_gate_merged_graph_blocked(command, input, target, graph, blocked_diag);
  }
  append_target_readiness_result_json(buf, input, NULL, target, command, &ir, &readiness_diag, ready);
  z_free_ir_program(&ir); return ready;
}

static int run_graph_check_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  if (command->out) {
    return reject_graph_unsupported_out(command, diag);
  }

  ZProgramGraph graph = {0};
  if (!z_program_graph_load(command->input, &graph, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    return 1;
  }

  SourceInput checked_input = {0};
  GraphCheckPhase phase = GRAPH_CHECK_PHASE_TYPECHECK;
  z_program_graph_seed_source_metadata(&checked_input, &graph);
  z_program_graph_seed_artifact_source_paths(&checked_input, &graph, command->input);
  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  bool ok = z_program_graph_name_contracts_ok(&graph, command->input, diag);
  if (!ok) phase = GRAPH_CHECK_PHASE_LOWER;
  if (ok) ok = z_program_graph_collect_resolution_facts(&graph, &resolution);
  if (ok) ok = graph_check_resolution_ok(&graph, &resolution, command->input, diag);
  if (ok && !graph_check_target_capabilities_ok(&graph, &resolution, target, diag)) {
    phase = GRAPH_CHECK_PHASE_TARGET_READINESS;
    ok = false;
  }
  ZBuf target_readiness;
  zbuf_init(&target_readiness);
  long long lower_ms = 0;
  bool graph_mir_used = false;
  ZDiag readiness_diag = {0};
  ZDiag blocked_diag = {0};
  bool buildability_blocked = false;
  if (ok) (void)append_graph_artifact_target_readiness_json(&target_readiness, &checked_input, &graph, target, command, &readiness_diag, &lower_ms, &graph_mir_used, &blocked_diag, &buildability_blocked);
  if (ok && buildability_blocked) {
    ok = false;
    phase = GRAPH_CHECK_PHASE_TARGET_READINESS;
    *diag = blocked_diag;
    /* The gate already mapped this diagnostic to projection source locations. */
    if (!diag->path) diag->path = graph_check_diagnostic_path(command);
  } else if (!ok) {
    if (phase == GRAPH_CHECK_PHASE_TYPECHECK || phase == GRAPH_CHECK_PHASE_TARGET_READINESS) graph_check_map_diag_path(command, &checked_input, diag);
    else if (!diag->path) diag->path = command->input;
  }

  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    append_graph_check_json(&json, command, target, &checked_input, &graph, ok, ok ? NULL : diag, graph_check_phase_name(phase), target_readiness.len > 0 ? target_readiness.data : NULL, lower_ms, graph_mir_used);
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else if (ok) {
    printf("ok\n");
  } else {
    print_diag(diag->path ? diag->path : command->input, diag);
  }

  zbuf_free(&target_readiness);
  z_program_graph_resolution_facts_free(&resolution);
  z_free_source(&checked_input);
  z_program_graph_free(&graph);
  return ok ? 0 : 1;
}

static int run_graph_size_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  SourceInput input = {0};
  Program program = {0};
  IrProgram ir = {0};
  ZProgramGraphArtifactSource artifact_source = {0};
  if (!z_program_graph_prepare_artifact_mir_input(command->input,
                                                  target,
                                                  emit_kind_name(command ? command->emit : EMIT_EXE),
                                                  command ? command->backend : NULL,
                                                  &program,
                                                  &input,
                                                  &ir,
                                                  &artifact_source,
                                                  diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    free_loaded_command_state(&input, &program, &ir);
    return 1;
  }

  if (!metadata_backend_request_buildable(command, &input, target, diag)) {
    if (command && command->json) print_diag_json(diag->path ? diag->path : command->input, diag); else print_diag(diag->path ? diag->path : (command ? command->input : NULL), diag);
    free_loaded_command_state(&input, &program, &ir);
    return 1;
  }

  GraphSizeSource graph_source = {.artifact_source = &artifact_source};
  touch_program_graph_compiler_caches(&input, target, command && command->profile ? command->profile : "release", artifact_source.graph_hash);
  int rc = run_size_report_command(command, &input, &program, target, &ir, &graph_source, diag);
  free_loaded_command_state(&input, &program, &ir);
  return rc;
}

static int run_graph_artifact_roundtrip_command(const Command *command, ZDiag *diag) {
  if (reject_graph_source_text_out(command, "zero roundtrip --out <program-graph-artifact> [graph-input]", diag)) return 1;
  ZProgramGraphDirectRoundtrip result = {0};
  if (!z_program_graph_direct_roundtrip_file(command->input, command->out, &result, diag)) {
    const char *path = diag->path ? diag->path : (command->out ? command->out : command->input);
    print_command_diag(command, path, diag);
    z_program_graph_direct_roundtrip_free(&result);
    return 1;
  }
  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    append_graph_roundtrip_json(&json,
                                command,
                                "artifact",
                                command->input,
                                &result.original,
                                &result.roundtrip,
                                &result.comparison,
                                "direct-program-graph",
                                NULL,
                                "program-graph");
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else if (result.comparison.ok) {
    printf("program graph roundtrip ok\n");
  } else {
    fprintf(stderr, "program graph roundtrip mismatch: %s (%s)\n", result.comparison.message, result.comparison.field);
  }

  bool ok = result.comparison.ok;
  z_program_graph_direct_roundtrip_free(&result);
  return ok ? 0 : 1;
}

static bool resolve_graph_repository_store_input(Command *command, bool *artifact_input, ZDiag *diag) {
  if (!command || !command->input || !z_program_graph_command_can_use_repository_store(command->kind)) return true;
  char *source_root = NULL;
  const char *manifest_input = command->input;
  if (is_zero_source_path(command->input)) {
    source_root = z_program_graph_store_root_for_input(command->input);
    manifest_input = source_root;
  }
  bool enabled = false;
  bool manifest_ok = z_program_graph_manifest_compiler_input_enabled(manifest_input, &enabled, diag);
  char *root = source_root ? source_root : z_program_graph_store_root_for_input(command->input);
  char *store_path = z_program_graph_store_path_for_root(root);
  if (!manifest_ok) {
    free(root);
    free(store_path);
    return false;
  }
  if (!enabled && !(source_root && path_has_program_graph_storage_header(store_path))) {
    free(root);
    free(store_path);
    return true;
  }
  free(root);
  command->repository_graph_source_input = command->input;
  command_set_owned_input(command, store_path);
  command->repository_graph_input = true;
  if (artifact_input) *artifact_input = true;
  return true;
}

static bool resolve_graph_command_manifest_input(Command *command, bool *artifact_input, ZDiag *diag) {
  if (artifact_input) *artifact_input = false;
  if (!command || !command->command || !command->input || !command_is_program_graph_command(command)) return true;
  ZProgramGraphInputMode input_mode = z_program_graph_command_input_mode(command->kind);
  if (input_mode == Z_PROGRAM_GRAPH_INPUT_UNKNOWN) return true;
  if (input_mode == Z_PROGRAM_GRAPH_INPUT_PATH) return true;
  if (input_mode == Z_PROGRAM_GRAPH_INPUT_SOURCE) {
    if (is_zero_source_path(command->input)) return true;
    if (is_existing_directory_path(command->input)) return true;
    char *manifest_path = z_manifest_path_for_input(command->input);
    if (manifest_path) {
      free(manifest_path);
      return true;
    }
    bool projection_inspection = command->kind && (strcmp(command->kind, "status") == 0 || strcmp(command->kind, "verify-projection") == 0);
    const char *input_name = strrchr(command->input, '/');
    input_name = input_name ? input_name + 1 : command->input;
    if (projection_inspection && strcmp(input_name, "zero.graph") == 0 && direct_file_exists(command->input)) {
      command_set_owned_input(command, direct_dirname_of(command->input));
      return true;
    }
    set_source_input_diag(command->input, diag);
    return false;
  }
  if (input_mode == Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT && is_zero_source_path(command->input)) {
    if (graph_source_or_artifact_command_prefers_package_source(command)) return true;
    char *sidecar = source_sidecar_graph_path(command->input);
    if (command->kind && strcmp(command->kind, "patch") == 0) {
      set_graph_patch_projection_input_diag(command->input, sidecar, diag);
      free(sidecar);
      return false;
    }
    if (sidecar && path_has_program_graph_storage_header(sidecar)) {
      command->repository_graph_source_input = command->input;
      command_set_owned_input(command, sidecar);
      if (artifact_input) *artifact_input = true;
      return true;
    }
    free(sidecar);
    if (!resolve_graph_repository_store_input(command, artifact_input, diag)) return false;
    if (command->repository_graph_input) return true;
    sidecar = source_sidecar_graph_path(command->input);
    set_graph_input_required_diag(command->input, sidecar, diag);
    free(sidecar);
    return false;
  }
  if ((input_mode == Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT || input_mode == Z_PROGRAM_GRAPH_INPUT_ARTIFACT) &&
      path_has_program_graph_storage_header(command->input)) {
    if (artifact_input) *artifact_input = true;
    return true;
  }
  if (!resolve_graph_repository_store_input(command, artifact_input, diag)) return false;
  if (command->repository_graph_input) return true;

  if (input_mode == Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT && graph_source_or_artifact_command_prefers_package_source(command)) {
    char *manifest_path = z_manifest_path_for_input(command->input);
    if (manifest_path) {
      free(manifest_path);
      return true;
    }
  }

  char *artifact_path = NULL;
  bool handled = false;
  bool require_graph = input_mode == Z_PROGRAM_GRAPH_INPUT_ARTIFACT;
  if (!z_resolve_manifest_graph_artifact_path(command->input, &artifact_path, &handled, require_graph, diag)) return false;
  if (handled) {
    command_set_owned_input(command, artifact_path);
    if (artifact_input) *artifact_input = true;
    return true;
  }
  if (input_mode == Z_PROGRAM_GRAPH_INPUT_SOURCE_OR_ARTIFACT) {
    char *manifest_path = z_manifest_path_for_input(command->input);
    if (manifest_path) {
      free(manifest_path);
      if (graph_source_or_artifact_command_prefers_package_source(command)) return true;
      set_graph_input_required_diag(command->input, NULL, diag);
      return false;
    }
  }
  if (artifact_input) *artifact_input = true;
  return true;
}

static ZProgramGraphProjectionSourceSync manifest_graph_store_source_sync(const char *input, char **out_store_hash, ZProgramGraph *out_store_graph) {
  char *root = z_program_graph_store_root_for_input(input);
  char *store_path = root ? z_program_graph_store_path_for_root(root) : NULL;
  ZProgramGraphProjectionSourceSync sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN;
  if (out_store_hash) *out_store_hash = NULL;
  if (store_path && z_program_graph_store_path_exists(store_path)) {
    ZProgramGraphProjectionSourceSync fast_sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN;
    if (!out_store_hash && !out_store_graph &&
        z_program_graph_projection_source_sync_state_binary_fast(store_path, root, &fast_sync)) {
      free(store_path); free(root); return fast_sync;
    }
    ZProgramGraphStore store;
    ZDiag store_diag = {0};
    if (z_program_graph_store_load_path(store_path, &store, &store_diag)) {
      bool sources_missing = z_program_graph_projection_sources_missing(&store);
      if (!sources_missing) {
        ZProgramGraphProjectionSourceSync store_sync = Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN;
        ZDiag sync_diag = {0};
        if (z_program_graph_projection_source_sync_state(&store, &store_sync, &sync_diag)) sync = store_sync;
      }
      if (out_store_hash && store.graph.graph_hash) *out_store_hash = z_strdup(store.graph.graph_hash);
      if (out_store_graph) {
        z_program_graph_free(out_store_graph);
        *out_store_graph = store.graph;
        z_program_graph_init(&store.graph);
      }
      z_program_graph_store_free(&store);
    }
  }
  free(store_path);
  free(root);
  return sync;
}

/*
 * Refresh tips are context-aware: the reconcile classifies the source edit
 * against the pre-refresh store graph so a signature edit teaches addParamTo
 * and setReturnType, a const edit teaches setConst, and everything else keeps
 * the replace-fn tip. The note stays once-per-state without a dedup marker
 * because the refresh itself only runs while the source is stale.
 */
static const char *manifest_graph_refresh_tip(ZProgramGraphReconcileEditKind edit_kind) {
  if (edit_kind == Z_PROGRAM_GRAPH_RECONCILE_EDIT_SIGNATURE) {
    return "zero patch --op 'addParamTo fn=... name=... type=... default=...' threads a new parameter through every call site in one step; setReturnType changes return types";
  }
  if (edit_kind == Z_PROGRAM_GRAPH_RECONCILE_EDIT_CONST) {
    return "zero patch --op 'setConst name=... value=...' replaces a top-level const initializer directly and skips this reconcile";
  }
  return "zero patch --replace-fn <fn> --body-file - edits the graph directly and skips this reconcile";
}

static int resolve_manifest_graph_input_sync(const Command *command, const ZTargetInfo *target, const char *source_input_path) {
  const char *input = source_input_path ? source_input_path : command->input;
  char *store_hash = NULL;
  ZProgramGraph store_graph;
  z_program_graph_init(&store_graph);
  ZProgramGraphProjectionSourceSync sync = manifest_graph_store_source_sync(input, &store_hash, &store_graph);
  if (sync == Z_PROGRAM_GRAPH_PROJECTION_SYNC_CLEAN) {
    free(store_hash);
    z_program_graph_free(&store_graph);
    return 0;
  }
  if (sync == Z_PROGRAM_GRAPH_PROJECTION_SYNC_STORE_NEWER) {
    uint64_t context = note_dedup_fold(NOTE_DEDUP_SEED, store_hash ? store_hash : "");
    if (note_dedup_should_print(input, "store-newer", context)) {
      fprintf(stderr, "note: zero %s is using zero.graph, which is newer than the .0 source projection; run zero export to sync sources or zero import to make the edited source authoritative\n", command->command ? command->command : "build");
    }
    free(store_hash);
    z_program_graph_free(&store_graph);
    return 0;
  }
  free(store_hash);
  if (sync == Z_PROGRAM_GRAPH_PROJECTION_SYNC_DIVERGED) {
    z_program_graph_free(&store_graph);
    return z_repository_graph_diverged_compiler_input_error(input, target, command->json);
  }
  const char *stale_mode = getenv("ZERO_STALE");
  if (stale_mode && strcmp(stale_mode, "fail") == 0) {
    z_program_graph_free(&store_graph);
    return z_repository_graph_stale_compiler_input_error(input, target, command->json);
  }
  Command load_command = *command;
  load_command.input = input;
  load_command.out = NULL;
  SourceInput source_input = {0};
  Program source_program = {0};
  ZProgramGraph source_graph = {0};
  int validate_rc = load_and_validate_repository_graph_source(command, &load_command, target, command->command ? command->command : "build", &source_input, &source_program, &source_graph);
  if (validate_rc != 0) {
    z_program_graph_free(&store_graph);
    z_program_graph_free(&source_graph);
    z_free_program(&source_program);
    z_free_source(&source_input);
    return validate_rc;
  }
  ZProgramGraphReconcileEditKind edit_kind = z_program_graph_reconcile_edit_kind(&store_graph, &source_graph);
  z_program_graph_free(&store_graph);
  int rc = z_repository_graph_refresh_compiler_store(input, target, command->json, &source_graph);
  z_program_graph_free(&source_graph);
  z_free_program(&source_program);
  z_free_source(&source_input);
  if (rc == 0) fprintf(stderr, "note: zero %s refreshed zero.graph from the edited package source projection; tip: %s\n", command->command ? command->command : "build", manifest_graph_refresh_tip(edit_kind));
  return rc;
}

static int refresh_stale_manifest_graph_input(const Command *command, const ZTargetInfo *target) {
  return resolve_manifest_graph_input_sync(command, target, NULL);
}

static bool input_is_package_store_path(const char *input) {
  if (!input || !direct_file_exists(input)) return false;
  const char *name = strrchr(input, '/');
  name = name ? name + 1 : input;
  return strcmp(name, "zero.graph") == 0;
}

static void resolve_compiler_command_package_source_input(Command *command) {
  if (!command || !command->input) return;
  if (!z_program_graph_manifest_command_can_use_compiler_input(command->command)) return;
  bool source_input = is_zero_source_path(command->input) && direct_file_exists(command->input);
  bool store_input = !source_input && input_is_package_store_path(command->input);
  if (!source_input && !store_input) return;
  char *root = store_input ? direct_dirname_of(command->input) : z_program_graph_store_root_for_input(command->input);
  char *manifest_path = root ? z_manifest_path_for_root(root) : NULL;
  if (manifest_path) {
    command_set_owned_input(command, root);
    root = NULL;
  }
  free(manifest_path);
  free(root);
}

static int resolve_direct_command_manifest_graph_input(Command *command, const ZTargetInfo *target, bool *handled) {
  if (handled) *handled = false;
  if (!command || !z_program_graph_manifest_command_can_use_compiler_input(command->command)) return 0;
  if (command->input && path_has_program_graph_storage_header(command->input)) return 0;

  bool enabled = false;
  ZDiag diag = {0};
  if (!z_program_graph_manifest_compiler_input_enabled(command->input, &enabled, &diag)) {
    if (command->json) print_command_diag_json(command, diag.path ? diag.path : command->input, &diag);
    else print_diag(diag.path ? diag.path : command->input, &diag);
    return 1;
  }
  if (!enabled) return 0;

  int stale_rc = refresh_stale_manifest_graph_input(command, target);
  if (stale_rc != 0) return stale_rc;

  char *store_path = NULL;
  int rc = z_repository_graph_verify_compiler_input(command->input, target, command->json, &store_path);
  if (rc != 0) {
    free(store_path);
    return rc;
  }

  command->repository_graph_source_input = command->input;
  command_set_owned_input(command, store_path);
  command->repository_graph_input = true;
  if (handled) *handled = true;
  return 0;
}

static char *repository_graph_check_verdict_manifest_text(const ZProgramGraphStore *store) {
  char *manifest_path = store && store->root ? z_manifest_path_for_root(store->root) : NULL;
  if (!manifest_path) return NULL;
  ZDiag read_diag = {0};
  char *text = z_read_file(manifest_path, &read_diag);
  free(manifest_path);
  return text;
}

static int run_repository_graph_check_command(Command *command, const ZTargetInfo *target, bool *handled) {
  if (handled) *handled = false;
  if (!command || !graph_check_text_eq(command->command, "check")) return 0;

  bool enabled = false;
  ZDiag diag = {0};
  if (!z_program_graph_manifest_compiler_input_enabled(command->input, &enabled, &diag)) {
    if (command->json) print_command_diag_json(command, diag.path ? diag.path : command->input, &diag);
    else print_diag(diag.path ? diag.path : command->input, &diag);
    return 1;
  }
  if (!enabled) return 0;
  if (handled) *handled = true;

  int stale_rc = refresh_stale_manifest_graph_input(command, target);
  if (stale_rc != 0) return stale_rc;

  char *store_path = NULL;
  int store_rc = z_repository_graph_require_compiler_store(command->input, target, command->json, &store_path);
  if (store_rc != 0) return store_rc;

  ZProgramGraphStore store;
  long long load_started = now_ms();
  if (!z_program_graph_store_load_path(store_path, &store, &diag)) {
    if (command->json) print_command_diag_json(command, diag.path ? diag.path : command->input, &diag);
    else print_diag(diag.path ? diag.path : command->input, &diag);
    free(store_path);
    return 1;
  }
  free(store_path);
  long long load_ms = now_ms() - load_started;

  SourceInput input = {0};
  input.source_file = z_strdup(store.path ? store.path : command->input);
  z_program_graph_seed_source_metadata(&input, &store.graph);
  input.program_graph_hash = z_strdup(store.graph.graph_hash ? store.graph.graph_hash : "");
  input.program_graph_module_identity = z_strdup(store.graph.module_identity ? store.graph.module_identity : "");
  if (!z_program_graph_manifest_attach_metadata_to_input(&input, command->input, &diag)) {
    if (command->json) print_command_diag_json(command, diag.path ? diag.path : command->input, &diag);
    else print_diag(diag.path ? diag.path : command->input, &diag);
    /* metadata attach diags always own their path copies */
    free((char *)diag.path);
    z_free_source(&input);
    z_program_graph_store_free(&store);
    return 1;
  }

  /*
   * The semantic verification below is a pure function of the stored graph,
   * the target, the embedded stdlib, the manifest, and the compiler version.
   * Plain checks consult the persisted verdict cache before redoing it; the
   * JSON surface always reverifies because it reports the collected facts.
   */
  char *verdict_manifest_text = command->json ? NULL : repository_graph_check_verdict_manifest_text(&store);
  bool semantics_verdict_known = !command->json && z_program_graph_check_semantics_verdict_known(&store, target, verdict_manifest_text);

  ZProgramGraphResolutionFacts resolution;
  z_program_graph_resolution_facts_init(&resolution);
  long long resolve_started = now_ms();
  bool collected_resolution = semantics_verdict_known ||
                              (z_program_graph_name_contracts_ok(&store.graph, store.path ? store.path : command->input, &diag) && z_program_graph_collect_resolution_facts(&store.graph, &resolution));
  long long resolve_ms = now_ms() - resolve_started;
  long long check_started = now_ms();
  bool ok = semantics_verdict_known ||
            (collected_resolution &&
             graph_stored_compiler_input_ok(&store.graph, &resolution, &input, target, store.path ? store.path : command->input, &diag));
  input.check_ms = now_ms() - check_started;
  if (ok && !command->json && !semantics_verdict_known) {
    z_program_graph_check_semantics_verdict_remember(&store, target, verdict_manifest_text);
  }
  free(verdict_manifest_text);

  /*
   * Check-time buildability: lower the stored typed graph for the checked
   * target and fail the check with the same BLD004 diagnostics that
   * zero build or zero run would report later.
   */
  RepositoryGraphCheckReadiness readiness = {0};
  bool gate_applies = ok && graph_check_buildability_gate_applies(&store.graph);
  if (ok && command->json) {
    repository_graph_check_readiness_compute(command, &input, &store, target, &readiness);
    if (gate_applies && !readiness.ready) {
      if (!readiness.prepared && readiness.graph_mir_used && z_check_gate_diag_is_buildability_blocker(&readiness.diag) &&
          z_check_gate_diag_is_known_construct(&store.graph, &readiness.diag)) {
        diag = readiness.diag;
        ok = false;
      } else if (readiness.prepared && check_gate_buildability_blocked(command, &input, target, &store.graph, &readiness.ir, &diag)) {
        ok = false;
      }
    }
  } else if (ok && gate_applies) {
    long long gate_lower_ms = 0;
    if (repository_graph_check_gate_blocked(command, &input, &store, target, &gate_lower_ms, &diag)) ok = false;
  }
  long long cache_started = now_ms();
  touch_program_graph_compiler_caches(&input, target, command->profile, store.graph.graph_hash);
  long long cache_ms = now_ms() - cache_started;

  if (ok) {
    if (command->json) print_repository_graph_check_json_success(command, target, &input, &store, &resolution, &readiness, load_ms, resolve_ms, input.check_ms, cache_ms);
    else printf("ok\n");
  } else {
    if (command->json) print_command_diag_json(command, diag.path ? diag.path : command->input, &diag);
    else print_diag(diag.path ? diag.path : command->input, &diag);
  }

  repository_graph_check_readiness_free(&readiness);
  z_program_graph_resolution_facts_free(&resolution);
  z_free_source(&input);
  z_program_graph_store_free(&store);
  return ok ? 0 : 1;
}

static bool graph_subcommand_consumes_package_store(const char *kind) {
  static const char *const kinds[] = {"query", "view", "diff", "inspect", "source-map", "dump", "size", "roundtrip", "patch"};
  for (size_t i = 0; kind && i < sizeof(kinds) / sizeof(kinds[0]); i++) {
    if (strcmp(kind, kinds[i]) == 0) return true;
  }
  return false;
}

static int resolve_graph_subcommand_store_input_sync(const Command *command, const ZTargetInfo *target) {
  if (!command || !graph_subcommand_consumes_package_store(command->kind)) return 0;
  if (!command->input || !path_has_program_graph_storage_header(command->input)) return 0;
  const char *source_input = command->repository_graph_input ? command->repository_graph_source_input : NULL;
  char *owned_root = NULL;
  if (!source_input) {
    if (!input_is_package_store_path(command->input)) return 0;
    owned_root = direct_dirname_of(command->input);
    source_input = owned_root;
  }
  char *root = source_input ? z_program_graph_store_root_for_input(source_input) : NULL;
  char *manifest_path = root ? z_manifest_path_for_root(root) : NULL;
  bool manifest_package = manifest_path != NULL;
  free(manifest_path);
  free(root);
  if (!manifest_package) {
    free(owned_root);
    return 0;
  }
  int rc = resolve_manifest_graph_input_sync(command, target, source_input);
  free(owned_root);
  return rc;
}

static int run_graph_subcommand_dispatch(Command *command, const ZTargetInfo *target, bool graph_command_artifact_input, ZDiag *diag, bool *handled) {
  if (handled) *handled = false;
  if (!command_is_program_graph_command(command)) return 0;
  if (handled) *handled = true;
  if (graph_check_text_eq(command->kind, "init")) return run_graph_init_command(command, diag);
  bool repo_graph_command = false;
  SourceInput repo_graph_input = {0}; Program repo_graph_program = {0}; ZProgramGraph repo_source_graph = {0};
  bool repo_wants_source_graph = z_repository_graph_needs_source_graph(command->kind, command->input, target, command->graph_export_from_graph, command->graph_import_from_source);
  char *repo_package_input = NULL;
  if (repo_wants_source_graph && !command->out && command->input && is_zero_source_path(command->input) && direct_file_exists(command->input)) {
    char *repo_package_root = z_program_graph_store_root_for_input(command->input);
    char *repo_package_manifest = repo_package_root ? z_manifest_path_for_root(repo_package_root) : NULL;
    if (repo_package_manifest) {
      repo_package_input = repo_package_root;
      repo_package_root = NULL;
    }
    free(repo_package_manifest);
    free(repo_package_root);
  }
  Command repo_graph_load_command = *command;
  if (repo_package_input) repo_graph_load_command.input = repo_package_input;
  ZDiag repo_source_graph_diag = {0};
  bool repo_has_source_graph = false;
  /*
   * zero import validates the source projection with the compiler typecheck
   * plus the zero check oracle, diffed against the existing store so
   * pre-existing diagnostics in untouched files never wall a refresh. Only
   * diagnostics the import would introduce fail it, and all of them report
   * in one shot.
   */
  if (repo_wants_source_graph && repository_graph_command_loads_checked_source(&repo_graph_load_command)) {
    int import_rc = load_and_validate_repository_graph_source(command, &repo_graph_load_command, target, "import", &repo_graph_input, &repo_graph_program, &repo_source_graph);
    if (import_rc != 0) {
      z_program_graph_free(&repo_source_graph); z_free_program(&repo_graph_program); z_free_source(&repo_graph_input);
      free(repo_package_input);
      return import_rc;
    }
    repo_has_source_graph = true;
  } else if (repo_wants_source_graph) {
    repo_has_source_graph = load_graph_from_current_source(&repo_graph_load_command, target, &repo_graph_input, &repo_graph_program, &repo_source_graph, NULL, &repo_source_graph_diag);
    if (!repo_has_source_graph) {
      if (command->json) print_command_diag_json(command, repo_source_graph_diag.path ? repo_source_graph_diag.path : command->input, &repo_source_graph_diag); else print_diag(repo_source_graph_diag.path ? repo_source_graph_diag.path : command->input, &repo_source_graph_diag);
      z_program_graph_free(&repo_source_graph); z_free_program(&repo_graph_program); z_free_source(&repo_graph_input);
      free(repo_package_input);
      return 1;
    }
  }
  RepositoryGraphSourceGraphLoader repo_graph_loader = {.command = &repo_graph_load_command, .target = target};
  int repo_graph_rc = z_repository_graph_maybe_command(command->kind, repo_graph_load_command.input, target, command->json, command->graph_export_from_graph, command->graph_import_from_source, command->merge_base, command->merge_left, command->merge_right, command->store_format, command->out, repo_has_source_graph ? &repo_source_graph : NULL, command->graph_export_from_graph ? load_repository_graph_checked_source_graph : NULL, &repo_graph_loader, &repo_graph_command);
  z_program_graph_free(&repo_source_graph); z_free_program(&repo_graph_program); z_free_source(&repo_graph_input);
  free(repo_package_input);
  if (repo_graph_command) return repo_graph_rc;
  int store_sync_rc = resolve_graph_subcommand_store_input_sync(command, target);
  if (store_sync_rc != 0) return store_sync_rc;
  if (graph_check_text_eq(command->kind, "validate")) return run_graph_validate_command(command, target, diag);
  if (graph_check_text_eq(command->kind, "dump")) return run_graph_dump_input_command(command, target, diag, false);
  if (graph_check_text_eq(command->kind, "import")) return run_graph_dump_input_command(command, target, diag, true);
  if (graph_check_text_eq(command->kind, "view")) return run_graph_view_command(command, diag);
  if (graph_check_text_eq(command->kind, "diff")) return run_graph_view_command(command, diag);
  if (graph_check_text_eq(command->kind, "query")) return run_graph_query_command(command, target, diag);
  if (graph_check_text_eq(command->kind, "inspect")) return run_graph_inspect_command(command, target, diag);
  if (graph_check_text_eq(command->kind, "source-map")) return run_graph_source_map_command(command, target, diag);
  if (graph_check_text_eq(command->kind, "reconcile")) return run_graph_reconcile_command(command, target, diag);
  if (graph_check_text_eq(command->kind, "size")) return run_graph_size_command(command, target, diag);
  if (graph_check_text_eq(command->kind, "roundtrip") && graph_command_artifact_input) return run_graph_artifact_roundtrip_command(command, diag);
  if (handled) *handled = false;
  return 0;
}

static int run_graph_roundtrip_command(const Command *command, SourceInput *input, Program *program, ZDiag *diag) {
  ZProgramGraph original = {0};
  ZProgramGraph roundtrip = {0};
  ZProgramGraphCompare comparison = {0};
  ZBuf view;
  zbuf_init(&view);

  if (!z_program_graph_from_program(input, program, &original)) {
    diag->code = 2002;
    diag->path = input ? input->source_file : command->input;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to build source program graph");
    print_command_diag(command, diag->path, diag);
    zbuf_free(&view);
    return 1;
  }
  original.canonical_source = input && input->canonical_text_source;
  if (!z_program_graph_merge_embedded_std_graph_modules(&original, input, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    z_program_graph_free(&original);
    zbuf_free(&view);
    return 1;
  }

  if (!z_program_graph_direct_roundtrip_graph(&original, input && input->source_file ? input->source_file : command->input, &roundtrip, &comparison, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    z_program_graph_free(&roundtrip);
    z_program_graph_free(&original);
    zbuf_free(&view);
    return 1;
  }

  if (command->json && !command->out && !z_program_graph_append_view(&view, &original, input && input->source_file ? input->source_file : command->input, diag)) {
    print_command_diag(command, diag->path ? diag->path : command->input, diag);
    z_program_graph_free(&roundtrip);
    z_program_graph_free(&original);
    zbuf_free(&view);
    return 1;
  }
  if (command->out) {
    if (reject_graph_source_text_out(command, "zero roundtrip --out <program-graph-artifact> [graph-input]", diag)) {
      z_program_graph_free(&roundtrip);
      z_program_graph_free(&original);
      zbuf_free(&view);
      return 1;
    }
    ZProgramGraphStoreFormat store_format = Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
    if (!command_repository_store_format(command, Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT, &store_format, diag) ||
        !z_program_graph_save_format(command->out, &roundtrip, store_format, diag)) {
      print_command_diag(command, diag->path ? diag->path : command->out, diag);
      z_program_graph_free(&roundtrip);
      z_program_graph_free(&original);
      zbuf_free(&view);
      return 1;
    }
  }

  if (command->json) {
    ZBuf json;
    zbuf_init(&json);
    append_graph_roundtrip_json(&json,
                                command,
                                "sourceFile",
                                input ? input->source_file : command->input,
                                &original,
                                &roundtrip,
                                &comparison,
                                "direct-program-graph",
                                view.data ? view.data : "",
                                "program-graph");
    fputs(json.data, stdout);
    zbuf_free(&json);
  } else if (comparison.ok) {
    printf("program graph roundtrip ok\n");
  } else {
    fprintf(stderr, "program graph roundtrip mismatch: %s (%s)\n", comparison.message, comparison.field);
  }

  z_program_graph_free(&roundtrip);
  z_program_graph_free(&original);
  zbuf_free(&view);
  return comparison.ok ? 0 : 1;
}

static int run_graph_command(const Command *command, SourceInput *input, Program *program, const ZTargetInfo *target, ZDiag *diag) {
  bool graph_import = command->kind && strcmp(command->kind, "import") == 0;
  bool graph_dump = command->kind && (strcmp(command->kind, "dump") == 0 || graph_import);
  bool graph_inspect = command->kind && strcmp(command->kind, "inspect") == 0;
  bool graph_roundtrip = command->kind && strcmp(command->kind, "roundtrip") == 0;
  if (command->kind && !graph_dump && !graph_inspect && !graph_roundtrip) {
    fprintf(stderr, "unknown graph mode: %s\n", command->kind);
    return 1;
  }
  if (command->out && !graph_dump && !graph_roundtrip) {
    return reject_graph_unsupported_out(command, diag);
  }
  if (graph_roundtrip) return run_graph_roundtrip_command(command, input, program, diag);
  if (graph_dump && command->out) {
    if (reject_graph_source_text_out(command, "zero dump --out <program-graph-artifact> [graph-input]", diag)) return 1;
    ZProgramGraph stored = {0};
    if (!z_program_graph_from_program(input, program, &stored)) {
      diag->code = 2002;
      diag->path = input ? input->source_file : command->input;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "failed to build source program graph");
      print_command_diag(command, diag->path ? diag->path : command->input, diag);
      return 1;
    }
    stored.canonical_source = false;
    if (!z_program_graph_merge_embedded_std_graph_modules(&stored, input, diag)) {
      print_command_diag(command, diag->path ? diag->path : command->input, diag);
      z_program_graph_free(&stored);
      return 1;
    }
    ZProgramGraphStoreFormat store_format = Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT;
    if (!command_repository_store_format(command, Z_PROGRAM_GRAPH_STORE_FORMAT_TEXT, &store_format, diag) ||
        !z_program_graph_save_format(command->out, &stored, store_format, diag)) {
      print_command_diag(command, diag->path ? diag->path : command->out, diag);
      z_program_graph_free(&stored);
      return 1;
    }
    if (command->json) {
      ZProgramGraphValidation validation = {0};
      z_program_graph_validate(&stored, &validation);
      ZBuf json;
      zbuf_init(&json);
      if (graph_import) append_graph_import_json(&json, command, input, &stored, &validation);
      else z_program_graph_append_json(&json, &stored, &validation);
      fputs(json.data, stdout);
      zbuf_free(&json);
    }
    z_program_graph_free(&stored);
    return 0;
  }
  ZBuf graph;
  zbuf_init(&graph);
  if (graph_dump) z_append_program_graph_dump(&graph, input, program, command->json);
  else if (graph_inspect && !command->json) print_graph_inspect_text(input, program, target, command);
  else append_graph_json(&graph, input, program, target, command);
  if (graph.data && graph.len > 0) fputs(graph.data, stdout);
  zbuf_free(&graph);
  return 0;
}

static int run_llvm_native_artifact_command(const Command *command, SourceInput *input, Program *program, IrProgram *ir, const ZTargetInfo *target, long long command_started_ms, bool build_command, bool run_command) {
  ZDiag diag = {0};
  const char *emit_kind = emit_kind_name(command->emit);
  if (command->emit != EMIT_EXE) {
    z_backend_init_llvm_unavailable_diag(&diag, target, emit_kind, input->source_file);
    int rc = return_buildability_error(command, input, &diag, ir, program); z_free_source(input); return rc;
  }
  if (!build_command && !run_command) {
    z_backend_init_llvm_unavailable_diag(&diag, target, emit_kind, input->source_file);
    snprintf(diag.message, sizeof(diag.message), "LLVM native executable output is experimental and is supported only by zero build or zero run");
    snprintf(diag.expected, sizeof(diag.expected), "zero build/run --backend llvm --emit exe");
    snprintf(diag.actual, sizeof(diag.actual), "%s --backend llvm --emit exe", command->command ? command->command : "command");
    snprintf(diag.help, sizeof(diag.help), "use zero build or zero run for explicit LLVM experiments");
    z_backend_blocker_set(&diag.backend_blocker,
                          target && target->name ? target->name : "unknown",
                          target && target->object_format ? target->object_format : "unknown",
                          "llvm",
                          "buildability",
                          "llvm executable command");
    int rc = return_buildability_error(command, input, &diag, ir, program); z_free_source(input); return rc;
  }
  if (!ir->mir_valid) {
    init_lowering_backend_diag(&diag, input, target, command, ir);
    int rc = return_buildability_error(command, input, &diag, ir, program); z_free_source(input); return rc;
  }
  if (!z_llvm_native_executable_ready(target, input->source_file, &diag)) {
    int rc = return_buildability_error(command, input, &diag, ir, program); z_free_source(input); return rc;
  }

  ZBuf llvm_ir;
  long long phase_started = now_ms();
  bool emitted_llvm_ir = z_emit_llvm_ir_from_ir(ir, &llvm_ir, &diag);
  input->codegen_ms = now_ms() - phase_started;
  if (!emitted_llvm_ir) {
    int rc = return_buildability_error(command, input, &diag, ir, program); z_free_source(input); return rc;
  }

  char *base_exe_file = command->out ? z_strdup(command->out) : command_default_exe_base_path(command, input);
  char *exe_file = apply_target_suffix(base_exe_file, target);
  free(base_exe_file);
  char *llvm_file = path_with_suffix(exe_file, ".ll");
  bool links_zero_runtime = input->direct_runtime_helper_count > 0;
  char *runtime_object_file = links_zero_runtime ? path_with_suffix(exe_file, ".zero-runtime.o") : NULL;
  ZToolchainPlan llvm_toolchain = z_llvm_c_toolchain_plan(target);

  phase_started = now_ms();
  input->emitted_object_cache_hit = compiler_cache_touch("emitted-llvm-native", command_compile_cache_key(command, input, target, command->profile, "llvm-clang-exe"));
  bool wrote_llvm_ir = z_write_file(llvm_file, llvm_ir.data ? llvm_ir.data : "", &diag);
  if (wrote_llvm_ir && links_zero_runtime) wrote_llvm_ir = compile_zero_runtime_object(runtime_object_file, &llvm_toolchain, command, target, true, &diag);
  input->object_ms = now_ms() - phase_started;
  if (!wrote_llvm_ir) {
    print_command_diag(command, diag.path ? diag.path : input->source_file, &diag);
    free(runtime_object_file); free(llvm_file); free(exe_file); zbuf_free(&llvm_ir); z_free_ir_program(ir); z_free_program(program); z_free_source(input);
    return 1;
  }

  phase_started = now_ms();
  bool linked = z_llvm_link_executable(llvm_file, runtime_object_file, exe_file, &llvm_toolchain, target, command->profile, links_zero_runtime, &diag);
  if (linked && !z_process_mark_executable(exe_file)) {
    init_executable_finalize_diag(&diag, exe_file);
    linked = false;
  }
  input->link_ms = now_ms() - phase_started;
  (void)z_process_remove_regular_file(llvm_file);
  if (runtime_object_file) (void)z_process_remove_regular_file(runtime_object_file);
  if (!linked) {
    print_command_diag(command, diag.path ? diag.path : input->source_file, &diag);
    free(runtime_object_file); free(llvm_file); free(exe_file); zbuf_free(&llvm_ir); z_free_ir_program(ir); z_free_program(program); z_free_source(input);
    return 1;
  }

  if (run_command) {
    int rc = run_executable_artifact_as(exe_file, exe_file, command);
    command_remove_ephemeral_run_artifact(command, exe_file);
    free(runtime_object_file); free(llvm_file); free(exe_file); zbuf_free(&llvm_ir); z_free_ir_program(ir); z_free_program(program); z_free_source(input);
    return rc;
  }

  long long elapsed_ms = now_ms() - command_started_ms;
  if (command->json) print_build_json(command, input, program, ir, target, "exe", exe_file, file_size_or_negative(exe_file), 0, elapsed_ms);
  else print_artifact(exe_file, elapsed_ms);
  free(runtime_object_file); free(llvm_file); free(exe_file); zbuf_free(&llvm_ir); z_free_ir_program(ir); z_free_program(program); z_free_source(input);
  return 0;
}

static int reject_retired_graph_subcommand(const Command *command) {
  ZDiag diag = {0};
  const char *subcommand = command && command->retired_graph_subcommand ? command->retired_graph_subcommand : "";
  const bool patch_subcommand = graph_check_text_eq(subcommand, "patch");
  const bool no_subcommand = !subcommand[0];
  diag.code = 2002; diag.line = 1; diag.column = 1; diag.length = 1;
  snprintf(diag.message, sizeof(diag.message), no_subcommand ? "zero graph is not supported" : "zero graph %s is not supported", subcommand);
  snprintf(diag.expected, sizeof(diag.expected), no_subcommand ? "zero inspect [graph-input]" : (patch_subcommand ? "zero patch [graph-input] (<patch-file>|--op <operation>)" : "zero %s [graph-input]"), subcommand);
  snprintf(diag.actual, sizeof(diag.actual), no_subcommand ? "zero graph" : "zero graph %s", subcommand);
  snprintf(diag.help, sizeof(diag.help), no_subcommand ? "run zero inspect, zero query, zero dump, or another first-class graph command" : (patch_subcommand ? "run zero patch; graph is now the first-class patch surface" : "run zero %s; graph commands are first-class root commands"), subcommand);
  if (command && command->json) print_command_diag_json(command, diag.actual, &diag);
  else print_diag(diag.actual, &diag);
  return 1;
}
static int reject_unknown_flag(const Command *command) {
  fprintf(stderr, "unknown flag: %s\n", command->unknown_flag);
  fprintf(stderr, "help: run `zero %s --help`", command->command);
  if (strncmp(command->unknown_flag, "--js", 4) == 0) fprintf(stderr, " or use --json");
  fprintf(stderr, "\n");
  return 1;
}

static int run_direct_graph_test_command(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  if (command && command->emit == EMIT_LLVM_IR) {
    if (z_backend_request_is_llvm(command->backend, emit_kind_name(command->emit))) init_llvm_ir_build_only_diag(diag, command, target, command->input);
    else init_direct_llvm_ir_unavailable_diag(diag, command, target, command->input);
    if (command->json) print_command_diag_json(command, diag->path ? diag->path : command->input, diag);
    else print_diag(diag->path ? diag->path : command->input, diag);
    return 1;
  }
  return z_program_graph_run_tests_direct(&(ZProgramGraphTestCommand){.input = command->input, .repository_source_input = command->repository_graph_source_input, .profile = command->profile, .filter = command->filter, .json = command->json, .repository_graph_input = command->repository_graph_input}, target, diag);
}

static bool prepare_graph_metadata_command(Command *command, const char *direct_graph_manifest_input, bool direct_graph_manifest_command, const ZTargetInfo *target, SourceInput *input, Program *program, ZDiag *diag) {
  ZProgramGraphStore store = {0};
  ZProgramGraph artifact_graph = {0};
  const ZProgramGraph *metadata_graph = NULL;
  bool loaded_store = false;
  if (command->repository_graph_input) {
    loaded_store = z_program_graph_store_load_path(command->input, &store, diag);
    metadata_graph = loaded_store ? &store.graph : NULL;
  } else if (z_program_graph_load(command->input, &artifact_graph, diag)) {
    metadata_graph = &artifact_graph;
  }
  if (!metadata_graph) return false;
  const char *display_input = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
  bool ok = z_program_graph_lower_to_program_with_source(metadata_graph, display_input, program, input, diag);
  if (ok) {
    z_set_check_target(target);
    ok = z_check_program(program, diag);
  }
  const char *metadata_input = direct_graph_manifest_command ? direct_graph_manifest_input : command->repository_graph_source_input;
  if (ok && metadata_input) ok = z_program_graph_manifest_attach_metadata_to_input(input, metadata_input, diag);
  if (ok) {
    input->program_graph_hash = z_strdup(metadata_graph->graph_hash ? metadata_graph->graph_hash : "");
    input->program_graph_module_identity = z_strdup(metadata_graph->module_identity ? metadata_graph->module_identity : "");
    command->graph_source.artifact = command->input;
    command->graph_source.graph_hash = input->program_graph_hash;
    command->graph_source.module_identity = input->program_graph_module_identity;
    command->graph_source.lowering = "graph-native-check";
    command->graph_source.source_projection_state = loaded_store ? z_program_graph_projection_state_label(&store, target, NULL, NULL, NULL) : NULL;
    command->graph_source.canonical_source = metadata_graph->canonical_source;
    touch_program_graph_compiler_caches(input, target, command->profile, command->graph_source.graph_hash);
  }
  if (loaded_store) z_program_graph_store_free(&store);
  else z_program_graph_free(&artifact_graph);
  return ok;
}

static bool validate_repository_graph_c_libraries_before_mir(const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  if (!command || !command->repository_graph_input) return true;
  ZProgramGraphStore store = {0};
  if (!z_program_graph_store_load_path(command->input, &store, diag)) return false;
  SourceInput input = {0};
  input.source_file = z_strdup(store.path ? store.path : command->input);
  z_program_graph_seed_source_metadata(&input, &store.graph);
  const char *manifest_input = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
  bool ok = z_program_graph_manifest_attach_metadata_to_input(&input, manifest_input, diag);
  if (ok) ok = validate_c_libraries_for_target(&input, target, command, diag);
  z_free_source(&input);
  z_program_graph_store_free(&store);
  return ok;
}

static bool graph_has_direct_c_import_node(const ZProgramGraph *graph) {
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_C_IMPORT) return true;
  }
  return false;
}

typedef enum {
  EARLY_CACHED_RUN_NOT_APPLICABLE,
  EARLY_CACHED_RUN_VALIDATED_NO_HIT,
  EARLY_CACHED_RUN_HANDLED,
  EARLY_CACHED_RUN_FAILED
} EarlyCachedRunResult;

static bool store_header_get_u32(const unsigned char *data, size_t len, size_t *offset, uint32_t *out) {
  if (!data || !offset || !out || *offset > len || len - *offset < 4) return false;
  const unsigned char *bytes = data + *offset;
  *out = ((uint32_t)bytes[0]) |
         ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) |
         ((uint32_t)bytes[3] << 24);
  *offset += 4;
  return true;
}

static bool store_header_get_u64(const unsigned char *data, size_t len, size_t *offset, uint64_t *out) {
  if (!data || !offset || !out || *offset > len || len - *offset < 8) return false;
  const unsigned char *bytes = data + *offset;
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; i++) value |= (uint64_t)bytes[i] << (i * 8);
  *out = value;
  *offset += 8;
  return true;
}

typedef struct {
  uint64_t string_bytes;
  uint64_t graph_off;
  uint64_t graph_len;
} RepositoryGraphHashRef;

static bool repository_graph_hash_ref_from_binary_header(const unsigned char *data, size_t len, size_t file_len, RepositoryGraphHashRef *out) {
  static const unsigned char magic[8] = {'Z', 'R', 'G', 'B', 'I', 'N', '1', 0};
  if (!data || !out || len < sizeof(magic) || memcmp(data, magic, sizeof(magic)) != 0) return false;
  size_t offset = sizeof(magic);
  uint32_t schema = 0, flags = 0, validation_state = 0, reserved = 0;
  uint64_t ignored = 0, string_bytes = 0;
  if (!store_header_get_u32(data, len, &offset, &schema) ||
      !store_header_get_u32(data, len, &offset, &flags) ||
      !store_header_get_u32(data, len, &offset, &validation_state) ||
      !store_header_get_u32(data, len, &offset, &reserved)) return false;
  if (schema != 1 || reserved != 0 || validation_state > (uint32_t)Z_PROGRAM_GRAPH_VALIDATION_BUILDABLE || (flags & ~3u) != 0) return false;
  for (unsigned i = 0; i < 6; i++) {
    if (!store_header_get_u64(data, len, &offset, i == 5 ? &string_bytes : &ignored)) return false;
  }
  uint64_t module_off = 0, module_len = 0, graph_off = 0, graph_len = 0, module_hash_off = 0, module_hash_len = 0;
  if (!store_header_get_u64(data, len, &offset, &module_off) ||
      !store_header_get_u64(data, len, &offset, &module_len) ||
      !store_header_get_u64(data, len, &offset, &graph_off) ||
      !store_header_get_u64(data, len, &offset, &graph_len) ||
      !store_header_get_u64(data, len, &offset, &module_hash_off) ||
      !store_header_get_u64(data, len, &offset, &module_hash_len)) return false;
  (void)module_off;
  (void)module_len;
  (void)module_hash_off;
  (void)module_hash_len;
  if (flags & (1u << 1)) {
    uint64_t source_hash_off = 0, source_hash_len = 0;
    if (!store_header_get_u64(data, len, &offset, &source_hash_off) ||
        !store_header_get_u64(data, len, &offset, &source_hash_len)) return false;
    (void)source_hash_off;
    (void)source_hash_len;
  }
  if (string_bytes == 0 || string_bytes > (uint64_t)file_len || graph_len == 0 || graph_len == UINT64_MAX || graph_len > (uint64_t)SIZE_MAX || graph_off > string_bytes || graph_len > string_bytes - graph_off) return false;
  *out = (RepositoryGraphHashRef){.string_bytes = string_bytes, .graph_off = graph_off, .graph_len = graph_len};
  return true;
}

static char *read_repository_graph_hash_binary_fast(FILE *file, const unsigned char *header, size_t header_len, size_t file_len) {
  RepositoryGraphHashRef ref = {0};
  if (!file || !repository_graph_hash_ref_from_binary_header(header, header_len, file_len, &ref)) return NULL;
  uint64_t string_table_offset = (uint64_t)file_len - ref.string_bytes;
  uint64_t graph_offset = string_table_offset + ref.graph_off;
  if (graph_offset > (uint64_t)LONG_MAX) return NULL;
  if (fseek(file, (long)graph_offset, SEEK_SET) != 0) return NULL;
  char *hash = z_checked_malloc((size_t)ref.graph_len + 1);
  size_t read_len = fread(hash, 1, (size_t)ref.graph_len, file);
  if (read_len != (size_t)ref.graph_len) {
    free(hash);
    return NULL;
  }
  hash[(size_t)ref.graph_len] = '\0';
  return hash;
}

static char *repository_graph_hash_from_text_header(const unsigned char *data, size_t len) {
  if (!data || len == 0) return NULL;
  static const char prefix[] = "graphHash \"";
  size_t offset = 0;
  while (offset < len) {
    size_t line_start = offset;
    while (offset < len && data[offset] != '\n') offset++;
    size_t line_len = offset - line_start;
    if (offset < len && data[offset] == '\n') offset++;
    if (line_len == 5 && memcmp(data + line_start, "graph", 5) == 0) break;
    if (line_len > sizeof(prefix) - 1 && memcmp(data + line_start, prefix, sizeof(prefix) - 1) == 0) {
      size_t value_start = line_start + sizeof(prefix) - 1;
      size_t value_end = value_start;
      while (value_end < line_start + line_len && data[value_end] != '"') value_end++;
      if (value_end > value_start && value_end < line_start + line_len) return z_strndup((const char *)(data + value_start), value_end - value_start);
    }
  }
  return NULL;
}

static char *read_repository_graph_hash_fast(const char *path) {
  if (!path || !path[0]) return NULL;
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  struct stat st;
  if (stat(path, &st) != 0 || st.st_size <= 0) {
    fclose(file);
    return NULL;
  }
  unsigned char header[4096];
  size_t len = fread(header, 1, sizeof(header), file);
  if (len == 0) {
    fclose(file);
    return NULL;
  }
  char *hash = z_program_graph_store_bytes_are_binary(header, len)
    ? read_repository_graph_hash_binary_fast(file, header, len, (size_t)st.st_size)
    : repository_graph_hash_from_text_header(header, len);
  fclose(file);
  return hash;
}

static EarlyCachedRunResult try_run_repository_graph_cached_executable_before_mir(const Command *command, const ZTargetInfo *target, int *rc_out, ZDiag *diag, bool allow_full_store_fallback) {
  if (rc_out) *rc_out = 1;
  if (!command || !target || !command->repository_graph_input || !command_uses_ephemeral_run_artifact(command) || command->emit != EMIT_EXE || command->json) return EARLY_CACHED_RUN_NOT_APPLICABLE;
  if (z_backend_request_is_llvm(command->backend, emit_kind_name(command->emit))) return EARLY_CACHED_RUN_NOT_APPLICABLE;
  if (command->repository_graph_source_input && !z_program_graph_projection_cached_run_allows_cache(command->repository_graph_source_input)) return EARLY_CACHED_RUN_NOT_APPLICABLE;
  char *fast_graph_hash = read_repository_graph_hash_fast(command->input);
  if (fast_graph_hash) {
    SourceInput fast_input = {0};
    fast_input.source_file = z_strdup(command->input);
    fast_input.package_root = runtime_dirname(command->input);
    const char *manifest_input = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
    bool needs_c_library_validation = true; if (!z_program_graph_manifest_attach_cache_metadata_to_input(&fast_input, manifest_input, &needs_c_library_validation, diag) ||
        (needs_c_library_validation && !validate_c_libraries_for_target(&fast_input, target, command, diag))) {
      free(fast_graph_hash);
      z_free_source(&fast_input);
      return EARLY_CACHED_RUN_FAILED;
    }
    Command cache_command = *command;
    cache_command.graph_source.graph_hash = fast_graph_hash;
    cache_command.graph_source.artifact = command->input;
    ZToolchainPlan runtime_toolchain = z_plan_toolchain(command->cc, command->profile, target);
    const char *direct_obj_request = z_backend_direct_request_name(command->backend);
    ZDirectObjectTargetFacts direct_obj = z_direct_object_target_facts(target);
    if (direct_obj_request && !z_direct_requested_backend_matches(direct_obj_request, direct_obj.backend)) {
      free(fast_graph_hash);
      z_free_source(&fast_input);
      return EARLY_CACHED_RUN_NOT_APPLICABLE;
    }
    if (!direct_obj.available) {
      free(fast_graph_hash);
      z_free_source(&fast_input);
      return EARLY_CACHED_RUN_NOT_APPLICABLE;
    }
    for (int http = 0; http < 2; http++) {
      ZDirectRuntimeObjectFacts runtime_object = z_direct_runtime_object_facts(target, http != 0);
      if (!runtime_object.supported) continue;
      char *cache_path = linked_executable_cache_path(&cache_command, &fast_input, target, &runtime_toolchain, true, http != 0, runtime_object.cache_key);
      if (cache_path && z_process_executable_file_ready(cache_path)) {
        char *base_argv0 = command->out ? z_strdup(command->out) : command_default_exe_base_path(&cache_command, &fast_input), *argv0_path = apply_target_suffix(base_argv0, target); free(base_argv0);
        int rc = exec_cached_executable_artifact(cache_path, argv0_path, command);
        free(argv0_path);
        if (rc_out) *rc_out = rc;
        free(cache_path);
        free(fast_graph_hash);
        z_free_source(&fast_input);
        return EARLY_CACHED_RUN_HANDLED;
      }
      free(cache_path);
    }
    free(fast_graph_hash);
    z_free_source(&fast_input);
  }
  if (!allow_full_store_fallback) return EARLY_CACHED_RUN_NOT_APPLICABLE;
  ZProgramGraphStore store = {0};
  if (!z_program_graph_store_load_path(command->input, &store, diag)) return EARLY_CACHED_RUN_FAILED;
  SourceInput input = {0};
  input.source_file = z_strdup(store.path ? store.path : command->input);
  input.package_root = z_strdup(store.root && store.root[0] ? store.root : ".");
  z_program_graph_seed_source_metadata(&input, &store.graph);
  const char *manifest_input = command->repository_graph_source_input ? command->repository_graph_source_input : command->input;
  bool needs_c_library_validation = true; if (!z_program_graph_manifest_attach_cache_metadata_to_input(&input, manifest_input, &needs_c_library_validation, diag) ||
      (needs_c_library_validation && !validate_c_libraries_for_target(&input, target, command, diag))) {
    z_free_source(&input);
    z_program_graph_store_free(&store);
    return EARLY_CACHED_RUN_FAILED;
  }
  if (graph_has_direct_c_import_node(&store.graph)) {
    z_free_source(&input);
    z_program_graph_store_free(&store);
    return EARLY_CACHED_RUN_VALIDATED_NO_HIT;
  }
  Command cache_command = *command;
  cache_command.graph_source.graph_hash = store.graph.graph_hash ? store.graph.graph_hash : "";
  cache_command.graph_source.artifact = command->input;
  ZToolchainPlan runtime_toolchain = z_plan_toolchain(command->cc, command->profile, target);
  const char *direct_obj_request = z_backend_direct_request_name(command->backend);
  ZDirectObjectTargetFacts direct_obj = z_direct_object_target_facts(target);
  if (direct_obj_request && !z_direct_requested_backend_matches(direct_obj_request, direct_obj.backend)) {
    z_free_source(&input);
    z_program_graph_store_free(&store);
    return EARLY_CACHED_RUN_NOT_APPLICABLE;
  }
  if (!direct_obj.available) {
    z_free_source(&input);
    z_program_graph_store_free(&store);
    return EARLY_CACHED_RUN_NOT_APPLICABLE;
  }
  for (int http = 0; http < 2; http++) {
    ZDirectRuntimeObjectFacts runtime_object = z_direct_runtime_object_facts(target, http != 0);
    if (!runtime_object.supported) continue;
    char *cache_path = linked_executable_cache_path(&cache_command, &input, target, &runtime_toolchain, true, http != 0, runtime_object.cache_key);
    if (cache_path && z_process_executable_file_ready(cache_path)) {
      char *base_argv0 = command->out ? z_strdup(command->out) : command_default_exe_base_path(&cache_command, &input), *argv0_path = apply_target_suffix(base_argv0, target); free(base_argv0);
      int rc = exec_cached_executable_artifact(cache_path, argv0_path, command);
      free(argv0_path);
      if (rc_out) *rc_out = rc;
      free(cache_path);
      z_free_source(&input);
      z_program_graph_store_free(&store);
      return EARLY_CACHED_RUN_HANDLED;
    }
    free(cache_path);
  }
  z_free_source(&input);
  z_program_graph_store_free(&store);
  return EARLY_CACHED_RUN_VALIDATED_NO_HIT;
}

static EarlyCachedRunResult try_run_manifest_graph_cached_executable_before_resolution(const Command *command, const ZTargetInfo *target, int *rc_out, ZDiag *diag) {
  if (!command || !command->input || !command->command || strcmp(command->command, "run") != 0) return EARLY_CACHED_RUN_NOT_APPLICABLE;
  if (command->repository_graph_input || path_has_program_graph_storage_header(command->input)) return EARLY_CACHED_RUN_NOT_APPLICABLE;
  char *manifest_path = z_manifest_path_for_input(command->input);
  if (!manifest_path) return EARLY_CACHED_RUN_NOT_APPLICABLE;
  free(manifest_path);
  char *root = z_program_graph_store_root_for_input(command->input);
  char *store_path = z_program_graph_store_path_for_root(root);
  free(root);
  if (!store_path || !z_program_graph_projection_cached_run_allows_cache(command->input)) {
    free(store_path);
    return EARLY_CACHED_RUN_NOT_APPLICABLE;
  }
  Command cache_command = *command;
  cache_command.input = store_path;
  cache_command.repository_graph_source_input = command->input;
  cache_command.repository_graph_input = true;
  EarlyCachedRunResult result = try_run_repository_graph_cached_executable_before_mir(&cache_command, target, rc_out, diag, false);
  free(store_path);
  return result;
}

int main(int argc, char **argv) {
  if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "help") == 0)) {
    print_help_with_fmt_modes();
    return 0;
  }
  Command command = {0};
  if (!parse_command(argc, argv, &command)) {
    print_help_with_fmt_modes();
    return 1;
  }
  if (command.retired_graph_subcommand || strcmp(command.command, "graph") == 0) {
    return reject_retired_graph_subcommand(&command);
  }
  if (command.kind && strcmp(command.kind, "help") == 0) {
    print_command_help_with_fmt_modes(command.command);
    return 0;
  }
  if (command.unknown_flag) return reject_unknown_flag(&command);
  if (command.invalid_emit) {
    ZDiag diag = {0};
    diag.code = 2002;
    diag.line = 1;
    diag.column = 1;
    diag.length = 1;
    snprintf(diag.message, sizeof(diag.message), "unknown emit kind '%s'", command.invalid_emit);
    snprintf(diag.expected, sizeof(diag.expected), "one of exe, obj, llvm-ir");
    snprintf(diag.actual, sizeof(diag.actual), "--emit %s", command.invalid_emit);
    snprintf(diag.help, sizeof(diag.help), "use --emit exe or --emit obj, or use --backend llvm --emit llvm-ir for textual LLVM IR");
    if (command.json) print_command_diag_json(&command, command.input, &diag);
    else print_diag(command.input, &diag);
    return 1;
  }
  if (strcmp(command.command, "--version") == 0 || strcmp(command.command, "version") == 0) {
    return print_version_command(command.json);
  }
  if (strcmp(command.command, "targets") == 0) {
    ZBuf targets;
    zbuf_init(&targets);
    z_append_targets_json(&targets);
    fputs(targets.data, stdout);
    zbuf_free(&targets);
    return 0;
  }
  if (strcmp(command.command, "doctor") == 0) {
    return doctor_command(command.json);
  }
  if (strcmp(command.command, "clean") == 0) {
    return clean_command(&command);
  }
  if (strcmp(command.command, "skills") == 0) {
    return embedded_skills_command(argc, argv, command.json);
  }
  if (command.graph_patch_command &&
      graph_check_text_eq(command.command, "patch") &&
      command.input &&
      !command.patch_file &&
      command.patch_op_len == 0 &&
      !command.patch_text &&
      !command.patch_body_file &&
      !command.patch_replace_fn &&
      !command.patch_replace_in_fn &&
      (path_has_program_graph_patch_header(command.input) ||
       path_starts_with_program_graph_patch_operation(command.input))) {
    command.patch_file = command.input;
    command.input = ".";
  }
  if (!command.input && command.graph_patch_command && (command.patch_op_len > 0 || command.patch_text || command.patch_file || command.patch_body_file || command.patch_replace_fn || command.patch_replace_in_fn)) {
    command.input = ".";
  }
  command_apply_query_bare_argument(&command);
  command_apply_current_directory_default(&command);
  if (!command.input) {
    print_command_help_with_fmt_modes(command.command);
    return 1;
  }

  long long command_started_ms = now_ms();
  ZDiag diag = {0};
  const ZTargetInfo *target = z_find_target(command.target);
  if (!z_is_known_target(command.target) || !target) {
    diag.code = 6001;
    diag.line = 1;
    diag.column = 1;
    snprintf(diag.message, sizeof(diag.message), "unknown target '%s'", command.target);
    snprintf(diag.expected, sizeof(diag.expected), "one of zero targets");
    snprintf(diag.actual, sizeof(diag.actual), "%s", command.target);
    snprintf(diag.help, sizeof(diag.help), "run zero targets and choose a supported target name");
    if (command.json) print_command_diag_json(&command, command.input, &diag);
    else print_diag(command.input, &diag);
    return 1;
  }

  if (!z_backend_request_is_known(command.backend, emit_kind_name(command.emit))) {
    z_backend_init_unknown_diag(&diag, command.backend, command.input);
    if (command.json) print_command_diag_json(&command, command.input, &diag); else print_diag(command.input, &diag);
    return 1;
  }

  if (command.legacy_backend || command.emit == EMIT_C) {
    diag.code = 2003;
    diag.line = 1;
    diag.column = 1;
    diag.length = 1;
    snprintf(diag.message, sizeof(diag.message), "C backend output is not supported");
    snprintf(diag.expected, sizeof(diag.expected), "zero build --emit exe|obj [graph-input]");
    snprintf(diag.actual, sizeof(diag.actual), command.legacy_backend ? "--legacy-backend" : "--emit c");
    snprintf(diag.help, sizeof(diag.help), "use direct emitters; C backend output is not a compatibility or debug path");
    if (command.json) print_command_diag_json(&command, command.input, &diag);
    else print_diag(command.input, &diag);
    return 1;
  }

  if (strcmp(command.command, "run") == 0) {
    if (command.json) {
      diag.code = 2002;
      diag.line = 1;
      diag.column = 1;
      diag.length = 1;
      snprintf(diag.message, sizeof(diag.message), "zero run does not support --json");
      snprintf(diag.expected, sizeof(diag.expected), "zero run [graph-input]");
      snprintf(diag.actual, sizeof(diag.actual), "zero run --json");
      snprintf(diag.help, sizeof(diag.help), "program stdout belongs to the program; use zero build --json to inspect the artifact before running it");
      print_command_diag_json(&command, command.input, &diag);
      return 1;
    }
    if (command.emit != EMIT_EXE) {
      if (command.emit == EMIT_LLVM_IR) {
        if (z_backend_request_is_llvm(command.backend, "llvm-ir")) init_llvm_ir_build_only_diag(&diag, &command, target, command.input);
        else init_direct_llvm_ir_unavailable_diag(&diag, &command, target, command.input);
        print_diag(command.input, &diag); return 1;
      }
      diag.code = 2002;
      diag.line = 1; diag.column = 1; diag.length = 1;
      snprintf(diag.message, sizeof(diag.message), "zero run only supports executable output");
      snprintf(diag.expected, sizeof(diag.expected), "zero run [graph-input]");
      snprintf(diag.actual, sizeof(diag.actual), "--emit %s", emit_kind_name(command.emit));
      snprintf(diag.help, sizeof(diag.help), "use zero build --emit %s when you need a non-executable artifact", emit_kind_name(command.emit));
      print_diag(command.input, &diag);
      return 1;
    }
    if (!z_target_is_host(target)) {
      diag.code = 2002;
      diag.line = 1;
      diag.column = 1;
      diag.length = 1;
      snprintf(diag.message, sizeof(diag.message), "zero run requires the host target");
      snprintf(diag.expected, sizeof(diag.expected), "target %s", z_host_target());
      snprintf(diag.actual, sizeof(diag.actual), "target %s", target && target->name ? target->name : "unknown");
      snprintf(diag.help, sizeof(diag.help), "use zero build --target %s for cross-target artifacts, then run them on a matching host", target && target->name ? target->name : "<target>");
      print_diag(command.input, &diag);
      return 1;
    }
  }

  if (command_is_program_graph_command(&command) && command.out && !z_program_graph_command_kind_supports_out(command.kind)) {
    return reject_graph_unsupported_out(&command, &diag);
  }
  if (strcmp(command.command, "test") == 0 && command.out) {
    Command test_command = command;
    test_command.kind = "test";
    return reject_graph_unsupported_out(&test_command, &diag);
  }

  if (strcmp(command.command, "explain") == 0) {
    return explain_command(&command);
  }

  if (strcmp(command.command, "fix") == 0 && !command.plan && !command.apply && !command.patch) {
    diag.code = 2002;
    diag.line = 1;
    diag.column = 1;
    snprintf(diag.message, sizeof(diag.message), "fix requires a mode");
    snprintf(diag.expected, sizeof(diag.expected), "zero fix --plan, --patch, or --apply --json [graph-input]");
    snprintf(diag.actual, sizeof(diag.actual), "zero fix without a mode");
    snprintf(diag.help, sizeof(diag.help), "rerun with --plan to inspect repairs, --patch to preview edits, or --apply for behavior-preserving edits");
    if (command.json) print_command_diag_json(&command, command.input, &diag);
    else print_diag(command.input, &diag);
    return 1;
  }

  bool graph_command_artifact_input = false;
  if (!resolve_graph_command_manifest_input(&command, &graph_command_artifact_input, &diag)) {
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    return 1;
  }

  bool graph_subcommand_handled = false;
  int graph_subcommand_rc = run_graph_subcommand_dispatch(&command, target, graph_command_artifact_input, &diag, &graph_subcommand_handled);
  if (graph_subcommand_handled) return command_return(&command, graph_subcommand_rc);

  if (graph_check_text_eq(command.command, "patch")) {
    bool top_patch_artifact_input = false;
    if (!resolve_graph_repository_store_input(&command, &top_patch_artifact_input, &diag)) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      return command_return(&command, 1);
    }
    int patch_rc = run_graph_patch_command(&command, target, &diag);
    return command_return(&command, patch_rc);
  }

  if (strcmp(command.command, "fmt") == 0) {
    if (command.fmt_check && command.fmt_write) {
      fprintf(stderr, "zero fmt: --check and --write are mutually exclusive\n");
      fprintf(stderr, "  help: pass --check OR --write, not both\n");
      return 1;
    }
    SourceInput fmt_input = {0};
    if (!load_format_source(command.input, &fmt_input, &diag)) {
      if (command.json) print_command_diag_json(&command, command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      return 1;
    }
    if (!fmt_input.source) {
      if (command.json) print_command_diag_json(&command, command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_source(&fmt_input);
      return 1;
    }
    diag.path = fmt_input.source_file;
    char *formatted = NULL;
    ZBuf out;
    zbuf_init(&out);
    if (z_canonical_text_format_source(fmt_input.source, &out, &diag)) {
      formatted = out.data;
    } else {
      zbuf_free(&out);
    }
    if (!formatted || diag.code != 0) {
      z_map_source_diag(&fmt_input, &diag);
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      free(formatted);
      z_free_source(&fmt_input);
      return 1;
    }
    const char *path_for_output = fmt_input.source_file ? fmt_input.source_file : command.input;
    bool matches = strcmp(formatted, fmt_input.source) == 0;
    if (command.fmt_check) {
      if (command.json) {
        ZBuf buf;
        zbuf_init(&buf);
        zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"command\": \"fmt\",\n  \"mode\": \"check\",\n  \"sourceFile\": ");
        append_json_string(&buf, path_for_output);
        zbuf_append(&buf, ",\n  \"ok\": ");
        zbuf_append(&buf, matches ? "true" : "false");
        zbuf_append(&buf, ",\n  \"unformatted\": [");
        if (!matches) {
          append_json_string(&buf, path_for_output);
        }
        zbuf_append(&buf, "]\n}\n");
        fputs(buf.data, stdout);
        zbuf_free(&buf);
      } else if (matches) {
        printf("fmt ok\n");
      } else {
        fprintf(stderr, "format differs: %s\n", path_for_output);
      }
      free(formatted);
      z_free_source(&fmt_input);
      return matches ? 0 : 1;
    }
    if (command.fmt_write) {
      if (!fmt_input.source_file) {
        fprintf(stderr, "zero fmt: --write requires a source file path on disk\n");
        fprintf(stderr, "  help: pass a .0 file or a project/zero.json that resolves to one\n");
        free(formatted);
        z_free_source(&fmt_input);
        return 1;
      }
      bool wrote = false;
      if (!matches) {
        FILE *out = fopen(fmt_input.source_file, "wb");
        if (!out) {
          fprintf(stderr, "zero fmt: could not open %s for writing\n", fmt_input.source_file);
          free(formatted);
          z_free_source(&fmt_input);
          return 1;
        }
        size_t len = strlen(formatted);
        size_t written = fwrite(formatted, 1, len, out);
        fclose(out);
        if (written != len) {
          fprintf(stderr, "zero fmt: short write to %s (%zu of %zu bytes)\n", fmt_input.source_file, written, len);
          free(formatted);
          z_free_source(&fmt_input);
          return 1;
        }
        wrote = true;
      }
      if (command.json) {
        ZBuf buf;
        zbuf_init(&buf);
        zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"command\": \"fmt\",\n  \"mode\": \"write\",\n  \"sourceFile\": ");
        append_json_string(&buf, path_for_output);
        zbuf_append(&buf, ",\n  \"ok\": true,\n  \"written\": [");
        if (wrote) {
          append_json_string(&buf, path_for_output);
        }
        zbuf_append(&buf, "]\n}\n");
        fputs(buf.data, stdout);
        zbuf_free(&buf);
      } else if (wrote) {
        printf("formatted: %s\n", path_for_output);
      } else {
        printf("unchanged: %s\n", path_for_output);
      }
      free(formatted);
      z_free_source(&fmt_input);
      return 0;
    }
    fputs(formatted, stdout);
    free(formatted);
    z_free_source(&fmt_input);
    return 0;
  }

  if (strcmp(command.command, "tokens") == 0) {
    SourceInput token_input = {0};
    if (!load_command_source(command.input, &token_input, &diag)) {
      if (command.json) print_command_diag_json(&command, command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      return 1;
    }
    if (!token_input.source) {
      if (command.json) print_command_diag_json(&command, command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_source(&token_input);
      return 1;
    }
    diag.path = token_input.source_file;
    ZCanonicalTokenVec tokens = z_canonical_text_tokenize(token_input.source, &diag);
    if (diag.code != 0) {
      z_map_source_diag(&token_input, &diag);
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_canonical_text_tokens(&tokens);
      z_free_source(&token_input);
      return 1;
    }
    if (command.json) {
      ZBuf buf;
      zbuf_init(&buf);
      append_canonical_tokens_json(&buf, token_input.source_file, &tokens);
      fputs(buf.data, stdout);
      zbuf_free(&buf);
    } else {
      for (size_t i = 0; i < tokens.len; i++) {
        printf("%s %s %d:%d\n", canonical_token_kind_name(tokens.items[i].kind), tokens.items[i].text, tokens.items[i].line, tokens.items[i].column);
      }
    }
    z_free_canonical_text_tokens(&tokens);
    z_free_source(&token_input);
    return 0;
  }

  if (strcmp(command.command, "parse") == 0) {
    SourceInput parse_input = {0};
    if (!load_command_source(command.input, &parse_input, &diag)) {
      if (command.json) print_command_diag_json(&command, command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      return 1;
    }
    if (!parse_input.source) {
      if (command.json) print_command_diag_json(&command, command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_source(&parse_input);
      return 1;
    }
    diag.path = parse_input.source_file;
    Program parsed = {0};
    z_parse_canonical_text_program_source(parse_input.source, &parsed, &diag);
    if (diag.code != 0) {
      z_map_source_diag(&parse_input, &diag);
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_program(&parsed);
      z_free_source(&parse_input);
      return 1;
    }
    if (command.json) {
      ZBuf buf;
      zbuf_init(&buf);
      append_parse_json(&buf, parse_input.source_file, &parsed);
      fputs(buf.data, stdout);
      zbuf_free(&buf);
    } else {
      printf("parse ok: %s\n", parse_input.source_file ? parse_input.source_file : command.input);
    }
    z_free_program(&parsed);
    z_free_source(&parse_input);
    return 0;
  }

  resolve_compiler_command_package_source_input(&command);
  const char *direct_graph_manifest_input = command.input;
  bool direct_graph_manifest_command = false;
  int repository_graph_check_rc = run_repository_graph_check_command(&command, target, &direct_graph_manifest_command);
  if (repository_graph_check_rc != 0 || direct_graph_manifest_command) return repository_graph_check_rc;
  if (graph_check_text_eq(command.command, "check") && path_has_program_graph_storage_header(command.input)) {
    if (command.out) {
      Command check_command = command;
      check_command.kind = "check";
      return reject_graph_unsupported_out(&check_command, &diag);
    }
    return run_graph_check_command(&command, target, &diag);
  }
  int manifest_cached_run_rc = 1;
  EarlyCachedRunResult manifest_cached_run = try_run_manifest_graph_cached_executable_before_resolution(&command, target, &manifest_cached_run_rc, &diag);
  if (manifest_cached_run == EARLY_CACHED_RUN_HANDLED) return manifest_cached_run_rc;
  if (manifest_cached_run == EARLY_CACHED_RUN_FAILED) {
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    return 1;
  }
  int direct_graph_manifest_rc = resolve_direct_command_manifest_graph_input(&command, target, &direct_graph_manifest_command); if (direct_graph_manifest_rc != 0) return direct_graph_manifest_rc;
  if (strcmp(command.command, "test") == 0 && (direct_graph_manifest_command || path_has_program_graph_storage_header(command.input))) return run_direct_graph_test_command(&command, target, &diag);

  SourceInput input = {0};
  Program program = {0};
  IrProgram graph_prepared_ir = {0};
  bool command_is_build = strcmp(command.command, "build") == 0, command_is_run = strcmp(command.command, "run") == 0, command_is_size = strcmp(command.command, "size") == 0, command_is_mem = strcmp(command.command, "mem") == 0;
  bool command_is_doc = strcmp(command.command, "doc") == 0, command_is_dev = strcmp(command.command, "dev") == 0, command_is_fix = strcmp(command.command, "fix") == 0, command_is_time = strcmp(command.command, "time") == 0, command_is_abi = strcmp(command.command, "abi") == 0;
  bool root_graph_artifact_input = !command.repository_graph_input && path_has_program_graph_storage_header(command.input);
  bool graph_build_command = command_is_build && root_graph_artifact_input;
  bool graph_run_artifact_command = command_is_run && root_graph_artifact_input;
  bool graph_size_artifact_command = command_is_size && root_graph_artifact_input;
  bool graph_mem_artifact_command = command_is_mem && root_graph_artifact_input;
  bool graph_doc_artifact_command = command_is_doc && root_graph_artifact_input;
  bool graph_dev_artifact_command = command_is_dev && root_graph_artifact_input;
  bool graph_time_artifact_command = command_is_time && root_graph_artifact_input;
  bool graph_fix_artifact_command = command_is_fix && root_graph_artifact_input;
  bool graph_abi_artifact_command = command_is_abi && root_graph_artifact_input;
  bool graph_artifact_mir_command = graph_build_command ||
                                    graph_run_artifact_command ||
                                    graph_size_artifact_command ||
                                    graph_mem_artifact_command ||
                                    graph_doc_artifact_command ||
                                    graph_dev_artifact_command ||
                                    graph_time_artifact_command ||
                                    graph_fix_artifact_command;
  bool graph_metadata_command = (command.repository_graph_input && (command_is_doc || command_is_dev || command_is_abi)) ||
                                graph_abi_artifact_command;
  if (command.repository_graph_input && command_is_run) {
    int cached_run_rc = 1;
    EarlyCachedRunResult cached_run = try_run_repository_graph_cached_executable_before_mir(&command, target, &cached_run_rc, &diag, false);
    if (cached_run == EARLY_CACHED_RUN_HANDLED) return cached_run_rc;
    if (cached_run == EARLY_CACHED_RUN_FAILED) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      return 1;
    }
  }
  if ((command.repository_graph_input || root_graph_artifact_input) && (command_is_run || command_is_build)) {
    ZProgramGraphStore listen_store;
    ZProgramGraph artifact_listen_graph = {0};
    z_program_graph_store_init(&listen_store);
    const ZProgramGraph *listen_graph = NULL;
    if (root_graph_artifact_input) {
      if (!z_program_graph_load(command.input, &artifact_listen_graph, &diag)) {
        if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
        else print_diag(diag.path ? diag.path : command.input, &diag);
        z_program_graph_free(&artifact_listen_graph);
        z_program_graph_store_free(&listen_store);
        return 1;
      }
      listen_graph = &artifact_listen_graph;
    } else if (!z_program_graph_store_load_for_input(command.input, &listen_store, &diag)) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_program_graph_free(&artifact_listen_graph);
      z_program_graph_store_free(&listen_store);
      return 1;
    } else {
      listen_graph = &listen_store.graph;
    }
    HttpListenSpec graph_http_listen = {0};
    bool graph_has_http_listen = listen_graph && find_http_listen_graph_program(listen_graph, &graph_http_listen, &diag);
    if (graph_has_http_listen && diag.code != 0) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_program_graph_free(&artifact_listen_graph);
      z_program_graph_store_free(&listen_store);
      return 1;
    }
    if (graph_has_http_listen) {
      if (!validate_http_listen_handler_graph(listen_graph, &graph_http_listen, &diag)) {
        if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
        else print_diag(diag.path ? diag.path : command.input, &diag);
        z_program_graph_free(&artifact_listen_graph);
        z_program_graph_store_free(&listen_store);
        return 1;
      }
      int listen_rc = run_http_listen_or_diag(&command, target, &graph_http_listen, argv[0], &diag);
      if (listen_rc != 0 && diag.code != 0) {
        if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
        else print_diag(diag.path ? diag.path : command.input, &diag);
      }
      z_program_graph_free(&artifact_listen_graph);
      z_program_graph_store_free(&listen_store);
      return listen_rc;
    }
    z_program_graph_free(&artifact_listen_graph);
    z_program_graph_store_free(&listen_store);
  }
  if (graph_metadata_command) {
    if (!prepare_graph_metadata_command(&command, direct_graph_manifest_input, direct_graph_manifest_command, target, &input, &program, &diag)) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      free_loaded_command_state(&input, &program, &graph_prepared_ir);
      return 1;
    }
  } else if (direct_graph_manifest_command || graph_artifact_mir_command) {
    ZProgramGraphArtifactSource graph_source = {0};
    long long graph_lower_started = now_ms();
    if (command.repository_graph_input && command.emit == EMIT_LLVM_IR) {
      if (!z_backend_request_is_llvm(command.backend, emit_kind_name(command.emit))) {
        init_direct_llvm_ir_unavailable_diag(&diag, &command, target, command.input);
        if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
        else print_diag(diag.path ? diag.path : command.input, &diag);
        return 1;
      }
      if (!graph_check_text_eq(command.command, "build")) {
        init_llvm_ir_build_only_diag(&diag, &command, target, command.input);
        if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
        else print_diag(diag.path ? diag.path : command.input, &diag);
        return 1;
      }
    }
    int cached_run_rc = 1;
    EarlyCachedRunResult cached_run = try_run_repository_graph_cached_executable_before_mir(&command, target, &cached_run_rc, &diag, true);
    if (cached_run == EARLY_CACHED_RUN_HANDLED) {
      free_loaded_command_state(&input, &program, &graph_prepared_ir);
      return cached_run_rc;
    }
    if (cached_run == EARLY_CACHED_RUN_FAILED) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      free_loaded_command_state(&input, &program, &graph_prepared_ir);
      return 1;
    }
    if (cached_run == EARLY_CACHED_RUN_NOT_APPLICABLE && !validate_repository_graph_c_libraries_before_mir(&command, target, &diag)) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      free_loaded_command_state(&input, &program, &graph_prepared_ir);
      return 1;
    }
    bool prepared_graph = command.repository_graph_input
      ? z_program_graph_prepare_repository_store_mir_input(command.input, target, emit_kind_name(command.emit), command.backend, !(command_is_build || command_is_run || command_is_size || command_is_mem), !command_is_run, &program, &input, &graph_prepared_ir, &graph_source, &diag)
      : z_program_graph_prepare_artifact_mir_input(command.input, target, emit_kind_name(command.emit), command.backend, &program, &input, &graph_prepared_ir, &graph_source, &diag);
    if (prepared_graph) {
      input.lower_ms = now_ms() - graph_lower_started;
      apply_ir_metrics_to_input(&input, &graph_prepared_ir, target);
    }
    if (!prepared_graph) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      free_loaded_command_state(&input, &program, &graph_prepared_ir);
      return 1;
    }
    if (direct_graph_manifest_command && !z_program_graph_manifest_attach_metadata_to_input(&input, direct_graph_manifest_input, &diag)) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : direct_graph_manifest_input, &diag);
      else print_diag(diag.path ? diag.path : direct_graph_manifest_input, &diag);
      free_loaded_command_state(&input, &program, &graph_prepared_ir);
      return 1;
    }
    touch_program_graph_compiler_caches(&input, target, command.profile, graph_source.graph_hash);
    command.graph_source = graph_source;
    if (graph_build_command || graph_run_artifact_command) {
      command.command = graph_run_artifact_command ? "run" : "build";
      command.kind = NULL;
    }
  } else if (z_program_graph_manifest_command_can_use_compiler_input(command.command)) {
    set_graph_input_required_diag(command.input, NULL, &diag);
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    return 1;
  } else if (!compile_input(command.input, target, command.profile, &input, &program, &diag)) {
    if (strcmp(command.command, "fix") == 0) {
      if (command.apply || command.patch) {
        int rc = print_or_apply_fix_json(diag.path ? diag.path : command.input, &input, &diag, command.apply);
        free_loaded_command_state(&input, &program, NULL);
        return rc;
      }
      print_fix_plan_json(diag.path ? diag.path : command.input, &diag);
      free_loaded_command_state(&input, &program, NULL);
      return 0;
    }
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    free_diag_path_if_owned(&diag, &input, command.input);
    free_loaded_command_state(&input, &program, NULL);
    return 1;
  }

  bool is_graph_command = command_is_program_graph_command(&command);
  if (!is_graph_command && !validate_target_capabilities(&program, target, &diag, input.source_file)) {
    if (strcmp(command.command, "fix") == 0) {
      print_fix_plan_json(input.source_file, &diag);
      free_loaded_command_state(&input, &program, &graph_prepared_ir);
      return 0;
    }
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    free_loaded_command_state(&input, &program, &graph_prepared_ir);
    return 1;
  }

  if (!is_graph_command && !validate_package_dependencies_for_target(&input, target, &diag)) {
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    free_loaded_command_state(&input, &program, &graph_prepared_ir);
    return 1;
  }

  if ((strcmp(command.command, "build") == 0 || strcmp(command.command, "run") == 0) &&
      !validate_c_libraries_for_target(&input, target, &command, &diag)) {
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    free_loaded_command_state(&input, &program, &graph_prepared_ir);
    return 1;
  }

  HttpListenSpec http_listen = {0};
  bool has_http_listen = find_http_listen_program(&program, &http_listen, &diag);
  if (has_http_listen && diag.code != 0) {
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    free_loaded_command_state(&input, &program, &graph_prepared_ir);
    return 1;
  }
  if (has_http_listen && strcmp(command.command, "build") == 0) {
    diag.code = 2002;
    diag.line = http_listen.line > 0 ? http_listen.line : 1;
    diag.column = http_listen.column > 0 ? http_listen.column : 1;
    snprintf(diag.message, sizeof(diag.message), "std.http.listen standalone artifacts are not implemented yet");
    snprintf(diag.expected, sizeof(diag.expected), "zero run starts the host listener");
    snprintf(diag.actual, sizeof(diag.actual), "zero %s requested a listen artifact", command.command);
    snprintf(diag.help, sizeof(diag.help), "run the graph package with zero run, or keep build targets to request/response handler functions for now");
    if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    free_loaded_command_state(&input, &program, &graph_prepared_ir);
    return 1;
  }
  if (has_http_listen && strcmp(command.command, "run") == 0) {
    if (!validate_http_listen_handler(&program, &http_listen, &diag)) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      free_loaded_command_state(&input, &program, &graph_prepared_ir);
      return 1;
    }
    ZHttpListenRunConfig listen_config = {
      .zero_exe = argv[0],
      .input = command.input,
      .target = command.target,
      .profile = command.profile,
      .backend = command.backend,
      .cc = command.cc,
      .handler = http_listen.handler,
      .port = http_listen.port,
      .auto_increment_port = http_listen.auto_increment_port,
    };
    int listen_rc = z_http_listen_run(&listen_config, &diag);
    if (listen_rc != 0 && diag.code != 0) {
      if (command.json) print_command_diag_json(&command, diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
    }
    free_loaded_command_state(&input, &program, &graph_prepared_ir);
    return listen_rc;
  }
  if (strcmp(command.command, "fix") == 0) {
    if (command.apply || command.patch) print_or_apply_fix_json(input.source_file, &input, NULL, command.apply);
    else print_fix_plan_json(input.source_file, NULL);
    free_loaded_command_state(&input, &program, NULL);
    return 0;
  }

  if (strcmp(command.command, "check") == 0) {
    if (command.json) print_check_json_success(input.source_file, &input, &program, target, &command, &graph_prepared_ir);
    else printf("ok\n");
    free_loaded_command_state(&input, &program, &graph_prepared_ir);
    return 0;
  }

  if (strcmp(command.command, "doc") == 0) {
    ZBuf doc;
    zbuf_init(&doc);
    append_public_docs_json(&doc, &input, &program, target);
    fputs(doc.data, stdout);
    zbuf_free(&doc);
    free_loaded_command_state(&input, &program, NULL);
    return 0;
  }

  if (strcmp(command.command, "dev") == 0) {
    ZBuf dev;
    zbuf_init(&dev);
    append_dev_plan_json(&dev, &input, &program, target, &command);
    fputs(dev.data, stdout);
    zbuf_free(&dev);
    free_loaded_command_state(&input, &program, NULL);
    return 0;
  }

  if (strcmp(command.command, "time") == 0) {
    ZBuf time_json;
    zbuf_init(&time_json);
    append_time_json(&time_json, &input, &program, target, &command);
    fputs(time_json.data, stdout);
    zbuf_free(&time_json);
    free_loaded_command_state(&input, &program, NULL);
    return 0;
  }

  if (strcmp(command.command, "abi") == 0) {
    int abi_rc = run_abi_command(&command, &input, &program, target);
    free_loaded_command_state(&input, &program, NULL);
    return abi_rc;
  }

  if (strcmp(command.command, "test") == 0) {
    if (!metadata_backend_request_buildable(&command, &input, target, &diag)) {
      int rc = return_buildability_error(&command, &input, &diag, NULL, &program);
      z_free_source(&input);
      return rc;
    }
    int rc = run_tests_direct(&command, &input, &program, target);
    free_loaded_command_state(&input, &program, &graph_prepared_ir);
    return rc;
  }

  if (is_graph_command) {
    int rc = run_graph_command(&command, &input, &program, target, &diag);
    free_loaded_command_state(&input, &program, NULL);
    return rc;
  }

  long long phase_started = now_ms();
  bool use_prepared_ir = graph_prepared_ir.mir_bytes > 0;
  IrProgram ir = use_prepared_ir ? graph_prepared_ir : z_lower_program_with_source(&program, &input, target);
  if (use_prepared_ir) graph_prepared_ir = (IrProgram){0};
  else {
    input.lower_ms = now_ms() - phase_started;
    apply_ir_metrics_to_input(&input, &ir, target);
  }
  if (strcmp(command.command, "mem") == 0) {
    if (!metadata_backend_request_buildable(&command, &input, target, &diag)) {
      int rc = return_buildability_error(&command, &input, &diag, &ir, &program);
      z_free_source(&input);
      return rc;
    }
    ZBuf mem_json;
    zbuf_init(&mem_json);
    append_direct_memory_json(&mem_json, &input, &program, target, &command, &ir);
    fputs(mem_json.data, stdout);
    zbuf_free(&mem_json);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  bool build_command = strcmp(command.command, "build") == 0;
  bool run_command = strcmp(command.command, "run") == 0;
  bool size_command = strcmp(command.command, "size") == 0;
  bool artifact_command = build_command || run_command;
  if (size_command && !metadata_backend_request_buildable(&command, &input, target, &diag)) {
    int rc = return_buildability_error(&command, &input, &diag, &ir, &program);
    z_free_source(&input);
    return rc;
  }
  if (artifact_command && command.emit == EMIT_LLVM_IR) {
    if (!z_backend_request_is_llvm(command.backend, emit_kind_name(command.emit))) {
      init_direct_llvm_ir_unavailable_diag(&diag, &command, target, input.source_file);
      int rc = return_buildability_error(&command, &input, &diag, &ir, &program); z_free_source(&input); return rc;
    }
    if (!build_command) {
      init_llvm_ir_build_only_diag(&diag, &command, target, input.source_file);
      int rc = return_buildability_error(&command, &input, &diag, &ir, &program); z_free_source(&input); return rc;
    }
    if (!ir.mir_valid) {
      init_lowering_backend_diag(&diag, &input, target, &command, &ir);
      int rc = return_buildability_error(&command, &input, &diag, &ir, &program);
      z_free_source(&input);
      return rc;
    }
    ZBuf llvm_ir;
    phase_started = now_ms();
    bool emitted_llvm_ir = z_emit_llvm_ir_from_ir(&ir, &llvm_ir, &diag);
    input.codegen_ms = now_ms() - phase_started;
    if (!emitted_llvm_ir) {
      int rc = return_buildability_error(&command, &input, &diag, &ir, &program);
      z_free_source(&input);
      return rc;
    }
    char *base_llvm_file = command.out ? z_strdup(command.out) : command_default_out_path(&command, &input);
    char *llvm_file = command.out ? base_llvm_file : path_with_suffix(base_llvm_file, ".ll");
    if (!command.out) free(base_llvm_file);
    phase_started = now_ms();
    input.emitted_object_cache_hit = compiler_cache_touch("emitted-llvm-ir", command_compile_cache_key(&command, &input, target, command.profile, "llvm-ir"));
    bool wrote_llvm_ir = z_write_file(llvm_file, llvm_ir.data ? llvm_ir.data : "", &diag);
    input.object_ms = now_ms() - phase_started;
    input.link_ms = 0;
    if (!wrote_llvm_ir) {
      if (command.json) print_diag_json(llvm_file, &diag);
      else print_diag(llvm_file, &diag);
      free(llvm_file);
      zbuf_free(&llvm_ir);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }
    long long elapsed_ms = now_ms() - command_started_ms;
    if (command.json) print_build_json(&command, &input, &program, &ir, target, "llvm-ir", llvm_file, file_size_or_negative(llvm_file), 0, elapsed_ms);
    else print_artifact(llvm_file, elapsed_ms);
    free(llvm_file);
    zbuf_free(&llvm_ir);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  if (artifact_command && z_backend_request_is_llvm(command.backend, emit_kind_name(command.emit))) {
    return run_llvm_native_artifact_command(&command, &input, &program, &ir, target, command_started_ms, build_command, run_command);
  }
  if (command.emit == EMIT_OBJ) {
    if (strcmp(command.command, "build") != 0) {
      fprintf(stderr, "--emit obj is currently supported only by zero build\n");
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }
    ZDirectObjectTargetFacts direct_obj = z_direct_object_target_facts(target);
    const char *direct_obj_request = z_backend_direct_request_name(command.backend);
    if (direct_obj_request && !z_direct_requested_backend_matches(direct_obj_request, direct_obj.backend)) {
      init_direct_backend_request_mismatch_diag(&diag, &command, &input, target, "obj");
      int rc = return_buildability_error(&command, &input, &diag, &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (!direct_obj.available) {
      int rc = return_direct_backend_error(&command, &input, target, "obj", direct_obj.unsupported_reason, &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (!direct_buildability_preflight(&command, &input, target, "obj", &ir, &diag)) { int rc = return_buildability_error(&command, &input, &diag, &ir, &program); z_free_source(&input); return rc; }
    ZBuf object;
    phase_started = now_ms();
    bool emitted_object = z_emit_direct_object_from_ir(direct_obj.backend, &ir, &object, &diag);
    input.codegen_ms = now_ms() - phase_started;
    if (!emitted_object) {
      z_map_source_diag(&input, &diag);
      if (!diag.path) diag.path = input.source_file;
      complete_backend_blocker_diag(&diag, target, &command, "obj", "emit");
      if (command.json) print_diag_json(input.source_file, &diag);
      else print_diag(input.source_file, &diag);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }
    char *base_object_file = command.out ? z_strdup(command.out) : command_default_out_path(&command, &input);
    char *object_file = base_object_file;
    phase_started = now_ms();
    input.emitted_object_cache_hit = compiler_cache_touch("emitted-object", command_compile_cache_key(&command, &input, target, command.profile, direct_obj.artifact_path));
    bool wrote_object = z_write_binary_file(object_file, (const unsigned char *)object.data, object.len, &diag);
    input.object_ms = now_ms() - phase_started;
    input.link_ms = 0;
    if (!wrote_object) {
      print_diag(object_file, &diag);
      free(object_file);
      zbuf_free(&object);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    long long elapsed_ms = now_ms() - command_started_ms;
    if (command.json) print_build_json(&command, &input, &program, &ir, target, "obj", object_file, file_size_or_negative(object_file), 0, elapsed_ms);
    else print_artifact(object_file, elapsed_ms);
    free(object_file);
    zbuf_free(&object);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  if (artifact_command && command.emit == EMIT_EXE &&
      !validate_c_libraries_for_target(&input, target, &command, &diag)) {
    if (command.json) print_diag_json(diag.path ? diag.path : input.source_file, &diag);
    else print_diag(diag.path ? diag.path : input.source_file, &diag);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 1;
  }
  bool needs_linked_executable = artifact_command && command.emit == EMIT_EXE && ir_needs_linked_executable_object(&ir);
  bool needs_zero_runtime = needs_linked_executable && ir_linked_executable_needs_zero_runtime_object(&ir);
  if (needs_linked_executable) {
    bool needs_http_runtime = ir.direct_http_runtime_import_count > 0;
    ZDirectRuntimeObjectFacts runtime_object = z_direct_runtime_object_facts(target, needs_http_runtime);
    ZDirectObjectTargetFacts direct_obj = z_direct_object_target_facts(target);
    const char *direct_obj_request = z_backend_direct_request_name(command.backend);
    if (direct_obj_request && !z_direct_requested_backend_matches(direct_obj_request, direct_obj.backend)) {
      init_direct_backend_request_mismatch_diag(&diag, &command, &input, target, "exe");
      int rc = return_buildability_error(&command, &input, &diag, &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (!direct_obj.available) { int rc = return_direct_backend_error(&command, &input, target, "exe", direct_obj.unsupported_reason, &ir, &program); z_free_source(&input); return rc; }
    if (!direct_buildability_preflight(&command, &input, target, "exe", &ir, &diag)) {
      int rc = return_buildability_error(&command, &input, &diag, &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (needs_zero_runtime && !runtime_object.supported) { int rc = return_direct_backend_error(&command, &input, target, "exe", runtime_object.blocker, &ir, &program); z_free_source(&input); return rc; }
    char *base_exe_file = command.out ? z_strdup(command.out) : command_default_exe_base_path(&command, &input);
    char *exe_file = apply_target_suffix(base_exe_file, target);
    free(base_exe_file);
    char *object_file = path_with_suffix(exe_file, ".zero.o");
    char *runtime_object_file = needs_zero_runtime ? path_with_suffix(exe_file, ".zero-runtime.o") : NULL;
    char *http_object_file = needs_http_runtime ? path_with_suffix(exe_file, ".zero-http-curl.o") : NULL;
    ZToolchainPlan runtime_toolchain = z_plan_toolchain(command.cc, command.profile, target);
    char *exe_cache_path = linked_executable_cache_path(&command, &input, target, &runtime_toolchain, needs_zero_runtime, needs_http_runtime, needs_zero_runtime ? runtime_object.cache_key : direct_obj.artifact_path);
    if (exe_cache_path && z_process_executable_file_ready(exe_cache_path)) {
      input.emitted_object_cache_hit = true;
      if (run_command && command_uses_ephemeral_run_artifact(&command)) {
        int rc = run_executable_artifact_as(exe_cache_path, exe_file, &command);
        free_linked_executable_paths(exe_cache_path, http_object_file, runtime_object_file, object_file, exe_file);
        free_loaded_command_state(&input, &program, &ir);
        return rc;
      }
      if (executable_cache_restore_to_path(exe_cache_path, exe_file)) {
        if (run_command) {
          int rc = run_executable_artifact_as(exe_file, exe_file, &command);
          command_remove_ephemeral_run_artifact(&command, exe_file);
          free_linked_executable_paths(exe_cache_path, http_object_file, runtime_object_file, object_file, exe_file);
          free_loaded_command_state(&input, &program, &ir);
          return rc;
        }
        long long elapsed_ms = now_ms() - command_started_ms;
        if (command.json) print_build_json(&command, &input, &program, &ir, target, "exe", exe_file, file_size_or_negative(exe_file), 0, elapsed_ms);
        else print_artifact(exe_file, elapsed_ms);
        free_linked_executable_paths(exe_cache_path, http_object_file, runtime_object_file, object_file, exe_file);
        free_loaded_command_state(&input, &program, &ir);
        return 0;
      }
    }
    ZBuf object;
    zbuf_init(&object);
    phase_started = now_ms();
    bool emitted_object = z_emit_direct_object_from_ir(direct_obj.backend, &ir, &object, &diag);
    input.codegen_ms = now_ms() - phase_started;
    if (!emitted_object) {
      z_map_source_diag(&input, &diag);
      if (!diag.path) diag.path = input.source_file;
      complete_backend_blocker_diag(&diag, target, &command, "exe", "emit");
      if (command.json) print_diag_json(input.source_file, &diag);
      else print_diag(input.source_file, &diag);
      free_linked_executable_paths(exe_cache_path, http_object_file, runtime_object_file, object_file, exe_file);
      zbuf_free(&object);
      free_loaded_command_state(&input, &program, &ir);
      return 1;
    }

    phase_started = now_ms();
    input.emitted_object_cache_hit = compiler_cache_touch("emitted-object", command_compile_cache_key(&command, &input, target, command.profile, needs_zero_runtime ? runtime_object.cache_key : direct_obj.artifact_path));
    bool wrote_object = z_write_binary_file(object_file, (const unsigned char *)object.data, object.len, &diag);
    if (wrote_object && needs_zero_runtime) wrote_object = compile_zero_runtime_object(runtime_object_file, &runtime_toolchain, &command, target, false, &diag);
    if (wrote_object && needs_http_runtime) wrote_object = compile_zero_http_curl_object(http_object_file, &runtime_toolchain, &command, target, &diag);
    input.object_ms = now_ms() - phase_started;
    if (!wrote_object) {
      if (command.json) print_diag_json(diag.path ? diag.path : input.source_file, &diag);
      else print_diag(diag.path ? diag.path : input.source_file, &diag);
      free_linked_executable_paths(exe_cache_path, http_object_file, runtime_object_file, object_file, exe_file);
      zbuf_free(&object);
      free_loaded_command_state(&input, &program, &ir);
      return 1;
    }

    phase_started = now_ms();
    bool linked = link_direct_object_executable(object_file, runtime_object_file, http_object_file, exe_file, &runtime_toolchain, target, &input, needs_zero_runtime, &diag);
    if (linked && !z_process_mark_executable(exe_file)) {
      init_executable_finalize_diag(&diag, exe_file);
      linked = false;
    }
    input.link_ms = now_ms() - phase_started;
    (void)z_process_remove_regular_file(object_file);
    if (runtime_object_file) (void)z_process_remove_regular_file(runtime_object_file);
    if (http_object_file) (void)z_process_remove_regular_file(http_object_file);
    if (!linked) {
      if (command.json) print_diag_json(diag.path ? diag.path : input.source_file, &diag);
      else print_diag(diag.path ? diag.path : input.source_file, &diag);
      free_linked_executable_paths(exe_cache_path, http_object_file, runtime_object_file, object_file, exe_file);
      zbuf_free(&object);
      free_loaded_command_state(&input, &program, &ir);
      return 1;
    }
    executable_cache_store_path(exe_cache_path, exe_file);

    if (run_command) {
      int rc = run_executable_artifact_as(exe_file, exe_file, &command);
      command_remove_ephemeral_run_artifact(&command, exe_file);
      free_linked_executable_paths(exe_cache_path, http_object_file, runtime_object_file, object_file, exe_file);
      zbuf_free(&object);
      free_loaded_command_state(&input, &program, &ir);
      return rc;
    }

    long long elapsed_ms = now_ms() - command_started_ms;
    if (command.json) print_build_json(&command, &input, &program, &ir, target, "exe", exe_file, file_size_or_negative(exe_file), 0, elapsed_ms);
    else print_artifact(exe_file, elapsed_ms);
    free_linked_executable_paths(exe_cache_path, http_object_file, runtime_object_file, object_file, exe_file);
    zbuf_free(&object);
    free_loaded_command_state(&input, &program, &ir);
    return 0;
  }
  const char *direct_request = command.emit == EMIT_EXE ? z_backend_direct_request_name(command.backend) : NULL;
  ZDirectExecutableTargetFacts direct_exe = z_direct_executable_target_facts(target, direct_request);
  if (artifact_command && command.emit == EMIT_EXE && direct_request && !direct_exe.request_supported) {
    init_direct_backend_request_mismatch_diag(&diag, &command, &input, target, "exe");
    int rc = return_buildability_error(&command, &input, &diag, &ir, &program);
    z_free_source(&input);
    return rc;
  }
  CapabilitySummary direct_exe_caps = program_or_ir_capabilities(&program, &ir);
  bool default_direct_exe = artifact_command && command.emit == EMIT_EXE && direct_exe.request_supported && !direct_request && self_host_subset_compatible(&program, &direct_exe_caps);
  bool requested_direct_exe = artifact_command && command.emit == EMIT_EXE && direct_exe.requested_name;
  if (default_direct_exe || requested_direct_exe) {
    ZDirectBackend exe_backend = direct_exe.backend;
    if (!direct_exe.request_supported) {
      int rc = return_direct_backend_error(&command, &input, target, "exe", "direct executable backend is not implemented for this target/backend pair; use --emit obj for direct target objects or choose a supported direct executable target", &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (!direct_request) command.backend = direct_exe.default_request_name;
    if (!direct_buildability_preflight(&command, &input, target, "exe", &ir, &diag)) { int rc = return_buildability_error(&command, &input, &diag, &ir, &program); z_free_source(&input); return rc; }
    ZBuf exe;
    phase_started = now_ms();
    bool emitted_exe = z_emit_direct_executable_from_ir(exe_backend, &ir, &exe, &diag);
    input.codegen_ms = now_ms() - phase_started;
    if (!emitted_exe) {
      z_map_source_diag(&input, &diag);
      if (!diag.path) diag.path = input.source_file;
      complete_backend_blocker_diag(&diag, target, &command, "exe", "emit");
      if (command.json) print_diag_json(input.source_file, &diag);
      else print_diag(input.source_file, &diag);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    char *base_exe_file = command.out ? z_strdup(command.out) : command_default_exe_base_path(&command, &input);
    char *exe_file = apply_target_suffix(base_exe_file, target);
    free(base_exe_file);
    phase_started = now_ms();
    input.emitted_object_cache_hit = compiler_cache_touch("emitted-object", command_compile_cache_key(&command, &input, target, command.profile, direct_exe.artifact_path));
    bool wrote_exe = z_write_binary_file(exe_file, (const unsigned char *)exe.data, exe.len, &diag);
    if (wrote_exe && !z_process_mark_executable(exe_file)) {
      init_executable_finalize_diag(&diag, exe_file);
      wrote_exe = false;
    }
    input.object_ms = now_ms() - phase_started;
    input.link_ms = 0;
    if (!wrote_exe) {
      print_diag(exe_file, &diag);
      free(exe_file);
      zbuf_free(&exe);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    if (run_command) {
      int rc = run_executable_artifact_as(exe_file, exe_file, &command);
      command_remove_ephemeral_run_artifact(&command, exe_file);
      free(exe_file);
      zbuf_free(&exe);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return rc;
    }

    long long elapsed_ms = now_ms() - command_started_ms;
    if (command.json) {
      print_build_json(&command, &input, &program, &ir, target, "exe", exe_file, file_size_or_negative(exe_file), 0, elapsed_ms);
    } else {
      print_artifact(exe_file, elapsed_ms);
    }
    free(exe_file);
    zbuf_free(&exe);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  if (size_command) {
    GraphSizeSource graph_size_source = {.artifact_source = &command.graph_source};
    int size_rc = run_size_report_command(&command, &input, &program, target, &ir, z_program_graph_artifact_source_present(&command.graph_source) ? &graph_size_source : NULL, &diag);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return size_rc;
  }
  int rc = return_direct_backend_error(&command, &input, target, "exe", "direct executable backend is not implemented for this target/backend pair; use --emit obj for direct target objects or choose a supported direct executable target", &ir, &program);
  z_free_source(&input);
  return rc;
}
