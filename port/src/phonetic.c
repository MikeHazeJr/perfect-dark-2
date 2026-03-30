/**
 * phonetic.c -- Phonetic IP:port encoding using consonant-vowel syllables.
 *
 * 8 syllables of 2 characters each, grouped as 4 words of 4 characters
 * separated by dashes.  Example: "BALE-GIFE-NOME-RIVA"
 *
 * Encoding uses 6 bits per syllable:
 *   bits [5:2] = consonant index (0-15)
 *   bits [1:0] = vowel index     (0-3)
 * 8 syllables * 6 bits = 48 bits total = 32-bit IPv4 + 16-bit port.
 *
 * Bit layout (big-endian byte order, MSB first):
 *   syllable[0] = bits 47..42
 *   syllable[1] = bits 41..36
 *   ...
 *   syllable[7] = bits  5..0
 */

#include "phonetic.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Alphabet tables
 * ------------------------------------------------------------------------- */

/* 16 consonants — chosen to be distinct when spoken aloud.
 * No C (sounds like K or S), no Q, no X (sounds like KS or Z), no Y.
 * Ordered by index 0-15. */
static const char s_Consonants[16] = {
    'B', 'D', 'F', 'G', 'J', 'K', 'L', 'M',
    'N', 'P', 'R', 'S', 'T', 'V', 'W', 'Z'
};

/* 4 vowels — A, E, I, O only (no U to avoid confusion with V). */
static const char s_Vowels[4] = { 'A', 'E', 'I', 'O' };

/* -------------------------------------------------------------------------
 * Encode
 * ------------------------------------------------------------------------- */

s32 phoneticEncode(u32 ip, u16 port, char *out, size_t outlen)
{
    if (!out || outlen < PHONETIC_STR_MAX) return -1;

    /* Pack 6 bytes into a 48-bit value (big-endian). */
    uint64_t bits = 0;
    bits |= (uint64_t)((ip >>  0) & 0xFF) << 40;
    bits |= (uint64_t)((ip >>  8) & 0xFF) << 32;
    bits |= (uint64_t)((ip >> 16) & 0xFF) << 24;
    bits |= (uint64_t)((ip >> 24) & 0xFF) << 16;
    bits |= (uint64_t)((port >> 8) & 0xFF) << 8;
    bits |= (uint64_t)((port >> 0) & 0xFF);

    /* Write 8 syllables, inserting a dash every 4 characters (= 2 syllables). */
    char *p = out;
    for (int i = 0; i < 8; i++) {
        /* Extract 6 bits for this syllable, MSB first. */
        int shift = 42 - i * 6;
        int syl = (int)((bits >> shift) & 0x3F);

        int consonant_idx = (syl >> 2) & 0xF;
        int vowel_idx     = (syl >> 0) & 0x3;

        *p++ = s_Consonants[consonant_idx];
        *p++ = s_Vowels[vowel_idx];

        /* Dash after every 2 syllables (every 4 chars), except after last. */
        if (i == 1 || i == 3 || i == 5) {
            *p++ = '-';
        }
    }
    *p = '\0';

    return 0;
}

/* -------------------------------------------------------------------------
 * Decode helpers
 * ------------------------------------------------------------------------- */

static int consonantIndex(char c)
{
    c = (char)toupper((unsigned char)c);
    for (int i = 0; i < 16; i++) {
        if (s_Consonants[i] == c) return i;
    }
    return -1;
}

static int vowelIndex(char c)
{
    c = (char)toupper((unsigned char)c);
    for (int i = 0; i < 4; i++) {
        if (s_Vowels[i] == c) return i;
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * Decode
 * ------------------------------------------------------------------------- */

s32 phoneticDecode(const char *str, u32 *out_ip, u16 *out_port)
{
    if (!str || !out_ip || !out_port) return -1;

    /* Strip separators and collect the bare syllable characters. */
    char clean[17]; /* 16 chars + null */
    int  clen = 0;

    for (const char *p = str; *p && clen < 16; p++) {
        char c = *p;
        if (c == '-' || c == ' ' || c == '.' || c == '_') continue;
        if (clen >= 16) return -1;
        clean[clen++] = c;
    }
    clean[clen] = '\0';

    if (clen != 16) return -1; /* must be exactly 8 syllables * 2 chars */

    /* Decode 8 syllables back to 48 bits. */
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) {
        int ci = consonantIndex(clean[i * 2 + 0]);
        int vi = vowelIndex    (clean[i * 2 + 1]);
        if (ci < 0 || vi < 0) return -1;

        int syl = (ci << 2) | vi;
        int shift = 42 - i * 6;
        bits |= (uint64_t)syl << shift;
    }

    /* Unpack bytes. */
    uint8_t b[6];
    b[0] = (uint8_t)((bits >> 40) & 0xFF);
    b[1] = (uint8_t)((bits >> 32) & 0xFF);
    b[2] = (uint8_t)((bits >> 24) & 0xFF);
    b[3] = (uint8_t)((bits >> 16) & 0xFF);
    b[4] = (uint8_t)((bits >>  8) & 0xFF);
    b[5] = (uint8_t)((bits >>  0) & 0xFF);

    /* Reconstruct IP (network byte order — same as encode input). */
    *out_ip = (u32)b[0]
            | ((u32)b[1] <<  8)
            | ((u32)b[2] << 16)
            | ((u32)b[3] << 24);

    /* Reconstruct port (host byte order from big-endian bytes). */
    *out_port = ((u16)b[4] << 8) | (u16)b[5];

    return 0;
}
