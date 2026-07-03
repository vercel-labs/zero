/*
 * zpm_native.h — C interface for zero-pm native extensions
 *
 * Zerolang extern-c interop: declare as `extern c "native/zpm_native.h" as zpm`
 * and call functions as `zpm.write_input(...)`, `zpm.sha256_hex(...)`, etc.
 *
 * Uses a shared global buffer pair (input/output) following the same pattern
 * as mysql-wrap and jwt-wrap in the zerolang repository.
 */

#ifndef ZPM_NATIVE_H
#define ZPM_NATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#define ZPM_BUF_SIZE 65536

/* ── Buffer I/O ─────────────────────────────────────────────────────── */

void  write_input (int offset, unsigned char byte_val);
unsigned char read_output(int offset);

/* ── SHA-256 ────────────────────────────────────────────────────────── */

/* Hash data_len input bytes → 64 hex chars in output buf. Returns 64 or -1. */
int sha256_hex(int data_len);

/* Verify: hash input[0..data_len], compare with output[0..64]. 1=match, 0=no. */
int sha256_verify(int data_len);

/* ── Directory creation ─────────────────────────────────────────────── */

/* mkdir -p for path in input[0..path_len]. Returns 0 on success, -1 on error. */
int mkdir_p(int path_len);

/* ── Minimal ustar tar extraction ───────────────────────────────────── */

/* Extract tar from input[0..archive_len] to dir in output[0..dest_path_len].
   Returns files extracted, or -1 on error. */
int tar_extract(int archive_len, int dest_path_len);

/* ── CRC-32 ─────────────────────────────────────────────────────────── */

/* CRC-32 of input[0..data_len]. Writes 8 hex chars to output[0..8].
   Returns CRC value (non-negative) or -1 on error. */
int crc32_compute(int data_len);

#ifdef __cplusplus
}
#endif

#endif /* ZPM_NATIVE_H */
