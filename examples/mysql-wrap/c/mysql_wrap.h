/*
 * mysql_wrap.h — thin C wrapper around libmysqlclient
 *
 * Modified for Zero 0.2.1:
 * - Exposes only scalar types (int32_t, uint64_t, int64_t, uint8_t) for C ABI safety.
 * - String transmission is mediated by a shared static buffer read/write API.
 */

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Connection handles ────────────────────────────────────────────────── */

typedef struct { int handle; } DbConn;
typedef struct { int handle; int64_t row_count; } DbResultHandle;
typedef struct { int conn_handle; int row_index; } DbRowHandle;

/* ── Buffer I/O API ────────────────────────────────────────────────────── */

void    db_write_buffer(int32_t offset, uint8_t val);
uint8_t db_read_buffer(int32_t offset);

/* ── Connection API ────────────────────────────────────────────────────── */

int32_t db_connect(int32_t host_off, uint16_t port,
                   int32_t user_off, int32_t pass_off,
                   int32_t db_off);

void db_close(int handle);

/* ── User queries ──────────────────────────────────────────────────────── */

int32_t db_query_user_by_username(int32_t conn_handle, int32_t username_off);
int32_t db_query_user_by_id(int32_t conn_handle, uint64_t user_id);
int64_t db_result_row_count(int result_handle);

/* Field accessors for a fetched row */
uint64_t db_row_u64   (int32_t conn_handle, int32_t row_index, int32_t field);
int64_t  db_row_i64   (int32_t conn_handle, int32_t row_index, int32_t field);
int32_t  db_row_i32   (int32_t conn_handle, int32_t row_index, int32_t field);

/* Write field string into dest_offset and return length */
int32_t  db_row_string(int32_t conn_handle, int32_t row_index, int32_t field, int32_t dest_offset);

void db_free_result(int result_handle);

int32_t db_count_users_by_username_or_email(int32_t conn_handle,
                                             int32_t username_off,
                                             int32_t email_off);

uint64_t db_insert_user(int32_t conn_handle, int32_t username_off,
                        int32_t email_off, int32_t password_hash_off,
                        int32_t is_admin);

/* Write serialized users JSON into dest_offset and return length */
int32_t db_list_users_json(int32_t conn_handle, int32_t dest_offset);

int32_t db_delete_user(int32_t conn_handle, uint64_t user_id);

#ifdef __cplusplus
}
#endif
