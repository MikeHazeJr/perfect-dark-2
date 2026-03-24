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
 * Match slot configuration — set by the ImGui lobby UI
 * ======================================================================== */

/* PC port: longer names — no N64 Controller Pak constraints */
#define MAX_PLAYER_NAME 32

/* Slot types */
#define SLOT_EMPTY    0
#define SLOT_PLAYER   1
#define SLOT_BOT      2

struct matchslot {
	u8 type;          /* SLOT_EMPTY, SLOT_PLAYER, SLOT_BOT */
	u8 team;          /* team number (0-7) */
	u8 headnum;       /* character head */
	u8 bodynum;       /* character body */
	u8 botType;       /* BOTTYPE_* (only for SLOT_BOT) */
	u8 botDifficulty; /* BOTDIFF_* (only for SLOT_BOT) */
	char name[MAX_PLAYER_NAME];  /* display name (PC: 32 chars, no N64 limit) */
};

#define MATCH_MAX_SLOTS MAX_MPCHRS

struct matchconfig {
	struct matchslot slots[MATCH_MAX_SLOTS];
	u8 scenario;                    /* MPSCENARIO_* */
	u8 stagenum;                    /* stage index */
	u8 timelimit;                   /* minutes (0 = unlimited) */
	u8 scorelimit;                  /* score to win (0 = unlimited) */
	u16 teamscorelimit;             /* team score limit */
	u32 options;                    /* MPOPTION_* bitmask */
	u8 weapons[NUM_MPWEAPONSLOTS];  /* weapon set (6 slots) */
	s8 weaponSetIndex;              /* -1 = custom, 0+ = preset index */
	u8 numSlots;                    /* number of active slots */
};

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
	g_MatchConfig.stagenum = STAGE_MP_COMPLEX;
	g_MatchConfig.timelimit = 60;     /* no time limit (>=60 disables timer) */
	g_MatchConfig.scorelimit = 9;     /* first to 10 kills */
	g_MatchConfig.teamscorelimit = 400; /* no team score limit */
	g_MatchConfig.options = 0;
	g_MatchConfig.weaponSetIndex = 0;   /* default to first available preset (Pistols) */
	g_MatchConfig.numSlots = 0;

	/* Apply the default weapon set so g_MpSetup.weapons[] is populated.
	 * mpSetWeaponSet() maps the user-facing index through the unlock filter
	 * and calls mpApplyWeaponSet() to fill the 6 weapon slots. */
	mpSetWeaponSet(g_MatchConfig.weaponSetIndex);

	/* Slot 0 = local player — use agent name from save file */
	struct matchslot *s0 = &g_MatchConfig.slots[0];
	s0->type = SLOT_PLAYER;
	s0->team = 0;
	s0->headnum = g_PlayerConfigsArray[0].base.mpheadnum;
	s0->bodynum = g_PlayerConfigsArray[0].base.mpbodynum;

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

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: config initialized — player '%s' body=%d head=%d",
	             s0->name, s0->bodynum, s0->headnum);
}

/* ========================================================================
 * Slot management — called from ImGui bridge
 * ======================================================================== */

s32 matchConfigAddBot(u8 botType, u8 botDifficulty, u8 headnum, u8 bodynum, const char *name)
{
	if (g_MatchConfig.numSlots >= MATCH_MAX_SLOTS) {
		return -1;
	}

	s32 idx = g_MatchConfig.numSlots;
	struct matchslot *slot = &g_MatchConfig.slots[idx];
	slot->type = SLOT_BOT;
	slot->botType = botType;
	slot->botDifficulty = botDifficulty;
	slot->headnum = headnum;
	slot->bodynum = bodynum;
	slot->team = 0;

	if (name && name[0]) {
		strncpy(slot->name, name, MAX_PLAYER_NAME - 1);
		slot->name[MAX_PLAYER_NAME - 1] = '\0';
	} else {
		/* Generate default name from bot type */
		static const char *botTypeNames[] = {
			"NormalSim", "PeaceSim", "ShieldSim", "RocketSim",
			"KazeSim", "FistSim", "PreySim", "CowardSim",
			"JudgeSim", "FeudSim", "SpeedSim", "TurtleSim", "VengeSim"
		};
		if (botType < ARRAYCOUNT(botTypeNames)) {
			strncpy(slot->name, botTypeNames[botType], MAX_PLAYER_NAME - 1);
		} else {
			snprintf(slot->name, MAX_PLAYER_NAME, "Bot %d", idx);
		}
		slot->name[MAX_PLAYER_NAME - 1] = '\0';
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

/* ========================================================================
 * Match start — the clean replacement for the old menutick flow
 * ======================================================================== */

s32 matchStart(void)
{
	sysLogPrintf(LOG_NOTE, "MATCHSETUP: starting match — %d slots, scenario=%d stage=0x%02x",
	             g_MatchConfig.numSlots, g_MatchConfig.scenario, g_MatchConfig.stagenum);

	/* --- Set up global vars like the old handler does --- */
	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;

	challengeDetermineUnlockedFeatures();

	/* --- Configure g_MpSetup from our match config --- */
	g_MpSetup.scenario = g_MatchConfig.scenario;
	g_MpSetup.stagenum = g_MatchConfig.stagenum;
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
	s32 playerSlot = 0;
	s32 botSlot = 0;

	for (s32 i = 0; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
		struct matchslot *ms = &g_MatchConfig.slots[i];

		if (ms->type == SLOT_PLAYER && playerSlot < MAX_PLAYERS) {
			/* Configure player — use catalog for safe body/head indices */
			g_MpSetup.chrslots |= (1u << playerSlot);

			struct mpchrconfig *cfg = &g_PlayerConfigsArray[playerSlot].base;
			cfg->mpheadnum = catalogGetSafeHead(ms->headnum);
			cfg->mpbodynum = catalogGetSafeBody(ms->bodynum);
			cfg->team = ms->team;

			/* Name: keep the first 14 chars + newline as PD expects */
			strncpy(cfg->name, ms->name, 14);
			cfg->name[14] = '\0';

			sysLogPrintf(LOG_NOTE, "MATCHSETUP: player slot %d: %s body=%d→%d head=%d→%d team=%d",
			             playerSlot, cfg->name, ms->bodynum, cfg->mpbodynum,
			             ms->headnum, cfg->mpheadnum, ms->team);
			playerSlot++;

		} else if (ms->type == SLOT_BOT && botSlot < MAX_BOTS) {
			/* Configure bot — use catalog for safe body/head indices */
			g_MpSetup.chrslots |= (1u << (botSlot + BOT_SLOT_OFFSET));

			struct mpbotconfig *bot = &g_BotConfigsArray[botSlot];
			bot->base.mpheadnum = catalogGetSafeHead(ms->headnum);
			bot->base.mpbodynum = catalogGetSafeBody(ms->bodynum);
			bot->base.team = ms->team;
			bot->type = ms->botType;
			bot->difficulty = ms->botDifficulty;

			strncpy(bot->base.name, ms->name, 14);
			bot->base.name[14] = '\0';

			sysLogPrintf(LOG_NOTE, "MATCHSETUP: bot slot %d: %s type=%d diff=%d body=%d→%d head=%d→%d team=%d",
			             botSlot, bot->base.name, ms->botType, ms->botDifficulty,
			             ms->bodynum, bot->base.mpbodynum,
			             ms->headnum, bot->base.mpheadnum, ms->team);
			botSlot++;
		}
	}

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: chrslots=0x%08x (%d players, %d bots)",
	             g_MpSetup.chrslots, playerSlot, botSlot);

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

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: match started successfully");
	return 0;
}
