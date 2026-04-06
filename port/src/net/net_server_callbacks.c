/**
 * net_server_callbacks.c — Server-side implementations of the protocol interface.
 *
 * These are called by the networking layer when events occur.
 * The server tracks state and logs — no N64 game logic.
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include "system.h"
#include "net/net_interface.h"

void netcb_OnPlayerJoin(u8 clientId, const char *name, const char *head_id, const char *body_id)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Player '%s' joined (client %u, body=%s, head=%s)",
                 name ? name : "???", clientId,
                 body_id ? body_id : "?", head_id ? head_id : "?");
}

void netcb_OnPlayerLeave(u8 clientId, const char *name, u32 reason)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Player '%s' left (client %u, reason=%u)",
                 name ? name : "???", clientId, reason);
}

void netcb_OnPlayerSettingsChanged(u8 clientId, const char *name, const char *head_id, const char *body_id, u8 team)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Player '%s' changed settings (body=%s, head=%s, team=%u)",
                 name ? name : "???",
                 body_id ? body_id : "?", head_id ? head_id : "?", team);
}

void netcb_OnMatchStart(const char *stage_id, u8 scenario, u32 rngSeed)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Match starting (stage=%s, scenario=%u, seed=%u)",
                 stage_id ? stage_id : "?", scenario, rngSeed);
}

void netcb_OnMatchEnd(void)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Match ended");
}

void netcb_OnPlayerDeath(u8 victimId, u8 killerId, u8 weaponId)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Player %u killed by %u (weapon %u)", victimId, killerId, weaponId);
}

void netcb_OnPlayerRespawn(u8 clientId)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Player %u respawned", clientId);
}

void netcb_OnPlayerPosition(u8 clientId, f32 x, f32 y, f32 z, f32 angle)
{
    /* Position tracking — not logged every frame to avoid spam */
    (void)clientId; (void)x; (void)y; (void)z; (void)angle;
}

void netcb_OnStageChange(const char *stage_id)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Stage changed to %s", stage_id ? stage_id : "?");
}

void netcb_OnStageEnd(void)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Stage ended, returning to lobby");
}

void netcb_OnChat(u8 clientId, const char *message)
{
    sysLogPrintf(LOG_CHAT, "SERVER: [%u] %s", clientId, message ? message : "");
}
