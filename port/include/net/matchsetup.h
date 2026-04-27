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

/* Issue E: Combined participant cap (humans + bots).  The engine's CHR.TICK
 * path starts to fault past 32 total participants (observed crash at
 * slot=32 chrnum=5033 with 1 player + 32 bots).  MATCH_MAX_SLOTS is the
 * storage-array size; MATCH_PARTICIPANT_CAP is the run-time ceiling that
 * matchConfigMaxBotsForHumans() and the CLC_LOBBY_START clamp enforce so
 * the total active count never exceeds what CHR.TICK can handle. */
#ifndef MATCH_PARTICIPANT_CAP
#define MATCH_PARTICIPANT_CAP 32
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

/* Spawn-weapon mode — what "Random" means and how it interacts with spawns.
 *
 * SPECIFIC: spawn_weapon_id names the weapon, used for every spawn. spawnWeaponNum
 *           is derived from it at matchStart().
 * RANDOM:   matchStart() rolls one weapon from the active match set (at the host)
 *           and stores the rolled WEAPON_* enum in spawnWeaponNum. Every spawn
 *           uses that same weapon for the duration of the match. Bots too.
 * FIESTA:   each spawn (every player, every bot, every respawn) rolls fresh
 *           from the active match set independently. spawnWeaponNum carries the
 *           SPAWNWEAPON_FIESTA_SENTINEL (0xFE) on the wire and at runtime so the
 *           spawn sites know to roll per-spawn.
 *
 * Wire / save format:
 *   - Wire: SVC_STAGE_START + CLC_LOBBY_START carry an explicit u8 mode +
 *     u8 spawnWeaponNum after the existing spawn_weapon_id string. Bumped
 *     NET_PROTOCOL_VER 44 -> 45 (2026-04-27, S482).
 *   - Save (scenario JSON): "spawnWeaponMode" key written; missing key
 *     defaults to RANDOM if spawn_weapon_id is empty, SPECIFIC otherwise
 *     (preserves legacy semantics for v <= 44 saves).
 *   - mpsetup WAD save: unchanged — spawn weapon is not persisted there.
 */
enum spawn_weapon_mode {
	SPAWNWEAPON_MODE_SPECIFIC = 0,
	SPAWNWEAPON_MODE_RANDOM   = 1,
	SPAWNWEAPON_MODE_FIESTA   = 2,
};

/* Sentinel WEAPON_* enum value for FIESTA mode. The spawn sites read this
 * and roll per-spawn instead of equipping a fixed weapon. Chosen as 0xFE
 * (one less than the legacy 0xFF "no spawn weapon" sentinel) so that any
 * code path checking `!= 0xFF && != 0` for a real weapon continues to
 * exclude this value as well. */
#ifndef SPAWNWEAPON_FIESTA_SENTINEL
#define SPAWNWEAPON_FIESTA_SENTINEL 0xFE
#endif

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
	u32 options;                    /* MPOPTION_* bitmask (engine view: includes any forced bits) */
	/* INV-4 / Cohort D (player-init-architectural-fixes-2026-04-26):
	 * mask of MPOPTION_* bits that were force-set by an engine fallback
	 * (e.g. setup.c B-181 force-enabling SPAWNWITHWEAPON when world
	 * pickups are sparse). matchStart() restores user-original options
	 * by clearing these bits from `options` + zeroing this field, so the
	 * user's original menu choice is recovered on each new match.
	 * Engine-forced bit setters MUST use matchOptionsForceBit so the
	 * mark and the apply cannot drift. Tests pin the bit math in
	 * port/src/options_forced.c. Not persisted to save files. */
	u32 options_engine_forced;
	/* PRIMARY: catalog ID strings for per-slot weapons (custom weapon sets).
	 * e.g. "base:falcon2", "base:dragon". Empty = no weapon (MPWEAPON_NONE).
	 * weapons[] (u8) is DERIVED — resolved from weapon_ids at matchStart(). */
	char weapon_ids[NUM_MPWEAPONSLOTS][64];
	u8 weapons[NUM_MPWEAPONSLOTS];  /* DEPRECATED: MPWEAPON_* indices. Use weapon_ids instead. */
	s8 weaponSetIndex;              /* -1 = custom, 0+ = preset index */
	u8 numSlots;                    /* number of active slots */
	/* PRIMARY: catalog ID for spawn weapon. Empty for RANDOM/FIESTA modes; the
	 * user's lobby intent is captured in spawnWeaponMode. spawnWeaponNum is
	 * the runtime-resolved WEAPON_* enum (set at matchStart() / on the wire). */
	char spawn_weapon_id[64];
	u8 spawnWeaponNum;              /* DEPRECATED: WEAPON_* enum value. RUNTIME-resolved
	                                 * (matchStart for SPECIFIC/RANDOM, sentinel for FIESTA). */
	u8 spawnWeaponMode;             /* enum spawn_weapon_mode (SPECIFIC / RANDOM / FIESTA). */
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
/* Re-roll the bot's name only (body/head untouched). */
void matchConfigRerollBotName(s32 idx);
s32 matchStart(void);
/* Returns max bot slots allowed for the current human count, clamped against
 * both participant slots and MAX_BOTS runtime limits. */
s32 matchConfigMaxBotsForHumans(s32 humanCount);

/* Count SLOT_PLAYER entries currently in g_MatchConfig (min 1). Canonical way
 * for any caller of matchConfigMaxBotsForHumans() to discover the human count
 * when no external lobby-aware count is available. */
s32 matchConfigCountHumans(void);

/* Pick a team index for a new bot balancing against existing slots.
 * numTeams: 2..MAX_TEAMS (clamped). Pass 2 for classic team mode, or the
 * scenario-dependent team count for multi-team modes. */
u8 matchConfigChooseBotTeam(s32 numTeams);

/* Handicap accessors (avoid exposing types.h to C++ translation units) */
u8   matchGetPlayerHandicap(s32 playernum);
void matchSetPlayerHandicap(s32 playernum, u8 val);
void matchResetHandicaps(void);

/* M0.1c: Get the catalog ID string for a weapon currently in g_MpSetup.weapons[slot].
 * Returns "" if the slot is empty or the weapon is not in the catalog. */
const char *matchGetWeaponSlotCatalogId(s32 slot);

/* S482 (2026-04-27): Random / Fiesta spawn-weapon helpers.
 *
 * spawnWeaponPickFromActiveSet — read g_MpSetup.weapons[0..NUM_MPWEAPONSLOTS-1],
 *   filter to non-NONE/non-DISABLED/non-SHIELD slots, and pick one uniformly at
 *   random via rngRandom(). Returns the chosen MPWEAPON_* index. If the active
 *   set has zero eligible slots, returns 0 (caller should treat as "no spawn
 *   weapon"; the existing miss path in player.c / bot.c already handles this).
 *
 * spawnWeaponPickFromSlots — pure variant for testing: takes the slot array +
 *   count + a pluggable u32 RNG. Same filtering rules. Returns the chosen
 *   MPWEAPON_* index, or 0 when degenerate.
 *
 * S483 (2026-04-27): host-eligible pool via match manifest.
 *
 * spawnWeaponPickFromMatchManifest — primary spawn-weapon roll. Walks the
 *   loaded match manifest (cascade: g_CurrentLoadedManifest, g_ServerManifest,
 *   g_ClientManifest), resolves each MANIFEST_TYPE_WEAPON entry to an
 *   MPWEAPON_* index, filters NONE/DISABLED/SHIELD, and rolls uniformly. Falls
 *   back to spawnWeaponPickFromActiveSet() when no manifest is loaded yet
 *   (e.g. solo CS, pre-broadcast) or when zero entries resolve.
 *
 * The pure pool-builder spawnWeaponBuildPoolFromManifest is internal to
 * matchsetup.c; tests in tests/test_spawn_weapon_mode.cpp replicate the
 * spec directly per the test_random_pool.cpp pattern.
 */
s32 spawnWeaponPickFromActiveSet(void);
s32 spawnWeaponPickFromSlots(const u8 *slots, s32 numSlots,
                             u32 (*rng_fn)(void *userdata), void *userdata);
s32 spawnWeaponPickFromMatchManifest(void);

/* Challenge-mode start: applies challenge config to g_MpSetup and calls
 * mpStartMatch() directly, bypassing the g_MatchConfig → g_MpSetup copy. */
s32 matchStartFromChallenge(s32 slot);
