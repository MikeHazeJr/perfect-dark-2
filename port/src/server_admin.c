/**
 * server_admin.c -- Admin token storage + verification (MASTER-C2a).
 *
 * The actual CLC_ADMIN dispatch (kick / ban / etc.) lives in netmsg.c next
 * to the other message handlers so that it can reach internal net state
 * without cross-TU accessors.  This TU owns only the token storage and the
 * constant-time comparison path.
 */

#include <string.h>
#include <stdio.h>
#include <PR/ultratypes.h>

#include "system.h"
#include "sha256.h"
#include "server_admin.h"

static u8  s_AdminTokenHash[SHA256_DIGEST_SIZE];
static s32 s_AdminEnabled = 0;

static void adminHashToken(const char *plaintext, u8 out[SHA256_DIGEST_SIZE])
{
    /* Domain-separate with a context string so a raw SHA-256 oracle can't
     * produce a valid hash via an unrelated input. */
    sha256_ctx ctx;
    sha256Init(&ctx);
    static const char kSalt[] = "pd2-server-admin-token-v1\n";
    sha256Update(&ctx, kSalt, sizeof(kSalt) - 1);
    sha256Update(&ctx, plaintext, strlen(plaintext));
    sha256Final(&ctx, out);
}

void serverAdminInit(const char *cliToken, const char *iniToken)
{
    const char *chosen = NULL;
    const char *src = "(none)";

    if (cliToken && cliToken[0]) {
        chosen = cliToken;
        src = "--admin-token CLI";
    } else if (iniToken && iniToken[0]) {
        chosen = iniToken;
        src = "server.ini [Admin] Token";
    }

    memset(s_AdminTokenHash, 0, sizeof(s_AdminTokenHash));
    s_AdminEnabled = 0;

    if (!chosen) {
        sysLogPrintf(LOG_NOTE, "ADMIN: no admin token configured — RCON disabled");
        return;
    }

    if (strlen(chosen) < 8) {
        sysLogPrintf(LOG_WARNING, "ADMIN: token from %s is shorter than 8 chars — rejected", src);
        return;
    }

    adminHashToken(chosen, s_AdminTokenHash);
    s_AdminEnabled = 1;
    sysLogPrintf(LOG_NOTE, "ADMIN: token loaded from %s (RCON enabled)", src);
}

s32 serverAdminEnabled(void)
{
    return s_AdminEnabled;
}

s32 serverAdminVerifyToken(const char *plaintext)
{
    if (!s_AdminEnabled || !plaintext || !plaintext[0]) {
        return 0;
    }

    u8 supplied[SHA256_DIGEST_SIZE];
    adminHashToken(plaintext, supplied);

    /* Constant-time compare.  Don't short-circuit on the first mismatch —
     * a timing oracle on the first mismatched byte would narrow the search
     * space for a weak token. */
    u8 diff = 0;
    for (s32 i = 0; i < SHA256_DIGEST_SIZE; i++) {
        diff |= (u8)(supplied[i] ^ s_AdminTokenHash[i]);
    }
    return (diff == 0) ? 1 : 0;
}
