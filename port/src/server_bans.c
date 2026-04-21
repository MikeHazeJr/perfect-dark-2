/**
 * server_bans.c -- Persistent IP ban list (MASTER-C2c, Wave 3 Batch A).
 *
 * Loaded once at server boot by serverBansInit().  Enforced in
 * netServerEvConnect before any ENet slot allocation.  Written on every
 * add/remove so a crash won't lose pending bans.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#include <PR/ultratypes.h>

#include "fs.h"
#include "system.h"
#include "server_bans.h"

#define BANS_FILE_RELPATH  "$S/bans.ini"
#define BANS_FILE_TMPPATH  "$S/bans.ini.tmp"

static server_ban_entry_t s_Bans[SERVER_BANS_MAX];
static s32 s_BanCount = 0;
static s32 s_Initialized = 0;

/* Parse IPv4 or IPv6 text into a canonical 128-bit form.  IPv4 addresses are
 * stored as IPv4-mapped IPv6 (::ffff:a.b.c.d) so they match the same address
 * written in either v4 or mapped-v6 form (e.g. 203.0.113.1 vs ::ffff:cb00:7101). */
static int banAddrParseNormalized(const char *s, struct in6_addr *out6)
{
    if (!s || !s[0]) return 0;
    if (inet_pton(AF_INET6, s, out6) == 1) {
        return 1;
    }
    struct in_addr v4;
    if (inet_pton(AF_INET, s, &v4) == 1) {
        memset(out6, 0, 10);
        out6->s6_addr[10] = 0xff;
        out6->s6_addr[11] = 0xff;
        memcpy(out6->s6_addr + 12, &v4.s_addr, 4);
        return 1;
    }
    return 0;
}

static int banAddrEq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    struct in6_addr a6, b6;
    if (banAddrParseNormalized(a, &a6) && banAddrParseNormalized(b, &b6)) {
        return memcmp(&a6, &b6, sizeof(a6)) == 0;
    }
    /* Hostnames or non-INET strings: legacy case-insensitive equality */
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* Strip trailing newline/CR and leading whitespace in place. */
static void banTrim(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == '\t' || s[n-1] == ' ')) {
        s[--n] = '\0';
    }
}

/* Parse one line.  Tab-delimited: addr \t name \t timestamp \t reason.
 * Reason may contain spaces but no further tabs.  Returns 1 on success. */
static int banParseLine(char *line, server_ban_entry_t *out)
{
    if (!line || !out) return 0;

    memset(out, 0, sizeof(*out));

    char *addr = line;
    char *tab1 = strchr(addr, '\t');
    if (!tab1) return 0;
    *tab1 = '\0';
    char *name = tab1 + 1;

    char *tab2 = strchr(name, '\t');
    if (!tab2) {
        /* Minimal form: just an address, rest optional. */
        strncpy(out->addr, addr, sizeof(out->addr) - 1);
        return out->addr[0] ? 1 : 0;
    }
    *tab2 = '\0';
    char *tsstr = tab2 + 1;

    char *tab3 = strchr(tsstr, '\t');
    char *reason = NULL;
    if (tab3) {
        *tab3 = '\0';
        reason = tab3 + 1;
    }

    strncpy(out->addr, addr, sizeof(out->addr) - 1);
    strncpy(out->name, name, sizeof(out->name) - 1);
    out->timestamp = (u64)strtoull(tsstr, NULL, 10);
    if (reason) strncpy(out->reason, reason, sizeof(out->reason) - 1);

    return out->addr[0] ? 1 : 0;
}

s32 serverBansInit(void)
{
    s_BanCount = 0;
    s_Initialized = 1;

    const char *path = fsFullPath(BANS_FILE_RELPATH);
    if (!path) {
        sysLogPrintf(LOG_NOTE, "BANS: fsFullPath failed for %s", BANS_FILE_RELPATH);
        return 0;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        sysLogPrintf(LOG_NOTE, "BANS: no ban file at %s (0 bans loaded)", path);
        return 0;
    }

    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        /* Strip trailing newline / whitespace so the last field doesn't inherit it. */
        banTrim(buf);
        if (buf[0] == '\0' || buf[0] == ';' || buf[0] == '#') continue;
        if (s_BanCount >= SERVER_BANS_MAX) {
            sysLogPrintf(LOG_WARNING, "BANS: entry cap %d reached; remaining file lines skipped", SERVER_BANS_MAX);
            break;
        }
        if (banParseLine(buf, &s_Bans[s_BanCount])) {
            s_BanCount++;
        }
    }
    fclose(f);

    sysLogPrintf(LOG_NOTE, "BANS: loaded %d ban entries from %s", s_BanCount, path);
    return s_BanCount;
}

s32 serverBansSave(void)
{
    const char *tmp = fsFullPath(BANS_FILE_TMPPATH);
    const char *final = fsFullPath(BANS_FILE_RELPATH);
    if (!tmp || !final) return 0;

    /* fsFullPath returns a shared static — copy out before the second call. */
    char tmpPath[FS_MAXPATH + 1];
    strncpy(tmpPath, tmp, sizeof(tmpPath) - 1);
    tmpPath[sizeof(tmpPath) - 1] = '\0';

    /* Re-resolve the final path (fsFullPath uses a shared static buffer). */
    const char *finalResolved = fsFullPath(BANS_FILE_RELPATH);
    char finalPath[FS_MAXPATH + 1];
    strncpy(finalPath, finalResolved, sizeof(finalPath) - 1);
    finalPath[sizeof(finalPath) - 1] = '\0';

    FILE *f = fopen(tmpPath, "w");
    if (!f) {
        sysLogPrintf(LOG_WARNING, "BANS: cannot open %s for write", tmpPath);
        return 0;
    }

    fprintf(f, "; PD2 dedicated-server ban list\n");
    fprintf(f, "; Format: <ip>\\t<name>\\t<timestamp>\\t<reason>\n");
    for (s32 i = 0; i < s_BanCount; i++) {
        const server_ban_entry_t *e = &s_Bans[i];
        fprintf(f, "%s\t%s\t%llu\t%s\n",
                e->addr, e->name,
                (unsigned long long)e->timestamp,
                e->reason);
    }
    fflush(f);
#if defined(_WIN32)
    if (_commit(_fileno(f)) != 0) {
        fclose(f);
        sysLogPrintf(LOG_WARNING, "BANS: _commit failed for %s", tmpPath);
        return 0;
    }
#endif
    fclose(f);

#if defined(_WIN32)
    if (!MoveFileExA(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        sysLogPrintf(LOG_WARNING, "BANS: MoveFileExA %s -> %s failed (err %lu)",
                     tmpPath, finalPath, (unsigned long)GetLastError());
        return 0;
    }
#else
    /* Atomic replace on the same filesystem (POSIX). */
    if (rename(tmpPath, finalPath) != 0) {
        sysLogPrintf(LOG_WARNING, "BANS: rename %s -> %s failed", tmpPath, finalPath);
        return 0;
    }
#endif
    sysLogPrintf(LOG_NOTE, "BANS: wrote %d entries to %s", s_BanCount, finalPath);
    return 1;
}

s32 serverBansIsBanned(const char *addr)
{
    if (!s_Initialized || !addr || !addr[0]) return 0;
    for (s32 i = 0; i < s_BanCount; i++) {
        if (banAddrEq(s_Bans[i].addr, addr)) return 1;
    }
    return 0;
}

s32 serverBansAdd(const char *addr, const char *name, const char *reason)
{
    if (!s_Initialized) serverBansInit();
    if (!addr || !addr[0]) return 0;

    /* Update in place if already present. */
    for (s32 i = 0; i < s_BanCount; i++) {
        if (banAddrEq(s_Bans[i].addr, addr)) {
            if (name) {
                strncpy(s_Bans[i].name, name, sizeof(s_Bans[i].name) - 1);
                s_Bans[i].name[sizeof(s_Bans[i].name) - 1] = '\0';
            }
            if (reason) {
                strncpy(s_Bans[i].reason, reason, sizeof(s_Bans[i].reason) - 1);
                s_Bans[i].reason[sizeof(s_Bans[i].reason) - 1] = '\0';
            }
            s_Bans[i].timestamp = (u64)time(NULL);
            serverBansSave();
            sysLogPrintf(LOG_NOTE, "BANS: updated %s", addr);
            return 1;
        }
    }

    if (s_BanCount >= SERVER_BANS_MAX) {
        sysLogPrintf(LOG_WARNING, "BANS: cannot add %s: list full (%d)", addr, SERVER_BANS_MAX);
        return 0;
    }

    server_ban_entry_t *e = &s_Bans[s_BanCount++];
    memset(e, 0, sizeof(*e));
    strncpy(e->addr, addr, sizeof(e->addr) - 1);
    if (name)   strncpy(e->name,   name,   sizeof(e->name)   - 1);
    if (reason) strncpy(e->reason, reason, sizeof(e->reason) - 1);
    e->timestamp = (u64)time(NULL);

    serverBansSave();
    sysLogPrintf(LOG_NOTE, "BANS: added %s (name='%s' reason='%s')",
                 e->addr, e->name, e->reason);
    return 1;
}

s32 serverBansRemove(const char *addr)
{
    if (!s_Initialized || !addr || !addr[0]) return 0;
    for (s32 i = 0; i < s_BanCount; i++) {
        if (banAddrEq(s_Bans[i].addr, addr)) {
            for (s32 j = i; j < s_BanCount - 1; j++) {
                s_Bans[j] = s_Bans[j + 1];
            }
            s_BanCount--;
            serverBansSave();
            sysLogPrintf(LOG_NOTE, "BANS: removed %s", addr);
            return 1;
        }
    }
    return 0;
}

s32 serverBansList(char *out, size_t outsize)
{
    if (!out || outsize == 0) return 0;
    out[0] = '\0';
    size_t used = 0;
    for (s32 i = 0; i < s_BanCount; i++) {
        const server_ban_entry_t *e = &s_Bans[i];
        char line[SERVER_BANS_ADDR_LEN + SERVER_BANS_NAME_LEN + SERVER_BANS_REASON_LEN + 64];
        int n = snprintf(line, sizeof(line),
                         "%s  %s  %s\n",
                         e->addr,
                         e->name[0] ? e->name : "?",
                         e->reason[0] ? e->reason : "no reason");
        if (n < 0) continue;
        if (used + (size_t)n + 1 >= outsize) break;
        memcpy(out + used, line, (size_t)n);
        used += (size_t)n;
        out[used] = '\0';
    }
    return (s32)used;
}

s32 serverBansGetCount(void)
{
    return s_BanCount;
}

const server_ban_entry_t *serverBansGetEntry(s32 idx)
{
    if (idx < 0 || idx >= s_BanCount) return NULL;
    return &s_Bans[idx];
}
