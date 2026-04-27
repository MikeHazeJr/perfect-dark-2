/*
 * tests/savebuffer_pure.c -- Pure bit-pack primitives extracted from
 * src/game/savebuffer.c lines 381-471 (as of 2026-04-26).
 *
 * Why a copy instead of compiling savebuffer.c directly: savebuffer.c
 * also contains GBI/VI/Mtx-dependent functions (func0f0d4690 et al.)
 * that drag in the entire renderer surface. Pulling savebuffer.c into
 * pd-tests would cascade through gfxAllocate, viGetWidth, mtx ops,
 * and ~50 stubs deep. The pure bit-pack subset is what regression
 * tests actually need, and it has zero external dependencies.
 *
 * The functions below are byte-identical copies of the originals.
 * If src/game/savebuffer.c diverges, this file should be re-synced.
 * The drift audit is a single `diff` command:
 *   diff <(sed -n '381,471p' src/game/savebuffer.c) tests/savebuffer_pure.c
 * (modulo the prefix `pdtest_` on each function name to avoid linker
 * collision when tests later compile against the real savebuffer.c).
 *
 * NOTE: These wrappers preserve the exact semantics of the originals,
 * including the historical quirks of savebufferWriteData (resets
 * bitpos to 0) and savebufferReadString_ext's null-terminator handling.
 */

#include <stddef.h>
#include "savebuffer_pure.h"

void pdtest_savebufferOr(pdtest_savebuffer *buffer, unsigned long long value, int numbits)
{
    unsigned long long bit = 1ULL << (numbits - 1ULL);

    for (; bit; bit >>= 1) {
        if (bit & value) {
            int bitindex = (int)(buffer->bitpos % 8);
            unsigned char mask = (unsigned char)(1 << (7 - bitindex));
            int byteindex = (int)(buffer->bitpos / 8);

            buffer->bytes[byteindex] |= mask;
        }

        buffer->bitpos++;
    }
}

unsigned long long pdtest_savebufferReadBits(pdtest_savebuffer *buffer, int numbits)
{
    unsigned long long bit = 1ULL << (numbits - 1);
    unsigned long long value = 0;

    for (; bit; bit >>= 1) {
        int bitindex = (int)(buffer->bitpos % 8);
        unsigned char mask = (unsigned char)(1 << (7 - bitindex));
        int byteindex = (int)(buffer->bitpos / 8);

        if (buffer->bytes[byteindex] & mask) {
            value |= bit;
        }

        buffer->bitpos++;
    }

    return value;
}

void pdtest_savebufferClear(pdtest_savebuffer *buffer)
{
    int i;

    buffer->bitpos = 0;

    for (i = 0; i < (int)sizeof(buffer->bytes);) {
        buffer->bytes[i] = 0;
        i++;
    }
}
