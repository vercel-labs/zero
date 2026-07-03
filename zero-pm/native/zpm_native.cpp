/*
 * zpm_native.cpp — C++ implementation of zero-pm native extensions
 *
 * Compiled as C++ but all symbols have C linkage (extern "C" in the header).
 * Build: see native/Makefile
 */

#include "zpm_native.h"

#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* ── Shared buffers ─────────────────────────────────────────────────── */

static unsigned char g_input[ZPM_BUF_SIZE];
static unsigned char g_output[ZPM_BUF_SIZE];

extern "C" void write_input(int offset, unsigned char byte_val) {
    if (offset >= 0 && offset < ZPM_BUF_SIZE) {
        g_input[offset] = byte_val;
    }
}

extern "C" unsigned char read_output(int offset) {
    if (offset >= 0 && offset < ZPM_BUF_SIZE) {
        return g_output[offset];
    }
    return 0;
}

/* ── SHA-256 implementation ─────────────────────────────────────────── */
// RFC 6234 / FIPS 180-4 compliant, no external dependencies.

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static inline uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_compress(uint32_t state[8], const unsigned char block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i*4]   << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] <<  8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i-15],  7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >>  3);
        uint32_t s1 = rotr32(w[i- 2], 17) ^ rotr32(w[i- 2], 19) ^ (w[i- 2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1  = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch  = (e & f) ^ (~e & g);
        uint32_t tmp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0  = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t tmp2 = S0 + maj;
        h = g; g = f; f = e; e = d + tmp1;
        d = c; c = b; b = a; a = tmp1 + tmp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_hash(const unsigned char *data, int len,
                        unsigned char out[32]) {
    uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    uint64_t bit_len = (uint64_t)len * 8;
    int remaining = len;
    const unsigned char *ptr = data;

    // Process full 64-byte blocks
    while (remaining >= 64) {
        sha256_compress(state, ptr);
        ptr += 64;
        remaining -= 64;
    }

    // Padding
    unsigned char pad[128] = {};
    memcpy(pad, ptr, (size_t)remaining);
    pad[remaining] = 0x80;
    int pad_len = (remaining < 56) ? 64 : 128;
    // Write big-endian bit length at the end
    for (int i = 0; i < 8; ++i) {
        pad[pad_len - 8 + i] = (unsigned char)(bit_len >> (56 - i * 8));
    }
    sha256_compress(state, pad);
    if (pad_len == 128) sha256_compress(state, pad + 64);

    for (int i = 0; i < 8; ++i) {
        out[i*4 + 0] = (unsigned char)(state[i] >> 24);
        out[i*4 + 1] = (unsigned char)(state[i] >> 16);
        out[i*4 + 2] = (unsigned char)(state[i] >>  8);
        out[i*4 + 3] = (unsigned char)(state[i]);
    }
}

static const char HEX[] = "0123456789abcdef";

extern "C" int sha256_hex(int data_len) {
    if (data_len < 0 || data_len > ZPM_BUF_SIZE) return -1;
    unsigned char digest[32];
    sha256_hash(g_input, data_len, digest);
    for (int i = 0; i < 32; ++i) {
        g_output[i*2    ] = (unsigned char)HEX[digest[i] >> 4];
        g_output[i*2 + 1] = (unsigned char)HEX[digest[i] & 0xf];
    }
    return 64;
}

extern "C" int sha256_verify(int data_len) {
    if (data_len < 0 || data_len > ZPM_BUF_SIZE) return 0;
    unsigned char digest[32];
    sha256_hash(g_input, data_len, digest);
    char computed[64];
    for (int i = 0; i < 32; ++i) {
        computed[i*2    ] = HEX[digest[i] >> 4];
        computed[i*2 + 1] = HEX[digest[i] & 0xf];
    }
    // Compare with output buffer (expected 64 hex chars)
    for (int i = 0; i < 64; ++i) {
        if (computed[i] != (char)g_output[i]) return 0;
    }
    return 1;
}

/* ── mkdir -p ───────────────────────────────────────────────────────── */

extern "C" int mkdir_p(int path_len) {
    if (path_len <= 0 || path_len >= ZPM_BUF_SIZE) return -1;
    char path[ZPM_BUF_SIZE + 1];
    memcpy(path, g_input, (size_t)path_len);
    path[path_len] = '\0';

    for (char *p = path + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* ── CRC-32 ─────────────────────────────────────────────────────────── */
// IEEE 802.3 polynomial (same as zlib/gzip)

static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void crc32_init() {
    if (crc32_table_ready) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = true;
}

extern "C" int crc32_compute(int data_len) {
    if (data_len < 0 || data_len > ZPM_BUF_SIZE) return -1;
    crc32_init();
    uint32_t crc = 0xffffffffu;
    for (int i = 0; i < data_len; ++i) {
        crc = crc32_table[(crc ^ g_input[i]) & 0xff] ^ (crc >> 8);
    }
    uint32_t result = crc ^ 0xffffffffu;
    // Store result as 8-char hex in output buffer
    for (int i = 0; i < 4; ++i) {
        uint8_t byte = (uint8_t)(result >> (24 - i * 8));
        g_output[i*2    ] = (unsigned char)HEX[byte >> 4];
        g_output[i*2 + 1] = (unsigned char)HEX[byte & 0xf];
    }
    g_output[8] = '\0';
    return (int)result;
}

/* ── Minimal ustar tar extraction ───────────────────────────────────── */
// Supports POSIX ustar format (typeflag '0' = regular file, '5' = directory).

struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

static uint64_t oct_to_u64(const char *s, int len) {
    uint64_t v = 0;
    for (int i = 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '7') break;
        v = v * 8 + (uint64_t)(s[i] - '0');
    }
    return v;
}

static int make_parent_dirs(const char *path) {
    char tmp[4096];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    return 0;
}

extern "C" int tar_extract(int archive_len, int dest_path_len) {
    if (archive_len <= 0 || archive_len > ZPM_BUF_SIZE) return -1;
    if (dest_path_len <= 0 || dest_path_len >= 4096) return -1;

    char dest[4097];
    memcpy(dest, g_output, (size_t)dest_path_len);
    dest[dest_path_len] = '\0';
    // Ensure dest ends with /
    int dlen = dest_path_len;
    if (dlen > 0 && dest[dlen - 1] != '/') {
        dest[dlen] = '/';
        dest[dlen + 1] = '\0';
        dlen++;
    }

    int extracted = 0;
    int pos = 0;
    const unsigned char *arc = g_input;

    while (pos + 512 <= archive_len) {
        const TarHeader *hdr = reinterpret_cast<const TarHeader *>(arc + pos);
        // End-of-archive: two consecutive zero blocks
        if (hdr->name[0] == '\0') {
            pos += 512;
            if (pos + 512 <= archive_len) {
                const TarHeader *hdr2 = reinterpret_cast<const TarHeader *>(arc + pos);
                if (hdr2->name[0] == '\0') break;
            }
            continue;
        }

        // Build full path: dest + prefix/name
        char full_path[4096];
        if (hdr->prefix[0] != '\0') {
            snprintf(full_path, sizeof(full_path), "%s%.*s/%.*s",
                     dest, 155, hdr->prefix, 100, hdr->name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s%.*s",
                     dest, 100, hdr->name);
        }

        uint64_t file_size = oct_to_u64(hdr->size, 12);
        int blocks = (int)((file_size + 511) / 512);
        pos += 512;

        char tf = hdr->typeflag;
        if (tf == '5' || (tf == '\0' && full_path[strlen(full_path) - 1] == '/')) {
            // Directory
            if (mkdir(full_path, 0755) != 0 && errno != EEXIST) { /* ignore */ }
        } else if (tf == '0' || tf == '\0') {
            // Regular file — create parent dirs then write
            make_parent_dirs(full_path);
            int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) {
                uint64_t remaining = file_size;
                int block_pos = pos;
                while (remaining > 0 && block_pos + 512 <= archive_len) {
                    uint64_t chunk = (remaining > 512) ? 512 : remaining;
                    ssize_t written = write(fd, arc + block_pos, (size_t)chunk);
                    if (written < 0) break;
                    remaining -= (uint64_t)written;
                    block_pos += 512;
                }
                close(fd);
                extracted++;
            }
        }
        pos += blocks * 512;
    }
    return extracted;
}
