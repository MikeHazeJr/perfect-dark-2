/**
 * ed25519.h -- Ed25519 signature verification for the signed updater (SEC-6).
 *
 * Backed by OpenSSL's EVP_DigestVerify with EVP_PKEY_ED25519 (OpenSSL 1.1.1+).
 * OpenSSL is already statically linked as part of the libcurl dependency chain
 * (see CMakeLists.txt), so this adds no new runtime dependency.
 *
 * Only the VERIFY path is exposed. Signing happens offline via devtools scripts
 * using the developer-owned private key.
 *
 * Threading: stateless. Safe to call from any thread.
 */

#ifndef _IN_ED25519_H
#define _IN_ED25519_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ED25519_PUBKEY_SIZE     32
#define ED25519_PRIVKEY_SIZE    32
#define ED25519_SIGNATURE_SIZE  64

/**
 * Verify an Ed25519 signature over a message.
 *
 * @param signature  64-byte Ed25519 signature
 * @param msg        message bytes that were signed
 * @param msgLen     length of msg in bytes
 * @param pubkey     32-byte raw Ed25519 public key
 *
 * @return 1 on valid signature, 0 on invalid, -1 on internal error
 *         (e.g. OpenSSL init failure). Callers MUST treat anything other
 *         than 1 as failure.
 */
s32 ed25519Verify(const u8 signature[ED25519_SIGNATURE_SIZE],
                  const void *msg, size_t msgLen,
                  const u8 pubkey[ED25519_PUBKEY_SIZE]);

/**
 * Run a one-shot self-test against an RFC 8032 test vector. This catches
 * the case where OpenSSL is misconfigured or missing Ed25519 support before
 * the updater attempts to verify a real release.
 *
 * @return 1 on pass, 0 on fail. Callers should refuse to auto-update when
 *         this returns 0 — verification is broken and cannot be trusted.
 */
s32 ed25519SelfTest(void);

/**
 * Generate a fresh Ed25519 keypair (RFC 8032 Section 5.1.5).
 *
 * @param outPriv  receives the 32-byte raw private (seed) bytes. NEVER
 *                 leave the device.
 * @param outPub   receives the 32-byte raw public key.
 *
 * @return 1 on success, 0 on failure. Callers must discard both outputs
 *         on failure.
 */
s32 ed25519GenerateKeypair(u8 outPriv[ED25519_PRIVKEY_SIZE],
                            u8 outPub[ED25519_PUBKEY_SIZE]);

/**
 * Sign a message with the supplied private (seed) key.
 *
 * @param privkey  32-byte raw private seed.
 * @param msg      bytes to sign.
 * @param msgLen   length of msg.
 * @param outSig   receives the 64-byte Ed25519 signature.
 *
 * @return 1 on success, 0 on failure. The signature must be discarded on
 *         failure.
 */
s32 ed25519Sign(const u8 privkey[ED25519_PRIVKEY_SIZE],
                const void *msg, size_t msgLen,
                u8 outSig[ED25519_SIGNATURE_SIZE]);

/**
 * Derive the public key for the supplied private (seed) key. Useful when
 * loading an identity that only persists the seed bytes.
 *
 * @param privkey  32-byte raw private seed.
 * @param outPub   receives the 32-byte raw public key.
 *
 * @return 1 on success, 0 on failure.
 */
s32 ed25519DerivePubkey(const u8 privkey[ED25519_PRIVKEY_SIZE],
                         u8 outPub[ED25519_PUBKEY_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ED25519_H */
