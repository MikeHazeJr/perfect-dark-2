/**
 * forge_runtime.c -- The Grid runtime instantiation layer.
 *
 * On forgeRuntimeEnterPlay (FREEFLY -> NORMAL):
 *   - SPAWN_POINT objects   -> injected into global spawn pool via
 *                              spawnPoolAppendForgePoints().
 *   - AI objects            -> allocated via botmgrAllocateBot() using
 *                              catalog-resolved body/head mp-indices.
 *   - Bot-tab requests      -> consumed per-tick in forgeRuntimeTick().
 *   - WEAPON_PAD objects    -> spawned as live weaponobj via weaponCreate /
 *                              func0f08ae0c + setup0f0923d4.
 *   - PROP / GEOMETRY       -> spawned as live defaultobj via objInit +
 *                              setup0f0923d4.  Collision is auto-generated
 *                              from the model bbox by objInit.
 *                              Note: full door lifecycle (open/close via
 *                              doorobj pool) deferred; DOOR catalog entries
 *                              spawn visually as static props.
 *   - ZONE objects          -> registered in s_zone_rt[]; intersection
 *                              checked per-tick for enter/exit events.
 *
 * On forgeRuntimeExitPlay (NORMAL -> FREEFLY):
 *   - Forge bots removed (botmgrRemoveAll).
 *   - Spawn pool rebuilt without forge-injected points.
 *   - Any directly-allocated props freed via propFree.
 *   - Zone runtime cleared.
 */

#include "forge/forge_runtime.h"
#include "forge/forge_core.h"

#include <string.h>
#include <math.h>

#include "system.h"
#include "assetcatalog.h"
#include "asset_source_debug.h"

#include "game/botmgr.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/modeldef.h"
#include "game/setuputils.h"
#include "game/spawnpool.h"
#include "game/bg.h"
#include "lib/meshcollision.h"
#include "lib/model.h"
#include "lib/memp.h"

#include "bss.h"
#include "data.h"
#include "types.h"
#include "constants.h"

/* P7 (2026-04-24): Grid Playtest HUD bot-runtime wire-up.
 *
 * g_BotUpdatesDisabled is defined in src/game/bot.c and has no public header
 * today; forward-declare locally so forgeRuntimeTick can mirror the Playtest
 * HUD's Freeze All toggle into the existing F6 freeze machinery.  When the
 * toggle is on, bot.c's botTick zeros speedmultforwards/sideways each frame
 * AND keeps chrTick running so models stay rendered (B-217 v2 fix). */
extern s32 g_BotUpdatesDisabled;

/* ================================================================
 * Module state
 * ================================================================ */

static forge_prop_handle_t s_handles[FORGE_RUNTIME_MAX_HANDLES];
static s32                 s_handle_count;
static s32                 s_active;
static s32                 s_spawn_injected; /* number of points added to pool */
static s32                 s_bots_spawned;   /* running forge-bot count */

/* Prop pool: defaultobj instances for forge-placed props/geometry.
 * Allocated once from MEMPOOL_STAGE (survives FREEFLY<->NORMAL toggles,
 * wiped only when the stage unloads). */
#define FORGE_PROP_POOL_SIZE 64
static struct defaultobj *s_prop_pool;
static s32                s_prop_count;

/* Door pool: doorobj instances for forge-placed interactive doors.
 * Allocated once from MEMPOOL_STAGE alongside the prop pool. */
#define FORGE_DOOR_POOL_SIZE  16
#define FORGE_DOOR_SLIDE_DIST 100.0f  /* world-units the door slides when fully open */

static struct doorobj *s_door_pool;
static s32             s_door_count;

static s32 s_forgeModelHandlePassesSourceOnlyCheck(asset_type_e type,
        const char *catalog_id, const char *context, asset_data_handle_t handle)
{
    assetSourceDebugFatalHandleFallback(type, context, catalog_id, handle);

    if (!assetSourceDebugHandleRequiresPublicFileSource(type, handle)) {
        return 1;
    }

    sysLogPrintf(LOG_WARNING,
            "GRID.RUNTIME: source-only %s '%s' refused non-public model handle",
            assetSourceDebugTypeLabel(type),
            catalog_id ? catalog_id : "(null)");
    return 0;
}

/* Zone runtime: per-zone state for player intersection checks. */
#define FORGE_ZONE_RT_MAX 128
typedef struct {
    u32  forge_uid;
    u8   type;          /* forge_zone_type_t */
    u8   shape;         /* forge_zone_shape_t */
    u8   team_filter;
    u8   enabled;
    f32  pos[3];
    f32  half[3];       /* box half-extents; half[0] = sphere radius for sphere */
    u32  teleport_uid;
    char channel_on_enter[FORGE_NAME_LEN];
    char channel_on_exit[FORGE_NAME_LEN];
    f32  damage_per_sec;
    u8   was_inside;    /* edge-trigger state for this player */
    u8   pad[3];
} forge_zone_rt_t;

static forge_zone_rt_t s_zone_rt[FORGE_ZONE_RT_MAX];
static s32             s_zone_count;

/* ================================================================
 * Handle registry helpers
 * ================================================================ */

static forge_prop_handle_t *s_alloc(u32 forge_uid)
{
    if (s_handle_count >= FORGE_RUNTIME_MAX_HANDLES) {
        sysLogPrintf(LOG_WARNING, "GRID.RUNTIME: handle pool full (max=%d)",
                FORGE_RUNTIME_MAX_HANDLES);
        return NULL;
    }
    forge_prop_handle_t *h = &s_handles[s_handle_count++];
    memset(h, 0, sizeof(*h));
    h->forge_uid = forge_uid;
    return h;
}

/* ================================================================
 * Bot config helpers
 * ================================================================ */

/*
 * Find an unused g_BotConfigsArray slot.  A slot is "free" when
 * body_id is empty -- the match-setup path fills it before use and
 * leaves it non-empty until the next full stage reset.
 * Walks from high indices so we don't stomp on match-setup-reserved
 * low slots.
 */
static s32 s_findFreeBotSlot(void)
{
    for (s32 i = MAX_BOTS - 1; i >= 0; --i) {
        if (g_BotConfigsArray[i].base.body_id[0] == '\0') {
            return i;
        }
    }
    return -1;
}

/*
 * Fill a g_BotConfigsArray slot with body/head/name derived from the
 * forge AI object.  Uses catalog mp_index (the established pattern from
 * matchsetup.c) for the legacy mpbodynum/mpheadnum fields that
 * botmgrAllocateBot still reads.
 */
static void s_fillBotSlot(s32 slot, const forge_object_t *o)
{
    struct mpbotconfig *bc  = &g_BotConfigsArray[slot];
    struct mpchrconfig *cfg = &bc->base;

    memset(bc, 0, sizeof(*bc));

    /* --- body --- */
    const char *body_id = (o->props.ai.body_id[0] != '\0')
                          ? o->props.ai.body_id
                          : "base:body_dd_guard";
    strncpy(cfg->body_id, body_id, sizeof(cfg->body_id) - 1);
    {
        const asset_entry_t *be = assetCatalogResolve(body_id);
        if (be && be->mp_index >= 0) {
            cfg->mpbodynum = (u8)be->mp_index;
        }
        /* mp_index == -1 falls through with mpbodynum == 0 (Dark Combat default). */
    }

    /* --- head --- */
    const char *head_id = (o->props.ai.head_id[0] != '\0')
                          ? o->props.ai.head_id
                          : "base:head_dd_guard";
    strncpy(cfg->head_id, head_id, sizeof(cfg->head_id) - 1);
    {
        const asset_entry_t *he = assetCatalogResolve(head_id);
        if (he && he->mp_index >= 0) {
            cfg->mpheadnum = (u8)he->mp_index;
        }
    }

    /* --- name (15 chars max including null) --- */
    if (o->label[0] != '\0') {
        strncpy(cfg->name, o->label, 14);
        cfg->name[14] = '\0';
    } else {
        strncpy(cfg->name, "Grid Bot", 14);
    }

    /* --- team --- */
    cfg->team = (u8)(o->team < 4 ? o->team : 0);

    /* --- difficulty: hostile=hard, neutral=normal, friendly=easy --- */
    switch (o->props.ai.faction) {
    case 1:  bc->difficulty = BOTDIFF_HARD;   break;
    case 2:  bc->difficulty = BOTDIFF_NORMAL; break;
    default: bc->difficulty = BOTDIFF_EASY;   break;
    }
}

/*
 * Attempt to allocate one AI forge object as a live bot.
 * Returns the aibotnum (slot) on success, -1 on failure.
 *
 * P7 (2026-04-24): return the slot so callers (e.g. the Playtest HUD bot
 * spawner) can teleport the newly live bot to a specific world position
 * via s_teleportBotNearPlayer().  Previously returned 0/1 for
 * success/failure which discarded that identity.
 */
static s32 s_spawnBot(const forge_object_t *o)
{
    s32 slot = s_findFreeBotSlot();
    if (slot < 0) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: no free bot config slot for AI uid=%u '%s'",
                o->uid, o->catalog_id);
        return -1;
    }

    s_fillBotSlot(slot, o);

    /* chrnum: use the current bot count as the sequential ID, matching how
     * setup.c allocates simulants (each gets a unique chrnum 0, 1, 2...). */
    s32 chrnum = (s32)g_BotCount;

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: spawn bot uid=%u slot=%d chrnum=%d body='%s' at (%.0f,%.0f,%.0f)",
            o->uid, slot, chrnum,
            g_BotConfigsArray[slot].base.body_id,
            o->pos[0], o->pos[1], o->pos[2]);

    botmgrAllocateBot(chrnum, slot);

    forge_prop_handle_t *h = s_alloc(o->uid);
    if (h) h->is_bot = 1;

    ++s_bots_spawned;
    return slot;
}

/*
 * P7 (2026-04-24): teleport a just-spawned bot to the player's current
 * position offset by `radius` units along the player's forward vector.
 * Safe to call any time after botmgrAllocateBot populates g_MpBotChrPtrs
 * for that slot.  Returns 1 on success, 0 on miss (bot not live yet /
 * no player / pointer issue).
 *
 * Math mirrors forgemode.c::forgeUpdateFreefly: PD yaw 0 points down +Z
 * and increases CW from above, so forward = (sin(yaw), 0, cos(yaw)).
 * We only offset on the XZ plane so the bot lands at the player's eye
 * height; gravity + collision resolve from there.
 */
static s32 s_teleportBotNearPlayer(s32 aibotnum, f32 radius)
{
    if (aibotnum < 0 || aibotnum >= MAX_BOTS) return 0;
    if (!g_Vars.currentplayer || !g_Vars.currentplayer->prop) return 0;

    struct chrdata *bchr = g_MpBotChrPtrs[aibotnum];
    if (!bchr || !bchr->prop) return 0;

    const struct player *p = g_Vars.currentplayer;
    const f32 yaw_rad = p->vv_theta * 0.017453292519943f;
    const f32 fwd_x   = sinf(yaw_rad);
    const f32 fwd_z   = cosf(yaw_rad);

    /* Clamp radius so a misconfigured slider can't park the bot at the
     * bottom of the skybox.  100..5000u mirrors the HUD slider range. */
    if (radius < 100.0f)  radius = 100.0f;
    if (radius > 5000.0f) radius = 5000.0f;

    bchr->prop->pos.x = p->prop->pos.x + fwd_x * radius;
    bchr->prop->pos.y = p->prop->pos.y;
    bchr->prop->pos.z = p->prop->pos.z + fwd_z * radius;

    /* Facing direction polish deferred: chr->yvisang is a u8 (256 steps
     * over 360deg) with a specific PD convention; the bot's own AI tick
     * will reorient on its next frame once it sees the player anyway.
     * Position matters more than orientation for Spawn Near Me -- the
     * bot walks toward whichever enemy is nearest, which is the player
     * by construction. */

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: Spawn Near Me -- teleported aibotnum=%d to "
            "(%.0f,%.0f,%.0f) (radius=%.0f from player yaw=%.0fdeg)",
            aibotnum,
            bchr->prop->pos.x, bchr->prop->pos.y, bchr->prop->pos.z,
            radius, p->vv_theta);
    return 1;
}

/* ================================================================
 * Prop / weapon spawn helpers
 * ================================================================ */

/*
 * Ensure the MEMPOOL_STAGE prop pool exists.  Called once per session;
 * the allocation persists until the stage unloads.
 */
static void s_ensure_prop_pool(void)
{
    if (s_prop_pool) return;
    s_prop_pool = (struct defaultobj *)mempAlloc(
            FORGE_PROP_POOL_SIZE * sizeof(struct defaultobj), MEMPOOL_STAGE);
    if (!s_prop_pool) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: failed to alloc prop pool (%d slots)",
                FORGE_PROP_POOL_SIZE);
    }
}

static void s_ensure_door_pool(void)
{
    if (s_door_pool) return;
    s_door_pool = (struct doorobj *)mempAlloc(
            FORGE_DOOR_POOL_SIZE * sizeof(struct doorobj), MEMPOOL_STAGE);
    if (!s_door_pool) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: failed to alloc door pool (%d slots)",
                FORGE_DOOR_POOL_SIZE);
    }
}

/* Returns 1 if catalog_id identifies a forge interactive door. */
static s32 s_is_forge_door(const char *catalog_id)
{
    return strstr(catalog_id, ":door_") != NULL;
}

/*
 * Spawn a FORGE_CAT_INTERACTABLE door as a live doorobj.
 * Allocates from s_door_pool, initialises physics defaults,
 * calls objInitWithModelDef so the engine ticks the door via
 * doorTick each frame.  OPEN/CLOSE logic actions call
 * doorsRequestMode via forgeRuntimeFindDoorByUid.
 */
static void s_spawn_door(const forge_object_t *o)
{
    if (!s_door_pool) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: door pool not available for uid=%u", o->uid);
        return;
    }
    if (s_door_count >= FORGE_DOOR_POOL_SIZE) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: door pool full -- uid=%u '%s' dropped",
                o->uid, o->catalog_id);
        return;
	}

	catalog_prop_result_t pr;
	if (!catalogResolveProp(o->catalog_id, &pr) || assetHandleIsNull(pr.handle)) {
		sysLogPrintf(LOG_WARNING,
				"GRID.RUNTIME: door uid=%u -- cannot resolve '%s'",
				o->uid, o->catalog_id);
        return;
    }

    if (!s_forgeModelHandlePassesSourceOnlyCheck(ASSET_PROP, o->catalog_id,
            "forge door modeldef", pr.handle)) {
        return;
    }

    struct modeldef *modeldef = modeldefLoadToNewFromHandle(pr.handle, pr.filenum);
    if (!modeldef) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: door uid=%u -- modeldefLoadToNewFromHandle(%d) failed",
                o->uid, pr.filenum);
        return;
    }

    struct doorobj *door = &s_door_pool[s_door_count];
    memset(door, 0, sizeof(*door));

    door->base.type       = OBJTYPE_DOOR;
    door->base.maxdamage  = 1000;
    door->base.extrascale = 256;
    door->base.floorcol   = 0x0fff;
    if (o->collision_mode != FORGE_COLLISION_SOLID) {
        door->base.flags3 |= OBJFLAG3_WALKTHROUGH;
    }

    /* Standard physics (matches a typical CI Training sliding door) */
    door->maxfrac       = 0.9f;
    door->perimfrac     = 0.3f;
    door->accel         = 0.003f;
    door->decel         = 0.003f;
    door->maxspeed      = 0.08f;
    door->autoclosetime = 300;   /* 5 s at 60 Hz */
    door->portalnum     = -1;    /* no portal -- forge doors are interior */
    door->sibling       = NULL;
    door->frac          = 0.0f;
    door->fracspeed     = 0.0f;
    door->mode          = DOORMODE_IDLE;
    door->fadealpha     = 255;

    /* Choose doortype and slide vector from the forge open_dir */
    const forge_door_props_t *dp = &o->props.door;
    switch ((forge_door_dir_t)dp->open_dir) {

    case FORGE_DOOR_SLIDE_UP:
        door->doortype = DOORTYPE_VERTICAL;
        door->unk98.x  = 0.0f;
        door->unk98.y  = FORGE_DOOR_SLIDE_DIST;
        door->unk98.z  = 0.0f;
        break;

    case FORGE_DOOR_SLIDE_LEFT: {
        f32 yaw = o->rot[1] * (3.14159265f / 180.0f);
        door->doortype = DOORTYPE_SLIDING;
        door->unk98.x  = -FORGE_DOOR_SLIDE_DIST * cosf(yaw);
        door->unk98.y  = 0.0f;
        door->unk98.z  = -FORGE_DOOR_SLIDE_DIST * sinf(yaw);
        break;
    }

    case FORGE_DOOR_SLIDE_RIGHT: {
        f32 yaw = o->rot[1] * (3.14159265f / 180.0f);
        door->doortype = DOORTYPE_SLIDING;
        door->unk98.x  = FORGE_DOOR_SLIDE_DIST * cosf(yaw);
        door->unk98.y  = 0.0f;
        door->unk98.z  = FORGE_DOOR_SLIDE_DIST * sinf(yaw);
        break;
    }

    case FORGE_DOOR_SWING:
    default:
        /* Swing mode not yet supported -- treat as horizontal slide */
        door->doortype = DOORTYPE_SLIDING;
        door->unk98.x  = FORGE_DOOR_SLIDE_DIST;
        door->unk98.y  = 0.0f;
        door->unk98.z  = 0.0f;
        break;
    }

    /* DOORFLAG_0080: sliding-family types use unk98 for the position offset */
    switch (door->doortype) {
    case DOORTYPE_SLIDING:
    case DOORTYPE_VERTICAL:
    case DOORTYPE_LASER:
    case DOORTYPE_FALLAWAY:
        door->doorflags |= DOORFLAG_0080;
        break;
    default:
        break;
    }

    if (dp->auto_close) {
        door->doorflags |= DOORFLAG_AUTOMATIC;
    }

    struct prop *prop = objInitWithModelDef(&door->base, modeldef);
    if (!prop) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: door uid=%u -- objInitWithModelDef failed", o->uid);
        return;
    }

    ++s_door_count;

    /* objInit sets prop->type = PROPTYPE_OBJ; promote to PROPTYPE_DOOR so
     * the prop tick dispatcher calls doorTick each frame. */
    prop->type      = PROPTYPE_DOOR;

    prop->pos.x     = o->pos[0];
    prop->pos.y     = o->pos[1];
    prop->pos.z     = o->pos[2];

    door->startpos.x = prop->pos.x;
    door->startpos.y = prop->pos.y;
    door->startpos.z = prop->pos.z;

    /* Identity orientation -- forge doors are axis-aligned for now */
    memset(door->base.realrot, 0, sizeof(door->base.realrot));
    door->base.realrot[0][0] = 1.0f;
    door->base.realrot[1][1] = 1.0f;
    door->base.realrot[2][2] = 1.0f;

    if (door->base.model) {
        modelSetScale(door->base.model, 1.0f);
    }
    if (o->collision_mode == FORGE_COLLISION_SOLID) {
        meshAttachModelToProp(prop, door->base.model);
    } else {
        meshDetachFromProp(prop);
    }

    propActivate(prop);
    propEnable(prop);
    setup0f0923d4(&door->base);

    forge_prop_handle_t *h = s_alloc(o->uid);
    if (h) {
        h->prop    = prop;
        h->doorobj = door;
    }

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: spawned door uid=%u '%s' type=%d at (%.0f,%.0f,%.0f)",
            o->uid, o->catalog_id, door->doortype,
            o->pos[0], o->pos[1], o->pos[2]);
}

/*
 * Spawn a FORGE_CAT_WEAPON_PAD as a live weapon pickup.
 * Resolves the weapon catalog ID, creates a weaponobj, positions it,
 * and registers it with the room system.
 */
static void s_spawn_weapon_pad(const forge_object_t *o)
{
    if (!o->props.weapon.weapon_id[0]) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: weapon pad uid=%u has no weapon_id", o->uid);
        return;
    }

	catalog_weapon_result_t wr;
	if (!catalogResolveWeapon(o->props.weapon.weapon_id, &wr)
			|| wr.weapon_num < 0
			|| assetHandleIsNull(wr.handle)) {
		sysLogPrintf(LOG_WARNING,
				"GRID.RUNTIME: weapon pad uid=%u -- cannot resolve '%s'",
				o->uid, o->props.weapon.weapon_id);
        return;
    }

    struct weaponobj *weapon = weaponCreate(0, 0, NULL);
    if (!weapon) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: weapon pad uid=%u -- no free weapon slot", o->uid);
        return;
    }

    weapon->weaponnum = (s32)wr.weapon_num;

    if (!s_forgeModelHandlePassesSourceOnlyCheck(ASSET_WEAPON,
            o->props.weapon.weapon_id, "forge weapon pad modeldef", wr.handle)) {
        return;
    }

    struct modeldef *modeldef = modeldefLoadToNewFromHandle(wr.handle, wr.filenum);
    if (!modeldef) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: weapon pad uid=%u -- modeldefLoadToNewFromHandle(%d) failed",
                o->uid, wr.filenum);
        return;
    }

    struct prop *prop = func0f08ae0c(weapon, modeldef);
    if (!prop) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: weapon pad uid=%u -- func0f08ae0c failed", o->uid);
        return;
    }

    /* func0f08ae0c uses g_ModelStates[0].scale which is 0 for
     * dynamically-loaded models -- override to 1:1. */
    if (weapon->base.model) {
        modelSetScale(weapon->base.model, 1.0f);
    }

    prop->pos.x = o->pos[0];
    prop->pos.y = o->pos[1];
    prop->pos.z = o->pos[2];

    /* Identity rotation (model-local to world). */
    memset(weapon->base.realrot, 0, sizeof(weapon->base.realrot));
    weapon->base.realrot[0][0] = 1.0f;
    weapon->base.realrot[1][1] = 1.0f;
    weapon->base.realrot[2][2] = 1.0f;

    /* Ammo uses weapon-default values on pickup; custom ammo setting deferred. */

    propActivate(prop);
    propEnable(prop);
    setup0f0923d4(&weapon->base);

    forge_prop_handle_t *h = s_alloc(o->uid);
    if (h) h->prop = prop;

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: spawned weapon pad uid=%u '%s' wnum=%d at (%.0f,%.0f,%.0f)",
            o->uid, o->props.weapon.weapon_id, wr.weapon_num,
            o->pos[0], o->pos[1], o->pos[2]);
}

/*
 * Spawn a FORGE_CAT_PROP or FORGE_CAT_GEOMETRY as a live static prop.
 * Allocates a defaultobj from the stage pool, initializes it with the
 * resolved model, positions it, and registers it with the room system.
 * Collision is auto-generated from the model bbox by objInit.
 *
 * DOOR catalog entries also route here -- full door lifecycle (doorobj
 * pool, open/close state) is deferred to a later session.
 */
static void s_spawn_prop(const forge_object_t *o)
{
    if (!s_prop_pool) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: prop pool not available for uid=%u", o->uid);
        return;
    }
    if (s_prop_count >= FORGE_PROP_POOL_SIZE) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: prop pool full -- uid=%u '%s' dropped",
                o->uid, o->catalog_id);
        return;
    }

	catalog_prop_result_t pr;
	if (!catalogResolveProp(o->catalog_id, &pr) || assetHandleIsNull(pr.handle)) {
		sysLogPrintf(LOG_WARNING,
				"GRID.RUNTIME: prop uid=%u -- cannot resolve '%s'",
				o->uid, o->catalog_id);
        return;
    }

    if (!s_forgeModelHandlePassesSourceOnlyCheck(ASSET_PROP, o->catalog_id,
            "forge prop modeldef", pr.handle)) {
        return;
    }

    struct modeldef *modeldef = modeldefLoadToNewFromHandle(pr.handle, pr.filenum);
    if (!modeldef) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: prop uid=%u -- modeldefLoadToNewFromHandle(%d) failed",
                o->uid, pr.filenum);
        return;
    }

    struct defaultobj *obj = &s_prop_pool[s_prop_count];
    memset(obj, 0, sizeof(*obj));
    obj->type       = OBJTYPE_BASIC;
    obj->maxdamage  = 1000;
    obj->floorcol   = 0x0fff;
    obj->extrascale = 256;
    if (o->collision_mode != FORGE_COLLISION_SOLID) {
        obj->flags3 |= OBJFLAG3_WALKTHROUGH;
    }

    struct prop *prop = objInit(obj, modeldef, NULL, NULL);
    if (!prop) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: prop uid=%u -- objInit failed", o->uid);
        return;
    }

    ++s_prop_count;

    /* objInit scales from g_ModelStates[obj->modelnum]; override for
     * dynamically-loaded models where modelnum==0 may be unset. */
    if (obj->model) {
        modelSetScale(obj->model, 1.0f);
    }
    if (o->collision_mode == FORGE_COLLISION_SOLID) {
        meshAttachModelToProp(prop, obj->model);
    } else {
        meshDetachFromProp(prop);
    }

    prop->pos.x = o->pos[0];
    prop->pos.y = o->pos[1];
    prop->pos.z = o->pos[2];

    /* Identity rotation. */
    memset(obj->realrot, 0, sizeof(obj->realrot));
    obj->realrot[0][0] = 1.0f;
    obj->realrot[1][1] = 1.0f;
    obj->realrot[2][2] = 1.0f;

    propActivate(prop);
    propEnable(prop);
    setup0f0923d4(obj);

    forge_prop_handle_t *h = s_alloc(o->uid);
    if (h) h->prop = prop;

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: spawned prop uid=%u '%s' at (%.0f,%.0f,%.0f)",
            o->uid, o->catalog_id, o->pos[0], o->pos[1], o->pos[2]);
}

/*
 * Register a FORGE_CAT_ZONE into the zone runtime array for per-tick
 * intersection checks.
 */
static void s_register_zone(const forge_object_t *o)
{
    if (s_zone_count >= FORGE_ZONE_RT_MAX) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: zone pool full -- uid=%u dropped", o->uid);
        return;
    }

    const forge_zone_props_t *zp = &o->props.zone;
    forge_zone_rt_t *z = &s_zone_rt[s_zone_count++];
    memset(z, 0, sizeof(*z));

    z->forge_uid    = o->uid;
    z->type         = zp->type;
    z->shape        = zp->shape;
    z->team_filter  = zp->team_filter;
    z->enabled      = 1;
    z->pos[0]       = o->pos[0];
    z->pos[1]       = o->pos[1];
    z->pos[2]       = o->pos[2];
    z->half[0]      = zp->size[0];
    z->half[1]      = zp->size[1];
    z->half[2]      = zp->size[2];
    z->teleport_uid = zp->teleport_target_uid;
    z->damage_per_sec = zp->damage_per_sec;

    strncpy(z->channel_on_enter, zp->channel_on_enter,
            sizeof(z->channel_on_enter) - 1);
    strncpy(z->channel_on_exit, zp->channel_on_exit,
            sizeof(z->channel_on_exit) - 1);

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: registered zone uid=%u type=%d shape=%d at (%.0f,%.0f,%.0f) half=(%.1f,%.1f,%.1f)",
            o->uid, zp->type, zp->shape,
            o->pos[0], o->pos[1], o->pos[2],
            zp->size[0], zp->size[1], zp->size[2]);
}

/* ================================================================
 * Public API
 * ================================================================ */

void forgeRuntimeEnterPlay(void)
{
    if (s_active) forgeRuntimeExitPlay();

    s_handle_count   = 0;
    s_spawn_injected = 0;
    s_bots_spawned   = 0;
    s_prop_count     = 0;
    s_door_count     = 0;
    s_zone_count     = 0;
    s_active         = 1;

    s_ensure_prop_pool();
    s_ensure_door_pool();

    s32 total = forgeObjectCount();
    if (total == 0) {
        sysLogPrintf(LOG_NOTE,
                "GRID.RUNTIME: enterPlay -- no objects placed, nothing to instantiate");
        return;
    }

    /* Collect forge spawn points for bulk injection after the walk. */
    struct coord spawn_pos[SPAWNPOOL_MAX];
    f32          spawn_facing[SPAWNPOOL_MAX];
    s32          n_spawns    = 0;
    s32          n_bots      = 0;
    s32          n_props     = 0;
    s32          n_weapons   = 0;
    s32          n_zones     = 0;
    s32          n_deferred  = 0;

    for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
        forge_object_t *o = forgeObjectGet(i);
        if (!o || !o->in_use) continue;
        if (!o->enabled) continue;

        switch ((forge_category_t)o->category) {

        case FORGE_CAT_SPAWN_POINT:
            if (n_spawns < SPAWNPOOL_MAX) {
                spawn_pos[n_spawns].x  = o->pos[0];
                spawn_pos[n_spawns].y  = o->pos[1];
                spawn_pos[n_spawns].z  = o->pos[2];
                spawn_facing[n_spawns] = o->props.spawn.facing_deg * 0.017453292519943f;
                ++n_spawns;

                forge_prop_handle_t *h = s_alloc(o->uid);
                if (h) h->is_spawn_point = 1;
            } else {
                sysLogPrintf(LOG_WARNING,
                        "GRID.RUNTIME: spawn point uid=%u dropped -- pool full",
                        o->uid);
            }
            break;

        case FORGE_CAT_AI:
            {
                /* AUDIT-24-M8 (2026-04-25): s_spawnBot returns -1 on failure
                 * (no free bot slot) and the slot index (>= 0) on success.
                 * `n_bots += slot` would silently subtract on failure AND on
                 * a success returning slot=0, doubly wrong as a counter.
                 * Increment by 1 only on success, log on failure (s_spawnBot
                 * already logs the warning internally so don't double-log). */
                s32 slot = s_spawnBot(o);
                if (slot >= 0) {
                    ++n_bots;
                }
            }
            break;

        case FORGE_CAT_WEAPON_PAD:
            s_spawn_weapon_pad(o);
            ++n_weapons;
            break;

        case FORGE_CAT_PROP:
        case FORGE_CAT_GEOMETRY:
            s_spawn_prop(o);
            ++n_props;
            break;

        case FORGE_CAT_ZONE:
            s_register_zone(o);
            ++n_zones;
            break;

        case FORGE_CAT_INTERACTABLE:
            ++n_props;
            if (s_is_forge_door(o->catalog_id)) {
                s_spawn_door(o);
            } else {
                /* Non-door interactables: visual presence via static prop;
                 * interaction logic (switches, terminals, lifts) deferred. */
                s_spawn_prop(o);
                ++n_deferred;
                sysLogPrintf(LOG_NOTE,
                        "GRID.RUNTIME: interactable '%s' uid=%u -- spawned visual; interaction deferred",
                        o->catalog_id, o->uid);
            }
            break;

        default:
            break;
        }
    }

    /* Inject collected spawn points into the global pool. */
    if (n_spawns > 0) {
        s_spawn_injected = spawnPoolAppendForgePoints(spawn_pos, spawn_facing, n_spawns);
        sysLogPrintf(LOG_NOTE,
                "GRID.RUNTIME: injected %d/%d forge spawn points into pool",
                s_spawn_injected, n_spawns);
    }

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: enterPlay -- spawns=%d bots=%d weapons=%d props=%d zones=%d deferred=%d",
            n_spawns, n_bots, n_weapons, n_props, n_zones, n_deferred);
}

void forgeRuntimeExitPlay(void)
{
    if (!s_active) return;
    s_active = 0;

    /* Free any directly-allocated props (not bots, not spawn points). */
    s32 freed = 0;
    for (s32 i = 0; i < s_handle_count; ++i) {
        forge_prop_handle_t *h = &s_handles[i];
        if (h->prop && !h->is_bot && !h->is_spawn_point) {
            propFree(h->prop);
            h->prop = NULL;
            ++freed;
        }
    }

    /* Remove all forge-spawned bots.  In forge mode every bot is editor-
     * managed; they'll be recreated the next time the author enters NORMAL. */
    if (s_bots_spawned > 0) {
        botmgrRemoveAll();
        sysLogPrintf(LOG_NOTE,
                "GRID.RUNTIME: removed %d forge bots via botmgrRemoveAll",
                s_bots_spawned);
    }

    /* Rebuild the spawn pool without forge-injected points. */
    if (s_spawn_injected > 0) {
        forge_map_settings_t *ms = forgeMapSettings();
        const char *stage_id = (ms && ms->base_stage_id[0] != '\0')
                               ? ms->base_stage_id
                               : "base:training";
        s32 needed = (g_MpNumChrs > 0) ? g_MpNumChrs : 8;
        spawnPoolBuildGlobal(stage_id, 0, needed);
        sysLogPrintf(LOG_NOTE,
                "GRID.RUNTIME: spawn pool rebuilt (forge points flushed)");
    }

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: exitPlay -- freed=%d bots_removed=%d spawns_flushed=%d zones_cleared=%d doors_cleared=%d",
            freed, s_bots_spawned, s_spawn_injected, s_zone_count, s_door_count);

    s_handle_count   = 0;
    s_spawn_injected = 0;
    s_bots_spawned   = 0;
    s_prop_count     = 0;
    s_door_count     = 0;
    s_zone_count     = 0;
}

void forgeRuntimeTick(void)
{
    if (!s_active) return;

    forge_bot_settings_t *bs = forgeBotSettings();

    /* P7: mirror Freeze All into the existing F6 bot-updates-disabled flag.
     * g_BotUpdatesDisabled is the established mechanism (src/game/bot.c);
     * setting it per-frame from the HUD toggle means bots keep rendering
     * via chrTick but movement intent is zeroed (B-217 v2 semantics).
     * Kept as an unconditional sync so the HUD remains authoritative;
     * the dev F6 keybind and the HUD share the same underlying state. */
    g_BotUpdatesDisabled = bs->all_frozen ? 1 : 0;

    /* Consume pending add-active (fighting) bot requests from the HUD /
     * Bots-tab.  P7: after each spawn, apply the current spawn_mode to
     * the newly live bot (Spawn Near Me -> teleport to player forward;
     * Any / Smart -> leave at the scenario-picked pad). */
    while (bs->pending_add_active > 0) {
        --bs->pending_add_active;

        /* Build a synthetic forge_object_t from the tab's default body setting.
         * The uid is non-zero synthetic (won't collide with the forge pool). */
        forge_object_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.uid = 0xFEED0000u | (u32)(s_bots_spawned & 0xFFFF);
        tmp.category = FORGE_CAT_AI;
        tmp.enabled  = 1;
        tmp.team     = 0;
        strncpy(tmp.props.ai.body_id, bs->default_body_id[0] != '\0'
                    ? bs->default_body_id : "base:body_dd_guard",
                FORGE_ID_LEN - 1);
        strncpy(tmp.props.ai.head_id, "base:head_dd_guard", FORGE_ID_LEN - 1);
        tmp.props.ai.faction = 1; /* hostile */
        s32 slot = s_spawnBot(&tmp);
        if (slot >= 0) {
            bs->active_count++;
            if (bs->spawn_mode == FORGE_BOT_SPAWN_NEAR_ME) {
                s_teleportBotNearPlayer(slot, bs->near_me_radius);
            }
            /* FORGE_BOT_SPAWN_SMART: the difficulty selection inside
             * s_fillBotSlot already biases to HARD on hostile bots;
             * s_fillBotSlot reads bs->smart_aggression as a future
             * hook.  Any = no post-spawn tweak. */
        }
    }

    /* Consume pending add-frozen bot requests.  Frozen bots are meant to
     * hold their spawn spot for placement validation, so the freeze flag
     * is set per-bot via faction=2 (neutral) today; combined with the
     * global Freeze All toggle, users can stand up a quiet group of
     * targets to stress-test cover / LoS. */
    while (bs->pending_add_frozen > 0) {
        --bs->pending_add_frozen;
        forge_object_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.uid = 0xFEED8000u | (u32)(s_bots_spawned & 0xFFFF);
        tmp.category = FORGE_CAT_AI;
        tmp.enabled  = 1;
        strncpy(tmp.props.ai.body_id, bs->default_body_id[0] != '\0'
                    ? bs->default_body_id : "base:body_dd_guard",
                FORGE_ID_LEN - 1);
        strncpy(tmp.props.ai.head_id, "base:head_dd_guard", FORGE_ID_LEN - 1);
        tmp.props.ai.faction = 2; /* neutral */
        s32 slot = s_spawnBot(&tmp);
        if (slot >= 0) {
            bs->frozen_count++;
            if (bs->spawn_mode == FORGE_BOT_SPAWN_NEAR_ME) {
                s_teleportBotNearPlayer(slot, bs->near_me_radius);
            }
        }
    }

    /* Consume remove-all request. */
    if (bs->pending_remove_all > 0) {
        bs->pending_remove_all = 0;
        if (s_bots_spawned > 0) {
            botmgrRemoveAll();
            sysLogPrintf(LOG_NOTE,
                    "GRID.RUNTIME: Bots-tab removeAll -- cleared %d forge bots",
                    s_bots_spawned);
            s_bots_spawned = 0;

            /* Compact handle registry -- drop bot entries. */
            for (s32 i = 0; i < s_handle_count; ) {
                if (s_handles[i].is_bot) {
                    s_handles[i] = s_handles[--s_handle_count];
                } else {
                    ++i;
                }
            }
        }
        bs->active_count = 0;
        bs->frozen_count = 0;
    }

    /* Zone intersection checks for the current player. */
    if (s_zone_count > 0 && g_Vars.currentplayer && g_Vars.currentplayer->prop) {
        struct coord *ppos = &g_Vars.currentplayer->prop->pos;

        for (s32 zi = 0; zi < s_zone_count; ++zi) {
            forge_zone_rt_t *z = &s_zone_rt[zi];
            if (!z->enabled) continue;

            s32 inside = 0;

            if ((forge_zone_shape_t)z->shape == FORGE_ZONE_SHAPE_SPHERE) {
                f32 dx = ppos->x - z->pos[0];
                f32 dy = ppos->y - z->pos[1];
                f32 dz = ppos->z - z->pos[2];
                f32 r  = z->half[0];
                inside = (dx*dx + dy*dy + dz*dz) < (r * r);
            } else {
                /* Box: half-extents in z->half[0/1/2]. */
                inside = (ppos->x >= z->pos[0] - z->half[0]) &&
                         (ppos->x <= z->pos[0] + z->half[0]) &&
                         (ppos->y >= z->pos[1] - z->half[1]) &&
                         (ppos->y <= z->pos[1] + z->half[1]) &&
                         (ppos->z >= z->pos[2] - z->half[2]) &&
                         (ppos->z <= z->pos[2] + z->half[2]);
            }

            /* Edge-triggered enter. */
            if (inside && !z->was_inside) {
                z->was_inside = 1;

                if (z->channel_on_enter[0]) {
                    forgeChannelSet(z->channel_on_enter, 1);
                }

                if ((forge_zone_type_t)z->type == FORGE_ZONE_TELEPORTER
                        && z->teleport_uid) {
                    forge_object_t *target = forgeObjectFindByUid(z->teleport_uid);
                    if (target) {
                        g_Vars.currentplayer->prop->pos.x = target->pos[0];
                        g_Vars.currentplayer->prop->pos.y = target->pos[1];
                        g_Vars.currentplayer->prop->pos.z = target->pos[2];
                    }
                }

                forgeLogicFireEvent(FORGE_OP_ON_PLAYER_ENTER, z->forge_uid);
            }

            /* Edge-triggered exit. */
            if (!inside && z->was_inside) {
                z->was_inside = 0;

                if (z->channel_on_exit[0]) {
                    forgeChannelSet(z->channel_on_exit, 1);
                }

                forgeLogicFireEvent(FORGE_OP_ON_PLAYER_EXIT, z->forge_uid);
            }
        }
    }
}

struct prop *forgeRuntimeFindPropByUid(u32 uid)
{
    if (!uid) return NULL;
    for (s32 i = 0; i < s_handle_count; ++i) {
        if (s_handles[i].forge_uid == uid) return s_handles[i].prop;
    }
    return NULL;
}

struct doorobj *forgeRuntimeFindDoorByUid(u32 uid)
{
    if (!uid) return NULL;
    for (s32 i = 0; i < s_handle_count; ++i) {
        if (s_handles[i].forge_uid == uid) return s_handles[i].doorobj;
    }
    return NULL;
}

void forgeRuntimeSpawnBotAt(u32 forge_uid)
{
    forge_object_t *o = forgeObjectFindByUid(forge_uid);
    if (!o) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: spawnBotAt uid=%u -- object not found", forge_uid);
        return;
    }
    s_spawnBot(o);
}
