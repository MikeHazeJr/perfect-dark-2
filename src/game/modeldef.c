#include <ultra64.h>
#include "constants.h"
#include "game/chraction.h"
#include "game/ceil.h"
#include "game/bondgun.h"
#include "game/game_0b0fd0.h"
#include "game/tex.h"
#include "game/menugfx.h"
#include "game/menu.h"
#include "game/mainmenu.h"
#include "game/inv.h"
#include "game/game_1531a0.h"
#include "game/file.h"
#include "game/texdecompress.h"
#include "game/tex.h"
#include "game/modeldef.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/options.h"
#include "bss.h"
#include "lib/vi.h"
#include "lib/main.h"
#include "lib/model.h"
#include "lib/memp.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "assetload.h"

struct skeleton *g_Skeletons[] = {
	&g_SkelChr,
	&g_SkelClassicGun,
	&g_Skel06,
	&g_SkelUzi,
	&g_SkelBasic,
	&g_SkelCctv,
	&g_SkelWindowedDoor,
	&g_Skel11,
	&g_Skel12,
	&g_Skel13,
	&g_SkelTerminal,
	&g_SkelCiHub,
	&g_SkelAutogun,
	&g_Skel17,
	&g_Skel18,
	&g_Skel19,
	&g_Skel0A,
	&g_Skel0B,
	&g_SkelCasing,
	&g_SkelChrGun,
	&g_Skel0C,
	&g_SkelJoypad,
	&g_SkelLift,
	&g_SkelSkedar,
	&g_SkelLogo,
	&g_SkelPdLogo,
	&g_SkelHoverbike,
	&g_SkelJumpship,
	&g_Skel20,
	&g_Skel21,
	&g_Skel22,
	&g_SkelLaptopGun,
	&g_SkelK7Avenger,
	&g_SkelChopper,
	&g_SkelFalcon2,
	&g_SkelKnife,
	&g_SkelDrCaroll,
	&g_SkelRope,
	&g_SkelCmp150,
	&g_SkelBanner,
	&g_SkelDragon,
	&g_SkelSuperDragon,
	&g_SkelRocket,
	&g_Skel4A,
	&g_SkelShotgun,
	&g_SkelFarsight,
	&g_Skel4D,
	&g_SkelReaper,
	&g_SkelDropship,
	&g_SkelMauler,
	&g_SkelDevastator,
	&g_SkelRobot,
	&g_SkelPistol,
	&g_SkelAr34,
	&g_SkelMagnum,
	&g_SkelSlayerRocket,
	&g_SkelCyclone,
	&g_SkelSniperRifle,
	&g_SkelTranquilizer,
	&g_SkelCrossbow,
	&g_SkelHudPiece,
	&g_SkelTimedProxyMine,
	&g_SkelPhoenix,
	&g_SkelCallisto,
	&g_SkelHand,
	&g_SkelRcp120,
	&g_SkelSkShuttle,
	&g_SkelLaser,
	&g_SkelMaianUfo,
	&g_SkelGrenade,
	&g_SkelCableCar,
	&g_SkelSubmarine,
	&g_SkelTarget,
	&g_SkelEcmMine,
	&g_SkelUplink,
	&g_SkelRareLogo,
	&g_SkelWireFence,
	&g_SkelRemoteMine,
	&g_SkelBB,
#ifdef AVOID_UB
	NULL // terminate list for sure
#endif
};

static void modeldefPromoteDisplayListsWithSizes(struct modeldef *modeldef, s32 filenum, s32 allocsize, s32 loadedsize, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5, bool update_fileinfo)
{
	s32 sp84;
	u32 s0;
	u32 s4;
	uintptr_t s5;
	struct modelnode *node;
	struct modelnode *prevnode;
	uintptr_t gdl;
	Vtx *vertices;

	if (allocsize <= 0 || loadedsize <= 0) {
		return;
	}

	node = NULL;

	modelIterateDisplayLists(modeldef, &node, (Gfx **)&gdl);

	s5 = gdl;

	if (gdl) {
		s32 v1 = allocsize - (loadedsize - (uintptr_t)(((uintptr_t)modeldef + (UNSEGADDR(gdl) & 0xffffff)) - (uintptr_t)modeldef));
		sp84 = (uintptr_t)v1 + (uintptr_t)((uintptr_t)modeldef - ((uintptr_t)modeldef + (UNSEGADDR(gdl) & 0xffffff)));

		texCopyGdls((Gfx *)((uintptr_t)modeldef + (UNSEGADDR(gdl) & 0xffffff)),
				(Gfx *)(v1 + (uintptr_t)modeldef),
				loadedsize - (uintptr_t)(((uintptr_t)modeldef + (UNSEGADDR(gdl) & 0xffffff)) - (uintptr_t)modeldef));
		texLoadFromConfigs(modeldef->texconfigs, modeldef->numtexconfigs, texpool, (uintptr_t)modeldef2 - (uintptr_t)arg2);

		while (node) {
			prevnode = node;
			s0 = gdl;

			modelIterateDisplayLists(modeldef, &node, (Gfx **) &gdl);

			if (gdl) {
				s4 = UNSEGADDR(gdl) - UNSEGADDR(s0);
			} else {
				s4 = loadedsize + (uintptr_t)modeldef - (uintptr_t)modeldef - (UNSEGADDR(s0) & 0xffffff);
			}

			modelNodeReplaceGdl(modeldef, prevnode, (Gfx *) s0, (Gfx *) s5);

			if (prevnode->type == MODELNODETYPE_DL) {
				struct modelrodata_dl *rodata = &prevnode->rodata->dl;
				vertices = rodata->vertices;
			} else {
				vertices = NULL;
			}

			s5 += texLoadFromGdl((Gfx *)((uintptr_t)modeldef + (UNSEGADDR(s0) & 0xffffff) + sp84), s4, (Gfx *)((uintptr_t)modeldef + (UNSEGADDR(s5) & 0xffffff)), texpool, (u8 *) vertices);
		}

		{
			u32 newsize = (((uintptr_t)modeldef + (UNSEGADDR(s5) & 0xffffff)) - (uintptr_t)modeldef + 0xf) & ~0xf;

			if (update_fileinfo && filenum >= 0) {
				fileSetSize(filenum, modeldef, newsize, arg5);
			} else if (arg5) {
				mempRealloc(modeldef, newsize, MEMPOOL_STAGE);
			}
		}
	}
}

void modeldef0f1a7560(struct modeldef *modeldef, u16 filenum, u32 arg2, struct modeldef *modeldef2, struct texpool *texpool, bool arg5)
{
	modeldefPromoteDisplayListsWithSizes(
		modeldef,
		(s32)filenum,
		fileGetAllocationSize(filenum),
		fileGetLoadedSize(filenum),
		arg2,
		modeldef2,
		texpool,
		arg5,
		true);
}

void modelPromoteTypeToPointer(struct modeldef *modeldef)
{
	s32 i;

	if ((u32)modeldef->skel < 0x10000) {
		for (i = 0; g_Skeletons[i] != NULL; i++) {
			if ((s16)modeldef->skel == g_Skeletons[i]->skel) {
				modeldef->skel = g_Skeletons[i];
				return;
			}
		}
	}
}

static struct modeldef *modeldefValidateLoaded(struct modeldef *modeldef, s32 source_filenum)
{
	if (modeldef->rootnode == NULL
			|| modeldef->numparts > 500) {
		sysLogPrintf(LOG_ERROR,
				"MODELDEF: file %u loaded torn -- parts=%d root=%p scale=%.3f -- rejecting",
				(unsigned)source_filenum,
				modeldef->numparts,
				(void *)modeldef->rootnode,
				modeldef->scale);
		g_LoadType = LOADTYPE_NONE;
		return NULL;
	}
	if (modeldef->scale <= 0.0f) {
		sysLogPrintf(LOG_WARNING,
				"MODELDEF: file %u loaded with degenerate scale %.3f -- clamping to 1.0",
				(unsigned)source_filenum, modeldef->scale);
		modeldef->scale = 1.0f;
	}

	return modeldef;
}

static struct modeldef *modeldefFinalizeLoadedWithSizes(struct modeldef *modeldef, s32 source_filenum, s32 allocsize, s32 loadedsize, u8 *dst, struct texpool *arg3, bool update_fileinfo)
{
	modelPromoteTypeToPointer(modeldef);
	modelPromoteOffsetsToPointers(modeldef, 0x5000000, (uintptr_t) modeldef);
	modeldefPromoteDisplayListsWithSizes(
		modeldef,
		source_filenum,
		allocsize,
		loadedsize,
		0x5000000,
		modeldef,
		arg3,
		dst == NULL,
		update_fileinfo);

	/* B-161 root-cause fix (updated 2026-04-18): validate the fully-promoted
	 * modeldef before returning so torn data never reaches any cache
	 * (g_ModelStates[].modeldef, g_HeadsAndBodies[].modeldef) or downstream
	 * code. Reject ONLY truly-structural corruption:
	 *   - rootnode == NULL  -> cannot walk the node tree at all
	 *   - numparts  > 500   -> preposterous count, likely decode garbage
	 *
	 * Do NOT reject numparts == 0. Simple non-skeletal props -- title logos
	 * (Nintendo, Rare, PD, MODEL_NINTENDOLOGO = file 221), doors, barrels,
	 * static geometry -- legitimately have 0 parts. parts[] is the skeletal
	 * part list; a model without skeletal animation has none. The prior
	 * "reject numparts<=0 at chokepoint" caught both real torn bodies AND
	 * legit simple props, wedging every stage 0x26 prop with scale!=0 and
	 * parts=0 on a live rootnode.
	 *
	 * Body/head modeldefs still need the numparts>0 invariant because
	 * body0f02ce8c (src/game/body.c:203) iterates parts to merge body+head
	 * skeletons. That per-caller guard (already present) keeps the stricter
	 * check where it's needed without poisoning prop loads. */
	return modeldefValidateLoaded(modeldef, source_filenum);
}

static struct modeldef *modeldefFinalizeLoaded(struct modeldef *modeldef, s32 source_filenum, u8 *dst, struct texpool *arg3)
{
	return modeldefFinalizeLoadedWithSizes(
		modeldef,
		source_filenum,
		fileGetAllocationSize(source_filenum),
		fileGetLoadedSize(source_filenum),
		dst,
		arg3,
		true);
}

struct modeldef *modeldefLoadFromHandle(asset_data_handle_t handle, s32 source_filenum, u8 *dst, s32 size, struct texpool *arg3)
{
	struct modeldef *modeldef;

	g_LoadType = LOADTYPE_MODEL;

	if (assetHandleIsNull(handle)) {
		char desc[128];
		g_LoadType = LOADTYPE_NONE;
		sysLogPrintf(LOG_ERROR,
			"CATALOG_CRITICAL: modeldef handle source unsupported -- source_filenum=%d source=%s",
			source_filenum,
			assetDescribe(handle, desc, sizeof(desc)));
		return NULL;
	}

	if (dst) {
		modeldef = assetLoadToAddr(handle, FILELOADMETHOD_EXTRAMEM, dst, size);
	} else {
		modeldef = assetLoadToNew(handle, FILELOADMETHOD_EXTRAMEM, LOADTYPE_MODEL);
	}

	if (modeldef == NULL) {
		/* assetLoadToNew returned NULL (file not in ROM data). Clear
		 * g_LoadType since fileLoad never ran to reset it -- leaving it
		 * stale would cause the next fileLoad to misapply model
		 * preprocessing to unrelated data. */
		g_LoadType = LOADTYPE_NONE;
		sysLogPrintf(LOG_ERROR, "CATALOG_CRITICAL: modeldef fileid=%d failed to load -- "
			"asset not in catalog or ROM data missing", source_filenum);
		return NULL;
	}

	if (handle.provider == romProvider() && source_filenum >= 0) {
		return modeldefFinalizeLoaded(modeldef, source_filenum, dst, arg3);
	}

	{
		s32 loadedsize = assetLoadGetLoadedSize(handle);
		s32 allocsize = dst ? size : ALIGN16(loadedsize + 0x20) + 0x8000;

		return modeldefFinalizeLoadedWithSizes(
			modeldef,
			source_filenum,
			allocsize,
			loadedsize,
			dst,
			arg3,
			false);
	}
}

struct modeldef *modeldefLoad(u16 fileid, u8 *dst, s32 size, struct texpool *arg3)
{
	struct modeldef *modeldef;

	g_LoadType = LOADTYPE_MODEL;

	if (dst) {
		modeldef = assetLoadRomToAddr(fileid, FILELOADMETHOD_EXTRAMEM, dst, size);
	} else {
		modeldef = assetLoadRomToNew((s32)fileid, FILELOADMETHOD_EXTRAMEM, LOADTYPE_MODEL);
	}

	if (modeldef == NULL) {
		g_LoadType = LOADTYPE_NONE;
		sysLogPrintf(LOG_ERROR, "CATALOG_CRITICAL: modeldef fileid=%d failed to load -- "
			"asset not in catalog or ROM data missing", fileid);
		return NULL;
	}

	return modeldefFinalizeLoaded(modeldef, (s32)fileid, dst, arg3);
}

struct modeldef *modeldefLoadToNew(u16 fileid)
{
	return modeldefLoad(fileid, NULL, 0, NULL);
}

struct modeldef *modeldefLoadToNewFromHandle(asset_data_handle_t handle, s32 source_filenum)
{
	return modeldefLoadFromHandle(handle, source_filenum, NULL, 0, NULL);
}

struct modeldef *modeldefLoadToAddr(u16 fileid, u8 *dst, s32 size)
{
	return modeldefLoad(fileid, dst, size, NULL);
}
