/**
 * ed25519.c -- Ed25519 signature verify for the signed updater (SEC-6).
 *
 * Backed by OpenSSL's EVP_DigestVerify with EVP_PKEY_ED25519. OpenSSL is
 * already statically linked via the libcurl TLS chain (libssl.a, libcrypto.a
 * in CMakeLists.txt), so this adds no new link-time dependency and preserves
 * the zero-DLL guarantee.
 *
 * Auto-discovered by GLOB_RECURSE for port source in the client build, and
 * explicitly listed in the server + pd-updater builds.
 */

#include <string.h>
#include <stdio.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#include "ed25519.h"

/* ========================================================================
 * Verify
 * ======================================================================== */

s32 ed25519Verify(const u8 signature[ED25519_SIGNATURE_SIZE],
                  const void *msg, size_t msgLen,
                  const u8 pubkey[ED25519_PUBKEY_SIZE])
{
	if (!signature || !pubkey || (!msg && msgLen > 0)) {
		return -1;
	}

	EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(
		EVP_PKEY_ED25519, NULL, pubkey, ED25519_PUBKEY_SIZE);
	if (!pkey) {
		return -1;
	}

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx) {
		EVP_PKEY_free(pkey);
		return -1;
	}

	s32 result = -1;
	if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) == 1) {
		int rc = EVP_DigestVerify(ctx, signature, ED25519_SIGNATURE_SIZE,
			(const unsigned char *)msg, msgLen);
		if (rc == 1) {
			result = 1;  /* valid */
		} else if (rc == 0) {
			result = 0;  /* invalid signature */
		} else {
			result = -1; /* internal error */
		}
	}

	EVP_MD_CTX_free(ctx);
	EVP_PKEY_free(pkey);
	/* Clear any OpenSSL errors left on the thread-local error queue so they
	 * don't bleed into unrelated TLS calls by libcurl. */
	ERR_clear_error();
	return result;
}

/* ========================================================================
 * Self-test (RFC 8032 test vector 1)
 *
 * This catches misconfigured OpenSSL builds where EVP_PKEY_ED25519 isn't
 * available, and also guards against anyone swapping this file for a stub.
 * Called by the updater once at init.
 *
 * Vector:
 *   secret key: 9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60
 *   public key: d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a
 *   message:    (empty)
 *   signature:  e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155
 *               5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b
 * ======================================================================== */

static const u8 ED25519_TV_PUBKEY[32] = {
	0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
	0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
	0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
	0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
};

static const u8 ED25519_TV_SIG[64] = {
	0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72,
	0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
	0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74,
	0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55,
	0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac,
	0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
	0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24,
	0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b,
};

s32 ed25519SelfTest(void)
{
	/* Valid signature over the empty message must return 1. */
	if (ed25519Verify(ED25519_TV_SIG, NULL, 0, ED25519_TV_PUBKEY) != 1) {
		return 0;
	}

	/* Tampered signature must return 0 (not 1, not -1). */
	u8 badsig[64];
	memcpy(badsig, ED25519_TV_SIG, 64);
	badsig[0] ^= 0x01;
	if (ed25519Verify(badsig, NULL, 0, ED25519_TV_PUBKEY) != 0) {
		return 0;
	}

	/* Signature valid for empty msg must fail for a non-empty msg. */
	static const u8 junk[1] = { 0x00 };
	if (ed25519Verify(ED25519_TV_SIG, junk, 1, ED25519_TV_PUBKEY) != 0) {
		return 0;
	}

	return 1;
}
