/**
 * server_bans.h -- Persistent ban list (MASTER-C2c).
 *
 * Wave 3 Batch A. Loads bans.ini at startup, enforces in netServerEvConnect,
 * adds/removes entries via admin RCON commands or the server GUI.
 *
 * File format (one entry per line; fields separated by tabs):
 *     <ip-address>\t<name>\t<timestamp>\t<reason>
 *
 * Lines beginning with ';' or '#' are comments.  Blank lines are ignored.
 * IP is matched exactly (IPv4 or IPv6 string form as produced by
 * enet_address_get_ip).  No CIDR / subnet matching yet.
 *
 * The file lives at "$S/bans.ini" (save dir) so it survives server restarts.
 */

#ifndef _IN_SERVER_BANS_H
#define _IN_SERVER_BANS_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERVER_BANS_MAX         256
#define SERVER_BANS_ADDR_LEN    64   /* matches enet_address_get_ip output size */
#define SERVER_BANS_NAME_LEN    32
#define SERVER_BANS_REASON_LEN  128

typedef struct server_ban_entry {
    char  addr[SERVER_BANS_ADDR_LEN];     /* IP string, no port */
    char  name[SERVER_BANS_NAME_LEN];     /* last-known player name (advisory) */
    u64   timestamp;                      /* unix time when banned */
    char  reason[SERVER_BANS_REASON_LEN]; /* human-readable reason */
} server_ban_entry_t;

/* Load bans from $S/bans.ini into memory.  Safe to call multiple times — idempotent.
 * Returns the number of entries loaded; 0 if file missing or unreadable. */
s32 serverBansInit(void);

/* Persist the in-memory ban list back to $S/bans.ini.  Atomic via temp-file swap. */
s32 serverBansSave(void);

/* Return 1 if the IP is banned, 0 otherwise.  Addresses are compared after
 * inet_pton normalization (IPv4 vs IPv4-mapped IPv6 canonicalize to the same
 * 16-byte form). */
s32 serverBansIsBanned(const char *addr);

/* Add an entry.  If addr is already banned, updates name/timestamp/reason.
 * Returns 1 on success, 0 if the list is full.  Automatically saves. */
s32 serverBansAdd(const char *addr, const char *name, const char *reason);

/* Remove the entry matching addr.  Returns 1 if removed, 0 if not found.
 * Automatically saves. */
s32 serverBansRemove(const char *addr);

/* Write a plain-text listing of all entries (one per line) to out.
 * Returns the number of bytes written (excluding the terminating NUL). */
s32 serverBansList(char *out, size_t outsize);

/* Return the raw entries.  Intended for the server GUI and CLC_ADMIN ADMIN_LIST. */
s32 serverBansGetCount(void);
const server_ban_entry_t *serverBansGetEntry(s32 idx);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SERVER_BANS_H */
