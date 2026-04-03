/**
 * matchsetup.h -- Shared match-slot types and g_MatchConfig declaration.
 *
 * Shared between matchsetup.c (definition) and netmsg.c (CLC_LOBBY_START
 * per-bot payload). pdgui_menu_room.cpp keeps its own inline copy to avoid
 * pulling C headers into C++ translation units.
 *
 * IMPORTANT: struct layout must stay in sync with pdgui_menu_room.cpp's
 * inline forward declarations.
 */

#pragma once

#include <PR/ultratypes.h>

/* PC port: longer names — no N64 Controller Pak constraints */
#ifndef MAX_PLAYER_NAME
#define MAX_PLAYER_NAME 32
#endif

/* Total match slots = participant pool size.
 * Must equal PARTICIPANT_DEFAULT_CAPACITY (32). */
#ifndef MATCH_MAX_SLOTS
#define MATCH_MAX_SLOTS 32
#endif

/* Weapon slots per match (must match constants.h NUM_MPWEAPONSLOTS) */
#ifndef NUM_MPWEAPONSLOTS
#define NUM_MPWEAPONSLOTS 6
#endif

/* Slot type constants */
#ifndef SLOT_EMPTY
#define SLOT_EMPTY  0
#define SLOT_PLAYER 1
#define SLOT_BOT    2
#endif

struct matchslot {
	u8 type;          /* SLOT_EMPTY, SLOT_PLAYER, SLOT_BOT */
	u8 team;          /* team number (0-7) */
	/* body_id/head_id are the PRIMARY identity — always set by matchConfigInit/
	 * matchConfigAddBot.  bodynum/headnum are DERIVED (mpbodynum/mpheadnum cache)
	 * used only for legacy engine handoff; resolved from body_id/head_id at
	 * matchStart() time via catalogBodynumToMpBodyIdx/catalogHeadnumToMpHeadIdx. */
	char body_id[64]; /* PRIMARY: catalog ID e.g. "base:dark_combat", "base:theking" */
	char head_id[64]; /* PRIMARY: catalog ID e.g. "base:head_dark_combat" */
	u8 headnum;       /* DERIVED: mpheadnum (g_MpHeads[] index) — set by matchStart */
	u8 bodynum;       /* DERIVED: mpbodynum (g_MpBodies[] index) — set by matchStart */
	u8 botType;       /* BOTTYPE_* (only for SLOT_BOT) */
	u8 botDifficulty; /* BOTDIFF_* (only for SLOT_BOT) */
	char name[MAX_PLAYER_NAME];  /* display name */
};

struct matchconfig {
	struct matchslot slots[MATCH_MAX_SLOTS];
	u8 scenario;                    /* MPSCENARIO_* */
	/* PRIMARY: catalog ID string (e.g. "base:mp_complex", "base:defection").
	 * stagenum is DERIVED — resolved from stage_id at matchStart() only. */
	char stage_id[64];              /* PRIMARY: catalog ID — e.g. "base:mp_complex" */
	u8 stagenum;                    /* DERIVED: resolved from stage_id at matchStart */
	u8 timelimit;                   /* minutes (0 = unlimited) */
	u8 scorelimit;                  /* score to win (0 = unlimited) */
	u16 teamscorelimit;             /* team score limit */
	u32 options;                    /* MPOPTION_* bitmask */
	u8 weapons[NUM_MPWEAPONSLOTS];  /* weapon set (6 slots) */
	s8 weaponSetIndex;              /* -1 = custom, 0+ = preset index */
	u8 numSlots;                    /* number of active slots */
	u8 spawnWeaponNum;              /* 0xFF = Random; weapon enum value otherwise */
};

/* Defined in matchsetup.c */
extern struct matchconfig g_MatchConfig;

void matchConfigInit(void);
/* body_id/head_id: catalog IDs (e.g. "base:dark_combat").  Pass NULL/"" to use
 * the default (base:dark_combat / base:head_dark_combat). */
s32 matchConfigAddBot(u8 botType, u8 botDifficulty, const char *body_id,
                      const char *head_id, const char *name);
s32 matchConfigRemoveSlot(s32 idx);
s32 matchStart(void);

/* Handicap accessors (avoid exposing types.h to C++ translation units) */
u8   matchGetPlayerHandicap(s32 playernum);
void matchSetPlayerHandicap(s32 playernum, u8 val);
void matchResetHandicaps(void);

/* Challenge-mode start: applies challenge config to g_MpSetup and calls
 * mpStartMatch() directly, bypassing the g_MatchConfig → g_MpSetup copy. */
s32 matchStartFromChallenge(s32 slot);
