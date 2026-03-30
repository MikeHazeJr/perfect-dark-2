/**
 * connectcode.h -- Encode/decode IP as a memorable 4-word sentence.
 *
 * An IPv4 address (4 bytes) is encoded as 4 words in sentence structure:
 *   [adjective] [noun] [verb] [noun]
 *   Example: "sneaky falcon chasing castle"
 *
 * Port is assumed to be the default (CONNECT_DEFAULT_PORT).
 * If a non-standard port is needed, append ":PORT" to the decoded IP.
 *
 * Case-insensitive decode. Separators: spaces, hyphens, dots.
 */

#ifndef _IN_CONNECTCODE_H
#define _IN_CONNECTCODE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONNECT_DEFAULT_PORT 27100
#define CONNECT_CODE_MAX     128

/**
 * Encode an IPv4 address into a 4-word sentence code.
 * @param ip      IPv4 address in network byte order (big-endian).
 * @param buf     Output buffer (at least CONNECT_CODE_MAX bytes).
 * @param bufsize Size of output buffer.
 * @return Number of characters written (excluding null), or -1 on error.
 */
s32 connectCodeEncode(u32 ip, char *buf, s32 bufsize);

/**
 * Decode a 4-word sentence code back to an IPv4 address.
 * Case-insensitive. Words separated by spaces, hyphens, or dots.
 * @param code   The sentence code string.
 * @param outIp  Output: IPv4 address in network byte order.
 * @return 0 on success, -1 on parse failure.
 */
s32 connectCodeDecode(const char *code, u32 *outIp);

/**
 * Encode an IPv4 address + port into a sentence code.
 * If port == CONNECT_DEFAULT_PORT, produces the standard 4-word code.
 * Otherwise appends two extra words (adjective + noun) encoding the port
 * as high-byte/low-byte using the same dictionaries:
 *   "[adj] [noun] [action] [place] [adj2] [noun2]"
 * @param ip      IPv4 address in network byte order.
 * @param port    Port in host byte order.
 * @param buf     Output buffer (at least CONNECT_CODE_MAX bytes).
 * @param bufsize Size of output buffer.
 * @return Number of characters written (excluding null), or -1 on error.
 */
s32 connectCodeEncodeWithPort(u32 ip, u16 port, char *buf, s32 bufsize);

/**
 * Decode a sentence code (4 or 6 words) into IPv4 + port.
 * A 4-word code sets *outPort = CONNECT_DEFAULT_PORT.
 * A 6-word code decodes the extra adjective+noun pair as the port.
 * @param code    The sentence code string.
 * @param outIp   Output: IPv4 address in network byte order.
 * @param outPort Output: port in host byte order.
 * @return 0 on success, -1 on parse failure.
 */
s32 connectCodeDecodeWithPort(const char *code, u32 *outIp, u16 *outPort);

#ifdef __cplusplus
}
#endif

#endif /* _IN_CONNECTCODE_H */
