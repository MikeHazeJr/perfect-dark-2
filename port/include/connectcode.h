/**
 * connectcode.h -- Encode/decode IP:port as Perfect Dark-themed word codes.
 *
 * An IPv4 address (4 bytes) + port (2 bytes) = 6 bytes is encoded as
 * 6 words from a 256-word vocabulary drawn from Perfect Dark lore.
 * Example: "JOANNA FALCON CARRINGTON SKEDAR PHOENIX DATADYNE"
 *
 * This provides light obfuscation of raw IP addresses for sharing
 * server connection info between players.
 */

#ifndef _IN_CONNECTCODE_H
#define _IN_CONNECTCODE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encode an IPv4 address and port into a word-based connect code.
 * @param ip   IPv4 address in network byte order (big-endian).
 * @param port Port number in host byte order.
 * @param buf  Output buffer (must be at least 256 bytes).
 * @param bufsize Size of output buffer.
 * @return Number of characters written (excluding null), or -1 on error.
 */
s32 connectCodeEncode(u32 ip, u16 port, char *buf, s32 bufsize);

/**
 * Decode a word-based connect code into IP and port.
 * Case-insensitive. Words separated by spaces, hyphens, or dots.
 * @param code The connect code string.
 * @param outIp  Output: IPv4 address in network byte order.
 * @param outPort Output: port number in host byte order.
 * @return 0 on success, -1 on parse failure.
 */
s32 connectCodeDecode(const char *code, u32 *outIp, u16 *outPort);

#ifdef __cplusplus
}
#endif

#endif /* _IN_CONNECTCODE_H */
