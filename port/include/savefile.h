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
 *   - No bit-packing constraints (names can be 32+ chars, fields are full-width)
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

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Constants
 * ======================================================================== */

#define SAVE_VERSION           1       /* increment when format changes */
#define SAVE_NAME_MAX         32       /* max agent/player name */
#define SAVE_TEAM_NAME_MAX    32       /* max team name */
#define SAVE_SETUP_NAME_MAX   32       /* max MP setup name */
#define SAVE_MAX_AGENTS       16       /* max agent profiles */
#define SAVE_MAX_PLAYERS      16       /* max MP player profiles */
#define SAVE_MAX_SETUPS       32       /* max MP setup profiles */

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
    u16 besttimes[60][3];              /* generous: 60 stages × 3 difficulties */

    /* Co-op completions per difficulty (bitmask of completed stages) */
    u32 coopcompletions[3];

    /* Firing range scores */
    u8  firingrangescores[9];

    /* Weapons found during campaign */
    u8  weaponsfound[6];

    /* Player options (stored as full bytes, no bit-packing) */
    u8  forwardpitch[2];               /* P1, P2 */
    u8  autoaim[2];
    u8  aimcontrol[2];
    u8  sightonscreen[2];
    u8  lookahead[2];
    u8  ammoonscreen[2];
    u8  headroll[2];
    u8  showgunfunction[2];
    u8  showzoomrange[2];
    u8  showtarget[2];
    u8  showmissiontime[2];
    u8  paintball;
    u8  screensplit;
    u8  screenratio;
    u8  screenwide;
    u8  hiresmode;
    u8  subtitlesingame;
    u8  subtitlescutscene;
    u8  langfilter;
    u8  coopradar;
    u8  coopfriendlyfire;
    u8  antiradar;

    /* Audio volumes (full range 0-255) */
    u8  sfxvolume;
    u8  musicvolume;

    /* Control modes */
    u8  controlmode[2];                /* P1, P2 */

    /* Challenge completions (expanded: 1 byte per challenge instead of 1 bit) */
    u8  challengecompleted[128];       /* generous allocation */
};

/* ========================================================================
 * MP player profile (replaces PAKFILETYPE_MPPLAYER / struct mpplayerconfig)
 * ======================================================================== */

struct savemplayer {
    s32 version;
    char name[SAVE_NAME_MAX];

    /* Appearance */
    u8  mpheadnum;
    u8  mpbodynum;
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
    u8  mpheadnum;
    u8  mpbodynum;
    u8  team;
    char name[SAVE_NAME_MAX];          /* bot display name */
};

struct savempsetup {
    s32 version;
    char name[SAVE_SETUP_NAME_MAX];

    /* Match config */
    u8  scenario;                      /* MPSCENARIO_* */
    u8  stagenum;
    u8  timelimit;                     /* minutes */
    u8  scorelimit;
    u16 teamscorelimit;
    u32 options;                       /* MPOPTION_* bitmask */

    /* Weapons */
    u8  weapons[8];                    /* weapon slots (expanded from 6) */
    s8  weaponSetIndex;                /* -1 = custom */

    /* Bots */
    u8  numBots;
    struct savempbot bots[MAX_BOTS];   /* up to MAX_BOTS bots */

    /* Player team assignments */
    u8  playerTeams[MAX_PLAYERS];      /* one per player slot */
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
 * Load an agent profile by name. Populates g_GameFile and engine structs.
 * Returns 0 on success, -1 on failure.
 */
s32 saveLoadAgent(const char *name);

/**
 * Save the current agent profile. Reads from g_GameFile and engine structs.
 * Returns 0 on success, -1 on failure.
 */
s32 saveSaveAgent(const char *name);

/**
 * Create a new agent profile with default values.
 * Returns 0 on success, -1 if name already exists.
 */
s32 saveCreateAgent(const char *name);

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
