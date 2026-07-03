/*
 * argon2_wrap.h — Argon2id password hashing via libargon2
 *
 * Modified for Zero 0.2.1:
 * - Exposes only scalar types for C ABI safety.
 * - String transmission is mediated by a shared static buffer read/write API.
 */

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARGON2_ENCODED_LEN 128
#define DEFAULT_TOKEN_KEY "change-me-in-production"
#define TOKEN_EXPIRES_SECS 86400

/* ── Buffer I/O API ────────────────────────────────────────────────────── */

void    ar_write_buffer(int32_t offset, uint8_t val);
uint8_t ar_read_buffer(int32_t offset);

/* ── Password hashing ───────────────────────────────────────────────────── */

int32_t ar_hash_str(int32_t password_off, int32_t dest_off);
int32_t ar_verify(int32_t encoded_hash_off, int32_t password_off);

/* ── Token helpers ─────────────────────────────────────────────────────── */

int32_t  ar_token_create(uint64_t user_id, int32_t is_admin, int32_t dest_off);
int32_t  ar_token_verify(int32_t token_off);
uint64_t ar_token_user_id(void);
int32_t  ar_token_is_admin(void);

/* ── JSON builder helpers ─────────────────────────────────────────────── */

int32_t ar_json_str2(int32_t k1_off, int32_t v1_off, int32_t k2_off, int32_t v2_off, int32_t dest_off);
int32_t ar_json_err(int32_t msg_off, int32_t dest_off);
int32_t ar_json_id(int32_t status_off, uint64_t id, int32_t dest_off);

#ifdef __cplusplus
}
#endif
