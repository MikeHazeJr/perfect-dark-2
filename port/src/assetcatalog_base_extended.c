/**
 * assetcatalog_base_extended.c -- S46a: Extended base game asset registration
 *
 * Registers 7 new asset types in the Asset Catalog:
 *   ASSET_WEAPON    -- all 47 MP weapons (MPWEAPON_* constants)
 *   ASSET_ANIMATION -- 1207 animations (full table, indices 0x0000..0x04B6)
 *   ASSET_TEXTURE   -- NUM_TEXTURES base textures (3503 NTSC / 3511 JPN-final)
 *   ASSET_PROP      -- 8 base prop categories (PROPTYPE_* constants)
 *   ASSET_GAMEMODE  -- all 6 Combat Simulator scenarios (MPSCENARIO_*)
 *   ASSET_AUDIO     -- 1545 SFX entries (full main bank, 0x0000..0x0608)
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
#include "data.h"
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

			/* Register "weapon_%d" alias (weapon_id value) so that the
			 * manifest/session catalog pipeline can resolve "weapon_1" etc.
			 * Mirrors the "stage_0x%02x" alias pattern used for stages. */
			{
				asset_entry_t *ea;
				snprintf(idbuf, sizeof(idbuf), "weapon_%d", (int)s_BaseWeapons[i].weapon_id);
				ea = assetCatalogRegisterWeapon(idbuf, s_BaseWeapons[i].weapon_id,
					s_BaseWeapons[i].name, "", 0.0f, 0.0f, 0,
					s_BaseWeapons[i].dual_wieldable);
				if (ea) {
					strncpy(ea->category, "base", CATALOG_CATEGORY_LEN - 1);
					ea->bundled = 1; ea->enabled = 1; ea->runtime_index = i;
					ea->load_state = ASSET_STATE_LOADED; ea->ref_count = ASSET_REF_BUNDLED;
				}
			}
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

			/* Register "prop_%d" alias (prop_type value) so that any code
			 * path can resolve by numeric index, e.g. "prop_2".
			 * Mirrors the "body_%d" / "weapon_%d" alias pattern. */
			{
				asset_entry_t *ea;
				snprintf(idbuf, sizeof(idbuf), "prop_%d", s_BaseProps[i].prop_type);
				ea = assetCatalogRegisterProp(idbuf, s_BaseProps[i].prop_type,
					s_BaseProps[i].name, "", 0, s_BaseProps[i].health);
				if (ea) {
					strncpy(ea->category, "base", CATALOG_CATEGORY_LEN - 1);
					ea->bundled = 1; ea->enabled = 1;
					ea->runtime_index = s_BaseProps[i].prop_type;
					ea->load_state = ASSET_STATE_LOADED; ea->ref_count = ASSET_REF_BUNDLED;
				}
			}
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

			/* Register "gamemode_%d" alias (mode_id value) so that any code
			 * path can resolve by numeric index, e.g. "gamemode_0". */
			{
				asset_entry_t *ea;
				snprintf(idbuf, sizeof(idbuf), "gamemode_%d", s_BaseGameModes[i].mode_id);
				ea = assetCatalogRegisterGameMode(idbuf, s_BaseGameModes[i].mode_id,
					s_BaseGameModes[i].name, s_BaseGameModes[i].description,
					s_BaseGameModes[i].min_players, s_BaseGameModes[i].max_players,
					s_BaseGameModes[i].team_based);
				if (ea) {
					strncpy(ea->category, "base", CATALOG_CATEGORY_LEN - 1);
					ea->bundled = 1; ea->enabled = 1;
					ea->runtime_index = s_BaseGameModes[i].mode_id;
					ea->load_state = ASSET_STATE_LOADED; ea->ref_count = ASSET_REF_BUNDLED;
				}
			}
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

			/* Register "hud_%d" alias (hud_id value) so that any code
			 * path can resolve by numeric index, e.g. "hud_0". */
			{
				asset_entry_t *ea;
				snprintf(idbuf, sizeof(idbuf), "hud_%d", s_BaseHud[i].hud_id);
				ea = assetCatalogRegisterHud(idbuf, s_BaseHud[i].hud_id,
					s_BaseHud[i].name, s_BaseHud[i].element_type, "");
				if (ea) {
					strncpy(ea->category, "base", CATALOG_CATEGORY_LEN - 1);
					ea->bundled = 1; ea->enabled = 1;
					ea->runtime_index = s_BaseHud[i].hud_id;
					ea->load_state = ASSET_STATE_LOADED; ea->ref_count = ASSET_REF_BUNDLED;
				}
			}
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
	 * runtime_index = MODEL_* enum value (index into g_ModelStates[])
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

			/* Register "model_%d" alias (MODEL_* enum index) so that any
			 * code path can resolve by numeric index, e.g. "model_5".
			 * The manifest pipeline uses catalogResolveByRuntimeIndex which
			 * returns "base:model_%04x"; the alias provides a shorter form. */
			{
				asset_entry_t *ea;
				snprintf(idbuf, sizeof(idbuf), "model_%d", i);
				ea = assetCatalogRegister(idbuf, ASSET_MODEL);
				if (ea) {
					strncpy(ea->category, "base", CATALOG_CATEGORY_LEN - 1);
					ea->bundled = 1; ea->enabled = 1;
					ea->runtime_index = i;
					ea->source_filenum = (s32)g_ModelStates[i].fileid;
					ea->load_state = ASSET_STATE_LOADED; ea->ref_count = ASSET_REF_BUNDLED;
				}
			}
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base prop models (ASSET_MODEL)", n);
		count += n;
	}

	sysLogPrintf(LOG_NOTE, "assetcatalog: T3/T4/T5 extended registration complete (%d entries)", count);
	return count;
}
