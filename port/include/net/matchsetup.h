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

/* Total match slots = MAX_PLAYERS + MAX_BOTS.
 * Bots occupy slots[MAX_PLAYERS .. MAX_PLAYERS+MAX_BOTS-1], so the array
 * must be large enough to hold both player and bot entries.  The old value
 * of 32 was too small when MAX_PLAYERS=8 and MAX_BOTS=32 (need 40). */
#ifndef MATCH_MAX_SLOTS
#define MATCH_MAX_SLOTS 40
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
	 * matchStart() time via entry->mp_index. */
	char body_id[64]; /* PRIMARY: catalog ID e.g. "base:dark_combat", "base:theking" */
	char head_id[64]; /* PRIMARY: catalog ID e.g. "base:head_dark_combat" */
	u8 headnum;       /* DEPRECATED: integer g_MpHeads[] index. Use head_id instead. Kept temporarily for unmigrated consumers. */
	u8 bodynum;       /* DEPRECATED: integer g_MpBodies[] index. Use body_id instead. Kept temporarily for unmigrated consumers. */
	u8 botType;       /* BOTTYPE_* (only for SLOT_BOT) */
	u8 botDifficulty; /* BOTDIFF_* (only for SLOT_BOT) */
	char name[MAX_PLAYER_NAME];  /* display name */
};

struct matchconfig {
	struct matchslot slots[MATCH_MAX_SLOTS];
	/* PRIMARY: catalog ID string for game mode (e.g. "base:combat", "base:king_of_the_hill").
	 * scenario (u8) is DERIVED — resolved from scenario_id at matchStart() only. */
	char scenario_id[64];           /* PRIMARY: catalog ID — e.g. "base:combat" */
	u8 scenario;                    /* DEPRECATED: MPSCENARIO_* integer. Use scenario_id instead. Kept temporarily for unmigrated consumers. */
	/* PRIMARY: catalog ID string (e.g. "base:mp_complex", "base:defection").
	 * stagenum is DERIVED — resolved from stage_id at matchStart() only. */
	char stage_id[64];              /* PRIMARY: catalog ID — e.g. "base:mp_complex" */
	u8 stagenum;                    /* DEPRECATED: integer stage index. Use stage_id instead. Kept temporarily for unmigrated consumers. */
	u8 timelimit;                   /* minutes (0 = unlimited) */
	u8 scorelimit;                  /* score to win (0 = unlimited) */
	u16 teamscorelimit;             /* team score limit */
	u32 options;                    /* MPOPTION_* bitmask */
	/* PRIMARY: catalog ID strings for per-slot weapons (custom weapon sets).
	 * e.g. "base:falcon2", "base:dragon". Empty = no weapon (MPWEAPON_NONE).
	 * weapons[] (u8) is DERIVED — resolved from weapon_ids at matchStart(). */
	char weapon_ids[NUM_MPWEAPONSLOTS][64];
	u8 weapons[NUM_MPWEAPONSLOTS];  /* DEPRECATED: MPWEAPON_* indices. Use weapon_ids instead. */
	s8 weaponSetIndex;              /* -1 = custom, 0+ = preset index */
	u8 numSlots;                    /* number of active slots */
	/* PRIMARY: catalog ID for spawn weapon. Empty = Random.
	 * e.g. "base:falcon2". spawnWeaponNum is DERIVED at spawn time. */
	char spawn_weapon_id[64];
	u8 spawnWeaponNum;              /* DEPRECATED: WEAPON_* enum value. Use spawn_weapon_id instead. */
};

/* Defined in matchsetup.c */
extern struct matchconfig g_MatchConfig;

void matchConfigInit(void);
/* body_id/head_id: catalog IDs (e.g. "base:dark_combat").  Pass NULL/"" to use
 * the default (base:dark_combat / base:head_dark_combat). */
s32 matchConfigAddBot(u8 botType, u8 botDifficulty, const char *body_id,
                      const char *head_id, const char *name);
s32 matchConfigRemoveSlot(s32 idx);
void matchConfigRerollBot(s32 idx);
s32 matchStart(void);

/* Handicap accessors (avoid exposing types.h to C++ translation units) */
u8   matchGetPlayerHandicap(s32 playernum);
void matchSetPlayerHandicap(s32 playernum, u8 val);
void matchResetHandicaps(void);

/* M0.1c: Get the catalog ID string for a weapon currently in g_MpSetup.weapons[slot].
 * Returns "" if the slot is empty or the weapon is not in the catalog. */
const char *matchGetWeaponSlotCatalogId(s32 slot);

/* Challenge-mode start: applies challenge config to g_MpSetup and calls
 * mpStartMatch() directly, bypassing the g_MatchConfig → g_MpSetup copy. */
s32 matchStartFromChallenge(s32 slot);
