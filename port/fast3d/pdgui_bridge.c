/**
 * pdgui_bridge.c -- C bridge functions for ImGui menu code.
 *
 * ImGui menu files (.cpp) cannot include types.h because it #defines
 * bool as s32, which breaks C++. This file provides small accessor
 * functions that the C++ menu code can call to safely read/write
 * game data structures that require types.h knowledge.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "net/netenet.h"  /* must precede types.h — enet.h #undef's bool */
#include "types.h"
#include "data.h"
#include "bss.h"
#include "system.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/cheats.h"
#include "game/endscreen.h"
#include "game/mainmenu.h"
#include "game/menu.h"
#include "modmgr.h"

/**
 * Set the MP player config name for a given player number.
 * Safely handles bounds checking and null termination.
 */
void mpPlayerConfigSetName(s32 playernum, const char *name)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return;
    }
    if (!name) {
        return;
    }

    strncpy(g_PlayerConfigsArray[playernum].base.name, name, 14);
    g_PlayerConfigsArray[playernum].base.name[14] = '\0';
}

/**
 * Set the MP player config head and body indices.
 */
void mpPlayerConfigSetHeadBody(s32 playernum, u8 headnum, u8 bodynum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return;
    }

    g_PlayerConfigsArray[playernum].base.mpheadnum = headnum;
    g_PlayerConfigsArray[playernum].base.mpbodynum = bodynum;
}

/**
 * Get the MP player config head index.
 */
u8 mpPlayerConfigGetHead(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return 0;
    }
    return g_PlayerConfigsArray[playernum].base.mpheadnum;
}

/**
 * Get the MP player config body index.
 */
u8 mpPlayerConfigGetBody(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return 0;
    }
    return g_PlayerConfigsArray[playernum].base.mpbodynum;
}

/**
 * Get the MP player config name (read-only pointer).
 */
const char *mpPlayerConfigGetName(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return "";
    }
    return g_PlayerConfigsArray[playernum].base.name;
}

/* ========================================================================
 * Network lobby bridge functions
 * ======================================================================== */

s32 netGetMode(void)
{
    return g_NetMode;
}

s32 netGetNumClients(void)
{
    return g_NetNumClients;
}

s32 netGetMaxClients(void)
{
    return g_NetMaxClients;
}

u32 netGetServerPort(void)
{
    return g_NetServerPort;
}

const char *netGetPublicIP(void)
{
    extern const char *netUpnpGetExternalIP(void);
    extern s32 netUpnpIsActive(void);

    /* Try UPnP first */
    if (netUpnpIsActive()) {
        const char *upnpIP = netUpnpGetExternalIP();
        if (upnpIP && upnpIP[0]) {
            return upnpIP;
        }
    }

    /* Try STUN next */
    {
        extern s32 stunGetStatus(void);
        extern const char *stunGetExternalIP(void);
        if (stunGetStatus() == 2) {  /* STUN_STATUS_SUCCESS */
            const char *stunIp = stunGetExternalIP();
            if (stunIp && stunIp[0]) return stunIp;
        }
    }

    /* Fallback: query external IP via HTTP (cached after first success).
     * Uses curl to query a lightweight IP echo service. */
    static char s_CachedIP[64] = "";
    static s32 s_Tried = 0;

    if (s_CachedIP[0]) {
        return s_CachedIP;
    }

    if (!s_Tried) {
        s_Tried = 1;
        extern s32 netHttpGetPublicIP(char *buf, s32 bufsize);
        if (netHttpGetPublicIP(s_CachedIP, sizeof(s_CachedIP)) == 0) {
            sysLogPrintf(LOG_NOTE, "NET: public IP resolved via HTTP fallback");
        } else {
            sysLogPrintf(LOG_WARNING, "NET: failed to resolve public IP (UPnP and HTTP both failed)");
        }
    }

    return s_CachedIP;
}

/**
 * Get number of connected client slots (including server's local client).
 * Returns count of clients in non-disconnected states.
 */
s32 netLobbyGetClientCount(void)
{
    if (g_NetMode == NETMODE_NONE) return 0;

    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            count++;
        }
    }
    return count;
}

/**
 * Get client state by sequential index (skipping disconnected slots).
 * Returns CLSTATE_DISCONNECTED if index out of range.
 */
s32 netLobbyGetClientState(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) return g_NetClients[i].state;
            count++;
        }
    }
    return CLSTATE_DISCONNECTED;
}

const char *netLobbyGetClientName(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) {
                if (g_NetClients[i].settings.name[0]) {
                    return g_NetClients[i].settings.name;
                }
                return "Player";
            }
            count++;
        }
    }
    return "???";
}

u8 netLobbyGetClientHead(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) {
                const asset_entry_t *e = assetCatalogResolve(g_NetClients[i].settings.head_id);
                return e ? (u8)e->runtime_index : 0;
            }
            count++;
        }
    }
    return 0;
}

u8 netLobbyGetClientBody(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) {
                const asset_entry_t *e = assetCatalogResolve(g_NetClients[i].settings.body_id);
                return e ? (u8)e->runtime_index : 0;
            }
            count++;
        }
    }
    return 0;
}

u8 netLobbyGetClientTeam(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) return g_NetClients[i].settings.team;
            count++;
        }
    }
    return 0;
}

s32 netLobbyIsLocalClient(s32 idx)
{
    s32 count = 0;
    for (s32 i = 0; i <= NET_MAX_CLIENTS; i++) {
        if (g_NetClients[i].state != CLSTATE_DISCONNECTED) {
            if (count == idx) return (&g_NetClients[i] == g_NetLocalClient) ? 1 : 0;
            count++;
        }
    }
    return 0;
}

/* ========================================================================
 * Lobby state bridge functions
 * ======================================================================== */

s32 lobbyGetPlayerCount(void)
{
    lobbyUpdate();
    return g_Lobby.numPlayers;
}

/* Fills a simplified player view struct for ImGui.
 * The struct layout must match lobbyplayer_view in pdgui_lobby.cpp. */
s32 lobbyGetPlayerInfo(s32 idx, void *out)
{
    if (idx < 0 || idx >= g_Lobby.numPlayers || !out) return 0;

    struct lobbyplayer *lp = &g_Lobby.players[idx];
    if (!lp->active) return 0;

    /* Write fields matching lobbyplayer_view layout */
    u8 *p = (u8 *)out;
    p[0] = lp->active;
    p[1] = lp->isLeader;
    p[2] = lp->isReady;
    p[3] = lp->headnum;
    p[4] = lp->bodynum;
    p[5] = lp->team;
    strncpy((char *)(p + 6), lp->name, 31);
    p[37] = '\0';

    /* isLocal (s32 at offset 40, aligned after name[32]) */
    s32 isLocal = (&g_NetClients[lp->clientId] == g_NetLocalClient) ? 1 : 0;
    memcpy(p + 40, &isLocal, sizeof(s32));

    /* state (s32 at offset 44) */
    s32 state = g_NetClients[lp->clientId].state;
    memcpy(p + 44, &state, sizeof(s32));

    return 1;
}

s32 netLocalClientInLobby(void)
{
    if (g_NetMode == NETMODE_NONE || !g_NetLocalClient) return 0;
    return (g_NetLocalClient->state == CLSTATE_LOBBY) ? 1 : 0;
}

u32 netGetClientPing(s32 clientId)
{
    if (clientId < 0 || clientId > NET_MAX_CLIENTS) return 0;
    struct netclient *cl = &g_NetClients[clientId];
    if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) return 0;
    /* ENet peer round-trip time in milliseconds */
    return cl->peer->roundTripTime;
}

void netServerKickClient(s32 clientId, const char *reason)
{
    if (clientId < 0 || clientId > NET_MAX_CLIENTS) return;
    if (g_NetMode != NETMODE_SERVER) return;

    struct netclient *cl = &g_NetClients[clientId];
    if (cl->state == CLSTATE_DISCONNECTED || !cl->peer) return;

    sysLogPrintf(LOG_NOTE, "NET: kicking client %d (%s): %s",
                 clientId, cl->settings.name, reason ? reason : "no reason");
    enet_peer_disconnect(cl->peer, 0);
}

/* ========================================================================
 * HUD bridge functions (pdgui_hud.cpp)
 * ======================================================================== */

/* g_MpTimeLimit60: match time limit in 60Hz ticks. 0 = unlimited.
 * Declared in lv.c but not exported via lv.h — extern here for bridge use. */
extern s32 g_MpTimeLimit60;

s32 pdguiHudGetTimeLimitTicks(void)
{
    return g_MpTimeLimit60;
}

/* ========================================================================
 * Pause menu bridge functions (pdgui_menu_pausemenu.cpp)
 * ======================================================================== */

u32 pdguiPauseGetChrSlots(void)
{
    return g_MpSetup.chrslots;
}

u32 pdguiPauseGetOptions(void)
{
    return g_MpSetup.options;
}

u8 pdguiPauseGetScenario(void)
{
    return g_MpSetup.scenario;
}

u8 pdguiPauseGetStagenum(void)
{
    return g_MpSetup.stagenum;
}

u8 pdguiPauseGetTimelimit(void)
{
    return g_MpSetup.timelimit;
}

u8 pdguiPauseGetScorelimit(void)
{
    return g_MpSetup.scorelimit;
}

u8 pdguiPauseGetPaused(void)
{
    return g_MpSetup.paused;
}

s32 pdguiPauseGetNormMplayerIsRunning(void)
{
    return g_Vars.normmplayerisrunning ? 1 : 0;
}

void pdguiPauseSetPlayerAborted(void)
{
    if (g_Vars.currentplayer) {
        g_Vars.currentplayer->aborted = true;
    }
}

const char *pdguiPauseGetStageName(u8 stagenum)
{
    s32 count = modmgrGetTotalArenas();
    for (s32 i = 0; i < count; i++) {
        struct mparena *arena = modmgrGetArena(i);
        if (arena && arena->stagenum == stagenum) {
            return langGet(arena->name);
        }
    }

    return "Unknown";
}

/* ========================================================================
 * Endscreen bridge functions (pdgui_menu_pausemenu.cpp + pdgui_menu_mpingame.cpp)
 * ======================================================================== */

/* Forward declarations — no public headers for these */
char *mpPlayerGetWeaponOfChoiceName(u32 playernum, u32 slot);
s32   challengeIsCompleteForEndscreen(void);

/**
 * Placement index (0=1st, 1=2nd, ...) for the local player at match end.
 * Returns -1 if no local player data is available.
 */
s32 pdguiEndscreenGetPlacementIndex(void)
{
    s32 idx = g_MpPlayerNum;
    if (idx < 0 || idx >= MAX_PLAYERS) idx = 0;
    return (s32)g_PlayerConfigsArray[idx].base.placement;
}

/**
 * Title string for the local player (e.g. "Agent", "Special Agent").
 * Returns "" if not available.
 */
const char *pdguiEndscreenGetTitle(void)
{
    s32 idx = g_MpPlayerNum;
    if (idx < 0 || idx >= MAX_PLAYERS) idx = 0;
    s32 title = g_PlayerConfigsArray[idx].title;
    return langGet(L_MISC_185 + title);
}

/**
 * New title text if title changed, else same as current title.
 * Used to detect a title-change flash at endscreen.
 */
s32 pdguiEndscreenTitleChanged(void)
{
    s32 idx = g_MpPlayerNum;
    if (idx < 0 || idx >= MAX_PLAYERS) idx = 0;
    return (g_PlayerConfigsArray[idx].title != g_PlayerConfigsArray[idx].newtitle) ? 1 : 0;
}

/**
 * Weapon of choice name for the local player. Returns "" if none.
 */
const char *pdguiEndscreenGetWeaponOfChoiceName(void)
{
    s32 idx = g_MpPlayerNum;
    if (idx < 0 || idx >= MAX_PLAYERS) idx = 0;
    char *name = mpPlayerGetWeaponOfChoiceName((u32)idx, 0);
    return name ? name : "";
}

/**
 * Award string 1 for the local player (e.g. "Most Kills"). Returns "" if none.
 */
const char *pdguiEndscreenGetAward1(void)
{
    s32 idx = g_MpPlayerNum;
    if (idx < 0 || idx >= MAX_PLAYERS) return "";
    if (!g_Vars.players[idx]) return "";
    const char *a = g_Vars.players[idx]->award1;
    return a ? a : "";
}

/**
 * Award string 2 for the local player. Returns "" if none.
 */
const char *pdguiEndscreenGetAward2(void)
{
    s32 idx = g_MpPlayerNum;
    if (idx < 0 || idx >= MAX_PLAYERS) return "";
    if (!g_Vars.players[idx]) return "";
    const char *a = g_Vars.players[idx]->award2;
    return a ? a : "";
}

/**
 * Medal bitmask for the local player.
 * Bits: 0=Killmaster, 1=Headshot, 2=Accuracy, 3=Survivor
 */
u32 pdguiEndscreenGetMedals(void)
{
    s32 idx = g_MpPlayerNum;
    if (idx < 0 || idx >= MAX_PLAYERS) idx = 0;
    return (u32)g_PlayerConfigsArray[idx].medals;
}

/**
 * Challenge outcome at match end.
 * Returns: 0=not a challenge, 1=completed, 2=failed, 3=cheated
 */
s32 pdguiEndscreenGetChallengeStatus(void)
{
    if (g_BossFile.locktype != MPLOCKTYPE_CHALLENGE) return 0;
    if (g_CheatsActiveBank0 || g_CheatsActiveBank1) return 3;
    return challengeIsCompleteForEndscreen() ? 1 : 2;
}

/**
 * Current mission difficulty (DIFF_A=0, DIFF_SA=1, DIFF_PA=2, DIFF_PD=3).
 */
s32 pdguiEndscreenGetDifficulty(void)
{
    return (s32)g_MissionConfig.difficulty;
}

/**
 * Timed-cheat unlock name from the last mission, or NULL if none.
 */
const char *pdguiEndscreenGetCheatTimedName(void)
{
    u32 info = g_Menus[g_MpPlayerNum].endscreen.cheatinfo;
    if ((info & 0x100) && cheatGetTime(info & 0xff) > 0) {
        return cheatGetName(info & 0xff);
    }
    return NULL;
}

/**
 * Completion-cheat unlock name from the last mission, or NULL if none.
 */
const char *pdguiEndscreenGetCheatComplName(void)
{
    u32 info = g_Menus[g_MpPlayerNum].endscreen.cheatinfo;
    if (info & 0x800) {
        return cheatGetName((info >> 16) & 0xff);
    }
    return NULL;
}

/**
 * Restart the current mission (retry). Equivalent to pressing Accept on the
 * retry dialog.
 */
void pdguiEndscreenStartMission(void)
{
    menuhandlerAcceptMission(MENUOP_SET, NULL, NULL);
}

/**
 * Advance to the next mission and start it. Equivalent to pressing Accept
 * on the Next Mission dialog.
 */
void pdguiEndscreenNextMission(void)
{
    endscreenAdvance();
    menuhandlerAcceptMission(MENUOP_SET, NULL, NULL);
}

/**
 * Exit the endscreen back to the main menu by popping all dialogs.
 */
void pdguiEndscreenExitToMainMenu(void)
{
    func0f0f8120();
}

/**
 * Returns 1 if there is a next mission to advance to, 0 if at the last stage.
 */
s32 pdguiEndscreenHasNextMission(void)
{
    return (g_MissionConfig.stageindex + 1 < NUM_SOLOSTAGES) ? 1 : 0;
}

/* ========================================================================
 * Recent server browser bridge functions
 * ======================================================================== */

s32 netRecentServerGetCount(void)
{
    return g_NetNumRecentServers;
}

s32 netRecentServerGetInfo(s32 idx, char *addr, s32 addrSize,
                           u8 *flags, u8 *numclients, u8 *maxclients,
                           u32 *online)
{
    if (idx < 0 || idx >= g_NetNumRecentServers) return 0;

    struct netrecentserver *srv = &g_NetRecentServers[idx];
    if (addr && addrSize > 0) {
        strncpy(addr, srv->addr, addrSize - 1);
        addr[addrSize - 1] = '\0';
    }
    if (flags) *flags = srv->flags;
    if (numclients) *numclients = srv->numclients;
    if (maxclients) *maxclients = srv->maxclients;
    if (online) *online = srv->online ? 1 : 0;
    return 1;
}

/* ========================================================================
 * Lobby command bridge — send CLC_LOBBY_START from C++ lobby UI
 * ======================================================================== */

s32 netLobbyRequestStartWithSims(u8 gamemode, u8 stagenum, u8 difficulty, u8 numSims, u8 simType, u8 timelimit, u32 options, u8 scenario, u8 scorelimit, u16 teamscorelimit, u8 weaponSetIndex)
{
    if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient) {
        return -1;
    }
    if (g_NetLocalClient->state < CLSTATE_LOBBY) {
        return -2;
    }

    /* Write to a fresh out-buffer then send immediately.
     * g_NetLocalClient->out is the per-client reliable send buffer; calling
     * netSend(cl, NULL, reliable, chan) flushes it via enet_peer_send to the
     * server.  Without the explicit netSend the packet sits unsent — the
     * netFlushSendBuffers() path only drains g_NetMsgRel / g_NetMsg. */
    netbufStartWrite(&g_NetLocalClient->out);
    netmsgClcLobbyStartWrite(&g_NetLocalClient->out, gamemode, stagenum, difficulty, numSims, simType, timelimit, options, scenario, scorelimit, teamscorelimit, weaponSetIndex);
    netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL);
    sysLogPrintf(LOG_NOTE, "BRIDGE: sent CLC_LOBBY_START gamemode=%u stage=%u diff=%u sims=%u simtype=%u tl=%u opt=0x%08x scen=%u sc=%u tsc=%u weaponset=%u",
                 gamemode, stagenum, difficulty, numSims, simType, timelimit, (unsigned)options, scenario, scorelimit, (unsigned)teamscorelimit, (unsigned)weaponSetIndex);
    return 0;
}

s32 netLobbyRequestStart(u8 gamemode, u8 stagenum, u8 difficulty)
{
    /* timelimit=60 (unlimited), options=0, no scenario/score limits for non-Combat-Sim modes */
    return netLobbyRequestStartWithSims(gamemode, stagenum, difficulty, 0, 0, 60, 0, 0, 0, 0, 0xFF);
}

/* ========================================================================
 * Match countdown cancel bridge — send CLC_LOBBY_CANCEL from C++ overlay
 * ======================================================================== */

s32 netLobbyRequestCancel(void)
{
    if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient) {
        return -1;
    }
    if (g_NetLocalClient->state != CLSTATE_PREPARING) {
        return -2;
    }

    netbufStartWrite(&g_NetLocalClient->out);
    netmsgClcLobbyCancelWrite(&g_NetLocalClient->out);
    netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL);
    sysLogPrintf(LOG_NOTE, "BRIDGE: sent CLC_LOBBY_CANCEL");
    return 0;
}

/* ========================================================================
 * Countdown state accessors — read-only bridge for C++ overlay
 * ======================================================================== */

/* Returns 1 when the MANIFEST_PHASE_LOADING countdown is active (3-2-1 visible). */
s32 pdguiCountdownIsActive(void)
{
    return (g_MatchCountdownState.active &&
            g_MatchCountdownState.phase == MANIFEST_PHASE_LOADING) ? 1 : 0;
}

/* Returns seconds remaining (3, 2, 1, 0 = GO). Only valid when pdguiCountdownIsActive(). */
s32 pdguiCountdownGetSecs(void)
{
    return (s32)g_MatchCountdownState.countdown_secs;
}

/* Returns 1 if a SVC_MATCH_CANCELLED has been received and not yet cleared. */
s32 pdguiCancelledIsActive(void)
{
    return g_MatchCancelledState.active;
}

/* Returns the name of the player who cancelled (valid when pdguiCancelledIsActive()). */
const char *pdguiCancelledGetName(void)
{
    return g_MatchCancelledState.name;
}

/* Clears the cancel message after the UI has finished displaying it. */
void pdguiCancelledClear(void)
{
    g_MatchCancelledState.active = 0;
    g_MatchCancelledState.name[0] = '\0';
}
