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

void netcb_OnPlayerJoin(u8 clientId, const char *name, u8 headnum, u8 bodynum)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Player '%s' joined (client %u, body=%u, head=%u)",
                 name ? name : "???", clientId, bodynum, headnum);
}

void netcb_OnPlayerLeave(u8 clientId, const char *name, u32 reason)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Player '%s' left (client %u, reason=%u)",
                 name ? name : "???", clientId, reason);
}

void netcb_OnPlayerSettingsChanged(u8 clientId, const char *name, u8 headnum, u8 bodynum, u8 team)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Player '%s' changed settings (body=%u, head=%u, team=%u)",
                 name ? name : "???", bodynum, headnum, team);
}

void netcb_OnMatchStart(u8 stagenum, u8 scenario, u32 rngSeed)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Match starting (stage=0x%02x, scenario=%u, seed=%u)",
                 stagenum, scenario, rngSeed);
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

void netcb_OnStageChange(u8 stagenum)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Stage changed to 0x%02x", stagenum);
}

void netcb_OnStageEnd(void)
{
    sysLogPrintf(LOG_NOTE, "SERVER: Stage ended, returning to lobby");
}

void netcb_OnChat(u8 clientId, const char *message)
{
    sysLogPrintf(LOG_CHAT, "SERVER: [%u] %s", clientId, message ? message : "");
}
