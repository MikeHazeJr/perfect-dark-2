#include <ultra64.h>
#include <string.h>
#include "constants.h"
#include "system.h"
#include "game/chraction.h"
#include "game/dlights.h"
#include "game/game_006900.h"
#include "game/setup.h"
#include "game/prop.h"
#include "game/propsnd.h"
#include "game/atan2f.h"
#include "game/bondgun.h"
#include "game/player.h"
#include "game/savebuffer.h"
#include "game/hudmsg.h"
#include "game/menu.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/game_1531a0.h"
#include "game/mplayer/setup.h"
#include "game/mplayer/scenarios.h"
#include "game/mplayer/participant.h"   /* B-12 Phase 3: mpIsParticipantActive() used in .inc files */
#include "game/radar.h"
#include "game/botcmd.h"
#include "game/botinv.h"
#include "game/bot.h"             /* gamemode-graphs.md slice 7: the simulants' mode commands */
#include "game/challenge.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/pad.h"
#include "game/propobj.h"
#include "game/options.h"
#include "bss.h"
#include "lib/vi.h"
#include "lib/snd.h"
#include "lib/memp.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "lib/anim.h"
#include "lib/collision.h"
#include "data.h"
#include "types.h"
#include "assetcatalog.h"   /* catalog universality sweep (2026-04-27) */
#include "asset_runtime.h"  /* c3849 Wave 6a: gamemode meta-family consumer */
#include "scenario_pad_fallback.h"  /* B-1125, B-1126: placement pads for arenas that carry too few */
#include "net/net.h"                /* g_NetMode: who decides scenario state */
#include "net/netmsg.h"             /* B-1209: announcing a scenario prop made in the match */
#include "net/net_scenario_state_wire.h"
#include "mode_events.h"           /* gamemode-graphs.md slice 1: record-only mode events */
#include "mode_rules.h"            /* gamemode-graphs.md slice 3: the dispatchers run the program's plan */

_Static_assert(MPSCENARIO_HOLDTHEBRIEFCASE == NET_SCENARIO_STATE_HOLDTHEBRIEFCASE
		&& MPSCENARIO_HACKERCENTRAL == NET_SCENARIO_STATE_HACKERCENTRAL
		&& MPSCENARIO_POPACAP == NET_SCENARIO_STATE_POPACAP
		&& MPSCENARIO_KINGOFTHEHILL == NET_SCENARIO_STATE_KINGOFTHEHILL
		&& MPSCENARIO_CAPTURETHECASE == NET_SCENARIO_STATE_CAPTURETHECASE,
	"scenario state wire ids must equal the MPSCENARIO_* values");
_Static_assert(MAX_TEAMS == NET_SCENARIO_STATE_MAX_TEAMS,
	"scenario state wire team range must equal MAX_TEAMS");
_Static_assert(MAX_MPCHRS == NET_SCENARIO_STATE_MAX_CHRS,
	"scenario state wire character slots must equal MAX_MPCHRS");
_Static_assert(sizeof(((struct scenariodata_htm *)0)->terminals) / sizeof(((struct scenariodata_htm *)0)->terminals[0])
		== NET_SCENARIO_HTM_MAX_TERMINALS,
	"scenario state wire terminal range must equal scenariodata_htm.terminals");
_Static_assert(sizeof(((struct scenariodata_ctc *)0)->spawnpadsperteam) / sizeof(((struct scenariodata_ctc *)0)->spawnpadsperteam[0])
		== NET_SCENARIO_CTC_BASES
		&& sizeof(((struct scenariodata_ctc *)0)->tokens) / sizeof(((struct scenariodata_ctc *)0)->tokens[0])
		== NET_SCENARIO_CTC_BASES
		&& sizeof(((struct ctcspawnpadsperteam *)0)->spawnpads) / sizeof(((struct ctcspawnpadsperteam *)0)->spawnpads[0])
		== NET_SCENARIO_CTC_SPAWNPADS,
	"scenario state wire Capture the Case shape must equal scenariodata_ctc");

/**
 * There are six multiplayer scenarios:
 *
 * - Combat
 * - Hold the Briefcase (HTB)
 * - Capture the Case (CTC)
 * - Hack that Mac (HTM) - labelled as Hacker Central
 * - King of the Hill (KOH)
 * - Pop a Cap (PAC)
 *
 * Each scenario registers callback functions for certain events. For example,
 * code elsewhere in the game may call scenarioTick, then scenarioTick checks
 * if the current scenario has a tick callback defined (eg. htbTick). If so,
 * that scenario's callback is run.
 */

struct mpscenario {
	struct menudialogdef *optionsdialog;
	void (*initfunc)(void);
	s32 (*numpropsfunc)(void);
	void (*initpropsfunc)(void);
	void (*tickfunc)(void);
	void (*tickchrfunc)(struct chrdata *chr);
	Gfx *(*hudfunc)(Gfx *gdl);
	void (*calcscorefunc)(struct mpchrconfig *mpchr, s32 chrnum, s32 *score, s32 *deaths);
	Gfx *(*radarextrafunc)(Gfx *gdl);
	bool (*radarchrfunc)(Gfx **gdl, struct prop *prop);
	bool (*highlightpropfunc)(struct prop *prop, s32 *colour);
	bool (*spawnfunc)(f32 arg0, struct coord *pos, RoomNum *rooms, struct prop *prop, f32 *arg4);
	s32 (*maxteamsfunc)(void);
	bool (*isroomhighlightedfunc)(RoomNum room);
	void (*highlightroomfunc)(RoomNum room, s32 *arg1, s32 *arg2, s32 *arg3);
	void *unk3c; // never hooked into nor fired
	void (*readsavefunc)(struct savebuffer *buffer, u8 version);
	void (*writesavefunc)(struct savebuffer *buffer);
	/* gamemode-graphs.md slice 4: the mode code that sat outside the table. */
	void (*resetfunc)(void);
	void (*intropadfunc)(s32 *cmd);
	s32 (*pickuptokenfunc)(struct chrdata *chr, struct prop *prop);
	void (*droppedtokenfunc)(struct chrdata *chr, struct prop *prop);
	void (*activatedpropfunc)(struct chrdata *chr, struct prop *prop);
	void (*playerdiedfunc)(s32 aplayernum, s32 vplayernum);
	/* gamemode-graphs.md slice 7: the simulants' mode commands (bot.c). */
	void (*botcommandfunc)(struct chrdata *chr, s32 teamsize);
	s32 (*botidlefunc)(struct chrdata *chr);
};

#if VERSION >= VERSION_JPN_FINAL
char *scenarioRemoveLineBreaks(char *src, s32 stringnum)
{
	static char strings[2][30];
	char *out = strings[stringnum];
	s32 i;

	for (; *src != '\0'; src++) {
		if (*src != '\n') {
			*out = *src;
			out++;
		}
	}

	*out = '\0';

	return strings[stringnum];
}
#endif

struct scenariodata g_ScenarioData;

MenuItemHandlerResult menuhandlerMpDisplayTeam(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_CHECKDISABLED) {
		if (g_MpSetup.options & MPOPTION_TEAMSENABLED) {
			return false;
		}

		return true;
	}

	return menuhandlerMpCheckboxOption(operation, item, data);
}

MenuItemHandlerResult menuhandlerMpOneHitKills(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_CHECKDISABLED || operation == MENUOP_CHECKHIDDEN) {
		if (challengeIsFeatureUnlocked(MPFEATURE_ONEHITKILLS)) {
			return false;
		}

		return true;
	}

	return menuhandlerMpCheckboxOption(operation, item, data);
}

MenuItemHandlerResult menuhandlerMpSlowMotion(s32 operation, struct menuitem *item, union handlerdata *data)
{
	u16 labels[] = {
		L_MPMENU_240, // "Off"
		L_MPMENU_241, // "On"
		L_MPMENU_242, // "Smart"
	};

	switch (operation) {
	case MENUOP_CHECKDISABLED:
	case MENUOP_CHECKHIDDEN:
		if (challengeIsFeatureUnlocked(MPFEATURE_SLOWMOTION)) {
			return false;
		}
		return true;
	case MENUOP_GETOPTIONCOUNT:
		data->dropdown.value = 3;
		break;
	case MENUOP_GETOPTIONTEXT:
		return (uintptr_t)langGet(labels[data->dropdown.value]);
	case MENUOP_SET:
		g_MpSetup.options &= ~(MPOPTION_SLOWMOTION_ON | MPOPTION_SLOWMOTION_SMART);

		if (data->dropdown.value == SLOWMOTION_ON) {
			g_MpSetup.options |= MPOPTION_SLOWMOTION_ON;
		} else if (data->dropdown.value == SLOWMOTION_SMART) {
			g_MpSetup.options |= MPOPTION_SLOWMOTION_SMART;
		}
		break;
	case MENUOP_GETSELECTEDINDEX:
		if (g_MpSetup.options & MPOPTION_SLOWMOTION_SMART) {
			data->dropdown.value = SLOWMOTION_SMART;
		} else if (g_MpSetup.options & MPOPTION_SLOWMOTION_ON) {
			data->dropdown.value = SLOWMOTION_ON;
		} else {
			data->dropdown.value = SLOWMOTION_OFF;
		}
		break;
	}

	return 0;
}

/**
 * PC (B-1125): whether a pad can carry a scenario object, and where it is.
 *
 * setupCreateObject only places an object on a pad that is in a room
 * (room > 0), and scenario banks treat pad numbers <= 0 as empty. A pad on a
 * lift is refused too: the object would ride the lift.
 */
static s32 scenarioPadCanHoldProp(s32 padnum, struct scenariopadpick *pick)
{
	struct pad pad;

	if (padnum <= 0) {
		return false;
	}

	padUnpack(padnum, PADFIELD_POS | PADFIELD_ROOM | PADFIELD_FLAGS | PADFIELD_LIFT, &pad);

	if (pad.room <= 0 || pad.liftnum != 0 || (pad.flags & (PADFLAG_AIWAITLIFT | PADFLAG_AIONLIFT))) {
		return false;
	}

	pick->padnum = padnum;
	pick->x = pad.pos.x;
	pick->z = pad.pos.z;
	pick->y = pad.pos.y;
	pick->room = pad.room;

	return true;
}

/**
 * PC (B-1126): whether a pad the arena itself names (an intro CASE, CASERESPAWN
 * or HILL pad) can carry a scenario object: a real pad in a room, the
 * condition under which setupCreateObject places an object. Unlike the banks,
 * pad 0 counts, and an authored pad on a lift is left to the arena.
 */
static s32 scenarioPadIsInRoom(s32 padnum)
{
	struct pad pad;

	if (padnum < 0 || g_PadsFile == NULL || padnum >= g_PadsFile->numpads) {
		return false;
	}

	padUnpack(padnum, PADFIELD_ROOM, &pad);

	return pad.room > 0;
}

/* Candidate storage shared by the fallback helpers below (setup runs on one
 * thread, and each helper finishes with it before returning). */
static struct scenariopadpick s_ScenarioFallbackCands[SCENARIO_PAD_FALLBACK_MAX_CANDIDATES];

/**
 * PC (B-1125, B-1126): walkable pads that can carry a scenario object: the
 * arena's navigation waypoints (the source the spawn fallback in playerReset
 * uses) or, when it has none, every pad in a room. Fills
 * s_ScenarioFallbackCands and returns the count; *source names the source.
 */
static s32 scenarioCollectFallbackCandidates(const char **source)
{
	struct scenariopadpick *cands = s_ScenarioFallbackCands;
	s32 maxcands = (s32)ARRAYCOUNT(s_ScenarioFallbackCands);
	s32 numcands = 0;
	s32 i;

	*source = "none";

	if (g_StageSetup.waypoints) {
		struct waypoint *wpts = g_StageSetup.waypoints;

		for (i = 0; wpts[i].padnum >= 0 && numcands < maxcands; i++) {
			if (scenarioPadCanHoldProp(wpts[i].padnum, &cands[numcands])) {
				numcands++;
			}
		}

		*source = "waypoint";
	}

	if (numcands == 0 && g_PadsFile) {
		for (i = 1; i < g_PadsFile->numpads && numcands < maxcands; i++) {
			if (scenarioPadCanHoldProp(i, &cands[numcands])) {
				numcands++;
			}
		}

		*source = "pad";
	}

	return numcands;
}

/**
 * PC (B-1126): loads the pads a scenario already has into picks, so new picks
 * keep their distance from them. Pads below minpad are skipped (the banks use
 * 0 and -1 for "empty"; an authored base or hill pad 0 is real).
 */
static s32 scenarioLoadExistingPicks(struct scenariopadpick *picks, s32 maxpicks,
		const s16 *existing, s32 numexisting, s32 minpad)
{
	s32 count = 0;
	s32 i;

	for (i = 0; existing && i < numexisting && count < maxpicks; i++) {
		struct pad pad;

		if (existing[i] < minpad) {
			continue;
		}

		padUnpack(existing[i], PADFIELD_POS | PADFIELD_ROOM, &pad);
		picks[count].padnum = existing[i];
		picks[count].x = pad.pos.x;
		picks[count].z = pad.pos.z;
		picks[count].y = pad.pos.y;
		picks[count].room = pad.room;
		count++;
	}

	return count;
}

/**
 * PC (B-1125): walkable pads for scenario objects on an arena that carries
 * too few placement pads of its own.
 *
 * Hacker Central takes its terminal and data uplink pads from the arena's
 * Capture the Case pads (intro CASE and CASERESPAWN commands) and any
 * multiplayer ammo crates. Arenas extracted today carry neither, and mod
 * arenas or solo maps used as arenas may not either.
 *
 * The candidates are the arena's navigation waypoints, the same source the
 * spawn fallback in playerReset uses, or every pad in a room when the arena
 * has no waypoints. existing[0..numexisting) are the pads the scenario
 * already has: they stay, and the new pads are spaced from them (widest
 * spacing first, see scenario_pad_fallback.h). Picks use rngRandom, so peers
 * that set up the same stage with the same seed pick the same pads.
 *
 * Writes up to maxout new, distinct pads to outpads and returns the number
 * written.
 */
static s32 scenarioFallbackPads(s16 *outpads, s32 maxout, const s16 *existing, s32 numexisting, const char *who)
{
	struct scenariopadpick chosen[64];
	const char *source;
	s32 numcands;
	s32 count;
	s32 total;
	s32 i;

	if (outpads == NULL || maxout <= 0) {
		return 0;
	}

	// Pads the scenario already has keep their places; new picks avoid them
	count = scenarioLoadExistingPicks(chosen, (s32)ARRAYCOUNT(chosen), existing, numexisting, 1);

	if (maxout > (s32)ARRAYCOUNT(chosen) - count) {
		maxout = (s32)ARRAYCOUNT(chosen) - count;
	}

	numcands = scenarioCollectFallbackCandidates(&source);
	total = scenarioPadPickSpaced(chosen, count, count + maxout, count + maxout, s_ScenarioFallbackCands, numcands, rngRandom);

	for (i = count; i < total; i++) {
		outpads[i - count] = chosen[i].padnum;
	}

	sysLogPrintf(total - count > 0 ? LOG_NOTE : LOG_WARNING,
		"SCENARIO.PADS: %s fallback added %d of %d pad(s) from %d %s candidate(s), existing=%d",
		who ? who : "?", total - count, maxout, numcands, source, count);

	return total - count;
}

/**
 * PC (B-1126): walkable pads as far as possible from each other and from
 * existing[0..numexisting) (Capture the Case bases). With no existing pad the
 * first pick is random (rngRandom), so bases vary between matches; later picks
 * take the candidate farthest from every pad chosen so far. Writes up to maxout
 * new, distinct pads to outpads and returns the number written.
 */
static s32 scenarioFallbackFarthestPads(s16 *outpads, s32 maxout, const s16 *existing, s32 numexisting, const char *who)
{
	struct scenariopadpick chosen[16];
	const char *source;
	s32 numcands;
	s32 count;
	s32 total;
	s32 i;

	if (outpads == NULL || maxout <= 0) {
		return 0;
	}

	count = scenarioLoadExistingPicks(chosen, (s32)ARRAYCOUNT(chosen), existing, numexisting, 0);

	if (maxout > (s32)ARRAYCOUNT(chosen) - count) {
		maxout = (s32)ARRAYCOUNT(chosen) - count;
	}

	numcands = scenarioCollectFallbackCandidates(&source);
	total = scenarioPadPickFarthest(chosen, count, count + maxout, count + maxout, s_ScenarioFallbackCands, numcands, rngRandom);

	for (i = count; i < total; i++) {
		outpads[i - count] = chosen[i].padnum;
	}

	sysLogPrintf(total - count > 0 ? LOG_NOTE : LOG_WARNING,
		"SCENARIO.PADS: %s farthest fallback added %d of %d pad(s) from %d %s candidate(s), existing=%d",
		who ? who : "?", total - count, maxout, numcands, source, count);

	return total - count;
}

/**
 * PC (B-1126): the walkable pads nearest to anchorpads[owner] that are nearer
 * to it than to any other anchor (Capture the Case respawn pads around a base;
 * see scenarioPadPickNearest). Writes up to maxout pads to outpads, nearest
 * first, and returns the number written. Makes no rngRandom call.
 */
static s32 scenarioFallbackNearestPads(s16 *outpads, s32 maxout, s32 owner,
		const s16 *anchorpads, s32 numanchors, const char *who)
{
	struct scenariopadpick anchors[8];
	const char *source;
	s32 numcands;
	s32 count;

	if (outpads == NULL || maxout <= 0 || owner < 0 || owner >= numanchors
			|| numanchors > (s32)ARRAYCOUNT(anchors)) {
		return 0;
	}

	if (scenarioLoadExistingPicks(anchors, (s32)ARRAYCOUNT(anchors), anchorpads, numanchors, 0) != numanchors) {
		return 0;
	}

	numcands = scenarioCollectFallbackCandidates(&source);
	count = scenarioPadPickNearest(outpads, maxout, owner, anchors, numanchors, s_ScenarioFallbackCands, numcands);

	sysLogPrintf(count > 0 ? LOG_NOTE : LOG_WARNING,
		"SCENARIO.PADS: %s nearest fallback added %d of %d pad(s) around pad %d from %d %s candidate(s), anchors=%d",
		who ? who : "?", count, maxout, anchorpads[owner], numcands, source, numanchors);

	return count;
}

/**
 * PC (B-1126): walkable pads in distinct rooms, spaced apart, in rooms that
 * none of existing[0..numexisting) is in (King of the Hill hills: the hill is
 * the room of its pad). Rooms crossed by two or more candidates are used when
 * there are enough of them for maxout; otherwise every room is. Each room
 * contributes the candidate nearest its middle. Writes up to maxout new pads to
 * outpads and returns the number written.
 */
static s32 scenarioFallbackRoomPads(s16 *outpads, s32 maxout, const s16 *existing, s32 numexisting, const char *who)
{
	static struct scenariopadpick rooms[SCENARIO_PAD_FALLBACK_MAX_CANDIDATES];
	struct scenariopadpick chosen[16];
	const char *source;
	s32 minperroom = 2;
	s32 numcands;
	s32 numrooms;
	s32 count;
	s32 total;
	s32 i;
	s32 j;

	if (outpads == NULL || maxout <= 0) {
		return 0;
	}

	count = scenarioLoadExistingPicks(chosen, (s32)ARRAYCOUNT(chosen), existing, numexisting, 0);

	if (maxout > (s32)ARRAYCOUNT(chosen) - count) {
		maxout = (s32)ARRAYCOUNT(chosen) - count;
	}

	numcands = scenarioCollectFallbackCandidates(&source);

	// Rooms that already hold one of the scenario's pads are taken
	for (i = 0, j = 0; i < numcands; i++) {
		s32 k;

		for (k = 0; k < count; k++) {
			if (chosen[k].room == s_ScenarioFallbackCands[i].room) {
				break;
			}
		}

		if (k == count) {
			s_ScenarioFallbackCands[j++] = s_ScenarioFallbackCands[i];
		}
	}

	numcands = j;

	memcpy(rooms, s_ScenarioFallbackCands, (size_t)numcands * sizeof(rooms[0]));
	numrooms = scenarioPadReduceToRooms(rooms, numcands, minperroom);

	if (numrooms < maxout) {
		minperroom = 1;
		memcpy(rooms, s_ScenarioFallbackCands, (size_t)numcands * sizeof(rooms[0]));
		numrooms = scenarioPadReduceToRooms(rooms, numcands, minperroom);
	}

	total = scenarioPadPickSpaced(chosen, count, count + maxout, count + maxout, rooms, numrooms, rngRandom);

	for (i = count; i < total; i++) {
		outpads[i - count] = chosen[i].padnum;
	}

	sysLogPrintf(total - count > 0 ? LOG_NOTE : LOG_WARNING,
		"SCENARIO.PADS: %s room fallback added %d of %d pad(s) from %d room(s) (%d+ candidate(s) each, %d %s candidate(s)), existing=%d",
		who ? who : "?", total - count, maxout, numrooms, minperroom, numcands, source, count);

	return total - count;
}

/*
 * PC (T-NETWORKING-011): host-authoritative scenario state, shared part. The
 * scenario files capture and present their own bodies; see scenarios.h.
 */
static net_scenario_state_list_t s_ScenarioNetLast;    // host: previous capture
static s32 s_ScenarioNetHaveLast;
static u32 s_ScenarioNetRevision;                 // host: revision of the newest change
static net_scenario_state_list_t s_ScenarioNetPending; // client: newest state from the host
static s32 s_ScenarioNetHavePending;
static u32 s_ScenarioNetPendingStates; /* the sections not yet taken, one bit per state */
static u32 s_ScenarioNetShownRevision;            // client: revision already presented
static s32 s_ScenarioNetHaveShown;

s32 scenarioNetIsAuthority(void)
{
	return g_NetMode != NETMODE_CLIENT;
}

/* B-1209 (SP-85): a scenario prop the host makes during the match (Hacker
 * Central's uplink after its carrier died, Hold the Briefcase's case when none
 * is left) has a dynamic sync id no client knows, and a client never makes its
 * own. Announce it as a simulant's dropped weapon is announced (botinvDrop),
 * or no client sees it and every host pickup of it is skipped. */
void scenarioNetAnnounceProp(struct prop *prop, const char *what)
{
	u32 rc;

	if (g_NetMode != NETMODE_SERVER || prop == NULL) {
		return;
	}

	rc = netmsgSvcPropSpawnWrite(&g_NetMsgRel, prop);

	if (rc == 0) {
		rc = netmsgSvcPropMoveWrite(&g_NetMsgRel, prop, NULL);
	}

	sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_WARNING,
		"SCENARIO.NET: host announced the new %s syncid=%u pos=(%.0f,%.0f,%.0f) rc=%u",
		what, (unsigned)prop->syncid, prop->pos.x, prop->pos.y, prop->pos.z, (unsigned)rc);
}

/* B-1272 (SP-93): a scenario's token is a weapon with no MP weapon slot, so
 * the match manifest's weapon pool leaves it out. Without a session catalog id
 * every message that names it (a carrier's drop, B-1209's replacement, a
 * simulant's held weapon, reconnect inventory) reaches a client as unarmed. The
 * host's lobby start adds the weapon named here. */
s32 scenarioTokenWeaponNum(s32 scenario)
{
	switch (scenario) {
	case MPSCENARIO_HACKERCENTRAL:
		return WEAPON_DATAUPLINK;
	case MPSCENARIO_HOLDTHEBRIEFCASE:
	case MPSCENARIO_CAPTURETHECASE:
		return WEAPON_BRIEFCASE2;
	default:
		return -1;
	}
}

/* B-1273 (SP-93): the model of the scenario's token prop, as its template in
 * hackthatmac.inc, holdthebriefcase.inc and capturethecase.inc gives it. The
 * uplink's is not the one playermgrGetModelOfWeapon names for the weapon. */
s32 scenarioTokenModelNum(s32 scenario)
{
	switch (scenario) {
	case MPSCENARIO_HACKERCENTRAL:
		return MODEL_CHRDATATHIEF;
	case MPSCENARIO_HOLDTHEBRIEFCASE:
	case MPSCENARIO_CAPTURETHECASE:
		return MODEL_CHRBRIEFCASE;
	default:
		return -1;
	}
}

/* Every match starts from revision 0 on both sides. */
static void scenarioNetResetState(void)
{
	memset(&s_ScenarioNetLast, 0, sizeof(s_ScenarioNetLast));
	memset(&s_ScenarioNetPending, 0, sizeof(s_ScenarioNetPending));
	s_ScenarioNetHaveLast = false;
	s_ScenarioNetRevision = 0;
	s_ScenarioNetHavePending = false;
	s_ScenarioNetPendingStates = 0;
	s_ScenarioNetShownRevision = 0;
	s_ScenarioNetHaveShown = false;
}

/* Client, scenario tick: takes the newest state the host sent, once. */
/* gamemode-graphs.md slice 5: each mode presents its own section of the
 * host's list; the list is shown once every section of it has been taken. */
static s32 scenarioNetTakePending(u8 state, net_scenario_state_t *out)
{
	s32 i;

	if (!s_ScenarioNetHavePending || state >= 32 || !(s_ScenarioNetPendingStates & (1u << state))) {
		return false;
	}

	for (i = 0; i < s_ScenarioNetPending.count; i++) {
		if (s_ScenarioNetPending.sections[i].scenario == state) {
			*out = s_ScenarioNetPending.sections[i];
			break;
		}
	}

	if (i >= s_ScenarioNetPending.count) {
		return false;
	}

	s_ScenarioNetPendingStates &= ~(1u << state);
	s_ScenarioNetShownRevision = out->revision;
	s_ScenarioNetHaveShown = true;

	if (s_ScenarioNetPendingStates == 0) {
		s_ScenarioNetHavePending = false;
	}

	return true;
}

// Include the code files where each scenario implements its callbacks
#include "scenarios/combat.inc"
#include "scenarios/holdthebriefcase.inc"
#include "scenarios/capturethecase.inc"
#include "scenarios/kingofthehill.inc"
#include "scenarios/hackthatmac.inc"
#include "scenarios/popacap.inc"

/* gamemode-graphs.md slice 4: what scenarioReset, the stage intro, the token
 * pickups and drops and the terminal did per mode, defined further down. */
static void kohReset(void);
static void ctcReset(void);
static void htbIntroPad(s32 *cmd);
static void htmIntroPad(s32 *cmd);
static void ctcIntroPad(s32 *cmd);
static void kohIntroPad(s32 *cmd);
static s32 htbPickUpToken(struct chrdata *chr, struct prop *prop);
static s32 ctcPickUpToken(struct chrdata *chr, struct prop *prop);
static s32 htmPickUpToken(struct chrdata *chr, struct prop *prop);
static void ctcDropToken(struct chrdata *chr, struct prop *prop);
static void htmActivateProp(struct chrdata *chr, struct prop *prop);

// Define the scenario callbacks. gamemode-graphs.md: each callback is a mode
// module (og.mode.<mode>.<seam>, port/src/mode_rules.c) and the mode's
// program chooses which of them run; pd-tests hold the two tables equal.
struct mpscenario g_MpScenarios[] = {
	[MPSCENARIO_COMBAT] = {
		.optionsdialog = &g_MpCombatOptionsMenuDialog,
	},
	[MPSCENARIO_HOLDTHEBRIEFCASE] = {
		.optionsdialog = &g_HtbOptionsMenuDialog,
		.initfunc = htbInit,
		.numpropsfunc = htbNumProps,
		.initpropsfunc = htbInitProps,
		.tickfunc = htbTick,
		.tickchrfunc = htbTickChr,
		.hudfunc = htbRenderHud,
		.calcscorefunc = htbCalculatePlayerScore,
		.radarextrafunc = htbRadarExtra,
		.radarchrfunc = htbRadarChr,
		.highlightpropfunc = htbHighlightProp,
		.resetfunc = htbReset,
		.intropadfunc = htbIntroPad,
		.pickuptokenfunc = htbPickUpToken,
		.botcommandfunc = htbBotChooseCommand,
		.botidlefunc = htbBotIdleAction,
	},
	[MPSCENARIO_HACKERCENTRAL] = {
		.optionsdialog = &g_HtmOptionsMenuDialog,
		.initfunc = htmInit,
		.numpropsfunc = htmNumProps,
		.initpropsfunc = htmInitProps,
		.tickfunc = htmTick,
		.tickchrfunc = htmTickChr,
		.hudfunc = htmRenderHud,
		.calcscorefunc = htmCalculatePlayerScore,
		.radarextrafunc = htmRadarExtra,
		.radarchrfunc = htmRadarChr,
		.highlightpropfunc = htmHighlightProp,
		.resetfunc = htmReset,
		.intropadfunc = htmIntroPad,
		.pickuptokenfunc = htmPickUpToken,
		.activatedpropfunc = htmActivateProp,
		.botcommandfunc = htmBotChooseCommand,
		.botidlefunc = htmBotIdleAction,
	},
	[MPSCENARIO_POPACAP] = {
		.optionsdialog = &g_PacOptionsMenuDialog,
		.initfunc = pacInit,
		.initpropsfunc = pacInitProps,
		.tickfunc = pacTick,
		.hudfunc = pacRenderHud,
		.calcscorefunc = pacCalculatePlayerScore,
		.radarextrafunc = pacRadarExtra,
		.radarchrfunc = pacRadarChr,
		.highlightpropfunc = pacHighlightProp,
		.playerdiedfunc = pacHandleDeath,
		.botcommandfunc = pacBotChooseCommand,
		.botidlefunc = pacBotIdleAction,
	},
	[MPSCENARIO_KINGOFTHEHILL] = {
		.optionsdialog = &g_KohOptionsMenuDialog,
		.initfunc = kohInit,
		.initpropsfunc = kohInitProps,
		.tickfunc = kohTick,
		.hudfunc = kohRenderHud,
		.calcscorefunc = kohCalculatePlayerScore,
		.radarextrafunc = kohRadarExtra,
		.isroomhighlightedfunc = kohIsRoomHighlighted,
		.highlightroomfunc = kohHighlightRoom,
		.readsavefunc = kohReadSave,
		.writesavefunc = kohWriteSave,
		.resetfunc = kohReset,
		.intropadfunc = kohIntroPad,
		.botcommandfunc = kohBotChooseCommand,
	},
	[MPSCENARIO_CAPTURETHECASE] = {
		.optionsdialog = &g_CtcOptionsMenuDialog,
		.initfunc = ctcInit,
		.numpropsfunc = ctcNumProps,
		.initpropsfunc = ctcInitProps,
		.tickfunc = ctcTick,
		.tickchrfunc = ctcTickChr,
		.calcscorefunc = ctcCalculatePlayerScore,
		.radarextrafunc = ctcRadarExtra,
		.radarchrfunc = ctcRadarChr,
		.highlightpropfunc = ctcHighlightProp,
		.spawnfunc = ctcChooseSpawnLocation,
		.maxteamsfunc = ctcGetMaxTeams,
		.isroomhighlightedfunc = ctcIsRoomHighlighted,
		.highlightroomfunc = ctcHighlightRoom,
		.resetfunc = ctcReset,
		.intropadfunc = ctcIntroPad,
		.pickuptokenfunc = ctcPickUpToken,
		.droppedtokenfunc = ctcDropToken,
		.botcommandfunc = ctcBotChooseCommand,
		.botidlefunc = ctcBotIdleAction,
	},
};

struct mpscenariooverview g_MpScenarioOverviews[] = {
	// name, short name, require feature, team only
	{ L_MPMENU_246, L_MPMENU_253, 0,                      false }, // "Combat", "Combat"
	{ L_MPMENU_247, L_MPMENU_254, MPFEATURE_SCENARIO_HTB, false }, // "Hold the Briefcase", "Briefcase"
	{ L_MPMENU_248, L_MPMENU_255, MPFEATURE_SCENARIO_HTM, false }, // "Hacker Central", "Hacker"
	{ L_MPMENU_249, L_MPMENU_256, MPFEATURE_SCENARIO_PAC, false }, // "Pop a Cap", "Pop"
	{ L_MPMENU_250, L_MPMENU_257, MPFEATURE_SCENARIO_KOH, true  }, // "King of the Hill", "Hill"
	{ L_MPMENU_251, L_MPMENU_258, MPFEATURE_SCENARIO_CTC, true  }, // "Capture the Case", "Capture"
};

static bool scenarioIndexIsValid(s32 scenario)
{
	return scenario >= 0 && scenario < (s32)ARRAYCOUNT(g_MpScenarios);
}

static s32 scenarioGetSafeIndex(const char *caller)
{
	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		sysLogPrintf(LOG_WARNING,
			"SCENARIO.MISS: %s invalid scenario=%d; using Combat",
			caller ? caller : "(unknown)", g_MpSetup.scenario);
		return MPSCENARIO_COMBAT;
	}

	return g_MpSetup.scenario;
}

/*
 * gamemode-graphs.md slice 3: the table is the program's. A mode's plan
 * (mode_rules.h) is what its mode archive's program chose for each seam; the
 * dispatchers below run the g_MpScenarios[] callback of each owner the plan
 * names, in its order, and their own default code where it names the default
 * module. A mode program's choice reads no game state, so the plan is made
 * when the mode is chosen (scenarioInit) and at each stage reset, and kept in
 * between. A mode whose program is not registered runs nothing: its stage
 * reset fails loudly (LOUDFAIL.MODE.PLAN) and every seam call differs.
 *
 * The parity probe: every dispatcher call compares the plan's seam with what
 * the original table runs, read from g_MpScenarios[] itself (the mode's
 * callback when it has one, the default score without one, the general spawn
 * chooser after the mode's own), and counts it in MODE.PARITY; the first
 * difference of each seam logs both answers.
 */
static mode_rules_plan_t s_ScenarioPlans[MODE_RULES_MODE_COUNT];
static u8 s_ScenarioPlanMade[MODE_RULES_MODE_COUNT];
static u32 s_ScenarioParityLogged[MODE_RULES_MODE_COUNT];

static void scenarioPlanMake(s32 scenario, const char *why)
{
	const char *id;
	const mission_graph_program_t *program;

	if (!scenarioIndexIsValid(scenario) || scenario >= MODE_RULES_MODE_COUNT) {
		return;
	}
	id = catalogGameModeIdByScenarioIndex(scenario);
	program = id ? assetRuntimeModeProgram(id) : NULL;
	s_ScenarioPlanMade[scenario] = 1;
	s_ScenarioParityLogged[scenario] = 0;
	if (program && modeRulesPlanFromProgram(program, &s_ScenarioPlans[scenario])) {
		sysLogPrintf(LOG_NOTE, "MODE.PLAN: %s mode=%s scenario=%d modules=%d",
			why, id, scenario, s_ScenarioPlans[scenario].modules);
		return;
	}
	modeRulesPlanClear(&s_ScenarioPlans[scenario]);
	if (strcmp(why, "stage_reset") == 0) {
		sysLoudFailf("MODE.PLAN", "mode %d ('%s') has no registered program; its seams run nothing",
			scenario, id ? id : "-");
	} else {
		sysLogPrintf(LOG_WARNING, "MODE.PLAN: %s mode %d ('%s') has no registered program yet",
			why, scenario, id ? id : "-");
	}
}

static const mode_rules_plan_t *scenarioPlan(s32 scenario)
{
	if (!scenarioIndexIsValid(scenario) || scenario >= MODE_RULES_MODE_COUNT) {
		return NULL;
	}
	if (!s_ScenarioPlanMade[scenario]) {
		scenarioPlanMake(scenario, "first_use");
	}
	return &s_ScenarioPlans[scenario];
}

/* The k-th owner the plan runs at the seam, or MODE_PLAN_NONE. */
static s32 scenarioPlanOwner(const mode_rules_plan_t *plan, s32 seam, s32 k)
{
	if (!plan || seam < 0 || seam >= MODE_SEAM_COUNT || k < 0 || k >= MODE_PLAN_MAX_PER_SEAM) {
		return MODE_PLAN_NONE;
	}
	return plan->owner[seam][k];
}

/* What g_MpScenarios[] runs at the seam for the mode, as a plan's seam. */
static void scenarioNativeSeam(s32 scenario, s32 seam, s8 out[MODE_PLAN_MAX_PER_SEAM])
{
	const struct mpscenario *row = &g_MpScenarios[scenario];
	bool has = false;
	s32 n = 0;

	switch (seam) {
	case MODE_SEAM_INIT: has = row->initfunc != NULL; break;
	case MODE_SEAM_NUM_PROPS: has = row->numpropsfunc != NULL; break;
	case MODE_SEAM_INIT_PROPS: has = row->initpropsfunc != NULL; break;
	case MODE_SEAM_TICK: has = row->tickfunc != NULL; break;
	case MODE_SEAM_TICK_CHR: has = row->tickchrfunc != NULL; break;
	case MODE_SEAM_HUD: has = row->hudfunc != NULL; break;
	case MODE_SEAM_SCORE: has = row->calcscorefunc != NULL; break;
	case MODE_SEAM_RADAR_EXTRA: has = row->radarextrafunc != NULL; break;
	case MODE_SEAM_RADAR_CHR: has = row->radarchrfunc != NULL; break;
	case MODE_SEAM_HIGHLIGHT_PROP: has = row->highlightpropfunc != NULL; break;
	case MODE_SEAM_SPAWN: has = row->spawnfunc != NULL; break;
	case MODE_SEAM_MAX_TEAMS: has = row->maxteamsfunc != NULL; break;
	case MODE_SEAM_ROOM_LIGHTS:
		has = row->isroomhighlightedfunc != NULL || row->highlightroomfunc != NULL;
		break;
	case MODE_SEAM_SAVE: has = row->readsavefunc != NULL || row->writesavefunc != NULL; break;
	case MODE_SEAM_STAGE_RESET: has = row->resetfunc != NULL; break;
	case MODE_SEAM_INTRO_PAD: has = row->intropadfunc != NULL; break;
	case MODE_SEAM_TOKEN_PICKED_UP: has = row->pickuptokenfunc != NULL; break;
	case MODE_SEAM_TOKEN_DROPPED: has = row->droppedtokenfunc != NULL; break;
	case MODE_SEAM_PROP_ACTIVATED: has = row->activatedpropfunc != NULL; break;
	case MODE_SEAM_PLAYER_DIED: has = row->playerdiedfunc != NULL; break;
	case MODE_SEAM_BOT_COMMAND: has = row->botcommandfunc != NULL; break;
	case MODE_SEAM_BOT_IDLE: has = row->botidlefunc != NULL; break;
	}
	out[0] = MODE_PLAN_NONE;
	out[1] = MODE_PLAN_NONE;
	if (has) {
		out[n++] = (s8)scenario;
	}
	if ((seam == MODE_SEAM_SCORE && !has) || seam == MODE_SEAM_SPAWN) {
		out[n++] = MODE_RULES_DEFAULT;
	}
}

static void scenarioPlanCompare(s32 scenario, const mode_rules_plan_t *plan, s32 seam)
{
	s8 native[MODE_PLAN_MAX_PER_SEAM];
	s32 differ;

	if (!plan || seam < 0 || seam >= MODE_SEAM_COUNT) {
		return;
	}
	scenarioNativeSeam(scenario, seam, native);
	differ = plan->owner[seam][0] != native[0] || plan->owner[seam][1] != native[1];
	modeParityRecord(differ);
	if (differ && !(s_ScenarioParityLogged[scenario] & (1u << seam))) {
		s_ScenarioParityLogged[scenario] |= 1u << seam;
		sysLogPrintf(LOG_WARNING, "MODE.PARITY: differ scenario=%d seam=%s native=%d,%d program=%d,%d",
			scenario, modeRulesSeamName(seam), native[0], native[1],
			plan->owner[seam][0], plan->owner[seam][1]);
	}
}

/**
 * While the options dialog is open, check if another player has changed the
 * scenario to a different one. If so, replace this dialog with the new one.
 */
MenuDialogHandlerResult mpOptionsMenuDialog(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data)
{
	if (operation == MENUOP_TICK) {
		s32 scenario = scenarioGetSafeIndex("mpOptionsMenuDialog");
		if (g_Menus[g_MpPlayerNum].curdialog->definition != g_MpScenarios[scenario].optionsdialog) {
			s32 i;
			s32 end = ARRAYCOUNT(g_MpScenarios);

			for (i = 0; i < end; i++) {
				if (g_Menus[g_MpPlayerNum].curdialog->definition == g_MpScenarios[i].optionsdialog) {
					break;
				}
			}

			if (i < end) {
				menuPopDialog();
				menuPushDialog(g_MpScenarios[scenario].optionsdialog);
			}
		}
	}

	return 0;
}

char *mpMenuTextScenarioShortName(struct menuitem *item)
{
	s32 scenario = scenarioGetSafeIndex("mpMenuTextScenarioShortName");
	const char *asset_id = catalogGameModeIdByScenarioIndex(scenario);
	const asset_runtime_binding_t *binding = asset_id
		? assetRuntimeFindByTypeAndId(ASSET_GAMEMODE, asset_id)
		: NULL;
	if (!binding || !binding->gamemode_name[0]) {
		sysFatalError("ASSET.CHAIN: selected gamemode %d has no public name "
			"binding; refusing native overview fallback.", scenario);
		return "";
	}
	snprintf(g_StringPointer, 300, "%s\n", binding->gamemode_name);
	return g_StringPointer;
}

char *mpMenuTextScenarioName(struct menuitem *item)
{
	s32 scenario = scenarioGetSafeIndex("mpMenuTextScenarioName");
	const char *asset_id = catalogGameModeIdByScenarioIndex(scenario);
	const asset_runtime_binding_t *binding = asset_id
		? assetRuntimeFindByTypeAndId(ASSET_GAMEMODE, asset_id)
		: NULL;
	if (!binding || !binding->gamemode_name[0]) {
		sysFatalError("ASSET.CHAIN: selected gamemode %d has no public name "
			"binding; refusing native overview fallback.", scenario);
		return "";
	}
	snprintf(g_StringPointer, 300, "%s\n", binding->gamemode_name);
	return g_StringPointer;
}

static char *mpMenuTextScenarioDescription(struct menuitem *item)
{
	s32 scenario = scenarioGetSafeIndex("mpMenuTextScenarioDescription");
	const char *asset_id = catalogGameModeIdByScenarioIndex(scenario);
	const asset_runtime_binding_t *binding = asset_id
		? assetRuntimeFindByTypeAndId(ASSET_GAMEMODE, asset_id)
		: NULL;
	if (!binding || !binding->gamemode_description[0]) {
		sysFatalError("ASSET.CHAIN: selected gamemode %d has no public "
			"description binding.", scenario);
		return "";
	}
	snprintf(g_StringPointer, 300, "%s", binding->gamemode_description);
	return g_StringPointer;
}

struct scenariogroup {
	s32 startindex;
	u16 textid;
};

/*
 * Catalog universality sweep (2026-04-27): scenario picker iterates
 * ASSET_GAMEMODE entries via the catalog, filtered by unlock state and
 * the team-only flag (which lives on ext.gamemode.team_based).  Layer A
 * `g_MpScenarioOverviews[]` remains the data home; only the SELECTOR
 * pool migrates per Mike's directive
 * "selector pool = catalog INTERSECT unlock-state".
 *
 * Catalog mp_index for scenarios mirrors mode_id (MPSCENARIO_*), which
 * matches the original `g_MpScenarioOverviews[]` ordering.  Iteration
 * order is therefore stable with the legacy ordering.
 */
struct scenario_pick_ctx {
	bool teamgame;       /* user's "team game" filter from item->param */
	s32  needle_idx;     /* "give me the N-th unlocked teamgame-eligible entry" */
	s32  cur;            /* running count */
	s32  max_mp_index;   /* upper bound for GETGROUPSTARTINDEX */
	s32  scenario_match; /* MPSCENARIO_* result for SET / GETOPTIONTEXT */
	char option_text[64]; /* public gamemode name for GETOPTIONTEXT */
};

static const asset_runtime_binding_t *scenarioBindingForEntry(
	const asset_entry_t *e)
{
	const asset_runtime_binding_t *binding =
		assetRuntimeFindByTypeAndId(ASSET_GAMEMODE, e->id);
	if (!binding || !binding->source_hydrated) {
		sysFatalError("ASSET.CHAIN: gamemode '%s' has no active public-source "
			"rules binding; refusing descriptor/catalog mirror fallback.", e->id);
		return NULL;
	}
	if (!assetRuntimePrimaryFileAccessible(binding)) {
		sysFatalError("ASSET.CHAIN: gamemode '%s' rules source '%s' is "
			"unreadable; refusing native rules fallback.",
			e->id, binding->primary_path);
		return NULL;
	}
	return binding;
}

/* B-1292: a mod mode whose activation was refused (its rules.json, or since
 * gamemode-graphs slice 2 its mode.graph.json, did not hydrate) keeps an
 * enabled catalog entry, so the selectors would meet it and the binding check
 * above would stop the game. They skip it instead, logging it once; a base
 * mode without its binding still stops the game, as the ASSET.CHAIN policy
 * wants. */
static bool scenarioEntryActivationRefused(const asset_entry_t *e)
{
	static char s_Logged[8][CATALOG_ID_LEN];
	static s32 s_NumLogged = 0;
	const asset_runtime_binding_t *binding;
	s32 i;

	if (strncmp(e->id, "base:", 5) == 0) {
		return false;
	}
	binding = assetRuntimeFindByTypeAndId(ASSET_GAMEMODE, e->id);
	if (binding && binding->source_hydrated && assetRuntimePrimaryFileAccessible(binding)) {
		return false;
	}
	for (i = 0; i < s_NumLogged; i++) {
		if (strcmp(s_Logged[i], e->id) == 0) {
			return true;
		}
	}
	if (s_NumLogged < (s32)ARRAYCOUNT(s_Logged)) {
		strncpy(s_Logged[s_NumLogged], e->id, CATALOG_ID_LEN - 1);
		s_Logged[s_NumLogged][CATALOG_ID_LEN - 1] = '\0';
		s_NumLogged++;
	}
	sysLogPrintf(LOG_WARNING,
		"MODE.PICKER: skipping gamemode '%s': its activation was refused (B-1292)", e->id);
	return true;
}

static bool scenarioCtxAccepts(const asset_entry_t *e, const struct scenario_pick_ctx *ctx)
{
	const asset_runtime_binding_t *binding;
	s32 participant_count = mpGetActiveParticipantCount();

	if (scenarioEntryActivationRefused(e)) {
		return false;
	}
	binding = scenarioBindingForEntry(e);
	if (!binding || !scenarioIndexIsValid(binding->runtime_id)) {
		return false;
	}

	if (!ctx->teamgame && binding->gamemode_team_based) {
		return false;
	}
	if (participant_count < binding->gamemode_min_players
			|| participant_count > binding->gamemode_max_players) {
		return false;
	}
	return true;
}

static void scenarioCountCb(const asset_entry_t *e, void *userdata)
{
	struct scenario_pick_ctx *ctx = (struct scenario_pick_ctx *)userdata;
	if (e->type != ASSET_GAMEMODE) return;
	if (!scenarioCtxAccepts(e, ctx)) return;
	ctx->cur++;
}

static void scenarioPickByIndexCb(const asset_entry_t *e, void *userdata)
{
	struct scenario_pick_ctx *ctx = (struct scenario_pick_ctx *)userdata;
	if (e->type != ASSET_GAMEMODE) return;
	if (!scenarioCtxAccepts(e, ctx)) return;
	if (ctx->scenario_match >= 0) return;
	if (ctx->cur == ctx->needle_idx) {
		const asset_runtime_binding_t *binding = scenarioBindingForEntry(e);
		if (!binding) return;
		ctx->scenario_match = binding->runtime_id;
		snprintf(ctx->option_text, sizeof(ctx->option_text), "%s",
			binding->gamemode_name);
	}
	ctx->cur++;
}

static void scenarioFindByModeIdCb(const asset_entry_t *e, void *userdata)
{
	struct scenario_pick_ctx *ctx = (struct scenario_pick_ctx *)userdata;
	if (e->type != ASSET_GAMEMODE) return;
	if (!scenarioCtxAccepts(e, ctx)) return;
	if (ctx->scenario_match >= 0) return;
	const asset_runtime_binding_t *binding = scenarioBindingForEntry(e);
	if (!binding) return;
	if (binding->runtime_id == ctx->needle_idx) {
		ctx->scenario_match = binding->runtime_id;
		return; /* mark found; subsequent calls return early */
	}
	ctx->cur++;
}

static void scenarioGroupStartCb(const asset_entry_t *e, void *userdata)
{
	struct scenario_pick_ctx *ctx = (struct scenario_pick_ctx *)userdata;
	if (e->type != ASSET_GAMEMODE) return;
	if (!scenarioCtxAccepts(e, ctx)) return;
	const asset_runtime_binding_t *binding = scenarioBindingForEntry(e);
	if (binding && binding->runtime_id < ctx->max_mp_index) {
		ctx->cur++;
	}
}

MenuItemHandlerResult scenarioScenarioMenuHandler(s32 operation, struct menuitem *item, union handlerdata *data)
{
	struct scenariogroup groups[] = {
		{ 0, L_MPMENU_244 }, // "Free for All!"
		{ 4, L_MPMENU_245 }, // "-Teamwork-"
	};

	struct scenario_pick_ctx ctx;
	ctx.teamgame = true;
	ctx.needle_idx = 0;
	ctx.cur = 0;
	ctx.max_mp_index = 0;
	ctx.scenario_match = -1;
	ctx.option_text[0] = '\0';

	if (item->param) {
		if (g_Vars.mpquickteam == MPQUICKTEAM_PLAYERSONLY || g_Vars.mpquickteam == MPQUICKTEAM_PLAYERSANDSIMS) {
			ctx.teamgame = false;
		}
	}

	switch (operation) {
	case MENUOP_GETOPTIONCOUNT:
		assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioCountCb, &ctx);
		data->list.value = ctx.cur;
		break;
	case MENUOP_GETOPTIONTEXT:
		ctx.needle_idx = data->list.value;
		assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioPickByIndexCb, &ctx);
		if (ctx.scenario_match >= 0) {
			snprintf(g_StringPointer, 300, "%s", ctx.option_text);
			return (uintptr_t)g_StringPointer;
		}
		break;
	case MENUOP_SET:
		ctx.needle_idx = data->list.value;
		assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioPickByIndexCb, &ctx);
		if (ctx.scenario_match >= 0) {
			g_MpSetup.scenario = ctx.scenario_match;
		}
		scenarioInit();
		break;
	case MENUOP_GETSELECTEDINDEX:
		/* needle == current scenario; cur counts unlocked & teamgame-eligible
		 * entries with mode_id < needle (catalog iterates in mode_id order). */
		ctx.needle_idx = g_MpSetup.scenario;
		ctx.cur = 0;
		ctx.scenario_match = -1;
		assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioFindByModeIdCb, &ctx);
		data->list.value = ctx.cur;
		break;
	case MENUOP_GETOPTGROUPCOUNT:
		data->list.value = 2;
		if (!ctx.teamgame || (!challengeIsFeatureUnlocked(MPFEATURE_SCENARIO_KOH) && !challengeIsFeatureUnlocked(MPFEATURE_SCENARIO_CTC))) {
			data->list.value--;
		}
		break;
	case MENUOP_GETOPTGROUPTEXT:
		return (uintptr_t)langGet(groups[data->list.value].textid);
	case MENUOP_GETGROUPSTARTINDEX:
		ctx.max_mp_index = groups[data->list.value].startindex;
		assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioGroupStartCb, &ctx);
		data->list.groupstartindex = ctx.cur;
		break;
	}

	return 0;
}

MenuItemHandlerResult menuhandlerMpOpenOptions(s32 operation, struct menuitem *item, union handlerdata *data)
{
	if (operation == MENUOP_SET) {
		s32 scenario = scenarioGetSafeIndex("menuhandlerMpOpenOptions");
		menuPushDialog(g_MpScenarios[scenario].optionsdialog);
	}

	return 0;
}

/**
 * Allow a callback to read data from the setup file's save file buffer.
 *
 * There is one byte available to be read. If not handled, the byte will be
 * consumed and ignored.
 *
 * Used by KOH to read the mphilltime.
 */
void scenarioReadSave(struct savebuffer *buffer, u8 version)
{
	s32 scenario = scenarioGetSafeIndex("scenarioReadSave");
	const mode_rules_plan_t *plan = scenarioPlan(scenario);
	s32 k;

	scenarioPlanCompare(scenario, plan, MODE_SEAM_SAVE);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_SAVE, k);
		if (owner >= 0 && g_MpScenarios[owner].readsavefunc) {
			g_MpScenarios[owner].readsavefunc(buffer, version);
			return;
		}
	}

	{
		u8 sz = version > 0 ? 32 : 8;
		savebufferReadBits(buffer, sz);
	}
}

/**
 * Allow a callback to write data to the setup file's save file buffer.
 *
 * There is one byte available to be written. If not handled, the byte will be
 * written as 0.
 *
 * Used by KOH to write the mphilltime.
 */
void scenarioWriteSave(struct savebuffer *buffer)
{
	s32 scenario = scenarioGetSafeIndex("scenarioWriteSave");
	const mode_rules_plan_t *plan = scenarioPlan(scenario);
	s32 k;

	scenarioPlanCompare(scenario, plan, MODE_SEAM_SAVE);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_SAVE, k);
		if (owner >= 0 && g_MpScenarios[owner].writesavefunc) {
			g_MpScenarios[owner].writesavefunc(buffer);
			return;
		}
	}

	savebufferOr(buffer, 0, 32);
}

/**
 * Called whenever a scenario is selected/applied in the match settings.
 *
 * The callback should initialise all the properties in g_ScenarioData.
 */
void scenarioInit(void)
{
	s32 scenario = scenarioGetSafeIndex("scenarioInit");
	const mode_rules_plan_t *plan;
	s32 k;

	/* gamemode-graphs.md slice 3: the mode is chosen; its program's plan. */
	scenarioPlanMake(scenario, "chosen");
	plan = scenarioPlan(scenario);
	scenarioPlanCompare(scenario, plan, MODE_SEAM_INIT);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_INIT, k);
		if (owner >= 0 && g_MpScenarios[owner].initfunc) {
			g_MpScenarios[owner].initfunc();
		}
	}
	modeEventRecord(MODE_EVENT_CHOSEN, -1);
}

/**
 * Return the number of additional props that will be created, such as
 * briefcases and uplinks.
 */
s32 scenarioNumProps(void)
{
	s32 result = 0;
	s32 scenario = scenarioGetSafeIndex("scenarioNumProps");
	const mode_rules_plan_t *plan = scenarioPlan(scenario);
	s32 k;

	scenarioPlanCompare(scenario, plan, MODE_SEAM_NUM_PROPS);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_NUM_PROPS, k);
		if (owner >= 0 && g_MpScenarios[owner].numpropsfunc) {
			result += g_MpScenarios[owner].numpropsfunc();
		}
	}

	return result;
}

/**
 * Create the additional props, such as briefcases and uplinks.
 */
void scenarioInitProps(void)
{
	s32 scenario = scenarioGetSafeIndex("scenarioInitProps");
	const mode_rules_plan_t *plan = scenarioPlan(scenario);
	s32 k;

	scenarioPlanCompare(scenario, plan, MODE_SEAM_INIT_PROPS);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_INIT_PROPS, k);
		if (owner >= 0 && g_MpScenarios[owner].initpropsfunc) {
			g_MpScenarios[owner].initpropsfunc();
		}
	}
	modeEventRecord(MODE_EVENT_PROPS_INIT, -1);
}

/**
 * At the start of each match, a hud message appears for all players containing
 * the challenge name if it's a challenge, or the scenario name if not.
 */
void scenarioCreateMatchStartHudmsgs(void)
{
	s32 i;
	s32 prevplayernum = g_Vars.currentplayernum;
	char challengename[60];
	char scenarioname[60];

	if (g_BossFile.locktype == MPLOCKTYPE_CHALLENGE) {
#if VERSION >= VERSION_JPN_FINAL
		snprintf(challengename, sizeof(challengename), "%s\n", challengeGetNameBySlot(challengeGetCurrent()));
#else
		snprintf(challengename, sizeof(challengename), "%s:\n", challengeGetNameBySlot(challengeGetCurrent()));
#endif
	}

	snprintf(scenarioname, sizeof(scenarioname), "%s\n",
		langGet(g_MpScenarioOverviews[scenarioGetSafeIndex("scenarioCreateMatchStartHudmsgs")].name));

	for (i = 0; i < g_MpNumChrs; i++) {
		if (g_MpAllChrPtrs[i] != NULL && g_MpAllChrPtrs[i]->aibot == NULL
				&& i < MAX_PLAYERS && g_Vars.players[i]) {
			setCurrentPlayerNum(i);

			if (g_BossFile.locktype == MPLOCKTYPE_CHALLENGE) {
#if VERSION >= VERSION_JPN_FINAL
				hudmsgCreateWithFlags(challengename, HUDMSGTYPE_DEFAULT, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
#else
				hudmsgCreateWithFlags(challengename, HUDMSGTYPE_DEFAULT, HUDMSGFLAG_ONLYIFALIVE);
#endif
			}

#if VERSION >= VERSION_JPN_FINAL
			hudmsgCreateWithFlags(scenarioname, HUDMSGTYPE_DEFAULT, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
#else
			hudmsgCreateWithFlags(scenarioname, HUDMSGTYPE_DEFAULT, HUDMSGFLAG_ONLYIFALIVE);
#endif
		}
	}

	setCurrentPlayerNum(prevplayernum);
}

/**
 * Called on every frame during a match, including while paused.
 */
/* PC (T-NETWORKING-011): the scenario points scenarioTick last logged per
 * runtime chr slot. Points are not logged where the scenarios award them, so a
 * match that ends without a score could not show whether anyone scored. */
static s16 s_ScenarioLoggedPoints[MAX_MPCHRS];

static void scenarioLogPointChanges(void)
{
	s32 i;

	// A new stage starts its frame count again: every chr starts on 0
	if (g_Vars.lvframenum <= 1) {
		memset(s_ScenarioLoggedPoints, 0, sizeof(s_ScenarioLoggedPoints));
	}

	for (i = 0; i < g_MpNumChrs && i < MAX_MPCHRS; i++) {
		const struct mpchrconfig *mpchr = g_MpAllChrConfigPtrs[i];

		if (mpchr && mpchr->numpoints != s_ScenarioLoggedPoints[i]) {
			sysLogPrintf(LOG_NOTE, "MATCH.POINTS: scenario=%d slot=%d %s points %d -> %d at %.1f s netmode=%d",
				(s32)g_MpSetup.scenario, i, i < PLAYERCOUNT() ? "player" : "simulant",
				(s32)s_ScenarioLoggedPoints[i], (s32)mpchr->numpoints, g_Vars.lvframe60 / 60.0f, (s32)g_NetMode);
			s_ScenarioLoggedPoints[i] = mpchr->numpoints;
		}
	}
}

void scenarioTick(void)
{
	static s32 s_ScenarioTickFirstRun = 1;
	s32 scenario = scenarioGetSafeIndex("scenarioTick");
	if (s_ScenarioTickFirstRun) {
		sysLogPrintf(LOG_NOTE, "TICK: scenarioTick first call lvframe60=%d scenario=%d normmplayerisrunning=%d tickfunc=%p",
			g_Vars.lvframe60, g_MpSetup.scenario, g_Vars.normmplayerisrunning,
			(void *)g_MpScenarios[scenario].tickfunc);
		s_ScenarioTickFirstRun = 0;
	}

	if (g_Vars.normmplayerisrunning) {
		if (g_Vars.lvframenum == 5) {
			scenarioCreateMatchStartHudmsgs();
		}

		{
			const mode_rules_plan_t *plan = scenarioPlan(scenario);
			s32 k;

			scenarioPlanCompare(scenario, plan, MODE_SEAM_TICK);
			for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
				const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_TICK, k);
				if (owner >= 0 && g_MpScenarios[owner].tickfunc) {
					g_MpScenarios[owner].tickfunc();
				}
			}
		}
		modeEventRecord(MODE_EVENT_TICK, -1);

		scenarioLogPointChanges();
	}
}

/**
 * Tick a single chr.
 *
 * If chr is provided then it will be a bot.
 * If chr is NULL then the handler should tick the current player instead.
 */
void scenarioTickChr(struct chrdata *chr)
{
	s32 scenario = scenarioGetSafeIndex("scenarioTickChr");
	if (g_Vars.normmplayerisrunning) {
		const mode_rules_plan_t *plan = scenarioPlan(scenario);
		s32 k;

		scenarioPlanCompare(scenario, plan, MODE_SEAM_TICK_CHR);
		for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_TICK_CHR, k);
			if (owner >= 0 && g_MpScenarios[owner].tickchrfunc) {
				g_MpScenarios[owner].tickchrfunc(chr);
			}
		}
	}
	if (g_Vars.normmplayerisrunning) {
		const struct chrdata *who = chr ? chr
			: g_Vars.currentplayer && g_Vars.currentplayer->prop
				? g_Vars.currentplayer->prop->chr : NULL;
		modeEventRecord(MODE_EVENT_CHR_TICK, who ? who->chrnum : -1);
	}
}

/**
 * Render any HUD information such as timers.
 */
Gfx *scenarioRenderHud(Gfx *gdl)
{
	s32 viewleft;
	s32 viewright;
	s32 viewtop;
	s32 viewheight;
	s32 cplayernum;
	s32 playercount;
	struct chrdata *chr;
	u32 colour;

	if (g_Vars.normmplayerisrunning) {
		s32 scenario = scenarioGetSafeIndex("scenarioRenderHud");
		const mode_rules_plan_t *plan = scenarioPlan(scenario);
		s32 k;

		scenarioPlanCompare(scenario, plan, MODE_SEAM_HUD);
		for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_HUD, k);
			if (owner < 0 || !g_MpScenarios[owner].hudfunc) {
				continue;
			}
#if VERSION >= VERSION_NTSC_1_0
			if (g_MpSetup.paused != MPPAUSEMODE_GAMEOVER && g_NumReasonsToEndMpMatch == 0) {
				gDPSetTextureFilter(gdl++, G_TF_POINT);
				gDPSetColorDither(gdl++, G_CD_DISABLE);
				gSPClearGeometryMode(gdl++, G_ZBUFFER);
				gDPPipeSync(gdl++);
				gDPSetTexturePersp(gdl++, G_TP_NONE);
				gDPSetCycleType(gdl++, G_CYC_FILL);
				gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
				gDPPipelineMode(gdl++, G_PM_1PRIMITIVE);

				gdl = g_MpScenarios[owner].hudfunc(gdl);
			}
#else
			gdl = g_MpScenarios[owner].hudfunc(gdl);
#endif
		}

		playercount = PLAYERCOUNT();

		if ((g_MpSetup.options & MPOPTION_TEAMSENABLED)
				&& (g_MpSetup.options & MPOPTION_DISPLAYTEAM)
				&& playercount >= 2) {
			chr = g_Vars.currentplayer->prop->chr;
			cplayernum = g_Vars.currentplayernum;

			gDPSetTextureFilter(gdl++, G_TF_POINT);
			gDPSetColorDither(gdl++, G_CD_DISABLE);
			gSPClearGeometryMode(gdl++, G_ZBUFFER);
			gDPPipeSync(gdl++);
			gDPSetTexturePersp(gdl++, G_TP_NONE);
			gDPSetCycleType(gdl++, G_CYC_FILL);
			gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
			gDPPipelineMode(gdl++, G_PM_1PRIMITIVE);

			colour = var80087ce4[radarGetTeamIndex(chr->team)];
			gDPSetFillColor(gdl++, colour);

			viewleft = viGetViewLeft();
			viewright = viewleft + viGetViewWidth();
			viewtop = viGetViewTop();
			viewheight = viGetViewHeight();

			if (playercount >= 3) {
				if (cplayernum <= 1) {
					// Player on top quarter - draw line at bottom of viewport
					gDPFillRectangle(gdl++, viewleft, viewheight + viewtop - 1, viewright - 1, viewheight + viewtop - 1);
				} else {
					// Player on bottom quarter - draw line at top of viewport
					gDPFillRectangle(gdl++, viewleft, viewtop, viewright - 1, viewtop);
				}
			}
			else {
				// @bug: No consideration is made for vertical splits here,
				// however when using a vertical split neither line is visible.
				// It's suspected that a screen border is drawn over the top of it.
				if (cplayernum == 0) {
					// Player on top half - draw line at bottom of viewport
					gDPFillRectangle(gdl++, viewleft, viewheight + viewtop - 1, viewright - 1, viewheight + viewtop - 1);
				} else {
					// Player on bottom half - draw line at top of viewport
					gDPFillRectangle(gdl++, viewleft, viewtop, viewright - 1, viewtop);
				}
			}
		}
	}

	return gdl;
}

/**
 * Calculate the score and number of deaths for the given mpchr.
 *
 * If no callback is registered, the default calculation below will apply.
 */
void scenarioCalculatePlayerScore(struct mpchrconfig *mpchr, s32 chrnum, s32 *score, s32 *deaths)
{
	struct mpchrconfig *othermpchr;
	s32 i;
	s32 scenario = scenarioGetSafeIndex("scenarioCalculatePlayerScore");
	const mode_rules_plan_t *plan = scenarioPlan(scenario);
	s32 owner = MODE_PLAN_NONE;
	s32 k;

	/* The first score module the plan names answers. */
	scenarioPlanCompare(scenario, plan, MODE_SEAM_SCORE);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM && owner == MODE_PLAN_NONE; k++) {
		owner = scenarioPlanOwner(plan, MODE_SEAM_SCORE, k);
		if (owner >= 0 && !g_MpScenarios[owner].calcscorefunc) {
			owner = MODE_PLAN_NONE;
		}
	}

	if (owner >= 0) {
		g_MpScenarios[owner].calcscorefunc(mpchr, chrnum, score, deaths);
	} else if (owner != MODE_RULES_DEFAULT) {
		/* No score module: the mode scores nothing. */
		*score = 0;
		*deaths = mpchr->numdeaths;
	} else {
		/* og.mode.default.score: kills minus suicides and team kills. */
		*score = 0;

		for (i = 0; i < MAX_MPCHRS; i++) {
			if (i == chrnum) {
				*score -= mpchr->killcounts[i];
			} else if (g_MpSetup.options & MPOPTION_TEAMSENABLED) {
				othermpchr = MPCHR(i);

				if (othermpchr->team == mpchr->team) {
					*score -= mpchr->killcounts[i];
				} else {
					*score += mpchr->killcounts[i];
				}
			} else {
				*score += mpchr->killcounts[i];
			}
		}

		*deaths = mpchr->numdeaths;
	}
	modeEventRecord(MODE_EVENT_SCORE, chrnum);
}

/**
 * Draw anything extra to the radar, such as props or the king of the hill room.
 */
Gfx *scenarioRadarExtra(Gfx *gdl)
{
	s32 scenario = scenarioGetSafeIndex("scenarioRadarExtra");
	if (g_Vars.normmplayerisrunning) {
		const mode_rules_plan_t *plan = scenarioPlan(scenario);
		s32 k;

		scenarioPlanCompare(scenario, plan, MODE_SEAM_RADAR_EXTRA);
		for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_RADAR_EXTRA, k);
			if (owner >= 0 && g_MpScenarios[owner].radarextrafunc) {
				gdl = g_MpScenarios[owner].radarextrafunc(gdl);
			}
		}
	}

	return gdl;
}

/**
 * Render a player or bot on the radar.
 *
 * Return true if handled or false if the generic radar code should render it.
 */
bool scenarioRadarChr(Gfx **gdl, struct prop *prop)
{
	s32 scenario = scenarioGetSafeIndex("scenarioRadarChr");
	if (g_Vars.normmplayerisrunning) {
		const mode_rules_plan_t *plan = scenarioPlan(scenario);
		s32 k;

		scenarioPlanCompare(scenario, plan, MODE_SEAM_RADAR_CHR);
		for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_RADAR_CHR, k);
			if (owner >= 0 && g_MpScenarios[owner].radarchrfunc
					&& g_MpScenarios[owner].radarchrfunc(gdl, prop)) {
				return true;
			}
		}
	}

	return false;
}

/**
 * Set the highlight colour of the given prop.
 *
 * The prop may be a chr, weapon or object.
 */
bool scenarioHighlightProp(struct prop *prop, s32 *colour)
{
	s32 scenario = scenarioGetSafeIndex("scenarioHighlightProp");
	const mode_rules_plan_t *plan = scenarioPlan(scenario);
	s32 k;

	scenarioPlanCompare(scenario, plan, MODE_SEAM_HIGHLIGHT_PROP);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_HIGHLIGHT_PROP, k);
		if (owner >= 0 && g_MpScenarios[owner].highlightpropfunc
				&& g_MpScenarios[owner].highlightpropfunc(prop, colour)) {
			return true;
		}
	}

	if (prop->type == PROPTYPE_OBJ || prop->type == PROPTYPE_DOOR || prop->type == PROPTYPE_WEAPON) {
		struct defaultobj *obj = prop->obj;

		if ((g_MpSetup.scenario != MPSCENARIO_COMBAT || (g_MpSetup.options & MPOPTION_NOPICKUPHIGHLIGHT) == 0)
				&& (g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].base.displayoptions & MPDISPLAYOPTION_HIGHLIGHTPICKUPS)) {
			switch (obj->type) {
			case OBJTYPE_AMMOCRATE:
			case OBJTYPE_WEAPON:
			case OBJTYPE_LINKGUNS:
			case OBJTYPE_MULTIAMMOCRATE:
			case OBJTYPE_SHIELD:
				colour[0] = 0;
				colour[1] = 0xcd;
				colour[2] = 0xff;
				colour[3] = menuGetSinOscFrac(20) * 255;
				return true;
			}
		}
	} else if (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) {
		struct chrdata *chr = prop->chr;
		bool pulse = false;
		bool isunselectedbot = false;
		bool useblue = false;
		bool useteamcolour = false;

		if (g_MpSetup.options & MPOPTION_TEAMSENABLED) {
			struct chrdata *botchr = currentPlayerGetCommandingAibot();

			if (botchr) {
				if (botchr == chr) {
					pulse = true;
					useteamcolour = true;
				} else {
					isunselectedbot = true;
				}
			}
		}

		if (!pulse && !isunselectedbot
				&& (g_MpSetup.scenario != MPSCENARIO_COMBAT || (g_MpSetup.options & MPOPTION_NOPLAYERHIGHLIGHT) == 0)) {
			if ((g_MpSetup.options & MPOPTION_TEAMSENABLED)
					&& (g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].base.displayoptions & MPDISPLAYOPTION_HIGHLIGHTTEAMS)) {
				useteamcolour = true;
			} else if (g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].base.displayoptions & MPDISPLAYOPTION_HIGHLIGHTPLAYERS) {
				useblue = true;
			}
		}

		if (useteamcolour && chr) {
			u32 tmp = g_TeamColours[radarGetTeamIndex(chr->team)];

			colour[0] = tmp >> 24 & 0xff;
			colour[1] = tmp >> 16 & 0xff;
			colour[2] = tmp >> 8 & 0xff;
			colour[3] = pulse ? (s32)(menuGetSinOscFrac(20) * 128) : 75;
			return true;
		}

		if (useblue) {
			colour[0] = 0;
			colour[1] = 0xcd;
			colour[2] = 0xff;
			colour[3] = menuGetSinOscFrac(20) * 205;
			return true;
		}
	}

	return false;
}

/**
 * Choose a spawn location for a chr.
 *
 * CTC uses this to ensure the chrs spawn near their base.
 */
f32 scenarioChooseSpawnLocation(f32 chrradius, struct coord *pos, RoomNum *rooms, struct prop *prop)
{
	f32 result;
	s32 scenario = scenarioGetSafeIndex("scenarioChooseSpawnLocation");

	sysLogPrintf(LOG_NOTE, "SPAWN: scenarioChooseSpawnLocation scenario=%d normmplay=%d spawnfunc=%d numpads=%d",
		g_MpSetup.scenario, g_Vars.normmplayerisrunning,
		(g_Vars.normmplayerisrunning && g_MpScenarios[scenario].spawnfunc) ? 1 : 0,
		g_NumSpawnPoints);
	if (g_Vars.normmplayerisrunning) {
		modeEventRecord(MODE_EVENT_SPAWN, prop && prop->chr ? prop->chr->chrnum : -1);
	}

	if (g_Vars.normmplayerisrunning) {
		const mode_rules_plan_t *plan = scenarioPlan(scenario);
		s32 k;

		/* In plan order: a mode's chooser may find no place, and the next
		 * module then answers (og.mode.default.spawn: the general chooser). */
		scenarioPlanCompare(scenario, plan, MODE_SEAM_SPAWN);
		for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_SPAWN, k);
			if (owner == MODE_RULES_DEFAULT) {
				break;
			}
			if (owner >= 0 && g_MpScenarios[owner].spawnfunc
					&& g_MpScenarios[owner].spawnfunc(chrradius, pos, rooms, prop, &result)) {
				return result;
			}
		}
	}

	return playerChooseGeneralSpawnLocation(chrradius, pos, rooms, prop);
}

/**
 * Called when starting a match.
 *
 * The function clears anything in the g_ScenarioData struct that is
 * stage specific such as pad and room numbers, then re-reads the setup
 * file to reload that information.
 */
/* gamemode-graphs.md slice 5: the states the running mode's plan uses that
 * have a wire body, one bit per state. */
static u32 scenarioNetStates(void)
{
	const mode_rules_plan_t *plan;
	u32 states;
	u32 wired = 0;
	s32 state;

	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		return 0;
	}

	plan = scenarioPlan(g_MpSetup.scenario);
	states = modeRulesPlanStates(plan);

	for (state = 0; state < MODE_RULES_MODE_COUNT; state++) {
		if ((states & (1u << state)) && netScenarioStatePayloadSize((u8)state) > 0) {
			wired |= 1u << state;
		}
	}

	return wired;
}

s32 scenarioNetHasState(void)
{
	return scenarioNetStates() != 0;
}

/* Whether two captures differ in anything but the revision and a running
 * timer (the King of the Hill hold time), which the host resends at a low
 * rate instead. Captures are zeroed before they are filled, so padding is
 * equal and memcmp is exact. */
static s32 scenarioNetSectionChanged(const net_scenario_state_t *a, const net_scenario_state_t *b)
{
	net_scenario_state_t x;
	net_scenario_state_t y;

	memcpy(&x, a, sizeof(x));
	memcpy(&y, b, sizeof(y));
	x.revision = 0;
	y.revision = 0;

	if (x.scenario == NET_SCENARIO_STATE_KINGOFTHEHILL && y.scenario == NET_SCENARIO_STATE_KINGOFTHEHILL) {
		x.body.koh.elapsed240 = 0;
		y.body.koh.elapsed240 = 0;
	}

	if (x.scenario == NET_SCENARIO_STATE_POPACAP && y.scenario == NET_SCENARIO_STATE_POPACAP) {
		x.body.pac.age240 = 0;
		y.body.pac.age240 = 0;
	}

	// A carried briefcase moves with its holder every frame; only who holds
	// it counts as a change
	if (x.scenario == NET_SCENARIO_STATE_HOLDTHEBRIEFCASE && y.scenario == NET_SCENARIO_STATE_HOLDTHEBRIEFCASE) {
		memset(x.body.htb.pos, 0, sizeof(x.body.htb.pos));
		memset(y.body.htb.pos, 0, sizeof(y.body.htb.pos));
	}

	// The same for the uplink, and download progress is a running timer
	if (x.scenario == NET_SCENARIO_STATE_HACKERCENTRAL && y.scenario == NET_SCENARIO_STATE_HACKERCENTRAL) {
		memset(x.body.htm.uplink_pos, 0, sizeof(x.body.htm.uplink_pos));
		memset(y.body.htm.uplink_pos, 0, sizeof(y.body.htm.uplink_pos));
		memset(x.body.htm.dltime240, 0, sizeof(x.body.htm.dltime240));
		memset(y.body.htm.dltime240, 0, sizeof(y.body.htm.dltime240));
	}

	// Carried cases move with their holders
	if (x.scenario == NET_SCENARIO_STATE_CAPTURETHECASE && y.scenario == NET_SCENARIO_STATE_CAPTURETHECASE) {
		memset(x.body.ctc.pos, 0, sizeof(x.body.ctc.pos));
		memset(y.body.ctc.pos, 0, sizeof(y.body.ctc.pos));
	}

	return memcmp(&x, &y, sizeof(x)) != 0;
}

static s32 scenarioNetStateChanged(const net_scenario_state_list_t *a, const net_scenario_state_list_t *b)
{
	s32 i;

	if (a->count != b->count) {
		return true;
	}

	for (i = 0; i < a->count; i++) {
		if (scenarioNetSectionChanged(&a->sections[i], &b->sections[i])) {
			return true;
		}
	}

	return false;
}

s32 scenarioNetCaptureState(net_scenario_state_list_t *out, s32 *changed)
{
	net_scenario_state_list_t list;
	u32 states = scenarioNetStates();
	s32 state;
	s32 i;

	if (changed) {
		*changed = false;
	}

	if (out == NULL || states == 0) {
		return false;
	}

	/* gamemode-graphs.md slice 5: one section per state the plan uses, keyed
	 * by the state, in mode order. */
	memset(&list, 0, sizeof(list));

	for (state = 0; state < MODE_RULES_MODE_COUNT && list.count < NET_SCENARIO_STATE_MAX_SECTIONS; state++) {
		net_scenario_state_t *section;

		if (!(states & (1u << state))) {
			continue;
		}

		section = &list.sections[list.count++];
		section->scenario = (u8)state;

		switch (state) {
		case NET_SCENARIO_STATE_KINGOFTHEHILL:
			kohNetCapture(&section->body.koh);
			break;
		case NET_SCENARIO_STATE_POPACAP:
			pacNetCapture(&section->body.pac);
			break;
		case NET_SCENARIO_STATE_HOLDTHEBRIEFCASE:
			htbNetCapture(&section->body.htb);
			break;
		case NET_SCENARIO_STATE_HACKERCENTRAL:
			htmNetCapture(&section->body.htm);
			break;
		case NET_SCENARIO_STATE_CAPTURETHECASE:
			ctcNetCapture(&section->body.ctc);
			break;
		}
	}

	if (!s_ScenarioNetHaveLast || scenarioNetStateChanged(&list, &s_ScenarioNetLast)) {
		s_ScenarioNetRevision++;

		if (changed) {
			*changed = true;
		}
	}

	for (i = 0; i < list.count; i++) {
		list.sections[i].revision = s_ScenarioNetRevision;
	}

	memcpy(&s_ScenarioNetLast, &list, sizeof(list));
	s_ScenarioNetHaveLast = true;
	memcpy(out, &list, sizeof(list));

	return true;
}

/*
 * B-1127: character slots in a state are the host's numbers; translate them
 * to this client's own (it runs itself as player 0). Counts indexed by slot
 * move with their slot.
 */
static void scenarioNetLocalizeSlots(net_scenario_state_t *state)
{
	s32 i;

	switch (state->scenario) {
	case NET_SCENARIO_STATE_POPACAP: {
		net_scenario_pac_state_t *pac = &state->body.pac;
		s16 kills[NET_SCENARIO_STATE_MAX_CHRS];
		s16 survivals[NET_SCENARIO_STATE_MAX_CHRS];

		if (pac->victim >= 0) {
			pac->victim = (s8)netChrSlotFromHost(pac->victim);
		}

		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			kills[netChrSlotFromHost(i)] = pac->killcounts[i];
			survivals[netChrSlotFromHost(i)] = pac->survivalcounts[i];
		}

		memcpy(pac->killcounts, kills, sizeof(kills));
		memcpy(pac->survivalcounts, survivals, sizeof(survivals));
		break;
	}
	case NET_SCENARIO_STATE_HOLDTHEBRIEFCASE:
		if (state->body.htb.holder >= 0) {
			state->body.htb.holder = (s8)netChrSlotFromHost(state->body.htb.holder);
		}
		break;
	case NET_SCENARIO_STATE_HACKERCENTRAL: {
		net_scenario_htm_state_t *htm = &state->body.htm;
		u16 times[NET_SCENARIO_STATE_MAX_CHRS];
		s16 points[NET_SCENARIO_STATE_MAX_CHRS];

		if (htm->uplink_holder >= 0) {
			htm->uplink_holder = (s8)netChrSlotFromHost(htm->uplink_holder);
		}

		if (htm->dl_player >= 0) {
			htm->dl_player = (s8)netChrSlotFromHost(htm->dl_player);
		}

		if (htm->player_in_range >= 0) {
			htm->player_in_range = (s8)netChrSlotFromHost(htm->player_in_range);
		}

		for (i = 0; i < NET_SCENARIO_STATE_MAX_CHRS; i++) {
			times[netChrSlotFromHost(i)] = htm->dltime240[i];
			points[netChrSlotFromHost(i)] = htm->numpoints[i];
		}

		memcpy(htm->dltime240, times, sizeof(times));
		memcpy(htm->numpoints, points, sizeof(points));
		break;
	}
	case NET_SCENARIO_STATE_CAPTURETHECASE:
		for (i = 0; i < NET_SCENARIO_CTC_BASES; i++) {
			if (state->body.ctc.holders[i] >= 0) {
				state->body.ctc.holders[i] = (s8)netChrSlotFromHost(state->body.ctc.holders[i]);
			}
		}
		break;
	default:
		break;
	}
}

s32 scenarioNetReceiveState(const net_scenario_state_list_t *in)
{
	const u32 states = scenarioNetStates();
	u32 received = 0;
	u32 revision;
	s32 i;

	if (in == NULL || in->count < 1 || scenarioNetIsAuthority()) {
		return false;
	}

	/* gamemode-graphs.md slice 5: the host sends exactly the states this
	 * mode's plan uses (both peers hold the same mode archive). */
	for (i = 0; i < in->count; i++) {
		if (in->sections[i].scenario < 32) {
			received |= 1u << in->sections[i].scenario;
		}
	}

	if (states == 0 || received != states) {
		sysLogPrintf(LOG_WARNING, "SCENARIO.STATE: dropped scenario=%u rev=%u, match runs scenario %d",
			(unsigned)in->sections[0].scenario, (unsigned)in->sections[0].revision, (s32)g_MpSetup.scenario);
		return false;
	}

	revision = in->sections[0].revision;

	// Reliable and ordered, so an older revision means a stale copy (for
	// example a baseline retried after a newer change was sent)
	if ((s_ScenarioNetHaveShown && revision < s_ScenarioNetShownRevision)
			|| (s_ScenarioNetHavePending && revision < s_ScenarioNetPending.sections[0].revision)) {
		return false;
	}

	if (!s_ScenarioNetHaveShown && !s_ScenarioNetHavePending) {
		sysLogPrintf(LOG_NOTE, "SCENARIO.STATE: first state from host scenario=%u rev=%u",
			(unsigned)in->sections[0].scenario, (unsigned)revision);
	}

	memcpy(&s_ScenarioNetPending, in, sizeof(s_ScenarioNetPending));
	for (i = 0; i < s_ScenarioNetPending.count; i++) {
		scenarioNetLocalizeSlots(&s_ScenarioNetPending.sections[i]);
	}
	s_ScenarioNetPendingStates = received;
	s_ScenarioNetHavePending = true;

	return true;
}

/* gamemode-graphs.md slice 4: what scenarioReset's switch did per mode. */
static void kohReset(void)
{
	g_ScenarioData.koh.hillcount = 0;
}

static void ctcReset(void)
{
	s32 i;
	s32 j;

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam); i++) {
		g_ScenarioData.ctc.spawnpadsperteam[i].homepad = -1;
		g_ScenarioData.ctc.spawnpadsperteam[i].numspawnpads = 0;

		for (j = 0; j < ARRAYCOUNT(g_ScenarioData.ctc.spawnpadsperteam[i].spawnpads); j++) {
			g_ScenarioData.ctc.spawnpadsperteam[i].spawnpads[j] = -1;
		}
	}

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.playercountsperteam); i++) {
		g_ScenarioData.ctc.playercountsperteam[i] = 0;
		g_ScenarioData.ctc.teamindexes[i] = -1;
	}
}

/* The stage intro's case and hill pads: each mode takes the kind it uses. */
static void htbIntroPad(s32 *cmd)
{
	if (cmd[0] == INTROCMD_CASE || cmd[0] == INTROCMD_CASERESPAWN) {
		htbAddPad(cmd[2]);
	}
}

static void htmIntroPad(s32 *cmd)
{
	if (cmd[0] == INTROCMD_CASE || cmd[0] == INTROCMD_CASERESPAWN) {
		htmAddPad(cmd[2]);
	}
}

static void ctcIntroPad(s32 *cmd)
{
	if (cmd[0] == INTROCMD_CASE || cmd[0] == INTROCMD_CASERESPAWN) {
		ctcAddPad(cmd);
	}
}

static void kohIntroPad(s32 *cmd)
{
	if (cmd[0] == INTROCMD_HILL) {
		kohAddHill(cmd);
	}
}

static void scenarioRunIntroPad(s32 scenario, const mode_rules_plan_t *plan, s32 *cmd)
{
	s32 k;

	scenarioPlanCompare(scenario, plan, MODE_SEAM_INTRO_PAD);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_INTRO_PAD, k);
		if (owner >= 0 && g_MpScenarios[owner].intropadfunc) {
			g_MpScenarios[owner].intropadfunc(cmd);
		}
	}
}

void scenarioReset(void)
{
	s32 *cmd = g_StageSetup.intro;
	s32 scenario = scenarioGetSafeIndex("scenarioReset");
	const mode_rules_plan_t *plan;
	s32 k;

	scenarioNetResetState();
	/* gamemode-graphs.md slice 3: a stage loads; the mode's program's plan. */
	scenarioPlanMake(scenario, "stage_reset");
	plan = scenarioPlan(scenario);

	/* Slice 4: each mode clears its stage state. */
	scenarioPlanCompare(scenario, plan, MODE_SEAM_STAGE_RESET);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_STAGE_RESET, k);
		if (owner >= 0 && g_MpScenarios[owner].resetfunc) {
			g_MpScenarios[owner].resetfunc();
		}
	}
	modeEventRecord(MODE_EVENT_STAGE_RESET, -1);

	if (cmd) {
		s32 safety = 0;
		while (cmd[0] != INTROCMD_END && ++safety < 10000) {
			switch (cmd[0]) {
			case INTROCMD_SPAWN:
				cmd += 3;
				break;
			case INTROCMD_CASE:
			case INTROCMD_CASERESPAWN:
				modeEventRecord(MODE_EVENT_INTRO_PAD, -1);
				scenarioRunIntroPad(scenario, plan, cmd);
				cmd += 3;
				break;
			case INTROCMD_HILL:
				modeEventRecord(MODE_EVENT_INTRO_PAD, -1);
				scenarioRunIntroPad(scenario, plan, cmd);
				cmd += 2;
				break;
			case INTROCMD_WEAPON:
				cmd += 4;
				break;
			case INTROCMD_AMMO:
				cmd += 4;
				break;
			case INTROCMD_3:
				cmd += 8;
				break;
			case INTROCMD_4:
				cmd += 2;
				break;
			case INTROCMD_OUTFIT:
				cmd += 2;
				break;
			case INTROCMD_6:
				cmd += 10;
				break;
			case INTROCMD_WATCHTIME:
				cmd += 3;
				break;
			case INTROCMD_CREDITOFFSET:
				cmd += 2;
				break;
			default:
				cmd++;
				break;
			}
		}
	}
}

static bool scenarioStageLoadOwnsMpSetup(s32 stagenum)
{
	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		return false;
	}

	return (s32)g_MpSetup.stagenum == stagenum;
}

void scenarioResetForStageLoad(s32 stagenum)
{
	if (!scenarioStageLoadOwnsMpSetup(stagenum)) {
		sysLogPrintf(LOG_NOTE,
			"SCENARIO: skip MP scenario reset for stage load stage=0x%02x mp_stage=0x%02x scenario=%d",
			stagenum, g_MpSetup.stagenum, g_MpSetup.scenario);
		return;
	}

	scenarioReset();
}

/**
 * Return the maximum number of teams permitted for this scenario.
 *
 * CTC sets this to 4, while the others use the default limit of 8.
 */
s32 scenarioGetMaxTeams(void)
{
	// PC (B-1132): the in-match team switch asks too; a scenario outside the
	// table has no callbacks (quiet: these three run per menu tick or per room)
	if (scenarioIndexIsValid(g_MpSetup.scenario)) {
		const mode_rules_plan_t *plan = scenarioPlan(g_MpSetup.scenario);
		s32 k;

		scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_MAX_TEAMS);
		for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_MAX_TEAMS, k);
			if (owner >= 0 && g_MpScenarios[owner].maxteamsfunc) {
				return g_MpScenarios[owner].maxteamsfunc();
			}
		}
	}

	return MAX_TEAMS;
}

/**
 * PC (T-NETWORKING-011, finding 8): the match-start position a team's spawn
 * region should be anchored on, for scenarios whose teams start at a base
 * (Capture the Case). False when the scenario has no such place for the
 * team; the orchestrator then keeps its own spread-out anchor.
 */
s32 scenarioGetTeamSpawnAnchor(s32 team, struct coord *out)
{
	if (!g_Vars.normmplayerisrunning || g_MpSetup.scenario != MPSCENARIO_CAPTURETHECASE) {
		return false;
	}

	return ctcGetTeamSpawnAnchor(team, out);
}

/**
 * This callback is unused.
 */
bool scenarioIsRoomHighlighted(RoomNum room)
{
	if (scenarioIndexIsValid(g_MpSetup.scenario)) {
		const mode_rules_plan_t *plan = scenarioPlan(g_MpSetup.scenario);
		s32 k;

		scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_ROOM_LIGHTS);
		for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_ROOM_LIGHTS, k);
			if (owner >= 0 && g_MpScenarios[owner].isroomhighlightedfunc
					&& g_MpScenarios[owner].isroomhighlightedfunc(room)) {
				return true;
			}
		}
	}

	return false;
}

/**
 * Override the colour for the given room.
 *
 * Used in CTC for the team bases and in KOH for the hill.
 */
void scenarioHighlightRoom(RoomNum room, s32 *arg1, s32 *arg2, s32 *arg3)
{
	if (scenarioIndexIsValid(g_MpSetup.scenario)) {
		const mode_rules_plan_t *plan = scenarioPlan(g_MpSetup.scenario);
		s32 k;

		scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_ROOM_LIGHTS);
		for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
			const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_ROOM_LIGHTS, k);
			if (owner >= 0 && g_MpScenarios[owner].highlightroomfunc) {
				g_MpScenarios[owner].highlightroomfunc(room, arg1, arg2, arg3);
			}
		}
	}
}

struct menuitem g_MpScenarioMenuItems[] = {
	{
		MENUITEMTYPE_LIST,
		0,
		MENUITEMFLAG_LIST_WIDE | MENUITEMFLAG_LOCKABLEMINOR,
		0x00000078,
		0x0000004d,
		scenarioScenarioMenuHandler,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_SMALLFONT,
		(uintptr_t)&mpMenuTextScenarioDescription,
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_MpScenarioMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	L_MPMENU_243, // "Scenario"
	g_MpScenarioMenuItems,
	NULL,
	MENUDIALOGFLAG_CLOSEONSELECT | MENUDIALOGFLAG_MPLOCKABLE,
	NULL,
};

struct menuitem g_MpQuickTeamScenarioMenuItems[] = {
	{
		MENUITEMTYPE_LIST,
		1,
		MENUITEMFLAG_LIST_WIDE | MENUITEMFLAG_LOCKABLEMINOR,
		0x00000078,
		0x0000004d,
		scenarioScenarioMenuHandler,
	},
	{
		MENUITEMTYPE_LABEL,
		0,
		MENUITEMFLAG_LESSLEFTPADDING | MENUITEMFLAG_SMALLFONT,
		(uintptr_t)&mpMenuTextScenarioDescription,
		0,
		NULL,
	},
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_MpQuickTeamScenarioMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	L_MPMENU_243, // "Scenario"
	g_MpQuickTeamScenarioMenuItems,
	NULL,
	MENUDIALOGFLAG_CLOSEONSELECT | MENUDIALOGFLAG_MPLOCKABLE,
	NULL,
};

/**
 * Create a general object.
 *
 * This is a helper function used by HTM to create the terminal.
 */
struct prop *scenarioCreateObj(s32 modelnum, s16 padnum, f32 arg2, u32 flags, u32 flags2, u32 flags3)
{
	struct defaultobj template = {
		256,                    // extrascale
		0,                      // hidden2
		OBJTYPE_BASIC,          // type
		MODEL_A51_CRATE1,       // modelnum
		-1,                     // pad
		0,                      // flags
		0,                      // flags2
		0,                      // flags3
		NULL,                   // prop
		NULL,                   // model
		1, 0, 0,                // realrot
		0, 1, 0,
		0, 0, 1,
		0,                      // hidden
		NULL,                   // geo
		NULL,                   // projectile
		0,                      // damage
		1000,                   // maxdamage
		0xff, 0xff, 0xff, 0x00, // shadecol
		0xff, 0xff, 0xff, 0x00, // nextcol
		0x0fff,                 // floorcol
		0,                      // tiles
	};

	struct defaultobj *obj = mempAlloc(ALIGN16(sizeof(struct defaultobj)), MEMPOOL_STAGE);
	*obj = template;

	obj->modelnum = modelnum;
	obj->pad = padnum;
	obj->flags = flags;
	obj->flags2 = flags2;
	obj->flags3 = flags3;
	obj->extrascale = arg2 * 256;
	obj->hidden2 &= ~OBJHFLAG_DELETING;

	setupCreateObject(obj, 123);

	/* PC (B-1125): setupCreateObject creates no prop when the pad is in no
	 * room or the model failed to load; activating NULL would crash. */
	if (obj->prop == NULL) {
		sysLogPrintf(LOG_WARNING, "SCENARIO: object model=%d not created on pad %d (pad in no room or model missing)",
			modelnum, (s32)padnum);
		return NULL;
	}

	propActivate(obj->prop);
	propEnable(obj->prop);

	return obj->prop;
}

/**
 * Create a HUD message for the given player.
 *
 * This is a helper function used by PAC.
 */
void scenarioCreateHudmsg(s32 playernum, char *message)
{
	if (playernum >= 0 && playernum < MAX_PLAYERS && g_Vars.players[playernum]) {
		s32 prevplayernum = g_Vars.currentplayernum;

		setCurrentPlayerNum(playernum);
#if VERSION >= VERSION_JPN_FINAL
		hudmsgCreateWithFlags(message, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
#else
		hudmsgCreateWithFlags(message, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
#endif
		setCurrentPlayerNum(prevplayernum);
	}
}

/**
 * CHeck if two player numbers are on the same team.
 *
 * This is a helper function used by PAC.
 */
bool scenarioChrsAreSameTeam(s32 playernum1, s32 playernum2)
{
	struct mpchrconfig *achr;
	struct mpchrconfig *bchr;

	if ((g_MpSetup.options & MPOPTION_TEAMSENABLED) && playernum1 >= 0 && playernum2 >= 0) {
		s32 a = func0f18d074(playernum1);
		s32 b = func0f18d074(playernum2);

		if (a >= 0 && b >= 0) {
			achr = MPCHR(a);
			bchr = MPCHR(b);

			return (achr->team == bchr->team) ? true : false;
		}
	}

	return false;
}

/**
 * gamemode-graphs.md slice 4: the plan's pickup module for this kind of token
 * (the briefcase of Hold the Briefcase and Capture the Case, the data uplink
 * of Hacker Central), or nothing.
 *
 * The return value is a TICKOP constant.
 */
/* Whether chr holds the token of this mode: its state's holder (the briefcase
 * of Hold the Briefcase, a case of Capture the Case, the uplink of Hacker
 * Central). */
static bool scenarioTokenHeldBy(s32 owner, const struct chrdata *chr)
{
	s32 i;

	if (!chr || !chr->prop) {
		return false;
	}
	switch (owner) {
	case MPSCENARIO_HOLDTHEBRIEFCASE:
		return g_ScenarioData.htb.token == chr->prop;
	case MPSCENARIO_HACKERCENTRAL:
		return g_ScenarioData.htm.uplink == chr->prop;
	case MPSCENARIO_CAPTURETHECASE:
		for (i = 0; i < (s32)ARRAYCOUNT(g_ScenarioData.ctc.tokens); i++) {
			if (g_ScenarioData.ctc.tokens[i] == chr->prop) {
				return true;
			}
		}
		return false;
	default:
		return false;
	}
}

static s32 scenarioPlanPickUpToken(struct chrdata *chr, struct prop *prop, s32 weaponnum)
{
	const mode_rules_plan_t *plan;
	s32 k;

	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		return TICKOP_NONE;
	}
	plan = scenarioPlan(g_MpSetup.scenario);
	scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_TOKEN_PICKED_UP);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_TOKEN_PICKED_UP, k);
		if (owner >= 0 && scenarioTokenWeaponNum(owner) == weaponnum
				&& g_MpScenarios[owner].pickuptokenfunc) {
			/* Review of PR 25: the event is an acquisition. Touching a token
			 * can also do nothing (your own case with no enemy case, a second
			 * enemy case) or score (bringing an enemy case home), so it is
			 * recorded only when chr holds the mode's token after the module
			 * and did not before. */
			const bool held = scenarioTokenHeldBy(owner, chr);
			const s32 result = g_MpScenarios[owner].pickuptokenfunc(chr, prop);

			if (!held && scenarioTokenHeldBy(owner, chr)) {
				modeEventRecord(MODE_EVENT_TOKEN_PICKED_UP, chr->chrnum);
			}
			return result;
		}
	}

	return TICKOP_NONE;
}

/**
 * Handle a player or bot picking up a briefcase, regardless of which scenario
 * it is.
 *
 * The return value is a TICKOP constant.
 */
s32 scenarioPickUpBriefcase(struct chrdata *chr, struct prop *prop)
{
	return scenarioPlanPickUpToken(chr, prop, WEAPON_BRIEFCASE2);
}

/* Hold the Briefcase: the briefcase is picked up. */
static s32 htbPickUpToken(struct chrdata *chr, struct prop *prop)
{
	struct defaultobj *obj = prop->obj;
	struct weaponobj *weapon = prop->weapon;
	s32 i;
	s32 prevplayernum;
	char text1[64];
	struct mpchrconfig *mpchr;

	// Player or bot has picked up the briefcase
	g_ScenarioData.htb.token = chr->prop;

	if (chr->aibot) {
		mpchr = g_MpAllChrConfigPtrs[mpPlayerGetIndex(chr)];
		propPlayPickupSound(prop, weapon->weaponnum);
		chr->aibot->hasbriefcase = true;
		botinvGiveSingleWeapon(chr, WEAPON_BRIEFCASE2);
	} else {
		mpchr = MPCHR(g_Vars.playerstats[g_Vars.currentplayernum].mpindex);

		invGiveSingleWeapon(WEAPON_BRIEFCASE2);
		currentPlayerQueuePickupWeaponHudmsg(WEAPON_BRIEFCASE2, false);
		weaponPlayPickupSound(WEAPON_BRIEFCASE2);
	}

#if VERSION >= VERSION_JPN_FINAL
	// "%shas the Briefcase"
	snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_000_2), scenarioRemoveLineBreaks(mpchr->name, 0), bgunGetShortName(WEAPON_BRIEFCASE2));
#elif VERSION >= VERSION_PAL_BETA
	// "%shas the Briefcase"
	snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_000_2), mpchr->name, bgunGetShortName(WEAPON_BRIEFCASE2));
#else
	// "%shas the\n%s"
	snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_000), mpchr->name, bgunGetShortName(WEAPON_BRIEFCASE2));
#endif

	prevplayernum = g_Vars.currentplayernum;

	for (i = 0; i < MAX_PLAYERS; i++) {
		if (!g_Vars.players[i]) {
			continue;
		}
		if (chr->aibot || i != prevplayernum) {
			setCurrentPlayerNum(i);
#if VERSION >= VERSION_JPN_FINAL
			hudmsgCreateWithFlags(text1, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
#else
			hudmsgCreateWithFlags(text1, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
#endif
		}
	}

	setCurrentPlayerNum(prevplayernum);

	if (chr->aibot) {
		obj->hidden |= OBJHFLAG_DELETING;
		return TICKOP_NONE;
	}

#if VERSION >= VERSION_NTSC_1_0
	objFree(obj, false, obj->hidden2 & OBJH2FLAG_CANREGEN);
	return TICKOP_FREE;
#else
	return TICKOP_GIVETOPLAYER;
#endif
}

/* Capture the Case: a team's case is picked up, or brought home. */
static s32 ctcPickUpToken(struct chrdata *chr, struct prop *prop)
{
	struct defaultobj *obj = prop->obj;
	struct weaponobj *weapon = prop->weapon;
	s32 i;
	s32 prevplayernum;
	s32 caseteam;
	char text1[64];
	char text2[64];
	char text3[64];
	struct mpchrconfig *mpchr;

	if (chr->aibot) {
		mpchr = g_MpAllChrConfigPtrs[mpPlayerGetIndex(chr)];
	} else {
		mpchr = MPCHR(g_Vars.playerstats[g_Vars.currentplayernum].mpindex);
	}

	if (weapon->team == mpchr->team) {
		if ((chr->aibot && chr->aibot->hascase) || (!chr->aibot && invHasBriefcase())) {
			// The chr already has a case and they are "picking up"
			// their home case. ie. A point has just been scored
			mpchr->numpoints++;

			for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.tokens); i++) {
				if (g_ScenarioData.ctc.tokens[i] == chr->prop) {
					break;
				}
			}

			if (chr->aibot) {
				botinvDropOne(chr, WEAPON_BRIEFCASE2);
				chr->aibot->hascase = false;
			} else {
				sndStart(var80095200, SFX_MP_SCOREPOINT, NULL, -1, -1, -1, -1, -1);
				weaponCreateForPlayerDrop(WEAPON_BRIEFCASE2);
				invRemoveItemByNum(WEAPON_BRIEFCASE2);
			}

#if VERSION >= VERSION_JPN_FINAL
			// "You captured the %s Briefcase"
			snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_004), scenarioRemoveLineBreaks(g_BossFile.teamnames[i], 0));

			// "%scaptured our Briefcase"
			snprintf(text2, sizeof(text2), langGet(L_MPWEAPONS_005), scenarioRemoveLineBreaks(mpchr->name, 0));

			// "%scaptured the %s Briefcase"
			snprintf(text3, sizeof(text3), langGet(L_MPWEAPONS_006), scenarioRemoveLineBreaks(mpchr->name, 0), scenarioRemoveLineBreaks(g_BossFile.teamnames[i], 1));
#elif VERSION >= VERSION_PAL_BETA
			// "You captured the %s Briefcase"
			snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_004), g_BossFile.teamnames[i]);

			// "%scaptured our Briefcase"
			snprintf(text2, sizeof(text2), langGet(L_MPWEAPONS_005), mpchr->name);

			// "%scaptured the %s Briefcase"
			snprintf(text3, sizeof(text3), langGet(L_MPWEAPONS_006), mpchr->name, g_BossFile.teamnames[i]);
#else
			// "You captured the %s%s"
			snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_004), g_BossFile.teamnames[i], bgunGetShortName(WEAPON_BRIEFCASE2));

			// "%scaptured our %s"
			snprintf(text2, sizeof(text2), langGet(L_MPWEAPONS_005), mpchr->name, bgunGetShortName(WEAPON_BRIEFCASE2));

			// "%scaptured the %s%s"
			snprintf(text3, sizeof(text3), langGet(L_MPWEAPONS_006), mpchr->name, g_BossFile.teamnames[i], bgunGetShortName(WEAPON_BRIEFCASE2));
#endif

			prevplayernum = g_Vars.currentplayernum;

			caseteam = i;

			if (caseteam);

			for (i = 0; i < MAX_PLAYERS; i++) {
				if (!g_Vars.players[i]) {
					continue;
				}
				setCurrentPlayerNum(i);

#if VERSION >= VERSION_JPN_FINAL
				if (!chr->aibot && i == prevplayernum) {
					hudmsgCreateWithFlags(text1, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
				} else if (caseteam == g_MpAllChrConfigPtrs[i]->team) {
					hudmsgCreateWithFlags(text2, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
				} else {
					hudmsgCreateWithFlags(text3, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
				}
#else
				if (!chr->aibot && i == prevplayernum) {
					hudmsgCreateWithFlags(text1, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
				} else if (caseteam == g_MpAllChrConfigPtrs[i]->team) {
					hudmsgCreateWithFlags(text2, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
				} else {
					hudmsgCreateWithFlags(text3, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
				}
#endif
			}

			setCurrentPlayerNum(prevplayernum);
		}

		if (chr->aibot) {
			return TICKOP_NONE;
		}

		return TICKOP_NONE;
	} else {
		// Picking up another team's case
		if ((chr->aibot && !chr->aibot->hascase) || (!chr->aibot && !invHasBriefcase())) {
			// And we have no case currently, so go ahead and pick it up
			if (chr->aibot) {
				chr->aibot->hascase = true;
				propPlayPickupSound(prop, weapon->weaponnum);
			}

			g_ScenarioData.ctc.tokens[weapon->team] = chr->prop;

#if VERSION >= VERSION_JPN_FINAL
			// "%shas the %s Briefcase"
			snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_000_3), scenarioRemoveLineBreaks(mpchr->name, 0), scenarioRemoveLineBreaks(g_BossFile.teamnames[weapon->team], 1));

			// "%shas our\nBriefcase"
			snprintf(text2, sizeof(text2), langGet(L_MPWEAPONS_002), scenarioRemoveLineBreaks(mpchr->name, 0));

			// "Got the %s Briefcase"
			snprintf(text3, sizeof(text3), langGet(L_MPWEAPONS_003), scenarioRemoveLineBreaks(g_BossFile.teamnames[weapon->team], 0));
#elif VERSION >= VERSION_PAL_BETA
			// "%shas the %s Briefcase"
			snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_000_3), mpchr->name, g_BossFile.teamnames[weapon->team]);

			// "%shas our\nBriefcase"
			snprintf(text2, sizeof(text2), langGet(L_MPWEAPONS_002), mpchr->name);

			// "Got the %s Briefcase"
			snprintf(text3, sizeof(text3), langGet(L_MPWEAPONS_003), g_BossFile.teamnames[weapon->team]);
#else
			// "%shas the %s%s"
			snprintf(text1, sizeof(text1), langGet(L_MPWEAPONS_001), mpchr->name, g_BossFile.teamnames[weapon->team], bgunGetShortName(WEAPON_BRIEFCASE2));

			// "%shas our %s"
			snprintf(text2, sizeof(text2), langGet(L_MPWEAPONS_002), mpchr->name, bgunGetShortName(WEAPON_BRIEFCASE2));

			// "Got the %s%s"
			snprintf(text3, sizeof(text3), langGet(L_MPWEAPONS_003), g_BossFile.teamnames[weapon->team], bgunGetShortName(WEAPON_BRIEFCASE2));
#endif

			prevplayernum = g_Vars.currentplayernum;

			for (i = 0; i < MAX_PLAYERS; i++) {
				if (!g_Vars.players[i]) {
					continue;
				}
				setCurrentPlayerNum(i);

#if VERSION >= VERSION_JPN_FINAL
				if (!chr->aibot && i == prevplayernum) {
					hudmsgCreateWithFlags(text3, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
				} else if (weapon->team == g_MpAllChrConfigPtrs[i]->team) {
					hudmsgCreateWithFlags(text2, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
				} else {
					hudmsgCreateWithFlags(text1, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
				}
#else
				if (!chr->aibot && i == prevplayernum) {
					hudmsgCreateWithFlags(text3, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
				} else if (weapon->team == g_MpAllChrConfigPtrs[i]->team) {
					hudmsgCreateWithFlags(text2, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
				} else {
					hudmsgCreateWithFlags(text1, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
				}
#endif
			}

			setCurrentPlayerNum(prevplayernum);

			if (chr->aibot) {
				botinvGiveSingleWeapon(chr, WEAPON_BRIEFCASE2);
				obj->hidden |= OBJHFLAG_DELETING;
				return TICKOP_NONE;
			} else {
				invGiveWeaponsByProp(prop);
#if VERSION >= VERSION_NTSC_1_0
				objFree(obj, false, obj->hidden2 & OBJH2FLAG_CANREGEN);
				return TICKOP_FREE;
#else
				return TICKOP_GIVETOPLAYER;
#endif
			}
		} else {
			// Already holding another team's case, so ignore the pickup
			if (chr->aibot) {
				return TICKOP_NONE;
			}

			return TICKOP_NONE;
		}
	}

	return TICKOP_NONE;
}

/**
 * Handle a token (briefcase) being dropped due to the holder being killed.
 *
 * For CTC, the token is warped back to its home base.
 */
void scenarioHandleDroppedToken(struct chrdata *chr, struct prop *prop)
{
	const mode_rules_plan_t *plan;
	s32 k;

	modeEventRecord(MODE_EVENT_TOKEN_DROPPED, chr ? chr->chrnum : -1);

	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		return;
	}
	/* gamemode-graphs.md slice 4: the plan's drop modules. */
	plan = scenarioPlan(g_MpSetup.scenario);
	scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_TOKEN_DROPPED);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_TOKEN_DROPPED, k);
		if (owner >= 0 && g_MpScenarios[owner].droppedtokenfunc) {
			g_MpScenarios[owner].droppedtokenfunc(chr, prop);
		}
	}
}

/* Capture the Case: a dropped case warps back to its team's base. */
static void ctcDropToken(struct chrdata *chr, struct prop *prop)
{
	s32 i;
	struct weaponobj *weapon = prop->weapon;
	struct defaultobj *obj;
	struct pad pad;
	Mtxf mtx;
	RoomNum rooms[2];

	for (i = 0; i < ARRAYCOUNT(g_ScenarioData.ctc.tokens); i++) {
		if (chr->prop == g_ScenarioData.ctc.tokens[i]) {
			objFreeProjectile(&weapon->base);

			g_ScenarioData.ctc.tokens[i] = prop;
			weapon->team = i;
			obj = prop->obj;

			padUnpack(g_ScenarioData.ctc.spawnpadsperteam[g_ScenarioData.ctc.teamindexes[i]].homepad,
					PADFIELD_POS | PADFIELD_LOOK | PADFIELD_UP | PADFIELD_ROOM, &pad);
			mtx00016d58(&mtx, 0, 0, 0, -pad.look.x, -pad.look.y, -pad.look.z, pad.up.x, pad.up.y, pad.up.z);

			if (obj->model) {
				mtx00015f04(obj->model->scale, &mtx);
			}

			rooms[0] = pad.room;
			rooms[1] = -1;

			func0f06a730(obj, &pad.pos, &mtx, rooms, &pad.pos);
		}
	}
}

/**
 * Handle a player or bot picking up a data uplink, regardless of which scenario
 * it is.
 *
 * The return value is a TICKOP constant.
 */
s32 scenarioPickUpUplink(struct chrdata *chr, struct prop *prop)
{
	return scenarioPlanPickUpToken(chr, prop, WEAPON_DATAUPLINK);
}

/* Hacker Central: the data uplink is picked up. */
static s32 htmPickUpToken(struct chrdata *chr, struct prop *prop)
{
	s32 i;
	char message[64];
	struct mpchrconfig *mpchr;
	u32 playernum;
#if VERSION >= VERSION_NTSC_1_0
	struct defaultobj *obj = prop->obj;
#endif

	g_ScenarioData.htm.uplink = chr->prop;

	if (chr->aibot) {
		mpchr = g_MpAllChrConfigPtrs[mpPlayerGetIndex(chr)];
	} else {
		mpchr = MPCHR(g_Vars.playerstats[g_Vars.currentplayernum].mpindex);
	}

#if VERSION >= VERSION_JPN_FINAL
	// "%shas the\nData Uplink%s"
	snprintf(message, sizeof(message), langGet(L_MPWEAPONS_000), scenarioRemoveLineBreaks(mpchr->name, 0));
#elif VERSION >= VERSION_PAL_BETA
	// "%shas the\nData Uplink%s"
	snprintf(message, sizeof(message), langGet(L_MPWEAPONS_000), mpchr->name);
#else
	// "%shas the\n%s"
	snprintf(message, sizeof(message), langGet(L_MPWEAPONS_000), mpchr->name, bgunGetShortName(WEAPON_DATAUPLINK));
#endif
	playernum = g_Vars.currentplayernum;

	for (i = 0; i < MAX_PLAYERS; i++) {
		if (!g_Vars.players[i]) {
			continue;
		}
		if (chr->aibot || i != playernum) {
			setCurrentPlayerNum(i);

#if VERSION >= VERSION_JPN_FINAL
			hudmsgCreateWithFlags(message, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE | HUDMSGFLAG_NOWRAP);
#else
			hudmsgCreateWithFlags(message, HUDMSGTYPE_MPSCENARIO, HUDMSGFLAG_ONLYIFALIVE);
#endif
		}
	}

	setCurrentPlayerNum(playernum);

	if (chr->aibot) {
		propPlayPickupSound(prop, WEAPON_DATAUPLINK);
		botinvGiveSingleWeapon(chr, WEAPON_DATAUPLINK);
		chr->aibot->hasuplink = true;

#if VERSION >= VERSION_NTSC_1_0
		obj->hidden |= OBJHFLAG_DELETING;
#else
		prop->obj->hidden |= OBJHFLAG_DELETING;
#endif

		return TICKOP_NONE;
	} else {
		invGiveSingleWeapon(WEAPON_DATAUPLINK);
		currentPlayerQueuePickupWeaponHudmsg(WEAPON_DATAUPLINK, false);
		weaponPlayPickupSound(WEAPON_DATAUPLINK);

#if VERSION >= VERSION_NTSC_1_0
		objFree(obj, false, obj->hidden2 & OBJH2FLAG_CANREGEN);
		return TICKOP_FREE;
#else
		return TICKOP_GIVETOPLAYER;
#endif

	}
}

/**
 * Handle a terminal being activated with the data uplink.
 */
void scenarioHandleActivatedProp(struct chrdata *chr, struct prop *prop)
{
	const mode_rules_plan_t *plan;
	s32 k;

	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		return;
	}
	/* gamemode-graphs.md slice 4: the plan's modules for a used object. */
	plan = scenarioPlan(g_MpSetup.scenario);
	scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_PROP_ACTIVATED);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_PROP_ACTIVATED, k);
		if (owner >= 0 && g_MpScenarios[owner].activatedpropfunc) {
			g_MpScenarios[owner].activatedpropfunc(chr, prop);
		}
	}
}

/* Hacker Central: the terminal is used with the data uplink. */
static void htmActivateProp(struct chrdata *chr, struct prop *prop)
{
	struct defaultobj *obj = prop->obj;

	if (obj->flags3 & OBJFLAG3_HTMTERMINAL) {
		u32 mpindex = mpPlayerGetIndex(chr);

		modeEventRecord(MODE_EVENT_PROP_ACTIVATED, chr ? chr->chrnum : -1);

		if ((obj->hidden & OBJHFLAG_ACTIVATED_BY_BOND) == 0) {
			obj->hidden &= 0x0fffffff;
			obj->hidden |= (mpindex << 28) & 0xf0000000;
			obj->hidden |= OBJHFLAG_ACTIVATED_BY_BOND;
		}
	}
}

/**
 * gamemode-graphs.md slice 7: a simulant on a team with no humans picks its
 * command (bot.c, every 20 to 60 seconds): the plan's first command module.
 */
void scenarioBotChooseCommand(struct chrdata *chr, s32 teamsize)
{
	const mode_rules_plan_t *plan;
	s32 k;

	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		return;
	}
	plan = scenarioPlan(g_MpSetup.scenario);
	scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_BOT_COMMAND);
	modeEventRecord(MODE_EVENT_BOT_COMMAND, chr ? chr->chrnum : -1);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_BOT_COMMAND, k);
		if (owner >= 0 && g_MpScenarios[owner].botcommandfunc) {
			g_MpScenarios[owner].botcommandfunc(chr, teamsize);
			return;
		}
	}
}

/**
 * gamemode-graphs.md slice 7: what a simulant with no command does for the
 * mode (follow a teammate with the briefcase, take a case home, go to the
 * terminal): the first idle module of the plan that finds an action, or -1
 * (the simulant then fights as in Combat).
 */
s32 scenarioBotIdleAction(struct chrdata *chr)
{
	const mode_rules_plan_t *plan;
	s32 k;

	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		return -1;
	}
	plan = scenarioPlan(g_MpSetup.scenario);
	scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_BOT_IDLE);
	modeEventRecord(MODE_EVENT_BOT_IDLE, chr ? chr->chrnum : -1);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_BOT_IDLE, k);
		if (owner >= 0 && g_MpScenarios[owner].botidlefunc) {
			const s32 action = g_MpScenarios[owner].botidlefunc(chr);
			if (action >= 0) {
				return action;
			}
		}
	}

	return -1;
}

/**
 * gamemode-graphs.md slice 4: a player or simulant died in a match (Pop a
 * Cap's victim). aplayernum and vplayernum are the Combat Simulator indexes of
 * the attacker and the victim.
 */
void scenarioHandlePlayerDied(s32 aplayernum, s32 vplayernum)
{
	const mode_rules_plan_t *plan;
	s32 k;

	if (!scenarioIndexIsValid(g_MpSetup.scenario)) {
		return;
	}
	plan = scenarioPlan(g_MpSetup.scenario);
	scenarioPlanCompare(g_MpSetup.scenario, plan, MODE_SEAM_PLAYER_DIED);
	for (k = 0; k < MODE_PLAN_MAX_PER_SEAM; k++) {
		const s32 owner = scenarioPlanOwner(plan, MODE_SEAM_PLAYER_DIED, k);
		if (owner >= 0 && g_MpScenarios[owner].playerdiedfunc) {
			g_MpScenarios[owner].playerdiedfunc(aplayernum, vplayernum);
		}
	}
}
