/**
 * savefile.h -- PC-native save system.
 *
 * Replaces the N64 EEPROM/Controller Pak save system with JSON files on disk.
 * Each save type gets its own human-readable file:
 *
 *   agent_<name>.json       — single-player agent profile (campaign progress)
 *   player_<name>.json      — multiplayer player config (stats, appearance)
 *   mpsetup_<name>.json     — multiplayer game setup (settings, bots)
 *   system.json             — global system settings (language, team names, etc.)
 *
 * Benefits over the old system:
 *   - No bit-packing constraints (fields are full-width and schema-owned)
 *   - Human-readable (debuggable, moddable)
 *   - Self-versioned (new fields don't break old saves)
 *   - Individual files (no monolithic EEPROM blob)
 *   - Extensible (add fields freely without offset calculations)
 *
 * Migration: on first run, the old eeprom.bin is read and converted to the
 * new JSON format. The old file is renamed to eeprom.bin.bak.
 *
 * The engine's internal structs (g_GameFile, g_PlayerConfigsArray, g_MpSetup,
 * g_BossFile) are populated from JSON on load and serialized to JSON on save.
 * This means existing engine code that reads these structs works unchanged.
 */

#ifndef _IN_SAVEFILE_H
#define _IN_SAVEFILE_H

#include <PR/ultratypes.h>
#include "agent_profile_codec.h"
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

#define SAVE_VERSION           2       /* SA-4: string IDs replace raw integers */
#define SAVE_AGENT_VERSION     AGENT_PROFILE_VERSION
#define SAVE_NAME_MAX         32       /* generic serialized name buffer */
#define SAVE_AGENT_NAME_LENGTH_MAX 10  /* struct gamefile identity domain */
#define SAVE_TEAM_NAME_MAX    32       /* max team name */
#define SAVE_SETUP_NAME_MAX   32       /* max MP setup name */
#define SAVE_MAX_AGENTS       30       /* matches the established Agent Select capacity */
#define SAVE_MAX_PLAYERS      16       /* max MP player profiles */
#define SAVE_MAX_SETUPS       32       /* max MP setup profiles */
#define SAVE_MAX_BOTS         32       /* max bots per MP setup (= MAX_BOTS = PARTICIPANT_DEFAULT_CAPACITY, raised S45) */
#define SAVE_MAX_PLAYERSLOTS   8       /* max player slots per match (= MAX_PLAYERS) */
#define SAVE_AGENT_STAGE_COUNT 21
#define SAVE_AGENT_CHALLENGE_COUNT 30
#define SAVE_AGENT_PLAYER_COUNTS 4

/* ========================================================================
 * Agent profile (replaces PAKFILETYPE_GAME / struct gamefile)
 * ======================================================================== */

struct saveagent {
    s32 version;
    char name[SAVE_NAME_MAX];

    /* Campaign progress */
    u32 totaltime;                     /* total play time in frames */
    u8  autodifficulty;
    u8  autostageindex;
    u8  thumbnail;                     /* stage index for thumbnail */

    /* Stage completion times: [stage][difficulty] in frames (0 = not completed) */
    u16 besttimes[SAVE_AGENT_STAGE_COUNT][3];

    /* Co-op completions per difficulty (bitmask of completed stages) */
    u32 coopcompletions[3];

    /* Firing range scores */
    u8  firingrangescores[9];

    /* Weapons found during campaign */
    u8  weaponsfound[6];

    /* Exact gamefile option and gameplay flag domain. */
    u8  flags[10];
    u16 unk1e;

    /* Audio and control state retained by the original agent contract. */
    u16 sfxvolume;
    u16 musicvolume;
    u8  soundmode;
    u8  controlmode[2];

    /* Any-player completion for each challenge and 1-4 player count. */
    u8  challengecompleted[SAVE_AGENT_CHALLENGE_COUNT][SAVE_AGENT_PLAYER_COUNTS];

    /* D-005 option A: the same Agent JSON owns per-agent preferences. */
    struct agent_profile_preferences preferences;
};

/**
 * Monotonic receipt for the latest PC-native agent-profile write attempt.
 * Runtime observers can snapshot serial before an authoritative progression
 * event, then require a newer successful receipt for the expected profile.
 */
struct saveagentwritereceipt {
    u32 serial;
    s32 result;
    char name[SAVE_NAME_MAX];
};

/**
 * Read-only Agent Profile Store row used by menus and tooling. The profile
 * name is the stable identity; legacy pak file IDs are not part of this API.
 */
struct saveagentsummary {
    char name[SAVE_NAME_MAX];
    u32 totaltime;
    u8 autodifficulty;
    u8 autostageindex;
    u8 thumbnail;
};

/* ========================================================================
 * MP player profile (replaces PAKFILETYPE_MPPLAYER / struct mpplayerconfig)
 * ======================================================================== */

struct savemplayer {
    s32 version;
    char name[SAVE_NAME_MAX];

    /* Appearance */
    char head_id[CATALOG_ID_LEN];      /* SA-4: catalog string ID for head */
    char body_id[CATALOG_ID_LEN];      /* SA-4: catalog string ID for body */
    u8  team;
    u32 displayoptions;

    /* Lifetime stats (full 32-bit, no truncation) */
    u32 kills;
    u32 deaths;
    u32 gamesplayed;
    u32 gameswon;
    u32 gameslost;
    u32 distance;                      /* in distance units */
    u32 headshots;
    u32 ammoused;
    u32 accuracy;                      /* 0-1000 */
    u32 damagedealt;
    u32 painreceived;

    /* Medals (full 32-bit) */
    u32 accuracymedals;
    u32 headshotmedals;
    u32 killmastermedals;
    u32 survivormedals;

    /* Play time (seconds, no 28-bit cap) */
    u32 playtime;

    /* Control */
    u8  controlmode;
    u16 options;

    /* Gun function preferences (8 bytes, no bit-packing) */
    u8  gunfuncs[8];

    /* Challenge completions */
    u8  challengecompleted[128];
};

/* ========================================================================
 * MP game setup (replaces PAKFILETYPE_MPSETUP)
 * ======================================================================== */

struct savempbot {
    u8  type;                          /* BOTTYPE_* */
    u8  difficulty;                    /* BOTDIFF_* */
    u8  team;
    char name[SAVE_NAME_MAX];          /* bot display name */
    char profile_id[CATALOG_ID_LEN];   /* PRIMARY: public .pdbotprofile ID */
    char head_id[CATALOG_ID_LEN];      /* SA-4: catalog string ID for head */
    char body_id[CATALOG_ID_LEN];      /* SA-4: catalog string ID for body */
};

struct savempsetup {
    s32 version;
    char name[SAVE_SETUP_NAME_MAX];

    /* Match config */
    u8  scenario;                      /* MPSCENARIO_* */
    char stage_id[CATALOG_ID_LEN];     /* SA-4: catalog string ID for stage */
    u8  timelimit;                     /* minutes */
    u8  scorelimit;
    u16 teamscorelimit;
    u32 options;                       /* MPOPTION_* bitmask */

    /* Weapons */
    u8  weapons[8];                    /* weapon slots (expanded from 6) */
    s8  weaponSetIndex;                /* -1 = custom */

    /* Bots */
    u8  numBots;
    struct savempbot bots[SAVE_MAX_BOTS];      /* up to 32 bots */

    /* Player team assignments */
    u8  playerTeams[SAVE_MAX_PLAYERSLOTS];    /* one per player slot */
};

/* ========================================================================
 * System settings (replaces PAKFILETYPE_BOSS / EEPROM)
 * ======================================================================== */

struct savesystem {
    s32 version;

    /* Language */
    u8  language;

    /* MP team names (expanded from 12 chars) */
    char teamnames[8][SAVE_TEAM_NAME_MAX];

    /* Soundtrack */
    s8  tracknum;                      /* -1 = random */
    u8  multipletracknums[8];
    u8  usingmultipletunes;

    /* Title screen */
    u8  altTitleUnlocked;
    u8  altTitleEnabled;

    /* Active agent/player indices */
    s32 activeAgentIndex;
    s32 activePlayerIndex;
};

/* ========================================================================
 * Save system API
 * ======================================================================== */

/**
 * Initialize the save system. Scans the save directory for existing files.
 * If eeprom.bin exists and no JSON saves found, migrates from old format.
 */
void saveInit(void);

/**
 * List available agent profiles.
 * Returns count, fills names array (up to maxcount).
 */
s32 saveListAgents(char names[][SAVE_NAME_MAX], s32 maxcount);

/**
 * List validated JSON agent profiles in deterministic name order.
 * Malformed, version-mismatched, aliased, and duplicate identities are
 * rejected rather than published to Agent Select.
 */
s32 saveListAgentProfiles(struct saveagentsummary *profiles, s32 maxcount);

/** Monotonic generation for successful profile-store mutations. */
u32 saveGetAgentProfileRevision(void);

/**
 * Load an agent profile by name. Populates g_GameFile and engine structs.
 * Returns 0 on success, -1 on failure.
 */
s32 saveLoadAgent(const char *name);

/**
 * Save the current agent profile. Reads from g_GameFile and engine structs.
 * Returns 0 on success, -1 on failure.
 */
s32 saveSaveAgent(const char *name);

/** Copy the latest agent write receipt into out. Safe before the first write. */
void saveGetLastAgentWriteReceipt(struct saveagentwritereceipt *out);

/**
 * Create a new agent profile with default values.
 * Returns 0 on success, -1 if name already exists.
 */
s32 saveCreateAgent(const char *name);

/** Atomically duplicate an agent under a new profile identity. */
s32 saveCopyAgent(const char *source_name, const char *destination_name);

/**
 * Delete an agent profile.
 * Returns 0 on success, -1 on failure.
 */
s32 saveDeleteAgent(const char *name);

/**
 * Load an MP player profile. Populates g_PlayerConfigsArray[playernum].
 */
s32 saveLoadMpPlayer(const char *name, s32 playernum);

/**
 * Save an MP player profile from g_PlayerConfigsArray[playernum].
 */
s32 saveSaveMpPlayer(const char *name, s32 playernum);

/**
 * Load an MP setup. Populates g_MpSetup and g_BotConfigsArray.
 */
s32 saveLoadMpSetup(const char *name);

/**
 * Save current MP setup from g_MpSetup and g_BotConfigsArray.
 */
s32 saveSaveMpSetup(const char *name);

/**
 * Load system settings. Populates g_BossFile equivalent.
 */
s32 saveLoadSystem(void);

/**
 * Save system settings.
 */
s32 saveSaveSystem(void);

/**
 * Migrate from old eeprom.bin to new JSON format.
 * Returns number of files migrated, or -1 on error.
 */
s32 saveMigrateFromEeprom(void);

/**
 * Get the save directory path.
 */
const char *saveGetDir(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SAVEFILE_H */
