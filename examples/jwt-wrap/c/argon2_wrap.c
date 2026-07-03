/*
 * argon2_wrap.c — implementation
 */

#include "argon2_wrap.h"
#include <argon2.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Argon2id parameters (OWASP 2023 minimum recommendation) */
#define T_COST      3
#define M_COST      (1 << 16)   /* 65536 KiB = 64 MiB */
#define PARALLELISM 1
#define HASH_LEN    32          /* 256-bit output */
#define SALT_LEN    16          /* 128-bit random salt */

static char g_buffer[65536];

void ar_write_buffer(int32_t offset, uint8_t val) {
    if (offset >= 0 && offset < sizeof(g_buffer)) {
        g_buffer[offset] = (char)val;
    }
}

uint8_t ar_read_buffer(int32_t offset) {
    if (offset >= 0 && offset < sizeof(g_buffer)) {
        return (uint8_t)g_buffer[offset];
    }
    return 0;
}

static int32_t argon2_hash_password(const char* password, char* out_buf, uint32_t out_len) {
    if (!password || !out_buf || out_len < ARGON2_ENCODED_LEN) return 0;

    /* Generate a random salt using /dev/urandom */
    uint8_t salt[SALT_LEN];
    FILE* urandom = fopen("/dev/urandom", "rb");
    if (!urandom) return 0;
    size_t n = fread(salt, 1, SALT_LEN, urandom);
    fclose(urandom);
    if (n != SALT_LEN) return 0;

    int rc = argon2id_hash_encoded(
        T_COST, M_COST, PARALLELISM,
        password, strlen(password),
        salt, SALT_LEN,
        HASH_LEN,
        out_buf, (size_t)out_len
    );

    return (rc == ARGON2_OK) ? 1 : 0;
}

static int32_t argon2_verify_password(const char* encoded_hash, const char* password) {
    if (!encoded_hash || !password) return -1;
    int rc = argon2id_verify(encoded_hash, password, strlen(password));
    if (rc == ARGON2_OK)              return 1;
    if (rc == ARGON2_VERIFY_MISMATCH) return 0;
    return -1;
}

/* ── Password hashing ───────────────────────────────────────────────────── */

int32_t ar_hash_str(int32_t password_off, int32_t dest_off) {
    const char* password = (password_off >= 0 && password_off < sizeof(g_buffer)) ? &g_buffer[password_off] : "";
    if (strlen(password) < 8) return 0;

    char hash_buf[ARGON2_ENCODED_LEN];
    if (!argon2_hash_password(password, hash_buf, ARGON2_ENCODED_LEN)) return 0;

    size_t len = strlen(hash_buf);
    if (dest_off >= 0 && dest_off + len < sizeof(g_buffer)) {
        memcpy(g_buffer + dest_off, hash_buf, len);
        g_buffer[dest_off + len] = '\0';
    }
    return (int32_t)len;
}

int32_t ar_verify(int32_t encoded_hash_off, int32_t password_off) {
    const char* encoded_hash = (encoded_hash_off >= 0 && encoded_hash_off < sizeof(g_buffer)) ? &g_buffer[encoded_hash_off] : "";
    const char* password = (password_off >= 0 && password_off < sizeof(g_buffer)) ? &g_buffer[password_off] : "";
    return argon2_verify_password(encoded_hash, password);
}

/* ── Token helpers ─────────────────────────────────────────────────────── */

/* Simple djb2-style HMAC using the secret key */
static uint32_t simple_hmac(const char* key, const char* payload) {
    uint32_t h = 5381;
    for (const char* k = key; *k; k++) h = h * 33 ^ (unsigned char)*k;
    h ^= 0xdeadbeef;
    for (const char* p = payload; *p; p++) h = h * 33 ^ (unsigned char)*p;
    return h;
}

static uint64_t g_verified_user_id = 0;
static int32_t  g_verified_is_admin = 0;

static int32_t token_create(uint64_t user_id, int32_t is_admin, char* out_buf, uint32_t out_len) {
    if (!out_buf || out_len < 64) return 0;
    long expires = (long)time(NULL) + TOKEN_EXPIRES_SECS;
    char tok_payload[512];
    snprintf(tok_payload, sizeof(tok_payload), "%llu:%d:%ld",
             (unsigned long long)user_id, is_admin ? 1 : 0, expires);
    uint32_t hmac = simple_hmac(DEFAULT_TOKEN_KEY, tok_payload);
    int n = snprintf(out_buf, out_len, "%s:%08x", tok_payload, hmac);
    return (n > 0 && (uint32_t)n < out_len) ? 1 : 0;
}

int32_t ar_token_create(uint64_t user_id, int32_t is_admin, int32_t dest_off) {
    char buf[256];
    if (!token_create(user_id, is_admin, buf, sizeof(buf))) return 0;
    size_t len = strlen(buf);
    if (dest_off >= 0 && dest_off + len < sizeof(g_buffer)) {
        memcpy(g_buffer + dest_off, buf, len);
        g_buffer[dest_off + len] = '\0';
    }
    return (int32_t)len;
}

int32_t ar_token_verify(int32_t token_off) {
    const char* token = (token_off >= 0 && token_off < sizeof(g_buffer)) ? &g_buffer[token_off] : "";
    if (!token) return 0;

    /* Find last ':' to split payload/hmac */
    const char* last_colon = strrchr(token, ':');
    if (!last_colon || strlen(last_colon + 1) != 8) return 0;

    /* Copy payload */
    size_t payload_len = (size_t)(last_colon - token);
    char payload[256];
    if (payload_len >= sizeof(payload)) return 0;
    memcpy(payload, token, payload_len);
    payload[payload_len] = '\0';

    /* Verify HMAC */
    uint32_t expected = simple_hmac(DEFAULT_TOKEN_KEY, payload);
    uint32_t actual   = (uint32_t)strtoul(last_colon + 1, NULL, 16);
    if (expected != actual) return 0;

    /* Parse payload: user_id:is_admin:expires */
    unsigned long long uid; int adm; long exp;
    if (sscanf(payload, "%llu:%d:%ld", &uid, &adm, &exp) != 3) return 0;
    if ((long)time(NULL) > exp) return 0;   /* expired */

    g_verified_user_id  = (uint64_t)uid;
    g_verified_is_admin = adm;
    return 1;
}

uint64_t ar_token_user_id(void) {
    return g_verified_user_id;
}

int32_t ar_token_is_admin(void) {
    return g_verified_is_admin;
}

/* ── JSON builder helpers ─────────────────────────────────────────────── */

int32_t ar_json_str2(int32_t k1_off, int32_t v1_off, int32_t k2_off, int32_t v2_off, int32_t dest_off) {
    const char* k1 = (k1_off >= 0 && k1_off < sizeof(g_buffer)) ? &g_buffer[k1_off] : "";
    const char* v1 = (v1_off >= 0 && v1_off < sizeof(g_buffer)) ? &g_buffer[v1_off] : "";
    const char* k2 = (k2_off >= 0 && k2_off < sizeof(g_buffer)) ? &g_buffer[k2_off] : "";
    const char* v2 = (v2_off >= 0 && v2_off < sizeof(g_buffer)) ? &g_buffer[v2_off] : "";

    char temp[512];
    int n = snprintf(temp, sizeof(temp), "{\"%s\":\"%s\",\"%s\":\"%s\"}", k1, v1, k2, v2);
    if (dest_off >= 0 && dest_off + n < sizeof(g_buffer)) {
        memcpy(g_buffer + dest_off, temp, n);
        g_buffer[dest_off + n] = '\0';
    }
    return n;
}

int32_t ar_json_err(int32_t msg_off, int32_t dest_off) {
    const char* msg = (msg_off >= 0 && msg_off < sizeof(g_buffer)) ? &g_buffer[msg_off] : "";
    char temp[256];
    int n = snprintf(temp, sizeof(temp), "{\"error\":\"%s\"}", msg);
    if (dest_off >= 0 && dest_off + n < sizeof(g_buffer)) {
        memcpy(g_buffer + dest_off, temp, n);
        g_buffer[dest_off + n] = '\0';
    }
    return n;
}

int32_t ar_json_id(int32_t status_off, uint64_t id, int32_t dest_off) {
    const char* status = (status_off >= 0 && status_off < sizeof(g_buffer)) ? &g_buffer[status_off] : "";
    char temp[128];
    int n = snprintf(temp, sizeof(temp), "{\"status\":\"%s\",\"id\":%llu}", status, (unsigned long long)id);
    if (dest_off >= 0 && dest_off + n < sizeof(g_buffer)) {
        memcpy(g_buffer + dest_off, temp, n);
        g_buffer[dest_off + n] = '\0';
    }
    return n;
}
