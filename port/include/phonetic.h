/**
 * phonetic.h -- Phonetic IP:port encoding using consonant-vowel syllables.
 *
 * Encodes an IPv4 address + port (6 bytes / 48 bits) as 8 two-character
 * CV syllables, displayed as 4 groups of 4 characters separated by dashes.
 *
 * Example:  192.168.1.100:27100  -->  "BALE-GIFE-NOME-RIVA"
 *
 * Shorter and faster to type than the full word-based connect code.
 * Both systems remain available; use whichever suits the context.
 *
 * Encoding:
 *   16 consonants (4 bits): B D F G J K L M N P R S T V W Z
 *    4 vowels     (2 bits): A E I O
 *   Syllable = consonant(hi 4 bits) | vowel(lo 2 bits) = 6 bits
 *   8 syllables x 6 bits = 48 bits = 32-bit IP + 16-bit port (big-endian)
 */

#ifndef _IN_PHONETIC_H
#define _IN_PHONETIC_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum characters in an encoded phonetic string (including null). */
#define PHONETIC_STR_MAX 24

/**
 * Encode an IPv4 address and port into a phonetic syllable string.
 *
 * @param ip      IPv4 address in network byte order.
 * @param port    Port number in host byte order.
 * @param out     Output buffer, at least PHONETIC_STR_MAX bytes.
 * @param outlen  Size of output buffer.
 * @return 0 on success, -1 if buffer too small.
 */
s32 phoneticEncode(u32 ip, u16 port, char *out, size_t outlen);

/**
 * Decode a phonetic syllable string back to IPv4 address and port.
 * Case-insensitive. Dashes, spaces, and dots are treated as separators
 * and are ignored during decoding.
 *
 * @param str      The phonetic string to decode.
 * @param out_ip   Output: IPv4 address in network byte order.
 * @param out_port Output: port number in host byte order.
 * @return 0 on success, -1 on parse failure.
 */
s32 phoneticDecode(const char *str, u32 *out_ip, u16 *out_port);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PHONETIC_H */
