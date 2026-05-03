/*
 * port/src/arenadata_authored.c -- Catalog Universality BYOR Completion
 * (2026-05-03).
 *
 * AUTHORED EXTRACTOR SOURCE-OF-TRUTH for ARENA metadata.
 *
 * 47 arena entries. Reverse-engineered from the historical g_MpArenas[]
 * table in src/game/mplayer/setup.c plus the s_ArenaNames[47] slug
 * table and s_ArenaGroupMap[5] category map in
 * port/src/assetcatalog_base.c. Catalog ID slugs match the prior
 * base/arenas.pdbase JSON archive so historical references survive.
 *
 * load_mode: ARENA_LOADMODE_CANVAS for the "Solo Missions" group
 * (audit Section H.1 invariant); ARENA_LOADMODE_PLAYABLE otherwise.
 *
 * Engine-API constraint: nothing in src/ or port/ outside the catalog
 * registration code (assetcatalog_base.c) and the runtime emitter
 * (port/src/romextract_pdarena.c) may include arenadata_authored.h.
 * Engine reads route through the catalog (catalog_mgr_arenas).
 *
 * @see context/audits/catalog-universality-pivot-plan-2026-05-02.md
 */

#include <PR/ultratypes.h>
#include "data.h"
#include "types.h"
#include "constants.h"
#include "lang.h"
#include "assetcatalog.h" /* ARENA_LOADMODE_* */
#include "arenadata_authored.h"

const arena_authored_record_t g_ArenaData[] = {
	/* Dark MP arenas (0-12) */
	{ "base:arena_mp_skedar",     "mp_skedar",     "Dark",          STAGE_MP_SKEDAR,     0, L_MPMENU_119, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_pipes",      "mp_pipes",      "Dark",          STAGE_MP_PIPES,      0, L_MPMENU_120, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_ravine",     "mp_ravine",     "Dark",          STAGE_MP_RAVINE,     0, L_MPMENU_121, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_g5building", "mp_g5building", "Dark",          STAGE_MP_G5BUILDING, 0, L_MPMENU_122, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_sewers",     "mp_sewers",     "Dark",          STAGE_MP_SEWERS,     0, L_MPMENU_123, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_warehouse",  "mp_warehouse",  "Dark",          STAGE_MP_WAREHOUSE,  0, L_MPMENU_124, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_grid",       "mp_grid",       "Dark",          STAGE_MP_GRID,       0, L_MPMENU_125, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_ruins",      "mp_ruins",      "Dark",          STAGE_MP_RUINS,      0, L_MPMENU_126, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_area52",     "mp_area52",     "Dark",          STAGE_MP_AREA52,     0, L_MPMENU_127, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_base",       "mp_base",       "Dark",          STAGE_MP_BASE,       0, L_MPMENU_128, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_fortress",   "mp_fortress",   "Dark",          STAGE_MP_FORTRESS,   0, L_MPMENU_130, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_villa",      "mp_villa",      "Dark",          STAGE_MP_VILLA,      0, L_MPMENU_131, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_carpark",    "mp_carpark",    "Dark",          STAGE_MP_CARPARK,    0, L_MPMENU_132, ARENA_LOADMODE_PLAYABLE },
	/* Solo Mission arenas (13-26) -- ARENA_LOADMODE_CANVAS group */
	{ "base:arena_defection",     "defection",     "Solo Missions", STAGE_DEFECTION,     0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_134 : L_OPTIONS_133), ARENA_LOADMODE_CANVAS },
	{ "base:arena_investigation", "investigation", "Solo Missions", STAGE_INVESTIGATION, 0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_136 : L_OPTIONS_135), ARENA_LOADMODE_CANVAS },
	{ "base:arena_villa",         "villa",         "Solo Missions", STAGE_VILLA,         0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_140 : L_OPTIONS_139), ARENA_LOADMODE_CANVAS },
	{ "base:arena_chicago",       "chicago",       "Solo Missions", STAGE_CHICAGO,       0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_142 : L_OPTIONS_141), ARENA_LOADMODE_CANVAS },
	{ "base:arena_g5building",    "g5building",    "Solo Missions", STAGE_G5BUILDING,    0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_144 : L_OPTIONS_143), ARENA_LOADMODE_CANVAS },
	{ "base:arena_infiltration",  "infiltration",  "Solo Missions", STAGE_INFILTRATION,  0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_146 : L_OPTIONS_145), ARENA_LOADMODE_CANVAS },
	{ "base:arena_airbase",       "airbase",       "Solo Missions", STAGE_AIRBASE,       0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_152 : L_OPTIONS_151), ARENA_LOADMODE_CANVAS },
	{ "base:arena_airforceone",   "airforceone",   "Solo Missions", STAGE_AIRFORCEONE,   0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_154 : L_OPTIONS_153), ARENA_LOADMODE_CANVAS },
	{ "base:arena_crashsite",     "crashsite",     "Solo Missions", STAGE_CRASHSITE,     0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_156 : L_OPTIONS_155), ARENA_LOADMODE_CANVAS },
	{ "base:arena_pelagic",       "pelagic",       "Solo Missions", STAGE_PELAGIC,       0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_158 : L_OPTIONS_157), ARENA_LOADMODE_CANVAS },
	{ "base:arena_deepsea",       "deepsea",       "Solo Missions", STAGE_DEEPSEA,       0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_160 : L_OPTIONS_159), ARENA_LOADMODE_CANVAS },
	{ "base:arena_defense",       "defense",       "Solo Missions", STAGE_DEFENSE,       0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_162 : L_OPTIONS_161), ARENA_LOADMODE_CANVAS },
	{ "base:arena_attackship",    "attackship",    "Solo Missions", STAGE_ATTACKSHIP,    0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_164 : L_OPTIONS_163), ARENA_LOADMODE_CANVAS },
	{ "base:arena_skedarruins",   "skedarruins",   "Solo Missions", STAGE_SKEDARRUINS,   0, (VERSION == VERSION_JPN_FINAL ? L_OPTIONS_166 : L_OPTIONS_165), ARENA_LOADMODE_CANVAS },
	/* Classic arenas (27-31) */
	{ "base:arena_mp_temple",     "mp_temple",     "Classic",       STAGE_MP_TEMPLE,     0, L_MPMENU_133, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_complex",    "mp_complex",    "Classic",       STAGE_MP_COMPLEX,    0, L_MPMENU_134, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_grid6",      "mp_grid6",      "Classic",       STAGE_TEST_MP6,      0, L_MPMENU_306, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_grid2",      "mp_grid2",      "Classic",       STAGE_TEST_MP2,      0, L_MPMENU_129, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_felicity",   "mp_felicity",   "Classic",       STAGE_MP_FELICITY,   0, L_MPMENU_135, ARENA_LOADMODE_PLAYABLE },
	/* Bonus arenas (32-44) */
	{ "base:arena_test_arch",     "test_arch",     "Bonus",         STAGE_TEST_ARCH,     0, L_MPMENU_324, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_test_dest",     "test_dest",     "Bonus",         STAGE_TEST_DEST,     0, L_MPMENU_325, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra16",       "extra16",       "Bonus",         STAGE_EXTRA16,       0, L_MPMENU_327, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra17",       "extra17",       "Bonus",         STAGE_EXTRA17,       0, L_MPMENU_328, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra18",       "extra18",       "Bonus",         STAGE_EXTRA18,       0, L_MPMENU_329, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra19",       "extra19",       "Bonus",         STAGE_EXTRA19,       0, L_MPMENU_330, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra20",       "extra20",       "Bonus",         STAGE_EXTRA20,       0, L_MPMENU_331, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra21",       "extra21",       "Bonus",         STAGE_EXTRA21,       0, L_MPMENU_332, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra22",       "extra22",       "Bonus",         STAGE_EXTRA22,       0, L_MPMENU_333, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra23",       "extra23",       "Bonus",         STAGE_EXTRA23,       0, L_MPMENU_334, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra24",       "extra24",       "Bonus",         STAGE_EXTRA24,       0, L_MPMENU_335, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_extra26",       "extra26",       "Bonus",         STAGE_EXTRA26,       0, L_MPMENU_337, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_test_lam",      "test_lam",      "Bonus",         STAGE_TEST_LAM,      0, L_MPMENU_338, ARENA_LOADMODE_PLAYABLE },
	/* Random (45-46) */
	{ "base:arena_mp_random_multi", "mp_random_multi", "Random",    STAGE_MP_RANDOM_MULTI, 0, L_MPMENU_294, ARENA_LOADMODE_PLAYABLE },
	{ "base:arena_mp_random_solo",  "mp_random_solo",  "Random",    STAGE_MP_RANDOM_SOLO,  0, L_MPMENU_295, ARENA_LOADMODE_PLAYABLE },
};

const s32 g_ArenaDataCount = (s32)(sizeof(g_ArenaData) / sizeof(g_ArenaData[0]));
