/**
 * assetcatalog_base.c -- D3R-3: Base game asset registration
 *
 * Registers all base game assets (stages, bodies, heads, arenas) in the
 * Asset Catalog with "base:" prefix IDs. This makes the entire game speak
 * the same name-based lookup language -- no special cases for base vs. mod.
 *
 * Each base asset gets:
 *   - id:            "base:{lowercase_name}"
 *   - type:          ASSET_MAP, ASSET_CHARACTER, etc.
 *   - category:      "base"
 *   - bundled:       1
 *   - enabled:       1
 *   - runtime_index: position in the original array (g_Stages[], etc.)
 *   - model_scale:   from the stage table's existing scale field
 *
 * Auto-discovered by CMake glob (port/*.c). No build system changes needed.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "types.h"
#include "constants.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "system.h"
#include "data.h"
#include "game/stagetable.h"

/* ========================================================================
 * Weapon Name Table
 * ======================================================================== */

/*
 * Maps MPWEAPON_* constant -> catalog name.
 * These become "base:{name}" entries (e.g., "base:weapon_falcon2").
 * weapon_id is the MPWEAPON_* constant value from constants.h.
 * TODO: full enumeration; remaining MPWEAPON_* constants can be added here.
 */
static const struct {
	s16 weapon_id;
	const char *name;
	const char *desc;
} s_BaseWeapons[] = {
	{ 0x01, "weapon_falcon2",        "Falcon 2" },
	{ 0x02, "weapon_falcon2_sil",    "Falcon 2 (Silencer)" },
	{ 0x03, "weapon_falcon2_scope",  "Falcon 2 (Scope)" },
	{ 0x04, "weapon_magsec4",        "MagSec 4" },
	{ 0x05, "weapon_mauler",         "Mauler" },
	{ 0x06, "weapon_phoenix",        "Phoenix" },
	{ 0x07, "weapon_dy357",          "DY357 Magnum" },
	{ 0x08, "weapon_dy357lx",        "DY357-LX" },
	{ 0x09, "weapon_cmp150",         "CMP150" },
	{ 0x0a, "weapon_cyclone",        "Cyclone" },
	{ 0x0b, "weapon_callisto",       "Callisto NTG" },
	{ 0x0c, "weapon_rcp120",         "RCP-120" },
	{ 0x0d, "weapon_laptopgun",      "Laptop Gun" },
	{ 0x0e, "weapon_dragon",         "Dragon" },
	{ 0x0f, "weapon_k7avenger",      "K7 Avenger" },
	{ 0x10, "weapon_ar34",           "AR34" },
	{ 0x11, "weapon_superdragon",    "SuperDragon" },
	{ 0x12, "weapon_shotgun",        "Shotgun" },
	{ 0x13, "weapon_reaper",         "Reaper" },
	{ 0x14, "weapon_sniperrifle",    "Sniper Rifle" },
	{ 0x15, "weapon_farsight",       "Farsight XR-20" },
	{ 0x16, "weapon_devastator",     "Devastator" },
	{ 0x17, "weapon_rocketlauncher", "Rocket Launcher" },
	{ 0x18, "weapon_slayer",         "Slayer" },
	{ 0x19, "weapon_combatknife",    "Combat Knife" },
	{ 0x1a, "weapon_crossbow",       "Crossbow" },
	{ 0x1b, "weapon_tranquilizer",   "Tranquilizer" },
	{ 0x1c, "weapon_grenade",        "Grenade" },
	{ 0x1d, "weapon_nbomb",          "N-Bomb" },
	{ 0x1e, "weapon_timedmine",      "Timed Mine" },
	{ 0x1f, "weapon_proximitymine",  "Proximity Mine" },
	{ 0x20, "weapon_remotemine",     "Remote Mine" },
	{ 0x21, "weapon_laser",          "Laser" },
	{ 0x22, "weapon_xrayscanner",    "X-Ray Scanner" },
	{ 0x25, "weapon_cloakingdevice", "Cloaking Device" },
	{ 0x26, "weapon_combatboost",    "Combat Boost" },
	{ 0x2f, "weapon_shield",         "Shield" },
};

#define NUM_BASE_WEAPONS (sizeof(s_BaseWeapons) / sizeof(s_BaseWeapons[0]))

/* ========================================================================
 * Game Mode Name Table
 * ======================================================================== */

/*
 * Maps MPSCENARIO_* constant -> catalog name.
 * These become "base:{name}" entries (e.g., "base:gamemode_combat").
 * scenario_id is the MPSCENARIO_* constant from constants.h.
 */
static const struct {
	s16 scenario_id;
	const char *name;
	const char *desc;
	u8 max_players;
	u8 min_players;
} s_BaseGameModes[] = {
	{ 0, "gamemode_combat",           "Combat Simulator", 4, 2 },
	{ 1, "gamemode_holdthebriefcase", "Hold the Briefcase", 4, 2 },
	{ 2, "gamemode_hackercentral",    "Hacker Central", 4, 2 },
	{ 3, "gamemode_popacap",          "Pop a Cap", 4, 2 },
	{ 4, "gamemode_kingofthehill",    "King of the Hill", 4, 2 },
	{ 5, "gamemode_capturethecase",   "Capture the Case", 4, 2 },
};

#define NUM_BASE_GAMEMODES (sizeof(s_BaseGameModes) / sizeof(s_BaseGameModes[0]))

/* ========================================================================
 * Stage Name Table
 * ======================================================================== */

/*
 * Maps stage table array index -> human-readable name for the catalog ID.
 * These become "base:{name}" entries (e.g., "base:maiansos", "base:villa").
 *
 * IMPORTANT: The stagenum stored in the catalog entry is g_Stages[i].id
 * (the logical stage ID from constants.h), NOT the array index. This matches
 * modconfig.txt convention and game code expectations.
 */
static const struct {
	s32 index;       /* array index in g_Stages[] */
	const char *name;
	const char *desc;
} s_BaseStages[] = {
	{  0, "maiansos",       "Maian SOS" },
	{  1, "test_silo",      "Silo" },
	{  2, "war",            "War" },
	{  3, "mp_ravine",      "Ravine" },
	{  4, "test_arch",      "Archives" },
	{  5, "escape",         "Escape" },
	{  6, "test_dest",      "Destroy" },
	{  7, "retaking",       "Retaking the Institute" },
	{  8, "crashsite",      "Crash Site" },
	{  9, "chicago",        "Chicago" },
	{ 10, "g5building",     "G5 Building" },
	{ 11, "mp_complex",     "Complex" },
	{ 12, "mp_g5building",  "G5 Building (MP)" },
	{ 13, "pelagic",        "Pelagic II" },
	{ 14, "extraction",     "Extraction" },
	{ 15, "test_run",       "Run" },
	{ 16, "stage_24",       "Stage 24" },
	{ 17, "mp_temple",      "Temple" },
	{ 18, "citraining",     "CI Training" },
	{ 19, "airbase",        "Air Base" },
	{ 20, "stage_28",       "Stage 28" },
	{ 21, "mp_pipes",       "Pipes" },
	{ 22, "skedarruins",    "Skedar Ruins" },
	{ 23, "stage_2b",       "Stage 2B" },
	{ 24, "villa",          "Villa" },
	{ 25, "defense",        "Defense" },
	{ 26, "test_ash",       "CI (Ash)" },
	{ 27, "infiltration",   "Infiltration" },
	{ 28, "defection",      "Defection" },
	{ 29, "airforceone",    "Air Force One" },
	{ 30, "mp_skedar",      "Skedar (MP)" },
	{ 31, "investigation",  "Investigation" },
	{ 32, "attackship",     "Attack Ship" },
	{ 33, "rescue",         "Rescue" },
	{ 34, "test_len",       "Len" },
	{ 35, "mbr",            "Mr. Blonde's Revenge" },
	{ 36, "deepsea",        "Deep Sea" },
	{ 37, "test_uff",       "Uff" },
	{ 38, "test_old",       "Old" },
	{ 39, "duel",           "Duel" },
	{ 40, "test_lam",       "Lam" },
	{ 41, "mp_base",        "Base" },
	{ 42, "test_mp2",       "Grid 2" },
	{ 43, "mp_area52",      "Area 52" },
	{ 44, "mp_warehouse",   "Warehouse" },
	{ 45, "mp_carpark",     "Car Park" },
	{ 46, "test_mp6",       "Grid 6" },
	{ 47, "test_mp7",       "Grid 7" },
	{ 48, "test_mp8",       "Grid 8" },
	{ 49, "mp_ruins",       "Ruins" },
	{ 50, "mp_sewers",      "Sewers" },
	{ 51, "mp_felicity",    "Felicity" },
	{ 52, "mp_fortress",    "Fortress" },
	{ 53, "mp_villa",       "Villa (MP)" },
	{ 54, "test_mp14",      "Grid 14" },
	{ 55, "mp_grid",        "Grid" },
	{ 56, "test_mp16",      "Grid 16" },
	{ 57, "test_mp17",      "Grid 17" },
	{ 58, "test_mp18",      "Grid 18" },
	{ 59, "test_mp19",      "Grid 19" },
	{ 60, "test_mp20",      "Grid 20" },
	/* Extended stages (mod slots) */
	{ 61, "extra1",         "Extra 1" },
	{ 62, "extra2",         "Extra 2" },
	{ 63, "extra3",         "Extra 3" },
	{ 64, "extra4",         "Extra 4" },
	{ 65, "extra5",         "Extra 5" },
	{ 66, "extra6",         "Extra 6" },
	{ 67, "extra7",         "Extra 7" },
	{ 68, "extra8",         "Extra 8" },
	{ 69, "extra9",         "Extra 9" },
	{ 70, "extra10",        "Extra 10" },
	{ 71, "extra11",        "Extra 11" },
	{ 72, "extra12",        "Extra 12" },
	{ 73, "extra13",        "Extra 13" },
	{ 74, "extra14",        "Extra 14" },
	{ 75, "extra15",        "Extra 15" },
	{ 76, "extra16",        "Extra 16" },
	{ 77, "extra17",        "Extra 17" },
	{ 78, "extra18",        "Extra 18" },
	{ 79, "extra19",        "Extra 19" },
	{ 80, "extra20",        "Extra 20" },
	{ 81, "extra21",        "Extra 21" },
	{ 82, "extra22",        "Extra 22" },
	{ 83, "extra23",        "Extra 23" },
	{ 84, "extra24",        "Extra 24" },
	{ 85, "extra25",        "Extra 25" },
	{ 86, "extra26",        "Extra 26" },
};

#define NUM_BASE_STAGES (sizeof(s_BaseStages) / sizeof(s_BaseStages[0]))

/* ========================================================================
 * Body Name Table
 * ======================================================================== */

/*
 * Maps g_MpBodies[] array index -> catalog name.
 * Names derived from BODY_* constants, lowercased.
 */
static const struct {
	s32 index;
	const char *name;
	const char *desc;
} s_BaseBodies[] = {
	{  0, "dark_combat",      "Joanna Dark (Combat)" },
	{  1, "dark_trench",      "Joanna Dark (Trench)" },
	{  2, "dark_frock",       "Joanna Dark (Frock)" },
	{  3, "dark_ripped",      "Joanna Dark (Ripped)" },
	{  4, "dark_af1",         "Joanna Dark (AF1)" },
	{  5, "dark_leather",     "Joanna Dark (Leather)" },
	{  6, "dark_negotiator",  "Joanna Dark (Negotiator)" },
	{  7, "darkwet",          "Joanna Dark (Wet)" },
	{  8, "darkaqualung",     "Joanna Dark (Aqualung)" },
	{  9, "darksnow",         "Joanna Dark (Snow)" },
	{ 10, "darklab",          "Joanna Dark (Lab)" },
	{ 11, "theking",          "Elvis (The King)" },
	{ 12, "elvis1",           "Elvis" },
	{ 13, "elviswaistcoat",   "Elvis (Waistcoat)" },
	{ 14, "carrington",       "Carrington" },
	{ 15, "carreveningsuit",  "Carrington (Evening Suit)" },
	{ 16, "mrblonde",         "Mr. Blonde" },
	{ 17, "cassandra",        "Cassandra De Vries" },
	{ 18, "trent",            "Trent Easton" },
	{ 19, "jonathan",         "Jonathan" },
	{ 20, "cilabtech",        "CI Lab Tech" },
	{ 21, "cifemtech",        "CI Female Tech" },
	{ 22, "cisoldier",        "CI Soldier" },
	{ 23, "ddshock",          "dataDyne Shock Trooper" },
	{ 24, "fem_guard",        "Female Guard" },
	{ 25, "dd_secguard",      "DD Security Guard" },
	{ 26, "dd_guard",         "dataDyne Guard" },
	{ 27, "dd_shock_inf",     "DD Shock (Infiltration)" },
	{ 28, "secretary",        "Secretary" },
	{ 29, "officeworker",     "Office Worker" },
	{ 30, "officeworker2",    "Office Worker 2" },
	{ 31, "negotiator",       "Negotiator" },
	{ 32, "ddsniper",         "DD Sniper" },
	{ 33, "g5_guard",         "G5 Guard" },
	{ 34, "g5_swat_guard",    "G5 SWAT Guard" },
	{ 35, "ciaguy",           "CIA Agent" },
	{ 36, "fbiguy",           "FBI Agent" },
	{ 37, "area51guard",      "Area 51 Guard" },
	{ 38, "a51trooper",       "Area 51 Trooper" },
	{ 39, "a51airman",        "Area 51 Airman" },
	{ 40, "overall",          "Overall" },
	{ 41, "stripes",          "Stripes" },
	{ 42, "labtech",          "Lab Tech" },
	{ 43, "femlabtech",       "Female Lab Tech" },
	{ 44, "dd_labtech",       "DD Lab Tech" },
	{ 45, "biotech",          "Bio Tech" },
	{ 46, "alaskan_guard",    "Alaskan Guard" },
	{ 47, "pilotaf1",         "AF1 Pilot" },
	{ 48, "steward",          "Steward" },
	{ 49, "stewardess",       "Stewardess" },
	{ 50, "stewardess_coat",  "Stewardess (Coat)" },
	{ 51, "president",        "President" },
	{ 52, "nsa_lackey",       "NSA Lackey" },
	{ 53, "pres_security",    "Presidential Security" },
	{ 54, "president_clone2", "President Clone" },
	{ 55, "pelagic_guard",    "Pelagic Guard" },
	{ 56, "maian_soldier",    "Maian Soldier" },
	{ 57, "connery",          "Bond (Classic)" },
	{ 58, "moore",            "Bond (Classic)" },
	{ 59, "dalton",           "Bond (Classic)" },
	{ 60, "djbond",           "Bond (Classic)" },
	{ 61, "skedar",           "Skedar" },
	{ 62, "drcaroll",         "Dr. Carroll" },
};

#define NUM_BASE_BODIES (sizeof(s_BaseBodies) / sizeof(s_BaseBodies[0]))

/* ========================================================================
 * Head Name Table
 * ======================================================================== */

/*
 * Maps g_MpHeads[] array index -> catalog name.
 * Names derived from MPHEAD_* constants, lowercased.
 */
static const struct {
	s32 index;
	const char *name;
	const char *desc;
} s_BaseHeads[] = {
	{  0, "head_dark_combat",  "Joanna Dark (Combat)" },
	{  1, "head_dark_frock",   "Joanna Dark (Frock)" },
	{  2, "head_darkaqua",     "Joanna Dark (Aqua)" },
	{  3, "head_dark_snow",    "Joanna Dark (Snow)" },
	{  4, "head_elvis",        "Elvis" },
	{  5, "head_elvis_gogs",   "Elvis (Goggles)" },
	{  6, "head_carrington",   "Carrington" },
	{  7, "head_mrblonde",     "Mr. Blonde" },
	{  8, "head_cassandra",    "Cassandra De Vries" },
	{  9, "head_trent",        "Trent Easton" },
	{ 10, "head_jonathan",     "Jonathan" },
	{ 11, "head_vd",           "VD" },
	{ 12, "head_president",    "President" },
	{ 13, "head_ddshock",      "DD Shock Trooper" },
	{ 14, "head_biotech",      "Bio Tech" },
	{ 15, "head_ddsniper",     "DD Sniper" },
	{ 16, "head_a51faceplate", "Area 51 Faceplate" },
	{ 17, "head_secretary",    "Secretary" },
	{ 18, "head_fem_guard",    "Female Guard" },
	{ 19, "head_fem_guard2",   "Female Guard 2" },
	{ 20, "head_maian_s",      "Maian Soldier" },
	{ 21, "head_jon",          "Jon" },
	{ 22, "head_beau1",        "Beau" },
	{ 23, "head_ross",         "Ross" },
	{ 24, "head_mark2",        "Mark" },
	{ 25, "head_christ",       "Christ" },
	{ 26, "head_russ",         "Russ" },
	{ 27, "head_darling",      "Darling" },
	{ 28, "head_brian",        "Brian" },
	{ 29, "head_jamie",        "Jamie" },
	{ 30, "head_duncan2",      "Duncan" },
	{ 31, "head_keith",        "Keith" },
	{ 32, "head_stevem",       "Steve M" },
	{ 33, "head_grant",        "Grant" },
	{ 34, "head_penny",        "Penny" },
	{ 35, "head_davec",        "Dave C" },
	{ 36, "head_jones",        "Jones" },
	{ 37, "head_graham",       "Graham" },
	{ 38, "head_robert",       "Robert" },
	{ 39, "head_neil2",        "Neil" },
	{ 40, "head_shaun",        "Shaun" },
	{ 41, "head_robin",        "Robin" },
	{ 42, "head_cook",         "Cook" },
	{ 43, "head_pryce",        "Pryce" },
	{ 44, "head_silke",        "Silke" },
	{ 45, "head_smith",        "Smith" },
	{ 46, "head_gareth",       "Gareth" },
	{ 47, "head_murchie",      "Murchie" },
	{ 48, "head_wong",         "Wong" },
	{ 49, "head_carter",       "Carter" },
	{ 50, "head_tintin",       "Tintin" },
	{ 51, "head_munton",       "Munton" },
	{ 52, "head_stamper",      "Stamper" },
	{ 53, "head_phelps",       "Phelps" },
	{ 54, "head_alex",         "Alex" },
	{ 55, "head_julianne",     "Julianne" },
	{ 56, "head_laura",        "Laura" },
	{ 57, "head_edmcg",        "Ed McG" },
	{ 58, "head_anka",         "Anka" },
	{ 59, "head_leslie_s",     "Leslie S" },
	{ 60, "head_matt_c",       "Matt C" },
	{ 61, "head_peer_s",       "Peer S" },
	{ 62, "head_eileen_t",     "Eileen T" },
	{ 63, "head_andy_r",       "Andy R" },
	{ 64, "head_ben_r",        "Ben R" },
	{ 65, "head_steve_k",      "Steve K" },
	{ 66, "head_sanchez",      "Sanchez" },
	{ 67, "head_tim",          "Tim" },
	{ 68, "head_ken",          "Ken" },
	{ 69, "head_eileen_h",     "Eileen H" },
	{ 70, "head_scott_h",      "Scott H" },
	{ 71, "head_joel",         "Joel" },
	{ 72, "head_griffey",      "Griffey" },
	{ 73, "head_moto",         "Moto" },
	{ 74, "head_winner",       "Winner" },
};

#define NUM_BASE_HEADS (sizeof(s_BaseHeads) / sizeof(s_BaseHeads[0]))

/* ========================================================================
 * Implementation
 * ======================================================================== */

extern struct stagetableentry g_Stages[];
extern struct mpbody g_MpBodies[];
extern struct mphead g_MpHeads[];
extern struct mparena g_MpArenas[];

s32 assetCatalogRegisterBaseGame(void)
{
	s32 count = 0;
	char idbuf[CATALOG_ID_LEN];

	/* ---- Register stages ---- */
	for (s32 i = 0; i < (s32)NUM_BASE_STAGES; i++) {
		const s32 idx = s_BaseStages[i].index;
		if (idx < 0 || idx >= 87) {
			continue;
		}

		snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseStages[i].name);

		asset_entry_t *e = assetCatalogRegisterMap(
			idbuf,
			g_Stages[idx].id,  /* logical stage ID, not array index */
			""                 /* base game stages have no component directory */
		);

		if (!e) {
			sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register base stage %s", idbuf);
			continue;
		}

		strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
		e->bundled = 1;
		e->enabled = 1;
		e->runtime_index = idx;
		count++;
	}

	sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base stages", count);

	/* ---- Register bodies ---- */
	/*
	 * Bodies are registered as ASSET_BODY with ext.body fields populated
	 * directly from g_MpBodies[]. This makes the catalog the authoritative
	 * source for body data — modmgrGetBody() reads from the catalog cache
	 * instead of the static arrays.
	 */
	s32 body_count = 0;
	for (s32 i = 0; i < (s32)NUM_BASE_BODIES; i++) {
		const s32 idx = s_BaseBodies[i].index;
		if (idx < 0 || idx >= 63) {
			continue;
		}

		snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseBodies[i].name);

		asset_entry_t *e = assetCatalogRegisterBody(
			idbuf,
			g_MpBodies[idx].bodynum,
			g_MpBodies[idx].name,
			g_MpBodies[idx].headnum,
			g_MpBodies[idx].requirefeature
		);
		if (!e) {
			sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register base body %s", idbuf);
			continue;
		}

		strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
		e->bundled = 1;
		e->enabled = 1;
		e->runtime_index = idx;
		e->model_scale = 1.0f;
		body_count++;
	}

	sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base bodies", body_count);
	count += body_count;

	/* ---- Register heads ---- */
	/*
	 * Heads are registered as ASSET_HEAD with ext.head fields populated
	 * from g_MpHeads[]. Same catalog-as-truth pattern as bodies and arenas.
	 */
	s32 head_count = 0;
	for (s32 i = 0; i < (s32)NUM_BASE_HEADS; i++) {
		const s32 idx = s_BaseHeads[i].index;
		if (idx < 0 || idx >= 76) {
			continue;
		}

		snprintf(idbuf, sizeof(idbuf), "base:%s", s_BaseHeads[i].name);

		asset_entry_t *e = assetCatalogRegisterHead(
			idbuf,
			g_MpHeads[idx].headnum,
			g_MpHeads[idx].requirefeature
		);
		if (!e) {
			sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register base head %s", idbuf);
			continue;
		}

		strncpy(e->category, "base", CATALOG_CATEGORY_LEN - 1);
		e->bundled = 1;
		e->enabled = 1;
		e->runtime_index = idx;
		e->model_scale = 1.0f;
		head_count++;
	}

	sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base heads", head_count);
	count += head_count;

	/* ---- Register arenas ---- */
	/*
	 * Arenas are stage references for the MP arena selection menu.
	 * Group mapping reads stagenum, requirefeature, and name directly
	 * from g_MpArenas[] (preserves VERSION-conditional lang IDs).
	 * Category field stores the arena group name for dropdown grouping.
	 */
	static const struct {
		s32 first;           /* first index in g_MpArenas[] */
		s32 count;           /* number of arenas in this group */
		const char *category;
	} s_ArenaGroupMap[] = {
		{  0, 13, "Dark" },
		{ 13, 14, "Solo Missions" },
		{ 27,  5, "Classic" },
		{ 32, 11, "GoldenEye X" },
		{ 43, 12, "GoldenEye X Bonus" },
		{ 55, 16, "Bonus" },
		{ 71,  4, "Random" },
	};
	#define NUM_ARENA_GROUPS (sizeof(s_ArenaGroupMap) / sizeof(s_ArenaGroupMap[0]))

	s32 arena_count = 0;
	for (s32 g = 0; g < (s32)NUM_ARENA_GROUPS; g++) {
		for (s32 j = 0; j < s_ArenaGroupMap[g].count; j++) {
			s32 idx = s_ArenaGroupMap[g].first + j;
			if (idx < 0 || idx >= 75) {
				continue;
			}

			snprintf(idbuf, sizeof(idbuf), "base:arena_%d", idx);

			asset_entry_t *e = assetCatalogRegisterArena(
				idbuf,
				g_MpArenas[idx].stagenum,
				g_MpArenas[idx].requirefeature,
				(s32)g_MpArenas[idx].name
			);

			if (!e) {
				sysLogPrintf(LOG_ERROR, "assetcatalog: failed to register base arena %s", idbuf);
				continue;
			}

			strncpy(e->category, s_ArenaGroupMap[g].category, CATALOG_CATEGORY_LEN - 1);
			e->bundled = 1;
			e->enabled = 1;
			e->runtime_index = idx;
			sysLogPrintf(LOG_NOTE, "assetcatalog: arena[%d] id=\"%s\" stagenum=0x%02x langid=0x%04x cat=\"%s\"",
				idx, idbuf, g_MpArenas[idx].stagenum, (s32)g_MpArenas[idx].name,
				s_ArenaGroupMap[g].category);
			arena_count++;
		}
	}

	sysLogPrintf(LOG_NOTE, "assetcatalog: registered %d base arenas", arena_count);
	count += arena_count;

	#undef NUM_ARENA_GROUPS

	/* Weapons and game modes are registered by assetCatalogRegisterBaseGameExtended()
	 * below with full rich ext fields. Do not register them here to avoid
	 * duplicate catalog entries. */

	sysLogPrintf(LOG_NOTE, "assetcatalog: base game registration complete (%d total entries)", count);

	/* Register extended types: weapons, animations, textures, props, game modes, audio, HUD */
	s32 ext_count = assetCatalogRegisterBaseGameExtended();
	if (ext_count > 0) {
		count += ext_count;
		sysLogPrintf(LOG_NOTE, "assetcatalog: extended registration added %d entries (%d total)", ext_count, count);
	}

	return count;
}
