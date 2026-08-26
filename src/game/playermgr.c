#include <ultra64.h>
#include "constants.h"
#include "game/cheats.h"
#include "game/bondgun.h"
#include "game/player.h"
#include "game/playerreset.h"
#include "game/playermgr.h"
#include "game/propobj.h"
#include "bss.h"
#include "lib/memp.h"
#include "lib/rng.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "net/net.h"
#include "assetcatalog.h"

void playermgrInit(void)
{
	s32 i;

	for (i = 0; i < MAX_PLAYERS; i++) {
		g_Vars.playerstats[i].damagescale = 1;
	}

	g_Vars.bondplayernum = 0;
	g_Vars.coopplayernum = -1;
	g_Vars.antiplayernum = -1;
}

void playermgrReset(void)
{
#if MAX_PLAYERS > 4
	for (s32 i = 0; i < MAX_PLAYERS; ++i) {
		g_Vars.players[i] = NULL;
	}
#else
	g_Vars.players[0] = NULL;
	g_Vars.players[1] = NULL;
	g_Vars.players[2] = NULL;
	g_Vars.players[3] = NULL;
#endif

	g_Vars.currentplayer = NULL;
	g_Vars.currentplayerindex = 0;
	g_Vars.currentplayerstats = NULL;
	g_Vars.currentplayernum = 0;

	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		g_Vars.playerorder[i] = i;
	}

	g_Vars.bond = NULL;
	g_Vars.coop = NULL;
	g_Vars.anti = NULL;
	g_Vars.bondvisible = false;
	g_Vars.bondcollisions = false;
}

static void playermgrInitializePlayer(struct player *player, s32 index);

static bool playermgrRolesAreValid(s32 playercount)
{
	if (g_Vars.bondplayernum < 0 || g_Vars.bondplayernum >= playercount) {
		return false;
	}

	if (g_Vars.coopplayernum >= playercount
			|| g_Vars.antiplayernum >= playercount
			|| g_Vars.coopplayernum < -1
			|| g_Vars.antiplayernum < -1) {
		return false;
	}

	if (g_Vars.coopplayernum >= 0 && g_Vars.antiplayernum >= 0) {
		return false;
	}

	if (g_Vars.coopplayernum == g_Vars.bondplayernum
			|| g_Vars.antiplayernum == g_Vars.bondplayernum) {
		return false;
	}

	return true;
}

const char *playermgrAllocateResultString(enum playermgr_allocate_result result)
{
	switch (result) {
	case PLAYMGR_ALLOC_OK: return "ok";
	case PLAYMGR_ALLOC_INVALID_COUNT: return "invalid_count";
	case PLAYMGR_ALLOC_OUT_OF_MEMORY: return "out_of_memory";
	case PLAYMGR_ALLOC_NETWORK_REJECTED: return "network_rejected";
	case PLAYMGR_ALLOC_INVALID_ROLES: return "invalid_roles";
	default: return "unknown";
	}
}

enum playermgr_allocate_result playermgrAllocatePlayers(s32 count)
{
	struct player *staged[MAX_PLAYERS];
	s32 requested = count > 0 ? count : 1;
	s32 i;

	if (count < 0 || requested > MAX_PLAYERS) {
		playermgrReset();
		sysLogPrintf(LOG_WARNING,
				"PLAYER.INIT.ROLLBACK phase=allocation reason=invalid_count requested=%d max=%d",
				requested, MAX_PLAYERS);
		return PLAYMGR_ALLOC_INVALID_COUNT;
	}

	playermgrReset();

	if (!playermgrRolesAreValid(requested)) {
		sysLogPrintf(LOG_WARNING,
			"PLAYER.INIT.ROLLBACK phase=allocation reason=invalid_roles count=%d bond=%d coop=%d anti=%d",
			requested, g_Vars.bondplayernum, g_Vars.coopplayernum,
			g_Vars.antiplayernum);
		return PLAYMGR_ALLOC_INVALID_ROLES;
	}

	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.PREFLIGHT phase=allocation count=%d", requested);

	for (i = 0; i < requested; i++) {
		staged[i] = mempAlloc(sizeof(struct player), MEMPOOL_STAGE);

		if (!staged[i]) {
			playermgrReset();
			sysLogPrintf(LOG_WARNING,
					"PLAYER.INIT.ROLLBACK phase=allocation reason=allocation_failed index=%d count=%d",
					i, requested);
			return PLAYMGR_ALLOC_OUT_OF_MEMORY;
		}

		playermgrInitializePlayer(staged[i], i);
	}

	if (g_NetMode && g_StageNum != STAGE_TITLE && g_StageNum != STAGE_CITRAINING) {
		enum net_player_allocate_result net_result =
			netPlayersAllocate(staged, requested);
		if (net_result != NET_PLAYER_ALLOC_OK) {
			playermgrReset();
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ROLLBACK phase=network reason=network_rejected status=%s count=%d globals_published=0",
				netPlayerAllocateResultString(net_result), requested);
			return PLAYMGR_ALLOC_NETWORK_REJECTED;
		}
	}

	/* No fallible work remains below this point. Publish the complete player set
	 * and all role/current-player links as one synchronous commit. Network
	 * preflight already bound its private candidates without consulting
	 * g_Vars.players[]. */
	for (i = 0; i < requested; i++) {
		g_Vars.players[i] = staged[i];
		playerResetCutsceneState(i);
	}

	setCurrentPlayerNum(0);
	g_Vars.bond = g_Vars.players[g_Vars.bondplayernum];

	if (count > 0) {
		if (g_Vars.coopplayernum >= 0) {
			g_Vars.coop = g_Vars.players[g_Vars.coopplayernum];
			g_Vars.anti = NULL;
		} else if (g_Vars.antiplayernum >= 0) {
			g_Vars.coop = NULL;
			g_Vars.anti = g_Vars.players[g_Vars.antiplayernum];
		}
	} else {
		playermgrSetViewSize(playerGetFbWidth(), playerGetFbHeight());
		g_Vars.coop = NULL;
		g_Vars.anti = NULL;
	}

	g_Vars.bondvisible = true;
	g_Vars.bondcollisions = true;

	sysLogPrintf(LOG_NOTE,
		"PLAYER.INIT.COMMIT phase=allocation count=%d", requested);
	return PLAYMGR_ALLOC_OK;
}

s32 playermgrBuildStageInitOrder(s32 out_order[MAX_PLAYERS])
{
	s32 order_keys[MAX_PLAYERS];
	const s32 player_count = PLAYERCOUNT();
	const bool network_order = g_NetMode == NETMODE_SERVER
		|| g_NetMode == NETMODE_CLIENT;
	s32 ordered_count = 0;

	if (!out_order || player_count <= 0 || player_count > MAX_PLAYERS) {
		sysLogPrintf(LOG_ERROR,
			"PLAYER.INIT.ORDER rejected reason=invalid_count count=%d max=%d",
			player_count, MAX_PLAYERS);
		return -1;
	}

	for (s32 playernum = 0; playernum < player_count; playernum++) {
		struct player *player = g_Vars.players[playernum];
		s32 order_key = playernum;
		s32 insert_at;

		if (!player) {
			sysLogPrintf(LOG_ERROR,
				"PLAYER.INIT.ORDER rejected reason=missing_player player=%d count=%d",
				playernum, player_count);
			return -1;
		}

		if (network_order) {
			if (!player->client || player->client->id >= NET_MAX_CLIENTS) {
				sysLogPrintf(LOG_ERROR,
					"PLAYER.INIT.ORDER rejected reason=invalid_client player=%d has_client=%d client=%d",
					playernum, player->client != NULL,
					player->client ? (s32)player->client->id : -1);
				return -1;
			}
			order_key = (s32)player->client->id;
		}

		for (s32 i = 0; i < ordered_count; i++) {
			if (order_keys[i] == order_key) {
				sysLogPrintf(LOG_ERROR,
					"PLAYER.INIT.ORDER rejected reason=duplicate_key player=%d key=%d other_player=%d",
					playernum, order_key, out_order[i]);
				return -1;
			}
		}

		insert_at = ordered_count;
		while (insert_at > 0 && order_keys[insert_at - 1] > order_key) {
			order_keys[insert_at] = order_keys[insert_at - 1];
			out_order[insert_at] = out_order[insert_at - 1];
			insert_at--;
		}
		order_keys[insert_at] = order_key;
		out_order[insert_at] = playernum;
		ordered_count++;
	}

	for (s32 position = 0; position < ordered_count; position++) {
		const s32 playernum = out_order[position];
		const s32 client_id = network_order
			? (s32)g_Vars.players[playernum]->client->id : -1;
		sysLogPrintf(LOG_NOTE,
			"PLAYER.INIT.ORDER position=%d player=%d client=%d mode=%d",
			position, playernum, client_id, g_NetMode);
	}

	return ordered_count;
}

static void playermgrInitializePlayer(struct player *player, s32 index)
{
	struct hand hand = {
		{0},
		0,
		0,
		1, // gunon
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		1, // unk06ac
		0,
		0,
		0,
		0,
		1, // unk06c0
		0,
		0,
		0,
		0,
		1, // unk06d4
		0,
		0,
		0,
		0,
		1, // unk06e8
		0,
		0,
		0,
		0,
		0,
		0,
		-1, // unk0704
		0,
		1, // unk070c
		0,
		0,
		0,
		0,
		0,
		0,
		PAL ? -16.750415802002f : -19.999996185303f, // unk0728
		0,
		PAL ? 16.750415802002f : 19.999996185303f, // unk0730
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		-1, // unk0770
		0,
		0,
		-1, // unk077c
		0,
		0,
		-1, // unk0788
		0,
		0,
		-1, // unk0794
		0,
		1, // unk079c
		0,
		0,
		1, // unk07a8
		0,
		0,
		1, // unk07b4
		0,
		0,
		1, // unk07c0
		0,
		0,
		0,
		1, // unk07d0
		1, // unk07d4
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		0,
		{0, 0, 1000}, // unk07f8
		NULL, // audiohandle2
		0,
		0,
		0,
		{-1}, // beam.age
	};

	s32 i;

	player->cameramode = CAMERAMODE_DEFAULT;
	player->memcampos.x = 0;
	player->memcampos.y = 0;
	player->memcampos.z = 0;
	player->memcamroom = -1;
	player->cam_pos.x = 0;
	player->cam_pos.y = 0;
	player->cam_pos.z = 0;
	player->cam_room = 1;
	player->globaldrawworldoffset.x = 0;
	player->globaldrawworldoffset.y = 0;
	player->globaldrawworldoffset.z = 0;
	player->globaldrawcameraoffset.x = 0;
	player->globaldrawcameraoffset.y = 0;
	player->globaldrawcameraoffset.z = 0;
	player->globaldrawworldbgoffset.x = 0;
	player->globaldrawworldbgoffset.y = 0;
	player->globaldrawworldbgoffset.z = 0;

	player->vv_manground = 0;
	player->vv_ground = 0;
	player->prop = NULL;

	player->bondperimenabled = true;
	player->periminfo.header.type = GEOTYPE_CYL;
	player->periminfo.header.flags = GEOFLAG_WALL | GEOFLAG_BLOCK_SHOOT;
	player->periminfo.ymax = 0;
	player->periminfo.ymin = 0;
	player->periminfo.x = 0;
	player->periminfo.z = 0;
	player->periminfo.radius = 0;

	player->bondactivateorreload = 0;
	player->model00d4 = 0;
	player->isdead = false;
	player->aborted = false;
	player->bondhealth = 1;
	player->stealhealth = -1;
	player->oldhealth = 1;
	player->oldarmour = 0;
	player->apparenthealth = 1;
	player->apparentarmour = 0;
	player->damageshowtime = -1;
	player->healthshowtime = -1;
	player->shieldshowtime = -1;
	player->healthshowmode = HEALTHSHOWMODE_HIDDEN;

	player->docentreupdown = false;
	player->lastupdown60 = 0;
	player->prevupdown = 0;
	player->movecentrerelease = 0;
	player->lookaheadcentreenabled = true;
	player->automovecentreenabled = true;
	player->fastmovecentreenabled = false;
	player->automovecentre = true;
	player->insightaimmode = false;

	player->autoyaimenabled = true;
	player->autoaimy = 0;
	player->autoyaimprop = NULL;
	player->autoyaimtime60 = -1;

	player->autoxaimenabled = true;
	player->autoaimx = 0;
	player->autoxaimprop = NULL;
	player->autoxaimtime60 = -1;

	player->vv_theta = 0;
	player->speedtheta = 0;
	player->speedthetacontrol = 0;
	player->vv_costheta = 1;
	player->vv_sintheta = 0;

	player->vv_verta = -4;
	player->vv_verta360 = -229.21960449219f;
	player->speedverta = 0;
	player->vv_cosverta = 1;
	player->vv_sinverta = 0;

	player->bondfadetime60 = -1;
	player->bondfadetimemax60 = -1;
	player->bondfadefracold = 0;
	player->bondfadefracnew = 0;
	player->bondbreathing = 0;

	player->playwatchup = true;

	player->colourscreenred = 0xff;
	player->colourscreengreen = 0xff;
	player->colourscreenblue = 0xff;
	player->colourscreenfrac = 0;
	player->colourfadetime60 = -1;
	player->colourfadetimemax60 = -1;
	player->colourfaderedold = 0xff;
	player->colourfaderednew = 0xff;
	player->colourfadegreenold = 0xff;
	player->colourfadegreennew = 0xff;
	player->colourfadeblueold = 0xff;
	player->colourfadebluenew = 0xff;
	player->colourfadefracold = 0;
	player->colourfadefracnew = 0;

	player->bondtype = OUTFIT_DEFAULT;
	player->startnewbonddie = true;
	player->redbloodfinished = false;
	player->deathanimfinished = false;
	player->controldef = 2;
	player->resetheadpos = true;
	player->resetheadrot = true;
	player->resetheadtick = true;

	player->headanim = HEADANIM_RESTING;
	player->headdamp = (PAL ? 0.9166f : 0.93f);
	player->headwalkingtime60 = 0;
	player->headamplitude = 1;
	player->sideamplitude = 1;
	player->headpos.x = 0;
	player->headpos.y = 0;
	player->headpos.z = 0;
	player->headlook.x = 0;
	player->headlook.y = 0;
	player->headlook.z = 1;
	player->headup.x = 0;
	player->headup.y = 1;
	player->headup.z = 0;
	player->headpossum.x = 0;
	player->headpossum.y = 0;
	player->headpossum.z = 0;
	player->headlooksum.x = 0;
	player->headlooksum.y = 0;
	player->headlooksum.z = (PAL ? 11.990406036377f : 14.285716056824f);
	player->headupsum.x = 0;
	player->headupsum.y = (PAL ? 11.990406036377f  : 14.285716056824f);
	player->headupsum.z = 0;
	player->headbodyoffset.x = 0;
	player->headbodyoffset.y = 0;
	player->headbodyoffset.z = 0;

	player->standheight = 0;
	player->standbodyoffset.x = 0;
	player->standbodyoffset.y = 0;
	player->standbodyoffset.z = 0;
	player->standfrac = 0;
	player->standlook[0].x = 0;
	player->standlook[0].y = 0;
	player->standlook[0].z = 1;
	player->standlook[1].x = 0;
	player->standlook[1].y = 0;
	player->standlook[1].z = 1;
	player->standup[0].x = 0;
	player->standup[0].y = 1;
	player->standup[0].z = 0;
	player->standup[1].x = 0;
	player->standup[1].y = 1;
	player->standup[1].z = 0;
	player->standcnt = 0;

	player->devicesactive = 0;
	player->devicesinhibit = 0;

	player->floorflags = 0;

	for (i = 0; i < ARRAYCOUNT(player->viewport); i++) {
		player->viewport[i].vp.vscale[0] = 640;
		player->viewport[i].vp.vscale[1] = (PAL ? 544 : 480);
		player->viewport[i].vp.vscale[2] = 511;
		player->viewport[i].vp.vscale[3] = 0;

		player->viewport[i].vp.vtrans[0] = 640;
		player->viewport[i].vp.vtrans[1] = (PAL ? 544 : 480);
		player->viewport[i].vp.vtrans[2] = 511;
		player->viewport[i].vp.vtrans[3] = 0;
	}

	player->viewwidth = 100;
	player->viewheight = 100;
	player->viewleft = 0;
	player->viewtop = 0;

	player->slayerrocket = NULL;
	player->badrockettime = 0;

	player->visionmode = VISIONMODE_NORMAL;

	player->gunctrl.gunmemtype = 0;
	player->gunctrl.gunmem = NULL;
	player->gunctrl.gunmodeldef = NULL;
	/* B-246 round-7: round-6 playtest log showed bgunReset reading
	 * handmodeldef=0x7c7b663545bbcbd1 (stale heap garbage, same value across
	 * all spawns) at every match init. Same class as the visionmode latent
	 * uninit -- gunmodeldef was the only modeldef pointer initialised at
	 * player creation; handmodeldef and cartmodeldef were never written and
	 * mempAlloc does not zero-fill. Fix mirrors the visionmode treatment:
	 * unconditional NULL init, no VERSION gate. */
	player->gunctrl.handmodeldef = NULL;
	player->gunctrl.cartmodeldef = NULL;

	player->gunctrl.switchtoweaponnum = -1;

	player->gunctrl.gunmemowner = GUNMEMOWNER_CHRBODY;
	player->gunctrl.gunlocktimer = 0;
	player->gunctrl.action = 0;

	player->hands[0] = hand;
	player->hands[1] = hand;

	player->gunposamplitude = 1;
	player->gunxamplitude = 1;

	player->doautoselect = false;
	player->playertriggeron = false;
	player->playertriggerprev = 0;
	player->playertrigtime240 = 0;
	player->curguntofire = 0;
	player->gunshadecol[0] = 0xff;
	player->gunshadecol[1] = 0xff;
	player->gunshadecol[2] = 0xff;
	player->gunshadecol[3] = 0;
	player->resetshadecol = true;
	player->aimtype = 0;
	player->lookingatprop.prop = NULL;

	for (i = 0; i < MAX_PLAYERS; i++) {
		player->trackedprops[i].prop = NULL;
	}

	player->crosspos[0] = 0;
	player->crosspos[1] = 0;
	player->crosspossum[0] = 0;
	player->crosspossum[1] = 0;
	player->guncrossdamp = 0.9f;

	player->hands[HAND_LEFT].crosspos[0] = 0;
	player->hands[HAND_LEFT].crosspos[1] = 0;
	player->hands[HAND_LEFT].guncrosspossum[0] = 0;
	player->hands[HAND_LEFT].guncrosspossum[1] = 0;

	player->hands[HAND_RIGHT].crosspos[0] = 0;
	player->hands[HAND_RIGHT].crosspos[1] = 0;
	player->hands[HAND_RIGHT].guncrosspossum[0] = 0;
	player->hands[HAND_RIGHT].guncrosspossum[1] = 0;

	player->crosspos2[0] = 0;
	player->crosspos2[1] = 0;
	player->crosssum2[0] = 0;
	player->crosssum2[1] = 0;
	player->gunaimdamp = 0.9f;
	player->aimangle.x = 0;
	player->aimangle.y = -M_PI;
	player->aimangle.z = 0;

	player->copiedgoldeneye = 0;
	player->gunammooff = 0;
	player->gunsync = 0;
	player->syncchange = 0;
	player->synccount = 0;
	player->syncoffset = 0;
	player->cyclesum = 0;
	player->gunampsum = 0;

	player->gunzoomfovs[0] = 15;
	player->gunzoomfovs[1] = 60;
	player->gunzoomfovs[2] = 30;

	player->lastroomforoffset = -1;

	player->c_screenwidth = SCREEN_320;
	player->c_screenheight = PAL ? 272 : SCREEN_240;
	player->c_screenleft = 0;
	player->c_screentop = 0;
	player->c_perspnear = 10;
	player->c_perspfovy = 46;
	player->c_perspaspect = 1;
	player->c_halfwidth = SCREEN_320 / 2;
	player->c_halfheight = (PAL ? 272 : SCREEN_240) / 2;
	player->c_scalex = 1;
	player->c_scaley = 1;
	player->c_recipscalex = 1;
	player->c_recipscaley = 1;

	player->mtxl1738 = NULL;
	player->mtxl173c = NULL;
	player->worldtoscreenmtx = NULL;
	player->c_viewfmdynticknum = -1;
	player->mtxf1748 = NULL;
	player->projectionmtx = NULL;
	player->perspmtxl = NULL;
	player->mtxf1754 = NULL;
	player->orthomtxl = NULL;
	player->lookat = NULL;
	player->prevworldtoscreenmtx = NULL;
	player->c_prevviewfmdynticknum = -1;
	player->prevprojectionmtx = NULL;

	player->unk0484 = NULL;
	player->unk0488 = NULL;

	player->c_scalelod60 = 1;
	player->c_scalelod = 1;
	player->c_lodscalez = 1;
	player->c_lodscalezu32 = 0x10000;

	player->screenxminf = 0;
	player->screenyminf = 0;
	player->screenxmaxf = SCREEN_320;
	player->screenymaxf = PAL ? 272 : SCREEN_240;

	player->gunsightoff = 0;
	player->unk1834 = 0;
	player->unk1838 = 0;
	player->unk183c = 0;

	player->zoomintime = 0;
	player->zoomintimemax = 0;
	player->zoominfovy = 60;
	player->zoominfovyold = 60;
	player->zoominfovynew = 60;
	player->fovy = 60;
	player->aspect = 640.0f / (PAL ? 544.0f : 480.0f);
	player->hudmessoff = 0;
	player->bondmesscnt = -1;

	player->weapons = NULL;
	player->equipment = NULL;
	player->equipmaxitems = 0;
	player->equipallguns = false;
	player->equipcuritem = 0;

	player->angleoffset = 0;
	player->invincible = cheatIsActive(CHEAT_INVINCIBLE);
	player->healthdamagetype = 7;
	player->vv_height = 1;
	player->vv_eyeheight = 1;
	player->vv_headheight = 1;
	player->bondleandown = 0;

	player->mpmenuon = false;
	player->damagetype = 7;
	player->deathcount = 0;
	player->lastkilltime60 = -1;
	player->lastkilltime60_2 = -1;
	player->lastkilltime60_3 = -1;
	player->lastkilltime60_4 = -1;
	player->healthdisplaytime60 = 0;

	player->chrmuzzlelast[0] = 0;
	player->chrmuzzlelast[1] = 0;
	player->healthscale = 1;
	player->armourscale = 1;

	player->haschrbody = false;
	player->pausemode = PAUSEMODE_UNPAUSED;
	player->pausetime60 = 0;
	player->activatetimelast = 0;
	player->activatetimethis = 0;
	player->bondmovemode = MOVEMODE_WALK;

	player->bondtankthetaspeedsum = 0;
	player->bondtankverta = 0;
	player->bondtankvertasum = 0;
	player->bondturrettheta = 0;
	player->bondturretthetasum = 0;
	player->bondturretspeedsum = 0;
	player->bondturretside = 0;
	player->bondturretchange = 0;
	player->bondtankslowtime = 0;

	player->hoverbike = NULL;
	player->bondonground = false;
	player->tank = NULL;
	player->unk1af0 = NULL;
	player->bondonturret = 0;
	player->grabbedprop = NULL;
	player->bondtankexplode = false;
	player->tickdiefinished = false;
	player->introanimnum = 0;
	player->lastsighton = 0;

	for (i = 0; i < ARRAYCOUNT(player->targetset); i++) {
		player->targetset[i] = 0;
	}

	player->sighttracktype = SIGHTTRACKTYPE_NONE;
	player->gunextraaimx = 0;
	player->gunextraaimy = 0;

	player->model.anim = &player->unk01c0;

	player->eyespy = NULL;
	player->eyespydarts = MAX_EYESPYDARTS;

	player->autocontrol_aimpad = 0;
	player->autocontrol_lookup = 0;
	player->autocontrol_dist = 0;
	player->autocontrol_walkspeed = 0;
	player->autocontrol_turnspeed = 0;

	player->autoerasertarget = NULL;
	player->autoeraserdist = -1;

	player->sighttimer240 = 0;
	player->aimtaptime = 0;
	player->cachedlookahead = -4;
	player->lookaheadframe = 0;

	player->numaibuddies = 0;

	for (i = 0; i < MAX_BOTS; i++) {
		player->aibuddynums[i] = 0;
	}

	player->teleportstate = TELEPORTSTATE_INACTIVE;
	player->teleporttime = 0;
	player->teleportpad = 0;

	player->commandingaibot = NULL;
	player->training = 0;
	player->deadtimer = -1;
	player->coopcanrestart = false;
	player->foot = 0;
	player->footstepdist = 0;

	player->unk1c64 = 0;
	player->bondextrapos.x = 0;
	player->bondextrapos.y = 0;
	player->bondextrapos.z = 0;

	player->disguised = false;
	player->dostartnewlife = false;

	player->pcinteractusekind = 0;

	player->client = NULL;
	player->ucmd = (g_NetMode == NETMODE_SERVER) ? UCMD_FL_FORCEMASK : 0;
	player->isremote = false;

	playerInitStageTransientDefaults(player);

}

void playermgrCalculateAiBuddyNums(void)
{
	s32 i;
	s32 playernum = g_Vars.currentplayernum;
	s32 playercount = PLAYERCOUNT();

	for (i = playercount; i < g_MpNumChrs; i++) {
		if (g_MpAllChrConfigPtrs[i]->team == g_MpAllChrConfigPtrs[playernum]->team) {
			g_Vars.players[playernum]->aibuddynums[g_Vars.players[playernum]->numaibuddies] = i;
			g_Vars.players[playernum]->numaibuddies++;
		}
	}
}

void setCurrentPlayerNum(s32 playernum)
{
	g_Vars.currentplayernum = playernum;
	g_Vars.currentplayer = g_Vars.players[playernum];
	g_Vars.currentplayerstats = &g_Vars.playerstats[playernum];
	g_Vars.currentplayerindex = playermgrGetOrderOfPlayer(playernum);
}

s32 playermgrGetPlayerNumByProp(struct prop *prop)
{
	s32 i;

	for (i = 0; i < PLAYERCOUNT(); i++) {
		if (!g_Vars.players[i]) continue;
		if (prop == g_Vars.players[i]->prop) {
			return i;
		}
	}

	return -1;
}

/**
 * Return the receiver-local runtime player slot.
 *
 * Network clients deliberately remap their own player object to a local
 * runtime slot, so the stable identity here is the committed player pointer,
 * not the server's wire player number. Offline and pre-network callers retain
 * the existing current-player context.
 */
s32 playermgrGetLocalPlayerNum(void)
{
	s32 localplayernum = -1;

	if (g_NetMode == NETMODE_NONE) {
		return g_Vars.currentplayernum >= 0
				&& g_Vars.currentplayernum < MAX_PLAYERS
				&& g_Vars.players[g_Vars.currentplayernum]
			? g_Vars.currentplayernum : -1;
	}

	if (!g_NetLocalClient || !g_NetLocalClient->player) {
		return -1;
	}

	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.players[i] == g_NetLocalClient->player) {
			/* Duplicate publication is ambiguous even when both slots point at
			 * the same struct. Never choose a convenient first match. */
			if (g_Vars.players[i]->isremote || localplayernum >= 0) {
				return -1;
			}
			localplayernum = i;
		}
	}

	return localplayernum;
}

/**
 * Resolve the player slot that owns camera/cutscene simulation.
 *
 * Shipping in-client networking always uses the exact receiver-local player.
 * The deprecated dedicated policy has no local presentation and deliberately
 * retains its valid ambient simulation slot so server scenario authority can
 * continue advancing without fabricating a local identity.
 */
s32 playermgrGetPresentationPlayerNum(void)
{
	if (g_NetMode == NETMODE_SERVER && g_NetDedicated) {
		return g_Vars.currentplayernum >= 0
				&& g_Vars.currentplayernum < MAX_PLAYERS
				&& g_Vars.players[g_Vars.currentplayernum]
			? g_Vars.currentplayernum : -1;
	}

	return playermgrGetLocalPlayerNum();
}

/**
 * Re-establish the process-local player before global gameplay work.
 *
 * The per-player tick and render loops intentionally finish on the final
 * replicated player. Carrying that remote context into the next global
 * lvTick made scenario graph commands (notably camera cutscenes) mutate a
 * remote presentation slot. Resolve through the committed local player
 * pointer and fail closed while stage/player allocation is incomplete.
 */
bool playermgrRestoreLocalPlayerContext(void)
{
	const s32 localplayernum = playermgrGetLocalPlayerNum();

	if (g_NetMode == NETMODE_NONE) {
		return localplayernum >= 0;
	}

	if (localplayernum < 0) {
		return false;
	}

	setCurrentPlayerNum(localplayernum);
	return true;
}

void playermgrSetViewSize(s32 width, s32 height)
{
	g_Vars.currentplayer->viewwidth = width;
	g_Vars.currentplayer->viewheight = height;
}

void playermgrSetViewPosition(s32 viewleft, s32 viewtop)
{
	g_Vars.currentplayer->viewleft = viewleft;
	g_Vars.currentplayer->viewtop = viewtop;
}

void playermgrSetFovY(f32 fovy)
{
	g_Vars.currentplayer->fovy = fovy;
}

void playermgrSetAspectRatio(f32 aspect)
{
	g_Vars.currentplayer->aspect = aspect;
}

s32 playermgrGetModelOfWeapon(s32 weapon)
{
	s32 model;

	if (weapon >= WEAPON_CUSTOM_START && weapon < WEAPON_CUSTOM_END) {
		const char *weapon_id = catalogWeaponIdByRuntimeWeaponNum(weapon);
		catalog_weapon_result_t weapon_result;

		if (weapon_id && catalogResolveWeapon(weapon_id, &weapon_result)
				&& weapon_result.filenum > 0) {
			const char *model_id =
				catalogIdBySourceFilenum(ASSET_MODEL, weapon_result.filenum);
			catalog_model_result_t model_result;

			if (model_id && catalogResolveModel(model_id, &model_result)
					&& model_result.modelnum >= 0) {
				return model_result.modelnum;
			}
		}
	}

	switch (weapon) {
	case WEAPON_NONE:
	case WEAPON_UNARMED:          model = -1; break;
	case WEAPON_FALCON2:          model = MODEL_CHRFALCON2; break;
	case WEAPON_MAGSEC4:          model = MODEL_CHRLEEGUN1; break;
	case WEAPON_MAULER:           model = MODEL_CHRMAULER; break;
	case WEAPON_DY357MAGNUM:      model = MODEL_CHRDY357; break;
	case WEAPON_DY357LX:          model = MODEL_CHRDY357TRENT; break;
	case WEAPON_PHOENIX:          model = MODEL_CHRMAIANPISTOL; break;
	case WEAPON_FALCON2_SILENCER: model = MODEL_CHRFALCON2SIL; break;
	case WEAPON_FALCON2_SCOPE:    model = MODEL_CHRFALCON2SCOPE; break;
	case WEAPON_CMP150:           model = MODEL_CHRCMP150; break;
	case WEAPON_AR34:             model = MODEL_CHRAR34; break;
	case WEAPON_DRAGON:           model = MODEL_CHRDRAGON; break;
	case WEAPON_SUPERDRAGON:      model = MODEL_CHRSUPERDRAGON; break;
	case WEAPON_K7AVENGER:        model = MODEL_CHRAVENGER; break;
	case WEAPON_CYCLONE:          model = MODEL_CHRCYCLONE; break;
	case WEAPON_CALLISTO:         model = MODEL_CHRMAIANSMG; break;
	case WEAPON_RCP120:           model = MODEL_CHRRCP120; break;
	case WEAPON_LAPTOPGUN:        model = MODEL_CHRPCGUN; break;
	case WEAPON_SHOTGUN:          model = MODEL_CHRSHOTGUN; break;
	case WEAPON_REAPER:           model = MODEL_CHRSKMINIGUN; break;
	case WEAPON_ROCKETLAUNCHER:   model = MODEL_CHRDYROCKET; break;
	case WEAPON_DEVASTATOR:       model = MODEL_CHRDEVASTATOR; break;
	case WEAPON_SLAYER:           model = MODEL_CHRSKROCKET; break;
	case WEAPON_FARSIGHT:         model = MODEL_CHRZ2020; break;
	case WEAPON_SNIPERRIFLE:      model = MODEL_CHRSNIPERRIFLE; break;
	case WEAPON_CROSSBOW:         model = MODEL_CHRCROSSBOW; break;
	case WEAPON_LASER:            model = MODEL_CHRLASER; break;
	case WEAPON_COMBATKNIFE:      model = MODEL_CHRKNIFE; break;
	case WEAPON_TRANQUILIZER:     model = MODEL_CHRDRUGGUN; break;
	case WEAPON_PSYCHOSISGUN:     model = MODEL_CHRDRUGGUN; break;
	case WEAPON_NBOMB:            model = MODEL_CHRNBOMB; break;
	case WEAPON_GRENADE:          model = MODEL_CHRGRENADE; break;
	case WEAPON_REMOTEMINE:       model = MODEL_CHRREMOTEMINE; break;
	case WEAPON_PROXIMITYMINE:    model = MODEL_CHRPROXIMITYMINE; break;
	case WEAPON_TIMEDMINE:        model = MODEL_CHRTIMEDMINE; break;
	case WEAPON_BRIEFCASE2:       model = MODEL_CHRBRIEFCASE; break;
	case WEAPON_CLOAKINGDEVICE:   model = MODEL_CHRCLOAKER; break;
	case WEAPON_COMBATBOOST:      model = -1; break;
	case WEAPON_HAMMER:           model = MODEL_CHRLUMPHAMMER; break;
	case WEAPON_SCREWDRIVER:      model = MODEL_CHRSONICSCREWER; break;
	default:
		model = weapon <= WEAPON_PSYCHOSISGUN ? MODEL_CHRSNIPERRIFLE : -1;
		break;
	}

	return model;
}

void playermgrDeleteWeapon(s32 hand)
{
	weaponDeleteFromChr(g_Vars.currentplayer->prop->chr, hand);
}

void playermgrCreateWeapon(s32 hand)
{
	struct chrdata *chr = g_Vars.currentplayer->prop->chr;

	if (chr->weapons_held[hand] == NULL) {
		s32 weaponnum = bgunGetWeaponNum(hand);
		s32 modelnum = playermgrGetModelOfWeapon(weaponnum);

		if (hand == HAND_LEFT && weaponnum == WEAPON_REMOTEMINE) {
			modelnum = -1;
		}

		if (modelnum >= 0) {
			u32 flags;

			if (hand == HAND_RIGHT) {
				flags = 0;
			} else {
				flags = OBJFLAG_WEAPON_LEFTHANDED;
			}

			weaponCreateForChr(chr, modelnum, weaponnum, flags, NULL, NULL);
		}
	}
}

void playermgrShuffle(void)
{
	s32 i;

	// Order them ascending
	for (i = 0; i < MAX_PLAYERS; i++) {
		g_Vars.playerorder[i] = i;
	}

	if (g_NetMode) {
		// don't shuffle in netgames
		// why is this a thing anyway?
		return;
	}

	// Randomly swap numbers with later elements
	for (i = 0; i < MAX_PLAYERS - 1; i++) {
		s32 otherindex = rngRandom() % (MAX_PLAYERS - i);
		s32 tmp = g_Vars.playerorder[i];

		g_Vars.playerorder[i] = g_Vars.playerorder[i + otherindex];
		g_Vars.playerorder[i + otherindex] = tmp;
	}
}

s32 playermgrGetOrderOfPlayer(s32 playernum)
{
	s32 index = 0;
	s32 i;

	for (i = 0; i < MAX_PLAYERS; i++) {
		s32 thisnum = g_Vars.playerorder[i];

		if (playernum == thisnum) {
			break;
		}

		if (g_Vars.players[thisnum]) {
			index++;
		}
	}

	return index;
}

s32 playermgrGetPlayerAtOrder(s32 ordernum)
{
	s32 i;

	for (i = 0; i < MAX_PLAYERS; i++) {
		if (g_Vars.players[g_Vars.playerorder[i]]) {
			if (ordernum == 0) {
				return g_Vars.playerorder[i];
			}

			ordernum--;
		}
	}

	return 0;
}
