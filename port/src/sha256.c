/**
 * sha256.c -- Standalone SHA-256 implementation.
 *
 * Based on FIPS 180-4 (Secure Hash Standard). No external dependencies.
 * Used by the update system (D13) for verifying downloaded binaries.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <stdio.h>
#include <string.h>
#include "sha256.h"

/* ========================================================================
 * Constants
 * ======================================================================== */

static const u32 K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

/* ========================================================================
 * Helper macros
 * ======================================================================== */

#define ROTR(x, n)    (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)         (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define EP1(x)         (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SIG0(x)        (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SIG1(x)        (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

/* ========================================================================
 * Core transform
 * ======================================================================== */

static void sha256Transform(sha256_ctx *ctx, const u8 block[64])
{
	u32 W[64];
	u32 a, b, c, d, e, f, g, h;
	u32 t1, t2;
	s32 i;

	/* Prepare message schedule */
	for (i = 0; i < 16; i++) {
		W[i] = ((u32)block[i * 4 + 0] << 24)
		     | ((u32)block[i * 4 + 1] << 16)
		     | ((u32)block[i * 4 + 2] <<  8)
		     | ((u32)block[i * 4 + 3]);
	}
	for (i = 16; i < 64; i++) {
		W[i] = SIG1(W[i - 2]) + W[i - 7] + SIG0(W[i - 15]) + W[i - 16];
	}

	/* Initialize working variables */
	a = ctx->state[0]; b = ctx->state[1];
	c = ctx->state[2]; d = ctx->state[3];
	e = ctx->state[4]; f = ctx->state[5];
	g = ctx->state[6]; h = ctx->state[7];

	/* 64 rounds */
	for (i = 0; i < 64; i++) {
		t1 = h + EP1(e) + CH(e, f, g) + K[i] + W[i];
		t2 = EP0(a) + MAJ(a, b, c);
		h = g; g = f; f = e;
		e = d + t1;
		d = c; c = b; b = a;
		a = t1 + t2;
	}

	/* Add to state */
	ctx->state[0] += a; ctx->state[1] += b;
	ctx->state[2] += c; ctx->state[3] += d;
	ctx->state[4] += e; ctx->state[5] += f;
	ctx->state[6] += g; ctx->state[7] += h;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void sha256Init(sha256_ctx *ctx)
{
	ctx->state[0] = 0x6a09e667;
	ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372;
	ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f;
	ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab;
	ctx->state[7] = 0x5be0cd19;
	ctx->bitcount = 0;
	memset(ctx->buffer, 0, SHA256_BLOCK_SIZE);
}

void sha256Update(sha256_ctx *ctx, const void *data, size_t len)
{
	const u8 *p = (const u8 *)data;
	size_t bufpos = (size_t)((ctx->bitcount >> 3) & 0x3F);

	ctx->bitcount += (u64)len << 3;

	/* Fill partial block */
	if (bufpos > 0) {
		size_t space = SHA256_BLOCK_SIZE - bufpos;
		if (len < space) {
			memcpy(ctx->buffer + bufpos, p, len);
			return;
		}
		memcpy(ctx->buffer + bufpos, p, space);
		sha256Transform(ctx, ctx->buffer);
		p += space;
		len -= space;
	}

	/* Process full blocks */
	while (len >= SHA256_BLOCK_SIZE) {
		sha256Transform(ctx, p);
		p += SHA256_BLOCK_SIZE;
		len -= SHA256_BLOCK_SIZE;
	}

	/* Store remainder */
	if (len > 0) {
		memcpy(ctx->buffer, p, len);
	}
}

void sha256Final(sha256_ctx *ctx, u8 digest[SHA256_DIGEST_SIZE])
{
	size_t bufpos = (size_t)((ctx->bitcount >> 3) & 0x3F);
	s32 i;

	/* Pad with 0x80 */
	ctx->buffer[bufpos++] = 0x80;

	/* If not enough room for the 8-byte length, pad this block and process */
	if (bufpos > 56) {
		memset(ctx->buffer + bufpos, 0, SHA256_BLOCK_SIZE - bufpos);
		sha256Transform(ctx, ctx->buffer);
		bufpos = 0;
	}

	/* Pad up to 56 bytes */
	memset(ctx->buffer + bufpos, 0, 56 - bufpos);

	/* Append bit length (big-endian 64-bit) */
	u64 bits = ctx->bitcount;
	ctx->buffer[56] = (u8)(bits >> 56);
	ctx->buffer[57] = (u8)(bits >> 48);
	ctx->buffer[58] = (u8)(bits >> 40);
	ctx->buffer[59] = (u8)(bits >> 32);
	ctx->buffer[60] = (u8)(bits >> 24);
	ctx->buffer[61] = (u8)(bits >> 16);
	ctx->buffer[62] = (u8)(bits >>  8);
	ctx->buffer[63] = (u8)(bits);

	sha256Transform(ctx, ctx->buffer);

	/* Produce digest (big-endian) */
	for (i = 0; i < 8; i++) {
		digest[i * 4 + 0] = (u8)(ctx->state[i] >> 24);
		digest[i * 4 + 1] = (u8)(ctx->state[i] >> 16);
		digest[i * 4 + 2] = (u8)(ctx->state[i] >>  8);
		digest[i * 4 + 3] = (u8)(ctx->state[i]);
	}
}

void sha256Hash(const void *data, size_t len, u8 digest[SHA256_DIGEST_SIZE])
{
	sha256_ctx ctx;
	sha256Init(&ctx);
	sha256Update(&ctx, data, len);
	sha256Final(&ctx, digest);
}

s32 sha256HashFile(const char *path, u8 digest[SHA256_DIGEST_SIZE])
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		return -1;
	}

	sha256_ctx ctx;
	sha256Init(&ctx);

	u8 buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		sha256Update(&ctx, buf, n);
	}

	s32 err = ferror(f);
	fclose(f);

	if (err) {
		return -1;
	}

	sha256Final(&ctx, digest);
	return 0;
}

void sha256ToHex(const u8 digest[SHA256_DIGEST_SIZE], char hex[SHA256_HEX_SIZE])
{
	static const char hexchars[] = "0123456789abcdef";
	s32 i;

	for (i = 0; i < SHA256_DIGEST_SIZE; i++) {
		hex[i * 2 + 0] = hexchars[(digest[i] >> 4) & 0x0F];
		hex[i * 2 + 1] = hexchars[digest[i] & 0x0F];
	}
	hex[64] = '\0';
}

s32 sha256VerifyFile(const char *path, const char *expectedhex)
{
	u8 digest[SHA256_DIGEST_SIZE];
	s32 result = sha256HashFile(path, digest);
	if (result != 0) {
		return -1;
	}

	char hex[SHA256_HEX_SIZE];
	sha256ToHex(digest, hex);

	/* Case-insensitive compare */
	s32 i;
	for (i = 0; i < 64; i++) {
		char a = hex[i];
		char b = expectedhex[i];
		if (b >= 'A' && b <= 'F') b += 32;
		if (a != b) return 0;
	}

	return 1;
}
