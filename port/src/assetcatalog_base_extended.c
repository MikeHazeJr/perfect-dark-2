/**
 * assetcatalog_base_extended.c -- S46a: Extended base game asset registration
 *
 * Registers 7 new asset types in the Asset Catalog:
 *   ASSET_WEAPON    -- all 47 MP weapons (MPWEAPON_* constants)
 *   ASSET_ANIMATION -- 10 representative animations (TODO S46b: full table)
 *   ASSET_TEXTURE   -- 5 stub base textures (TODO S46b: full table)
 *   ASSET_PROP      -- 8 base prop categories (PROPTYPE_* constants)
 *   ASSET_GAMEMODE  -- all 6 Combat Simulator scenarios (MPSCENARIO_*)
 *   ASSET_AUDIO     -- 10 representative SFX entries (TODO S46b: full table)
 *   ASSET_HUD       -- 6 HUD element categories (HUD_ELEM_*)
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
#include "game/mplayer/scenarios.h"

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
 * Representative set from animations.json. anim_id = table index
 * (file NNN.bin -> id NNN). frame_count sourced from JSON where available.
 * TODO S46b: enumerate full animation table (~1000 entries) from JSON.
 */
static const struct {
	s32         anim_id;
	const char *slug;
	const char *name;
	s32         frame_count;
} s_BaseAnimations[] = {
	{   0, "idle",             "Idle",              1 },
	{   1, "two_gun_hold",     "Two-Gun Hold",    163 },
	{   5, "run",              "Run",               0 },
	{   6, "walk",             "Walk",              0 },
	{   7, "crouch",           "Crouch",            0 },
	{   8, "kneel_two_handed", "Kneel Two-Handed", 158 },
	{  50, "death_back",       "Death Backward",    0 },
	{  51, "death_forward",    "Death Forward",     0 },
	{ 100, "gun_raise",        "Gun Raise",         0 },
	{ 101, "gun_lower",        "Gun Lower",         0 },
};

#define NUM_BASE_ANIMATIONS (sizeof(s_BaseAnimations) / sizeof(s_BaseAnimations[0]))

/* ========================================================================
 * Texture Table
 * ======================================================================== */

/*
 * Stub entries for key base textures. width/height/format = 0 (ROM-loaded).
 * TODO S46b: enumerate base texture table from ROM metadata.
 */
static const struct {
	s32         texture_id;
	const char *slug;
	const char *name;
} s_BaseTextures[] = {
	{ 0, "menu_background",  "Menu Background"  },
	{ 1, "loading_screen",   "Loading Screen"   },
	{ 2, "crosshair_dot",    "Crosshair Dot"    },
	{ 3, "health_bar",       "Health Bar"       },
	{ 4, "radar_bg",         "Radar Background" },
};

#define NUM_BASE_TEXTURES (sizeof(s_BaseTextures) / sizeof(s_BaseTextures[0]))

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
 * Representative SFX entries. sound_id = SFX enum value from sfx.h.
 * duration_ms = 0 (unknown). file_path = "" (ROM-embedded).
 * category: 0=AUDIO_CAT_SFX, 1=AUDIO_CAT_MUSIC, 2=AUDIO_CAT_VOICE.
 * TODO S46b: enumerate broader SFX table from sfx.h (1545 entries total).
 */
static const struct {
	s32         sound_id;
	const char *slug;
	const char *name;
	s32         category;
} s_BaseAudio[] = {
	{ 0x0001, "sfx_rocket_launch",   "Rocket Launch",         0 },
	{ 0x0002, "sfx_horizon_equip",   "Horizon Scanner Equip", 0 },
	{ 0x0010, "sfx_bottle_break",    "Bottle Break",          0 },
	{ 0x002b, "sfx_menu_cancel",     "Menu Cancel",           0 },
	{ 0x003e, "sfx_hud_message",     "HUD Message",           0 },
	{ 0x0052, "sfx_regen",           "Shield Regen",          0 },
	{ 0x005b, "sfx_cloak_on",        "Cloak Activate",        0 },
	{ 0x005c, "sfx_cloak_off",       "Cloak Deactivate",      0 },
	{ 0x000d, "sfx_argh_female",     "Pain Female",           2 },
	{ 0x0000, "sfx_silence",         "Silence",               0 },
};

#define NUM_BASE_AUDIO (sizeof(s_BaseAudio) / sizeof(s_BaseAudio[0]))

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
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base weapons", n);
		count += n;
	}

	/* ---- animations ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_ANIMATIONS; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseAnimations[i].slug);
			asset_entry_t *e = assetCatalogRegisterAnimation(
				idbuf, s_BaseAnimations[i].anim_id,
				s_BaseAnimations[i].name,
				s_BaseAnimations[i].frame_count, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register animation %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = s_BaseAnimations[i].anim_id;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base animations (partial -- TODO S46b)", n);
		count += n;
	}

	/* ---- textures (stub set) ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_TEXTURES; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseTextures[i].slug);
			asset_entry_t *e = assetCatalogRegisterTexture(
				idbuf, s_BaseTextures[i].texture_id,
				0, 0, 0, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register texture %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = s_BaseTextures[i].texture_id;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base textures (stub -- TODO S46b)", n);
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
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base game modes", n);
		count += n;
	}

	/* ---- audio ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_AUDIO; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseAudio[i].slug);
			asset_entry_t *e = assetCatalogRegisterAudio(
				idbuf, s_BaseAudio[i].sound_id,
				s_BaseAudio[i].name,
				s_BaseAudio[i].category, 0, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register audio %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = s_BaseAudio[i].sound_id;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base audio entries (partial -- TODO S46b)", n);
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
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base HUD elements", n);
		count += n;
	}

	sysLogPrintf(LOG_NOTE, "assetcatalog: S46a extended registration complete (%d entries)", count);
	return count;
}
