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
#include "game/options.h"
#include "game/training.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "files.h"
#include "modmgr.h"
#include "assetcatalog.h"
#include "modelcatalog.h"
#include "inputctx.h"
#include "config.h"

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
 * Set the MP player config head and body by catalog ID strings.
 * Resolves DEPRECATED integer indices internally for unmigrated consumers.
 */
void mpPlayerConfigSetHeadBody(s32 playernum, const char *head_id, const char *body_id)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return;
    }

    struct mpchrconfig *cfg = &g_PlayerConfigsArray[playernum].base;

    /* PRIMARY: store catalog ID strings directly */
    if (head_id && head_id[0]) {
        strncpy(cfg->head_id, head_id, sizeof(cfg->head_id) - 1);
        cfg->head_id[sizeof(cfg->head_id) - 1] = '\0';
    } else {
        cfg->head_id[0] = '\0';
    }

    if (body_id && body_id[0]) {
        strncpy(cfg->body_id, body_id, sizeof(cfg->body_id) - 1);
        cfg->body_id[sizeof(cfg->body_id) - 1] = '\0';
    } else {
        cfg->body_id[0] = '\0';
    }

    /* DERIVED: resolve integer indices for DEPRECATED fields */
    cfg->mpheadnum = 0;
    cfg->mpbodynum = 0;

    if (head_id && head_id[0]) {
        const asset_entry_t *he = assetCatalogResolve(head_id);
        if (he && he->type == ASSET_HEAD && he->mp_index >= 0) {
            cfg->mpheadnum = (u8)he->mp_index;
        }
    }

    if (body_id && body_id[0]) {
        const asset_entry_t *be = assetCatalogResolve(body_id);
        if (be && be->type == ASSET_BODY && be->mp_index >= 0) {
            cfg->mpbodynum = (u8)be->mp_index;
        }
    }
}

/**
 * DEPRECATED: Get the MP player config head index. Use mpPlayerConfigGetHeadId instead.
 */
u8 mpPlayerConfigGetHead(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return 0;
    }
    return g_PlayerConfigsArray[playernum].base.mpheadnum;
}

/**
 * DEPRECATED: Get the MP player config body index. Use mpPlayerConfigGetBodyId instead.
 */
u8 mpPlayerConfigGetBody(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return 0;
    }
    return g_PlayerConfigsArray[playernum].base.mpbodynum;
}

/**
 * Get the MP player config head catalog ID string.
 */
const char *mpPlayerConfigGetHeadId(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return "";
    }
    return g_PlayerConfigsArray[playernum].base.head_id;
}

/**
 * Get the MP player config body catalog ID string.
 */
const char *mpPlayerConfigGetBodyId(s32 playernum)
{
    if (playernum < 0 || playernum >= ARRAYCOUNT(g_PlayerConfigsArray)) {
        return "";
    }
    return g_PlayerConfigsArray[playernum].base.body_id;
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

/* Phase 5: catalog ID accessors for lobby player identity */
const char *lobbyGetPlayerBodyId(s32 idx)
{
    if (idx < 0 || idx >= g_Lobby.numPlayers) return "";
    struct lobbyplayer *lp = &g_Lobby.players[idx];
    if (!lp->active) return "";
    return lp->body_id[0] ? lp->body_id : "";
}

const char *lobbyGetPlayerHeadId(s32 idx)
{
    if (idx < 0 || idx >= g_Lobby.numPlayers) return "";
    struct lobbyplayer *lp = &g_Lobby.players[idx];
    if (!lp->active) return "";
    return lp->head_id[0] ? lp->head_id : "";
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
            const char *s = langGet(arena->name);
            return s ? s : "???";
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
    if (inputCtxIsActive(&g_CtxImGuiMenu)) {
        inputCtxPopDeferred(&g_CtxImGuiMenu);
    }
}

/**
 * Advance to the next mission and start it. Equivalent to pressing Accept
 * on the Next Mission dialog.
 */
void pdguiEndscreenNextMission(void)
{
    endscreenAdvance();
    menuhandlerAcceptMission(MENUOP_SET, NULL, NULL);
    if (inputCtxIsActive(&g_CtxImGuiMenu)) {
        inputCtxPopDeferred(&g_CtxImGuiMenu);
    }
}

/**
 * Exit the endscreen back to the main menu by popping all dialogs.
 * B-117 fix: pop the ImGuiMenu input context that was pushed on window appear,
 * otherwise a stale context survives the stage transition and causes a crash.
 * M2.2: auto-save player config on match exit (PC has no pak — just write pd.ini).
 */
void pdguiEndscreenExitToMainMenu(void)
{
    configSave("pd.ini");
    if (inputCtxIsActive(&g_CtxImGuiMenu)) {
        inputCtxPopDeferred(&g_CtxImGuiMenu);
    }
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

/* Resolve a catalog stage_id to a stagenum for legacy wire encoding.
 * Accepts both ASSET_ARENA (MP arenas) and ASSET_MAP (co-op/counter-op maps).
 * Returns the stagenum on success, -1 if the catalog entry cannot be found. */
static s32 s_resolveStageIdToStagenum(const char *stage_id)
{
    if (!stage_id || !stage_id[0]) {
        sysLogPrintf(LOG_ERROR, "BRIDGE: null/empty stage_id");
        return -1;
    }
    const asset_entry_t *ae = assetCatalogResolve(stage_id);
    if (!ae) {
        sysLogPrintf(LOG_ERROR, "BRIDGE: stage_id '%s' not found in catalog", stage_id);
        return -1;
    }
    if (ae->type == ASSET_ARENA) return ae->ext.arena.stagenum;
    if (ae->type == ASSET_MAP)   return ae->ext.map.stagenum;
    sysLogPrintf(LOG_ERROR, "BRIDGE: stage_id '%s' is not ASSET_ARENA or ASSET_MAP (type=%d)",
                 stage_id, (int)ae->type);
    return -1;
}

s32 netLobbyRequestStartWithSims(u8 gamemode, const char *stage_id, u8 difficulty, u8 numSims, u8 simType, u8 timelimit, u32 options, u8 scenario, u8 scorelimit, u16 teamscorelimit, u8 weaponSetIndex)
{
    if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient) {
        return -1;
    }
    if (g_NetLocalClient->state < CLSTATE_LOBBY) {
        return -2;
    }

    /* Resolve catalog ID → stagenum for wire encoding (temporary — will be
     * replaced by full catalog ID string wire format in the next protocol bump). */
    const s32 stagenum = s_resolveStageIdToStagenum(stage_id);
    if (stagenum < 0) {
        return -3;
    }

    /* Write to a fresh out-buffer then send immediately.
     * g_NetLocalClient->out is the per-client reliable send buffer; calling
     * netSend(cl, NULL, reliable, chan) flushes it via enet_peer_send to the
     * server.  Without the explicit netSend the packet sits unsent — the
     * netFlushSendBuffers() path only drains g_NetMsgRel / g_NetMsg. */
    netbufStartWrite(&g_NetLocalClient->out);
    netmsgClcLobbyStartWrite(&g_NetLocalClient->out, gamemode, (u8)stagenum, difficulty, numSims, simType, timelimit, options, scenario, scorelimit, teamscorelimit, weaponSetIndex);
    netSend(g_NetLocalClient, NULL, true, NETCHAN_CONTROL);
    sysLogPrintf(LOG_NOTE, "BRIDGE: sent CLC_LOBBY_START gamemode=%u stage='%s'(0x%02x) diff=%u sims=%u simtype=%u tl=%u opt=0x%08x scen=%u sc=%u tsc=%u weaponset=%u",
                 gamemode, stage_id, (unsigned)stagenum, difficulty, numSims, simType, timelimit, (unsigned)options, scenario, scorelimit, (unsigned)teamscorelimit, (unsigned)weaponSetIndex);
    return 0;
}

s32 netLobbyRequestStart(u8 gamemode, const char *stage_id, u8 difficulty)
{
    /* timelimit=60 (unlimited), options=0, no scenario/score limits for non-Combat-Sim modes */
    return netLobbyRequestStartWithSims(gamemode, stage_id, difficulty, 0, 0, 60, 0, 0, 0, 0, 0xFF);
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

/* ========================================================================
 * MP Pause bridge (pdgui_menu_mppause.cpp, Batch 8)
 *
 * Single accessor so the C++ renderer file does not need to know the
 * layout of struct bossfile.  teamnames is indexed 0..MAX_TEAMS-1; any
 * out-of-range index is clamped to team 0.
 * ======================================================================== */

const char *pdguiMppGetTeamName(u32 team)
{
    if (team >= MAX_TEAMS) team = 0;
    return g_BossFile.teamnames[team];
}

/* ========================================================================
 * Player Config bridge (pdgui_menu_playerconfig.cpp, Batch 11)
 *
 * Small accessors over g_Menus[].mpsetup and g_PlayerConfigsArray[].
 *
 * The C++ renderer cannot include types.h (bool redefinition breaks C++),
 * so it cannot directly touch g_Menus.  Two of the Batch 11 dialogs
 * (g_MpLoadSettingsMenuDialog, g_MpLoadPresetMenuDialog) need to ensure
 * mpsetup.showpresets is set so the legacy mpLoadSettingsMenuHandler
 * returns non-zero numpresets — otherwise unlocked preset slots are
 * invisible regardless of dialog.  The ImGui Load Settings view always
 * shows both groups (PC has scroll, N64 collapse is obsolete), and Load
 * Preset needs showpresets=1 so the SET branch hits the preset-path.
 * ======================================================================== */

void pdguiPcMpSetShowPresets(s32 on)
{
    if (g_MpPlayerNum < 0 || g_MpPlayerNum >= MAX_PLAYERS) {
        return;
    }
    g_Menus[g_MpPlayerNum].mpsetup.showpresets = on ? 1 : 0;
}

s32 pdguiPcMpGetShowPresets(void)
{
    if (g_MpPlayerNum < 0 || g_MpPlayerNum >= MAX_PLAYERS) {
        return 0;
    }
    return g_Menus[g_MpPlayerNum].mpsetup.showpresets;
}

/* Accessor for the selected player config's MPPLAYERTITLE_* value —
 * used by Player Stats dialog to decide whether to show the legacy
 * USERNAME/PASSWORD Easter egg rows (Perfect Dark / Perfect Agent tier). */
s32 pdguiPcPlayerConfigGetTitle(void)
{
    if (g_MpPlayerNum < 0 || g_MpPlayerNum >= MAX_PLAYERS) {
        return 0;
    }
    return g_PlayerConfigsArray[g_MpPlayerNum].title;
}

/* Accessor for individual medal counts — the legacy medal rows in the
 * stats dialog render a colored icon via mpMedalMenuHandler MENUOP_RENDER
 * (GBI path).  The ImGui version draws its own colored circle + the count
 * from the dynamic-text helper, which matches the N64 visual intent. */
s32 pdguiPcPlayerConfigGetMedalCount(s32 which)
{
    if (g_MpPlayerNum < 0 || g_MpPlayerNum >= MAX_PLAYERS) {
        return 0;
    }
    switch (which) {
    case 0: return g_PlayerConfigsArray[g_MpPlayerNum].killmastermedals;
    case 1: return g_PlayerConfigsArray[g_MpPlayerNum].headshotmedals;
    case 2: return g_PlayerConfigsArray[g_MpPlayerNum].accuracymedals;
    case 3: return g_PlayerConfigsArray[g_MpPlayerNum].survivormedals;
    default: return 0;
    }
}

/* ========================================================================
 * MP Settings bridge (pdgui_menu_mpsettings.cpp, Batch 12 Music & Misc)
 *
 * Team name accessors for the inline Team Names editor.  These mirror the
 * read/write semantics of the legacy `mpTeamNameMenuHandler::MENUOP_GETTEXT`
 * and `MENUOP_SETTEXT` branches in `src/game/mplayer/setup.c:4589`:
 *
 *   - Storage layout: `g_BossFile.teamnames[MAX_TEAMS][12]`.  Each slot is
 *     up to 11 printable chars followed by a `'\n'` terminator, with any
 *     remaining bytes zero-filled.
 *   - Write: clamp to 11 chars, place `'\n'` at the first free position,
 *     zero the rest, set `MODFILE_MPSETUP` dirty flag so the boss file
 *     persists on save.
 *
 * The C++ renderer cannot include types.h (bool redefinition breaks C++),
 * so it goes through these accessors instead of touching g_BossFile
 * directly.  Same pattern as `pdguiMppGetTeamName` (Batch 8) which reads
 * the same field from the pause team-rankings dialog.
 * ======================================================================== */

void pdguiMpsTeamNameGet(u32 team, char *out, u32 outlen)
{
    u32 i;
    if (!out || outlen == 0) {
        return;
    }
    out[0] = '\0';
    if (team >= MAX_TEAMS) {
        team = 0;
    }
    /* Copy until '\n' or '\0' or the 11-char source cap.  Reserve one byte
     * of outlen for the trailing '\0'. */
    for (i = 0; i < 11 && (i + 1) < outlen; i++) {
        char c = g_BossFile.teamnames[team][i];
        if (c == '\n' || c == '\0') {
            break;
        }
        out[i] = c;
    }
    out[i] = '\0';
}

void pdguiMpsTeamNameSet(u32 team, const char *text)
{
    s32 i;
    if (!text) {
        return;
    }
    if (team >= MAX_TEAMS) {
        return;
    }
    /* Mirror mpTeamNameMenuHandler::MENUOP_SETTEXT exactly:
     *   - copy up to 11 chars from `text` stopping at first '\0'
     *   - place '\n' at the first free position
     *   - zero-fill the rest
     *   - mark MP setup modified so it persists on next save */
    i = 0;
    while (i < 11 && text[i] != '\0') {
        g_BossFile.teamnames[team][i] = text[i];
        i++;
    }
    g_BossFile.teamnames[team][i] = '\n';
    i++;
    while (i < 11) {
        g_BossFile.teamnames[team][i] = '\0';
        i++;
    }
    /* The legacy storage is [12] wide — ensure the final byte is clean. */
    g_BossFile.teamnames[team][11] = '\0';
    g_Vars.modifiedfiles |= MODFILE_MPSETUP;
}

/* ========================================================================
 * Training bridge (pdgui_menu_training.cpp, Batch 10 Training NULL-FN)
 *
 * Batch 10 ImGui replacements for the 10 Training/Hangar NULL-FN dialogs:
 *
 *   g_FrWeaponListMenuDialog            (needs weapon slot/name/score tier/filenum)
 *   g_BioListMenuDialog                 (needs grouped chrbio/miscbio list)
 *   g_BioProfileMenuDialog              (needs 3D char preview + name/age/race/desc)
 *   g_DtListMenuDialog                  (needs device list)
 *   g_DtDetailsMenuDialog               (needs 3D weapon preview + desc)
 *   g_HtDetailsMenuDialog               (needs 3D weapon preview + desc)
 *   g_HangarListMenuDialog              (needs grouped location/vehicle list)
 *   g_HangarVehicleHolographMenuDialog  (needs 3D vehicle preview)
 *   g_HangarVehicleDetailsMenuDialog    (needs hangar bio text)
 *   g_HangarLocationDetailsMenuDialog   (needs hangar bio text)
 *
 * The C++ renderer cannot include types.h (bool redefinition breaks C++),
 * so all `struct chrbio` / `struct miscbio` / `struct hangarbio` field reads
 * and global slot updates go through these accessors.  The actual training
 * data providers (`trainingmenus.c`, `trainingmisc.c`) are UNCHANGED; this
 * file only wraps their existing public API surface (`game/training.h`) in
 * small C-linkage helpers the renderer can call without including types.h.
 *
 * Zero function loss: every accessor forwards to a legacy function; there
 * are no alternate-path implementations of the underlying predicates.
 * ======================================================================== */

/* ---- Firing Range ------------------------------------------------------ */

s32 pdguiTrFrNumWeaponsAvailable(void)
{
    return frGetNumWeaponsAvailable();
}

u32 pdguiTrFrWeaponBySlot(s32 slot)
{
    return frGetWeaponBySlot(slot);
}

const char *pdguiTrFrWeaponName(u32 weaponnum)
{
    return bgunGetName((s32)weaponnum);
}

s32 pdguiTrFrWeaponScoreTier(u32 weaponnum)
{
    return ciGetFiringRangeScore(frGetWeaponIndexByWeapon(weaponnum));
}

u32 pdguiTrFrWeaponFilenum(u32 weaponnum)
{
    return (u32)weaponGetFileNum((s32)weaponnum);
}

s32 pdguiTrFrGetSlot(void)
{
    return frGetSlot();
}

void pdguiTrFrSetSlot(s32 slot)
{
    frSetSlot(slot);
}

const char *pdguiTrFrWeaponDescription(void)
{
    /* Returns DESCRIPTION_FRWEAPON body (the weapon-info scrollable).  Legacy
     * path resolves it against the CURRENT slot via frGetSlot(), so callers
     * typically set the slot first via pdguiTrFrSetSlot(). */
    return frGetWeaponDescription();
}

s32 pdguiTrFrIsInTraining(void)
{
    return frIsInTraining();
}

/* ---- Bios (characters + misc) ------------------------------------------ */

s32 pdguiTrBioNumChr(void)
{
    return ciGetNumUnlockedChrBios();
}

s32 pdguiTrBioNumMisc(void)
{
    return ciGetNumUnlockedMiscBios();
}

const char *pdguiTrBioChrName(s32 slot)
{
    struct chrbio *bio = ciGetChrBioByBodynum((u32)ciGetChrBioBodynumBySlot(slot));
    if (!bio) return "";
    return langGet((s32)bio->name);
}

const char *pdguiTrBioMiscName(s32 slot)
{
    struct miscbio *bio = ciGetMiscBio(ciGetMiscBioIndexBySlot(slot));
    if (!bio) return "";
    return langGet((s32)bio->name);
}

s32 pdguiTrBioGetSlot(void)
{
    return (s32)g_ChrBioSlot;
}

void pdguiTrBioSetSlot(s32 slot)
{
    if (slot < 0) slot = 0;
    g_ChrBioSlot = (u8)slot;
}

const char *pdguiTrBioChrAge(void)
{
    struct chrbio *bio =
        ciGetChrBioByBodynum((u32)ciGetChrBioBodynumBySlot((s32)g_ChrBioSlot));
    if (!bio) return "";
    return langGet((s32)bio->age);
}

const char *pdguiTrBioChrRace(void)
{
    struct chrbio *bio =
        ciGetChrBioByBodynum((u32)ciGetChrBioBodynumBySlot((s32)g_ChrBioSlot));
    if (!bio) return "";
    return langGet((s32)bio->race);
}

const char *pdguiTrBioChrDescription(void)
{
    /* Legacy handler reads from g_ChrBioSlot via ciGetChrBioDescription(). */
    return ciGetChrBioDescription();
}

const char *pdguiTrBioMiscDescription(void)
{
    return ciGetMiscBioDescription();
}

/* For the character preview: the chrbio is keyed by bodynum (legacy constant,
 * not mp index).  pdguiModelPreviewDraw() wants catalog ID strings.  This
 * helper mirrors ciCharacterProfileMenuDialog's resolution: bodynum ->
 * mpbodynum via mpGetMpbodynumByBodynum(), then catalog_id via catalogMpBodyId()
 * for the body, and the default paired head via catalogGetBodyDefaultMpHeadIdx()
 * + catalogMpHeadId(). */
void pdguiTrBioGetCurrentChrCatalogIds(const char **head_id_out,
                                        const char **body_id_out)
{
    if (head_id_out) *head_id_out = NULL;
    if (body_id_out) *body_id_out = NULL;

    s32 bodynum = ciGetChrBioBodynumBySlot((s32)g_ChrBioSlot);
    if (bodynum < 0) {
        return;
    }

    s32 mpbodynum = mpGetMpbodynumByBodynum((u16)bodynum);
    if (mpbodynum < 0) {
        return;
    }

    if (body_id_out) {
        *body_id_out = catalogMpBodyId(mpbodynum);
    }

    s32 mpheadnum = catalogGetBodyDefaultMpHeadIdx(mpbodynum);
    if (mpheadnum < 0) {
        mpheadnum = 0;
    }
    if (head_id_out) {
        *head_id_out = catalogMpHeadId(mpheadnum);
    }
}

/* ---- Device Training --------------------------------------------------- */

s32 pdguiTrDtNumAvailable(void)
{
    return dtGetNumAvailable();
}

u32 pdguiTrDtWeaponBySlot(s32 slot)
{
    return dtGetWeaponByDeviceIndex(dtGetIndexBySlot(slot));
}

const char *pdguiTrDtDeviceName(s32 slot)
{
    return bgunGetName((s32)dtGetWeaponByDeviceIndex(dtGetIndexBySlot(slot)));
}

s32 pdguiTrDtGetSlot(void)
{
    return (s32)g_DtSlot;
}

void pdguiTrDtSetSlot(s32 slot)
{
    if (slot < 0) slot = 0;
    g_DtSlot = (u8)slot;
}

const char *pdguiTrDtCurrentDescription(void)
{
    return dtGetDescription();
}

u32 pdguiTrDtCurrentWeaponFilenum(void)
{
    u32 weaponnum = dtGetWeaponByDeviceIndex(dtGetIndexBySlot((s32)g_DtSlot));
    return (u32)weaponGetFileNum((s32)weaponnum);
}

s32 pdguiTrDtIsInTraining(void)
{
    struct trainingdata *data = dtGetData();
    return data ? data->intraining : 0;
}

/* ---- Holo Training ----------------------------------------------------- */

s32 pdguiTrHtGetSlot(void)
{
    return (s32)var80088bb4;
}

void pdguiTrHtSetSlot(s32 slot)
{
    if (slot < 0) slot = 0;
    var80088bb4 = (u8)slot;
}

const char *pdguiTrHtCurrentDescription(void)
{
    return htGetDescription();
}

s32 pdguiTrHtIsInTraining(void)
{
    struct trainingdata *data = getHoloTrainingData();
    return data ? data->intraining : 0;
}

/* HT details uses the same "current-selection drives the preview model" rule
 * as DT.  The legacy MENUOP_OPEN handler calls func0f1a2198() which sets up
 * the menumodel for the current HT slot.  Mirror its internals so the bridge
 * can hand the caller a filenum without requiring the legacy OPEN path.
 *
 * Reading the legacy asset path: htGetIndexBySlot(slot) -> a holo-training
 * index; func0f1a25c0(index) -> a weapon num for that hologram (i.e. the
 * weapon the training simulates).  We resolve through the same public chain
 * and take weaponGetFileNum on the resulting weaponnum. */
u32 pdguiTrHtCurrentWeaponFilenum(void)
{
    s32 index = htGetIndexBySlot((s32)var80088bb4);
    if (index < 0) {
        return 0;
    }
    u32 weaponnum = func0f1a25c0(index);
    if (weaponnum == 0) {
        return 0;
    }
    return (u32)weaponGetFileNum((s32)weaponnum);
}

/* ---- Hangar ------------------------------------------------------------ */

s32 pdguiTrHangarNumTotal(void)
{
    return ciGetNumUnlockedHangarBios();
}

s32 pdguiTrHangarNumLocations(void)
{
    return ciGetNumUnlockedLocationBios();
}

s32 pdguiTrHangarGetSlot(void)
{
    return (s32)g_HangarBioSlot;
}

void pdguiTrHangarSetSlot(s32 slot)
{
    if (slot < 0) slot = 0;
    g_HangarBioSlot = (u8)slot;
}

s32 pdguiTrHangarSlotIsLocation(s32 slot)
{
    s32 bioindex = ciGetHangarBioIndexBySlot(slot);
    /* HANGARBIO_SKEDARRUINS (13) is the last location index, matching the
     * legacy ciHangarInformationMenuHandler boundary check. */
    return bioindex <= 13;
}

const char *pdguiTrHangarSlotName(s32 slot)
{
    struct hangarbio *bio = ciGetHangarBio(ciGetHangarBioIndexBySlot(slot));
    if (!bio) return "";
    return langGet((s32)bio->name);
}

/* The current hangar name/subheading/description functions all key off
 * g_HangarBioSlot, so set the slot first via pdguiTrHangarSetSlot() before
 * reading these. */
const char *pdguiTrHangarCurrentFullName(void)
{
    struct hangarbio *bio =
        ciGetHangarBio(ciGetHangarBioIndexBySlot((s32)g_HangarBioSlot));
    if (!bio) return "";
    return langGet((s32)bio->name);
}

/* The subheading lives in the same string as the name, separated by a pipe:
 * "Lucerne Tower\0|Global headquarters\n".  Mirror the legacy parser in
 * ciMenuTextHangarBioSubheading() -- find the '|' and return the rest. */
const char *pdguiTrHangarCurrentSubheading(void)
{
    struct hangarbio *bio =
        ciGetHangarBio(ciGetHangarBioIndexBySlot((s32)g_HangarBioSlot));
    if (!bio) return "";
    const char *name = langGet((s32)bio->name);
    if (!name) return "";
    const char *p = name;
    while (*p && *p != '|') p++;
    if (*p == '|') return p + 1;
    return "";
}

const char *pdguiTrHangarCurrentDescription(void)
{
    return ciGetHangarBioDescription();
}

/* For the vehicle holograph: mirror the legacy biovehicleitem[] table in
 * ciHangarHolographMenuDialog (trainingmenus.c:2657).  The legacy table is
 * indexed by (hangar bio index - NUM_BIO_LOCATIONS) and stores the file id
 * that drives MENUMODELPARAMS_SET_FILENUM for the holograph render.  Kept in
 * sync with the legacy table so no behavior drift. */
u32 pdguiTrHangarCurrentVehicleFilenum(void)
{
    const u32 items[] = {
        FILE_PDROPSHIP,        /* dropship */
        FILE_PHOVERCRATE1,     /* hovercrate */
        FILE_PHOVBIKE,         /* hoverbike */
        FILE_PHOOVERBOT,       /* hoverbot */
        FILE_PDD_HOVERCOPTER,  /* hovercopter */
        FILE_CCHICROB,         /* chicken robot */
        FILE_PA51INTERCEPTOR,  /* A51 interceptor */
        FILE_PELVIS_SAUCER,    /* flying saucer */
        FILE_PSK_SHUTTLE,      /* skedar shuttle */
    };
    s32 bioindex = ciGetHangarBioIndexBySlot((s32)g_HangarBioSlot);
    /* Locations occupy indices 0..13; vehicles start at 14.  Indices beyond
     * the items[] length are defensively clamped to zero so the preview
     * gracefully falls back to the placeholder silhouette. */
    s32 veh = bioindex - 14;
    if (veh < 0 || veh >= (s32)(sizeof(items) / sizeof(items[0]))) {
        return 0;
    }
    return items[veh];
}

/* ========================================================================
 * Control diagram bridge (pdgui_menu_controldiagram.cpp, Batch 10)
 *
 * Small wrappers over optionsGetControlMode() / optionsSetControlMode() and
 * the PC ext-controls flag.  Same pattern as the Aim Control dropdown in
 * mpsettings — the C++ renderer should not reach into g_PlayerExtCfg[].
 * ======================================================================== */

s32 pdguiCdGetControlMode(s32 mpindex)
{
    if (mpindex < 0) mpindex = 0;
    return optionsGetControlMode(mpindex);
}

void pdguiCdSetControlMode(s32 mpindex, s32 mode)
{
    if (mpindex < 0) mpindex = 0;
    optionsSetControlMode(mpindex, mode);
    g_PlayerExtCfg[mpindex & 3].extcontrols = (mode == CONTROLMODE_PC);
    g_Vars.modifiedfiles |= MODFILE_GAME;
}

/* ---- MP Control Options (Batch 10: g_MpControlMenuDialog) -------------- *
 * The legacy menuhandlerMpControlCheckbox reads/writes single bits on
 * g_PlayerConfigsArray[g_MpPlayerNum].options.  OPTION_* constants are the
 * bit masks.  We mirror that directly to preserve behavior including the
 * OPTION_FORWARDPITCH inversion quirk (its GET inverts the stored bit). */

s32 pdguiCdGetPlayerOption(s32 option)
{
    if (g_MpPlayerNum < 0 || g_MpPlayerNum >= MAX_PLAYERS) {
        return 0;
    }
    /* Legacy OPTION_FORWARDPITCH inversion: stored bit = "use raw pitch",
     * displayed as "reverse pitch" (inverted). */
    if (option == OPTION_FORWARDPITCH) {
        return ((g_PlayerConfigsArray[g_MpPlayerNum].options & (u32)option) == 0) ? 1 : 0;
    }
    return ((g_PlayerConfigsArray[g_MpPlayerNum].options & (u32)option) != 0) ? 1 : 0;
}

void pdguiCdSetPlayerOption(s32 option, s32 on)
{
    if (g_MpPlayerNum < 0 || g_MpPlayerNum >= MAX_PLAYERS) {
        return;
    }
    s32 effective = on;
    /* FORWARDPITCH inverted the same way as the legacy SET branch — clearing
     * the UI checkbox stores the bit; setting it clears the bit. */
    if (option == OPTION_FORWARDPITCH) {
        effective = on ? 0 : 1;
    }
    g_PlayerConfigsArray[g_MpPlayerNum].options &= ~(u32)option;
    if (effective) {
        g_PlayerConfigsArray[g_MpPlayerNum].options |= (u32)option;
    }
    g_Vars.modifiedfiles |= MODFILE_GAME;
}

s32 pdguiCdGetAimControl(void)
{
    if (g_MpPlayerNum < 0 || g_MpPlayerNum >= MAX_PLAYERS) {
        return 0;
    }
    return optionsGetAimControl(g_MpPlayerNum);
}

void pdguiCdSetAimControl(s32 mode)
{
    if (g_MpPlayerNum < 0 || g_MpPlayerNum >= MAX_PLAYERS) {
        return;
    }
    optionsSetAimControl(g_MpPlayerNum, mode);
    g_Vars.modifiedfiles |= MODFILE_GAME;
}

