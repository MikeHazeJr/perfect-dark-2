/**
 * server_admin.h -- Admin auth + RCON-style command dispatch (MASTER-C2a/C2b).
 *
 * Wave 3 Batch A.  Lets a headless server operator send kick/ban/unban/list/
 * status commands from a connected peer over the existing ENet link.
 *
 * Token flow:
 *   1. Operator configures the server with --admin-token <secret> (or an
 *      [Admin] Token=<secret> line in server.ini).
 *   2. Server stores the SHA-256 hash of the secret (never the plaintext).
 *   3. Operator's client sends CLC_ADMIN with ADMIN_AUTH subcode and the
 *      plaintext token.  Server hashes and compares in constant time.
 *   4. On match, cl->is_admin is set on that netclient.  Subsequent
 *      CLC_ADMIN messages are accepted on that peer without re-sending the
 *      token.  Disconnect drops the flag automatically.
 *
 * All reply flow goes through SVC_ADMIN.
 */

#ifndef _IN_SERVER_ADMIN_H
#define _IN_SERVER_ADMIN_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CLC_ADMIN subcode (wire u8 after the msgid). */
#define ADMIN_SUB_AUTH    0x00  /* token in str field; authenticates this peer */
#define ADMIN_SUB_KICK    0x01  /* u8 clientId + str reason */
#define ADMIN_SUB_BAN     0x02  /* u8 clientId + str reason */
#define ADMIN_SUB_UNBAN   0x03  /* str addr */
#define ADMIN_SUB_LIST    0x04  /* (no args) — reply with ban list */
#define ADMIN_SUB_STATUS  0x05  /* (no args) — reply with server status */

/* SVC_ADMIN response codes (wire u8 after the msgid). */
#define ADMIN_RESP_OK          0x00
#define ADMIN_RESP_BAD_TOKEN   0x01
#define ADMIN_RESP_NOT_AUTH    0x02  /* command sent before successful ADMIN_AUTH */
#define ADMIN_RESP_BAD_ARG     0x03
#define ADMIN_RESP_NOT_FOUND   0x04
#define ADMIN_RESP_SERVER_ERR  0x05
#define ADMIN_RESP_LIST        0x10  /* payload is a multi-line ban listing */
#define ADMIN_RESP_STATUS      0x11  /* payload is a multi-line status dump */

/* Maximum admin-reply payload length on the wire.  Must fit in
 * NET_CLIENT_BUFSIZE (16KB) with plenty of slack. */
#define ADMIN_PAYLOAD_MAX 4096u

/* Load admin configuration.
 *   cliToken — plaintext token passed via --admin-token (or NULL).
 *   iniToken — plaintext token from server.ini (or NULL).
 * The first non-NULL, non-empty token wins.  If both are NULL, the admin
 * subsystem is disabled and all CLC_ADMIN messages are refused with
 * ADMIN_RESP_BAD_TOKEN. */
void serverAdminInit(const char *cliToken, const char *iniToken);

/* Return 1 if an admin token is configured, 0 otherwise.  Used by the
 * message dispatcher to fast-reject when disabled. */
s32 serverAdminEnabled(void);

/* Verify a plaintext token against the configured hash.  Constant-time.
 * Returns 1 on match, 0 on mismatch or when disabled. */
s32 serverAdminVerifyToken(const char *plaintext);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SERVER_ADMIN_H */
