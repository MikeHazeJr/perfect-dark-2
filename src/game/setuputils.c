#include <ultra64.h>
#include "constants.h"
#include "game/prop.h"
#include "game/setuputils.h"
#include "game/modelmgr.h"
#include "game/bg.h"
#include "game/modeldef.h"
#include "game/propobj.h"
#include "lib/main.h"
#include "lib/model.h"
#include "bss.h"
#include "data.h"
#include "types.h"
#include "platform.h"
#include "assetcatalog.h"
#include "asset_source_debug.h"
#include "net/netmanifest.h"
#include "system.h"
#include "model_rodata_guard.h" /* S483b: load-time rodata-tree validator */

struct stagesetup g_StageSetup;
u8 *g_GeCreditsData;

u32 setupGetCmdLength(u32 *cmd)
{
#if VERSION < VERSION_NTSC_1_0
	static u32 crash1 = 0;

	mainOverrideVariable("crash1", &crash1);
#endif

	switch ((u8)PD_BE32(cmd[0])) {
	case OBJTYPE_CHR:                return sizeof(struct packedchr) / sizeof(u32);
	case OBJTYPE_DOOR:               return sizeof(struct doorobj) / sizeof(u32);
	case OBJTYPE_DOORSCALE:          return sizeof(struct doorscaleobj) / sizeof(u32);
	case OBJTYPE_BASIC:              return sizeof(struct defaultobj) / sizeof(u32);
	case OBJTYPE_DEBRIS:             return sizeof(struct debrisobj) / sizeof(u32);
	case OBJTYPE_GLASS:              return sizeof(struct glassobj) / sizeof(u32);
	case OBJTYPE_TINTEDGLASS:        return sizeof(struct tintedglassobj) / sizeof(u32);
	case OBJTYPE_SAFE:               return sizeof(struct safeobj) / sizeof(u32);
	case OBJTYPE_GASBOTTLE:          return sizeof(struct gasbottleobj) / sizeof(u32);
	case OBJTYPE_KEY:                return sizeof(struct keyobj) / sizeof(u32);
	case OBJTYPE_ALARM:              return sizeof(struct alarmobj) / sizeof(u32);
	case OBJTYPE_CCTV:               return sizeof(struct cctvobj) / sizeof(u32);
	case OBJTYPE_AMMOCRATE:          return sizeof(struct ammocrateobj) / sizeof(u32);
	case OBJTYPE_WEAPON:             return sizeof(struct weaponobj) / sizeof(u32);
	case OBJTYPE_SINGLEMONITOR:      return sizeof(struct singlemonitorobj) / sizeof(u32);
	case OBJTYPE_MULTIMONITOR:       return sizeof(struct multimonitorobj) / sizeof(u32);
	case OBJTYPE_HANGINGMONITORS:    return sizeof(struct hangingmonitorsobj) / sizeof(u32);
	case OBJTYPE_AUTOGUN:            return sizeof(struct autogunobj) / sizeof(u32);
	case OBJTYPE_LINKGUNS:           return sizeof(struct linkgunsobj) / sizeof(u32);
	case OBJTYPE_HAT:                return sizeof(struct hatobj) / sizeof(u32);
	case OBJTYPE_GRENADEPROB:        return sizeof(struct grenadeprobobj) / sizeof(u32);
	case OBJTYPE_LINKLIFTDOOR:       return sizeof(struct linkliftdoorobj) / sizeof(u32);
	case OBJTYPE_SAFEITEM:           return sizeof(struct safeitemobj) / sizeof(u32);
	case OBJTYPE_MULTIAMMOCRATE:     return sizeof(struct multiammocrateobj) / sizeof(u32);
	case OBJTYPE_SHIELD:             return sizeof(struct shieldobj) / sizeof(u32);
	case OBJTYPE_TAG:                return sizeof(struct tag) / sizeof(u32);
	case OBJTYPE_RENAMEOBJ:          return sizeof(struct textoverride) / sizeof(u32);
	case OBJTYPE_BEGINOBJECTIVE:     return sizeof(struct objective) / sizeof(u32);
	case OBJTYPE_ENDOBJECTIVE:       return 1;
	case OBJECTIVETYPE_DESTROYOBJ:   return 2;
	case OBJECTIVETYPE_COMPFLAGS:    return 2;
	case OBJECTIVETYPE_FAILFLAGS:    return 2;
	case OBJECTIVETYPE_COLLECTOBJ:   return 2;
	case OBJECTIVETYPE_THROWOBJ:     return 2;
	case OBJECTIVETYPE_HOLOGRAPH:    return sizeof(struct criteria_holograph) / sizeof(u32);
	case OBJECTIVETYPE_1F:           return 1;
	case OBJECTIVETYPE_ENTERROOM:    return sizeof(struct criteria_roomentered) / sizeof(u32);
	case OBJECTIVETYPE_THROWINROOM:  return sizeof(struct criteria_throwinroom) / sizeof(u32);
	case OBJTYPE_22:                 return 1;
	case OBJTYPE_BRIEFING:           return sizeof(struct briefingobj) / sizeof(u32);
	case OBJTYPE_PADLOCKEDDOOR:      return sizeof(struct padlockeddoorobj) / sizeof(u32);
	case OBJTYPE_TRUCK:              return sizeof(struct truckobj) / sizeof(u32);
	case OBJTYPE_HELI:               return sizeof(struct heliobj) / sizeof(u32);
	case OBJTYPE_TANK:               return 32;
	case OBJTYPE_CAMERAPOS:          return sizeof(struct cameraposobj) / sizeof(u32);
	case OBJTYPE_LIFT:               return sizeof(struct liftobj) / sizeof(u32);
	case OBJTYPE_CONDITIONALSCENERY: return sizeof(struct linksceneryobj) / sizeof(u32);
	case OBJTYPE_BLOCKEDPATH:        return sizeof(struct blockedpathobj) / sizeof(u32);
	case OBJTYPE_HOVERBIKE:          return sizeof(struct hoverbikeobj) / sizeof(u32);
	case OBJTYPE_HOVERPROP:          return sizeof(struct hoverpropobj) / sizeof(u32);
	case OBJTYPE_FAN:                return sizeof(struct fanobj) / sizeof(u32);
	case OBJTYPE_HOVERCAR:           return sizeof(struct hovercarobj) / sizeof(u32);
	case OBJTYPE_CHOPPER:            return sizeof(struct chopperobj) / sizeof(u32);
	case OBJTYPE_PADEFFECT:          return sizeof(struct padeffectobj) / sizeof(u32);
	case OBJTYPE_MINE:               return sizeof(struct weaponobj) / sizeof(u32);
	case OBJTYPE_ESCASTEP:           return sizeof(struct escalatorobj) / sizeof(u32);
	}

#if VERSION < VERSION_NTSC_1_0
	if (crash1) {
		CRASH();
	}
#endif

	return 1;
}

u32 *setupGetCmdByIndex(s32 wantindex)
{
	u32 *cmd = g_StageSetup.props;

	if (wantindex >= 0 && cmd) {
		s32 cmdindex = 0;

		while ((u8)PD_BE32(cmd[0]) != OBJTYPE_END) {
			if (cmdindex == wantindex) {
				return cmd;
			}

			cmd = cmd + setupGetCmdLength(cmd);
			cmdindex++;
		}
	}

	return NULL;
}

s32 setupGetCmdIndexByTag(struct tag *tag)
{
	u32 *cmd = g_StageSetup.props;

	if (cmd) {
		s32 cmdindex = 0;

		while ((u8)PD_BE32(cmd[0]) != OBJTYPE_END) {
			if ((struct tag *)cmd == tag) {
				return cmdindex;
			}

			cmd = cmd + setupGetCmdLength(cmd);
			cmdindex++;
		}
	}

	return -1;
}

u32 setupGetCmdIndexByProp(struct prop *prop)
{
	u32 *cmd = g_StageSetup.props;

	if (cmd) {
		s32 cmdindex = 0;

		while ((u8)PD_BE32(cmd[0]) != OBJTYPE_END) {
			if ((struct prop *)cmd[5] == prop) {
				return cmdindex;
			}

			cmd = cmd + setupGetCmdLength(cmd);
			cmdindex++;
		}
	}

	return -1;
}

/* S483b (2026-04-27): walk a freshly-loaded modeldef's tree and probe
 * each node's rodata pointer for readability.  Logs a per-modelnum
 * summary plus a per-bad-node MODEL.RODATA.MISS warning so we can
 * correlate corrupt-load events with the runtime AVs.  Mike's
 * hypothesis (catalog-data-missing) for the 04:21 LVTICK 1836 crash
 * is that this kind of post-load probe will surface partial loads
 * before a tick ever touches them.  The walker iterates exactly the
 * same way modelUpdateRelations / modelUpdateRelationsQuick do
 * (rootnode -> child -> next -> back to parent->next) so every node
 * the tick code can reach gets probed once at load time. */
/* Indexed type-count buckets covering the full single-byte type range
 * (MODELNODETYPE_DL = 0x18 is the highest used by tree walkers). */
#define SETUP_RODATA_TYPE_BUCKETS 0x20

static void setupValidateModeldefRodata(s32 modelnum, struct modeldef *modeldef, u16 fileid, const char *model_id)
{
	struct modelnode *node;
	struct modelnode *root;
	u32 total_nodes = 0;
	u32 bad_rodata = 0;
	u32 type_counts[SETUP_RODATA_TYPE_BUCKETS];
	u32 i;

	if (modeldef == NULL || modeldef->rootnode == NULL) {
		return;
	}

	for (i = 0; i < SETUP_RODATA_TYPE_BUCKETS; i++) {
		type_counts[i] = 0;
	}

	root = modeldef->rootnode;
	node = root;
	while (node) {
		u32 type = node->type & 0xff;
		u32 needed = 0;

		total_nodes++;
		if (type < SETUP_RODATA_TYPE_BUCKETS) {
			type_counts[type]++;
		}

		/* Pick the rodata-variant size to probe.  We only probe variants
		 * the relations-update tree dereferences directly; DL nodes and
		 * skeletal nodes are skipped because their tick paths read
		 * different fields with their own guards. */
		switch (type) {
		case MODELNODETYPE_DISTANCE:
			needed = sizeof(struct modelrodata_distance);
			break;
		case MODELNODETYPE_REORDER:
			needed = sizeof(struct modelrodata_reorder);
			break;
		case MODELNODETYPE_TOGGLE:
			needed = sizeof(struct modelrodata_toggle);
			break;
		case MODELNODETYPE_BBOX:
			needed = sizeof(struct modelrodata_bbox);
			break;
		default:
			needed = 0;
			break;
		}

		if (needed > 0) {
			if (!modelRodataIsReadable(node->rodata, needed)) {
				bad_rodata++;
				modelRodataLogMiss("LoadValidate", modeldef, node, node->rodata, needed,
						node->rodata == NULL ? "NULL" : "page-unmapped");
			}
		}

		/* Tree walk: descend into children, then siblings, then back up. */
		if (node->child) {
			node = node->child;
		} else {
			while (node) {
				if (node == root && node->next == NULL) {
					node = NULL;
					break;
				}
				if (node->next) {
					node = node->next;
					break;
				}
				node = node->parent;
			}
		}
	}

	if (bad_rodata > 0) {
		sysLogPrintf(LOG_WARNING,
				"MODEL.RODATA.LOAD: PARTIAL modelnum=%d fileid=0x%04x id=%s nodes=%u bad=%u "
				"reorder=%u distance=%u toggle=%u bbox=%u dl=%u -- ticks of this model will skip bad nodes",
				modelnum, (unsigned)fileid,
				model_id ? model_id : "<unknown>",
				total_nodes, bad_rodata,
				type_counts[MODELNODETYPE_REORDER],
				type_counts[MODELNODETYPE_DISTANCE],
				type_counts[MODELNODETYPE_TOGGLE],
				type_counts[MODELNODETYPE_BBOX],
				type_counts[MODELNODETYPE_DL]);
	}
}

static s32 setupModelHandlePassesSourceOnlyCheck(s32 modelnum, const char *model_id, asset_data_handle_t handle)
{
	assetSourceDebugFatalHandleFallback(ASSET_MODEL,
			"setup modeldef", model_id, handle);

	if (!assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, handle)) {
		return true;
	}

	sysLogPrintf(LOG_WARNING,
			"SETUP.MODELDEF: source-only model '%s' modelnum=%d refused non-public model handle",
			model_id ? model_id : "(null)", modelnum);
	return false;
}

bool setupLoadModeldef(s32 modelnum)
{
	s32 source_filenum;
	const char *model_id;
	asset_data_handle_t model_handle;
	catalog_model_result_t model_result;
	s32 have_model_result;

	/* Ensure this model is tracked in the active SP asset manifest.
	 * manifestEnsureLoaded is a no-op in MP mode or before the manifest is
	 * built, so this guard is unconditionally safe.  All prop/weapon/hat/
	 * projectile model loads funnel through this function, making it the
	 * canonical chokepoint for MANIFEST_TYPE_MODEL tracking. */
	have_model_result = catalogResolveModelByModelnum(modelnum, &model_result);
	model_id = have_model_result ? model_result.entry->id : NULL;
	if (model_id) {
		manifestEnsureLoaded(model_id, MANIFEST_TYPE_MODEL);
	}

	if (g_ModelStates[modelnum].modeldef == NULL) {
		if (!have_model_result) {
			sysLogPrintf(LOG_WARNING, "FIX-B.2: modelnum %d missing catalog entry -- entity skipped",
				modelnum);
			return false;
		}

		model_handle = model_result.handle;
		source_filenum = model_result.filenum;
		if (!setupModelHandlePassesSourceOnlyCheck(modelnum, model_id, model_handle)) {
			return false;
		}
		g_ModelStates[modelnum].modeldef =
			modeldefLoadToNewFromHandle(model_handle, source_filenum);

		/* FIX-B.2: graceful fallback for missing prop/weapon models.
		 * modeldefLoadToNew() returns NULL when the file is absent from ROM
		 * data (e.g., a mod asset not present in the manifest, or a catalog
		 * gap).  Calling modelAllocateRwData(NULL) would immediately crash
		 * at modeldef->rwdatalen.  Instead, log a warning and return false
		 * so the caller skips entity initialisation for this modelnum.
		 * This converts a hard crash into a missing prop -- visually wrong
		 * but diagnosable. */
		if (g_ModelStates[modelnum].modeldef == NULL) {
			sysLogPrintf(LOG_WARNING, "FIX-B.2: modelnum %d (fileid=0x%04x, id=%s) failed to load -- entity skipped",
				modelnum, (unsigned)source_filenum,
				model_id ? model_id : "<unknown>");
			return false;
		}

		modelAllocateRwData(g_ModelStates[modelnum].modeldef);

		/* S483b (2026-04-27): probe the freshly-loaded model's rodata tree
		 * once and warn if any node is unreadable.  See
		 * setupValidateModeldefRodata above. */
		setupValidateModeldefRodata(modelnum, g_ModelStates[modelnum].modeldef,
				(u16)source_filenum, model_id);

		return true;
	}

	return false;
}

bool setupGetObjBbox(struct defaultobj *obj, struct coord *pos, f32 realrot[3][3], struct coord *arg3, struct coord *arg4)
{
	struct modelrodata_bbox *bbox = objFindBboxRodata(obj);

	if (bbox != NULL) {
		arg3->x = pos->x + objGetRotatedLocalXMinByMtx3(bbox, realrot);
		arg3->y = pos->y + objGetRotatedLocalYMinByMtx3(bbox, realrot);
		arg3->z = pos->z + objGetRotatedLocalZMinByMtx3(bbox, realrot);

		arg4->x = pos->x + objGetRotatedLocalXMaxByMtx3(bbox, realrot);
		arg4->y = pos->y + objGetRotatedLocalYMaxByMtx3(bbox, realrot);
		arg4->z = pos->z + objGetRotatedLocalZMaxByMtx3(bbox, realrot);

		return true;
	}

	return false;
}

bool setup0f092304(struct defaultobj *obj, struct coord *arg1, struct coord *arg2)
{
	return setupGetObjBbox(obj, &obj->prop->pos, obj->realrot, arg1, arg2);
}

void setup0f09233c(struct defaultobj *obj, struct coord *pos, f32 realrot[3][3], RoomNum *rooms)
{
	struct coord a;
	struct coord b;
	u32 stack;

	if (setupGetObjBbox(obj, pos, realrot, &a, &b)) {
		a.x -= 1;
		a.y -= 1;
		a.z -= 1;
		b.x += 1;
		b.y += 1;
		b.z += 1;

		bgFindEnteredRooms(&a, &b, rooms, 7, false);
	}
}

void setup0f0923d4(struct defaultobj *obj)
{
	propDeregisterRooms(obj->prop);
	setup0f09233c(obj, &obj->prop->pos, obj->realrot, obj->prop->rooms);
	propRegisterRooms(obj->prop);
}

struct defaultobj *setupGetObjByCmdIndex(u32 cmdindex)
{
	u32 *cmd = setupGetCmdByIndex(cmdindex);

	if (cmd) {
		switch ((u8)PD_BE32(cmd[0])) {
		case OBJTYPE_DOOR:
		case OBJTYPE_BASIC:
		case OBJTYPE_KEY:
		case OBJTYPE_ALARM:
		case OBJTYPE_CCTV:
		case OBJTYPE_AMMOCRATE:
		case OBJTYPE_WEAPON:
		case OBJTYPE_SINGLEMONITOR:
		case OBJTYPE_MULTIMONITOR:
		case OBJTYPE_HANGINGMONITORS:
		case OBJTYPE_AUTOGUN:
		case OBJTYPE_DEBRIS:
		case OBJTYPE_HAT:
		case OBJTYPE_MULTIAMMOCRATE:
		case OBJTYPE_SHIELD:
		case OBJTYPE_GASBOTTLE:
		case OBJTYPE_TRUCK:
		case OBJTYPE_HELI:
		case OBJTYPE_29:
		case OBJTYPE_GLASS:
		case OBJTYPE_SAFE:
		case OBJTYPE_TANK:
		case OBJTYPE_TINTEDGLASS:
		case OBJTYPE_LIFT:
		case OBJTYPE_HOVERBIKE:
		case OBJTYPE_HOVERPROP:
		case OBJTYPE_FAN:
		case OBJTYPE_HOVERCAR:
		case OBJTYPE_CHOPPER:
		case OBJTYPE_ESCASTEP:
			return (struct defaultobj *)cmd;
		case OBJTYPE_DOORSCALE:
		case OBJTYPE_CHR:
		case OBJTYPE_LINKGUNS:
		case OBJTYPE_GRENADEPROB:
		case OBJTYPE_LINKLIFTDOOR:
		case OBJTYPE_TAG:
		case OBJTYPE_BEGINOBJECTIVE:
		case OBJTYPE_ENDOBJECTIVE:
		case OBJECTIVETYPE_DESTROYOBJ:
		case OBJECTIVETYPE_COMPFLAGS:
		case OBJECTIVETYPE_FAILFLAGS:
		case OBJECTIVETYPE_COLLECTOBJ:
		case OBJECTIVETYPE_THROWOBJ:
		case OBJECTIVETYPE_HOLOGRAPH:
		case OBJECTIVETYPE_1F:
		case OBJECTIVETYPE_ENTERROOM:
		case OBJECTIVETYPE_THROWINROOM:
		case OBJTYPE_22:
		case OBJTYPE_BRIEFING:
		case OBJTYPE_RENAMEOBJ:
		case OBJTYPE_PADLOCKEDDOOR:
		case OBJTYPE_SAFEITEM:
		case OBJTYPE_CAMERAPOS:
		case OBJTYPE_CONDITIONALSCENERY:
		case OBJTYPE_BLOCKEDPATH:
		case OBJTYPE_PADEFFECT:
			return NULL;
		}
	}

	return (struct defaultobj *)cmd;
}

/**
 * Find an object in the setup file of little importance that can be reused for
 * something of greater importance.
 *
 * For example, if a weapon is being dropped on screen and there are no extra
 * weapon structs left, this function is used to find another weapon elsewhere
 * in the stage so it can be repurposed.
 *
 * The caller can specify if the obj must already have a prop or model. The
 * caller sets these to true if the game is already at the prop or model limit.
 *
 * If an unused object can be found then it will be returned. Otherwise, for
 * non-weapons, the function will return NULL and write some candidates to
 * offscreenobjptr and anyobjptr. offscreenobjptr will be an object that is
 * offscreen, while anyobjptr will be any object.
 *
 * In reality this function is only called with objtypes weapon and hat, and the
 * hat caller is unreachable because hats don't exist in PD. So it's only used
 * for weapons which means the candidate logic isn't used.
 */
struct defaultobj *setupFindObjForReuse(s32 wanttype, struct defaultobj **offscreenobjptr, struct defaultobj **anyobjptr, bool musthaveprop, bool musthavemodel, struct modeldef *modeldef)
{
	struct defaultobj *offscreenobj = NULL;
	struct defaultobj *anyobj = NULL;

	u32 *cmd = g_StageSetup.props;

	if (cmd) {
		while ((u8)PD_BE32(cmd[0]) != OBJTYPE_END) {
			if ((wanttype & 0xff) == (u8)PD_BE32(cmd[0])) {
				struct defaultobj *obj = (struct defaultobj *)cmd;

				if (obj->prop == NULL) {
					if (!musthaveprop && !musthavemodel) {
						return obj;
					}
				} else if (wanttype != OBJTYPE_WEAPON
						&& (obj->hidden & OBJHFLAG_PROJECTILE) == 0
						&& (obj->hidden2 & OBJH2FLAG_CANREGEN) == 0
#if VERSION >= VERSION_NTSC_1_0
						&& (obj->flags & OBJFLAG_HELDROCKET) == 0
#endif
						&& obj->prop->parent == NULL
						&& (!musthavemodel || modelmgrCanSlotFitRwdata(obj->model, modeldef))) {
					if (offscreenobj == NULL && (obj->prop->flags & (PROPFLAG_ONTHISSCREENTHISTICK | PROPFLAG_ONANYSCREENTHISTICK | PROPFLAG_ONANYSCREENPREVTICK)) == 0) {
						offscreenobj = obj;
					}

					if (anyobj == NULL) {
						anyobj = obj;
					}
				}
			}

			cmd = cmd + setupGetCmdLength(cmd);
		}
	}

	*offscreenobjptr = offscreenobj;
	*anyobjptr = anyobj;

	return NULL;
}
