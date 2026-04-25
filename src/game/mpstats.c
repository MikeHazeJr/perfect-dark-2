#include <ultra64.h>
#include "constants.h"
#include "game/cheats.h"
#include "game/inv.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/player.h"
#include "game/hudmsg.h"
#include "game/playermgr.h"
#include "game/mplayer/setup.h"
#include "game/botcmd.h"
#include "game/botinv.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/options.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "pdgui_hud.h"

/* PC: persistent stats tracking */
extern void statIncrement(const char *key, u64 amount);

/* ---- Weapon name lookup for stat keys ----
 * Maps WEAPON_* number → short catalog name (e.g., 0x01 → "falcon2").
 * Used to build keys like "kills.weapon.falcon2". */
static const struct { s16 id; const char *name; } s_WeaponStatNames[] = {
	{ 0x01, "falcon2" },        { 0x02, "falcon2_sil" },
	{ 0x03, "falcon2_scope" },  { 0x04, "magsec4" },
	{ 0x05, "mauler" },         { 0x06, "phoenix" },
	{ 0x07, "dy357" },          { 0x08, "dy357lx" },
	{ 0x09, "cmp150" },         { 0x0a, "cyclone" },
	{ 0x0b, "callisto" },       { 0x0c, "rcp120" },
	{ 0x0d, "laptopgun" },      { 0x0e, "dragon" },
	{ 0x0f, "k7avenger" },      { 0x10, "ar34" },
	{ 0x11, "superdragon" },    { 0x12, "shotgun" },
	{ 0x13, "reaper" },         { 0x14, "sniperrifle" },
	{ 0x15, "farsight" },       { 0x16, "devastator" },
	{ 0x17, "rocketlauncher" }, { 0x18, "slayer" },
	{ 0x19, "combatknife" },    { 0x1a, "crossbow" },
	{ 0x1b, "tranquilizer" },   { 0x1c, "grenade" },
	{ 0x1d, "nbomb" },          { 0x1e, "timedmine" },
	{ 0x1f, "proximitymine" },  { 0x20, "remotemine" },
	{ 0x21, "laser" },
};
#define NUM_WEAPON_STAT_NAMES (s32)(sizeof(s_WeaponStatNames) / sizeof(s_WeaponStatNames[0]))

static const char *weaponStatName(s32 weaponnum)
{
	s32 i;
	for (i = 0; i < NUM_WEAPON_STAT_NAMES; i++) {
		if (s_WeaponStatNames[i].id == weaponnum) {
			return s_WeaponStatNames[i].name;
		}
	}
	return NULL;
}

/* ---- Scenario name lookup for mode stat keys ---- */
static const char * const s_ScenarioStatNames[] = {
	"combat",          /* MPSCENARIO_COMBAT */
	"hold_briefcase",  /* MPSCENARIO_HOLDTHEBRIEFCASE */
	"hacker_central",  /* MPSCENARIO_HACKERCENTRAL */
	"pop_a_cap",       /* MPSCENARIO_POPACAP */
	"king_of_hill",    /* MPSCENARIO_KINGOFTHEHILL */
	"capture_case",    /* MPSCENARIO_CAPTURETHECASE */
};
#define NUM_SCENARIO_STAT_NAMES (s32)(sizeof(s_ScenarioStatNames) / sizeof(s_ScenarioStatNames[0]))

static void statIncrementWeapon(const char *prefix, s32 weaponnum)
{
	const char *wname = weaponStatName(weaponnum);
	if (wname) {
		char key[128];
		snprintf(key, sizeof(key), "%s.weapon.%s", prefix, wname);
		statIncrement(key, 1);
	}
}

static void statIncrementMode(const char *prefix)
{
	if (g_Vars.normmplayerisrunning && g_MpSetup.scenario >= 0
		&& g_MpSetup.scenario < NUM_SCENARIO_STAT_NAMES) {
		char key[128];
		snprintf(key, sizeof(key), "%s.mode.%s", prefix, s_ScenarioStatNames[g_MpSetup.scenario]);
		statIncrement(key, 1);
	}
}

u32 var80070590 = 0x00000000;

void mpstatsIncrementPlayerShotCount(struct gset *gset, s32 region)
{
	if (!weaponHasFlag(gset->weaponnum, WEAPONFLAG_DONTCOUNTSHOTS)) {
		g_Vars.currentplayerstats->shotcount[region]++;

		/* PC: track shots in persistent stats */
		statIncrement("shots.total", 1);
		statIncrementWeapon("shots", gset->weaponnum);
		if (region == SHOTREGION_HEAD) {
			statIncrement("shots.headshot", 1);
		}
	}
}

void mpstatsIncrementPlayerShotCount2(struct gset *gset, s32 region)
{
	if (region == 0) {
		if (!weaponHasFlag(gset->weaponnum, WEAPONFLAG_DONTCOUNTSHOTS)) {
			var80070590 = 1;
			g_Vars.currentplayerstats->shotcount[region]++;
		}
	} else {
		if (var80070590) {
			if (!weaponHasFlag(gset->weaponnum, WEAPONFLAG_DONTCOUNTSHOTS)) {
				g_Vars.currentplayerstats->shotcount[region]++;
			}

			var80070590 = 0;
		}
	}
}

void mpstats0f0b0520(void)
{
	var80070590 = 0;
}

s32 mpstatsGetPlayerShotCountByRegion(u32 type)
{
	return g_Vars.currentplayerstats->shotcount[type];
}

void mpstatsIncrementTotalKillCount(void)
{
	g_Vars.killcount++;
}

void mpstatsIncrementTotalKnockoutCount(void)
{
	g_Vars.knockoutcount++;
}

void mpstatsDecrementTotalKnockoutCount(void)
{
	g_Vars.knockoutcount--;
}

u8 mpstatsGetTotalKnockoutCount(void)
{
	return g_Vars.knockoutcount;
}

u32 mpstatsGetTotalKillCount(void)
{
	return g_Vars.killcount;
}

void mpstatsRecordPlayerKill(void)
{
	char text[256];
	s32 simulkills;
	s32 duration;
	s32 time;

	g_Vars.currentplayerstats->killcount++;
	g_Vars.currentplayer->killsthislife++;

	if (g_Vars.normmplayerisrunning) {
		time = playerGetMissionTime();

		// Show HUD message
		// "Kill count: %d"
		snprintf(text, sizeof(text), "%s: %d\n", langGet(L_GUN_001), g_Vars.currentplayerstats->killcount);
		hudmsgCreate(text, HUDMSGTYPE_DEFAULT);

		// Update slowest/fastest two kills
		if (g_Vars.currentplayerstats->killcount > 1) {
			duration = time - g_Vars.currentplayer->lastkilltime60;

			if (duration > g_Vars.currentplayerstats->slowest2kills) {
				g_Vars.currentplayerstats->slowest2kills = duration;
			}

			if (duration < g_Vars.currentplayerstats->fastest2kills) {
				g_Vars.currentplayerstats->fastest2kills = duration;
			}
		}

		// Update max simultaneous kills
		simulkills = 1;

		g_Vars.currentplayer->lastkilltime60_4 = g_Vars.currentplayer->lastkilltime60_3;
		g_Vars.currentplayer->lastkilltime60_3 = g_Vars.currentplayer->lastkilltime60_2;
		g_Vars.currentplayer->lastkilltime60_2 = g_Vars.currentplayer->lastkilltime60;
		g_Vars.currentplayer->lastkilltime60 = time;

		if (g_Vars.currentplayer->lastkilltime60_2 != -1 && g_Vars.currentplayer->lastkilltime60 - g_Vars.currentplayer->lastkilltime60_2 < 120) {
			simulkills++;

			if (g_Vars.currentplayer->lastkilltime60_3 != -1 && g_Vars.currentplayer->lastkilltime60 - g_Vars.currentplayer->lastkilltime60_3 < 120) {
				simulkills++;

				if (g_Vars.currentplayer->lastkilltime60_4 != -1 && g_Vars.currentplayer->lastkilltime60 - g_Vars.currentplayer->lastkilltime60_4 < 120) {
					simulkills++;
				}
			}
		}

		if (simulkills > g_Vars.currentplayerstats->maxsimulkills) {
			g_Vars.currentplayerstats->maxsimulkills = simulkills;
		}
	}
}

s32 mpstatsGetPlayerKillCount(void)
{
	return g_Vars.currentplayerstats->killcount;
}

void mpstatsIncrementPlayerGgKillCount(void)
{
	g_Vars.currentplayerstats->ggkillcount++;
}

void mpstatsRecordPlayerDeath(void)
{
	char buffer[256];

	g_Vars.currentplayer->deathcount++;

	if (g_Vars.normmplayerisrunning) {
		if (g_Vars.currentplayer->deathcount == 1) {
			snprintf(buffer, sizeof(buffer), "%s", langGet(L_GUN_002)); // "Died once"
		} else {
			snprintf(buffer, sizeof(buffer), "%s %d %s\n",
					langGet(L_GUN_003), // "Died"
					g_Vars.currentplayer->deathcount,
					langGet(L_GUN_004)); // "times"
		}

		hudmsgCreate(buffer, HUDMSGTYPE_DEFAULT);
	}
}

void mpstatsRecordPlayerSuicide(void)
{
	char text[256];
	s32 simulkills;
	s32 duration;
	s32 time;
	s32 mpindex;
	struct mpchrconfig *mpchr;

	if (g_Vars.normmplayerisrunning) {
		time = playerGetMissionTime();
		mpindex = g_Vars.currentplayerstats->mpindex;

		mpchr = MPCHR(mpindex);

		// Show HUD message
		// "Suicide count: %d"
		snprintf(text, sizeof(text), "%s: %d\n", langGet(L_GUN_005), mpchr->killcounts[mpindex]);
		hudmsgCreate(text, HUDMSGTYPE_DEFAULT);

		// Update slowest/fastest two kills
		if (g_Vars.currentplayerstats->killcount > 1) {
			duration = time - g_Vars.currentplayer->lastkilltime60;

			if (duration > g_Vars.currentplayerstats->slowest2kills) {
				g_Vars.currentplayerstats->slowest2kills = duration;
			}

			if (duration < g_Vars.currentplayerstats->fastest2kills) {
				g_Vars.currentplayerstats->fastest2kills = duration;
			}
		}

		// Update max simultaneous kills
		simulkills = 1;

		g_Vars.currentplayer->lastkilltime60_4 = g_Vars.currentplayer->lastkilltime60_3;
		g_Vars.currentplayer->lastkilltime60_3 = g_Vars.currentplayer->lastkilltime60_2;
		g_Vars.currentplayer->lastkilltime60_2 = g_Vars.currentplayer->lastkilltime60;
		g_Vars.currentplayer->lastkilltime60 = time;

		if (g_Vars.currentplayer->lastkilltime60_2 != -1 && g_Vars.currentplayer->lastkilltime60 - g_Vars.currentplayer->lastkilltime60_2 < 120) {
			simulkills++;

			if (g_Vars.currentplayer->lastkilltime60_3 != -1 && g_Vars.currentplayer->lastkilltime60 - g_Vars.currentplayer->lastkilltime60_3 < 120) {
				simulkills++;

				if (g_Vars.currentplayer->lastkilltime60_4 != -1 && g_Vars.currentplayer->lastkilltime60 - g_Vars.currentplayer->lastkilltime60_4 < 120) {
					simulkills++;
				}
			}
		}

		if (simulkills > g_Vars.currentplayerstats->maxsimulkills) {
			g_Vars.currentplayerstats->maxsimulkills = simulkills;
		}
	}
}

void mpstatsRecordDeath(s32 aplayernum, s32 vplayernum)
{
	s32 vmpindex = -1;
	struct mpchrconfig *vmpchr = NULL;
	s32 ampindex;
	struct mpchrconfig *ampchr = NULL;
	s32 prevplayernum;
	char text[256];

	if (g_Vars.normmplayerisrunning && g_MpSetup.scenario == MPSCENARIO_POPACAP) {
		pacHandleDeath(aplayernum, vplayernum);
	}

	// Find attacker and victim mpchrs
	if (aplayernum >= 0) {
		ampindex = func0f18d074(aplayernum);

		if (ampindex >= 0) {
			ampchr = MPCHR(ampindex);
		}
	}

	if (vplayernum >= 0) {
		vmpindex = func0f18d074(vplayernum);

		if (vmpindex >= 0) {
			vmpchr = MPCHR(vmpindex);
		}
	}

	if (vplayernum >= 0 && aplayernum == vplayernum) {
		// Player suicide
		if (vmpchr && vmpindex >= 0) {
			vmpchr->numdeaths++;
			vmpchr->killcounts[vmpindex]++;
		}

		/* PC: ImGui killfeed — suicide. Fire for any MP mode (normal,
		 * co-op, counter-op). B-175: previously gated on
		 * `normmplayerisrunning` which suppressed the killfeed in co-op
		 * modes; `mplayerisrunning` covers all MP scenarios. */
		if (g_Vars.mplayerisrunning && vmpchr) {
			pdguiKillfeedPush(NULL, 0, vmpchr->name, vmpchr->team, 1);
			sysLogPrintf(LOG_NOTE,
				"KILLFEED: suicide vplayernum=%d v='%s'",
				vplayernum, vmpchr->name);
		}
		{
			extern s32 g_NetMode;
			extern void netDistribSendKillFeed(const char *, const char *, const char *, u8);
			if (g_NetMode == NETMODE_SERVER && vmpchr) {
				netDistribSendKillFeed("", vmpchr->name, "", 0);
			}
		}

		/* PC: track suicide in persistent stats */
		if (vplayernum < PLAYERCOUNT()) {
			statIncrement("deaths.total", 1);
			statIncrement("deaths.suicide", 1);
			statIncrementMode("deaths");
		}

		if (vplayernum < PLAYERCOUNT()) {
			prevplayernum = g_Vars.currentplayernum;
			setCurrentPlayerNum(vplayernum);
			mpstatsRecordPlayerSuicide();
			setCurrentPlayerNum(prevplayernum);
		}
	} else {
		// Normal kill
		if (vplayernum >= 0) {
			if (vmpchr) {
				vmpchr->numdeaths++;
			}

			if (vplayernum < PLAYERCOUNT()) {
				// Victim was a player
				prevplayernum = g_Vars.currentplayernum;
				setCurrentPlayerNum(vplayernum);

				if (g_Vars.normmplayerisrunning && aplayernum >= 0) {
					// "Killed by %s"
					snprintf(text, sizeof(text), "%s %s", langGet(L_MISC_183), g_MpAllChrConfigPtrs[aplayernum]->name);
					hudmsgCreate(text, HUDMSGTYPE_DEFAULT);
				}

				/* PC: track death in persistent stats */
				statIncrement("deaths.total", 1);
				statIncrementMode("deaths");
				if (aplayernum >= PLAYERCOUNT()) {
					statIncrement("deaths.by_bot", 1);
				} else {
					statIncrement("deaths.by_player", 1);
				}

				mpstatsRecordPlayerDeath();
				setCurrentPlayerNum(prevplayernum);
			}
		}

		if (ampchr && vmpindex >= 0) {
			ampchr->killcounts[vmpindex]++;
		}

		/* PC: ImGui killfeed — normal kill. B-175: accept bot-on-bot
		 * equally with human kills. Gate on `mplayerisrunning` (any MP
		 * mode, incl co-op / counter-op) and require only `vmpchr`;
		 * if attacker mpchrconfig lookup fails, pass NULL attacker so
		 * the killfeed still emits with a "?" attacker pill rather than
		 * being silently dropped. */
		if (g_Vars.mplayerisrunning && vmpchr) {
			const char *aname = (ampchr && ampchr->name[0]) ? ampchr->name : NULL;
			u8 ateam = ampchr ? (u8)ampchr->team : (u8)0;
			pdguiKillfeedPush(aname, ateam,
			                  vmpchr->name, (u8)vmpchr->team,
			                  aname ? 0 : 1);
			sysLogPrintf(LOG_NOTE,
				"KILLFEED: push aplayernum=%d vplayernum=%d a='%s' v='%s' ampchr=%p",
				aplayernum, vplayernum,
				aname ? aname : "(null)", vmpchr->name, (void *)ampchr);
		} else if (g_Vars.mplayerisrunning) {
			sysLogPrintf(LOG_WARNING,
				"KILLFEED: skipped — mplay=%d aplayernum=%d vplayernum=%d ampchr=%p vmpchr=%p",
				g_Vars.mplayerisrunning, aplayernum, vplayernum,
				(void *)ampchr, (void *)vmpchr);
		}
		{
			extern s32 g_NetMode;
			extern void netDistribSendKillFeed(const char *, const char *, const char *, u8);
			if (g_NetMode == NETMODE_SERVER && vmpchr) {
				netDistribSendKillFeed(ampchr ? ampchr->name : "",
				                       vmpchr->name, "", 0);
			}
		}

		if (aplayernum >= 0 && aplayernum < PLAYERCOUNT()) {
			/* B-249 (2026-04-25) diagnostic instrumentation -- bot-vs-bot
			 * kills attributed to player 0.
			 *
			 * Mike's report: "during one of my earlier test matches, bot
			 * deaths were counting as kills for me, despite me never even
			 * having use of a weapon or fists."  The scorecard read for
			 * player 0 incremented when player 0 didn't fire.
			 *
			 * The static-altitude analysis ruled out the obvious paths
			 * (mpPlayerGetIndex returns -1 for not-found; func0f18d074
			 * returns -1 for not-found; suicide path is gated on
			 * aplayernum == vplayernum).  The remaining hypothesis is
			 * that `aplayernum` is being resolved as 0 for an attacker
			 * whose chr is actually a BOT -- this would mean either:
			 *   (a) g_MpAllChrPtrs[0] holds a bot chr instead of player 0
			 *       (slot-assignment collision at match setup), or
			 *   (b) a separate aplayernum=0 default-fallthrough exists
			 *       that we haven't located in the static read.
			 *
			 * This LOG_WARNING fires the moment the attribution credits
			 * player 0 BUT the resolved attacker chr is a bot (aibot != NULL).
			 * Catches the symptom in logs so the next playtest pinpoints
			 * which call site set aplayernum=0.  Also dumps the relevant
			 * slot pointers so a follow-up audit can verify slot integrity.
			 *
			 * No behaviour change otherwise -- diagnostic only. */
			{
				struct chrdata *att_chr = (aplayernum >= 0 && aplayernum < g_MpNumChrs)
				                          ? g_MpAllChrPtrs[aplayernum] : NULL;
				if (att_chr && att_chr->aibot != NULL) {
					sysLogPrintf(LOG_WARNING,
						"B-249.DIAG: kill credited to player slot %d but attacker "
						"chr at that slot is a BOT (aibot != NULL).  ampchr='%s' "
						"vplayernum=%d att_chr=%p plr0_chr=%p numchrs=%d",
						aplayernum,
						ampchr ? ampchr->name : "(null)",
						vplayernum,
						(void *)att_chr,
						g_Vars.players[0] ? (void *)g_Vars.players[0]->prop->chr : NULL,
						g_MpNumChrs);
				}
			}

			// Attacker was a player -- record kill in persistent stats
			statIncrement("kills.total", 1);
			statIncrementMode("kills");
			if (vplayernum >= PLAYERCOUNT()) {
				statIncrement("kills.vs_bot", 1);
			} else {
				statIncrement("kills.vs_player", 1);
			}

			prevplayernum = g_Vars.currentplayernum;
			setCurrentPlayerNum(aplayernum);

			/* Weapon-specific kill tracking (currentplayer set by setCurrentPlayerNum above) */
			statIncrementWeapon("kills", g_Vars.currentplayer->gunctrl.weaponnum);

			if (g_Vars.normmplayerisrunning && vplayernum >= 0) {
				// "Killed %s"
				snprintf(text, sizeof(text), "%s %s", langGet(L_MISC_184), g_MpAllChrConfigPtrs[vplayernum]->name);
				hudmsgCreate(text, HUDMSGTYPE_DEFAULT);
			}

			mpstatsRecordPlayerKill();
			setCurrentPlayerNum(prevplayernum);
		}

		// If someone killed an aibot
		if (g_Vars.normmplayerisrunning
				&& aplayernum >= 0
				&& vplayernum >= PLAYERCOUNT()
				&& aplayernum != vplayernum) {
			g_MpAllChrPtrs[vplayernum]->aibot->lastkilledbyplayernum = aplayernum;
		}
	}

	if (g_Vars.normmplayerisrunning && aplayernum >= 0 && g_MpAllChrPtrs[aplayernum]->aibot) {
		s32 index = mpGetWeaponSlotByWeaponNum(g_MpAllChrPtrs[aplayernum]->aibot->weaponnum);

		if (index >= 0) {
			if (aplayernum == vplayernum) {
				g_MpAllChrPtrs[aplayernum]->aibot->suicidesbygunfunc[index][g_MpAllChrPtrs[aplayernum]->aibot->gunfunc]++;
			} else {
				g_MpAllChrPtrs[aplayernum]->aibot->killsbygunfunc[index][g_MpAllChrPtrs[aplayernum]->aibot->gunfunc]++;
			}
		}
	}

	g_Vars.totalkills++;

	/* PC: event-driven score sync — flag a score resync to all clients.
	 * NET_RESYNC_FLAG_SCORES is consumed in netEndFrame() which sends
	 * SVC_PLAYER_SCORES to all connected clients.  Without this, scores
	 * only sync on reconnect/demand, not per-kill.
	 * Only set when hosting — in solo g_NetMode==0 and the flag is never consumed. */
	{
		extern u32 g_NetPendingResyncFlags;
		extern s32 g_NetMode;
		if (g_NetMode == NETMODE_SERVER) {
			g_NetPendingResyncFlags |= (1 << 2);  /* NET_RESYNC_FLAG_SCORES */
		}
	}
}
