/**
 * matchsetup.c -- Clean match start function for the new lobby system.
 *
 * Bypasses the old menutick.c dialog-stack flow entirely.
 * Configures g_MpSetup, chrslots, player configs, and bot configs
 * directly from lobby state, then calls mpStartMatch() and triggers
 * stage load.
 *
 * Also defines g_MatchSetupMenuDialog for hotswap registration.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "system.h"
#include "net/net.h"
#include "net/netlobby.h"
#include "game/menu.h"
#include "game/mplayer/mplayer.h"
#include "game/challenge.h"
#include "romdata.h"
#include "modelcatalog.h"
#include "assetcatalog.h"
#include "game/mplayer/participant.h"
#include "net/matchsetup.h"
#include "input.h"
#include "pdmain.h"
#include <stdlib.h>

/* ========================================================================
 * Dialog definition for hotswap
 * ======================================================================== */

/* Minimal menu item list — the actual rendering is done by ImGui via hotswap */
static struct menuitem g_MatchSetupMenuItems[] = {
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_MatchSetupMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Match Setup",
	g_MatchSetupMenuItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_STARTSELECTS,
	NULL,
};

/* ========================================================================
 * Match slot configuration — types in net/matchsetup.h
 * ======================================================================== */

/* Definition of the match config global (declaration in net/matchsetup.h) */
struct matchconfig g_MatchConfig;

/* ========================================================================
 * Match config initialization
 * ======================================================================== */

void matchConfigInit(void)
{
	memset(&g_MatchConfig, 0, sizeof(g_MatchConfig));

	/* Set up global vars like the old Combat Simulator handler does */
	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;
	g_Vars.mpquickteam = MPQUICKTEAM_NONE;

	/* Default settings
	 *
	 * Engine encoding (see mpApplyLimits in mplayer.c):
	 *   timelimit:  (value + 1) * 60 seconds.  0 = 1 min, 9 = 10 min, >=60 = no limit.
	 *   scorelimit: value + 1 kills.  0 = 1 kill, 9 = 10 kills, >=100 = no limit.
	 *   teamscorelimit: similar, >=400 = no limit. */
	g_MatchConfig.scenario = MPSCENARIO_COMBAT;
	/* stage_id is PRIMARY — resolve stagenum from it at matchStart().
	 * Default arena: Complex ("base:mp_complex"). */
	strncpy(g_MatchConfig.stage_id, "base:mp_complex", sizeof(g_MatchConfig.stage_id) - 1);
	g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
	{
		const asset_entry_t *ae = assetCatalogResolve(g_MatchConfig.stage_id);
		g_MatchConfig.stagenum = (ae && ae->type == ASSET_ARENA)
		    ? (u8)ae->ext.arena.stagenum : 0u;
	}
	g_MatchConfig.timelimit = 60;     /* no time limit (>=60 disables timer) */
	g_MatchConfig.scorelimit = 9;     /* first to 10 kills */
	g_MatchConfig.teamscorelimit = 400; /* no team score limit */
	/* F.6/B-70: Default spawn-with-weapon ON so bots and players always start armed. */
	g_MatchConfig.options = MPOPTION_SPAWNWITHWEAPON;
	g_MatchConfig.weaponSetIndex = 0;   /* default to first available preset (Pistols) */
	g_MatchConfig.spawnWeaponNum = 0xFF; /* Random = use weapons[0] from active set */
	g_MatchConfig.numSlots = 0;

	/* Apply the default weapon set so g_MpSetup.weapons[] is populated.
	 * mpSetWeaponSet() maps the user-facing index through the unlock filter
	 * and calls mpApplyWeaponSet() to fill the 6 weapon slots. */
	mpSetWeaponSet(g_MatchConfig.weaponSetIndex);

	/* Slot 0 = local player — use agent name from save file */
	struct matchslot *s0 = &g_MatchConfig.slots[0];
	s0->type = SLOT_PLAYER;
	s0->team = 0;
	/* Resolve catalog IDs from mpbodynum/mpheadnum — these are the PRIMARY identity. */
	{
		const u8 mpbody = g_PlayerConfigsArray[0].base.mpbodynum;
		const u8 mphead = g_PlayerConfigsArray[0].base.mpheadnum;
		const char *bid = catalogResolveBodyByMpIndex((s32)mpbody);
		const char *hid = catalogResolveHeadByMpIndex((s32)mphead);
		strncpy(s0->body_id,
		        bid ? bid : "base:dark_combat",
		        sizeof(s0->body_id) - 1);
		s0->body_id[sizeof(s0->body_id) - 1] = '\0';
		strncpy(s0->head_id,
		        hid ? hid : "base:head_dark_combat",
		        sizeof(s0->head_id) - 1);
		s0->head_id[sizeof(s0->head_id) - 1] = '\0';
		/* Cache derived mpbodynum/mpheadnum — matchStart re-derives but keep in sync */
		s0->bodynum = mpbody;
		s0->headnum = mphead;
	}

	/* Get the agent name from g_GameFile (loaded from save data).
	 * The old menu flow copies this via dialog handlers, but since we
	 * bypass those menus entirely, we pull it directly.
	 *
	 * NOTE: g_GameFile.name is still 11 chars (legacy save format).
	 * Our matchslot.name supports up to MAX_PLAYER_NAME (32) chars —
	 * the longer capacity is used for network names and future save
	 * format upgrades. For now we read what the save provides. */
	if (g_GameFile.name[0] != '\0') {
		strncpy(s0->name, g_GameFile.name, MAX_PLAYER_NAME - 1);
		s0->name[MAX_PLAYER_NAME - 1] = '\0';
		/* Also update the engine's player config (still 15 char limit internally) */
		strncpy(g_PlayerConfigsArray[0].base.name, g_GameFile.name, 11);
		/* PD names are terminated with \n then \0 */
		g_PlayerConfigsArray[0].base.name[strlen(g_GameFile.name)] = '\n';
		g_PlayerConfigsArray[0].base.name[strlen(g_GameFile.name) + 1] = '\0';
	} else {
		strncpy(s0->name, g_PlayerConfigsArray[0].base.name, MAX_PLAYER_NAME - 1);
		s0->name[MAX_PLAYER_NAME - 1] = '\0';
	}
	g_MatchConfig.numSlots = 1;

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: config initialized — player '%s' body_id='%s' head_id='%s'",
	             s0->name, s0->body_id, s0->head_id);
}

/* ========================================================================
 * Bot name + character randomization
 * ======================================================================== */

static const char *s_BotAdjectives[] = {
	"Bumbling","Crusty","Dopey","Greasy","Lumpy","Manky","Mushy","Nasal",
	"Pudgy","Queasy","Rancid","Soggy","Wonky","Gassy","Clammy","Blobby",
	"Grumpy","Salty","Wheezy","Clunky","Funky","Lanky","Squishy","Burpy",
	"Drippy","Cranky","Slippery","Stinky","Wobbly","Dizzy","Rusty","Floppy",
};
#define NUM_BOT_ADJECTIVES (sizeof(s_BotAdjectives) / sizeof(s_BotAdjectives[0]))

static const char *s_BotNames[] = {
	"Tud","Rodrick","Frunge","Stanley","Buttersworth","Jenkins","Gorp",
	"Blimpo","Sneed","Winkle","Gribble","Plonk","Dingle","Spudge",
	"Crambo","Muggins","Dorkus","Flimble","Noodge","Bumstead",
	"Cletus","Gormley","Pickles","Barnaby","Squib","Thudwick",
};
#define NUM_BOT_NAMES (sizeof(s_BotNames) / sizeof(s_BotNames[0]))

static void generateBotName(char *dst, s32 maxLen)
{
	s32 ai = rand() % NUM_BOT_ADJECTIVES;
	s32 ni = rand() % NUM_BOT_NAMES;
	snprintf(dst, maxLen, "%s %s", s_BotAdjectives[ai], s_BotNames[ni]);
}

static void pickRandomBodyHead(char *body_id, s32 bodyLen, char *head_id, s32 headLen)
{
	u32 numBodies = mpGetNumBodies();
	if (numBodies == 0) {
		strncpy(body_id, "base:dark_combat", bodyLen - 1);
		body_id[bodyLen - 1] = '\0';
		strncpy(head_id, "base:head_dark_combat", headLen - 1);
		head_id[headLen - 1] = '\0';
		return;
	}

	/* Try up to 10 times to avoid duplicate body with existing bots */
	const char *picked_body = NULL;
	for (s32 attempt = 0; attempt < 10; attempt++) {
		u32 idx = rand() % numBodies;
		picked_body = catalogResolveBodyByMpIndex(idx);
		if (!picked_body || !picked_body[0]) continue;

		/* Check for duplicates among existing slots */
		bool dup = false;
		for (s32 s = 0; s < g_MatchConfig.numSlots; s++) {
			if (g_MatchConfig.slots[s].type != SLOT_EMPTY &&
			    strcmp(g_MatchConfig.slots[s].body_id, picked_body) == 0) {
				dup = true;
				break;
			}
		}
		if (!dup || attempt == 9) break;
		picked_body = NULL;
	}

	if (!picked_body || !picked_body[0]) {
		picked_body = "base:dark_combat";
	}

	strncpy(body_id, picked_body, bodyLen - 1);
	body_id[bodyLen - 1] = '\0';

	/* Pair with default head for this body */
	const char *paired_head = catalogGetBodyDefaultHead(picked_body);
	if (paired_head && paired_head[0]) {
		strncpy(head_id, paired_head, headLen - 1);
	} else {
		strncpy(head_id, "base:head_dark_combat", headLen - 1);
	}
	head_id[headLen - 1] = '\0';
}

/* ========================================================================
 * Slot management — called from ImGui bridge
 * ======================================================================== */

s32 matchConfigAddBot(u8 botType, u8 botDifficulty, const char *body_id,
                      const char *head_id, const char *name)
{
	if (g_MatchConfig.numSlots >= MATCH_MAX_SLOTS) {
		return -1;
	}

	s32 idx = g_MatchConfig.numSlots;
	struct matchslot *slot = &g_MatchConfig.slots[idx];
	slot->type = SLOT_BOT;
	slot->botType = botType;
	slot->botDifficulty = botDifficulty;
	slot->team = 0;

	/* Set catalog IDs as PRIMARY identity. Random if not specified. */
	if (body_id && body_id[0]) {
		strncpy(slot->body_id, body_id, sizeof(slot->body_id) - 1);
		slot->body_id[sizeof(slot->body_id) - 1] = '\0';
		strncpy(slot->head_id,
		        (head_id && head_id[0]) ? head_id : "base:head_dark_combat",
		        sizeof(slot->head_id) - 1);
		slot->head_id[sizeof(slot->head_id) - 1] = '\0';
	} else {
		pickRandomBodyHead(slot->body_id, sizeof(slot->body_id),
		                   slot->head_id, sizeof(slot->head_id));
	}

	/* Derive cached mpbodynum/mpheadnum from catalog for legacy path.
	 * matchStart() re-derives at the last moment; these are just for display. */
	slot->bodynum = 0; /* MPBODY_DARK_COMBAT default */
	slot->headnum = 0; /* MPHEAD_DARK_COMBAT default */
	{
		const asset_entry_t *be = assetCatalogResolve(slot->body_id);
		if (be && be->type == ASSET_BODY) {
			const s32 mpb = catalogBodynumToMpBodyIdx(be->runtime_index);
			if (mpb >= 0) slot->bodynum = (u8)mpb;
		}
	}
	{
		const asset_entry_t *he = assetCatalogResolve(slot->head_id);
		if (he && he->type == ASSET_HEAD) {
			const s32 mph = catalogHeadnumToMpHeadIdx(he->runtime_index);
			if (mph >= 0) slot->headnum = (u8)mph;
		}
	}

	if (name && name[0]) {
		strncpy(slot->name, name, MAX_PLAYER_NAME - 1);
		slot->name[MAX_PLAYER_NAME - 1] = '\0';
	} else {
		generateBotName(slot->name, MAX_PLAYER_NAME);
	}

	g_MatchConfig.numSlots++;
	return idx;
}

s32 matchConfigRemoveSlot(s32 idx)
{
	if (idx < 1 || idx >= g_MatchConfig.numSlots) {
		return -1; /* Can't remove slot 0 (local player) or invalid */
	}

	/* Shift remaining slots down */
	for (s32 i = idx; i < g_MatchConfig.numSlots - 1; i++) {
		g_MatchConfig.slots[i] = g_MatchConfig.slots[i + 1];
	}

	g_MatchConfig.numSlots--;
	memset(&g_MatchConfig.slots[g_MatchConfig.numSlots], 0, sizeof(struct matchslot));
	return 0;
}

void matchConfigRerollBot(s32 idx)
{
	if (idx < 1 || idx >= g_MatchConfig.numSlots) return;
	struct matchslot *sl = &g_MatchConfig.slots[idx];
	if (sl->type != SLOT_BOT) return;

	generateBotName(sl->name, MAX_PLAYER_NAME);
	pickRandomBodyHead(sl->body_id, sizeof(sl->body_id),
	                   sl->head_id, sizeof(sl->head_id));

	/* Re-derive cached mpbodynum/mpheadnum */
	sl->bodynum = 0;
	sl->headnum = 0;
	const asset_entry_t *be = assetCatalogResolve(sl->body_id);
	if (be && be->type == ASSET_BODY) {
		s32 mpb = catalogBodynumToMpBodyIdx(be->runtime_index);
		if (mpb >= 0) sl->bodynum = (u8)mpb;
	}
	const asset_entry_t *he = assetCatalogResolve(sl->head_id);
	if (he && he->type == ASSET_HEAD) {
		s32 mph = catalogHeadnumToMpHeadIdx(he->runtime_index);
		if (mph >= 0) sl->headnum = (u8)mph;
	}
}

/* ========================================================================
 * Match start — the clean replacement for the old menutick flow
 * ======================================================================== */

s32 matchStart(void)
{
	sysLogPrintf(LOG_NOTE, "MATCHSETUP: starting match — %d slots, scenario=%d stage='%s'",
	             g_MatchConfig.numSlots, g_MatchConfig.scenario, g_MatchConfig.stage_id);

	/* --- Set up global vars like the old handler does --- */
	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;

	challengeDetermineUnlockedFeatures();

	/* --- Configure g_MpSetup from our match config --- */
	g_MpSetup.scenario = g_MatchConfig.scenario;

	/* Resolve stagenum from stage_id (PRIMARY). stage_id may refer to an ASSET_ARENA
	 * (MP arena) or ASSET_MAP (co-op/counter-op mission). */
	{
		const asset_entry_t *ae = assetCatalogResolve(g_MatchConfig.stage_id);
		if (ae && ae->type == ASSET_ARENA) {
			g_MpSetup.stagenum = (u8)ae->ext.arena.stagenum;
		} else if (ae && ae->type == ASSET_MAP) {
			g_MpSetup.stagenum = (u8)ae->ext.map.stagenum;
		} else {
			sysLogPrintf(LOG_ERROR,
			    "MATCHSETUP: cannot resolve stage '%s' — aborting",
			    g_MatchConfig.stage_id);
			return -1;
		}
		sysLogPrintf(LOG_NOTE, "MATCHSETUP: stage '%s' → stagenum=0x%02x",
		             g_MatchConfig.stage_id, g_MpSetup.stagenum);
	}
	g_MpSetup.timelimit = g_MatchConfig.timelimit;
	g_MpSetup.scorelimit = g_MatchConfig.scorelimit;
	g_MpSetup.teamscorelimit = g_MatchConfig.teamscorelimit;
	g_MpSetup.options = g_MatchConfig.options;

	/* Re-apply the selected weapon set — this populates g_MpSetup.weapons[]
	 * through the engine's own mpApplyWeaponSet(). This handles presets,
	 * random, random-five, and custom sets correctly. */
	mpSetWeaponSet(g_MatchConfig.weaponSetIndex);
	sysLogPrintf(LOG_NOTE, "MATCHSETUP: weapon set %d applied — slots: %d %d %d %d %d %d",
	             g_MatchConfig.weaponSetIndex,
	             g_MpSetup.weapons[0], g_MpSetup.weapons[1], g_MpSetup.weapons[2],
	             g_MpSetup.weapons[3], g_MpSetup.weapons[4], g_MpSetup.weapons[5]);

	/* --- Build chrslots bitmask and configure player/bot arrays --- */
	g_MpSetup.chrslots = 0;
	mpClearAllParticipants(); /* B-12 Phase 2 */
	s32 playerSlot = 0;
	s32 botSlot = 0;

	for (s32 i = 0; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
		struct matchslot *ms = &g_MatchConfig.slots[i];

		if (ms->type == SLOT_PLAYER && playerSlot < MAX_PLAYERS) {
			g_MpSetup.chrslots |= (1ull << playerSlot);
			mpAddParticipantAt(playerSlot, PARTICIPANT_LOCAL, ms->team, 0, (u8)playerSlot); /* B-12 Phase 2 */

			struct mpchrconfig *cfg = &g_PlayerConfigsArray[playerSlot].base;

			/* Resolve mpbodynum/mpheadnum from body_id/head_id (PRIMARY identity).
			 * catalogBodynumToMpBodyIdx/catalogHeadnumToMpHeadIdx are the ONLY valid
			 * integer-domain conversion — called here at the last moment before
			 * handing off to the legacy engine. */
			if (ms->body_id[0]) {
				const asset_entry_t *be = assetCatalogResolve(ms->body_id);
				if (be && be->type == ASSET_BODY) {
					const s32 mpb = catalogBodynumToMpBodyIdx(be->runtime_index);
					cfg->mpbodynum = (mpb >= 0) ? (u8)mpb : 0u;
				} else {
					cfg->mpbodynum = ms->bodynum; /* cached fallback */
				}
			} else {
				cfg->mpbodynum = ms->bodynum;
			}
			if (ms->head_id[0]) {
				const asset_entry_t *he = assetCatalogResolve(ms->head_id);
				if (he && he->type == ASSET_HEAD) {
					const s32 mph = catalogHeadnumToMpHeadIdx(he->runtime_index);
					cfg->mpheadnum = (mph >= 0) ? (u8)mph : 0u;
				} else {
					cfg->mpheadnum = ms->headnum; /* cached fallback */
				}
			} else {
				cfg->mpheadnum = ms->headnum;
			}
			cfg->team = ms->team;

			strncpy(cfg->name, ms->name, 14);
			cfg->name[14] = '\0';

			sysLogPrintf(LOG_NOTE,
			    "MATCHSETUP: player slot %d: %s body='%s' head='%s' mpbody=%d mphead=%d team=%d",
			    playerSlot, cfg->name, ms->body_id, ms->head_id,
			    cfg->mpbodynum, cfg->mpheadnum, ms->team);
			playerSlot++;

		} else if (ms->type == SLOT_BOT && botSlot < MAX_BOTS) {
			g_MpSetup.chrslots |= (1ull << (botSlot + BOT_SLOT_OFFSET));
			mpAddParticipantAt(botSlot + BOT_SLOT_OFFSET, PARTICIPANT_BOT, ms->team, -1, 0xFF); /* B-12 Phase 2 */

			struct mpbotconfig *bot = &g_BotConfigsArray[botSlot];

			/* Same last-moment resolution from body_id/head_id (PRIMARY). */
			if (ms->body_id[0]) {
				const asset_entry_t *be = assetCatalogResolve(ms->body_id);
				if (be && be->type == ASSET_BODY) {
					const s32 mpb = catalogBodynumToMpBodyIdx(be->runtime_index);
					bot->base.mpbodynum = (mpb >= 0) ? (u8)mpb : 0u;
				} else {
					bot->base.mpbodynum = ms->bodynum;
				}
			} else {
				bot->base.mpbodynum = ms->bodynum;
			}
			if (ms->head_id[0]) {
				const asset_entry_t *he = assetCatalogResolve(ms->head_id);
				if (he && he->type == ASSET_HEAD) {
					const s32 mph = catalogHeadnumToMpHeadIdx(he->runtime_index);
					bot->base.mpheadnum = (mph >= 0) ? (u8)mph : 0u;
				} else {
					bot->base.mpheadnum = ms->headnum;
				}
			} else {
				bot->base.mpheadnum = ms->headnum;
			}
			bot->base.team = ms->team;
			bot->type = ms->botType;
			bot->difficulty = ms->botDifficulty;

			strncpy(bot->base.name, ms->name, 14);
			bot->base.name[14] = '\0';

			sysLogPrintf(LOG_NOTE,
			    "MATCHSETUP: bot slot %d: %s type=%d diff=%d body='%s' head='%s' mpbody=%d mphead=%d",
			    botSlot, bot->base.name, ms->botType, ms->botDifficulty,
			    ms->body_id, ms->head_id,
			    bot->base.mpbodynum, bot->base.mpheadnum);
			botSlot++;
		}
	}

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: chrslots=0x%016llx (%d players, %d bots)",
	             (unsigned long long)g_MpSetup.chrslots, playerSlot, botSlot);

	if (playerSlot == 0) {
		sysLogPrintf(LOG_WARNING, "MATCHSETUP: no players configured — aborting");
		return -1;
	}

	/* --- Free ROM data and start --- */
	g_NotLoadMod = false;
	romdataFileFreeForSolo();

	/* Call mpStartMatch which handles weapon randomization,
	 * quick team sims, random stage, etc. */
	mpStartMatch();

	/* Stop the menu system and let the game take over */
	menuStop();

	/* B-66: Capture mouse for gameplay. The lobby UI holds pdguiIsActive() true
	 * during setup, which deferred the SDL relative-mouse apply inside
	 * inputLockMouse(). Now that menus are stopped, force the capture. */
	inputLockMouse(1);
	pdmainSetInputMode(INPUTMODE_GAMEPLAY);

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: match started successfully");
	return 0;
}

/* ========================================================================
 * Handicap accessors (wrap g_PlayerConfigsArray without exposing types.h)
 * ======================================================================== */

u8 matchGetPlayerHandicap(s32 playernum)
{
	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return 0x80;
	}
	return g_PlayerConfigsArray[playernum].handicap;
}

void matchSetPlayerHandicap(s32 playernum, u8 val)
{
	if (playernum >= 0 && playernum < MAX_PLAYERS) {
		g_PlayerConfigsArray[playernum].handicap = val;
	}
}

void matchResetHandicaps(void)
{
	s32 i;
	for (i = 0; i < MAX_PLAYERS; i++) {
		g_PlayerConfigsArray[i].handicap = 0x80;
	}
}

/* ========================================================================
 * Challenge start — sets the challenge, bypasses g_MatchConfig → g_MpSetup
 * copy so the challenge's own config (stage, bots, weapons) survives intact.
 * ======================================================================== */

s32 matchStartFromChallenge(s32 slot)
{
	sysLogPrintf(LOG_NOTE, "MATCHSETUP: starting challenge slot %d", slot);

	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;

	/* Apply challenge config → sets g_MpSetup (scenario, stage, bots, etc.) */
	challengeSetCurrentBySlot(slot);

	/* Sync back the challenge's stage into g_MatchConfig so our port-side
	 * tracking stays consistent (arena picker, etc.).
	 * stagenum comes from the challenge; resolve to catalog ID for stage_id. */
	g_MatchConfig.stagenum = (u8)g_MpSetup.stagenum;
	g_MatchConfig.scenario = (u8)g_MpSetup.scenario;
	{
		/* Use catalog ID directly — stage_id is the primary key */
		if (g_MpSetup.stage_id[0]) {
			strncpy(g_MatchConfig.stage_id, g_MpSetup.stage_id, sizeof(g_MatchConfig.stage_id) - 1);
			g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
		} else {
			g_MatchConfig.stage_id[0] = '\0';
		}
	}

	g_NotLoadMod = false;
	romdataFileFreeForSolo();

	/* Start directly — g_MpSetup already fully configured by challengeApply() */
	mpStartMatch();
	menuStop();

	/* B-92 sibling: challenge start path was missing the mouse capture that
	 * matchStartFromSetup applies.  pdguiIsActive() deferred the SDL
	 * relative-mouse apply inside inputLockMouse(); force it now. */
	inputLockMouse(1);
	pdmainSetInputMode(INPUTMODE_GAMEPLAY);

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: challenge match started");
	return 0;
}
