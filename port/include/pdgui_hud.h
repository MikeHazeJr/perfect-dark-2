/**
 * pdgui_hud.h -- In-match HUD overlay: top scorers + timer.
 *
 * Rendered during CLSTATE_GAME (normmplayerisrunning active).
 * Shows top 2 players by score and remaining match time.
 */

#pragma once

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void pdguiHudRender(s32 winW, s32 winH);

/**
 * Push a kill event into the ImGui killfeed overlay.
 * Called from mpstatsRecordDeath() for every kill in the match.
 * Team values are 0-7 matching MpSetup team indices.
 */
void pdguiKillfeedPush(const char *attackerName, u8 attackerTeam,
                       const char *victimName, u8 victimTeam,
                       s32 isSuicide);

/* Bridge functions for HUD score panel (pdgui_bridge.c) */
s32 pdguiHudIsRadarVisible(void);
s32 pdguiHudGetRadarRect(float *outX, float *outY, float *outW, float *outH);
s32 pdguiHudGetScoreLimit(void);
s32 pdguiHudGetTeamScoreLimit(void);
s32 pdguiHudIsTeamsEnabled(void);
u32 pdguiHudGetTeamColor(s32 teamIndex);
const char *pdguiHudGetTeamName(s32 teamIndex);

#ifdef __cplusplus
}
#endif
