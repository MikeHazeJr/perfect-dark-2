/**
 * assetcatalog_base_extended.c -- S46a: Extended base game asset registration
 *
 * Registers 8 new asset types in the Asset Catalog:
 *   ASSET_WEAPON    -- all 47 MP weapons (MPWEAPON_* constants)
 *   ASSET_ANIMATION -- 1207 animations (full table, indices 0x0000..0x04B6)
 *   ASSET_TEXTURE   -- NUM_TEXTURES base textures (3503 NTSC / 3511 JPN-final)
 *   ASSET_PROP      -- 8 base prop categories (PROPTYPE_* constants)
 *   ASSET_GAMEMODE  -- all 6 Combat Simulator scenarios (MPSCENARIO_*)
 *   ASSET_AUDIO     -- 1545 SFX entries (full main bank, 0x0000..0x0608)
 *   ASSET_HUD       -- 6 HUD element categories (HUD_ELEM_*)
 *   ASSET_LANG      -- 68 language string banks (LANGBANK_* constants)
 *
 * Called by assetCatalogRegisterBaseGame() at the end of base registration.
 * Auto-discovered by CMake glob (port/*.c). No build system changes needed.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "constants.h"
#include "assetcatalog.h"
#include "system.h"
#include "data.h"
#include "game/mplayer/scenarios.h"
#include "game/lang.h"

/* ========================================================================
 * Weapon Table
 * ======================================================================== */

/*
 * Maps MPWEAPON_* constant -> catalog slug, display name, dual-wield flag.
 * weapon_id is the value used by the game engine (MPWEAPON_* range 0x01-0x2f).
 * model_file, damage, fire_rate are left at defaults -- base game loads from ROM.
 */
static const struct {
	s32         weapon_id;
	const char *slug;
	const char *name;
	s32         dual_wieldable;
} s_BaseWeapons[] = {
	{ 0x01, "falcon2",          "Falcon 2",            0 },
	{ 0x02, "falcon2_silencer", "Falcon 2 Silencer",   0 },
	{ 0x03, "falcon2_scope",    "Falcon 2 Scope",      1 },
	{ 0x04, "magsec4",          "Magsec 4",            1 },
	{ 0x05, "mauler",           "Mauler",              1 },
	{ 0x06, "phoenix",          "Phoenix",             0 },
	{ 0x07, "dy357magnum",      "DY357 Magnum",        1 },
	{ 0x08, "dy357lx",          "DY357-LX",            1 },
	{ 0x09, "cmp150",           "CMP150",              1 },
	{ 0x0a, "cyclone",          "Cyclone",             1 },
	{ 0x0b, "callisto",         "Callisto NTG",        1 },
	{ 0x0c, "rcp120",           "RC-P120",             0 },
	{ 0x0d, "laptopgun",        "Laptop Gun",          0 },
	{ 0x0e, "dragon",           "Dragon",              0 },
	{ 0x0f, "k7avenger",        "K7 Avenger",          0 },
	{ 0x10, "ar34",             "AR34",                0 },
	{ 0x11, "superdragon",      "SuperDragon",         0 },
	{ 0x12, "shotgun",          "Shotgun",             0 },
	{ 0x13, "reaper",           "Reaper",              0 },
	{ 0x14, "sniperrifle",      "Sniper Rifle",        0 },
	{ 0x15, "farsight",         "FarSight XR-20",      0 },
	{ 0x16, "devastator",       "Devastator",          0 },
	{ 0x17, "rocketlauncher",   "Rocket Launcher",     0 },
	{ 0x18, "slayer",           "Slayer",              0 },
	{ 0x19, "combatknife",      "Combat Knife",        1 },
	{ 0x1a, "crossbow",         "Crossbow",            0 },
	{ 0x1b, "tranquilizer",     "Tranquilizer",        0 },
	{ 0x1c, "grenade",          "Grenade",             0 },
	{ 0x1d, "nbomb",            "N-Bomb",              0 },
	{ 0x1e, "timedmine",        "Timed Mine",          0 },
	{ 0x1f, "proximitymine",    "Proximity Mine",      0 },
	{ 0x20, "remotemine",       "Remote Mine",         0 },
	{ 0x21, "laser",            "Laser",               0 },
	{ 0x22, "xrayscanner",      "X-Ray Scanner",       0 },
	{ 0x23, "nightvision",      "Night Vision",        0 },
	{ 0x24, "irscanner",        "IR Scanner",          0 },
	{ 0x25, "cloakingdevice",   "Cloaking Device",     0 },
	{ 0x26, "combatboost",      "Combat Boost",        0 },
	{ 0x27, "pp9i",             "PP9i",                1 },
	{ 0x28, "cc13",             "CC13",                0 },
	{ 0x29, "kl01313",          "Kl01313",             0 },
	{ 0x2a, "kf7special",       "KF7 Special",         0 },
	{ 0x2b, "zzt",              "ZZT x3",              0 },
	{ 0x2c, "dmc",              "DMC",                 0 },
	{ 0x2d, "ar53",             "AR53",                0 },
	{ 0x2e, "rcp45",            "RC-P45",              0 },
	{ 0x2f, "shield",           "Shield",              0 },
};

#define NUM_BASE_WEAPONS (sizeof(s_BaseWeapons) / sizeof(s_BaseWeapons[0]))

/* ========================================================================
 * Animation Table
 * ======================================================================== */

/*
 * Full animation table: 1207 entries (indices 0x0000..0x04B6).
 * Count derived from src/assets/ntsc-final/animations.json (1207 entries,
 * last file 04b6.bin). IDs generated as "base:anim_XXXX"; metadata zeroed
 * since frame counts aren't available as compile-time constants.
 * g_NumAnimations is set at runtime (after catalog init), so we use the
 * static count from the JSON rather than animGetNumAnimations().
 */
#define NUM_BASE_ANIM_ENTRIES 1207

/* ========================================================================
 * Texture Table
 * ======================================================================== */

/*
 * Full texture table: NUM_TEXTURES entries (3503 NTSC / 3511 JPN-final).
 * NUM_TEXTURES is defined in constants.h and is a compile-time constant.
 * width/height/format = 0 (loaded from ROM at runtime). IDs generated
 * as "base:tex_XXXX".
 */
/* NUM_TEXTURES pulled from constants.h */

/* ========================================================================
 * Prop Table
 * ======================================================================== */

/*
 * One entry per PROPTYPE_* constant (fundamental prop categories).
 * model_file = "" (base game props load models from ROM / object tables).
 * These are prop *categories*, not individual prop definitions.
 */
static const struct {
	s32         prop_type;
	const char *slug;
	const char *name;
	f32         health;
} s_BaseProps[] = {
	{ 1, "prop_obj",       "Object",        100.0f },
	{ 2, "prop_door",      "Door",          200.0f },
	{ 3, "prop_chr",       "Character",     100.0f },
	{ 4, "prop_weapon",    "Weapon Pickup",   0.0f },
	{ 5, "prop_eyespy",    "Eye Spy",        50.0f },
	{ 6, "prop_player",    "Player",        100.0f },
	{ 7, "prop_explosion", "Explosion",       0.0f },
	{ 8, "prop_smoke",     "Smoke",           0.0f },
};

#define NUM_BASE_PROPS (sizeof(s_BaseProps) / sizeof(s_BaseProps[0]))

/* ========================================================================
 * Game Mode Table
 * ======================================================================== */

/*
 * All 6 base Combat Simulator scenarios (MPSCENARIO_* constants).
 * Names and descriptions are hardcoded here; the engine uses language IDs
 * (stored in g_MpScenarioOverviews[].name) for in-game display.
 * team_based mirrors g_MpScenarioOverviews[].teamonly.
 */
static const struct {
	s32         mode_id;
	const char *slug;
	const char *name;
	const char *description;
	s32         min_players;
	s32         max_players;
	s32         team_based;
} s_BaseGameModes[] = {
	{ 0 /* MPSCENARIO_COMBAT */,
	  "combat",
	  "Combat Simulator",
	  "Eliminate opponents to score points.",
	  2, 8, 0 },
	{ 1 /* MPSCENARIO_HOLDTHEBRIEFCASE */,
	  "hold_the_briefcase",
	  "Hold the Briefcase",
	  "Carry the briefcase as long as possible to accumulate points.",
	  2, 8, 0 },
	{ 2 /* MPSCENARIO_HACKERCENTRAL */,
	  "hacker_central",
	  "Hacker Central",
	  "Hack terminals across the level. Points awarded per hack.",
	  2, 8, 0 },
	{ 3 /* MPSCENARIO_POPACAP */,
	  "pop_a_cap",
	  "Pop a Cap",
	  "Score points by eliminating the designated target.",
	  2, 8, 0 },
	{ 4 /* MPSCENARIO_KINGOFTHEHILL */,
	  "king_of_the_hill",
	  "King of the Hill",
	  "Control the hill zone to score points for your team.",
	  2, 8, 1 },
	{ 5 /* MPSCENARIO_CAPTURETHECASE */,
	  "capture_the_case",
	  "Capture the Case",
	  "Capture the opposing team case and defend your own.",
	  2, 8, 1 },
};

#define NUM_BASE_GAMEMODES (sizeof(s_BaseGameModes) / sizeof(s_BaseGameModes[0]))

/* ========================================================================
 * Audio Table
 * ======================================================================== */

/*
 * Full SFX table: 1545 entries (indices 0x0000..0x0608), the main sound bank.
 * Per sfx.h comment: "There are 1545 (0x609) sound effects in the bank."
 * The high-bit mapped entries (SFX_8000+) are internal aliases remapped by
 * snd.c and are not registered as separate catalog entries.
 * category = 0 (AUDIO_CAT_SFX) for all base SFX entries.
 */
#define NUM_BASE_SFX_ENTRIES 1545

/* ========================================================================
 * HUD Table
 * ======================================================================== */

/*
 * One entry per HUD_ELEM_* element type.
 * texture_file = "" (engine-rendered, no standalone texture file).
 */
static const struct {
	s32         hud_id;
	const char *slug;
	const char *name;
	s32         element_type;
} s_BaseHud[] = {
	{ 0, "hud_crosshair", "Crosshair",     0 /* HUD_ELEM_CROSSHAIR */ },
	{ 1, "hud_ammo",      "Ammo Display",  1 /* HUD_ELEM_AMMO      */ },
	{ 2, "hud_radar",     "Radar",         2 /* HUD_ELEM_RADAR     */ },
	{ 3, "hud_health",    "Health Bar",    3 /* HUD_ELEM_HEALTH    */ },
	{ 4, "hud_timer",     "Timer",         4 /* HUD_ELEM_TIMER     */ },
	{ 5, "hud_score",     "Score Display", 5 /* HUD_ELEM_SCORE     */ },
};

#define NUM_BASE_HUD (sizeof(s_BaseHud) / sizeof(s_BaseHud[0]))

/* ========================================================================
 * Language Bank Table (Phase 3: lang bank manifesting)
 * ======================================================================== */

/*
 * Maps LANGBANK_* constant -> catalog slug.
 * Each entry becomes "base:lang_{slug}" (e.g. "base:lang_options").
 *
 * bundled=1 so assetCatalogClearMods() never removes them.
 * load_state=ASSET_STATE_ENABLED (not LOADED): lang banks require explicit
 * loading via langLoad() and are NOT pre-loaded at catalog registration time.
 */
static const struct {
	s32 bank_id;
	const char *slug;
} s_BaseLangBanks[] = {
	{ LANGBANK_AME,       "lang_ame"       },
	{ LANGBANK_ARCH,      "lang_arch"      },
	{ LANGBANK_ARK,       "lang_ark"       },
	{ LANGBANK_ASH,       "lang_ash"       },
	{ LANGBANK_AZT,       "lang_azt"       },
	{ LANGBANK_CAT,       "lang_cat"       },
	{ LANGBANK_CAVE,      "lang_cave"      },
	{ LANGBANK_AREC,      "lang_arec"      },
	{ LANGBANK_CRAD,      "lang_crad"      },
	{ LANGBANK_CRYP,      "lang_cryp"      },
	{ LANGBANK_DAM,       "lang_dam"       },
	{ LANGBANK_DEPO,      "lang_depo"      },
	{ LANGBANK_DEST,      "lang_dest"      },
	{ LANGBANK_DISH,      "lang_dish"      },
	{ LANGBANK_EAR,       "lang_ear"       },
	{ LANGBANK_ELD,       "lang_eld"       },
	{ LANGBANK_IMP,       "lang_imp"       },
	{ LANGBANK_JUN,       "lang_jun"       },
	{ LANGBANK_LEE,       "lang_lee"       },
	{ LANGBANK_LEN,       "lang_len"       },
	{ LANGBANK_LIP,       "lang_lip"       },
	{ LANGBANK_LUE,       "lang_lue"       },
	{ LANGBANK_OAT,       "lang_oat"       },
	{ LANGBANK_PAM,       "lang_pam"       },
	{ LANGBANK_PETE,      "lang_pete"      },
	{ LANGBANK_REF,       "lang_ref"       },
	{ LANGBANK_RIT,       "lang_rit"       },
	{ LANGBANK_RUN,       "lang_run"       },
	{ LANGBANK_SEVB,      "lang_sevb"      },
	{ LANGBANK_SEV,       "lang_sev"       },
	{ LANGBANK_SEVX,      "lang_sevx"      },
	{ LANGBANK_SEVXB,     "lang_sevxb"     },
	{ LANGBANK_SHO,       "lang_sho"       },
	{ LANGBANK_SILO,      "lang_silo"      },
	{ LANGBANK_STAT,      "lang_stat"      },
	{ LANGBANK_TRA,       "lang_tra"       },
	{ LANGBANK_WAX,       "lang_wax"       },
	{ LANGBANK_GUN,       "lang_gun"       },
	{ LANGBANK_TITLE,     "lang_title"     },
	{ LANGBANK_MPMENU,    "lang_mpmenu"    },
	{ LANGBANK_PROPOBJ,   "lang_propobj"   },
	{ LANGBANK_MPWEAPONS, "lang_mpweapons" },
	{ LANGBANK_OPTIONS,   "lang_options"   },
	{ LANGBANK_MISC,      "lang_misc"      },
	{ LANGBANK_UFF,       "lang_uff"       },
	{ LANGBANK_OLD,       "lang_old"       },
	{ LANGBANK_ATE,       "lang_ate"       },
	{ LANGBANK_LAM,       "lang_lam"       },
	{ LANGBANK_MP1,       "lang_mp1"       },
	{ LANGBANK_MP2,       "lang_mp2"       },
	{ LANGBANK_MP3,       "lang_mp3"       },
	{ LANGBANK_MP4,       "lang_mp4"       },
	{ LANGBANK_MP5,       "lang_mp5"       },
	{ LANGBANK_MP6,       "lang_mp6"       },
	{ LANGBANK_MP7,       "lang_mp7"       },
	{ LANGBANK_MP8,       "lang_mp8"       },
	{ LANGBANK_MP9,       "lang_mp9"       },
	{ LANGBANK_MP10,      "lang_mp10"      },
	{ LANGBANK_MP11,      "lang_mp11"      },
	{ LANGBANK_MP12,      "lang_mp12"      },
	{ LANGBANK_MP13,      "lang_mp13"      },
	{ LANGBANK_MP14,      "lang_mp14"      },
	{ LANGBANK_MP15,      "lang_mp15"      },
	{ LANGBANK_MP16,      "lang_mp16"      },
	{ LANGBANK_MP17,      "lang_mp17"      },
	{ LANGBANK_MP18,      "lang_mp18"      },
	{ LANGBANK_MP19,      "lang_mp19"      },
	{ LANGBANK_MP20,      "lang_mp20"      },
};

#define NUM_BASE_LANG_BANKS (sizeof(s_BaseLangBanks) / sizeof(s_BaseLangBanks[0]))

/* ========================================================================
 * Public Entry Point
 * ======================================================================== */

s32 assetCatalogRegisterBaseGameExtended(void)
{
	s32 count = 0;
	char idbuf[CATALOG_ID_LEN];

	/* ---- weapons ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_WEAPONS; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseWeapons[i].slug);
			asset_entry_t *e = assetCatalogRegisterWeapon(
				idbuf, s_BaseWeapons[i].weapon_id,
				s_BaseWeapons[i].name,
				"", 0.0f, 0.0f, 0,
				s_BaseWeapons[i].dual_wieldable);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register weapon %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1; e->runtime_index = i;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base weapons", n);
		count += n;
	}

	/* ---- animations ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < NUM_BASE_ANIM_ENTRIES; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:anim_%04x", i);
			asset_entry_t *e = assetCatalogRegisterAnimation(
				idbuf, i, "", 0, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register animation %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base animations", n);
		count += n;
	}

	/* ---- textures ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < NUM_TEXTURES; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:tex_%04x", i);
			asset_entry_t *e = assetCatalogRegisterTexture(
				idbuf, i, 0, 0, 0, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register texture %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base textures", n);
		count += n;
	}

	/* ---- props ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_PROPS; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseProps[i].slug);
			asset_entry_t *e = assetCatalogRegisterProp(
				idbuf, s_BaseProps[i].prop_type,
				s_BaseProps[i].name,
				"", 0, s_BaseProps[i].health);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register prop %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = s_BaseProps[i].prop_type;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base props", n);
		count += n;
	}

	/* ---- game modes ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_GAMEMODES; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseGameModes[i].slug);
			asset_entry_t *e = assetCatalogRegisterGameMode(
				idbuf, s_BaseGameModes[i].mode_id,
				s_BaseGameModes[i].name,
				s_BaseGameModes[i].description,
				s_BaseGameModes[i].min_players,
				s_BaseGameModes[i].max_players,
				s_BaseGameModes[i].team_based);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register gamemode %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = s_BaseGameModes[i].mode_id;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base game modes", n);
		count += n;
	}

	/* ---- audio ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < NUM_BASE_SFX_ENTRIES; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:sfx_%04x", i);
			asset_entry_t *e = assetCatalogRegisterAudio(
				idbuf, i, "", 0, 0, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register audio %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base audio entries", n);
		count += n;
	}

	/* ---- HUD elements ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_HUD; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseHud[i].slug);
			asset_entry_t *e = assetCatalogRegisterHud(
				idbuf, s_BaseHud[i].hud_id,
				s_BaseHud[i].name,
				s_BaseHud[i].element_type, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register HUD element %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = s_BaseHud[i].hud_id;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base HUD elements", n);
		count += n;
	}

	/* ---- prop models (SA-5c) ---- */
	/*
	 * Register every g_ModelStates[] entry (indexed by MODEL_* enum) as an
	 * ASSET_MODEL catalog entry.  This enables mod overrides: a mod registers
	 * an ASSET_MODEL entry with the same runtime_index and a different
	 * source_filenum to replace a specific prop model.
	 *
	 * runtime_index = MODEL_* enum value (index into g_ModelStates[]).
	 * The manifest pipeline uses catalogResolveByRuntimeIndex(ASSET_MODEL, modelnum)
	 * to get the canonical "base:model_%04x" ID.
	 * source_filenum = g_ModelStates[i].fileid (FILE_* ROM constant)
	 *
	 * ASSET_MODEL is separate from ASSET_PROP (which tracks PROPTYPE_*
	 * categories) to avoid runtime_index collisions.
	 */
	{
		s32 n = 0;
		s32 i;
		for (i = 0; i < NUM_MODELS; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:model_%04x", i);
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_MODEL);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register model %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			e->source_filenum = (s32)g_ModelStates[i].fileid;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base prop models (ASSET_MODEL)", n);
		count += n;
	}

	/* ---- lang banks (Phase 3: manifest-based lang loading) ---- */
	{
		s32 n = 0;
		s32 i;
		for (i = 0; i < (s32)NUM_BASE_LANG_BANKS; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseLangBanks[i].slug);
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_LANG);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register lang bank %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = s_BaseLangBanks[i].bank_id;
			e->ext.lang.bank_id = s_BaseLangBanks[i].bank_id;
			/* ENABLED not LOADED: langLoad() must be called explicitly */
			e->load_state = ASSET_STATE_ENABLED; e->ref_count = 0;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base lang banks", n);
		count += n;
	}

	sysLogPrintf(LOG_NOTE, "assetcatalog: T3/T4/T5 extended registration complete (%d entries)", count);
	return count;
}
