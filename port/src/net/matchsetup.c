/**
 * matchsetup.c -- Clean match start function for the new lobby system.
 *
 * Bypasses the old menutick.c dialog-stack flow entirely.
 * Configures g_MpSetup, the participant pool, player configs, and bot
 * configs directly from lobby state, then calls mpStartMatch() and
 * triggers stage load.
 *
 * Also defines g_MatchSetupMenuDialog (stub dialog — legacy; ImGui room screen replaced it).
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "system.h"
#include "crashbreadcrumb.h"
#include "net/net.h"
#include "net/netlobby.h"
#include "options_forced.h" /* INV-4: engine-forced bit restore at matchStart */
#include "game/menu.h"
#include "game/mplayer/mplayer.h"
#include "game/challenge.h"
#include "romdata.h"
#include "modelcatalog.h"
#include "assetcatalog.h"
#include "asset_runtime.h"
#include "player_identity.h"
#include "game/mplayer/participant.h"
#include "net/matchsetup.h"
#include "input.h"
#include "inputctx.h"
#include "menupool.h"
#include "scene_transition.h"
#include "fs.h"
#include "lib/rng.h"
#include "net/netmanifest.h"
#include <stdlib.h>
#include <stdio.h>

/* ========================================================================
 * Dialog definition for hotswap
 * ======================================================================== */

/* Minimal menu item list — the actual rendering is done by ImGui via hotswap */
static struct menuitem g_MatchSetupMenuItems[] = {
	{ MENUITEMTYPE_END },
};

struct menudialogdef g_MatchSetupMenuDialog = {
	MENUDIALOGTYPE_DEFAULT,
	(uintptr_t)"Match Setup",
	g_MatchSetupMenuItems,
	NULL,
	MENUDIALOGFLAG_LITERAL_TEXT | MENUDIALOGFLAG_STARTSELECTS,
	NULL,
};

/* ========================================================================
 * Match slot configuration — types in net/matchsetup.h
 * ======================================================================== */

/* Definition of the match config global (declaration in net/matchsetup.h) */
struct matchconfig g_MatchConfig;

u32 matchConfigGetUserOptions(void)
{
	return matchOptionsUserView(g_MatchConfig.options,
	                            g_MatchConfig.options_engine_forced);
}

void matchConfigSetUserOption(u32 bit, s32 enabled)
{
	matchOptionsSetUserBit(&g_MatchConfig.options,
	                       &g_MatchConfig.options_engine_forced,
	                       bit, enabled);
}

void matchConfigReplaceUserOptions(u32 options, const char *reason)
{
	const u32 prior_forced = g_MatchConfig.options_engine_forced;

	matchOptionsReplaceUserOriginal(&g_MatchConfig.options,
	                                &g_MatchConfig.options_engine_forced,
	                                options);

	if (prior_forced != 0) {
		sysLogPrintf(LOG_NOTE,
			"MATCH.OPTIONS.REBASE: reason=%s discarded_overlay=0x%08x user=0x%08x",
			reason ? reason : "unspecified", prior_forced, options);
	}
}

void matchConfigRestoreUserOptions(const char *reason)
{
	const u32 prior_forced = g_MatchConfig.options_engine_forced;

	if (prior_forced == 0) {
		return;
	}

	matchOptionsRestoreUserOriginal(&g_MatchConfig.options,
	                                &g_MatchConfig.options_engine_forced);
	sysLogPrintf(LOG_NOTE,
		"MATCH.OPTIONS.RESTORE: reason=%s cleared_overlay=0x%08x user=0x%08x",
		reason ? reason : "unspecified", prior_forced, g_MatchConfig.options);
}

/* ========================================================================
 * Match config initialization
 * ======================================================================== */

static s32 matchConfigPrepareExactWeaponIds(
		const u8 weapons[NUM_MPWEAPONSLOTS],
		char out_ids[NUM_MPWEAPONSLOTS][CATALOG_ID_LEN])
{
	for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		const char *id;
		const asset_entry_t *entry;

		out_ids[i][0] = '\0';
		if (weapons[i] == MPWEAPON_NONE) {
			continue;
		}
		id = catalogWeaponIdByMpWeaponId((s32)weapons[i]);
		entry = id ? assetCatalogResolve(id) : NULL;
		if (!entry || !entry->occupied || !entry->enabled
				|| entry->type != ASSET_WEAPON
				|| entry->mp_index != (s32)weapons[i]
				|| entry->runtime_index <= 0
				|| catalogGetMpWeaponNum(entry->mp_index)
					!= entry->runtime_index) {
			return 0;
		}
		strncpy(out_ids[i], entry->id, CATALOG_ID_LEN - 1);
		out_ids[i][CATALOG_ID_LEN - 1] = '\0';
	}
	return 1;
}

s32 matchConfigSelectWeaponSet(s32 weaponsetnum)
{
	u8 prepared[NUM_MPWEAPONSLOTS];
	char ids[NUM_MPWEAPONSLOTS][CATALOG_ID_LEN];
	s32 resolved_set;
	u64 rng_seed = g_RngSeed;
	u64 rng_seed_2 = g_Rng2Seed;

	if (mpPrepareWeaponSet(weaponsetnum, g_MpSetup.weapons,
			prepared, &resolved_set) != 0
			|| !matchConfigPrepareExactWeaponIds(prepared, ids)) {
		g_RngSeed = rng_seed;
		g_Rng2Seed = rng_seed_2;
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK weapon-set index=%d globals_published=0",
			weaponsetnum);
		return 0;
	}

	mpCommitPreparedWeaponSet(resolved_set, prepared);
	g_MatchConfig.weaponSetIndex = (s8)weaponsetnum;
	memcpy(g_MatchConfig.weapon_ids, ids, sizeof(ids));
	memcpy(g_MatchConfig.weapons, prepared, sizeof(prepared));
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT weapon-set index=%d resolved=%d",
		weaponsetnum, resolved_set);
	return 1;
}

s32 matchConfigSetWeaponSlotId(s32 slot, const char *weapon_id)
{
	const asset_entry_t *entry;

	if (slot < 0 || slot >= NUM_MPWEAPONSLOTS || !weapon_id
			|| memchr(weapon_id, '\0', CATALOG_ID_LEN) == NULL) {
		return 0;
	}
	if (!weapon_id[0]) {
		g_MatchConfig.weapon_ids[slot][0] = '\0';
		g_MatchConfig.weapons[slot] = MPWEAPON_NONE;
		g_MpSetup.weapons[slot] = MPWEAPON_NONE;
		g_MatchConfig.weaponSetIndex = -1;
		mpCommitPreparedWeaponSet(WEAPONSET_CUSTOM, g_MpSetup.weapons);
		return 1;
	}
	entry = assetCatalogResolve(weapon_id);
	if (!entry || !entry->occupied || !entry->enabled
			|| entry->type != ASSET_WEAPON
			|| entry->mp_index < 0 || entry->mp_index > 255
			|| entry->runtime_index <= 0
			|| catalogGetMpWeaponNum(entry->mp_index) != entry->runtime_index) {
		return 0;
	}
	strncpy(g_MatchConfig.weapon_ids[slot], entry->id,
		sizeof(g_MatchConfig.weapon_ids[slot]) - 1);
	g_MatchConfig.weapon_ids[slot][
		sizeof(g_MatchConfig.weapon_ids[slot]) - 1] = '\0';
	g_MatchConfig.weapons[slot] = (u8)entry->mp_index;
	g_MpSetup.weapons[slot] = (u8)entry->mp_index;
	g_MatchConfig.weaponSetIndex = -1;
	mpCommitPreparedWeaponSet(WEAPONSET_CUSTOM, g_MpSetup.weapons);
	return 1;
}

void matchConfigInit(void)
{
	memset(&g_MatchConfig, 0, sizeof(g_MatchConfig));

	/* Set up global vars like the old Combat Simulator handler does */
	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;
	g_Vars.mpquickteam = MPQUICKTEAM_NONE;

	/* Default settings
	 *
	 * Engine encoding (see mpApplyLimits in mplayer.c):
	 *   timelimit:  (value + 1) * 60 seconds.  0 = 1 min, 9 = 10 min, >=60 = no limit.
	 *   scorelimit: value + 1 kills.  0 = 1 kill, 9 = 10 kills, >=100 = no limit.
	 *   teamscorelimit: similar, >=400 = no limit. */
	g_MatchConfig.scenario = MPSCENARIO_COMBAT;
	/* M0.1d: scenario_id is PRIMARY — resolve scenario integer at matchStart(). */
	strncpy(g_MatchConfig.scenario_id, "base:combat", sizeof(g_MatchConfig.scenario_id) - 1);
	g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
	/* stage_id is PRIMARY — resolve stagenum from it at matchStart().
	 * Default arena: Complex ("base:arena_mp_complex"). */
	strncpy(g_MatchConfig.stage_id, "base:arena_mp_complex", sizeof(g_MatchConfig.stage_id) - 1);
	g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
	{
		const asset_entry_t *ae = assetCatalogResolve(g_MatchConfig.stage_id);
		g_MatchConfig.stagenum = (ae && ae->type == ASSET_ARENA)
		    ? (u8)ae->ext.arena.stagenum : 0u;
	}
	g_MatchConfig.timelimit = 60;     /* no time limit (>=60 disables timer) */
	g_MatchConfig.scorelimit = 9;     /* first to 10 kills */
	g_MatchConfig.teamscorelimit = 400; /* no team score limit */
	/* F.6/B-70: Default spawn-with-weapon ON so bots and players always start armed. */
	g_MatchConfig.options = MPOPTION_SPAWNWITHWEAPON;
	/* INV-4 / Cohort D: engine-forced bit history starts empty. */
	g_MatchConfig.options_engine_forced = 0;
	g_MatchConfig.weaponSetIndex = 0;   /* default to first available preset (Pistols) */
	/* M0.1c: catalog ID is PRIMARY for spawn weapon. Empty + mode=RANDOM = roll
	 * once at matchStart(). Empty + mode=FIESTA = roll per-spawn. Non-empty +
	 * mode=SPECIFIC = use the named weapon. */
	g_MatchConfig.spawn_weapon_id[0] = '\0';
	g_MatchConfig.spawnWeaponNum = 0xFF; /* DEPRECATED derived cache */
	/* S482 (2026-04-27): default mode = RANDOM. The legacy default was the
	 * degenerate "fall back to weapons[0]" path which the user labeled
	 * "Random" in the dropdown but did NOT actually roll. RANDOM here makes
	 * the lobby pick reflect its label: every match rolls once across the
	 * active weapon set and uses that for every spawn. */
	g_MatchConfig.spawnWeaponMode = SPAWNWEAPON_MODE_RANDOM;
	/* The transactional selection below publishes exact IDs with the cache. */
	memset(g_MatchConfig.weapon_ids, 0, sizeof(g_MatchConfig.weapon_ids));
	g_MatchConfig.numSlots = 0;

	/* Ensure handicaps start at 100% (0x80).  g_PlayerConfigsArray is BSS
	 * (zero-initialized), and handicap=0 maps to ~0% — not the intended default. */
	matchResetHandicaps();

	/* Apply the default weapon set so g_MpSetup.weapons[] is populated.
	 * mpSetWeaponSet() maps the user-facing index through the unlock filter
	 * and calls mpApplyWeaponSet() to fill the 6 weapon slots. */
	if (!matchConfigSelectWeaponSet(g_MatchConfig.weaponSetIndex)) {
		sysLogPrintf(LOG_ERROR,
			"MATCHSETUP: default weapon set has no exact typed identity");
	}

	/* Slot 0 = local player — use agent name from save file */
	struct matchslot *s0 = &g_MatchConfig.slots[0];
	s0->type = SLOT_PLAYER;
	s0->team = 0;
	g_PlayerConfigsArray[0].base.team = s0->team;
	/* Copy catalog IDs directly from playerconfig; fall back to defaults
	 * if the save file hasn't populated them yet. */
	{
		const u8 mpbody = g_PlayerConfigsArray[0].base.mpbodynum;
		const u8 mphead = g_PlayerConfigsArray[0].base.mpheadnum;
		strncpy(s0->body_id,
		        g_PlayerConfigsArray[0].base.body_id[0] ? g_PlayerConfigsArray[0].base.body_id : "base:dark_combat",
		        sizeof(s0->body_id) - 1);
		s0->body_id[sizeof(s0->body_id) - 1] = '\0';
		strncpy(s0->head_id,
		        g_PlayerConfigsArray[0].base.head_id[0] ? g_PlayerConfigsArray[0].base.head_id : "base:head_dark_combat",
		        sizeof(s0->head_id) - 1);
		s0->head_id[sizeof(s0->head_id) - 1] = '\0';
		/* Cache derived mpbodynum/mpheadnum — matchStart re-derives but keep in sync */
		s0->bodynum = mpbody;
		s0->headnum = mphead;
	}

	/* Get the agent name from g_GameFile (loaded from save data).
	 * The old menu flow copies this via dialog handlers, but since we
	 * bypass those menus entirely, we pull it directly.
	 *
	 * NOTE: g_GameFile.name is still 11 chars (legacy save format).
	 * Our matchslot.name supports up to MAX_PLAYER_NAME (32) chars —
	 * the longer capacity is used for network names and future save
	 * format upgrades. For now we read what the save provides. */
	if (g_GameFile.name[0] != '\0') {
		strncpy(s0->name, g_GameFile.name, MAX_PLAYER_NAME - 1);
		s0->name[MAX_PLAYER_NAME - 1] = '\0';
		/* Also update the engine's player config (still 15 char limit internally) */
		strncpy(g_PlayerConfigsArray[0].base.name, g_GameFile.name, 11);
		/* PD names are terminated with \n then \0 */
		g_PlayerConfigsArray[0].base.name[strlen(g_GameFile.name)] = '\n';
		g_PlayerConfigsArray[0].base.name[strlen(g_GameFile.name) + 1] = '\0';
	} else {
		strncpy(s0->name, g_PlayerConfigsArray[0].base.name, MAX_PLAYER_NAME - 1);
		s0->name[MAX_PLAYER_NAME - 1] = '\0';
	}
	g_MatchConfig.numSlots = 1;

	/* matchResetHandicaps and the local identity writes above replace values
	 * cached when networking started. Refresh the one local network settings
	 * record after the reset; remote clients publish CLC_SETTINGS, while an
	 * in-client listen host updates its authoritative cache in place. */
	netClientSettingsChanged();

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: config initialized — player '%s' body_id='%s' head_id='%s'",
	             s0->name, s0->body_id, s0->head_id);
}

/* ========================================================================
 * Bot name + character randomization
 * ======================================================================== */

/* Bot name dictionaries — 256 entries each.
 * Combined "[adj] [name]" must fit in 14 chars (mpchrconfig.name limit).
 * Mods can override by placing botnames_adj.txt / botnames_noun.txt in their
 * mod folder (one word per line, max 8 chars each). The FS layer resolves
 * mod files with priority over base data. */

static const char *s_DefaultBotAdj[] = {
	/* Action/verb-style */
	"Runnin","Sneaky","Farmin","Slidin","Lurkin","Jumpin","Rollin","Creepin",
	"Dozin","Flexin","Fumblin","Hustlin","Idlin","Joggin","Kickin","Loafin",
	"Nappin","Pacin","Roamin","Sittin","Vibin","Walkin","Yeelin","Zoomin",
	/* Physical */
	"Fat","Tiny","Big","Lanky","Pudgy","Lumpy","Blobby","Stumpy",
	"Thicc","Scrawny","Chunky","Gangly","Broad","Gaunt","Husky","Burly",
	"Petite","Stocky","Wiry","Squat","Hulkin","Beefy","Tubby","Stout",
	/* Adjective */
	"Crusty","Dopey","Greasy","Manky","Mushy","Nasal","Soggy","Wonky",
	"Gassy","Clammy","Grumpy","Salty","Wheezy","Clunky","Funky","Squishy",
	"Cranky","Stinky","Wobbly","Dizzy","Rusty","Floppy","Burpy","Drippy",
	"Queasy","Rancid","Bumpy","Crushed","Dusty","Fizzy","Grimy","Gusty",
	/* Personality */
	"Angry","Sad","Shy","Bold","Calm","Dense","Edgy","Fierce",
	"Gentle","Hardy","Jolly","Keen","Loud","Meek","Noble","Plucky",
	"Rowdy","Sly","Tense","Uptight","Vivid","Wild","Zany","Moody",
	/* Texture/material */
	"Crispy","Crunchy","Gooey","Fuzzy","Slimy","Slick","Smooth","Rough",
	"Sticky","Flaky","Chalky","Silky","Gritty","Foamy","Chewy","Soggy",
	/* Name-style first words */
	"Hingle","Doink","Bimbus","Shmoop","Gronk","Skuzz","Plimbo","Fingle",
	"Blorp","Chumbo","Dweeb","Flonk","Gunge","Honk","Jimbo","Klonk",
	/* Greased-up / compound (short) */
	"Greased","Oiled","Buttery","Sweaty","Soaked","Frosted","Toasty","Cooked",
	"Baked","Fried","Steamed","Grilled","Poached","Smoked","Salted","Pickled",
	/* Funny misc */
	"Gay","Moist","Turbo","Ultra","Mega","Hyper","Super","Uber",
	"Lil","Old","Wee","Raw","Hot","Cold","Dry","Wet",
	"Evil","Holy","Dark","Void","Dank","Rare","Epic","Bogus",
	"Spicy","Zesty","Tangy","Bland","Mild","Tart","Sweet","Sour",
	/* More variety */
	"Basic","Fancy","Plain","Slap","Limp","Brisk","Stark","Blunt",
	"Cheap","Posh","Rank","Sketchy","Dodgy","Iffy","Grim","Dire",
	"Wack","Bonk","Gonk","Dank","Jank","Yeet","Based","Cringe",
	"Spooky","Eerie","Cursed","Haunted","Ghostly","Ashy","Musty","Moldy",
};
#define NUM_DEFAULT_ADJ (sizeof(s_DefaultBotAdj) / sizeof(s_DefaultBotAdj[0]))

static const char *s_DefaultBotNoun[] = {
	/* User-specified names */
	"Hershel","Doris","Irene","Truman","Hatman","Lad","Skittle","Smiff",
	"Teeth","Nick","Carlos","Gork","EEEEEEE",
	/* Original set */
	"Tud","Rodrick","Frunge","Stanley","Jenkins","Gorp","Blimpo","Sneed",
	"Winkle","Gribble","Plonk","Dingle","Spudge","Crambo","Muggins","Dorkus",
	"Flimble","Noodge","Cletus","Gormley","Pickles","Barnaby","Squib",
	/* Classic silly names */
	"Grungo","Bort","Clump","Thud","Splunk","Drongo","Fungus","Grelb",
	"Honkus","Jimbus","Klang","Lorf","Morp","Nub","Ogbert","Prunt",
	"Quimby","Runt","Slurp","Twerp","Ulp","Vronk","Whelk","Yonk","Zonk",
	/* Surname-style */
	"Smithers","Higgins","Perkins","Dawkins","Dobbs","Griggs","Hobbs","Judkins",
	"Kruggs","Lumley","Muffins","Norbert","Pudding","Quentin","Ruggles","Snodgrass",
	"Tompkins","Wiggins","Crumbs","Figgins","Guppy","Hubble","Dibbles","Fudge",
	/* Pop-culture-ish */
	"Bingus","Dingus","Goober","Boomer","Zoomer","Gamer","Karen","Chad",
	"Chonk","Stonks","Yolo","Bruh","Pleb","Noob","Simp","Chungus",
	/* Food names */
	"Turnip","Potato","Biscuit","Waffle","Nugget","Dumpling","Pretzel","Muffin",
	"Sausage","Pancake","Crouton","Gherkin","Noodle","Radish","Tofu","Sprout",
	/* Object names */
	"Bucket","Plunger","Sponge","Wrench","Cactus","Anvil","Brick","Cork",
	"Hinge","Knob","Plug","Shelf","Stump","Wedge","Trunk","Bolt",
	/* Animal-adjacent */
	"Badger","Ferret","Newt","Toad","Gecko","Stoat","Slug","Grub",
	"Shrimp","Roach","Gnat","Moth","Crab","Clam","Snail","Worm",
	/* More fun names */
	"Herb","Mort","Earl","Ned","Gus","Bud","Clem","Vern",
	"Otis","Floyd","Merle","Bruno","Angus","Rufus","Boris","Hank",
	"Agnes","Mabel","Ethel","Pearl","Myrtle","Gladys","Eunice","Bertha",
	"Norma","Edna","Selma","Wilma","Helga","Olga","Brunhild","Maude",
	/* Absurd compound */
	"Bingbong","Dingdong","Goopus","Shlorp","Blungus","Crumbus","Dongus","Flarb",
	"Glorb","Hunkus","Jelp","Klonkus","Mungus","Norkle","Plimbus","Quonk",
	/* More names */
	"Reginald","Percival","Thaddeus","Chadwick","Montague","Cornelius","Barnacle","Numbskull",
	"Bumstead","Thudwick","Plonker","Shlumpf","Gruntle","Smorkle","Blarney","Crumpet",
	"Scooter","Binky","Moomoo","Chowder","Brisket","Baguette","Crimp","Dweezil",
	"Flange","Giblet","Haggis","Inkblot","Jetsam","Kibble","Lint","Morsel",
};
#define NUM_DEFAULT_NOUN (sizeof(s_DefaultBotNoun) / sizeof(s_DefaultBotNoun[0]))

/* Runtime name pools — initialized from defaults, can be overridden by mod files.
 * botnames_adj.txt and botnames_noun.txt: one word per line, max 8 chars per word.
 * Placed in data/ or a mod's root folder. */
#define BOT_NAME_MAX_WORD 14 /* 13 chars + null — words can be longer for ImGui display */
#define BOT_NAME_POOL_MAX 256

static char s_AdjPool[BOT_NAME_POOL_MAX][BOT_NAME_MAX_WORD];
static s32  s_AdjCount = 0;
static char s_NounPool[BOT_NAME_POOL_MAX][BOT_NAME_MAX_WORD];
static s32  s_NounCount = 0;
static bool s_NamesLoaded = false;

static s32 loadNameFile(const char *filename, char pool[][BOT_NAME_MAX_WORD], s32 maxEntries)
{
	s32 count = 0;
	FILE *f = fsFileOpenRead(filename);
	if (!f) return 0;

	char line[64];
	while (count < maxEntries && fgets(line, sizeof(line), f)) {
		/* Strip newline/carriage return */
		s32 len = (s32)strlen(line);
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
			line[--len] = '\0';
		}
		if (len == 0 || len >= BOT_NAME_MAX_WORD) continue;
		strncpy(pool[count], line, BOT_NAME_MAX_WORD - 1);
		pool[count][BOT_NAME_MAX_WORD - 1] = '\0';
		count++;
	}

	fclose(f);
	sysLogPrintf(LOG_NOTE, "BOTNAMES: loaded %d entries from %s", count, filename);
	return count;
}

static void ensureNamePoolsLoaded(void)
{
	if (s_NamesLoaded) return;
	s_NamesLoaded = true;

	/* Try loading mod-overridable files first */
	s_AdjCount = loadNameFile("botnames_adj.txt", s_AdjPool, BOT_NAME_POOL_MAX);
	s_NounCount = loadNameFile("botnames_noun.txt", s_NounPool, BOT_NAME_POOL_MAX);

	/* Fall back to built-in defaults if no files found */
	if (s_AdjCount == 0) {
		s32 n = NUM_DEFAULT_ADJ < BOT_NAME_POOL_MAX ? (s32)NUM_DEFAULT_ADJ : BOT_NAME_POOL_MAX;
		for (s32 i = 0; i < n; i++) {
			strncpy(s_AdjPool[i], s_DefaultBotAdj[i], BOT_NAME_MAX_WORD - 1);
			s_AdjPool[i][BOT_NAME_MAX_WORD - 1] = '\0';
		}
		s_AdjCount = n;
	}
	if (s_NounCount == 0) {
		s32 n = NUM_DEFAULT_NOUN < BOT_NAME_POOL_MAX ? (s32)NUM_DEFAULT_NOUN : BOT_NAME_POOL_MAX;
		for (s32 i = 0; i < n; i++) {
			strncpy(s_NounPool[i], s_DefaultBotNoun[i], BOT_NAME_MAX_WORD - 1);
			s_NounPool[i][BOT_NAME_MAX_WORD - 1] = '\0';
		}
		s_NounCount = n;
	}

	sysLogPrintf(LOG_NOTE, "BOTNAMES: %d adjectives, %d nouns ready", s_AdjCount, s_NounCount);
}

static void generateBotName(char *dst, s32 maxLen)
{
	ensureNamePoolsLoaded();
	if (s_AdjCount == 0 || s_NounCount == 0) {
		snprintf(dst, maxLen, "Bot %d", rand() % 999);
		return;
	}
	s32 ai = rand() % s_AdjCount;
	s32 ni = rand() % s_NounCount;
	snprintf(dst, maxLen, "%s %s", s_AdjPool[ai], s_NounPool[ni]);
}

/* B-235 follow-up (2026-04-23): resolve the "head id for a given body" in a
 * way that respects HEAD_RANDOM_GENDER. catalogGetBodyDefaultHead returns NULL
 * for bodies with the random-gender sentinel (headnum=1000), which was forcing
 * callers to hardcode "base:head_dark_combat" as fallback - so Connery/Moore/
 * Dalton/Brosnan bots all landed on dark_combat head instead of a random male
 * head. mpDefaultHeadForBody (mplayer.c) already handles the gender pool; wrap
 * it here and convert the head mp_index back to a catalog ID string.
 *
 * P3 (2026-04-24): promoted to catalogPickRandomHeadIdForBody.  The new
 * accessor enumerates every head whose HEADBODYTYPE_* is compatible with
 * the body and returns a fresh random pick per call.  For a specific-pair
 * body (exactly one valid head) the result is deterministic; for Maian /
 * Skedar / human-male / human-female bodies with multiple heads in the
 * pool, each call yields a varied head so e.g. 31 Maian bots spawn with
 * 31 different Maian heads instead of all sharing one face. */
static const char *pickHeadIdForBody(const char *body_id)
{
	return catalogPickRandomHeadIdForBody(body_id);
}

/* Bodies catalog migration (Step 5): collector for the unlocked body
 * pool used by pickRandomBodyHead.  Reads catalog IDs straight off the
 * entry; capped at MS_MAX_RANDOM_POOL because the body pool fits in 128
 * with mods (63 base + headroom) and we want a deterministic stack
 * footprint.  When the iterator finds zero unlocked entries (e.g.
 * dedicated-server build with no assetCatalogRegisterBaseGame call) the
 * caller falls back to the "base:dark_combat" sentinel. */
#define MS_MAX_RANDOM_POOL 128
struct ms_random_body_ctx {
	const char *ids[MS_MAX_RANDOM_POOL];
	s32         count;
};

static void ms_collect_random_body(const asset_entry_t *e, void *userdata)
{
	struct ms_random_body_ctx *ctx = (struct ms_random_body_ctx *)userdata;
	if (ctx->count >= MS_MAX_RANDOM_POOL) return;
	if (!e->id || !e->id[0]) return;
	if (e->mp_index < 0) return;
	ctx->ids[ctx->count++] = e->id;
}

static void pickRandomBodyHead(char *body_id, s32 bodyLen, char *head_id, s32 headLen)
{
	struct ms_random_body_ctx pool;
	pool.count = 0;
	assetCatalogIterateUnlockedByType(ASSET_BODY, ms_collect_random_body, &pool);

	const char *picked_body = NULL;

	/* Empty pool fallback: dedicated server (no entries registered) or a
	 * playthrough with no unlocked bodies (theoretically impossible since
	 * base:dark_combat has requirefeature == 0).  Constant fallback keeps
	 * the function defined under all conditions, matching the heads I.6
	 * graceful-fallback intent. */
	if (pool.count == 0) {
		strncpy(body_id, "base:dark_combat", bodyLen - 1);
		body_id[bodyLen - 1] = '\0';
		strncpy(head_id, "base:head_dark_combat", headLen - 1);
		head_id[headLen - 1] = '\0';
		return;
	}

	/* Try up to 10 times to avoid duplicate body with existing bots */
	for (s32 attempt = 0; attempt < 10; attempt++) {
		u32 idx = (u32)rand() % (u32)pool.count;
		const char *candidate = pool.ids[idx];
		if (!candidate || !candidate[0]) continue;

		/* Check for duplicates among existing slots */
		bool dup = false;
		for (s32 s = 0; s < g_MatchConfig.numSlots; s++) {
			if (g_MatchConfig.slots[s].type != SLOT_EMPTY &&
			    strcmp(g_MatchConfig.slots[s].body_id, candidate) == 0) {
				dup = true;
				break;
			}
		}
		if (!dup || attempt == 9) {
			picked_body = candidate;
			break;
		}
	}

	if (!picked_body || !picked_body[0]) {
		picked_body = "base:dark_combat";
	}

	strncpy(body_id, picked_body, bodyLen - 1);
	body_id[bodyLen - 1] = '\0';

	/* B-235: use pickHeadIdForBody (catalogPickRandomHeadIdForBody) so
	 * HEAD_RANDOM_GENDER bodies get a gender-pool random head instead of
	 * the dark_combat fallback.  Heads Step 2 added the unlock filter
	 * inside that helper. */
	const char *paired_head = pickHeadIdForBody(picked_body);
	if (paired_head && paired_head[0]) {
		strncpy(head_id, paired_head, headLen - 1);
	} else {
		strncpy(head_id, "base:head_dark_combat", headLen - 1);
	}
	head_id[headLen - 1] = '\0';
}

/* ========================================================================
 * Slot management — called from ImGui bridge
 * ======================================================================== */

s32 matchConfigMaxBotsForHumans(s32 humanCount)
{
	s32 humans = humanCount > 0 ? humanCount : 1;
	/* Issue E: the total (humans + bots) must stay within
	 * MATCH_PARTICIPANT_CAP or CHR.TICK faults on the extra slot.  The
	 * previous formula used MATCH_MAX_SLOTS (the array size, 40) and let
	 * the combined count reach 33+, which crashed.  Still clamp against
	 * MAX_BOTS so we never exceed g_BotConfigsArray[]. */
	s32 bycap = MATCH_PARTICIPANT_CAP - humans;
	s32 maxbots = bycap < MAX_BOTS ? bycap : MAX_BOTS;

	return maxbots > 0 ? maxbots : 0;
}

static s32 matchConfigCountBots(void)
{
	s32 bots = 0;

	for (s32 i = 0; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
		if (g_MatchConfig.slots[i].type == SLOT_BOT) {
			bots++;
		}
	}

	return bots;
}

s32 matchConfigCountHumans(void)
{
	s32 humans = 0;

	for (s32 i = 0; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
		if (g_MatchConfig.slots[i].type == SLOT_PLAYER) {
			humans++;
		}
	}

	/* Always at least 1 (local player) to avoid degenerate caps when slots[] is
	 * mid-rebuild (e.g., SVC_ROOM_SETTINGS read before slot 0 is populated). */
	return humans > 0 ? humans : 1;
}

u8 matchConfigChooseBotTeam(s32 numTeams)
{
	/* Balance default bot assignment across the active team count (2..MAX_TEAMS).
	 * Ties are broken by lowest-index team, preserving prior 2-team behavior. */
	if (numTeams < 2) numTeams = 2;
	if (numTeams > MAX_TEAMS) numTeams = MAX_TEAMS;

	s32 counts[MAX_TEAMS] = {0};

	for (s32 i = 0; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
		if (g_MatchConfig.slots[i].type == SLOT_PLAYER
				|| g_MatchConfig.slots[i].type == SLOT_BOT) {
			u8 team = g_MatchConfig.slots[i].team;

			if (team < (u8)numTeams) {
				counts[team]++;
			}
		}
	}

	s32 bestTeam = 0;
	s32 bestCount = counts[0];
	for (s32 t = 1; t < numTeams; t++) {
		if (counts[t] < bestCount) {
			bestTeam = t;
			bestCount = counts[t];
		}
	}
	return (u8)bestTeam;
}

static s32 s_matchSlotApplyBotProfile(struct matchslot *slot,
		const char *profile_id, s32 apply_profile_body)
{
	const asset_runtime_binding_t *profile;
	const asset_entry_t *body;

	if (!slot || !profile_id || !profile_id[0]) {
		return -1;
	}

	profile = mpBotProfileRuntimeBindingById(profile_id);
	if (!profile) {
		return -1;
	}

	strncpy(slot->profile_id, profile_id, sizeof(slot->profile_id) - 1);
	slot->profile_id[sizeof(slot->profile_id) - 1] = '\0';
	slot->botType = (u8)profile->bot_profile_type;
	slot->botDifficulty = (u8)profile->bot_profile_difficulty;

	if (!apply_profile_body) {
		return 0;
	}
	if (!profile->target_id[0]) {
		sysLogPrintf(LOG_ERROR,
			"MATCHSETUP: bot profile '%s' has no target_body catalog ID",
			profile_id);
		return -1;
	}

	body = assetCatalogResolve(profile->target_id);
	if (!body || body->type != ASSET_BODY) {
		sysLogPrintf(LOG_ERROR,
			"MATCHSETUP: bot profile '%s' target body '%s' is unavailable",
			profile_id, profile->target_id);
		return -1;
	}

	strncpy(slot->body_id, body->id, sizeof(slot->body_id) - 1);
	slot->body_id[sizeof(slot->body_id) - 1] = '\0';
	{
		const char *head = pickHeadIdForBody(slot->body_id);
		if (!head || !head[0]) {
			head = "base:head_dark_combat";
		}
		strncpy(slot->head_id, head, sizeof(slot->head_id) - 1);
		slot->head_id[sizeof(slot->head_id) - 1] = '\0';
	}
	return 0;
}

s32 matchConfigSetBotProfile(s32 idx, const char *profile_id)
{
	struct matchslot *slot;

	if (idx < 0 || idx >= g_MatchConfig.numSlots) {
		return -1;
	}
	slot = &g_MatchConfig.slots[idx];
	if (slot->type != SLOT_BOT
			|| s_matchSlotApplyBotProfile(slot, profile_id, 1) != 0) {
		return -1;
	}

	slot->bodynum = 0;
	slot->headnum = 0;
	{
		const asset_entry_t *body = assetCatalogResolve(slot->body_id);
		const asset_entry_t *head = assetCatalogResolve(slot->head_id);
		if (body && body->type == ASSET_BODY && body->mp_index >= 0) {
			slot->bodynum = (u8)body->mp_index;
		}
		if (head && head->type == ASSET_HEAD && head->mp_index >= 0) {
			slot->headnum = (u8)head->mp_index;
		}
	}
	return 0;
}

s32 matchConfigSetBotTraits(s32 idx, u8 botType, u8 botDifficulty)
{
	const char *profile_id = mpBotProfileIdForTraits(botType, botDifficulty);

	if (!profile_id) {
		sysLogPrintf(LOG_ERROR,
			"MATCHSETUP: no unlocked bot profile for type=%u difficulty=%u",
			(unsigned)botType, (unsigned)botDifficulty);
		return -1;
	}
	return matchConfigSetBotProfile(idx, profile_id);
}

s32 matchConfigAddBotWithProfile(const char *profile_id, const char *body_id,
		const char *head_id, const char *name)
{
	if (g_MatchConfig.numSlots >= MATCH_MAX_SLOTS) {
		return -1;
	}

	if (matchConfigCountBots() >= matchConfigMaxBotsForHumans(matchConfigCountHumans())) {
		return -1;
	}

	s32 idx = g_MatchConfig.numSlots;
	struct matchslot *slot = &g_MatchConfig.slots[idx];
	memset(slot, 0, sizeof(*slot));
	slot->type = SLOT_BOT;
	slot->team = (matchConfigGetUserOptions() & MPOPTION_TEAMSENABLED)
		? matchConfigChooseBotTeam(2)
		: 0;
	if (s_matchSlotApplyBotProfile(slot, profile_id, 1) != 0) {
		memset(slot, 0, sizeof(*slot));
		return -1;
	}

	/* Explicit body/head are creator or saved-match overrides. Otherwise the
	 * public profile's target_body and body-default head remain authoritative. */
	if (body_id && body_id[0]) {
		strncpy(slot->body_id, body_id, sizeof(slot->body_id) - 1);
		slot->body_id[sizeof(slot->body_id) - 1] = '\0';
		{
			/* B-235: pickHeadIdForBody handles HEAD_RANDOM_GENDER bodies
			 * (returns a gender-pool random head); catalogGetBodyDefaultHead
			 * would have returned NULL for those and forced the dark_combat
			 * fallback, so Bond-actor / generic NPC bodies landed on the
			 * same head regardless of body. */
			const char *h = (head_id && head_id[0])
			              ? head_id
			              : pickHeadIdForBody(body_id);
			if (!h || !h[0]) h = "base:head_dark_combat";
			strncpy(slot->head_id, h, sizeof(slot->head_id) - 1);
			slot->head_id[sizeof(slot->head_id) - 1] = '\0';
		}
	}

	/* Derive cached mpbodynum/mpheadnum from catalog for legacy path.
	 * matchStart() re-derives at the last moment; these are just for display. */
	slot->bodynum = 0; /* MPBODY_DARK_COMBAT default */
	slot->headnum = 0; /* MPHEAD_DARK_COMBAT default */
	{
		const asset_entry_t *be = assetCatalogResolve(slot->body_id);
		if (be && be->type == ASSET_BODY && be->mp_index >= 0) {
			slot->bodynum = (u8)be->mp_index;
		}
	}
	{
		const asset_entry_t *he = assetCatalogResolve(slot->head_id);
		if (he && he->type == ASSET_HEAD && he->mp_index >= 0) {
			slot->headnum = (u8)he->mp_index;
		}
	}

	if (name && name[0]) {
		strncpy(slot->name, name, MAX_PLAYER_NAME - 1);
		slot->name[MAX_PLAYER_NAME - 1] = '\0';
	} else {
		generateBotName(slot->name, MAX_PLAYER_NAME);
	}

	g_MatchConfig.numSlots++;
	return idx;
}

s32 matchConfigAddBot(u8 botType, u8 botDifficulty, const char *body_id,
                      const char *head_id, const char *name)
{
	const char *profile_id = mpBotProfileIdForTraits(botType, botDifficulty);

	if (!profile_id) {
		sysLogPrintf(LOG_ERROR,
			"MATCHSETUP: cannot add bot without an unlocked profile for "
			"type=%u difficulty=%u", (unsigned)botType,
			(unsigned)botDifficulty);
		return -1;
	}
	return matchConfigAddBotWithProfile(profile_id, body_id, head_id, name);
}

s32 matchConfigRemoveSlot(s32 idx)
{
	if (idx < 1 || idx >= g_MatchConfig.numSlots) {
		return -1; /* Can't remove slot 0 (local player) or invalid */
	}

	/* Shift remaining slots down */
	for (s32 i = idx; i < g_MatchConfig.numSlots - 1; i++) {
		g_MatchConfig.slots[i] = g_MatchConfig.slots[i + 1];
	}

	g_MatchConfig.numSlots--;
	memset(&g_MatchConfig.slots[g_MatchConfig.numSlots], 0, sizeof(struct matchslot));
	return 0;
}

void matchConfigRerollBot(s32 idx)
{
	if (idx < 1 || idx >= g_MatchConfig.numSlots) return;
	struct matchslot *sl = &g_MatchConfig.slots[idx];
	if (sl->type != SLOT_BOT) return;

	generateBotName(sl->name, MAX_PLAYER_NAME);
	pickRandomBodyHead(sl->body_id, sizeof(sl->body_id),
	                   sl->head_id, sizeof(sl->head_id));

	/* Re-derive cached mpbodynum/mpheadnum */
	sl->bodynum = 0;
	sl->headnum = 0;
	const asset_entry_t *be = assetCatalogResolve(sl->body_id);
	if (be && be->type == ASSET_BODY && be->mp_index >= 0) {
		sl->bodynum = (u8)be->mp_index;
	}
	const asset_entry_t *he = assetCatalogResolve(sl->head_id);
	if (he && he->type == ASSET_HEAD && he->mp_index >= 0) {
		sl->headnum = (u8)he->mp_index;
	}
}

void matchConfigRerollBotName(s32 idx)
{
	if (idx < 1 || idx >= g_MatchConfig.numSlots) return;
	struct matchslot *sl = &g_MatchConfig.slots[idx];
	if (sl->type != SLOT_BOT) return;
	generateBotName(sl->name, MAX_PLAYER_NAME);
}

/* ========================================================================
 * Spawn-weapon roll helpers (S482, 2026-04-27)
 *
 * Random / Fiesta semantics depend on a uniform pick across the active
 * match weapon set, with NONE / DISABLED / SHIELD slots filtered out.
 *
 * spawnWeaponPickFromSlots is the pure variant — pluggable RNG, no globals.
 * The test suite (tests/test_spawn_weapon_mode.cpp) replicates the spec
 * directly per the test_random_pool.cpp pattern; this signature is provided
 * so callers in non-test code (or future test bins that link the live
 * function) have a stable API.
 *
 * spawnWeaponPickFromActiveSet is the live entry: reads g_MpSetup.weapons[]
 * and rngRandom() and returns the chosen MPWEAPON_* index.
 * ======================================================================== */

s32 spawnWeaponPickFromSlots(const u8 *slots, s32 numSlots,
                             u32 (*rng_fn)(void *userdata), void *userdata)
{
	u8 eligible[NUM_MPWEAPONSLOTS];
	s32 num_eligible = 0;

	if (slots == NULL || numSlots <= 0 || rng_fn == NULL) {
		return 0;
	}
	if (numSlots > NUM_MPWEAPONSLOTS) {
		numSlots = NUM_MPWEAPONSLOTS;
	}
	for (s32 i = 0; i < numSlots; i++) {
		u8 w = slots[i];
		if (w == MPWEAPON_NONE) continue;
		if (w == MPWEAPON_DISABLED) continue;
		if (w == MPWEAPON_SHIELD) continue;
		eligible[num_eligible++] = w;
	}
	if (num_eligible == 0) {
		return 0;
	}
	u32 r = rng_fn(userdata);
	return (s32)eligible[r % (u32)num_eligible];
}

static u32 spawnWeaponRngBridge(void *userdata)
{
	(void)userdata;
	return rngRandom();
}

s32 spawnWeaponPickFromActiveSet(void)
{
	return spawnWeaponPickFromSlots(g_MpSetup.weapons, NUM_MPWEAPONSLOTS,
	                                spawnWeaponRngBridge, NULL);
}

/* ------------------------------------------------------------------------
 * S483 (2026-04-27): host-eligible weapon pool via match manifest.
 *
 * Mike's clarification on Random/Fiesta: the eligible pool draws from the
 * host's full unlocked-weapon catalog, distributed via the match manifest
 * (`SVC_MATCH_MANIFEST` carries every MANIFEST_TYPE_WEAPON entry the host
 * enumerated at match start — see netmanifest.c manifestBuild +
 * manifestBuildForHost). At spawn time both host and clients walk the
 * loaded manifest, resolve catalog IDs to MPWEAPON_* indices, filter
 * NONE/DISABLED/SHIELD, and roll. Mod-only weapons distribute "as needed"
 * via the existing SVC_CATALOG_INFO + SVC_DISTRIB pipeline (ASSET_WEAPON
 * is already in the SVC_CATALOG_INFO type list — see netmsg.c
 * netmsgSvcCatalogInfoWrite).
 *
 * Fallback discipline (per directive): if the manifest pool resolves to
 * zero eligible weapons (e.g. solo CS where no manifest has been broadcast,
 * or a degenerate manifest), fall back to spawnWeaponPickFromActiveSet()
 * so the match never spawns players empty-handed.
 *
 * Test surface: tests/test_spawn_weapon_mode.cpp replicates the helper as
 * a pure spec (catalog + manifest globals are not linked into pd-tests).
 * ------------------------------------------------------------------------ */

/* Internal: pick the most-relevant manifest for spawn-time pool resolution.
 * Cascade:
 *   1. g_CurrentLoadedManifest -- post-transition definitive list.
 *   2. g_ServerManifest        -- host-side built manifest (pre-broadcast).
 *   3. g_ClientManifest        -- received from server (post-wire).
 *   4. NULL                    -- caller falls back to active weapon set.
 */
static const match_manifest_t *spawnWeaponSelectManifest(void)
{
	if (g_CurrentLoadedManifest.num_entries > 0) return &g_CurrentLoadedManifest;
	if (g_ServerManifest.num_entries > 0)        return &g_ServerManifest;
	if (g_ClientManifest.num_entries > 0)        return &g_ClientManifest;
	return NULL;
}

static s32 spawnWeaponBuildPoolFromManifest(const match_manifest_t *m,
                                            u8 *out_pool, s32 out_cap)
{
	if (!m || !out_pool || out_cap <= 0) return 0;
	s32 count = 0;
	for (u16 i = 0; i < m->num_entries && count < out_cap; i++) {
		const match_manifest_entry_t *e = &m->entries[i];
		if (e->type != MANIFEST_TYPE_WEAPON) continue;
		const asset_entry_t *ae = e->id[0] ? assetCatalogResolve(e->id) : NULL;
		if (!ae || ae->type != ASSET_WEAPON) continue;
		s32 wid = (s32)ae->ext.weapon.weapon_id;
		if (wid <= 0 || wid >= NUM_MPWEAPONS) continue;
		if (wid == MPWEAPON_NONE)     continue;
		if (wid == MPWEAPON_DISABLED) continue;
		if (wid == MPWEAPON_SHIELD)   continue;
		out_pool[count++] = (u8)wid;
	}
	return count;
}

s32 spawnWeaponPickFromMatchManifest(void)
{
	const match_manifest_t *m = spawnWeaponSelectManifest();
	if (!m) {
		return spawnWeaponPickFromActiveSet();
	}
	u8 pool[NUM_MPWEAPONS];
	s32 count = spawnWeaponBuildPoolFromManifest(m, pool, (s32)NUM_MPWEAPONS);
	if (count == 0) {
		return spawnWeaponPickFromActiveSet();
	}
	u32 r = rngRandom();
	return (s32)pool[r % (u32)count];
}

static s32 spawnWeaponPickFromPreparedMatch(const u8 *prepared_slots)
{
	const match_manifest_t *manifest = spawnWeaponSelectManifest();
	u8 pool[NUM_MPWEAPONS];
	s32 count = spawnWeaponBuildPoolFromManifest(
		manifest, pool, (s32)ARRAYCOUNT(pool));

	if (count > 0) {
		return (s32)pool[rngRandom() % (u32)count];
	}

	return spawnWeaponPickFromSlots(prepared_slots, NUM_MPWEAPONSLOTS,
		spawnWeaponRngBridge, NULL);
}

typedef struct match_start_participant_plan_t {
	ParticipantType type;
	u8 participant_slot;
	u8 team;
	player_identity_plan_t identity;
	struct mpchrconfig player;
	struct mpbotconfig bot;
} match_start_participant_plan_t;

typedef struct match_start_plan_t {
	match_start_status_e status;
	struct mpsetup setup;
	char weapon_ids[NUM_MPWEAPONSLOTS][CATALOG_ID_LEN];
	u8 spawn_weapon_num;
	s32 resolved_weapon_set;
	s32 num_participants;
	s32 num_players;
	s32 num_bots;
	match_start_participant_plan_t participants[MATCH_PARTICIPANT_CAP];
} match_start_plan_t;

const char *matchStartStatusString(match_start_status_e status)
{
	switch (status) {
	case MATCH_START_OK: return "ok";
	case MATCH_START_INVALID_SLOT_COUNT: return "invalid_slot_count";
	case MATCH_START_INVALID_PARTICIPANT: return "invalid_participant";
	case MATCH_START_NO_PLAYERS: return "no_players";
	case MATCH_START_INVALID_SCENARIO: return "invalid_scenario";
	case MATCH_START_INVALID_STAGE: return "invalid_stage";
	case MATCH_START_INVALID_WEAPON_SET: return "invalid_weapon_set";
	case MATCH_START_INVALID_WEAPON: return "invalid_weapon";
	case MATCH_START_INVALID_SPAWN_WEAPON: return "invalid_spawn_weapon";
	case MATCH_START_INVALID_PLAYER_IDENTITY: return "invalid_player_identity";
	case MATCH_START_INVALID_BOT_PROFILE: return "invalid_bot_profile";
	case MATCH_START_INVALID_BOT_IDENTITY: return "invalid_bot_identity";
	case MATCH_START_PARTICIPANT_POOL_UNAVAILABLE: return "participant_pool_unavailable";
	default: return "unknown";
	}
}

static match_start_status_e matchStartReject(match_start_plan_t *plan,
		match_start_status_e status, const char *detail)
{
	if (plan != NULL) {
		plan->status = status;
	}
	sysLogPrintf(LOG_ERROR,
		"PLAYER.INIT.PREFLIGHT match=reject status=%s detail=%s",
		matchStartStatusString(status), detail ? detail : "unspecified");
	return status;
}

static s32 matchStartIdIsValid(const char *id, size_t capacity)
{
	return id != NULL && capacity > 0 && id[0] != '\0'
		&& memchr(id, '\0', capacity) != NULL;
}

static match_start_status_e matchStartPrepareIdentity(
		const struct matchslot *slot, player_identity_plan_t *identity,
		match_start_status_e failure_status, match_start_plan_t *plan)
{
	player_identity_status_e status;

	status = playerIdentityPrepare(slot->body_id, slot->head_id, identity);
	if (status != PLAYER_IDENTITY_OK) {
		return matchStartReject(plan, failure_status,
			playerIdentityStatusString(status));
	}

	return MATCH_START_OK;
}

static match_start_status_e matchStartPrepare(match_start_plan_t *plan)
{
	const asset_entry_t *entry;
	u8 custom_weapons[NUM_MPWEAPONSLOTS];
	s32 player_slot = 0;
	s32 bot_slot = 0;
	s32 i;

	if (plan == NULL) {
		return MATCH_START_INVALID_PARTICIPANT;
	}
	memset(plan, 0, sizeof(*plan));
	plan->status = MATCH_START_INVALID_PARTICIPANT;
	plan->setup = g_MpSetup;

	if (g_MatchConfig.numSlots == 0
			|| g_MatchConfig.numSlots > MATCH_MAX_SLOTS
			|| g_MatchConfig.numSlots > MATCH_PARTICIPANT_CAP) {
		return matchStartReject(plan, MATCH_START_INVALID_SLOT_COUNT,
			"numSlots outside participant contract");
	}
	if (g_MpParticipants.slots == NULL
			|| g_MpParticipants.capacity < MAX_PLAYERS + MAX_BOTS) {
		return matchStartReject(plan, MATCH_START_PARTICIPANT_POOL_UNAVAILABLE,
			"participant pool is not initialized at full runtime capacity");
	}

	if (!matchStartIdIsValid(g_MatchConfig.scenario_id,
			sizeof(g_MatchConfig.scenario_id))) {
		return matchStartReject(plan, MATCH_START_INVALID_SCENARIO,
			"scenario ID is empty or unterminated");
	}
	entry = assetCatalogResolve(g_MatchConfig.scenario_id);
	if (!entry || entry->type != ASSET_GAMEMODE
			|| entry->ext.gamemode.mode_id < MPSCENARIO_COMBAT
			|| entry->ext.gamemode.mode_id > MPSCENARIO_CAPTURETHECASE) {
		return matchStartReject(plan, MATCH_START_INVALID_SCENARIO,
			g_MatchConfig.scenario_id);
	}
	plan->setup.scenario = (u8)entry->ext.gamemode.mode_id;

	if (!matchStartIdIsValid(g_MatchConfig.stage_id,
			sizeof(g_MatchConfig.stage_id))) {
		return matchStartReject(plan, MATCH_START_INVALID_STAGE,
			"stage ID is empty or unterminated");
	}
	entry = assetCatalogResolve(g_MatchConfig.stage_id);
	if (entry && entry->type == ASSET_ARENA) {
		if (entry->ext.arena.stagenum <= 0 || entry->ext.arena.stagenum > 255) {
			return matchStartReject(plan, MATCH_START_INVALID_STAGE,
				"arena runtime stagenum is outside the wire/runtime domain");
		}
		plan->setup.stagenum = (u8)entry->ext.arena.stagenum;
	} else if (entry && entry->type == ASSET_MAP) {
		if (entry->ext.map.stagenum <= 0 || entry->ext.map.stagenum > 255) {
			return matchStartReject(plan, MATCH_START_INVALID_STAGE,
				"map runtime stagenum is outside the wire/runtime domain");
		}
		plan->setup.stagenum = (u8)entry->ext.map.stagenum;
	} else {
		return matchStartReject(plan, MATCH_START_INVALID_STAGE,
			g_MatchConfig.stage_id);
	}
	if (plan->setup.stagenum == 0) {
		return matchStartReject(plan, MATCH_START_INVALID_STAGE,
			"stage has no runtime stagenum binding");
	}
	strncpy(plan->setup.stage_id, g_MatchConfig.stage_id,
		sizeof(plan->setup.stage_id) - 1);
	plan->setup.stage_id[sizeof(plan->setup.stage_id) - 1] = '\0';
	plan->setup.timelimit = g_MatchConfig.timelimit;
	plan->setup.scorelimit = g_MatchConfig.scorelimit;
	plan->setup.teamscorelimit = g_MatchConfig.teamscorelimit;
	plan->setup.options = matchConfigGetUserOptions();

	memcpy(custom_weapons, g_MpSetup.weapons, sizeof(custom_weapons));
	for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		if (g_MatchConfig.weapon_ids[i][0] == '\0') {
			if (g_MatchConfig.weaponSetIndex < 0) {
				return matchStartReject(plan, MATCH_START_INVALID_WEAPON,
					"custom weapon slot has no typed ID");
			}
			continue;
		}

		if (!matchStartIdIsValid(g_MatchConfig.weapon_ids[i],
				sizeof(g_MatchConfig.weapon_ids[i]))) {
			return matchStartReject(plan, MATCH_START_INVALID_WEAPON,
				"weapon ID is unterminated");
		}
		entry = assetCatalogResolve(g_MatchConfig.weapon_ids[i]);
		if (!entry || entry->type != ASSET_WEAPON || entry->mp_index < 0
				|| entry->mp_index > 255 || entry->runtime_index <= 0
				|| catalogGetMpWeaponNum(entry->mp_index) != entry->runtime_index) {
			return matchStartReject(plan, MATCH_START_INVALID_WEAPON,
				g_MatchConfig.weapon_ids[i]);
		}
		custom_weapons[i] = (u8)entry->mp_index;
	}

	if (mpPrepareWeaponSet(g_MatchConfig.weaponSetIndex,
			custom_weapons, plan->setup.weapons,
			&plan->resolved_weapon_set) != 0) {
		return matchStartReject(plan, MATCH_START_INVALID_WEAPON_SET,
			"weapon set could not prepare without global mutation");
	}
	/* Typed custom IDs are authoritative even when the selected base set is
	 * retained for UI grouping. */
	for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		if (g_MatchConfig.weapon_ids[i][0] != '\0') {
			plan->setup.weapons[i] = custom_weapons[i];
		}
	}
	/* Freeze an exact typed identity for every resolved non-empty slot.  Preset
	 * selection begins as an index, but the committed match and network writer
	 * must never reconstruct public identity from a numeric cache later. */
	for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		const s32 mp_weapon_id = (s32)plan->setup.weapons[i];
		const char *weapon_id;

		if (mp_weapon_id == MPWEAPON_NONE) {
			plan->weapon_ids[i][0] = '\0';
			continue;
		}
		weapon_id = g_MatchConfig.weapon_ids[i][0]
			? g_MatchConfig.weapon_ids[i]
			: catalogWeaponIdByMpWeaponId(mp_weapon_id);
		entry = weapon_id ? assetCatalogResolve(weapon_id) : NULL;
		if (!entry || !entry->occupied || !entry->enabled
				|| entry->type != ASSET_WEAPON
				|| entry->mp_index != mp_weapon_id
				|| entry->runtime_index <= 0
				|| catalogGetMpWeaponNum(entry->mp_index)
					!= entry->runtime_index) {
			return matchStartReject(plan, MATCH_START_INVALID_WEAPON,
				"resolved weapon slot has no exact typed identity");
		}
		strncpy(plan->weapon_ids[i], entry->id,
			sizeof(plan->weapon_ids[i]) - 1);
		plan->weapon_ids[i][sizeof(plan->weapon_ids[i]) - 1] = '\0';
	}

	switch (g_MatchConfig.spawnWeaponMode) {
	case SPAWNWEAPON_MODE_FIESTA:
		plan->spawn_weapon_num = SPAWNWEAPON_FIESTA_SENTINEL;
		break;
	case SPAWNWEAPON_MODE_RANDOM: {
		s32 picked = spawnWeaponPickFromPreparedMatch(plan->setup.weapons);
		if (picked <= MPWEAPON_NONE || picked >= NUM_MPWEAPONS) {
			sysLogPrintf(LOG_WARNING,
				"MATCH.PREFLIGHT: random spawn pool empty; using Falcon 2 safety fallback");
			picked = MPWEAPON_FALCON2;
		}
		plan->spawn_weapon_num = (u8)catalogGetMpWeaponNum(picked);
		if (!spawnWeaponNumIsResolved(plan->spawn_weapon_num)) {
			return matchStartReject(plan, MATCH_START_INVALID_SPAWN_WEAPON,
				"random weapon has no runtime binding");
		}
		break;
	}
	case SPAWNWEAPON_MODE_SPECIFIC:
		if (!matchStartIdIsValid(g_MatchConfig.spawn_weapon_id,
				sizeof(g_MatchConfig.spawn_weapon_id))) {
			return matchStartReject(plan, MATCH_START_INVALID_SPAWN_WEAPON,
				"specific spawn weapon ID is empty or unterminated");
		}
		entry = assetCatalogResolve(g_MatchConfig.spawn_weapon_id);
		if (!entry || entry->type != ASSET_WEAPON || entry->runtime_index <= 0
				|| entry->runtime_index >= WEAPON_CUSTOM_END) {
			return matchStartReject(plan, MATCH_START_INVALID_SPAWN_WEAPON,
				g_MatchConfig.spawn_weapon_id);
		}
		plan->spawn_weapon_num = (u8)entry->runtime_index;
		break;
	default:
		return matchStartReject(plan, MATCH_START_INVALID_SPAWN_WEAPON,
			"unknown spawn weapon mode");
	}

	for (i = 0; i < g_MatchConfig.numSlots; i++) {
		const struct matchslot *slot = &g_MatchConfig.slots[i];
		match_start_participant_plan_t *participant;

		if (slot->type == SLOT_EMPTY) {
			continue;
		}
		if (plan->num_participants >= MATCH_PARTICIPANT_CAP) {
			return matchStartReject(plan, MATCH_START_INVALID_PARTICIPANT,
				"participant plan capacity exceeded");
		}
		participant = &plan->participants[plan->num_participants];
		if (slot->team >= MAX_TEAMS) {
			return matchStartReject(plan, MATCH_START_INVALID_PARTICIPANT,
				"participant team is outside the runtime domain");
		}
		participant->team = slot->team;

		if (slot->type == SLOT_PLAYER) {
			match_start_status_e status;
			if (player_slot >= MAX_PLAYERS) {
				return matchStartReject(plan, MATCH_START_INVALID_PARTICIPANT,
					"too many player slots");
			}
			status = matchStartPrepareIdentity(slot, &participant->identity,
				MATCH_START_INVALID_PLAYER_IDENTITY, plan);
			if (status != MATCH_START_OK) return status;
			participant->type = PARTICIPANT_LOCAL;
			participant->participant_slot = (u8)player_slot;
			participant->player = g_PlayerConfigsArray[player_slot].base;
			participant->player.mpbodynum = participant->identity.mp_body_index >= 0
				? (u8)participant->identity.mp_body_index : 0xFF;
			participant->player.mpheadnum = participant->identity.mp_head_index >= 0
				? (u8)participant->identity.mp_head_index : 0xFF;
			participant->player.team = slot->team;
			strncpy(participant->player.body_id, participant->identity.body_id,
				sizeof(participant->player.body_id) - 1);
			participant->player.body_id[sizeof(participant->player.body_id) - 1] = '\0';
			strncpy(participant->player.head_id, participant->identity.head_id,
				sizeof(participant->player.head_id) - 1);
			participant->player.head_id[sizeof(participant->player.head_id) - 1] = '\0';
			strncpy(participant->player.name, slot->name,
				sizeof(participant->player.name) - 1);
			participant->player.name[sizeof(participant->player.name) - 1] = '\0';
			player_slot++;
		} else if (slot->type == SLOT_BOT) {
			const asset_runtime_binding_t *profile;
			match_start_status_e status;
			if (bot_slot >= MAX_BOTS) {
				return matchStartReject(plan, MATCH_START_INVALID_PARTICIPANT,
					"too many bot slots");
			}
			if (!matchStartIdIsValid(slot->profile_id,
					sizeof(slot->profile_id))) {
				return matchStartReject(plan, MATCH_START_INVALID_BOT_PROFILE,
					"bot profile ID is empty or unterminated");
			}
			entry = assetCatalogResolve(slot->profile_id);
			profile = entry && entry->type == ASSET_BOT_PROFILE
				? assetRuntimeFindByTypeAndId(ASSET_BOT_PROFILE, slot->profile_id)
				: NULL;
			if (!profile || !profile->active || !profile->source_hydrated
					|| profile->type != ASSET_BOT_PROFILE
					|| strcmp(profile->id, slot->profile_id) != 0
					|| !assetRuntimePrimaryFileAccessible(profile)
					|| profile->bot_profile_type < 0
					|| profile->bot_profile_type > 255
					|| profile->bot_profile_difficulty < 0
					|| profile->bot_profile_difficulty > 255) {
				return matchStartReject(plan, MATCH_START_INVALID_BOT_PROFILE,
					slot->profile_id);
			}
			status = matchStartPrepareIdentity(slot, &participant->identity,
				MATCH_START_INVALID_BOT_IDENTITY, plan);
			if (status != MATCH_START_OK) return status;
			participant->type = PARTICIPANT_BOT;
			participant->participant_slot = (u8)(MAX_PLAYERS + bot_slot);
			participant->bot = g_BotConfigsArray[bot_slot];
			participant->bot.base.mpbodynum = participant->identity.mp_body_index >= 0
				? (u8)participant->identity.mp_body_index : 0xFF;
			participant->bot.base.mpheadnum = participant->identity.mp_head_index >= 0
				? (u8)participant->identity.mp_head_index : 0xFF;
			participant->bot.base.team = slot->team;
			participant->bot.type = (u8)profile->bot_profile_type;
			participant->bot.difficulty = (u8)profile->bot_profile_difficulty;
			strncpy(participant->bot.profile_id, slot->profile_id,
				sizeof(participant->bot.profile_id) - 1);
			participant->bot.profile_id[sizeof(participant->bot.profile_id) - 1] = '\0';
			strncpy(participant->bot.base.body_id, participant->identity.body_id,
				sizeof(participant->bot.base.body_id) - 1);
			participant->bot.base.body_id[sizeof(participant->bot.base.body_id) - 1] = '\0';
			strncpy(participant->bot.base.head_id, participant->identity.head_id,
				sizeof(participant->bot.base.head_id) - 1);
			participant->bot.base.head_id[sizeof(participant->bot.base.head_id) - 1] = '\0';
			strncpy(participant->bot.base.name, slot->name,
				sizeof(participant->bot.base.name) - 1);
			participant->bot.base.name[sizeof(participant->bot.base.name) - 1] = '\0';
			bot_slot++;
		} else {
			return matchStartReject(plan, MATCH_START_INVALID_PARTICIPANT,
				"unknown match slot type");
		}

		plan->num_participants++;
	}

	if (player_slot == 0) {
		return matchStartReject(plan, MATCH_START_NO_PLAYERS,
			"match has no human player");
	}

	plan->num_players = player_slot;
	plan->num_bots = bot_slot;
	plan->status = MATCH_START_OK;
	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.PREFLIGHT match=ok players=%d bots=%d stage='%s' scenario='%s'",
		player_slot, bot_slot, plan->setup.stage_id, g_MatchConfig.scenario_id);
	return MATCH_START_OK;
}

static void matchStartCommit(const match_start_plan_t *plan)
{
	s32 i;

	matchConfigRestoreUserOptions("matchStart commit");
	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;
	g_MpSetup = plan->setup;
	g_MatchConfig.scenario = plan->setup.scenario;
	g_MatchConfig.stagenum = plan->setup.stagenum;
	memcpy(g_MatchConfig.weapon_ids, plan->weapon_ids,
		sizeof(g_MatchConfig.weapon_ids));
	memcpy(g_MatchConfig.weapons, plan->setup.weapons,
		sizeof(g_MatchConfig.weapons));
	g_MatchConfig.spawnWeaponNum = plan->spawn_weapon_num;
	mpCommitPreparedWeaponSet(plan->resolved_weapon_set, plan->setup.weapons);
	mpClearAllParticipants();

	for (i = 0; i < plan->num_participants; i++) {
		const match_start_participant_plan_t *participant = &plan->participants[i];
		if (participant->type == PARTICIPANT_LOCAL) {
			g_PlayerConfigsArray[participant->participant_slot].base = participant->player;
			mpAddParticipantAt(participant->participant_slot,
				PARTICIPANT_LOCAL, participant->team, 0,
				participant->participant_slot);
		} else {
			const s32 bot_slot = participant->participant_slot - MAX_PLAYERS;
			g_BotConfigsArray[bot_slot] = participant->bot;
			mpAddParticipantAt(participant->participant_slot,
				PARTICIPANT_BOT, participant->team, -1, 0xFF);
		}
	}

	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT match players=%d bots=%d activeMask=0x%016llx",
		plan->num_players, plan->num_bots,
		(unsigned long long)mpParticipantsEncodeActiveMask());
}

/* ========================================================================
 * Match start — the clean replacement for the old menutick flow
 * ======================================================================== */

s32 matchStart(void)
{
	match_start_plan_t plan;
	match_start_status_e status;
	const u64 rng_seed = g_RngSeed;
	const u64 rng2_seed = g_Rng2Seed;

	sysLogPrintf(LOG_NOTE,
		"MATCHSTART.DIAG: preflight stage_id='%s' scenario_id='%s' slots=%d "
		"weaponSet=%d spawn_weapon='%s'",
		g_MatchConfig.stage_id, g_MatchConfig.scenario_id,
		g_MatchConfig.numSlots, g_MatchConfig.weaponSetIndex,
		g_MatchConfig.spawn_weapon_id);
	crashBreadcrumbPush("MATCHSTART preflight stage='%s' slots=%d",
		g_MatchConfig.stage_id, g_MatchConfig.numSlots);

	challengeDetermineUnlockedFeatures();
	status = matchStartPrepare(&plan);

	if (status != MATCH_START_OK) {
		/* Random weapon-set/spawn preparation is allowed to consume RNG only
		 * when the transaction commits. Failed preflight is observationally
		 * side-effect free. */
		g_RngSeed = rng_seed;
		g_Rng2Seed = rng2_seed;
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ROLLBACK match status=%s globals_published=0",
			matchStartStatusString(status));
		return -(s32)status;
	}

	matchStartCommit(&plan);
	g_NotLoadMod = false;
	romdataFileFreeForSolo();

	sysLogPrintf(LOG_NOTE,
		"MATCHSTART.DIAG: committed stage=0x%02x activeMask=0x%llx "
		"players=%d bots=%d",
		(u32)g_MpSetup.stagenum,
		(unsigned long long)mpParticipantsEncodeActiveMask(),
		plan.num_players, plan.num_bots);
	crashBreadcrumbPush("MATCHSTART commit stage=0x%02x players=%d bots=%d",
		(u32)g_MpSetup.stagenum, plan.num_players, plan.num_bots);

	mpStartMatch();

	sysLogPrintf(LOG_NOTE,
		"MATCHSTART.DIAG: post-mpStartMatch returned, about to menuStop()");
	crashBreadcrumbPush("MATCHSTART post-mpStartMatch");

	menuStop();
	sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL,
		"matchStart");

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: match started successfully");
	return 0;
}
/* ========================================================================
 * M0.1c: Weapon slot catalog ID accessor
 * ======================================================================== */

const char *matchGetWeaponSlotCatalogId(s32 slot)
{
	if (slot < 0 || slot >= NUM_MPWEAPONSLOTS) return "";
	u8 mpw = g_MpSetup.weapons[slot];
	if (mpw == 0) return "";
	const char *cid = catalogWeaponIdByMpWeaponId((s32)mpw);
	return cid ? cid : "";
}

/* ========================================================================
 * Handicap accessors (wrap g_PlayerConfigsArray without exposing types.h)
 * ======================================================================== */

u8 matchGetPlayerHandicap(s32 playernum)
{
	if (playernum < 0 || playernum >= MAX_PLAYERS) {
		return 0x80;
	}
	return g_PlayerConfigsArray[playernum].handicap;
}

void matchSetPlayerHandicap(s32 playernum, u8 val)
{
	if (playernum >= 0 && playernum < MAX_PLAYERS) {
		g_PlayerConfigsArray[playernum].handicap = val;
	}
}

void matchResetHandicaps(void)
{
	s32 i;
	for (i = 0; i < MAX_PLAYERS; i++) {
		g_PlayerConfigsArray[i].handicap = 0x80;
	}
}

/* ========================================================================
 * Challenge start — sets the challenge, bypasses g_MatchConfig → g_MpSetup
 * copy so the challenge's own config (stage, bots, weapons) survives intact.
 * ======================================================================== */

s32 matchStartFromChallenge(s32 slot)
{
	sysLogPrintf(LOG_NOTE, "MATCHSETUP: starting challenge slot %d", slot);

	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;

	/* Apply challenge config → sets g_MpSetup (scenario, stage, bots, etc.) */
	challengeSetCurrentBySlot(slot);

	/* Sync back the challenge's stage into g_MatchConfig so our port-side
	 * tracking stays consistent (arena picker, etc.).
	 * stagenum comes from the challenge; resolve to catalog ID for stage_id. */
	g_MatchConfig.stagenum = (u8)g_MpSetup.stagenum;
	g_MatchConfig.scenario = (u8)g_MpSetup.scenario;
	/* M0.1d: sync scenario_id from integer (challenge configs set integers) */
	{
		const char *sid = catalogGameModeIdByScenarioIndex((s32)g_MpSetup.scenario);
		if (sid) {
			strncpy(g_MatchConfig.scenario_id, sid, sizeof(g_MatchConfig.scenario_id) - 1);
			g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
		} else {
			g_MatchConfig.scenario_id[0] = '\0';
		}
	}
	{
		/* Use catalog ID directly — stage_id is the primary key */
		if (g_MpSetup.stage_id[0]) {
			strncpy(g_MatchConfig.stage_id, g_MpSetup.stage_id, sizeof(g_MatchConfig.stage_id) - 1);
			g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
		} else {
			g_MatchConfig.stage_id[0] = '\0';
		}
	}

	g_NotLoadMod = false;
	romdataFileFreeForSolo();

	/* Start directly — g_MpSetup already fully configured by challengeApply() */
	mpStartMatch();
	menuStop();

	/* Phase 2 / Priority K-b3: release every pool slot — see matchStart for
	 * rationale; menupoolReleaseAll already pops the ctx so no paired pop. */
	sceneStageTransitionPrepare(SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL,
		"matchStartFromChallenge");

	sysLogPrintf(LOG_NOTE, "MATCHSETUP: challenge match started");
	return 0;
}
