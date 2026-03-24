/**
 * sha256.h -- Standalone SHA-256 implementation for download verification.
 *
 * No external dependencies. Used by the update system (D13) to verify
 * downloaded binaries against published checksums.
 *
 * Usage:
 *   sha256_ctx ctx;
 *   sha256Init(&ctx);
 *   sha256Update(&ctx, data, len);
 *   sha256Final(&ctx, hash);  // hash is 32 bytes
 *
 *   // Or all-in-one:
 *   sha256Hash(data, len, hash);
 *
 *   // Or from a file:
 *   sha256HashFile("path/to/file", hash);
 *
 *   // Convert to hex string:
 *   char hex[65];
 *   sha256ToHex(hash, hex);
 */

#ifndef _IN_SHA256_H
#define _IN_SHA256_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE    65  /* 64 hex chars + null */

typedef struct {
	u32  state[8];
	u64  bitcount;
	u8   buffer[SHA256_BLOCK_SIZE];
} sha256_ctx;

/** Initialize a SHA-256 context. */
void sha256Init(sha256_ctx *ctx);

/** Feed data into the hash. Can be called multiple times. */
void sha256Update(sha256_ctx *ctx, const void *data, size_t len);

/** Finalize and produce the 32-byte digest. Context is invalidated after. */
void sha256Final(sha256_ctx *ctx, u8 digest[SHA256_DIGEST_SIZE]);

/** All-in-one: hash a buffer and produce the 32-byte digest. */
void sha256Hash(const void *data, size_t len, u8 digest[SHA256_DIGEST_SIZE]);

/**
 * Hash a file on disk. Returns 0 on success, -1 on file error.
 * Reads in 8KB chunks — safe for large files.
 */
s32 sha256HashFile(const char *path, u8 digest[SHA256_DIGEST_SIZE]);

/** Convert a 32-byte digest to a 64-character lowercase hex string (null-terminated). */
void sha256ToHex(const u8 digest[SHA256_DIGEST_SIZE], char hex[SHA256_HEX_SIZE]);

/**
 * Verify a file against an expected hex hash string.
 * Returns 1 if match, 0 if mismatch, -1 on file error.
 */
s32 sha256VerifyFile(const char *path, const char *expectedhex);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SHA256_H */
