/**
 * assetcatalog_base_extended.c -- S46a: Extended base game asset registration
 *
 * Registers 9 base game asset types in the Asset Catalog:
 *   ASSET_WEAPON      -- all 41 MP weapon slots (MPWEAPON_* constants)
 *                       requirefeature populated from g_MpWeapons[].unlockfeature
 *                       (catalog universality sweep, 2026-04-27)
 *   ASSET_ANIMATION   -- 1207 animations (full table, indices 0x0000..0x04B6)
 *   ASSET_TEXTURE     -- NUM_TEXTURES base textures (3503 NTSC / 3511 JPN-final)
 *   ASSET_PROP        -- 8 base prop categories (PROPTYPE_* constants)
 *   ASSET_MATERIAL    -- base renderer/material presets
 *   ASSET_SKIN        -- one editable default skin binding per base body
 *   ASSET_EFFECT      -- base renderer/effect archetypes
 *   ASSET_VEHICLE     -- base vehicle archetypes
 *   ASSET_GAMEMODE    -- all 6 Combat Simulator scenarios (MPSCENARIO_*)
 *                       requirefeature populated from g_MpScenarioOverviews[].requirefeature
 *                       (catalog universality sweep, 2026-04-27)
 *   ASSET_AUDIO       -- 1545 SFX entries (full main bank, 0x0000..0x0608)
 *                     -- 43 music tracks (g_MpTracks[], AUDIO_CAT_MUSIC)
 *                       unlockstage populated from g_MpTracks[].unlockstage
 *                       (catalog universality sweep, 2026-04-27)
 *   ASSET_HUD         -- 6 HUD element categories (HUD_ELEM_*)
 *   ASSET_LANG        -- 68 language string banks (LANGBANK_* constants)
 *   ASSET_BOT_PROFILE -- 18 base MP simulant profiles (g_BotProfiles[])
 *                       (catalog universality sweep, 2026-04-27)
 *   ASSET_MODEL       -- base prop models plus first-person hand model files
 *
 * Called by assetCatalogRegisterBaseGame() at the end of base registration.
 * Auto-discovered by CMake glob (port/*.c). No build system changes needed.
 */

#include <PR/ultratypes.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "constants.h"
#include "catalog_readable_ids.h"
#include "files.h"
#include "assetcatalog.h"
#include "fs.h"          /* Phase 3 Pass B: fsFullPath probe of extracted file */
#include "loader_enum_reverse.h"
#include "romextract.h"  /* Phase 3 Pass B: romExtractRelPathForFilenum */
#include "system.h"
#include "data.h"
#include "game/mplayer/scenarios.h"
#include "game/lang.h"
/* S484-followup (2026-05-01): weapon model file registration reads
 * hi_model / lo_model out of the loader-populated weapon pool.
 * Client-only: pd-server doesn't link catalog_mgr_weapons.c (the loader
 * pool is never populated server-side), so guard the include too. */
#if !defined(PD_SERVER)
#include "catalog_mgr_weapons.h"
#endif
/* BYOR completion (2026-05-03): hand-model probe reads from
 * bodydata_authored.c instead of g_HeadsAndBodies[]. */
#include "bodydata_authored.h"

/* Catalog universality sweep (2026-04-27): externs for sources that
 * carry the unlock-state fields the catalog now mirrors.  Layer A
 * remains the data home; the catalog adds requirefeature / unlockstage
 * so selectors can ride assetCatalogIterateUnlockedByType. */
extern struct mpweapon          g_MpWeapons[];
extern struct mpscenariooverview g_MpScenarioOverviews[];
extern struct mptrack           g_MpTracks[];
extern struct botprofile        g_BotProfiles[18];

#ifndef MP_BODY_COUNT
#define MP_BODY_COUNT 63
#endif

static s32 s_catalogIdSeen(char ids[][CATALOG_ID_LEN], s32 count, const char *id)
{
	for (s32 i = 0; i < count; i++) {
		if (strncmp(ids[i], id, CATALOG_ID_LEN) == 0) {
			return 1;
		}
	}
	return 0;
}

static void s_noteCatalogId(char ids[][CATALOG_ID_LEN], s32 *count, const char *id)
{
	if (!ids || !count || !id) return;
	if (*count < NUM_MODELS) {
		strncpy(ids[*count], id, CATALOG_ID_LEN - 1);
		ids[*count][CATALOG_ID_LEN - 1] = '\0';
		(*count)++;
	}
}

static void s_makeReadableCatalogIdUnique(char ids[][CATALOG_ID_LEN],
	s32 count, s32 ordinal, char *id, size_t id_n)
{
	if (!s_catalogIdSeen(ids, count, id)) {
		return;
	}

	char base[CATALOG_ID_LEN];
	char alpha[24];
	strncpy(base, id, sizeof(base) - 1);
	base[sizeof(base) - 1] = '\0';
	catalogReadableAlphaOrdinal(ordinal, alpha, sizeof(alpha));
	snprintf(id, id_n, "%s_variant_%s", base, alpha);
}

static void s_catalogIdToFilenameSlug(const char *id, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	if (!id) return;
	size_t j = 0;
	for (size_t i = 0; id[i] && j + 1 < out_n; i++) {
		char c = id[i];
		out[j++] = (c == ':' || c == '/' || c == '\\') ? '_' : c;
	}
	out[j] = '\0';
}

static void s_buildArchiveMemberPath(const char *dir, const char *id,
	const char *ext, const char *member, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	char slug[CATALOG_ID_LEN];
	char rel[FS_MAXPATH];
	s_catalogIdToFilenameSlug(id, slug, sizeof(slug));
	snprintf(rel, sizeof(rel), "%s/%s%s::%s", dir, slug, ext, member);
	fsDataPathFor(rel, out, out_n);
}

static const char *s_bodyCatalogIdForMpIndex(s32 mp_body_index)
{
	if (mp_body_index < 0 || mp_body_index >= MP_BODY_COUNT) {
		return NULL;
	}
	s32 bodynum = (s32)g_MpBodies[mp_body_index].bodynum;
	for (s32 i = 0; i < assetCatalogGetCount(); i++) {
		const asset_entry_t *e = assetCatalogGetByIndex(i);
		if (e && e->occupied && e->type == ASSET_BODY &&
				e->runtime_index == bodynum) {
			return e->id;
		}
	}
	return NULL;
}

/* ========================================================================
 * Weapon Table
 * ======================================================================== */

/*
 * Maps MPWEAPON_* constant -> catalog slug, display name, dual-wield flag.
 * weapon_id is the base MP weapon table slot (MPWEAPON_* range 0x00-0x28).
 * Private custom MP slots live after MPWEAPON_DISABLED and are allocated
 * by the .pdweapon walker from catalog IDs, not listed here.
 * Mesh, stats, and behavior are populated by the .pdweapon emitter/loader path.
 */
static const struct {
	s32         weapon_id;
	const char *slug;
	const char *name;
	s32         dual_wieldable;
} s_BaseWeapons[] = {
	{ MPWEAPON_NONE,            "nothing",           "Nothing",             0 },
	{ MPWEAPON_FALCON2,         "falcon2",           "Falcon 2",            0 },
	{ MPWEAPON_FALCON2_SILENCER, "falcon2_silencer", "Falcon 2 Silencer",   0 },
	{ MPWEAPON_FALCON2_SCOPE,   "falcon2_scope",     "Falcon 2 Scope",      1 },
	{ MPWEAPON_MAGSEC4,         "magsec",            "Magsec 4",            1 },
	{ MPWEAPON_MAULER,          "mauler",            "Mauler",              1 },
	{ MPWEAPON_PHOENIX,         "phoenix",           "Phoenix",             0 },
	{ MPWEAPON_DY357MAGNUM,     "dy357",             "DY357 Magnum",        1 },
	{ MPWEAPON_DY357LX,         "dy357lx",           "DY357-LX",            1 },
	{ MPWEAPON_CMP150,          "cmp150",            "CMP150",              1 },
	{ MPWEAPON_CYCLONE,         "cyclone",           "Cyclone",             1 },
	{ MPWEAPON_CALLISTO,        "callisto",          "Callisto NTG",        1 },
	{ MPWEAPON_RCP120,          "rcp120",            "RC-P120",             0 },
	{ MPWEAPON_LAPTOPGUN,       "laptopgun",         "Laptop Gun",          0 },
	{ MPWEAPON_DRAGON,          "dragon",            "Dragon",              0 },
	{ MPWEAPON_K7AVENGER,       "k7avenger",         "K7 Avenger",          0 },
	{ MPWEAPON_AR34,            "ar34",              "AR34",                0 },
	{ MPWEAPON_SUPERDRAGON,     "superdragon",       "SuperDragon",         0 },
	{ MPWEAPON_SHOTGUN,         "shotgun",           "Shotgun",             0 },
	{ MPWEAPON_REAPER,          "reaper",            "Reaper",              0 },
	{ MPWEAPON_SNIPERRIFLE,     "sniperrifle",       "Sniper Rifle",        0 },
	{ MPWEAPON_FARSIGHT,        "farsight",          "FarSight XR-20",      0 },
	{ MPWEAPON_DEVASTATOR,      "devastator",        "Devastator",          0 },
	{ MPWEAPON_ROCKETLAUNCHER,  "rocketlauncher",    "Rocket Launcher",     0 },
	{ MPWEAPON_SLAYER,          "slayer",            "Slayer",              0 },
	{ MPWEAPON_COMBATKNIFE,     "combatknife",       "Combat Knife",        1 },
	{ MPWEAPON_CROSSBOW,        "crossbow",          "Crossbow",            0 },
	{ MPWEAPON_TRANQUILIZER,    "tranquilizer",      "Tranquilizer",        0 },
	{ MPWEAPON_GRENADE,         "grenade",           "Grenade",             0 },
	{ MPWEAPON_NBOMB,           "nbomb",             "N-Bomb",              0 },
	{ MPWEAPON_TIMEDMINE,       "timedmine",         "Timed Mine",          0 },
	{ MPWEAPON_PROXIMITYMINE,   "proximitymine",     "Proximity Mine",      0 },
	{ MPWEAPON_REMOTEMINE,      "remotemine",        "Remote Mine",         0 },
	{ MPWEAPON_LASER,           "laser",             "Laser",               0 },
	{ MPWEAPON_XRAYSCANNER,     "xrayscanner",       "X-Ray Scanner",       0 },
	{ MPWEAPON_NIGHTVISION,     "nightvision",       "Night Vision",        0 },
	{ MPWEAPON_IRSCANNER,       "irscanner",         "IR Scanner",          0 },
	{ MPWEAPON_CLOAKINGDEVICE,  "cloakingdevice",    "Cloaking Device",     0 },
	{ MPWEAPON_COMBATBOOST,     "combatboost",       "Combat Boost",        0 },
	{ MPWEAPON_SHIELD,          "hammer_slot83",     "Shield",              0 },
	{ MPWEAPON_DISABLED,        "hammer_slot84",     "Disabled",            0 },
};

#define NUM_BASE_WEAPONS (sizeof(s_BaseWeapons) / sizeof(s_BaseWeapons[0]))
_Static_assert(NUM_BASE_WEAPONS == MPWEAPON_DISABLED + 1,
	"base weapon catalog table must cover every base MPWEAPON_* slot");

/* ========================================================================
 * Animation Table
 * ======================================================================== */

/*
 * Full animation table: 1207 entries (indices 0x0000..0x04B6).
 * Count derived from src/assets/ntsc-final/animations.json (1207 entries,
 * last file 04b6.bin). IDs are generated through catalog_readable_ids so
 * the public catalog identity stays symbolic/readable; metadata zeroed
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
 * IDs are generated as readable texture placeholders; the numeric texture
 * index is kept only in runtime_index metadata for extraction/adapter lookup.
 */
/* NUM_TEXTURES pulled from constants.h */

/* ========================================================================
 * Prop Table
 * ======================================================================== */

/*
 * One entry per PROPTYPE_* constant (fundamental prop categories).
 * These are prop category definitions emitted as .pdprop source archives.
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
 * Material / Effect / Vehicle Tables
 * ======================================================================== */

static const struct {
	const char *slug;
	const char *name;
	const char *shading_model;
	f32 base_color[4];
	f32 roughness;
	f32 metallic;
} s_BaseMaterials[] = {
	{ "material_default", "Default Material", "classic_lit",
		{ 1.0f, 1.0f, 1.0f, 1.0f }, 0.55f, 0.0f },
	{ "material_translucent", "Translucent Material", "classic_alpha",
		{ 1.0f, 1.0f, 1.0f, 0.5f }, 0.45f, 0.0f },
	{ "material_emissive", "Emissive Material", "classic_emissive",
		{ 1.0f, 0.92f, 0.62f, 1.0f }, 0.2f, 0.0f },
};

#define NUM_BASE_MATERIALS (sizeof(s_BaseMaterials) / sizeof(s_BaseMaterials[0]))

static const struct {
	const char *slug;
	const char *name;
	s32 effect_type;
	s32 target;
	const char *shader_id;
	f32 intensity;
} s_BaseEffects[] = {
	{ "effect_tint", "Tint", EFFECT_TYPE_TINT, EFFECT_TARGET_SCENE, "classic_tint", 1.0f },
	{ "effect_glow", "Glow", EFFECT_TYPE_GLOW, EFFECT_TARGET_PROP, "classic_glow", 1.0f },
	{ "effect_shimmer", "Shimmer", EFFECT_TYPE_SHIMMER, EFFECT_TARGET_PROP, "classic_shimmer", 1.0f },
	{ "effect_darken", "Darken", EFFECT_TYPE_DARKEN, EFFECT_TARGET_SCENE, "classic_darken", 1.0f },
	{ "effect_screen", "Screen Effect", EFFECT_TYPE_SCREEN, EFFECT_TARGET_PLAYER, "classic_screen", 1.0f },
	{ "effect_particle", "Particle", EFFECT_TYPE_PARTICLE, EFFECT_TARGET_PROP, "classic_particle", 1.0f },
};

#define NUM_BASE_EFFECTS (sizeof(s_BaseEffects) / sizeof(s_BaseEffects[0]))

static const struct {
	const char *slug;
	const char *name;
	s32 modelnum;
	const char *behavior_key;
} s_BaseVehicles[] = {
	{ "vehicle_hoverbike", "Hoverbike", MODEL_HOVBIKE, "og.hoverbike" },
};

#define NUM_BASE_VEHICLES (sizeof(s_BaseVehicles) / sizeof(s_BaseVehicles[0]))

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
 * The high-bit mapped entries (SFX_8000+) are configured sound references.
 * They are registered as readable aliases so authored source can preserve
 * the same sound number the native runtime receives, including audioconfig
 * volume/pan/filter behavior.
 *
 * Per Phase 3 Pass B Slice 10 (2026-05-02), entries whose russ-id falls
 * inside g_AudioRussMappings[] AND whose audioconfig is one of the seven
 * voice-only slots register as AUDIO_CAT_VOICE; remaining base SFX
 * register as AUDIO_CAT_SFX (category = 0). See
 * context/audits/catalog-phase3-slice10-voice-retag-2026-05-02.md
 * for the full inventory + the criteria.
 *
 * B-778 (2026-06-06): weapon animation command source can carry these
 * configured sound IDs directly. Keeping them out of the catalog made
 * valid public source fail to resolve at runtime and, when normalized,
 * dropped the original audioconfig behavior.
 */
#define NUM_BASE_SFX_ENTRIES 1545

/* ========================================================================
 * Phase 3 Pass B Slice 10 (2026-05-02): voice retag predicate.
 *
 * The russ table at src/lib/snd.c:178 maps russ-id (0..0x01bc) to
 * { soundnum, audioconfig_index }. Seven of the audioconfig slots are
 * exclusively used by voice content (per the russ-table inline comments
 * and config-flag patterns; see audit Section B.2). Catalog SFX entries
 * whose runtime_index lies in the russ-table range AND whose paired
 * audioconfig is one of these seven slots get tagged AUDIO_CAT_VOICE
 * instead of the default AUDIO_CAT_SFX.
 *
 * The slot enumerator names live in src/lib/snd.c::enum audioconfig_e
 * which is file-local; this TU mirrors the integer literals here. The
 * enum is dense sequential and stable per snd.c:105-176.
 *
 * AUDIOCONFIG_62 only exists on NTSC-1.0+ builds; the russ entries
 * that reference it are gated on the same VERSION macro, so the
 * predicate's #if guard keeps version determinism.
 * ======================================================================== */
#if !defined(PD_SERVER)
#ifndef AUDIOCONFIG_01
#define AUDIOCONFIG_01 1
#define AUDIOCONFIG_02 2
#define AUDIOCONFIG_03 3
#define AUDIOCONFIG_47 47
#define AUDIOCONFIG_48 48
#define AUDIOCONFIG_60 60
#define AUDIOCONFIG_62 62
#endif

static s32 s_audioConfigIsVoice(s32 audioconfig_idx)
{
	switch (audioconfig_idx) {
	case AUDIOCONFIG_01:
	case AUDIOCONFIG_02:
	case AUDIOCONFIG_03:
	case AUDIOCONFIG_47:
	case AUDIOCONFIG_48:
	case AUDIOCONFIG_60:
#if VERSION >= VERSION_NTSC_1_0
	case AUDIOCONFIG_62:
#endif
		return 1;
	default:
		return 0;
	}
}
#endif /* !PD_SERVER */

/* ========================================================================
 * Music Track Table (base game)
 *
 * Maps g_MpTracks[] index to human-readable catalog slug + display name.
 * musicnum is the N64 sequencer index (MUSIC_* enum from sequences.h).
 * duration is in seconds (from g_MpTracks[].duration field).
 * Slug format: "track_<descriptive_name>" — matches catalog ID mandate.
 * ======================================================================== */

static const struct {
	s32         musicnum;     /* MUSIC_* enum value (from sequences.h via constants.h) */
	s32         duration;     /* seconds (from g_MpTracks[].duration) */
	const char *slug;         /* catalog slug (without "base:" prefix) */
	const char *display_name; /* human-readable display name */
} s_BaseMusicTracks[] = {
	{ MUSIC_DARK_COMBAT,       160, "track_dark_combat",           "Dark Combat" },
	{ MUSIC_SKEDAR_MYSTERY,    170, "track_skedar_mystery",        "Skedar Mystery" },
	{ MUSIC_CI_OPERATIVE,      170, "track_ci_operative",          "CI Operative" },
	{ MUSIC_DATADYNE_ACTION,   180, "track_datadyne_action",       "dataDyne Action" },
	{ MUSIC_MAIAN_TEARS,       200, "track_maian_tears",           "Maian Tears" },
	{ MUSIC_ALIEN_CONFLICT,    197, "track_alien_conflict",        "Alien Conflict" },
	{ MUSIC_CI,                120, "track_carrington_institute",  "Carrington Institute" },
	{ MUSIC_DEFECTION,         120, "track_dd_central",            "dD Central" },
	{ MUSIC_DEFECTION_X,       120, "track_dd_central_x",          "dD Central X" },
	{ MUSIC_INVESTIGATION,     120, "track_dd_research",           "dD Research" },
	{ MUSIC_INVESTIGATION_X,   120, "track_dd_research_x",         "dD Research X" },
	{ MUSIC_EXTRACTION,        120, "track_dd_extraction",         "dD Extraction" },
	{ MUSIC_EXTRACTION_X,      120, "track_dd_extraction_x",       "dD Extraction X" },
	{ MUSIC_VILLA,             120, "track_carrington_villa",      "Carrington Villa" },
	{ MUSIC_VILLA_X,           120, "track_carrington_villa_x",    "Carrington Villa X" },
	{ MUSIC_CHICAGO,           120, "track_chicago",               "Chicago" },
	{ MUSIC_CHICAGO_X,         120, "track_chicago_x",             "Chicago X" },
	{ MUSIC_G5,                120, "track_g5_building",           "G5 Building" },
	{ MUSIC_G5_X,              120, "track_g5_building_x",         "G5 Building X" },
	{ MUSIC_INFILTRATION,      120, "track_a51_infiltration",      "A51 Infiltration" },
	{ MUSIC_INFILTRATION_X,    120, "track_a51_infiltration_x",    "A51 Infiltration X" },
	{ MUSIC_RESCUE,            120, "track_a51_rescue",            "A51 Rescue" },
	{ MUSIC_RESCUE_X,          120, "track_a51_rescue_x",          "A51 Rescue X" },
	{ MUSIC_ESCAPE,            120, "track_a51_escape",            "A51 Escape" },
	{ MUSIC_ESCAPE_X,          120, "track_a51_escape_x",          "A51 Escape X" },
	{ MUSIC_AIRBASE,           120, "track_air_base",              "Air Base" },
	{ MUSIC_AIRBASE_X,         120, "track_air_base_x",            "Air Base X" },
	{ MUSIC_AIRFORCEONE,       120, "track_air_force_one",         "Air Force One" },
	{ MUSIC_AIRFORCEONE_X,     120, "track_air_force_one_x",       "Air Force One X" },
	{ MUSIC_CRASHSITE,         120, "track_crash_site",            "Crash Site" },
	{ MUSIC_CRASHSITE_X,       120, "track_crash_site_x",          "Crash Site X" },
	{ MUSIC_PELAGIC,           120, "track_pelagic_ii",            "Pelagic II" },
	{ MUSIC_PELAGIC_X,         120, "track_pelagic_ii_x",          "Pelagic II X" },
	{ MUSIC_DEEPSEA,           120, "track_deep_sea",              "Deep Sea" },
	{ MUSIC_DEEPSEA_X,         120, "track_deep_sea_x",            "Deep Sea X" },
	{ MUSIC_DEFENSE,           120, "track_institute_defense",     "Institute Defense" },
	{ MUSIC_DEFENSE_X,         120, "track_institute_defense_x",   "Institute Defense X" },
	{ MUSIC_ATTACKSHIP,        120, "track_attack_ship",           "Attack Ship" },
	{ MUSIC_ATTACKSHIP_X,      120, "track_attack_ship_x",         "Attack Ship X" },
	{ MUSIC_SKEDARRUINS,       120, "track_skedar_ruins",          "Skedar Ruins" },
	{ MUSIC_SKEDARRUINS_X,     120, "track_skedar_ruins_x",        "Skedar Ruins X" },
	{ MUSIC_CREDITS,           120, "track_end_credits",           "End Credits" },
	{ MUSIC_SKEDARRUINS_KING,  120, "track_skedar_warrior",        "Skedar Warrior" },
};

#define NUM_BASE_MUSIC_TRACKS (sizeof(s_BaseMusicTracks) / sizeof(s_BaseMusicTracks[0]))

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
 * When extracted .pdlang archives are present, loader_walker_lang.c registers
 * locale-suffixed base rows such as base:lang_options_en with public
 * strings.json FileProvider source. langLoad() prefers those source rows.
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
				"",
				s_BaseWeapons[i].dual_wieldable);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register weapon %s", idbuf);
				continue;
			}
			s32 mpw = s_BaseWeapons[i].weapon_id;
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = (mpw >= 0 && mpw < NUM_MPWEAPONS)
				? (s32)g_MpWeapons[mpw].weaponnum : -1;
			e->mp_index = (s16)mpw;
			/* Catalog universality sweep (2026-04-27): mirror the unlock
			 * gate so assetCatalogIterateUnlockedByType(ASSET_WEAPON, ...)
			 * matches challengeIsFeatureUnlocked semantics. */
			e->ext.weapon.requirefeature = (mpw >= 0 && mpw < NUM_MPWEAPONS)
				? g_MpWeapons[mpw].unlockfeature : 0;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "CATALOG.SWEEP: registered %d base weapons (with requirefeature)", n);
		count += n;
	}

	/* ---- animations ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < NUM_BASE_ANIM_ENTRIES; i++) {
			catalogReadableAnimationId(i, "unlabeled", idbuf, sizeof(idbuf));
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
			catalogReadableTextureId(i, idbuf, sizeof(idbuf));
			asset_entry_t *e = assetCatalogRegisterTexture(
				idbuf, i, 0, 0, 0, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register texture %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			s_buildArchiveMemberPath("textures", idbuf, ".pdtexture",
				"texture.png", e->ext.texture.file_path,
				sizeof(e->ext.texture.file_path));
			catalogSetPrimaryFile(e, e->ext.texture.file_path);
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
			s_buildArchiveMemberPath("props", idbuf, ".pdprop",
				"prop.json", e->ext.prop.prop_file,
				sizeof(e->ext.prop.prop_file));
			catalogSetPrimaryFile(e, e->ext.prop.prop_file);
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base props", n);
		count += n;
	}

	/* ---- materials ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_MATERIALS; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseMaterials[i].slug);
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_MATERIAL);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register material %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			s_buildArchiveMemberPath("materials", idbuf, ".pdmaterial",
				"material.json", e->ext.material.material_file,
				sizeof(e->ext.material.material_file));
			catalogSetPrimaryFile(e, e->ext.material.material_file);
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base materials", n);
		count += n;
	}

	/* ---- default body skins ---- */
	{
		s32 n = 0;
		s32 body_count_snapshot = assetCatalogGetCount();
		for (s32 i = 0; i < body_count_snapshot; i++) {
			const asset_entry_t *body = assetCatalogGetByIndex(i);
			if (!body || !body->occupied || body->type != ASSET_BODY ||
					!body->id[0]) {
				continue;
			}
			const char *body_slug = body->id;
			if (strncmp(body_slug, "base:body_", 10) == 0) {
				body_slug += 10;
			} else if (strncmp(body_slug, "base:", 5) == 0) {
				body_slug += 5;
			}
			char skin_id[CATALOG_ID_LEN];
			snprintf(skin_id, sizeof(skin_id), "base:skin_%s_default", body_slug);
			asset_entry_t *e = assetCatalogRegisterSkin(skin_id, body->id);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register skin %s", skin_id);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = body->runtime_index;
			s_buildArchiveMemberPath("skins", skin_id, ".pdskin",
				"skin.json", e->ext.skin.skin_file,
				sizeof(e->ext.skin.skin_file));
			s_buildArchiveMemberPath("skins", skin_id, ".pdskin",
				"swatches.json", e->ext.skin.swatches_file,
				sizeof(e->ext.skin.swatches_file));
			catalogSetPrimaryFile(e, e->ext.skin.skin_file);
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base default skins", n);
		count += n;
	}

	/* ---- effects ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_EFFECTS; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseEffects[i].slug);
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_EFFECT);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register effect %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			strncpy(e->ext.effect.name, s_BaseEffects[i].name,
				sizeof(e->ext.effect.name) - 1);
			e->ext.effect.effect_type = s_BaseEffects[i].effect_type;
			e->ext.effect.target = s_BaseEffects[i].target;
			strncpy(e->ext.effect.shader_id, s_BaseEffects[i].shader_id,
				sizeof(e->ext.effect.shader_id) - 1);
			e->ext.effect.intensity = s_BaseEffects[i].intensity;
			s_buildArchiveMemberPath("effects", idbuf, ".pdeffect",
				"effect.graph.json", e->ext.effect.effect_file,
				sizeof(e->ext.effect.effect_file));
			catalogSetPrimaryFile(e, e->ext.effect.effect_file);
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base effects", n);
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
			e->mp_index = (s16)s_BaseGameModes[i].mode_id;
			/* Catalog universality sweep (2026-04-27): mode_id matches the
			 * scenario's index into g_MpScenarioOverviews[] (MPSCENARIO_*),
			 * so the requirefeature read is direct. */
			e->ext.gamemode.requirefeature = g_MpScenarioOverviews[s_BaseGameModes[i].mode_id].requirefeature;
			s_buildArchiveMemberPath("gamemodes", idbuf, ".pdgamemode",
				"rules.json", e->ext.gamemode.rules_file,
				sizeof(e->ext.gamemode.rules_file));
			catalogSetPrimaryFile(e, e->ext.gamemode.rules_file);
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "CATALOG.SWEEP: registered %d base game modes (with requirefeature)", n);
		count += n;
	}

	/* ---- audio (SFX + Phase 3 Slice 10 voice retag) ---- */
	{
#if !defined(PD_SERVER)
		/* The russ table lives in src/lib/snd.c which is client-only.
		 * Server builds get plain SFX classification for every entry;
		 * pd-server has no audio runtime and never consults the
		 * voice/SFX distinction.
		 *
		 * c106 sister fix (2026-05-13): the prior code treated
		 * g_AudioRussMappings[i] as if i is a leaf SFX index, but the
		 * russ table is indexed by `confignum` extracted from each
		 * sound's packed soundnum (per propsnd.c::psGetTheoreticalVolPan
		 * line 1409-1412 and union soundnumhack in types.h:3484). The
		 * two index spaces are different. The correct mapping is: walk
		 * the russ table, decode each entry's packed soundnum to get
		 * the 11-bit leaf SFX `id`, and flag that index as voice. Then
		 * during the leaf-SFX seed loop use the bitset instead of the
		 * raw parallel-array assumption. Identical pattern to the c106
		 * fix in port/src/romextract_pdsfx.c::s_buildVoiceCache.
		 *
		 * Audit ref: context/audits/2026-05-13-followup-and-migration
		 * -sweep.md MF-2. */
		const s32 russCount = g_NumAudioRussMappings;
		u8 voice_cache[NUM_BASE_SFX_ENTRIES];
		memset(voice_cache, 0, sizeof(voice_cache));
		for (s32 r = 0; r < russCount; r++) {
			s32 cfg = (s32)g_AudioRussMappings[r].audioconfig_index;
			if (!s_audioConfigIsVoice(cfg)) continue;
			union soundnumhack hack;
			hack.packed = g_AudioRussMappings[r].soundnum;
			s32 sfx_idx = (s32)hack.id;
			if ((u32)sfx_idx < NUM_BASE_SFX_ENTRIES) {
				voice_cache[sfx_idx] = 1;
			}
		}
#else
		const s32 russCount = 0;
#endif
		s32 sfx_n = 0;
		s32 voice_n = 0;
		for (s32 i = 0; i < NUM_BASE_SFX_ENTRIES; i++) {
			s32 category = AUDIO_CAT_SFX;
#if !defined(PD_SERVER)
			if (voice_cache[i]) {
				category = AUDIO_CAT_VOICE;
			}
#else
			(void)russCount;
#endif
			if (category == AUDIO_CAT_VOICE) {
				catalogReadableVoiceId(i, idbuf, sizeof(idbuf));
			} else {
				catalogReadableSfxId(i, idbuf, sizeof(idbuf));
			}
			asset_entry_t *e = assetCatalogRegisterAudio(
				idbuf, i, "", category, 0, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register audio %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			if (category == AUDIO_CAT_VOICE) voice_n++;
			else                              sfx_n++;
		}
		sysLogPrintf(LOG_NOTE,
			"assetcatalog: registered %d base audio entries (%d SFX + %d VOICE)",
			sfx_n + voice_n, sfx_n, voice_n);
		count += sfx_n + voice_n;

#if !defined(PD_SERVER)
		s32 alias_n = 0;
		for (s32 r = 0; r < russCount; r++) {
			union soundnumhack ref;
			ref.packed = 0;
			ref.hasconfig = 1;
			ref.confignum = (u16)r;

			s32 packed = (s32)(u16)ref.packed;
			if (!loaderEnumNameForSfxEnum(packed)) {
				continue;
			}

			catalogReadableSfxId(packed, idbuf, sizeof(idbuf));
			asset_entry_t *e = assetCatalogRegisterAudio(
				idbuf, packed, "", AUDIO_CAT_SFX, 0, "");
			if (!e) {
				sysLogPrintf(LOG_ERROR,
					"assetcatalog: failed to register configured audio alias %s",
					idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = packed;
			e->load_state = ASSET_STATE_LOADED;
			e->ref_count = ASSET_REF_BUNDLED;
			alias_n++;
		}
		sysLogPrintf(LOG_NOTE,
			"assetcatalog: registered %d configured SFX aliases", alias_n);
		count += alias_n;
#endif
	}

	/* ---- music tracks (AUDIO_CAT_MUSIC) ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_MUSIC_TRACKS; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseMusicTracks[i].slug);
			asset_entry_t *e = assetCatalogRegisterAudio(
				idbuf,
				s_BaseMusicTracks[i].musicnum,
				s_BaseMusicTracks[i].display_name,
				AUDIO_CAT_MUSIC,
				s_BaseMusicTracks[i].duration * 1000, /* seconds -> ms */
				"");
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register music track %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i; /* index into g_MpTracks[] */
			e->mp_index = (s16)i; /* base tracks: catalog mp_index mirrors g_MpTracks[] index */
			/* Catalog universality sweep (2026-04-27): mirror the
			 * best-time gate so assetCatalogIterateUnlockedMusic matches
			 * mpIsTrackUnlocked semantics. */
			e->ext.audio.unlockstage = g_MpTracks[i].unlockstage;
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "CATALOG.SWEEP: registered %d base music tracks (with unlockstage)", n);
		count += n;
	}

	/* ---- bot profiles (g_BotProfiles[]) ---- */
	/*
	 * Catalog universality sweep (2026-04-27): bot profiles register as
	 * ASSET_BOT_PROFILE so the simulant pickers ride the catalog
	 * INTERSECT unlock-state pattern.  Layer A `g_BotProfiles[]` remains
	 * the source of record (AI dispatch reads `botprofile.type` /
	 * `.difficulty` / `.body` directly); only the SELECTORS migrate.
	 *
	 * Catalog ID = "base:bot_<bottype>_<difficulty>" where the slug is
	 * derived from BOTTYPE_* + BOTDIFF_* short names.  The mapping is
	 * stable across builds because g_BotProfiles[] is enum-keyed.
	 */
	{
		static const char *const s_BotProfileSlugs[18] = {
			"bot_meat",         /* GENERAL  / MEAT    */
			"bot_easy",         /* GENERAL  / EASY    */
			"bot_normal",       /* GENERAL  / NORMAL  */
			"bot_hard",         /* GENERAL  / HARD    */
			"bot_perfect",      /* GENERAL  / PERFECT */
			"bot_dark",         /* GENERAL  / DARK    */
			"bot_peace",        /* PEACE    / NORMAL  */
			"bot_shield",       /* SHIELD   / NORMAL  */
			"bot_rocket",       /* ROCKET   / NORMAL  */
			"bot_kaze",         /* KAZE     / NORMAL  */
			"bot_fist",         /* FIST     / NORMAL  */
			"bot_prey",         /* PREY     / NORMAL  */
			"bot_coward",       /* COWARD   / NORMAL  */
			"bot_judge",        /* JUDGE    / NORMAL  */
			"bot_feud",         /* FEUD     / NORMAL  */
			"bot_speed",        /* SPEED    / NORMAL  */
			"bot_turtle",       /* TURTLE   / NORMAL  */
			"bot_venge",        /* VENGE    / NORMAL  */
		};
		s32 n = 0;
		for (s32 i = 0; i < 18; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BotProfileSlugs[i]);
			asset_entry_t *e = assetCatalogRegisterBotProfile(
				idbuf,
				g_BotProfiles[i].type,
				g_BotProfiles[i].difficulty,
				g_BotProfiles[i].body,
				g_BotProfiles[i].name,
				g_BotProfiles[i].requirefeature);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register bot profile %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i; /* index into g_BotProfiles[] */
			e->mp_index = (s16)i;
			{
				const char *body_id = s_bodyCatalogIdForMpIndex(g_BotProfiles[i].body);
				if (body_id) {
					strncpy(e->ext.bot_profile.target_body, body_id,
						sizeof(e->ext.bot_profile.target_body) - 1);
					e->ext.bot_profile.target_body[
						sizeof(e->ext.bot_profile.target_body) - 1] = '\0';
				}
				s_buildArchiveMemberPath("botprofiles", idbuf,
					".pdbotprofile", "profile.json",
					e->ext.bot_profile.profile_file,
					sizeof(e->ext.bot_profile.profile_file));
				catalogSetPrimaryFile(e, e->ext.bot_profile.profile_file);
			}
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "CATALOG.SWEEP: registered %d base bot profiles", n);
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
			s_buildArchiveMemberPath("hud", idbuf, ".pdhud",
				"layout.json", e->ext.hud.layout_file,
				sizeof(e->ext.hud.layout_file));
			catalogSetPrimaryFile(e, e->ext.hud.layout_file);
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
	 * The manifest pipeline uses catalogIdByRuntime(ASSET_MODEL, modelnum)
	 * to get a readable "base:model_<file-symbol>" ID.
	 * source_filenum = g_ModelStates[i].fileid (FILE_* ROM constant)
	 *
	 * ASSET_MODEL is separate from ASSET_PROP (which tracks PROPTYPE_*
	 * categories) to avoid runtime_index collisions.
	 */
	{
		s32 n = 0;
		s32 i;
		char seen_ids[NUM_MODELS][CATALOG_ID_LEN];
		s32 seen_count = 0;
		memset(seen_ids, 0, sizeof(seen_ids));
		for (i = 0; i < NUM_MODELS; i++) {
			catalogReadableModelIdForModelnum(i, (s32)g_ModelStates[i].fileid,
				idbuf, sizeof(idbuf));
			s_makeReadableCatalogIdUnique(seen_ids, seen_count, i,
				idbuf, sizeof(idbuf));
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_MODEL);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register model %s", idbuf);
				continue;
			}
			s_noteCatalogId(seen_ids, &seen_count, idbuf);
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = i;
			e->source_filenum = (s32)g_ModelStates[i].fileid;
			/* Phase 3 Pass B Slice 7: disk-or-ROM bind for prop models. */
			if (e->source_filenum > 0) {
				catalogBindPrimaryFromDiskOrRom(e, e->source_filenum);
			}
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base prop models (ASSET_MODEL)", n);
		count += n;
	}

	/* ---- vehicles ---- */
	{
		s32 n = 0;
		for (s32 i = 0; i < (s32)NUM_BASE_VEHICLES; i++) {
			snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseVehicles[i].slug);
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_VEHICLE);
			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register vehicle %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = s_BaseVehicles[i].modelnum;
			const char *model_id = catalogModelIdByModelnum(s_BaseVehicles[i].modelnum);
			if (model_id && model_id[0]) {
				char model_slug[CATALOG_ID_LEN];
				s_catalogIdToFilenameSlug(model_id, model_slug, sizeof(model_slug));
				snprintf(e->ext.vehicle.model_file,
					sizeof(e->ext.vehicle.model_file),
					"dependencies/assets/models/%s.pdmesh", model_slug);
			}
			s_buildArchiveMemberPath("vehicles", idbuf, ".pdvehicle",
				"physics.json", e->ext.vehicle.physics_file,
				sizeof(e->ext.vehicle.physics_file));
			s_buildArchiveMemberPath("vehicles", idbuf, ".pdvehicle",
				"behavior.graph.json", e->ext.vehicle.behavior_graph,
				sizeof(e->ext.vehicle.behavior_graph));
			const char *primary_file = e->ext.vehicle.model_file[0] ?
				e->ext.vehicle.model_file :
				e->ext.vehicle.behavior_graph;
			if (primary_file[0]) {
				catalogSetPrimaryFile(e, primary_file);
			}
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base vehicles", n);
		count += n;
	}

	/* ---- first-person hand models (B-275/S483 runtime coverage) ---- */
	/*
	 * bgun queues hand models by raw FILE_* filenum from the active body.
	 * Hand filenums are now sourced from bodydata_authored.c (was direct
	 * g_HeadsAndBodies[] read pre-pivot). Register distinct hand files
	 * as ASSET_MODEL provider-backed catalog entries with negative
	 * runtime_index so they don't collide with g_ModelStates[] MODEL_*.
	 */
	{
		u16 seen[152];
		s32 seen_count = 0;
		s32 n = 0;

		for (s32 i = 0; i < g_BodyDataCount; i++) {
			s32 handfilenum = (s32)g_BodyData[i].handfilenum;
			s32 duplicate = 0;

			if (g_BodyData[i].filenum == 0 || handfilenum <= 0) {
				continue;
			}

			for (s32 j = 0; j < seen_count; j++) {
				if (seen[j] == (u16)handfilenum) {
					duplicate = 1;
					break;
				}
			}
			if (duplicate) {
				continue;
			}
			seen[seen_count++] = (u16)handfilenum;

			catalogReadableModelIdForFile(handfilenum, "hand", "hand",
				idbuf, sizeof(idbuf));
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_MODEL);
			if (!e) {
				sysLogPrintf(LOG_ERROR,
					"assetcatalog: failed to register hand model %s", idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1; e->enabled = 1;
			e->runtime_index = -handfilenum;
			e->source_filenum = handfilenum;
			/* Phase 3 Pass B Slice 4 (hand model variant): disk-or-ROM bind. */
			catalogBindPrimaryFromDiskOrRom(e, e->source_filenum);
			e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
			n++;
		}

		sysLogPrintf(LOG_NOTE,
			"assetcatalog: registered %d base first-person hand models (ASSET_MODEL)", n);
		count += n;
	}

	/* ---- lang banks (Phase 3: manifest-based lang loading) ----
	 *
	 * Catalog coverage audit (2026-05-01) Section 3.E closure originally
	 * bound each ASSET_LANG entry to its ROM source filenum for override
	 * lookup. Under c3844 the filenum is migration/provenance only:
	 * steady-state language loading is routed by bank id through
	 * langManifestLoadBankFromCatalog() and the .pdlang FileProvider source.
	 *
	 * The bank_id-to-filenum map (g_LangFiles[bank] + JPN offset)
	 * lives in src/game/lang.c.  Calling langGetFileId at registration
	 * time pins the binding.  langGetFileNumOffset depends on g_Jpn
	 * which is decided before assetCatalogRegisterBaseGameExtended
	 * runs, so the offset is stable here. */
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
			/* Migration/provenance only: runtime language loading should
			 * prefer the locale-suffixed .pdlang FileProvider rows. */
			s32 fnum = langGetFileId(s_BaseLangBanks[i].bank_id);
			if (fnum > 0) {
				/* Phase 3 Pass B Slice 3: disk-or-ROM bind for lang banks. */
				catalogBindPrimaryFromDiskOrRom(e, fnum);
			}
			n++;
		}
		sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base lang banks (with source_filenum)", n);
		count += n;
	}

	sysLogPrintf(LOG_NOTE, "assetcatalog: T3/T4/T5 extended registration complete (%d entries)", count);
	return count;
}

/* ========================================================================
 * S484-followup (2026-05-01): weapon model file registration
 * ========================================================================
 *
 * Why this exists:
 *
 * The base game's weapon-fire path (bgunTickGunLoad in src/game/bondgun.c)
 * loads three classes of model files by filenum:
 *   1. HAND models   -- bgunQueueModelLoad(handfilenum) at line 4477.
 *                       handfilenum comes from catalogGetBodyHandFilenum,
 *                       sourced from g_HeadsAndBodies[bodynum].handfilenum.
 *                       Already covered by the hand-model registration loop
 *                       above (registerHandModels).
 *   2. GUN models    -- bgunQueueModelLoad(weaponGetFileNum(weaponnum)) at
 *                       line 4512. The filenum comes from
 *                       struct weapon::hi_model loaded from the loader_pool.
 *                       Covered by this function's hi_model + lo_model loop.
 *   3. CARTRIDGE     -- bgunQueueModelLoad(g_CartFileNums[casingindex]) at
 *      models          line 4562. The filenums come from
 *                       g_CartFileNums[] (FILE_GCARTRIDGE / FILE_GCARTRIFLE
 *                       / FILE_GCARTBLUE / FILE_GCARTSHELL).
 *                       Covered by this function's cartridge loop.
 *   4. MENU HUDPIECE -- menuInit() seeds g_MenuData.hudpiece.newparams with
 *                       FILE_GHUDPIECE, then menuRenderModel loads that raw
 *                       filenum through catalogHandleByModelSourceFilenum.
 *
 * Pre-S484 F4 the bgun load went through assetLoadRomToAddr(filenum, ...)
 * directly. The F4 refactor (commit ecc9d880) routed the load through
 * the catalog's source-filenum lookup (catalogHandleByModelSourceFilenum)
 * on the assumption that every model file involved would already be
 * catalog-registered. That assumption held only for hand models (the
 * existing hand-model loop) and chr-side prop models (g_ModelStates[]).
 *
 * Mike's playtest log (S594h-Unit-C-followup) showed
 *   ERROR: CATALOG_CRITICAL: bgun model filenum=907 failed to load
 * (FILE_GZ2020 = Farsight hi_model). Cartridge model loads would fail
 * the same way the moment any weapon with a casing tried to fire.
 *
 * Resolution per Mike's no-half-measures directive (2026-05-01):
 *   "The correct solution is not to fallback to legacy, but to
 *    strengthen our initial cataloging to be full, correct, and
 *    complete."
 *
 * This function walks the loader-populated weapon pool and the
 * g_CartFileNums[] cartridge table, and registers each unique filenum as
 * an ASSET_MODEL entry with source_filenum binding. Idempotent: skips
 * files already registered (overlap with g_ModelStates / hand-model /
 * prior weapon iterations).
 *
 * Why a separate function rather than baking into the body of
 * assetCatalogRegisterBaseGameExtended: the loader (loaderPool) is
 * scanned and its weapon pool populated AFTER the base-game catalog
 * registration runs. catalogManagerGetWeaponByIndex(i) returns NULL
 * during base-game registration. Caller (main.c) invokes this function
 * AFTER loaderPoolFinalize so weapon data is available.
 *
 * No legacy ROM fallback in bondgun.c. Catalog is the sole pipeline.
 * If any future weapon ships with a model file we miss here, the load
 * fails LOUD via CATALOG_CRITICAL (one ERROR per missing filenum thanks
 * to the bondgun.c throttle), not silently routed through ROM. Pressure
 * stays on the registration side to be complete.
 */
/* Client-only: depends on catalogManager* (catalog_mgr_weapons.c) and
 * g_CartFileNums (bondgun.c), neither of which is in the pd-server source
 * list. The function is called only from port/src/main.c after the loader
 * populates the weapon pool, which never happens on a dedicated server. */
#if !defined(PD_SERVER)
extern u16 g_CartFileNums[];

s32 assetCatalogRegisterWeaponModelFiles(void)
{
	char idbuf[CATALOG_ID_LEN];
	s32 registered = 0;
	s32 skipped_dup = 0;
	s32 skipped_zero = 0;

	/* ---- weapon hi_model / lo_model ---- */
	const s32 weapon_count = catalogManagerWeaponCount();
	for (s32 wi = 0; wi < weapon_count; wi++) {
		const struct weapon *w = catalogManagerGetWeaponByIndex(wi);
		if (!w) continue;

		const s32 filenums[2] = {
			(s32)w->hi_model,
			(s32)w->lo_model,
		};
		const char *suffixes[2] = { "hi", "lo" };

		for (s32 fi = 0; fi < 2; fi++) {
			const s32 fnum = filenums[fi];
			if (fnum <= 0) {
				skipped_zero++;
				continue;
			}

			/* Dedup: skip if any existing ASSET_MODEL entry already
			 * has this source_filenum (e.g. g_ModelStates path covers
			 * a CHR-side model that happens to share a filenum, or
			 * a prior weapon's hi_model already registered the same
			 * shared model file). */
			asset_data_handle_t existing =
				catalogHandleBySourceFilenum(ASSET_MODEL, fnum);
			if (!assetHandleIsNull(existing)) {
				skipped_dup++;
				continue;
			}

			catalogReadableModelIdForFile(fnum, suffixes[fi], "weapon",
				idbuf, sizeof(idbuf));
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_MODEL);
			if (!e) {
				sysLogPrintf(LOG_ERROR,
					"assetcatalog: failed to register weapon model %s",
					idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1;
			e->enabled = 1;
			/* Negative runtime_index keeps clear of g_ModelStates[]
			 * MODEL_* indices and the hand-model -handfilenum scheme.
			 * Subtract 100000 to leave the hand-model range (which uses
			 * -handfilenum, max ~16-bit) untouched. */
			e->runtime_index = -(100000 + fnum);
			e->source_filenum = fnum;
			/* Phase 3 Pass B Slice 1: disk-or-ROM bind for weapon /
			 * cart model files. */
			catalogBindPrimaryFromDiskOrRom(e, fnum);
			e->load_state = ASSET_STATE_LOADED;
			e->ref_count = ASSET_REF_BUNDLED;
			registered++;
		}
	}

	/* ---- cartridge / casing model files ----
	 * Loaded by bgunTickMasterLoad (bondgun.c:4562) when a weapon's
	 * ammo definition has a casing. Same load path as gun / hand models. */
	{
		static const s32 kCartCount = 4; /* matches array length in bondgun.c */
		const char *cart_slugs[4] = { "rifle", "rifle_alt", "blue", "shell" };
		for (s32 ci = 0; ci < kCartCount; ci++) {
			const s32 fnum = (s32)g_CartFileNums[ci];
			if (fnum <= 0) {
				skipped_zero++;
				continue;
			}

			asset_data_handle_t existing =
				catalogHandleBySourceFilenum(ASSET_MODEL, fnum);
			if (!assetHandleIsNull(existing)) {
				skipped_dup++;
				continue;
			}

			snprintf(idbuf, sizeof(idbuf),
				"base:model_cartridge_%s", cart_slugs[ci]);
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_MODEL);
			if (!e) {
				sysLogPrintf(LOG_ERROR,
					"assetcatalog: failed to register cartridge model %s",
					idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1;
			e->enabled = 1;
			e->runtime_index = -(100000 + fnum);
			e->source_filenum = fnum;
			/* Phase 3 Pass B Slice 1: disk-or-ROM bind for weapon /
			 * cart model files. */
			catalogBindPrimaryFromDiskOrRom(e, fnum);
			e->load_state = ASSET_STATE_LOADED;
			e->ref_count = ASSET_REF_BUNDLED;
			registered++;
		}
	}

	/* ---- menu HUD piece model ----
	 * The options/menu projection model is a raw filenum, not a g_ModelStates
	 * MODEL_* entry. Register it here so the menu path stays catalog/provider
	 * backed instead of warning and bypassing the typed asset graph. */
	{
		const s32 fnum = (s32)FILE_GHUDPIECE;
		asset_data_handle_t existing =
			catalogHandleBySourceFilenum(ASSET_MODEL, fnum);
		if (!assetHandleIsNull(existing)) {
			skipped_dup++;
		} else {
			catalogReadableModelIdForFile(fnum, "menu", "menu",
				idbuf, sizeof(idbuf));
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_MODEL);
			if (!e) {
				sysLogPrintf(LOG_ERROR,
					"assetcatalog: failed to register menu model %s",
					idbuf);
			} else {
				strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
				e->bundled = 1;
				e->enabled = 1;
				e->runtime_index = -(100000 + fnum);
				e->source_filenum = fnum;
				catalogBindPrimaryFromDiskOrRom(e, fnum);
				e->load_state = ASSET_STATE_LOADED;
				e->ref_count = ASSET_REF_BUNDLED;
				registered++;
			}
		}
	}

	sysLogPrintf(LOG_NOTE,
		"assetcatalog: registered %d weapon/menu-pipeline model files (ASSET_MODEL); "
		"skipped %d already-registered, %d zero filenums",
		registered, skipped_dup, skipped_zero);
	return registered;
}
#endif /* !PD_SERVER */

/* ========================================================================
 * Catalog coverage audit (2026-05-01) Section 3.A:
 * stage scene file registration
 * ========================================================================
 *
 * The stagetableentry struct carries five per-stage file IDs:
 *   bgfileid       -- BG geometry segment (read by bg.c::bgLoadFile)
 *   tilefileid     -- collision tile data (read by tilesreset.c)
 *   padsfileid     -- collision pad placement (read by setup.c)
 *   setupfileid    -- SP mission setup script (read by setup.c)
 *   mpsetupfileid  -- MP setup script (read by setup.c, MP-class stages)
 *
 * Each is a ROM filenum. Pre-this-pass none had a catalog entry, so
 * `catalogResolveFile(filenum)` returned no override and mods could
 * not redirect any of these per-stage scene assets.
 *
 * Closure: walk g_Stages[] and register each non-zero scene-file ID
 * as ASSET_MODEL with `source_filenum` binding plus
 * `catalogSetPrimaryRomFilenum`. Same pattern as
 * assetCatalogRegisterWeaponModelFiles (cartridge / hand / hi+lo
 * model files). The discriminator is the readable catalog ID slug
 * ("base:model_<stage>_<class>") so a future migration to dedicated
 * ASSET_BG / ASSET_TILES / ASSET_PADS / ASSET_SETUP types can rename
 * without breaking on-wire identity (these IDs are not referenced from
 * mods or saves today).
 *
 * Negative `runtime_index` keeps clear of g_ModelStates[] MODEL_*
 * indices and the hand / weapon / cart model schemes (which already
 * use -(100000 + fnum)). Stage scene files use -(200000 + fnum) to
 * separate the namespace.
 *
 * Idempotent: dedup against any existing ASSET_MODEL with same
 * source_filenum. Multiple stages may share files (e.g. mod stages
 * pointing at base BG); the first registration wins, subsequent
 * stages see the existing entry and skip.
 *
 * Server build: g_NumStages == 0 server-side per stageTableInit guard,
 * so the function returns early with zero registrations.
 *
 * No legacy ROM fallback in consumers. The existing romdataFileLoad
 * plumbing already consults catalogResolveFile, so this registration
 * alone is sufficient to enable mod overrides for stage scene files.
 * Strict-handle migration of the five consumer call sites is a
 * separate hardening pass tracked in the audit doc.
 */
s32 assetCatalogRegisterStageSceneFiles(void)
{
	char idbuf[CATALOG_ID_LEN];
	s32 registered = 0;
	s32 skipped_dup = 0;
	s32 skipped_zero = 0;

	if (g_NumStages <= 0) {
		/* Server build (no ROM data) or stage table not yet built. */
		return 0;
	}

	struct {
		const char *slug;
		size_t      offset; /* offsetof in stagetableentry, declared inline below */
	} kFileSlots[5] = {
		{ "bg",      offsetof(struct stagetableentry, bgfileid)      },
		{ "tile",    offsetof(struct stagetableentry, tilefileid)    },
		{ "pads",    offsetof(struct stagetableentry, padsfileid)    },
		{ "setup",   offsetof(struct stagetableentry, setupfileid)   },
		{ "mpsetup", offsetof(struct stagetableentry, mpsetupfileid) },
	};

	for (s32 si = 0; si < g_NumStages; si++) {
		const struct stagetableentry *st = &g_Stages[si];
		for (s32 fi = 0; fi < 5; fi++) {
			const u16 *p = (const u16 *)((const u8 *)st + kFileSlots[fi].offset);
			const s32 fnum = (s32)*p;

			if (fnum <= 0) {
				skipped_zero++;
				continue;
			}

			asset_data_handle_t existing =
				catalogHandleBySourceFilenum(ASSET_MODEL, fnum);
			if (!assetHandleIsNull(existing)) {
				skipped_dup++;
				continue;
			}

			catalogReadableStageSceneId(catalogStageIdByStageTableIndex(si),
				kFileSlots[fi].slug, fnum, idbuf, sizeof(idbuf));
			asset_entry_t *e = assetCatalogRegister(idbuf, ASSET_MODEL);
			if (!e) {
				sysLogPrintf(LOG_ERROR,
					"assetcatalog: failed to register stage scene file %s",
					idbuf);
				continue;
			}
			strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1;
			e->enabled = 1;
			e->runtime_index = -(200000 + fnum);
			/* Phase 3 Pass B Slice 9: disk-or-ROM bind for stage
			 * scene files (bg / tile / pads / setup / mpsetup). */
			catalogBindPrimaryFromDiskOrRom(e, fnum);
			e->load_state = ASSET_STATE_LOADED;
			e->ref_count = ASSET_REF_BUNDLED;
			registered++;
		}
	}

	sysLogPrintf(LOG_NOTE,
		"assetcatalog: registered %d stage scene files across %d stages "
		"(ASSET_MODEL); skipped %d already-registered, %d zero filenums",
		registered, g_NumStages, skipped_dup, skipped_zero);
	return registered;
}
