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
	u8 headnum;       /* character head */
	u8 bodynum;       /* character body */
	u8 botType;       /* BOTTYPE_* (only for SLOT_BOT) */
	u8 botDifficulty; /* BOTDIFF_* (only for SLOT_BOT) */
	char name[MAX_PLAYER_NAME];  /* display name */
};

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
	u8 spawnWeaponNum;              /* 0xFF = Random; weapon enum value otherwise */
};

/* Defined in matchsetup.c */
extern struct matchconfig g_MatchConfig;

void matchConfigInit(void);
s32 matchConfigAddBot(u8 botType, u8 botDifficulty, u8 headnum, u8 bodynum,
                      const char *name);
s32 matchConfigRemoveSlot(s32 idx);
s32 matchStart(void);
