#include <ultra64.h>
#include "constants.h"
#include "game/cheats.h"
#include "game/chraction.h"
#include "game/chr.h"
#include "game/body.h"
#include "game/prop.h"
#include "game/atan2f.h"
#include "game/modelmgr.h"
#include "game/lv.h"
#include "game/forgemode.h"
#include "game/hudmsg.h" /* INV-5: HUD message on body identity substitution */
#include "game/modeldef.h"
#include "game/mplayer/mplayer.h"
#include "game/pad.h"
#include "game/propobj.h"
#include "bss.h"
#include "system.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "lib/mema.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "lib/ailist.h"
#include "lib/anim.h"
#include "lib/collision.h"
#include "data.h"
#include "types.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "asset_source_debug.h"
#include "catalog_mgr_heads.h"  /* Catalog Gate 3 F5: catalogManagerHeadIsModeldefLoaded */
#include "net/netmanifest.h"

s32 g_NumActiveHeadsPerGender;
u32 var8009cd24;
s32 g_ActiveMaleHeads[8];
s32 g_ActiveFemaleHeads[8];

s32 g_NumBondBodies = 0;
s32 g_NumMaleGuardHeads = 0;
s32 g_NumFemaleGuardHeads = 0;
s32 g_NumMaleGuardTeamHeads = 0;
s32 g_NumFemaleGuardTeamHeads = 0;
s32 var80062b14 = 0;
s32 var80062b18 = 0;

s32 g_BondBodies[] = {
	BODY_DJBOND,
	BODY_CONNERY,
	BODY_DALTON,
	BODY_MOORE,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

s32 g_MaleGuardHeads[] = {
	HEAD_BEAU1,
	HEAD_CHRIST,
	HEAD_DARLING,
	HEAD_JON,
	HEAD_ROSS,
	HEAD_RUSS,
	HEAD_MARK2,
	HEAD_JAMIE,
	HEAD_DUNCAN2,
	HEAD_BRIAN,
	HEAD_STEVEM,
	HEAD_KEITH,
	HEAD_GRANT,
	HEAD_PENNY,
	HEAD_DAVEC,
	HEAD_JONES,
	HEAD_GRAHAM,
	HEAD_SHAUN,
	HEAD_NEIL2,
	HEAD_EDMCG,
	HEAD_MATT_C,
	HEAD_PEER_S,
	HEAD_ANDY_R,
	HEAD_BEN_R,
	HEAD_STEVE_K,
	HEAD_SCOTT_H,
	HEAD_SANCHEZ,
	HEAD_COOK,
	HEAD_PRYCE,
	HEAD_SILKE,
	HEAD_SMITH,
	HEAD_GARETH,
	HEAD_MURCHIE,
	HEAD_WONG,
	HEAD_CARTER,
	HEAD_TINTIN,
	HEAD_MUNTON,
	HEAD_PHELPS,
	HEAD_KEN,
	HEAD_JOEL,
	HEAD_TIM,
	HEAD_ROBIN,
	-1,
};

s32 g_MaleGuardTeamHeads[] = {
	HEAD_BEAU1,
	HEAD_CHRIST,
	HEAD_DARLING,
	HEAD_JON,
	HEAD_ROSS,
	HEAD_RUSS,
	HEAD_MARK2,
	HEAD_JAMIE,
	HEAD_DUNCAN2,
	HEAD_BRIAN,
	HEAD_STEVEM,
	HEAD_KEITH,
	HEAD_GRANT,
	HEAD_PENNY,
	HEAD_DAVEC,
	HEAD_JONES,
	-1,
};

s32 g_FemaleGuardHeads[] = {
	HEAD_LESLIE_S,
	HEAD_ANKA,
	HEAD_EILEEN_T,
	HEAD_EILEEN_H,
	-1,
};

s32 g_FemaleGuardTeamHeads[] = {
	HEAD_LESLIE_S,
	HEAD_ANKA,
	HEAD_EILEEN_T,
	HEAD_EILEEN_H,
	-1,
};

s32 var80062c80 = 0;
s32 g_ActiveMaleHeadsIndex = 0;
s32 g_ActiveFemaleHeadsIndex = 0;

s32 g_FemGuardHeads[3] = {
	HEAD_ALEX,
	HEAD_JULIANNE,
	HEAD_LAURA,
};

u32 bodyGetRace(s32 bodynum)
{
	switch (bodynum) {
	case BODY_SKEDAR:
	case BODY_MINISKEDAR:
	case BODY_SKEDARKING:
		return RACE_SKEDAR;
	case BODY_DRCAROLL:
		return RACE_DRCAROLL;
	case BODY_EYESPY:
		return RACE_EYESPY;
	case BODY_CHICROB:
		return RACE_ROBOT;
	}

	return RACE_HUMAN;
}

#define BODY_TINY_MODE_ENEMY_MULTIPLIER 3
#define BODY_TINY_MODE_ENEMY_SCALE 0.4f

static void bodyFatalSourceOnlyCharacterAssetFailure(asset_type_e type,
		const char *asset_id, s32 runtime_index, const char *context)
{
	if (!assetSourceDebugIsEnabledFor(type)) {
		return;
	}

	sysFatalError("ASSET.SOURCE_ONLY: %s '%s' %s failed for runtime index %d; "
	              "refusing body fallback/substitution.",
	              assetSourceDebugTypeLabel(type),
	              asset_id && asset_id[0] ? asset_id : "?",
	              context && context[0] ? context : "body modeldef",
	              runtime_index);
}

static bool bodyTinyModeCheatActive(void)
{
	return cheatIsActive(CHEAT_SMALLJO);
}

static bool bodyTinyModeIsUniqueBody(s32 bodynum)
{
	switch (bodynum) {
	case BODY_DJBOND:
	case BODY_CONNERY:
	case BODY_DALTON:
	case BODY_MOORE:
	case BODY_ELVIS1:
	case BODY_CARRINGTON:
	case BODY_TRENT:
	case BODY_CASSANDRA:
	case BODY_THEKING:
	case BODY_DRCAROLL:
	case BODY_EYESPY:
	case BODY_TESTCHR:
	case BODY_CHICROB:
	case BODY_PRESIDENT:
	case BODY_PRESIDENT_CLONE:
	case BODY_PRESIDENT_CLONE2:
	case BODY_CARREVENINGSUIT:
	case BODY_JONATHAN:
	case BODY_SKEDARKING:
	case BODY_ELVISWAISTCOAT:
		return true;
	}

	return false;
}

static bool bodyTinyModeIsGenericEnemy(const struct packedchr *packed, s32 bodynum)
{
	if (packed == NULL) {
		return false;
	}

	if (g_Vars.normmplayerisrunning || g_Vars.mplayerisrunning) {
		return false;
	}

	if (packed->team != TEAM_ENEMY) {
		return false;
	}

	if ((packed->spawnflags & SPAWNFLAG_BASICGUARD) == 0) {
		return false;
	}

	if (packed->spawnflags & SPAWNFLAG_INVINCIBLE) {
		return false;
	}

	if (packed->chair != -1 || packed->convtalk != 0) {
		return false;
	}

	if (packed->flags & (CHRFLAG0_CAN_HEARSPAWN | CHRFLAG0_CHUCKNORRIS)) {
		return false;
	}

	return !bodyTinyModeIsUniqueBody(bodynum);
}

s32 bodyTinyModeExtraChrCountForPacked(const struct packedchr *packed)
{
	s32 bodynum;

	if (!bodyTinyModeCheatActive() || packed == NULL) {
		return 0;
	}

	bodynum = packed->bodynum == 255 ? body0f02d3f8() : packed->bodynum;

	if (!bodyTinyModeIsGenericEnemy(packed, bodynum)) {
		return 0;
	}

	return BODY_TINY_MODE_ENEMY_MULTIPLIER - 1;
}

static void bodyTinyModeScaleGenericEnemy(struct chrdata *chr, const struct packedchr *packed, s32 bodynum)
{
	bool modelscalealreadyapplied;

	if (!bodyTinyModeCheatActive() || !bodyTinyModeIsGenericEnemy(packed, bodynum)) {
		return;
	}

	if (chr == NULL) {
		return;
	}

	chr->chrflags |= CHRCFLAG_TINYMODE_MOVESPEED;

	modelscalealreadyapplied = cheatIsActive(CHEAT_SMALLJO) && bodyGetRace(bodynum) == RACE_HUMAN;

	if (!modelscalealreadyapplied && chr->model != NULL) {
		modelSetScale(chr->model, chr->model->scale * BODY_TINY_MODE_ENEMY_SCALE);
	}

	if (chr->radius > 4) {
		chr->radius = (s32)(chr->radius * BODY_TINY_MODE_ENEMY_SCALE);

		if (chr->radius < 4) {
			chr->radius = 4;
		}
	}

	if (chr->height > 24) {
		chr->height = (s32)(chr->height * BODY_TINY_MODE_ENEMY_SCALE);

		if (chr->height < 24) {
			chr->height = 24;
		}
	}
}

/* B-314 (2026-05-03): centralised allocator for chr->unk348[] fireslot/beam
 * pair used by RACE_ROBOT chrs (BODY_CHICROB). propsRenderBeams (propobj.c
 * around line 11723) does `chr->unk348[0]->beam` and `chr->unk348[1]->beam`
 * for every chr whose CHRRACE() returns RACE_ROBOT. Without this allocation
 * the deref AVs on the first render frame.
 *
 * Mike's 2026-05-03 Combat Sim repro: 31 bots on the Skedar map, two
 * happened to roll BODY_CHICROB (body=118), neither got unk348[] alloc
 * because the MP bot path (botmgr.c::botCreate) lacked the init. Crash at
 * frame=0 of stage 0x32, PC +0x13fdbf inside propsRenderBeams.
 *
 * Solo bodyAllocateChr below STILL has the inline alloc historically; this
 * helper is the single source of truth, callers in botmgr.c and chraction.c
 * use it too. The helper is a no-op for non-CHICROB bodies so existing
 * solo-spawn paths can drop the inline check without changing behaviour. */
void bodyInitChrBeams(struct chrdata *chr, s32 bodynum)
{
	if (bodynum != BODY_CHICROB) {
		return;
	}
	chr->unk348[0] = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
	chr->unk348[1] = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
	chr->unk348[0]->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
	chr->unk348[1]->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
	chr->unk348[0]->beam->age = -1;
	chr->unk348[1]->beam->age = -1;
}

bool bodyLoad(s32 bodynum)
{
	/* SA-5f: lazy-load via catalog; callers ignore return value */
	struct modeldef *md = catalogGetBodyModeldef(bodynum);
	if (!md) {
		sysLogPrintf(LOG_ERROR, "CATALOG_CRITICAL: bodyLoad failed bodynum=%d filenum=%d -- "
			"body model not in catalog or ROM data missing",
			bodynum, catalogGetBodyFilenumByIndex(bodynum));
	}
	return md != NULL;
}

struct model *body0f02ce8c(s32 bodynum, s32 headnum, struct modeldef *bodymodeldef, struct modeldef *headmodeldef, bool sunglasses, struct model *model, bool isplayer, u8 varyheight)
{
	const char *body_source_id = NULL;

	body_source_id = catalogBodyIdByBodynum(bodynum);
	if (!body_source_id) {
		sysLogPrintf(LOG_ERROR,
			"BODY.IDENTITY: bodynum=%d has no catalog body; refusing slot-0 visual fallback "
			"(isplayer=%d headnum=%d)",
			bodynum, isplayer, headnum);
		if (isplayer && g_Vars.currentplayer != NULL && g_Vars.currentplayer->prop != NULL) {
			hudmsgCreate("Character load failed -- missing body source", HUDMSGTYPE_DEFAULT);
		}
		return NULL;
	}

	/* INV-1 / Cohort A.4 (player-init-architectural-fixes-2026-04-26):
	 * checked variants surface catalog miss as a CATALOG.MISS WARNING
	 * rather than silently substituting 1.0f. The substitution still
	 * happens (spawn proceeds with default scale) so existing rendering
	 * paths are unchanged on miss; the diff is the loud diagnostic. */
	f32 scaleRaw = 1.0f;
	f32 animscale = 1.0f;
	(void)catalogGetBodyScaleChecked(bodynum, &scaleRaw);
	(void)catalogGetBodyAnimScaleChecked(bodynum, &animscale);
	f32 scale = scaleRaw * 0.10000001f; /* SA-5-cleanup */
	struct modelnode *node = NULL;
	bool public_source_generated_modeldef = false;
	bool public_source_static_modeldef = false;
	u32 stack[2];

	if (cheatIsActive(CHEAT_DKMODE)) {
		scale *= 0.8f;
	}

	if (bodymodeldef == NULL) {
		bodymodeldef = catalogGetBodyModeldef(bodynum); /* SA-5f */
		if (!bodymodeldef) {
			bodyFatalSourceOnlyCharacterAssetFailure(ASSET_BODY, body_source_id,
				bodynum, "body modeldef load");
			sysLogPrintf(LOG_ERROR, "CATALOG_CRITICAL: body0f02ce8c bodynum=%d filenum=%d -- "
				"model not in catalog", bodynum, catalogGetBodyFilenumByIndex(bodynum));
		}
	}

	public_source_generated_modeldef = (bodymodeldef != NULL
		&& bodymodeldef->rootnode != NULL
		&& body_source_id != NULL
		&& catalogGetLoadedModeldef(body_source_id) == bodymodeldef);
	public_source_static_modeldef = public_source_generated_modeldef
		&& bodymodeldef->skel == NULL;

	/* Safety: if model still couldn't load or contains garbage data, bail out.
	 * The ROM data loader can return allocated-but-uninitialized memory when a
	 * file is missing, so a non-NULL pointer doesn't guarantee valid data.
	 * Check multiple fields for basic sanity. */
	if (bodymodeldef == NULL
		|| bodymodeldef->rootnode == NULL
		|| (!public_source_generated_modeldef && bodymodeldef->skel == NULL)
		|| (!public_source_generated_modeldef && bodymodeldef->numparts <= 0)
		|| bodymodeldef->numparts > 500) {
		bodyFatalSourceOnlyCharacterAssetFailure(ASSET_BODY, body_source_id,
			bodynum, "body modeldef validation");
		sysLogPrintf(LOG_WARNING, "body0f02ce8c: truly invalid bodymodeldef for bodynum %d (file 0x%04x) "
		             "ptr=%p skel=%p root=%p parts=%d -- skipping",
		             bodynum, catalogGetBodyFilenumByIndex(bodynum), /* SA-5f */
		             (void *)bodymodeldef,
		             bodymodeldef ? (void *)bodymodeldef->skel : NULL,
		             bodymodeldef ? (void *)bodymodeldef->rootnode : NULL,
		             bodymodeldef ? bodymodeldef->numparts : -1);
		return model;
	}

	/* Log modeldef scale for diagnostics. Values of 700-2000 are normal for
	 * AllInOneMods replacement models — do NOT clamp. The previous clamp to
	 * 1.0 destroyed model geometry, hit radii, and animation positions.
	 * Only reject truly degenerate values (zero/negative). */
	if (bodymodeldef->scale <= 0.0f) {
		sysLogPrintf(LOG_WARNING, "body0f02ce8c: degenerate scale %.2f for bodynum %d (file 0x%04x) -- setting to 1.0",
		             bodymodeldef->scale, bodynum, catalogGetBodyFilenumByIndex(bodynum)); /* SA-5f */
		bodymodeldef->scale = 1.0f;
	} else {
		/* S593h-followup-4 (2026-05-03): demoted from LOG_NOTE to
		 * LOG_VERBOSE. The line is per-spawn diagnostic (one per
		 * bodyAllocateModel call); under default --verbose=off it is
		 * dropped, under verbose=on it still surfaces. Was contributing
		 * 256+ lines per swarm cycle to log IO load. The "→ base"
		 * rename in catalogGetBodyFilenumByIndex (assetcatalog_api.c)
		 * lands in the same merge so verbose runs see the renamed
		 * notation. */
		sysLogPrintf(LOG_VERBOSE, "body0f02ce8c: bodynum %d (file 0x%04x) modeldef->scale=%.2f",
		             bodynum, catalogGetBodyFilenumByIndex(bodynum), bodymodeldef->scale); /* SA-5f */
	}

	modelAllocateRwData(bodymodeldef);

	if (public_source_generated_modeldef) {
		headmodeldef = NULL;
	} else if (!catalogGetBodyIsComplete(bodynum)) { /* SA-5d */
		if (bodymodeldef->skel == &g_SkelChr) {
			node = modelGetPart(bodymodeldef, MODELPART_CHR_HEADSPOT);

			if (node != NULL) {
				if (headnum < 0) {
					headmodeldef = func0f18e57c(-1 - headnum, &headnum);
					/* B-163: func0f18e57c returns from var800acc28[] which can be
					 * NULL if random-head slot was never populated. */
					if (headmodeldef != NULL) {
						bodymodeldef->rwdatalen += headmodeldef->rwdatalen;
					} else {
						sysLogPrintf(LOG_WARNING,
							"body0f02ce8c: random headmodeldef NULL (bodynum %d) -- skipping head merge",
							bodynum);
					}
				} else if (headnum > 0) {
					/* SA-5f: bodyCalculateHeadOffset modifies the modeldef in-place
					 * (not idempotent) -- must only run on first load.  Capture the
					 * pre-load state before calling catalogGetHeadModeldef().
					 * Catalog Gate 3 F5: probe via catalogManagerHeadIsModeldefLoaded
					 * which checks the manager pool slot s_Heads[h].modeldef
					 * (where the cache lives from F3 onward). */
					s32 head_needs_offset = !catalogManagerHeadIsModeldefLoaded(headnum);
					if (!catalogGetHeadModeldefChecked(headnum, &headmodeldef)) { /* SA-5f */
						bodyFatalSourceOnlyCharacterAssetFailure(ASSET_HEAD,
							catalogHeadIdByHeadnum(headnum), headnum,
							"head modeldef load");
						headmodeldef = NULL;
					}
					if (head_needs_offset && headmodeldef != NULL) {
						bodyCalculateHeadOffset(headmodeldef, headnum, bodynum);
					}

					/* B-163: catalogGetHeadModeldef may return NULL (out-of-range,
					 * torn asset, or HEAD_RANDOM_GENDER).  Skip merging rwdatalen
					 * rather than dereferencing NULL. */
					if (headmodeldef != NULL) {
						modelAllocateRwData(headmodeldef);
						bodymodeldef->rwdatalen += headmodeldef->rwdatalen;
					} else {
						bodyFatalSourceOnlyCharacterAssetFailure(ASSET_HEAD,
							catalogHeadIdByHeadnum(headnum), headnum,
							"head merge");
						sysLogPrintf(LOG_WARNING,
							"body0f02ce8c: headmodeldef NULL for headnum %d (bodynum %d) -- skipping head merge",
							headnum, bodynum);
					}

					if (catalogGetBodyCanVaryHeight(bodynum) && varyheight) { /* SA-5d */
						// Set height to between 95% and 115%
						f32 frac = RANDOMFRAC() * 0.05f;
						scale *= 2.0f * frac - 0.05f + 1.0f;
					}
				}

				if (!isplayer) {
					if (cheatIsActive(CHEAT_SMALLJO)) {
						scale *= 0.4f;
					}

					if (cheatIsActive(CHEAT_DKMODE)) {
						scale *= 1.25f;
					}
				}
			}
		} else if (bodymodeldef->skel == &g_SkelSkedar) {
			if (catalogGetBodyCanVaryHeight(bodynum) && varyheight && bodynum == BODY_SKEDAR) { /* SA-5d */
				// Set height to between 65% and 85%
				f32 frac = RANDOMFRAC();
				scale *= 2.0f * (0.1f * frac) - 0.1f + 0.75f;
			}

			if (1);
		}
	}

	if (model) {
		if (model->rwdatalen < bodymodeldef->rwdatalen);
	} else {
		model = public_source_static_modeldef
			? modelmgrInstantiateModelWithoutAnim(bodymodeldef)
			: modelmgrInstantiateModelWithAnim(bodymodeldef);
	}

	if (model) {
		modelSetScale(model, scale);
		modelSetAnimScale(model, animscale);

		if (headmodeldef && node != NULL && !catalogGetBodyIsComplete(bodynum)) { /* SA-5d */
			bodymodeldef->rwdatalen -= headmodeldef->rwdatalen;

			modelmgrAttachHead(model, node, headmodeldef);

			if ((s16)*(s32 *)&headmodeldef->skel == SKEL_HEAD) {
				struct modelnode *node2;

				if (!sunglasses) {
					node2 = modelGetPart(headmodeldef, MODELPART_HEAD_SUNGLASSES);

					if (node2) {
						union modelrwdata *rwdata = modelGetNodeRwData(model, node2);
						rwdata->toggle.visible = false;
					}
				}

				node2 = modelGetPart(headmodeldef, MODELPART_HEAD_HUDPIECE);

				if (node2) {
					union modelrwdata *rwdata = modelGetNodeRwData(model, node2);
					rwdata->toggle.visible = false;
				}
			}
		} else if (headmodeldef && node == NULL && !catalogGetBodyIsComplete(bodynum)) { /* SA-5d */
			sysLogPrintf(LOG_WARNING,
				"body0f02ce8c: missing headspot for bodynum %d headnum %d -- skipping head attach",
				bodynum, headnum);
		}
	}

	return model;
}

struct model *body0f02d338(s32 bodynum, s32 headnum, struct modeldef *bodymodeldef, struct modeldef *headmodeldef, bool sunglasses, u8 varyheight)
{
	return body0f02ce8c(bodynum, headnum, bodymodeldef, headmodeldef, sunglasses, NULL, false, varyheight);
}

struct model *bodyAllocateModel(s32 bodynum, s32 headnum, u32 spawnflags)
{
	bool sunglasses = false;
	u8 varyheight = true;
	const char *body_canon;
	const char *head_canon;

	/* Ensure body/head are tracked in the active SP asset manifest.
	 * This covers all spawn paths including AI-command spawns (chrSpawnAtCoord)
	 * that bypass bodyAllocateChr.  manifestEnsureLoaded is a no-op in MP mode
	 * or before the manifest is built, so this call is unconditionally safe. */
	body_canon = catalogBodyIdByBodynum(bodynum);
	head_canon = NULL;
	if (body_canon) { manifestEnsureLoaded(body_canon, MANIFEST_TYPE_BODY); }
	if (headnum >= 0 && headnum != HEAD_RANDOM_GENDER) {
		head_canon = catalogHeadIdByHeadnum(headnum);
		if (head_canon) { manifestEnsureLoaded(head_canon, MANIFEST_TYPE_HEAD); }
	}

	/* S301 Bug D diag: log at the catalog boundary. If body_canon or
	 * head_canon is NULL but bodynum >= 0, the runtime_index is invalid
	 * for the active catalog registration — a classic reason for
	 * invisible chr (model load fails). */
	if (!body_canon) {
		sysLogPrintf(LOG_WARNING,
			"CHR.DIAG: bodyAllocateModel body_canon=NULL for bodynum=%d "
			"-- catalog not registered, body model will be missing",
			bodynum);
	}
	/* Integrated-head bodies (Skedar, Dr Caroll, EyeSpy) carry their head
	 * geometry inside the body model itself; the headnum slot is unused
	 * by the body alloc path. The S593g gate below skips the warning for
	 * those bodies so a swarm-test session (which spawns 256 Skedars in
	 * one frame) does not flood the log with 256 head-canon misses that
	 * are structurally meaningless. The warning still fires for normal
	 * (separate-head) bodies where a missing catalog head IS a real
	 * load-time problem. */
	if (headnum >= 0 && headnum != HEAD_RANDOM_GENDER && !head_canon
			&& !catalogGetBodyIsComplete(bodynum)) {
		sysLogPrintf(LOG_WARNING,
			"CHR.DIAG: bodyAllocateModel head_canon=NULL for headnum=%d "
			"-- catalog not registered, head model will be missing",
			headnum);
	}

	if (spawnflags & SPAWNFLAG_FORCESUNGLASSES) {
		sunglasses = true;
	} else if (spawnflags & SPAWNFLAG_MAYBESUNGLASSES) {
		sunglasses = rngRandom() % 2 == 0;
	}

	if (spawnflags & SPAWNFLAG_FIXEDHEIGHT) {
		varyheight = false;
	}

	struct model *m = body0f02d338(bodynum, headnum, NULL, NULL, sunglasses, varyheight);
	if (m == NULL) {
		sysLogPrintf(LOG_WARNING,
			"CHR.DIAG: body0f02d338 returned NULL bodynum=%d headnum=%d "
			"body_id='%s' head_id='%s' flags=0x%x",
			bodynum, headnum,
			body_canon ? body_canon : "(unresolved)",
			head_canon ? head_canon : "(unresolved)",
			spawnflags);
	}
	return m;
}

s32 body0f02d3f8(void)
{
	return g_BondBodies[var80062c80];
}

s32 bodyChooseHead(s32 bodynum)
{
	s32 head;

	if (catalogGetBodyIsMale(bodynum)) { /* SA-5d */
		head = g_ActiveMaleHeads[g_ActiveMaleHeadsIndex++];

		if (g_ActiveMaleHeadsIndex == g_NumActiveHeadsPerGender) {
			g_ActiveMaleHeadsIndex = 0;
		}
	} else if (bodynum == BODY_FEM_GUARD) {
		head = g_FemGuardHeads[rngRandom() % 3];
	} else {
		head = g_ActiveFemaleHeads[g_ActiveFemaleHeadsIndex++];

		if (g_ActiveFemaleHeadsIndex == g_NumActiveHeadsPerGender) {
			g_ActiveFemaleHeadsIndex = 0;
		}
	}

	return head;
}

/**
 * Read a "packed" chr definition and create a runtime chr from it.
 *
 * Chr definitions are stored in a packed format in each stage's setup file.
 * The packed format is used for space saving reasons.
 */
struct chrdata *bodyAllocateChr(s32 stagenum, struct packedchr *packed, s32 cmdindex)
{
	struct pad pad;
	RoomNum rooms[2];
	struct chrdata *chr;
	struct modeldef *headmodeldef;
	struct model *model;
	struct prop *prop;
	s32 bodynum;
	s32 headnum;
	f32 angle;
	s32 index;
	const char *body_canon;
	const char *head_canon;

	/* B-254 (2026-04-25): canvas-mode chr-spawn gate at the function-API
	 * level (defense in depth).  setupCreateProps already short-circuits
	 * the OBJTYPE_CHR case when forgeIsCanvasMode() is true, but any
	 * future path that calls bodyAllocateChr from outside the setup loop
	 * (e.g. AI-script-driven dynamic spawns, mod content) will hit this
	 * guard.  In canvas mode we suppress NPC creation regardless of the
	 * caller. */
	if (forgeIsCanvasMode()) {
		sysLogPrintf(LOG_NOTE,
			"GRID.CANVAS: bodyAllocateChr(stagenum=0x%02x cmdindex=%d) suppressed",
			stagenum, cmdindex);
		return NULL;
	}

	padUnpack(packed->padnum, PADFIELD_POS | PADFIELD_LOOK | PADFIELD_ROOM, &pad);

	rooms[0] = pad.room;
	rooms[1] = -1;

	if (cdTestVolume(&pad.pos, 20, rooms, CDTYPE_ALL, CHECKVERTICAL_YES, 200, -200) == CDRESULT_COLLISION
			&& packed->chair == -1
			&& (packed->spawnflags & SPAWNFLAG_IGNORECOLLISION) == 0) {
		return NULL;
	}

	if (packed->spawnflags & (SPAWNFLAG_ONLYONA | SPAWNFLAG_ONLYONSA | SPAWNFLAG_ONLYONPA)) {
		if ((packed->spawnflags & (SPAWNFLAG_ONLYONA | SPAWNFLAG_ONLYONSA | SPAWNFLAG_ONLYONPA)) == 0) {
			return NULL;
		}

		if (((packed->spawnflags & SPAWNFLAG_ONLYONA) && lvGetDifficulty() == DIFF_A)
				|| ((packed->spawnflags & SPAWNFLAG_ONLYONSA) && lvGetDifficulty() == DIFF_SA)
				|| ((packed->spawnflags & SPAWNFLAG_ONLYONPA) && lvGetDifficulty() == DIFF_PA)) {
			// ok
		} else {
			return NULL;
		}
	}

	headnum = -55555;
	headmodeldef = NULL;

	if (packed->bodynum == 255) {
		bodynum = body0f02d3f8();
	} else {
		bodynum = packed->bodynum;
	}

	if (!catalogGetBodyIsComplete(bodynum)) { /* SA-5d */
		if (packed->headnum >= 0) {
			headnum = packed->headnum;
		} else if (headnum == -55555) {
			headnum = bodyChooseHead(bodynum);
		}
	}

	/* Ensure body and head are tracked in the SP asset manifest.
	 * manifestEnsureLoaded() is a no-op if no SP manifest is active (MP mode
	 * or pre-load), so this guard is safe to leave unconditional.
	 * headnum -55555 means the head is built into the body model — no
	 * separate head catalog entry exists for that case. */
	body_canon = catalogBodyIdByBodynum(bodynum);
	if (body_canon) { manifestEnsureLoaded(body_canon, MANIFEST_TYPE_BODY); }
	if (headnum >= 0 && headnum != HEAD_RANDOM_GENDER) {
		head_canon = catalogHeadIdByHeadnum(headnum);
		if (head_canon) { manifestEnsureLoaded(head_canon, MANIFEST_TYPE_HEAD); }
	}

	if (headnum < 0) {
		index = -1 - headnum;

		if (index >= 0 && index < 22) {
			headmodeldef = func0f18e57c(index, &headnum);
		}

		model = body0f02ce8c(bodynum, headnum, NULL, headmodeldef, false, NULL, false, false);
	} else {
		model = bodyAllocateModel(bodynum, headnum, packed->spawnflags);
	}

	if (model != NULL) {
		angle = atan2f(pad.look.x, pad.look.z);
		prop = chrAllocate(model, &pad.pos, rooms, angle, ailistFindById(packed->ailistnum));

		if (prop != NULL) {
			propActivate(prop);
			propEnable(prop);

			chr = prop->chr;
			chrSetChrnum(chr, packed->chrnum);
			chr->hearingscale = packed->hearscale / 1000.0f;
			chr->visionrange = packed->viewdist;
			chr->padpreset1 = packed->padpreset;
			chr->chrpreset1 = packed->chrpreset;
			chr->headnum = headnum;
			chr->bodynum = bodynum;
			chr->race = bodyGetRace(chr->bodynum);

			chr->rtracked = false;

			if (bodynum == BODY_DRCAROLL) {
				chr->drcarollimage_left = 0;
				chr->drcarollimage_right = 0;
				chr->height = 185;
				chr->radius = 30;
			} else if (bodynum == BODY_CHICROB) {
				/* B-314: alloc moved to bodyInitChrBeams helper so
				 * MP bot + AI-spawn paths get the same init. The
				 * radius/height tweak stays here because it is a
				 * solo-cmd-spawn footprint preset (MP bots get
				 * their own per-bot height/radius from chrAllocate
				 * defaults plus the swarm scale). */
				bodyInitChrBeams(chr, bodynum);
				chr->height = 200;
				chr->radius = 42;
			}

			if (packed->spawnflags & SPAWNFLAG_INVINCIBLE) {
				chr->chrflags |= CHRCFLAG_INVINCIBLE;
			}

			if (packed->spawnflags & SPAWNFLAG_BASICGUARD) {
				chr->hidden |= CHRHFLAG_BASICGUARD;
			}

			if (packed->spawnflags & SPAWNFLAG_ANTINONINTERACTABLE) {
				chr->hidden |= CHRHFLAG_ANTINONINTERACTABLE;
			}

			if (packed->spawnflags & SPAWNFLAG_DONTSHOOTME) {
				chr->hidden |= CHRHFLAG_DONTSHOOTME;
			}

			if (packed->spawnflags & SPAWNFLAG_HIDDEN) {
				chr->chrflags |= CHRCFLAG_HIDDEN;
			}

			if (packed->spawnflags & SPAWNFLAG_RTRACKED) {
				chr->rtracked = true;
			}

			if (packed->spawnflags & SPAWNFLAG_NOBLOOD) {
				chr->noblood = true;
			}

			if (packed->spawnflags & SPAWNFLAG_BLUESIGHT) {
				chr->hidden2 |= CHRH2FLAG_BLUESIGHT;
			}

			chr->flags = packed->flags;
			chr->flags2 = packed->flags2;

			if (cheatIsActive(CHEAT_MARQUIS)) {
				chr->flags2 &= ~CHRFLAG1_NOHANDCOMBAT;
				chr->flags2 |= CHRFLAG1_HANDCOMBATONLY;
			}

			chr->team = packed->team;
			chr->squadron = packed->squadron;
			chr->aibot = NULL;

			if (packed->tude != 4) {
				chr->tude = packed->tude;
			} else {
				chr->tude = rngRandom() % 4;
			}

			chr->voicebox = rngRandom() % 3;

			if (!catalogGetBodyIsMale(chr->bodynum)) { /* SA-5d */
				chr->voicebox = VOICEBOX_FEMALE;
			}

			chr->naturalanim = packed->naturalanim;
			chr->myspecial = packed->chair;
			chr->yvisang = packed->yvisang;

			packed->chrindex = chr - g_ChrSlots;

			chr->teamscandist = packed->teamscandist;
			chr->convtalk = packed->convtalk;

			if (chr->flags & CHRFLAG0_CAN_HEARSPAWN) {
				chr->chrflags |= CHRCFLAG_CLONEABLE;
			}

			if (!g_Vars.normmplayerisrunning && g_MissionConfig.iscoop && g_Vars.numaibuddies > 0) {
				chr->flags |= CHRFLAG0_AIVSAI;
			}

			if (rngRandom() % 5 == 0) {
				// Make chr punch slower
				chr->flags2 |= CHRFLAG1_ADJUSTPUNCHSPEED;
			}

			if (CHRRACE(chr) == RACE_SKEDAR) {
				chr->chrflags |= CHRCFLAG_FORCEAUTOAIM;
			}

			bodyTinyModeScaleGenericEnemy(chr, packed, bodynum);

			return chr;
		}
	}

	return NULL;
}

struct prop *bodyAllocateEyespy(struct pad *pad, RoomNum room)
{
	RoomNum rooms[2];
	struct prop *prop;
	struct chrdata *chr;
	struct model *model;
	s32 inlift;
	struct prop *lift;
	f32 ground;

	rooms[0] = room;
	rooms[1] = -1;

#if PIRACYCHECKS
	{
		u32 stack[2];
		u32 checksum = 0;
		s32 *ptr = (s32 *)&lvReset;
		s32 *end = (s32 *)&lvConfigureFade;

		while (ptr < end) {
			checksum <<= 1;
			checksum ^= *ptr;
			ptr++;
		}

		if (checksum != CHECKSUM_PLACEHOLDER) {
			s32 *ptr2 = (s32 *)_memaFree;
			s32 *end2 = (s32 *)memaInit;

			while (ptr2 < end2) {
				ptr2[0] = 0;
				ptr2++;
			}
		}
	}
#endif

	model = bodyAllocateModel(BODY_EYESPY, 0, 0);

	if (model) {
		prop = chrAllocate(model, &pad->pos, rooms, 0, ailistFindById(GAILIST_IDLE));

		if (prop) {
			propActivate(prop);
			propEnable(prop);
			chr = prop->chr;
			chrSetChrnum(chr, chrsGetNextUnusedChrnum());
			chr->bodynum = BODY_EYESPY;
			chr->padpreset1 = 0;
			chr->chrpreset1 = 0;
			chr->headnum = 0;
			chr->hearingscale = 0;
			chr->visionrange = 0;
			chr->race = bodyGetRace(chr->bodynum);

			ground = cdFindGroundInfoAtCyl(&pad->pos, 30, rooms, NULL, NULL, NULL, NULL, &inlift, &lift);
			chr->ground = ground;
			chr->manground = ground;

			chr->flags = 0;
			chr->flags2 = 0;
			chr->team = 0;
			chr->squadron = 0;
			chr->maxdamage = 2;
			chr->tude = rngRandom() & 3;
			chr->voicebox = rngRandom() % 3;
			chr->naturalanim = 0;
			chr->myspecial = 0;
			chr->yvisang = 0;
			chr->teamscandist = 0;
			chr->convtalk = 0;
			chr->radius = 26;
			chr->height = 200;
			func0f02e9a0(chr, 0);
			chr->chrflags |= CHRCFLAG_HIDDEN;

#if VERSION >= VERSION_NTSC_1_0
			chr->hidden2 |= CHRH2FLAG_CONSIDERPROXIES;
#else
			chr->hidden |= CHRHFLAG_CONSIDERPROXIES;
#endif

			return prop;
		}
	}

	return NULL;
}

void body0f02ddbf(void)
{
	// empty
}

/**
 * Tweak the head's Y offset to suit the body.
 *
 * By default, heads and their matching bodies align perfectly and don't need
 * any tweaking. This function is used in multiplayer where players can put any
 * heads on any bodies.
 */
void bodyCalculateHeadOffset(struct modeldef *headmodeldef, s32 headnum, s32 bodynum)
{
	struct modelnode *node;
	struct modelnode *prev;
	Gfx *gdl;
	s32 offset;
	struct modelrodata_bbox *bbox;
	s32 i;

#if VERSION >= VERSION_JPN_FINAL
	offset = 0;

	switch (headnum) {
	case HEAD_DARK_COMBAT:
	case HEAD_DARK_FROCK:
	case HEAD_DARKAQUA:
	case HEAD_DARK_SNOW:
		switch (bodynum) {
		case BODY_DARK_COMBAT:
		case BODY_DARK_FROCK:
		case BODY_DARK_TRENCH:
		case BODY_DARK_RIPPED:
		case BODY_DARK_AF1:
		case BODY_DARKWET:
		case BODY_DARKAQUALUNG:
		case BODY_DARKSNOW:
		case BODY_DARKLAB:
		case BODY_DARK_LEATHER:
		case BODY_DARK_NEGOTIATOR:
			break;
		default:
			offset = -12;
			break;
		}
		break;
	}
#endif

	if ((s16)(*(s32 *)&headmodeldef->skel) == SKEL_HEAD) {
#if VERSION >= VERSION_JPN_FINAL
		/* SA-5d: body/head type resolution via catalog accessors */
		if (catalogGetHeadType(headnum) == catalogGetBodyType(bodynum) && offset == 0) {
			return;
		}
#else
		if (catalogGetHeadType(headnum) == catalogGetBodyType(bodynum)) {
			return;
		}
#endif

#if VERSION >= VERSION_JPN_FINAL
		switch (catalogGetHeadType(headnum)) { /* SA-5d */
		default:
		case HEADBODYTYPE_FEMALE:
			offset += 0;
			break;
		case HEADBODYTYPE_MAIAN:
			offset += 0;
			break;
		case HEADBODYTYPE_DEFAULT:
			offset -= 35;
			break;
		case HEADBODYTYPE_MRBLONDE:
			offset += 0;
			break;
		case HEADBODYTYPE_CASS:
			offset -= 20;
			break;
		case HEADBODYTYPE_FEMALEGUARD:
			offset -= 40;
			break;
		}
#else
		// Same as JPN, but sets the value rather than adjusts
		switch (catalogGetHeadType(headnum)) { /* SA-5d */
		default:
		case HEADBODYTYPE_FEMALE:
			offset = 0;
			break;
		case HEADBODYTYPE_MAIAN:
			offset = 0;
			break;
		case HEADBODYTYPE_DEFAULT:
			offset = -35;
			break;
		case HEADBODYTYPE_MRBLONDE:
			offset = 0;
			break;
		case HEADBODYTYPE_CASS:
			offset = -20;
			break;
		case HEADBODYTYPE_FEMALEGUARD:
			offset = -40;
			break;
		}
#endif

		switch (catalogGetBodyType(bodynum)) { /* SA-5d */
		case HEADBODYTYPE_FEMALE:
			break;
		case HEADBODYTYPE_MAIAN:
			offset -= 30;
			break;
		case HEADBODYTYPE_DEFAULT:
			offset += 35;
			break;
		case HEADBODYTYPE_MRBLONDE:
			break;
		case HEADBODYTYPE_CASS:
			offset += 20;
			break;
		case HEADBODYTYPE_FEMALEGUARD:
			offset += 40;
			break;
		}

		if (catalogGetBodyType(bodynum) == HEADBODYTYPE_FEMALE) { /* SA-5d */
			if (catalogGetHeadType(headnum) == HEADBODYTYPE_DEFAULT
					|| catalogGetHeadType(headnum) == HEADBODYTYPE_MRBLONDE) {
				offset -= 10;
			} else if (catalogGetHeadType(headnum) == HEADBODYTYPE_CASS
					|| catalogGetHeadType(headnum) == HEADBODYTYPE_FEMALEGUARD) {
				offset -= 5;
			}
		} else if (catalogGetBodyType(bodynum) == HEADBODYTYPE_CASS
				&& (catalogGetHeadType(headnum) == HEADBODYTYPE_DEFAULT
					|| catalogGetHeadType(headnum) == HEADBODYTYPE_MRBLONDE)) {
			offset -= 5;
		}

		// Apply the offset
		if (offset != 0) {
			node = NULL;

			do {
				prev = node;

				modelIterateDisplayLists(headmodeldef, &node, &gdl);

				if (node && node != prev && node->type == MODELNODETYPE_DL) {
					struct modelrodata_dl *rodata = &node->rodata->dl;

					for (i = 0; i < rodata->numvertices; i++) {
						rodata->vertices[i].y += offset;
					}
				}
			} while (node);

			bbox = modeldefFindBboxRodata(headmodeldef);

			if (bbox != NULL) {
				bbox->ymin += offset;
				bbox->ymax += offset;
			}
		}
	}
}
