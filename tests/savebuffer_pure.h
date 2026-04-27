/*
 * tests/savebuffer_pure.h -- Header for the savebuffer pure-subset copy.
 *
 * See savebuffer_pure.c for the rationale (no game-types pull, byte-
 * identical copy of the bit-pack primitives from src/game/savebuffer.c).
 *
 * `pdtest_savebuffer` mirrors the layout of `struct savebuffer` in
 * src/include/types.h (u32 bitpos + u8 bytes[220]). The 220-byte size
 * matches the live struct exactly.
 */

#ifndef PDTEST_SAVEBUFFER_PURE_H
#define PDTEST_SAVEBUFFER_PURE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pdtest_savebuffer {
    unsigned int  bitpos;
    unsigned char bytes[220];
} pdtest_savebuffer;

void pdtest_savebufferOr(pdtest_savebuffer *buffer, unsigned long long value, int numbits);
unsigned long long pdtest_savebufferReadBits(pdtest_savebuffer *buffer, int numbits);
void pdtest_savebufferClear(pdtest_savebuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif /* PDTEST_SAVEBUFFER_PURE_H */
