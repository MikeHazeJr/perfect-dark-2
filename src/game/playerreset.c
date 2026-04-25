#include <ultra64.h>
#include "constants.h"
#include "constants.h"
#include "game/bondmove.h"
#include "game/cheats.h"
#include "game/inv.h"
#include "game/playerreset.h"
#include "game/chr.h"
#include "game/body.h"
#include "game/prop.h"
#include "game/setuputils.h"
#include "game/bondgun.h"
#include "game/player.h"
#include "game/inv.h"
#include "game/stagetable.h"
#include "game/mplayer/scenarios.h"
#include "game/mplayer/mplayer.h"
#include "game/pad.h"
#include "game/atan2f.h"
#include "bss.h"
#include "lib/collision.h"
#include "lib/memp.h"
#include "lib/mtx.h"
#include "lib/anim.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "net/net.h"
#include "lib/rng.h"
#include "navspawn.h"
#include "game/spawnpool.h"

void playerInitEyespy(void)
{
	struct prop *prop;
	struct pad pad;
	struct chrdata *propchr;
	struct chrdata *playerchr;
	static u8 nextpad = 0;

	if (g_Vars.currentplayer->eyespy == NULL) {
		/**
		 * To create the eyespy's prop, a pad must be passed to bodyAllocateEyespy.
		 * However the eyespy doesn't have a pad because it's held by the
		 * player, so it needs to choose one from the stage. The method used
		 * will increment the chosen pad number each time the stage is loaded
		 * and wrap at 256.
		 *
		 * @bug: This method means if you play G5 Building enough times then
		 * the camspy will start in a trigger point for the mid cutscene,
		 * causing the mid cutscene to play instead of the intro.
		 */
		padUnpack(nextpad++, PADFIELD_ROOM | PADFIELD_POS, &pad);
		prop = bodyAllocateEyespy(&pad, pad.room);

		if (prop) {
			g_Vars.currentplayer->eyespy = mempAlloc(sizeof(struct eyespy), MEMPOOL_STAGE);

			if (g_Vars.currentplayer->eyespy) {
				g_Vars.currentplayer->eyespy->prop = prop;
				g_Vars.currentplayer->eyespy->look.x = 0;
				g_Vars.currentplayer->eyespy->look.y = 0;
				g_Vars.currentplayer->eyespy->look.z = 1;
				g_Vars.currentplayer->eyespy->up.x = 0;
				g_Vars.currentplayer->eyespy->up.y = 1;
				g_Vars.currentplayer->eyespy->up.z = 0;
				g_Vars.currentplayer->eyespy->theta = 0;
				g_Vars.currentplayer->eyespy->costheta = 1;
				g_Vars.currentplayer->eyespy->sintheta = 0;
				g_Vars.currentplayer->eyespy->verta = 0;
				g_Vars.currentplayer->eyespy->cosverta = 1;
				g_Vars.currentplayer->eyespy->sinverta = 0;
				g_Vars.currentplayer->eyespy->held = true;
				g_Vars.currentplayer->eyespy->deployed = false;
				g_Vars.currentplayer->eyespy->active = false;
				g_Vars.currentplayer->eyespy->buttonheld = false;
				g_Vars.currentplayer->eyespy->camerabuttonheld = false;
				g_Vars.currentplayer->eyespy->bobdir = 1;
				g_Vars.currentplayer->eyespy->bobtimer = 0;
				g_Vars.currentplayer->eyespy->bobactive = true;
				g_Vars.currentplayer->eyespy->vel.x = 0;
				g_Vars.currentplayer->eyespy->vel.y = 0;
				g_Vars.currentplayer->eyespy->vel.z = 0;
				g_Vars.currentplayer->eyespy->speed = 0;
				g_Vars.currentplayer->eyespy->oldground = 0;
				g_Vars.currentplayer->eyespy->height = 0;
				g_Vars.currentplayer->eyespy->gravity = 0;
				g_Vars.currentplayer->eyespy->hit = EYESPYHIT_NONE;
				g_Vars.currentplayer->eyespy->opendoor = false;
				g_Vars.currentplayer->eyespy->mode = EYESPYMODE_CAMSPY;
				propchr = prop->chr;
				playerchr = g_Vars.currentplayer->prop->chr;
				propchr->team = playerchr->team;

				if (stageGetIndex(g_Vars.stagenum) == STAGEINDEX_AIRBASE) {
					g_Vars.currentplayer->eyespy->mode = EYESPYMODE_DRUGSPY;
					g_Weapons[WEAPON_EYESPY]->name = L_GUN_061; // "DrugSpy"
					g_Weapons[WEAPON_EYESPY]->shortname = L_GUN_061; // "DrugSpy"
				} else if (stageGetIndex(g_Vars.stagenum) == STAGEINDEX_MBR || stageGetIndex(g_Vars.stagenum) == STAGEINDEX_CHICAGO) {
					g_Vars.currentplayer->eyespy->mode = EYESPYMODE_BOMBSPY;
				} else {
					g_Vars.currentplayer->eyespy->mode = EYESPYMODE_CAMSPY;
				}
			}
		}
	}
}

struct cmd32 {
	s32 type;
	s32 param1;
	s32 param2;
	s32 param3;
};

void playerReset(void)
{
	struct coord pos = {0, 0, 0};
	RoomNum rooms[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	f32 turnanglerad = 0;
	f32 groundy;
	bool hasdefaultweapon = false;
	struct cmd32 *cmd = (struct cmd32 *)g_StageSetup.intro;
	u8 haseyespy = false;
	s32 stack[7];
	s32 i;
	s32 numchrs;
	struct gecreditsdata *thing;
	struct chrdata *chr;
	s32 bodynum;
	s32 headnum;

	func0f18e558();

	g_InCutscene = false;

#if PAL
	var8009e388pf = 0;
#else
	g_CutsceneFrameOverrun240 = 0;
#endif

	var8007072c = 1;
	var80070738 = 0;
	var8007073c = 0;

	g_CurrentGeCreditsData = NULL;
	g_Vars.currentplayer->bondexploding = false;
	g_NumSpawnPoints = 0;
	g_Vars.currentplayer->bondtankexplode = false;
	g_Vars.currentplayer->gunmem2 = NULL;
	g_PlayersWithControl[0] = true;
	g_PlayersWithControl[1] = true;
	g_PlayersWithControl[2] = true;
	g_PlayersWithControl[3] = true;
#if MAX_PLAYERS > 4
	g_PlayersWithControl[4] = true;
	g_PlayersWithControl[5] = true;
	g_PlayersWithControl[6] = true;
	g_PlayersWithControl[7] = true;
#endif
	g_PlayerInvincible = false;

	playerSetTickMode(TICKMODE_GE_FADEIN);

	g_PlayerTriggerGeFadeIn = 0;
	var80070748 = 0;
	var8007074c = 0;

	g_Vars.currentplayer->bondviewlevtime60 = 0;
	g_Vars.currentplayer->bondwatchtime60 = 0;
	g_Vars.currentplayer->introanimnum = 0;

	g_DefaultWeapons[HAND_LEFT] = 0;
	g_DefaultWeapons[HAND_RIGHT] = 0;

	/* S303: log player-reset entry so playtest can attribute weapon loadout
	 * + spawn decisions to a specific player in a specific mode. */
	{
		const char *mode =
			g_Vars.coopplayernum >= 0 ? "COOP" :
			g_Vars.antiplayernum >= 0 ? "COUNTEROP" :
			g_Vars.normmplayerisrunning ? "MP_COMBATSIM" : "CAMPAIGN";
		const char *role =
			(g_Vars.antiplayernum >= 0 && g_Vars.currentplayer == g_Vars.anti) ? "anti" :
			(g_Vars.coopplayernum >= 0 && g_Vars.currentplayer == g_Vars.coop) ? "coop" :
			"bond";
		sysLogPrintf(LOG_NOTE,
			"GAMELOOP.%s: playerReset begin playernum=%d role=%s stage=0x%02x diff=%d intro=%p",
			mode, g_Vars.currentplayernum, role,
			g_Vars.stagenum, g_MissionConfig.difficulty, (void *)cmd);
	}

	sysLogPrintf(LOG_NOTE, "LOAD: playerReset intro cmd=%p", (void *)cmd);
	if (cmd) {
		s32 introSafety = 0;
		while (cmd->type != INTROCMD_END) {
			if (++introSafety > 10000) {
				sysLogPrintf(LOG_WARNING, "LOAD: playerReset intro loop exceeded 10000 iterations — breaking (cmd type=%d at %p)", cmd->type, (void *)cmd);
				break;
			}
			switch (cmd->type) {
			case INTROCMD_SPAWN:
				if (cmd->param2 == 0 && g_NumSpawnPoints < MAX_MPCHRS) {
					g_SpawnPoints[g_NumSpawnPoints++] = cmd->param1;
				}
				cmd = (struct cmd32 *)((uintptr_t)cmd + 12);
				break;
			case INTROCMD_CASE:
			case INTROCMD_CASERESPAWN:
				cmd = (struct cmd32 *)((uintptr_t)cmd + 12);
				break;
			case INTROCMD_HILL:
				cmd = (struct cmd32 *)((uintptr_t)cmd + 8);
				break;
			case INTROCMD_WEAPON:
				/* B-219: MP spawn-with-weapon fallback must own loadout.
				 * Init order (lv.c ~597-628): loop 1 calls playerReset() which runs
				 * INTROCMD_WEAPON (inventory + default weapon); loop 2 calls
				 * playerSpawn() which applies spawn-with-weapon and bgunEquipWeapon2
				 * (queues switch via switchtoweaponnum; bgunTickSwitch2 runs later in
				 * the main loop). Both paths then apply — two weapons, conflicting
				 * equip state vs HUD. Skip intro weapons in norm MP when the spawn
				 * fallback option is active. */
				if (g_Vars.normmplayerisrunning
						&& (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)) {
					sysLogPrintf(LOG_NOTE,
						"GAMELOOP.WEAPON: playernum=%d mission=0x%02x INTRO skipped (spawn-with-weapon owns MP loadout)",
						g_Vars.currentplayernum, g_Vars.stagenum);
					cmd = (struct cmd32 *)((uintptr_t)cmd + 16);
					break;
				}
				if (cmd->param3 == 0 && PLAYER_IS_NOT_ANTI(g_Vars.currentplayer)) {

					modelmgrLoadProjectileModeldefs(cmd->param1);

					if (cmd->param2 >= 0) {
						modelmgrLoadProjectileModeldefs(cmd->param2);
						invGiveDoubleWeapon(cmd->param1, cmd->param2);
					} else {
						invGiveSingleWeapon(cmd->param1);
					}

					if (!hasdefaultweapon) {
						g_DefaultWeapons[HAND_RIGHT] = cmd->param1;

						if (cmd->param2 >= 0) {
							g_DefaultWeapons[HAND_LEFT] = cmd->param2;
						}

						hasdefaultweapon = true;
					}

					if (cmd->param1 == WEAPON_EYESPY) {
						haseyespy = true;
					}

					/* S303 weapon audit: log every INTROCMD_WEAPON granted to
					 * the player so playtest can reconstruct the exact loadout
					 * a mission intended vs. what the inventory actually holds. */
					sysLogPrintf(LOG_NOTE,
						"GAMELOOP.WEAPON: playernum=%d mission=0x%02x INTRO gave R=%d L=%d default=%d%s",
						g_Vars.currentplayernum, g_Vars.stagenum,
						(s32)cmd->param1, (s32)cmd->param2,
						hasdefaultweapon ? 1 : 0,
						cmd->param1 == WEAPON_EYESPY ? " +eyespy" : "");
				} else if (cmd->param3 == 0 && !PLAYER_IS_NOT_ANTI(g_Vars.currentplayer)) {
					/* Counter-Op: anti-player skips INTROCMD_WEAPON entirely
					 * and gets its weapons from chrPossess() at possession
					 * time (player.c:1262-1303 copies possessed chr's
					 * weapons_held to inventory). Log the skip so playtest
					 * can correlate empty-hands-on-start with "waiting for
					 * first possession" rather than a loadout bug. */
					sysLogPrintf(LOG_NOTE,
						"GAMELOOP.WEAPON: playernum=%d mission=0x%02x INTRO skipped for anti role (weapons come from possession)",
						g_Vars.currentplayernum, g_Vars.stagenum);
				}
				cmd = (struct cmd32 *)((uintptr_t)cmd + 16);
				break;
			case INTROCMD_AMMO:
				if (cmd->param3 == 0 && PLAYER_IS_NOT_ANTI(g_Vars.currentplayer)) {
					bgunSetAmmoQuantity(cmd->param1, cmd->param2);
				}
				cmd = (struct cmd32 *)((uintptr_t)cmd + 16);
				break;
			case INTROCMD_3:
				cmd = (struct cmd32 *)((uintptr_t)cmd + 32);
				break;
			case INTROCMD_4:
				cmd = (struct cmd32 *)((uintptr_t)cmd + 8);
				break;
			case INTROCMD_OUTFIT:
				g_Vars.currentplayer->bondtype = cmd->param1;
				cmd = (struct cmd32 *)((uintptr_t)cmd + 8);
				break;
			case INTROCMD_6:
				cmd = (struct cmd32 *)((uintptr_t)cmd + 40);
				break;
			case INTROCMD_WATCHTIME:
				g_Vars.currentplayer->bondwatchtime60 = 0;
				if (cmd->param2 > 0) {
					g_Vars.currentplayer->bondwatchtime60 += (cmd->param2 % 60) * 3600;
				}
				if (cmd->param1 > 0) {
					g_Vars.currentplayer->bondwatchtime60 += (cmd->param1 % 12) * 3600 * 60;
				}
				cmd = (struct cmd32 *)((uintptr_t)cmd + 12);
				break;
			case INTROCMD_CREDITOFFSET:
				thing = (struct gecreditsdata *)((uintptr_t)g_GeCreditsData + cmd->param1);
				g_CurrentGeCreditsData = thing;
				{
					s32 creditSafety = 0;
					while ((thing->text1 || thing->text2) && ++creditSafety < 1000) {
						thing++;
					}
				}
				cmd = (struct cmd32 *)((uintptr_t)cmd + 8);
				break;
			default:
				cmd = (struct cmd32 *)((uintptr_t)cmd + 4);
			}
		}
	}

	/* PC: If no spawn points were set by INTROCMD_SPAWN (e.g. SP maps used as
	 * MP arenas or mod stages without intro data), populate g_SpawnPoints so
	 * the dispersal algorithm has something to work with.
	 * Preference order:
	 *   1. Waypoints (navmesh nodes) with rejection-sampling for spread — these
	 *      represent walkable interior positions designed for AI navigation.
	 *   2. Sequential pad scan fallback (B-19) for maps with no waypoints.
	 * Covers networked matches (g_NetMode) AND local Combat Sim. Solo missions
	 * define their own player placement and must not be overridden here. */
	if (g_NumSpawnPoints == 0 && (g_NetMode != NETMODE_NONE || g_Vars.normmplayerisrunning)) {
		s32 added = 0;

		/* Attempt 1: waypoint-based selection with adaptive spacing.
		 * Start with 500-unit spacing, halve it progressively if not enough
		 * spawn points are found. Solo mission maps are often compact. */
		if (g_StageSetup.waypoints) {
			struct waypoint *wpts = g_StageSetup.waypoints;
			s32 numwpts = 0;
			struct coord chosen_pos[24];

			while (wpts[numwpts].padnum >= 0) {
				numwpts++;
			}

			/* Try with decreasing spacing: 500 → 250 → 125 → 60 → 0
			 * AIDROP pads are valid spawn locations — the flag only
			 * affects AI pathfinding (drop-off-ledge behavior), not
			 * spawn suitability.  Filtering them caused Chicago and
			 * other multi-level maps to collapse all spawns to pad 0. */
			f32 spacings[] = { 500.0f, 250.0f, 125.0f, 60.0f, 0.0f };
			for (s32 pass = 0; pass < 5 && added < 8; pass++) {
				f32 minDist = spacings[pass];
				f32 minDistSq = minDist * minDist;

				/* Reset for this pass */
				added = 0;
				g_NumSpawnPoints = 0;

				for (s32 outer = 0; outer < numwpts * 4 && added < 24; outer++) {
					s32 idx = rngRandom() % numwpts;
					struct pad probePad;

					padUnpack(wpts[idx].padnum, PADFIELD_POS | PADFIELD_ROOM | PADFIELD_FLAGS, &probePad);

					if (probePad.room < 0) continue;

					s32 too_close = 0;
					if (minDistSq > 0.0f) {
						for (s32 j = 0; j < added; j++) {
							f32 dx = probePad.pos.x - chosen_pos[j].x;
							f32 dz = probePad.pos.z - chosen_pos[j].z;
							if (dx * dx + dz * dz < minDistSq) {
								too_close = 1;
								break;
							}
						}
					}

					if (!too_close) {
						chosen_pos[added] = probePad.pos;
						g_SpawnPoints[g_NumSpawnPoints++] = (s16)wpts[idx].padnum;
						added++;
					}
				}

				if (added >= 8) break; /* enough for reasonable dispersal */
			}

			/* Safety: if all spawn pads collapsed to the same padnum,
			 * the random selection failed.  Re-populate sequentially
			 * from the waypoint array to guarantee diversity. */
			if (added > 1) {
				s32 allSame = 1;
				for (s32 v = 1; v < g_NumSpawnPoints; v++) {
					if (g_SpawnPoints[v] != g_SpawnPoints[0]) {
						allSame = 0;
						break;
					}
				}
				if (allSame) {
					sysLogPrintf(LOG_WARNING, "SPAWN: all %d pads collapsed to padnum=%d — sequential fallback",
						g_NumSpawnPoints, (s32)g_SpawnPoints[0]);
					g_NumSpawnPoints = 0;
					added = 0;
					for (s32 si = 0; si < numwpts && added < 24; si++) {
						struct pad p;
						padUnpack(wpts[si].padnum, PADFIELD_POS | PADFIELD_ROOM, &p);
						if (p.room >= 0) {
							chosen_pos[added] = p.pos;
							g_SpawnPoints[g_NumSpawnPoints++] = (s16)wpts[si].padnum;
							added++;
						}
					}
				}
			}

			if (added > 0) {
				sysLogPrintf(LOG_NOTE, "SPAWN: populated %d spawn points from %d navmesh waypoints (adaptive spacing)", added, numwpts);

				/* ONE-TIME diagnostic: dump first+last pad positions */
				for (s32 di = 0; di < g_NumSpawnPoints && di < 6; di++) {
					struct pad diagPad;
					padUnpack(g_SpawnPoints[di], PADFIELD_POS | PADFIELD_ROOM | PADFIELD_FLAGS, &diagPad);
					sysLogPrintf(LOG_NOTE, "SPAWN-DIAG: pad[%d] padnum=%d pos=(%.0f,%.0f,%.0f) room=%d flags=0x%x",
						di, (s32)g_SpawnPoints[di],
						diagPad.pos.x, diagPad.pos.y, diagPad.pos.z,
						(s32)diagPad.room, diagPad.flags);
				}
			}
		}

		/* Attempt 2: sequential pad scan if waypoints gave nothing */
		if (added == 0 && g_PadsFile != NULL) {
			s32 maxpads = g_PadsFile->numpads;
			s32 pi;

			for (pi = 0; pi < maxpads && added < 24; pi++) {
				struct pad probePad;
				padUnpack(pi, PADFIELD_ROOM, &probePad);
				if (probePad.room >= 0) {
					g_SpawnPoints[g_NumSpawnPoints++] = (s16)pi;
					added++;
				}
			}

			if (added > 0) {
				sysLogPrintf(LOG_NOTE, "SPAWN: populated %d spawn points from pad file (B-19 fallback)", added);
			} else {
				sysLogPrintf(LOG_WARNING, "SPAWN: all fallbacks found 0 valid spawn positions (netmode=%d normmplay=%d)",
					g_NetMode, g_Vars.normmplayerisrunning);
			}
		}
	}

	sysLogPrintf(LOG_NOTE, "SPAWN: summary stage=0x%02x npts=%d mplay=%d normmplay=%d netmode=%d intro=%s pads=%s",
		g_Vars.stagenum, g_NumSpawnPoints,
		g_Vars.mplayerisrunning, g_Vars.normmplayerisrunning,
		g_NetMode,
		cmd ? "ok" : "null",
		g_PadsFile ? "ok" : "null");

	/* S303: co-op telefrag audit — co-op missions whose intro data declares
	 * fewer spawn pads than active humans will land both players on the
	 * same pad (scenarioChooseSpawnLocation → playerChooseGeneralSpawnLocation
	 * → playerChooseSpawnLocation). The pad shortlist only dedupes on enemy
	 * teams; co-op teammates share TEAM_ALLY so they're not filtered apart.
	 * playerTrySelectPoolSpawn is gated on g_Vars.mplayerisrunning so the
	 * spawn pool doesn't save us in co-op. Log a warning so playtest can
	 * identify the problem stages; a real fix would need coop-aware pad
	 * scoring or an additive pad-file fallback here. */
	if ((g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0)
			&& g_NumSpawnPoints < 2) {
		sysLogPrintf(LOG_WARNING,
			"GAMELOOP.COOP: only %d spawn pad(s) declared for %s stage 0x%02x — P1/P2 telefrag risk (mode=%s)",
			g_NumSpawnPoints,
			g_Vars.coopplayernum >= 0 ? "coop" : "counter-op",
			g_Vars.stagenum,
			g_Vars.coopplayernum >= 0 ? "coop" : "anti");
	}

	/* L2 spawn pool: build validated pool from L1-L4 hierarchy.
	 * This runs after INTROCMD_SPAWN + waypoint/pad fallbacks have populated
	 * g_SpawnPoints[]. The pool guarantees enough spawn points for all
	 * participants even on maps with zero declared spawns.
	 * match_seed: use g_NetMatchSeed (distributed via SVC_STAGE_START) for
	 * networked matches. For offline/solo, derive from stagenum. */
	if (g_Vars.mplayerisrunning || g_NetMode != NETMODE_NONE) {
		/* S302: size the pool for the worst-case participant count, not
		 * just the local humans.  `PLAYERCOUNT()` is driven by
		 * `g_Vars.players[]`, which in netplay is populated AFTER the
		 * remote-join handshake — so during stage load it can be 1 even
		 * when 8 clients are joining.  Use MAX_PLAYERS as the ceiling
		 * for any netplay build and add a respawn headroom for local
		 * Combat Sim. */
		s32 humans = (g_NetMode != NETMODE_NONE)
			? MAX_PLAYERS
			: PLAYERCOUNT();
		s32 spawn_needed = humans + g_BotCount + 4; /* +4 respawn buffer */
		u32 match_seed = (g_NetMode != NETMODE_NONE && g_NetMatchSeed != 0)
			? g_NetMatchSeed
			: (u32)g_Vars.stagenum ^ (u32)g_Vars.lvframe60;
		spawn_aabb_t aabb;
		f32 span_x;
		f32 span_z;
		f32 dominant_span;
		s32 span_bonus = 0;

		/* Floor: always build at least 16 slots so respawn cycling has
		 * breathing room on tiny maps with only 1-2 humans. */
		if (spawn_needed < 16) spawn_needed = 16;

		/* Large maps need a deeper candidate pool to avoid repeated spawn reuse. */
		spawnPoolComputeAABB(&aabb);
		if (aabb.valid) {
			span_x = aabb.max.x - aabb.min.x;
			span_z = aabb.max.z - aabb.min.z;
			dominant_span = span_x > span_z ? span_x : span_z;
			if (dominant_span > 4000.0f) {
				span_bonus = (s32)((dominant_span - 4000.0f) / 2000.0f) + 1;
			}
		}

		spawn_needed += span_bonus;
		if (spawn_needed > MAX_MPCHRS) {
			spawn_needed = MAX_MPCHRS;
		}

		spawnPoolBuildGlobal(g_MissionConfig.stage_id, match_seed, spawn_needed);
	} else {
		spawnPoolReset();
	}

	invGiveSingleWeapon(WEAPON_UNARMED);

	if (cheatIsActive(CHEAT_TRENTSMAGNUM)) {
		invGiveSingleWeapon(WEAPON_DY357LX);
		bgunSetAmmoQuantity(AMMOTYPE_MAGNUM, 80);
	}

	if (cheatIsActive(CHEAT_FARSIGHT)) {
		invGiveSingleWeapon(WEAPON_FARSIGHT);
		bgunSetAmmoQuantity(AMMOTYPE_FARSIGHT, 80);
	}

	if (cheatIsActive(CHEAT_CLOAKINGDEVICE)) {
		invGiveSingleWeapon(WEAPON_CLOAKINGDEVICE);
#if VERSION >= VERSION_PAL_FINAL
		bgunSetAmmoQuantity(AMMOTYPE_CLOAK, TICKS(7200));
#else
		bgunSetAmmoQuantity(AMMOTYPE_CLOAK, 7200);
#endif
	}

	if (cheatIsActive(CHEAT_PERFECTDARKNESS)) {
		invGiveSingleWeapon(WEAPON_NIGHTVISION);
	}

	if (cheatIsActive(CHEAT_RTRACKER)) {
		invGiveSingleWeapon(WEAPON_RTRACKER);
	}

	if (cheatIsActive(CHEAT_ROCKETLAUNCHER)) {
		invGiveSingleWeapon(WEAPON_ROCKETLAUNCHER);
		bgunSetAmmoQuantity(AMMOTYPE_ROCKET, 10);
	}

	if (cheatIsActive(CHEAT_SNIPERRIFLE)) {
		invGiveSingleWeapon(WEAPON_SNIPERRIFLE);
		bgunSetAmmoQuantity(AMMOTYPE_RIFLE, 200);
	}

	if (cheatIsActive(CHEAT_XRAYSCANNER)) {
		invGiveSingleWeapon(WEAPON_XRAYSCANNER);
	}

	if (cheatIsActive(CHEAT_SUPERDRAGON)) {
		invGiveSingleWeapon(WEAPON_SUPERDRAGON);
		bgunSetAmmoQuantity(AMMOTYPE_RIFLE, 200);
		bgunSetAmmoQuantity(AMMOTYPE_DEVASTATOR, 20);
	}

	if (cheatIsActive(CHEAT_LAPTOPGUN)) {
		invGiveSingleWeapon(WEAPON_LAPTOPGUN);
		bgunSetAmmoQuantity(AMMOTYPE_SMG, 200);
	}

	if (cheatIsActive(CHEAT_PHOENIX)) {
		invGiveSingleWeapon(WEAPON_PHOENIX);
		bgunSetAmmoQuantity(AMMOTYPE_PISTOL, 200);
	}

#if VERSION >= VERSION_NTSC_1_0
	if (cheatIsActive(CHEAT_PSYCHOSISGUN) || cheatIsActive(CHEAT_ALLGUNS)) {
		bgunSetAmmoQuantity(AMMOTYPE_PSYCHOSIS, 4);

		if (cheatIsActive(CHEAT_PSYCHOSISGUN)) {
			invGiveSingleWeapon(WEAPON_PSYCHOSISGUN);
		}
	}
#else
	if (cheatIsActive(CHEAT_PSYCHOSISGUN)) {
		invGiveSingleWeapon(WEAPON_PSYCHOSISGUN);
		bgunSetAmmoQuantity(AMMOTYPE_PSYCHOSIS, 4);
	}
#endif

	if (cheatIsActive(CHEAT_PP9I)) {
		invGiveSingleWeapon(WEAPON_PP9I);
		bgunSetAmmoQuantity(AMMOTYPE_PISTOL, 200);
	}

	if (cheatIsActive(CHEAT_CC13)) {
		invGiveSingleWeapon(WEAPON_CC13);
		bgunSetAmmoQuantity(AMMOTYPE_PISTOL, 200);
	}

	if (cheatIsActive(CHEAT_KL01313)) {
		invGiveSingleWeapon(WEAPON_KL01313);
		bgunSetAmmoQuantity(AMMOTYPE_SMG, 200);
	}

	if (cheatIsActive(CHEAT_KF7SPECIAL)) {
		invGiveSingleWeapon(WEAPON_KF7SPECIAL);
		bgunSetAmmoQuantity(AMMOTYPE_RIFLE, 200);
	}

	if (cheatIsActive(CHEAT_ZZT)) {
		invGiveSingleWeapon(WEAPON_ZZT);
		bgunSetAmmoQuantity(AMMOTYPE_SMG, 200);
	}

	if (cheatIsActive(CHEAT_DMC)) {
		invGiveSingleWeapon(WEAPON_DMC);
		bgunSetAmmoQuantity(AMMOTYPE_SMG, 200);
	}

	if (cheatIsActive(CHEAT_AR53)) {
		invGiveSingleWeapon(WEAPON_AR53);
		bgunSetAmmoQuantity(AMMOTYPE_RIFLE, 200);
	}

	if (cheatIsActive(CHEAT_RCP45)) {
		invGiveSingleWeapon(WEAPON_RCP45);
		bgunSetAmmoQuantity(AMMOTYPE_SMG, 200);
	}

	if (!hasdefaultweapon) {
		g_DefaultWeapons[HAND_RIGHT] = WEAPON_UNARMED;
	}

	g_Vars.currentplayer->prop = propAllocate();
	g_Vars.currentplayer->prop->chr = NULL;
	g_Vars.currentplayer->prop->type = PROPTYPE_PLAYER;

	propActivate(g_Vars.currentplayer->prop);
	propEnable(g_Vars.currentplayer->prop);
	chrInit(g_Vars.currentplayer->prop, NULL);

	if (g_Vars.coopplayernum >= 0) {
		g_Vars.currentplayer->prop->chr->team = TEAM_ALLY;
	} else if (g_Vars.antiplayernum >= 0) {
		if (g_Vars.currentplayer == g_Vars.bond) {
			g_Vars.currentplayer->prop->chr->team = TEAM_ALLY;
		} else {
			g_Vars.currentplayer->prop->chr->team = TEAM_ENEMY;
		}
	} else {
		if (g_Vars.mplayerisrunning) {
			g_Vars.currentplayer->prop->chr->team = 1 << g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].base.team;
		} else {
			g_Vars.currentplayer->prop->chr->team = TEAM_ALLY;
		}
	}

	if (haseyespy) {
		playerInitEyespy();
	}

	if (g_NumSpawnPoints > 0) {
		if (g_Vars.coopplayernum >= 0) {
			turnanglerad = M_BADTAU - scenarioChooseSpawnLocation(30, &pos, rooms, g_Vars.currentplayer->prop);
		} else if (g_Vars.antiplayernum >= 0) {
			turnanglerad = M_BADTAU - scenarioChooseSpawnLocation(30, &pos, rooms, g_Vars.currentplayer->prop);
		} else if (g_Vars.mplayerisrunning && spawnPoolIsReady() && g_Vars.lvframe60 == 0) {
			/* Initial MP placement is owned by mpOrchestrateMatchStartSpawns()
			 * (Hungarian + team anchors + relax).  Use a stable temp point
			 * until lv.c runs the orchestrator before playerSpawn(). */
			const spawn_pool_t *pool = spawnPoolGet();

			if (pool && pool->count > 0) {
				pos = pool->points[0].pos;
				rooms[0] = pool->points[0].room;
				rooms[1] = -1;
				turnanglerad = pool->points[0].angle_rad;
			} else {
				struct coord lr_pos;
				RoomNum lr_room = -1;
				f32 lr_angle = 0.0f;
				spawn_select_tier_t lr_tier =
					spawnPoolLastResort(NULL, 0, &lr_pos, &lr_room, &lr_angle);
				(void)lr_tier;
				pos = lr_pos;
				rooms[0] = lr_room;
				rooms[1] = -1;
				turnanglerad = lr_angle;
			}
			sysLogPrintf(LOG_NOTE,
				"SPAWN: MP initial placement deferred to orchestrator (temp)");
		} else {
			if (g_Vars.mplayerisrunning == 0) {
				g_NumSpawnPoints = 1;
			}

			turnanglerad = M_BADTAU - scenarioChooseSpawnLocation(30, &pos, rooms, g_Vars.currentplayer->prop);
		}
	} else if (g_Vars.mplayerisrunning) {
		// PC: Mod stages may have setup files with no valid intro data, leaving
		// g_NumSpawnPoints at 0. Without a spawn location, rooms[] is uninitialized
		// garbage and cdFindGroundInfoAtCyl will crash reading invalid room numbers.
		// Scan pads for the first one with a valid (non-negative) room number —
		// pad 0 may be a non-player pad with room < 0, which causes CD queries
		// to fail silently and leaves the player spawning in the void.
		// Then probe 8 directions for the nearest wall and face away from it.
		struct pad fallbackpad;
		s32 fallbackpadnum = 0;
		{
			s32 maxpads = (g_PadsFile != NULL) ? g_PadsFile->numpads : 1;
			s32 pi;
			for (pi = 0; pi < maxpads && pi < 64; pi++) {
				struct pad probePad;
				padUnpack(pi, PADFIELD_ROOM, &probePad);
				if (probePad.room >= 0) {
					fallbackpadnum = pi;
					break;
				}
			}
		}
		padUnpack(fallbackpadnum, PADFIELD_POS | PADFIELD_ROOM, &fallbackpad);
		pos.x = fallbackpad.pos.x;
		pos.y = fallbackpad.pos.y;
		pos.z = fallbackpad.pos.z;
		rooms[0] = fallbackpad.room;
		rooms[1] = -1;

		// Probe 8 compass directions to find walls, accumulate a wall vector,
		// then face the opposite direction so the player doesn't spawn staring
		// at a wall. Each probe tests for wall collision at 200 units out.
		{
			static const f32 dirX[8] = {0.0f, 0.707f, 1.0f, 0.707f, 0.0f, -0.707f, -1.0f, -0.707f};
			static const f32 dirZ[8] = {1.0f, 0.707f, 0.0f, -0.707f, -1.0f, -0.707f, 0.0f, 0.707f};
			f32 wallX = 0, wallZ = 0;
			s32 wallCount = 0;
			s32 dir;

			for (dir = 0; dir < 8; dir++) {
				struct coord probe;
				probe.x = pos.x + dirX[dir] * 200.0f;
				probe.y = pos.y;
				probe.z = pos.z + dirZ[dir] * 200.0f;

				if (cdExamCylMove01(&pos, &probe, 30, rooms, CDTYPE_BG, false, 0, 0) == CDRESULT_COLLISION) {
					wallX += dirX[dir];
					wallZ += dirZ[dir];
					wallCount++;
				}
			}

			if (wallCount > 0) {
				// Face away from the average wall direction
				turnanglerad = atan2f(wallX, -wallZ);
			}
			// else turnanglerad stays 0 — no walls nearby, any direction is fine
		}

		sysLogPrintf(LOG_WARNING, "LOAD: no spawn points from intro data, using pad %d fallback (room=%d, angle=%.1f)", fallbackpadnum, fallbackpad.room, turnanglerad);
	}

	sysLogPrintf(LOG_NOTE, "SPAWN: pre-ground pos=(%.1f,%.1f,%.1f) room=%d angle=%.3f",
		pos.x, pos.y, pos.z, rooms[0], turnanglerad);

	/* B-242 (Priority O): post-pick capsule clip check + radial sweep
	 * for the player's initial spawn. Skip the deferred MP-orchestrator
	 * placeholder case (the orchestrator overwrites pos via
	 * playerApplyOrchestratedSpawnFromPool, where the same check runs).
	 * For SP, coop, anti, and the non-deferred MP path: ensure the
	 * picked position has clearance for the chr's full bounding capsule;
	 * sweep radially if needed. */
	if (!(g_Vars.mplayerisrunning && spawnPoolIsReady() && g_Vars.lvframe60 == 0
			&& g_Vars.coopplayernum < 0 && g_Vars.antiplayernum < 0)) {
		struct chrdata *playerchr = g_Vars.currentplayer->prop
			? g_Vars.currentplayer->prop->chr : NULL;
		f32 chr_height = spawnPoolGetChrCapsuleHeight(playerchr);
		f32 chr_radius = (playerchr && playerchr->radius > 0.0f)
			? playerchr->radius : 30.0f;
		(void)spawnPoolFindClearPosition(&pos, rooms, chr_radius, chr_height);
	}

	groundy = cdFindGroundInfoAtCyl(&pos, 30, rooms,
			&g_Vars.currentplayer->floorcol,
			&g_Vars.currentplayer->floortype,
			&g_Vars.currentplayer->floorflags,
			&g_Vars.currentplayer->floorroom,
			0, 0);

	pos.y = g_Vars.currentplayer->vv_eyeheight + groundy;
	g_Vars.currentplayer->vv_manground = groundy;
	g_Vars.currentplayer->vv_ground = groundy;
	g_Vars.currentplayer->vv_theta = (turnanglerad * 360.0f) / M_BADTAU;

	playerResetBond(&g_Vars.currentplayer->bond2, &pos);

	g_Vars.currentplayer->bond2.unk00.x = -sinf(turnanglerad);
	g_Vars.currentplayer->bond2.unk00.y = 0;
	g_Vars.currentplayer->bond2.unk00.z = cosf(turnanglerad);


	g_Vars.currentplayer->prop->pos.f[0] = g_Vars.currentplayer->bondprevpos.f[0] = pos.f[0];
	g_Vars.currentplayer->prop->pos.f[1] = g_Vars.currentplayer->bondprevpos.f[1] = pos.f[1];
	g_Vars.currentplayer->prop->pos.f[2] = g_Vars.currentplayer->bondprevpos.f[2] = pos.f[2];

	propDeregisterRooms(g_Vars.currentplayer->prop);

	g_Vars.currentplayer->prop->rooms[0] = rooms[0];
	g_Vars.currentplayer->prop->rooms[1] = -1;

	playerSetCamPropertiesWithRoom(&pos,
			&g_Vars.currentplayer->bond2.unk28,
			&g_Vars.currentplayer->bond2.unk1c, rooms[0]);

	numchrs = chrsGetNumSlots();

	for (i = 0; i < numchrs; i++) {
		chr = &g_ChrSlots[i];

		if (chr->target == -2) {
			chr->target = g_Vars.currentplayer->prop - g_Vars.props;
		}
	}

	bmoveUpdateRooms(g_Vars.currentplayer);

	if (g_Vars.normmplayerisrunning) {
		playersBeginMpSwirl();
	} else {
		player0f0b9a20();
	}

	g_NumDeathAnimations = 0;

	while (g_DeathAnimations[g_NumDeathAnimations] > 0) {
		g_NumDeathAnimations++;
	}

	g_Vars.currentplayer->tickdiefinished = false;
	g_Vars.currentplayer->chokehandle = NULL;

	for (i = 0; i < ARRAYCOUNT(g_Vars.aibuddies); i++) {
		g_Vars.aibuddies[i] = NULL;
	}

	playerChooseBodyAndHead(&bodynum, &headnum, 0);
	g_Vars.currentplayer->prop->chr->bodynum = bodynum;
	g_Vars.currentplayer->prop->chr->headnum = headnum;
}
