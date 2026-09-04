/*
 * mysql_wrap.c — implementation of the MySQL C wrapper
 */

#include "mysql_wrap.h"
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Connection pool ───────────────────────────────────────────────────── */

#define MAX_CONNS 16
#define MAX_RESULTS 64

static MYSQL*      g_conns[MAX_CONNS];
static MYSQL_RES*  g_results[MAX_RESULTS];
static int         g_initialized = 0;

static char g_buffer[65536];

void db_write_buffer(int32_t offset, uint8_t val) {
    if (offset >= 0 && offset < sizeof(g_buffer)) {
        g_buffer[offset] = (char)val;
    }
}

uint8_t db_read_buffer(int32_t offset) {
    if (offset >= 0 && offset < sizeof(g_buffer)) {
        return (uint8_t)g_buffer[offset];
    }
    return 0;
}

static void init_pools(void) {
    if (g_initialized) return;
    memset(g_conns,   0, sizeof(g_conns));
    memset(g_results, 0, sizeof(g_results));
    g_initialized = 1;
}

static int alloc_conn_slot(void) {
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!g_conns[i]) return i;
    }
    return -1;
}

static int alloc_result_slot(void) {
    for (int i = 0; i < MAX_RESULTS; i++) {
        if (!g_results[i]) return i;
    }
    return -1;
}

/* ── Connection API ────────────────────────────────────────────────────── */

int32_t db_connect(int32_t host_off, uint16_t port,
                   int32_t user_off, int32_t pass_off,
                   int32_t db_off) {
    init_pools();
    int slot = alloc_conn_slot();
    if (slot < 0) return -1;

    const char* host = (host_off >= 0 && host_off < sizeof(g_buffer)) ? &g_buffer[host_off] : "";
    const char* user = (user_off >= 0 && user_off < sizeof(g_buffer)) ? &g_buffer[user_off] : "";
    const char* password = (pass_off >= 0 && pass_off < sizeof(g_buffer)) ? &g_buffer[pass_off] : "";
    const char* database = (db_off >= 0 && db_off < sizeof(g_buffer)) ? &g_buffer[db_off] : "";

    MYSQL* m = mysql_init(NULL);
    if (!m) return -1;

    if (!mysql_real_connect(m, host, user, password, database, port, NULL, 0)) {
        fprintf(stderr, "[mysql_wrap] connect error: %s\n", mysql_error(m));
        mysql_close(m);
        return -1;
    }

    /* Enable auto-reconnect */
    my_bool reconnect = 1;
    mysql_options(m, MYSQL_OPT_RECONNECT, &reconnect);

    g_conns[slot] = m;
    return slot;
}

void db_close(int handle) {
    if (handle < 0 || handle >= MAX_CONNS || !g_conns[handle]) return;
    mysql_close(g_conns[handle]);
    g_conns[handle] = NULL;
}

/* ── Query helpers ─────────────────────────────────────────────────────── */

static MYSQL* get_conn(int handle) {
    if (handle < 0 || handle >= MAX_CONNS) return NULL;
    return g_conns[handle];
}

static int store_result(MYSQL* m, int64_t* out_rows) {
    MYSQL_RES* res = mysql_store_result(m);
    if (!res) { *out_rows = 0; return -1; }
    int slot = alloc_result_slot();
    if (slot < 0) { mysql_free_result(res); *out_rows = 0; return -1; }
    g_results[slot] = res;
    *out_rows = (int64_t)mysql_num_rows(res);
    return slot;
}

int32_t db_query_user_by_username(int32_t conn_handle, int32_t username_off) {
    MYSQL* m = get_conn(conn_handle);
    if (!m) return -1;

    const char* username = (username_off >= 0 && username_off < sizeof(g_buffer)) ? &g_buffer[username_off] : "";

    char sql[512];
    /* Safe: username validated by handlers before reaching here */
    snprintf(sql, sizeof(sql),
        "SELECT id, username, email, password_hash, is_admin, created_at "
        "FROM users WHERE username = '%s' LIMIT 1",
        mysql_real_escape_string(m, sql + 256, username, strlen(username)) ?
            sql + 256 : username);

    /* Use parameterized query for safety */
    MYSQL_STMT* stmt = mysql_stmt_init(m);
    if (!stmt) return -1;

    const char* query =
        "SELECT id, username, email, password_hash, is_admin, created_at "
        "FROM users WHERE username = ? LIMIT 1";
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind_in[1];
    memset(bind_in, 0, sizeof(bind_in));
    unsigned long ulen = (unsigned long)strlen(username);
    bind_in[0].buffer_type   = MYSQL_TYPE_STRING;
    bind_in[0].buffer        = (void*)username;
    bind_in[0].buffer_length = ulen;
    bind_in[0].length        = &ulen;

    if (mysql_stmt_bind_param(stmt, bind_in) != 0 ||
        mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    if (!meta) { mysql_stmt_close(stmt); return -1; }
    mysql_free_result(meta);
    mysql_stmt_store_result(stmt);
    int64_t rows = (int64_t)mysql_stmt_num_rows(stmt);
    mysql_stmt_close(stmt);

    /* Re-run as plain query to use mysql_store_result for row access */
    char safe_user[256];
    mysql_real_escape_string(m, safe_user, username, strlen(username));
    snprintf(sql, sizeof(sql),
        "SELECT id, username, email, password_hash, is_admin, created_at "
        "FROM users WHERE username = '%s' LIMIT 1", safe_user);

    if (mysql_query(m, sql) != 0)
        return -1;

    int64_t row_count = 0;
    int slot = store_result(m, &row_count);
    return slot;
}

int32_t db_query_user_by_id(int32_t conn_handle, uint64_t user_id) {
    MYSQL* m = get_conn(conn_handle);
    if (!m) return -1;

    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT id, username, email, password_hash, is_admin, created_at "
        "FROM users WHERE id = %llu LIMIT 1", (unsigned long long)user_id);

    if (mysql_query(m, sql) != 0)
        return -1;

    int64_t row_count = 0;
    int slot = store_result(m, &row_count);
    return slot;
}

int64_t db_result_row_count(int result_handle) {
    if (result_handle < 0 || result_handle >= MAX_RESULTS || !g_results[result_handle])
        return 0;
    return (int64_t)mysql_num_rows(g_results[result_handle]);
}

static MYSQL_ROW current_row(int result_handle, int row_index) {
    if (result_handle < 0 || result_handle >= MAX_RESULTS || !g_results[result_handle])
        return NULL;
    mysql_data_seek(g_results[result_handle], (my_ulonglong)row_index);
    return mysql_fetch_row(g_results[result_handle]);
}

uint64_t db_row_u64(int32_t rh, int32_t ri, int32_t field) {
    MYSQL_ROW row = current_row(rh, ri);
    if (!row || !row[field]) return 0;
    return (uint64_t)strtoull(row[field], NULL, 10);
}

int64_t db_row_i64(int32_t rh, int32_t ri, int32_t field) {
    MYSQL_ROW row = current_row(rh, ri);
    if (!row || !row[field]) return 0;
    return (int64_t)strtoll(row[field], NULL, 10);
}

int32_t db_row_i32(int32_t rh, int32_t ri, int32_t field) {
    MYSQL_ROW row = current_row(rh, ri);
    if (!row || !row[field]) return 0;
    return (int32_t)atoi(row[field]);
}

int32_t db_row_string(int32_t rh, int32_t ri, int32_t field, int32_t dest_offset) {
    MYSQL_ROW row = current_row(rh, ri);
    const char* val = (row && row[field]) ? row[field] : "";
    size_t len = strlen(val);
    if (dest_offset >= 0 && dest_offset + len < sizeof(g_buffer)) {
        memcpy(g_buffer + dest_offset, val, len);
        g_buffer[dest_offset + len] = '\0';
    }
    return (int32_t)len;
}

void db_free_result(int result_handle) {
    if (result_handle < 0 || result_handle >= MAX_RESULTS) return;
    if (g_results[result_handle]) {
        mysql_free_result(g_results[result_handle]);
        g_results[result_handle] = NULL;
    }
}

int32_t db_count_users_by_username_or_email(int32_t conn_handle,
                                             int32_t username_off,
                                             int32_t email_off) {
    MYSQL* m = get_conn(conn_handle);
    if (!m) return -1;

    const char* username = (username_off >= 0 && username_off < sizeof(g_buffer)) ? &g_buffer[username_off] : "";
    const char* email = (email_off >= 0 && email_off < sizeof(g_buffer)) ? &g_buffer[email_off] : "";

    char su[256], se[512];
    mysql_real_escape_string(m, su, username, strlen(username));
    mysql_real_escape_string(m, se, email,    strlen(email));
    char sql[1024];
    snprintf(sql, sizeof(sql),
        "SELECT COUNT(*) FROM users WHERE username='%s' OR email='%s'", su, se);
    if (mysql_query(m, sql) != 0) return -1;
    MYSQL_RES* res = mysql_store_result(m);
    if (!res) return -1;
    MYSQL_ROW row = mysql_fetch_row(res);
    int32_t count = row && row[0] ? (int32_t)atoi(row[0]) : 0;
    mysql_free_result(res);
    return count;
}

uint64_t db_insert_user(int32_t conn_handle, int32_t username_off,
                        int32_t email_off, int32_t password_hash_off,
                        int32_t is_admin) {
    MYSQL* m = get_conn(conn_handle);
    if (!m) return 0;

    const char* username = (username_off >= 0 && username_off < sizeof(g_buffer)) ? &g_buffer[username_off] : "";
    const char* email = (email_off >= 0 && email_off < sizeof(g_buffer)) ? &g_buffer[email_off] : "";
    const char* password_hash = (password_hash_off >= 0 && password_hash_off < sizeof(g_buffer)) ? &g_buffer[password_hash_off] : "";

    char su[256], se[512], sh[1024];
    mysql_real_escape_string(m, su, username,      strlen(username));
    mysql_real_escape_string(m, se, email,         strlen(email));
    mysql_real_escape_string(m, sh, password_hash, strlen(password_hash));

    char sql[2048];
    snprintf(sql, sizeof(sql),
        "INSERT INTO users (username, email, password_hash, is_admin, created_at) "
        "VALUES ('%s','%s','%s',%d,%ld)",
        su, se, sh, is_admin ? 1 : 0, (long)time(NULL));

    if (mysql_query(m, sql) != 0) {
        /* Duplicate key = 1062 */
        if (mysql_errno(m) == 1062) return 0;
        return (uint64_t)-1;
    }
    return (uint64_t)mysql_insert_id(m);
}

int32_t db_list_users_json(int32_t conn_handle, int32_t dest_offset) {
    MYSQL* m = get_conn(conn_handle);
    if (!m) return 0;

    const char* sql =
        "SELECT id, username, email, is_admin, created_at FROM users ORDER BY id";
    if (mysql_query(m, sql) != 0) return 0;

    MYSQL_RES* res = mysql_store_result(m);
    if (!res) return 0;

    char temp_buf[65536];
    int pos = 0;
    pos += snprintf(temp_buf + pos, sizeof(temp_buf) - pos, "[");
    MYSQL_ROW row;
    int first = 1;
    while ((row = mysql_fetch_row(res)) != NULL) {
        if (!first) pos += snprintf(temp_buf + pos, sizeof(temp_buf) - pos, ",");
        pos += snprintf(temp_buf + pos, sizeof(temp_buf) - pos,
            "{\"id\":%s,\"username\":\"%s\",\"email\":\"%s\","
            "\"is_admin\":%s,\"created_at\":%s}",
            row[0] ? row[0] : "0",
            row[1] ? row[1] : "",
            row[2] ? row[2] : "",
            (row[3] && row[3][0] == '1') ? "true" : "false",
            row[4] ? row[4] : "0");
        first = 0;
    }
    pos += snprintf(temp_buf + pos, sizeof(temp_buf) - pos, "]");
    mysql_free_result(res);

    if (dest_offset >= 0 && dest_offset + pos < sizeof(g_buffer)) {
        memcpy(g_buffer + dest_offset, temp_buf, pos);
        g_buffer[dest_offset + pos] = '\0';
    }
    return (int32_t)pos;
}

int32_t db_delete_user(int32_t conn_handle, uint64_t user_id) {
    MYSQL* m = get_conn(conn_handle);
    if (!m) return -1;
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM users WHERE id=%llu",
             (unsigned long long)user_id);
    if (mysql_query(m, sql) != 0) return -1;
    return (int32_t)mysql_affected_rows(m);
}
