#include <string.h>
#include <ultra64.h>
#include "constants.h"
#include "testscenarios.h"
#include "game/cheats.h"
#include "game/game_00b820.h"
#include "game/setup.h"
#include "game/objectives.h"
#include "game/playerreset.h"
#include "game/spawnpool.h"
#include "options_forced.h" /* INV-4: matchOptionsForceBit for B-181 fallback */
#include "game/botmgr.h"
#include "game/bot.h"
#include "game/chr.h"
#include "game/chraction.h"
#include "game/chrmgr.h"
#include "game/body.h"
#include "game/prop.h"
#include "game/setuputils.h"
#include "game/atan2f.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/bg.h"
#include "game/stagetable.h"
#include "game/file.h"
#include "game/lv.h"
#include "game/forgemode.h"
#include "game/mplayer/scenarios.h"
#include "game/challenge.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/mplayer/participant.h"
#include "game/pad.h"
#include "game/propobj.h"
#include "bss.h"
#include "lib/args.h"
#include "lib/memp.h"
#include "lib/model.h"
#include "lib/path.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "lib/ailist.h"
#include "lib/anim.h"
#include "lib/collision.h"
#include "data.h"
#include "types.h"
#include "system.h"
#include "assetcatalog.h"
#include "assetload.h"
#include "asset_source_debug.h"
#include "scenario_source_runtime.h"
#include "net/matchsetup.h"

/* Phase 3: lang manifest tracking (port/src/langmanifest.c) */
void langManifestRecordBank(s32 bank);

s32 g_SetupCurMpLocation;
static s32 s_SetupMpWeaponLocationCount;
static s32 s_SetupMpCreatedWeaponCount;

#define SETUP_TINY_MODE_EXTRA_SPACING 48.0f
#define SETUP_TINY_MODE_OFFSET_ATTEMPTS 8

static void setupRequireScenarioSourceHandle(const char *context,
	const catalog_stage_result_t *stage, asset_data_handle_t handle)
{
	const char *stageid = "?";

	if (stage && stage->entry && stage->entry->id[0]) {
		stageid = stage->entry->id;
	}

	assetSourceDebugFatalHandleFallback(ASSET_SCENARIO, context, stageid,
		handle);
}

static s32 setupIntroCommandWords(s32 type)
{
	static const u8 sizes[] = {
		3,  /* INTROCMD_SPAWN */
		4,  /* INTROCMD_WEAPON */
		4,  /* INTROCMD_AMMO */
		8,  /* INTROCMD_3 */
		2,  /* INTROCMD_4 */
		2,  /* INTROCMD_OUTFIT */
		10, /* INTROCMD_6 */
		3,  /* INTROCMD_WATCHTIME */
		2,  /* INTROCMD_CREDITOFFSET */
		3,  /* INTROCMD_CASE */
		3,  /* INTROCMD_CASERESPAWN */
		2,  /* INTROCMD_HILL */
		1,  /* INTROCMD_END */
	};

	if (type < 0 || type > INTROCMD_END) {
		return 0;
	}

	return sizes[type];
}

static bool setupIntroCommandsAreValid(const struct stagesetup *setup,
	s32 loaded_size, const s32 *intro, const u32 *props)
{
	const uintptr_t base = (uintptr_t)setup;
	const uintptr_t intro_addr = (uintptr_t)intro;
	const uintptr_t props_addr = (uintptr_t)props;
	const uintptr_t end_addr = base + (uintptr_t)loaded_size;
	const s32 *cmd = intro;

	if (!setup || !intro || loaded_size < (s32)sizeof(*setup) ||
			intro_addr < base || intro_addr + sizeof(s32) > end_addr) {
		return false;
	}

	for (s32 safety = 0; safety < 10000; safety++) {
		s32 type;
		s32 words;
		uintptr_t next_addr;

		if ((uintptr_t)cmd + sizeof(s32) > end_addr) {
			return false;
		}

		type = cmd[0];
		words = setupIntroCommandWords(type);
		if (words <= 0) {
			return false;
		}

		next_addr = (uintptr_t)cmd + (uintptr_t)words * sizeof(s32);
		if (next_addr > end_addr) {
			return false;
		}
		if (props_addr > intro_addr && next_addr > props_addr) {
			return false;
		}
		if (type == INTROCMD_END) {
			return true;
		}

		cmd += words;
	}

	return false;
}

struct tvscreen var80061a80 = {
	g_TvCmdlist00, // cmdlist
	0,           // offset
	0xffff,      // pause60
	0,           // tconfig
	0,           // rot
	1,           // xscale
	0,           // xscalefrac
	0,           // xscaleinc
	1,           // xscaleold
	1,           // xscalenew
	1,           // yscale
	0,           // yscalefrac
	0,           // yscaleinc
	1,           // yscaleold
	1,           // yscalenew
	0.5,         // xmid
	0,           // xmidfrac
	0,           // xmidinc
	0.5,         // xmidold
	0.5,         // xmidnew
	0.5,         // ymid
	0,           // ymidfrac
	0,           // ymidinc
	0.5,         // ymidold
	0.5,         // ymidnew
	0xff,        // red
	0xff,        // redold
	0xff,        // rednew
	0xff,        // green
	0xff,        // greenold
	0xff,        // greennew
	0xff,        // blue
	0xff,        // blueold
	0xff,        // bluenew
	0xff,        // alpha
	0xff,        // alphaold
	0xff,        // alphanew
	1,           // colfrac
	0,           // colinc
};

struct tvscreen var80061af4 = {
	var8006aaa0, // cmdlist
	0,           // offset
	0xffff,      // pause60
	0,           // tconfig
	0,           // rot
	1,           // xscale
	0,           // xscalefrac
	0,           // xscaleinc
	1,           // xscaleold
	1,           // xscalenew
	1,           // yscale
	0,           // yscalefrac
	0,           // yscaleinc
	1,           // yscaleold
	1,           // yscalenew
	0.5,         // xmid
	0,           // xmidfrac
	0,           // xmidinc
	0.5,         // xmidold
	0.5,         // xmidnew
	0.5,         // ymid
	0,           // ymidfrac
	0,           // ymidinc
	0.5,         // ymidold
	0.5,         // ymidnew
	0xff,        // red
	0xff,        // redold
	0xff,        // rednew
	0xff,        // green
	0xff,        // greenold
	0xff,        // greennew
	0xff,        // blue
	0xff,        // blueold
	0xff,        // bluenew
	0xff,        // alpha
	0xff,        // alphaold
	0xff,        // alphanew
	1,           // colfrac
	0,           // colinc
};

struct tvscreen var80061b68 = {
	var8006aae4, // cmdlist
	0,           // offset
	0xffff,      // pause60
	0,           // tconfig
	0,           // rot
	1,           // xscale
	0,           // xscalefrac
	0,           // xscaleinc
	1,           // xscaleold
	1,           // xscalenew
	1,           // yscale
	0,           // yscalefrac
	0,           // yscaleinc
	1,           // yscaleold
	1,           // yscalenew
	0.5,         // xmid
	0,           // xmidfrac
	0,           // xmidinc
	0.5,         // xmidold
	0.5,         // xmidnew
	0.5,         // ymid
	0,           // ymidfrac
	0,           // ymidinc
	0.5,         // ymidold
	0.5,         // ymidnew
	0xff,        // red
	0xff,        // redold
	0xff,        // rednew
	0xff,        // green
	0xff,        // greenold
	0xff,        // greennew
	0xff,        // blue
	0xff,        // blueold
	0xff,        // bluenew
	0xff,        // alpha
	0xff,        // alphaold
	0xff,        // alphanew
	1,           // colfrac
	0,           // colinc
};

u32 var80061bdc = 0x00000000;
f32 g_DoorScale = 1;
u32 var80061be4 = 0x00000000;
u32 var80061be8 = 0x00000000;
u32 var80061bec = 0x00000000;

void propsReset(void)
{
	s32 i;

	/* S311-followup (2026-05-01): clear g_InteractProp on stage reset.
	 *
	 * g_InteractProp is a global pointer to whichever interactable prop
	 * is currently in the player's reticle. propFindForInteract resets
	 * it to NULL at the top of every per-frame call (prop.c:1602), but
	 * that per-frame call happens AFTER bmoveTick in the lvTickPlayer
	 * pipeline. On the FIRST frame after a stage transition the prop
	 * pool has been freed (mempResetPool(STAGE)) but g_InteractProp
	 * still points to whichever prop was the player's interact target
	 * in the PREVIOUS stage -- now a dangling pointer to freed memory.
	 *
	 * bmoveProcessInput -> propGetActionUseHoldThresholdMs ->
	 * propInteractPromptLabel reads `prop->obj->type` (prop.c:1735) and
	 * crashes on the dereference. The prop->obj NULL check at prop.c:1724
	 * passes because obj happens to be non-zero garbage in the freed slot.
	 *
	 * Reproduced in Mike's playtest log (`0c9d7d73-pdclient.log`) on the
	 * first frame of stage 0x47 (mp_grid) entered from Main Menu after
	 * being on stage 0x26 (CI Training). Crash signature: PC +0x130d99
	 * in propInteractPromptLabel.
	 *
	 * The fix is to clear g_InteractProp here so the FIRST bmoveTick of
	 * the new stage sees NULL and the propInteractPromptLabel NULL guard
	 * works correctly. propFindForInteract still resets it every
	 * subsequent frame as before. No defensive null-check shim added in
	 * the read sites -- the actual broken state (stale global) gets
	 * fixed at the source. */
	g_InteractProp = NULL;

	for (i = 0; i < ARRAYCOUNT(g_Lifts); i++) {
		g_Lifts[i] = NULL;
	}

	/* PC port: Increase object pool limits for 32 simultaneous characters.
	 * Original N64 values were tuned for 4 players max. With 32 chars
	 * all fighting, weapon drops and projectiles can easily exceed the
	 * original limits. */
	g_MaxWeaponSlots = 100;
	g_MaxHatSlots = 20;
	g_MaxAmmoCrates = 40;
	g_MaxDebrisSlots = 30;
	g_MaxProjectiles = 200;
	g_MaxEmbedments = 160;

	if (STAGE_IS_SYSTEM(g_Vars.stagenum)) {
		g_MaxWeaponSlots = 0;
		g_MaxHatSlots = 0;
		g_MaxAmmoCrates = 0;
		g_MaxDebrisSlots = 0;
		g_MaxProjectiles = 0;
		g_MaxEmbedments = 0;
	}

	setupReset0f00cc8c();
	setupResetProxyMines();

	g_AlarmTimer = 0;
	g_AlarmAudioHandle = NULL;
	g_AlarmSpeakerWeight = 64;

	g_GasReleaseTimer240 = 0;
	g_GasReleasing = false;
	g_GasPos.x = 0;
	g_GasPos.y = 0;
	g_GasPos.z = 0;
	g_GasLastCough60 = 0;
	g_GasSoundTimer240 = 0;
	g_GasAudioHandle = NULL;

	g_CountdownTimerOff = COUNTDOWNTIMERREASON_AI;
	g_CountdownTimerRunning = false;
	g_CountdownTimerValue60 = 0;

	g_PlayersDetonatingMines = 0;
	g_TintedGlassEnabled = false;

	if (g_MaxWeaponSlots == 0) {
		g_WeaponSlots = NULL;
	} else {
		g_WeaponSlots = mempAlloc(ALIGN16(g_MaxWeaponSlots * sizeof(struct weaponobj)), MEMPOOL_STAGE);

		for (i = 0; i < g_MaxWeaponSlots; i++) {
			g_WeaponSlots[i].base.prop = NULL;
		}

		g_NextWeaponSlot = 0;
	}

	if (g_MaxHatSlots == 0) {
		g_HatSlots = NULL;
	} else {
		g_HatSlots = mempAlloc(ALIGN16(g_MaxHatSlots * sizeof(struct hatobj)), MEMPOOL_STAGE);

		for (i = 0; i < g_MaxHatSlots; i++) {
			g_HatSlots[i].base.prop = NULL;
		}

		g_NextHatSlot = 0;
	}

	if (g_MaxAmmoCrates == 0) {
		g_AmmoCrates = NULL;
	} else {
		g_AmmoCrates = mempAlloc(ALIGN16(g_MaxAmmoCrates * sizeof(struct ammocrateobj)), MEMPOOL_STAGE);

		for (i = 0; i < g_MaxAmmoCrates; i++) {
			g_AmmoCrates[i].base.prop = NULL;
		}
	}

	if (g_MaxDebrisSlots == 0) {
		g_DebrisSlots = NULL;
	} else {
		g_DebrisSlots = mempAlloc(ALIGN16(g_MaxDebrisSlots * sizeof(struct defaultobj)), MEMPOOL_STAGE);

		for (i = 0; i < g_MaxDebrisSlots; i++) {
			g_DebrisSlots[i].prop = NULL;
		}
	}

	if (g_MaxProjectiles == 0) {
		g_Projectiles = NULL;
	} else {
		g_Projectiles = mempAlloc(ALIGN16(g_MaxProjectiles * sizeof(struct projectile)), MEMPOOL_STAGE);

		for (i = 0; i < g_MaxProjectiles; i++) {
			g_Projectiles[i].flags = PROJECTILEFLAG_FREE;
		}
	}

	if (g_MaxEmbedments == 0) {
		g_Embedments = NULL;
	} else {
		g_Embedments = mempAlloc(ALIGN16(g_MaxEmbedments * sizeof(struct embedment)), MEMPOOL_STAGE);

		for (i = 0; i < g_MaxEmbedments; i++) {
			g_Embedments[i].flags = EMBEDMENTFLAG_FREE;
		}
	}

	g_LiftDoors = NULL;
	g_PadlockedDoors = NULL;
	g_SafeItems = NULL;
	g_LinkedScenery = NULL;
	g_BlockedPaths = NULL;

	g_EmbedProp = NULL;
	g_EmbedHitPart = -1;
	g_CctvWaitScale = 1;
	g_CctvDamageRxScale = 1;
	g_AutogunAccuracyScale = 1;
	g_AutogunDamageTxScale = 1;
	g_AutogunDamageRxScale = 1;
	g_AmmoQuantityScale = 1;

	g_MaxThrownLaptops = g_Vars.normmplayerisrunning ? 12 : PLAYERCOUNT();

	g_ThrownLaptops = mempAlloc(ALIGN16(g_MaxThrownLaptops * sizeof(struct autogunobj)), MEMPOOL_STAGE);
	g_ThrownLaptopBeams = mempAlloc(ALIGN16(g_MaxThrownLaptops * sizeof(struct beam)), MEMPOOL_STAGE);

	for (i = 0; i < g_MaxThrownLaptops; i++) {
		g_ThrownLaptops[i].base.prop = NULL;
	}
}

void setupCreateLiftDoor(struct linkliftdoorobj *link)
{
	link->next = g_LiftDoors;
	g_LiftDoors = link;
}

void setupCreatePadlockedDoor(struct padlockeddoorobj *link)
{
	link->next = g_PadlockedDoors;
	g_PadlockedDoors = link;
}

void setupCreateSafeItem(struct safeitemobj *link)
{
	link->next = g_SafeItems;
	g_SafeItems = link;
}

void setupCreateConditionalScenery(struct linksceneryobj *link)
{
	link->next = g_LinkedScenery;
	g_LinkedScenery = link;
}

void setupCreateBlockedPath(struct blockedpathobj *blockedpath)
{
	blockedpath->next = g_BlockedPaths;
	g_BlockedPaths = blockedpath;
}

void setupReset0f00cc8c(void)
{
	struct tvscreen tmp1;
	struct tvscreen tmp2;
	struct tvscreen tmp3;

	tmp1 = var80061a80;
	var8009ce98 = tmp1;

	tmp2 = var80061af4;
	var8009cf10 = tmp2;

	tmp3 = var80061b68;
	var8009cf88 = tmp3;
}

void setupResetProxyMines(void)
{
	s32 i;

	for (i = 0; i < ARRAYCOUNT(g_Proxies); i++) {
		g_Proxies[i] = NULL;
	}
}

s32 setupCountCommandType(u32 type)
{
	struct defaultobj *obj = (struct defaultobj *)g_StageSetup.props;
	s32 count = 0;

	if (obj) {
		while (obj->type != OBJTYPE_END) {
			if (obj->type == (u8)type) {
				count++;
			}

			obj = (struct defaultobj *)((u32 *)obj + setupGetCmdLength((u32 *)obj));
		}
	}

	return count;
}

static s32 setupCountTinyModeExtraChrs(void)
{
	struct defaultobj *obj = (struct defaultobj *)g_StageSetup.props;
	s32 count = 0;

	if (obj) {
		while (obj->type != OBJTYPE_END) {
			if (obj->type == OBJTYPE_CHR) {
				count += bodyTinyModeExtraChrCountForPacked((struct packedchr *)obj);
			}

			obj = (struct defaultobj *)((u32 *)obj + setupGetCmdLength((u32 *)obj));
		}
	}

	return count;
}

static void setupTinyModeGetSpawnBasis(const struct packedchr *packed, f32 *forwardx, f32 *forwardz, f32 *sidex, f32 *sidez)
{
	struct pad pad;
	f32 len;

	padUnpack(packed->padnum, PADFIELD_LOOK, &pad);

	*forwardx = pad.look.x;
	*forwardz = pad.look.z;

	len = sqrtf(*forwardx * *forwardx + *forwardz * *forwardz);

	if (len > 0.001f) {
		*forwardx /= len;
		*forwardz /= len;
	} else {
		*forwardx = 0.0f;
		*forwardz = 1.0f;
	}

	*sidex = *forwardz;
	*sidez = -*forwardx;
}

static f32 setupTinyModeThetaFromChr(struct chrdata *chr)
{
	f32 theta = 360.0f - BADRAD2DEG(chrGetInverseTheta(chr));

	while (theta >= 360.0f) {
		theta -= 360.0f;
	}

	while (theta < 0.0f) {
		theta += 360.0f;
	}

	return theta;
}

static bool setupTinyModeSpawnPosIsOpen(struct chrdata *chr, struct coord *pos, RoomNum *rooms)
{
	f32 radius;
	f32 ymax;

	if (chr == NULL || rooms == NULL) {
		return false;
	}

	radius = chr->radius > 4.0f ? chr->radius : 4.0f;
	ymax = chr->height > 24.0f ? chr->height : 24.0f;

	return cdTestVolume(pos, radius, rooms, CDTYPE_ALL, CHECKVERTICAL_YES, ymax + 20.0f, -20.0f) != CDRESULT_COLLISION;
}

static void setupPositionTinyModeExtraChr(struct chrdata *chr, const struct chrdata *origin, const struct packedchr *packed, s32 extraindex)
{
	static const f32 attempts[SETUP_TINY_MODE_OFFSET_ATTEMPTS][2] = {
		{  1.0f,  0.0f },
		{ -1.0f,  0.0f },
		{  0.8f,  0.8f },
		{ -0.8f,  0.8f },
		{  0.0f,  1.0f },
		{  0.0f, -1.0f },
		{  1.4f,  0.0f },
		{ -1.4f,  0.0f },
	};
	struct coord base;
	RoomNum *rooms;
	f32 forwardx;
	f32 forwardz;
	f32 sidex;
	f32 sidez;
	f32 sign;
	f32 theta;
	s32 attempt;

	if (chr == NULL || chr->prop == NULL || origin == NULL || origin->prop == NULL || packed == NULL) {
		return;
	}

	base = origin->prop->pos;
	rooms = origin->prop->rooms;
	sign = (extraindex & 1) == 0 ? 1.0f : -1.0f;
	theta = setupTinyModeThetaFromChr(chr);

	setupTinyModeGetSpawnBasis(packed, &forwardx, &forwardz, &sidex, &sidez);

	for (attempt = 0; attempt < SETUP_TINY_MODE_OFFSET_ATTEMPTS; attempt++) {
		f32 side = attempts[attempt][0] * sign;
		f32 forward = attempts[attempt][1];
		struct coord pos = base;
		bool open;

		pos.x += (sidex * side + forwardx * forward) * SETUP_TINY_MODE_EXTRA_SPACING;
		pos.z += (sidez * side + forwardz * forward) * SETUP_TINY_MODE_EXTRA_SPACING;

		open = setupTinyModeSpawnPosIsOpen(chr, &pos, rooms);

		if (open || attempt == SETUP_TINY_MODE_OFFSET_ATTEMPTS - 1) {
			chrSetPos(chr, &pos, rooms, theta, !open);
			return;
		}
	}
}

static void setupCreateTinyModeExtraChrs(s32 stagenum, const struct packedchr *packed, s32 cmdindex, struct chrdata *origin)
{
	s32 extra;
	s32 i;

	if (packed == NULL || origin == NULL) {
		return;
	}

	extra = bodyTinyModeExtraChrCountForPacked(packed);

	for (i = 0; i < extra; i++) {
		struct packedchr clone = *packed;

		clone.chrindex = -1;
		clone.chrnum = chrsGetNextUnusedChrnum();
		clone.spawnflags |= SPAWNFLAG_IGNORECOLLISION;

		setupPositionTinyModeExtraChr(bodyAllocateChr(stagenum, &clone, cmdindex), origin, packed, i);
	}
}

static bool setupResolvePropsInLoadedSetup(struct stagesetup *setup, s32 loadedsize, u32 **props)
{
	uintptr_t base;
	uintptr_t end;
	uintptr_t rawprops;
	uintptr_t propsaddr;

	if (!setup || !props || loadedsize <= (s32)sizeof(*setup)) {
		return false;
	}

	base = (uintptr_t)setup;
	end = base + (uintptr_t)loadedsize;
	rawprops = (uintptr_t)setup->props;

	if (rawprops >= base && rawprops < end) {
		propsaddr = rawprops;
	} else if (rawprops < (uintptr_t)loadedsize) {
		propsaddr = base + rawprops;
	} else {
		return false;
	}

	if (propsaddr < base + sizeof(*setup) || propsaddr + sizeof(u32) > end || (propsaddr & 3) != 0) {
		return false;
	}

	*props = (u32 *)propsaddr;
	return true;
}

static bool setupResolvePointerInLoadedSetup(struct stagesetup *setup, s32 loadedsize,
		const void *rawptr, size_t minsize, size_t align, void **out)
{
	uintptr_t base;
	uintptr_t end;
	uintptr_t raw;
	uintptr_t addr;

	if (!setup || !out || rawptr == NULL || loadedsize <= (s32)sizeof(*setup)) {
		return false;
	}

	base = (uintptr_t)setup;
	end = base + (uintptr_t)loadedsize;
	raw = (uintptr_t)rawptr;

	if (raw >= base && raw < end) {
		addr = raw;
	} else if (raw < (uintptr_t)loadedsize) {
		addr = base + raw;
	} else {
		return false;
	}

	if (addr < base + sizeof(*setup)
			|| addr + minsize > end
			|| (align > 0 && (addr & (align - 1)) != 0)) {
		return false;
	}

	*out = (void *)addr;
	return true;
}

void setupCreateObject(struct defaultobj *obj, s32 cmdindex)
{
	f32 f0;
	s32 modelnum;
	struct pad pad;
	Mtxf mtx;
	struct coord centre;
	f32 scale;
	struct coord pos;
	RoomNum rooms[8];
	struct prop *prop2;
	u32 flag40;
	u32 stack;
	struct chrdata *chr;
	struct prop *prop;

	modelnum = obj->modelnum;
	setupLoadModeldef(modelnum);

	/* B-163: FIX-B.2 returns early when modeldefLoadToNew fails (catalog miss,
	 * missing ROM file, or torn load returning parts=0), leaving
	 * g_ModelStates[modelnum].modeldef NULL.  Every downstream dereference of
	 * obj->model->scale below would then AV.  Skip creation and log so the
	 * prop shows up in diagnostics. */
	if (g_ModelStates[modelnum].modeldef == NULL) {
		sysLogPrintf(LOG_WARNING,
			"SETUP: object modelnum %d modeldef NULL after load — prop skipped",
			modelnum);
		return;
	}

	/* B-146 regression: pickup macros in props.h OR in OBJFLAG3_WALKTHROUGH
	 * so the PC auto-floor synthesizer (propobj.c:2321) skips collision tile
	 * emission on ground weapons and ammo crates.  That fix only affects
	 * macros compiled from C source — base-game map setup files are loaded
	 * as pre-compiled binary from ROM, where the flags3 byte predates the
	 * macro change and the WALKTHROUGH bit is zero.  On Grid (and any other
	 * base-game arena) ground weapons + ammo crates were still spawning with
	 * auto-generated floor tiles, making them standable.  Force the bit here
	 * at runtime for every pickup type so ROM-sourced setup data matches
	 * macro-sourced / network-sourced pickups. */
	if (obj->type == OBJTYPE_WEAPON
			|| obj->type == OBJTYPE_AMMOCRATE
			|| obj->type == OBJTYPE_MULTIAMMOCRATE) {
		obj->flags3 |= OBJFLAG3_WALKTHROUGH;
	}

	scale = obj->extrascale * (1.0f / 256.0f);

	if (g_Vars.normmplayerisrunning || g_Vars.lvmpbotlevel) {
		obj->hidden2 |= OBJH2FLAG_CANREGEN;
	}

	if (obj->flags & OBJFLAG_INSIDEANOTHEROBJ) {
		if (obj->type == OBJTYPE_WEAPON) {
			func0f08ae0c((struct weaponobj *)obj, g_ModelStates[modelnum].modeldef);
		} else {
			objInitWithModelDef(obj, g_ModelStates[modelnum].modeldef);
		}

		modelSetScale(obj->model, obj->model->scale * scale);
		return;
	}

	if (obj->flags & OBJFLAG_ASSIGNEDTOCHR) {
		chr = chrFindByLiteralId(obj->pad);

		if (chr && chr->prop && chr->model) {
			if (obj->type == OBJTYPE_WEAPON) {
				prop = func0f08ae0c((struct weaponobj *)obj, g_ModelStates[modelnum].modeldef);
			} else {
				prop = objInitWithModelDef(obj, g_ModelStates[modelnum].modeldef);
			}

			modelSetScale(obj->model, obj->model->scale * scale);
			propReparent(prop, chr->prop);
		}
	} else {
		if (obj->pad < 0) {
			if (obj->type == OBJTYPE_WEAPON) {
				func0f08ae0c((struct weaponobj *)obj, g_ModelStates[modelnum].modeldef);
			} else {
				objInitWithModelDef(obj, g_ModelStates[modelnum].modeldef);
			}

			modelSetScale(obj->model, obj->model->scale * scale);
			return;
		}

		padUnpack(obj->pad, PADFIELD_POS | PADFIELD_LOOK | PADFIELD_UP | PADFIELD_BBOX | PADFIELD_ROOM, &pad);

		if (pad.room > 0) {
			mtx00016d58(&mtx, 0, 0, 0, -pad.look.x, -pad.look.y, -pad.look.z, pad.up.x, pad.up.y, pad.up.z);

			pos.x = pad.pos.x;
			pos.y = pad.pos.y;
			pos.z = pad.pos.z;

			rooms[0] = pad.room;
			rooms[1] = -1;

			if (!padHasBboxData(obj->pad)) {
				if (obj->flags & OBJFLAG_00000002) {
					centre.x = pad.pos.x;
					centre.y = pad.pos.y;
					centre.z = pad.pos.z;
				} else {
					centre.x = pad.pos.x;
					centre.y = pad.pos.y;
					centre.z = pad.pos.z;
				}
			} else {
				padGetCentre(obj->pad, &centre);
				centre.x += (pad.bbox.ymin - pad.bbox.ymax) * 0.5f * pad.up.x;
				centre.y += (pad.bbox.ymin - pad.bbox.ymax) * 0.5f * pad.up.y;
				centre.z += (pad.bbox.ymin - pad.bbox.ymax) * 0.5f * pad.up.z;
			}

			if (obj->type == OBJTYPE_WEAPON) {
				prop2 = func0f08ae0c((struct weaponobj *)obj, g_ModelStates[modelnum].modeldef);
			} else {
				prop2 = objInitWithAutoModel(obj);
			}

			if (padHasBboxData(obj->pad)) {
				struct modelrodata_bbox *bbox = objFindBboxRodata(obj);

				if (bbox != NULL) {
					f32 xscale = 1.0f;
					f32 yscale = 1.0f;
					f32 zscale = 1.0f;
					f32 minscale;
					f32 maxscale;

					flag40 = OBJFLAG_YTOPADBOUNDS;

					if (obj->flags & OBJFLAG_XTOPADBOUNDS) {
						if (bbox->xmin < bbox->xmax) {
							if (obj->flags & OBJFLAG_00000002) {
								xscale = (pad.bbox.xmax - pad.bbox.xmin) / ((bbox->xmax - bbox->xmin) * obj->model->scale);
							} else {
								xscale = (pad.bbox.xmax - pad.bbox.xmin) / ((bbox->xmax - bbox->xmin) * obj->model->scale);
							}
						}
					}

					if (obj->flags & flag40) {
						if (bbox->ymin < bbox->ymax) {
							if (obj->flags & OBJFLAG_00000002) {
								zscale = (pad.bbox.zmax - pad.bbox.zmin) / ((bbox->ymax - bbox->ymin) * obj->model->scale);
							} else {
								yscale = (pad.bbox.ymax - pad.bbox.ymin) / ((bbox->ymax - bbox->ymin) * obj->model->scale);
							}
						}
					}

					if (obj->flags & OBJFLAG_ZTOPADBOUNDS) {
						if (bbox->zmin < bbox->zmax) {
							if (obj->flags & OBJFLAG_00000002) {
								yscale = (pad.bbox.ymax - pad.bbox.ymin) / ((bbox->zmax - bbox->zmin) * obj->model->scale);
							} else {
								zscale = (pad.bbox.zmax - pad.bbox.zmin) / ((bbox->zmax - bbox->zmin) * obj->model->scale);
							}
						}
					}

					minscale = xscale;

					if (yscale < minscale) {
						minscale = yscale;
					}

					if (zscale < minscale) {
						minscale = zscale;
					}

					maxscale = xscale;

					if (yscale > maxscale) {
						maxscale = yscale;
					}

					if (zscale > maxscale) {
						maxscale = zscale;
					}

					if ((obj->flags & OBJFLAG_XTOPADBOUNDS) == 0) {
						if (obj->flags & OBJFLAG_00000002) {
							if (bbox->xmax == bbox->xmin) {
								xscale = maxscale;
							}
						} else if (bbox->xmax == bbox->xmin) {
							xscale = maxscale;
						}
					}

					if ((u32)(obj->flags & flag40) == 0) {
						if (obj->flags & OBJFLAG_00000002) {
							if (bbox->ymax == bbox->ymin) {
								zscale = maxscale;
							}
						} else if (bbox->ymax == bbox->ymin) {
							yscale = maxscale;
						}
					}

					if ((obj->flags & OBJFLAG_ZTOPADBOUNDS) == 0) {
						if (obj->flags & OBJFLAG_00000002) {
							if (bbox->zmax == bbox->zmin) {
								yscale = maxscale;
							}
						} else if (bbox->zmax == bbox->zmin) {
							zscale = maxscale;
						}
					}

					xscale /= maxscale;
					yscale /= maxscale;
					zscale /= maxscale;

					if (xscale <= 0.000001f || yscale <= 0.000001f || zscale <= 0.000001f) {
						xscale = yscale = zscale = 1;
					}

					mtx00015e24(xscale, &mtx);
					mtx00015e80(yscale, &mtx);
					mtx00015edc(zscale, &mtx);

					modelSetScale(obj->model, obj->model->scale * maxscale);
				}
			}

			modelSetScale(obj->model, obj->model->scale * scale);
			mtx00015f04(obj->model->scale, &mtx);

			if (obj->flags2 & OBJFLAG2_DONTPAUSE) {
				prop2->flags |= PROPFLAG_DONTPAUSE;
			}

			if (obj->flags & OBJFLAG_00000002) {
				func0f06ab60(obj, &pos, &mtx, rooms, &centre);
			} else {
				func0f06a730(obj, &pos, &mtx, rooms, &centre);
			}

			if (obj->hidden & OBJHFLAG_00008000) {
				propActivateThisFrame(prop2);
			} else {
				propActivate(prop2);
			}

			propEnable(prop2);
		}
	}
}

/**
 * Assigns a weapon to its home.
 *
 * Its home is a chr's hand or a pad, as defined in the stage's setup file.
 *
 * The Marquis of Queensbury Rules (everyone unarmed) and Enemy Rockets cheats
 * are implemented here.
 */
void setupPlaceWeapon(struct weaponobj *weapon, s32 cmdindex)
{
	if (weapon->base.flags & OBJFLAG_ASSIGNEDTOCHR) {
		u32 stack[2];
		struct chrdata *chr = chrFindByLiteralId(weapon->base.pad);

		if (chr && chr->prop && chr->model) {
			if (cheatIsActive(CHEAT_MARQUIS)) {
				// NTSC 1.0 and newer simplifies the Marquis logic
#if VERSION >= VERSION_NTSC_1_0
				weapon->base.flags &= ~OBJFLAG_DEACTIVATED;
				weapon->base.flags |= OBJFLAG_WEAPON_AICANNOTUSE;
				modelmgrLoadProjectileModeldefs(weapon->weaponnum);
				func0f08b25c(weapon, chr);
#else
				if (g_Vars.stagenum == STAGE_INVESTIGATION
						&& lvGetDifficulty() == DIFF_PA
						&& weapon->weaponnum == WEAPON_K7AVENGER) {
					modelmgrLoadProjectileModeldefs(weapon->weaponnum);
					func0f08b25c(weapon, chr);
				} else if (g_Vars.stagenum == STAGE_ATTACKSHIP) {
					weapon->base.flags &= ~OBJFLAG_DEACTIVATED;
					weapon->base.flags |= OBJFLAG_WEAPON_AICANNOTUSE;
					modelmgrLoadProjectileModeldefs(weapon->weaponnum);
					func0f08b25c(weapon, chr);
				} else {
					weapon->weaponnum = WEAPON_NONE;
				}
#endif
			} else {
				if (cheatIsActive(CHEAT_ENEMYROCKETS)) {
					switch (weapon->weaponnum) {
					case WEAPON_FALCON2:
					case WEAPON_FALCON2_SILENCER:
					case WEAPON_FALCON2_SCOPE:
					case WEAPON_MAGSEC4:
					case WEAPON_MAULER:
					case WEAPON_PHOENIX:
					case WEAPON_DY357MAGNUM:
					case WEAPON_DY357LX:
					case WEAPON_CMP150:
					case WEAPON_CYCLONE:
					case WEAPON_CALLISTO:
					case WEAPON_RCP120:
					case WEAPON_LAPTOPGUN:
					case WEAPON_DRAGON:
					case WEAPON_AR34:
					case WEAPON_SUPERDRAGON:
					case WEAPON_SHOTGUN:
					case WEAPON_REAPER:
					case WEAPON_SNIPERRIFLE:
					case WEAPON_FARSIGHT:
					case WEAPON_DEVASTATOR:
					case WEAPON_ROCKETLAUNCHER:
					case WEAPON_SLAYER:
					case WEAPON_COMBATKNIFE:
					case WEAPON_CROSSBOW:
					case WEAPON_TRANQUILIZER:
					case WEAPON_GRENADE:
					case WEAPON_NBOMB:
					case WEAPON_TIMEDMINE:
					case WEAPON_PROXIMITYMINE:
					case WEAPON_REMOTEMINE:
						weapon->weaponnum = WEAPON_ROCKETLAUNCHER;
						weapon->base.modelnum = MODEL_CHRDYROCKET;
						weapon->base.extrascale = 256;
						break;
					case WEAPON_K7AVENGER:
						// Don't replace the K7 guard's weapon in Investigation
						// because it would make an objective impossible.
						// @bug: It's still replaced on PD mode difficulty.
						if (g_Vars.stagenum != STAGE_INVESTIGATION || lvGetDifficulty() != DIFF_PA) {
							weapon->weaponnum = WEAPON_ROCKETLAUNCHER;
							weapon->base.modelnum = MODEL_CHRDYROCKET;
							weapon->base.extrascale = 256;
						}
						break;
					}
				}

				modelmgrLoadProjectileModeldefs(weapon->weaponnum);
				func0f08b25c(weapon, chr);
			}
		}
	} else {
		bool createweapon = true;

		if (g_Vars.normmplayerisrunning || g_Vars.lvmpbotlevel) {
			struct mpweapon *mpweapon;
			s32 locationindex;

			g_SetupCurMpLocation = -1;

			switch (weapon->weaponnum) {
			case WEAPON_MPLOCATION00:
			case WEAPON_MPLOCATION01:
			case WEAPON_MPLOCATION02:
			case WEAPON_MPLOCATION03:
			case WEAPON_MPLOCATION04:
			case WEAPON_MPLOCATION05:
			case WEAPON_MPLOCATION06:
			case WEAPON_MPLOCATION07:
			case WEAPON_MPLOCATION08:
			case WEAPON_MPLOCATION09:
			case WEAPON_MPLOCATION10:
			case WEAPON_MPLOCATION11:
			case WEAPON_MPLOCATION12:
			case WEAPON_MPLOCATION13:
			case WEAPON_MPLOCATION14:
			case WEAPON_MPLOCATION15:
				locationindex = weapon->weaponnum - WEAPON_MPLOCATION00;
				s_SetupMpWeaponLocationCount++;
				mpweapon = mpGetMpWeaponByLocation(locationindex);
				g_SetupCurMpLocation = locationindex;
				weapon->weaponnum = mpweapon->weaponnum;
				weapon->base.modelnum = mpweapon->model;
				weapon->base.extrascale = mpweapon->extrascale;
				createweapon = mpweapon->hasweapon;

				if (mpweapon->weaponnum == WEAPON_MPSHIELD) {
					struct shieldobj *shield = (struct shieldobj *)weapon;
					shield->base.modelnum = MODEL_CHRSHIELD;
					shield->base.type = OBJTYPE_SHIELD;
					shield->base.flags |= OBJFLAG_01000000 | OBJFLAG_INVINCIBLE;
					shield->base.flags2 |= OBJFLAG2_IMMUNETOEXPLOSIONS | OBJFLAG2_IMMUNETOGUNFIRE;
					shield->initialamount = 1;
					shield->amount = 1;
					setupCreateObject(&shield->base, cmdindex);
					createweapon = false;
				}
				break;
			}
		}

		if (weapon->weaponnum != WEAPON_NONE && createweapon) {
			modelmgrLoadProjectileModeldefs(weapon->weaponnum);
			setupCreateObject(&weapon->base, cmdindex);
			if (g_Vars.normmplayerisrunning || g_Vars.lvmpbotlevel) {
				s_SetupMpCreatedWeaponCount++;
			}
		}
	}
}

void setupCreateHat(struct hatobj *hat, s32 cmdindex)
{
	if (hat->base.flags & OBJFLAG_ASSIGNEDTOCHR) {
		struct chrdata *chr = chrFindByLiteralId(hat->base.pad);

		if (chr && chr->prop && chr->model) {
			hatAssignToChr(hat, chr);
		}
	} else {
		setupCreateObject(&hat->base, cmdindex);
	}
}

void setupCreateKey(struct keyobj *key, s32 cmdindex)
{
	setupCreateObject(&key->base, cmdindex);
}

void setupCreateMine(struct mineobj *mine, s32 cmdindex)
{
	mine->base.type = OBJTYPE_WEAPON;

	setupCreateObject(&mine->base, cmdindex);

	if (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) {
		mine->base.hidden = (mine->base.hidden & 0x0fffffff) | (2 << 28);
	}

	mine->base.prop->forcetick = true;
}

void setupCreateCctv(struct cctvobj *cctv, s32 cmdindex)
{
	struct defaultobj *obj = &cctv->base;

	setupCreateObject(obj, cmdindex);

	if (cctv->lookatpadnum >= 0) {
		struct coord lenspos;
		union modelrodata *lens = modelGetPartRodata(obj->model->definition, MODELPART_CCTV_CASING);
		struct pad pad;
		f32 xdiff;
		f32 ydiff;
		f32 zdiff;

		padUnpack(cctv->lookatpadnum, PADFIELD_POS, &pad);

		lenspos.x = lens->position.pos.x;
		lenspos.y = lens->position.pos.y;
		lenspos.z = lens->position.pos.z;

		mtx00016208(obj->realrot, &lenspos);

		lenspos.x += obj->prop->pos.x;
		lenspos.y += obj->prop->pos.y;
		lenspos.z += obj->prop->pos.z;

		xdiff = lenspos.x - pad.pos.x;
		ydiff = lenspos.y - pad.pos.y;
		zdiff = lenspos.z - pad.pos.z;

		if (ydiff) {
			// empty
		}

		mtx00016d58(&cctv->camrotm, 0.0f, 0.0f, 0.0f, xdiff, ydiff, zdiff, 0.0f, 1.0f, 0.0f);
		mtx00015f04(obj->model->scale, &cctv->camrotm);

		cctv->toleft = 0;
		cctv->yleft = *(s32 *)&cctv->yleft * M_BADTAU / 65536.0f;
		cctv->yright = *(s32 *)&cctv->yright * M_BADTAU / 65536.0f;
		cctv->yspeed = 0.0f;
		cctv->ymaxspeed = *(s32 *)&cctv->ymaxspeed * M_BADTAU / 65536.0f;
		cctv->maxdist = *(s32 *)&cctv->maxdist;
		cctv->yrot = cctv->yleft;

		cctv->yzero = atan2f(xdiff, zdiff);
		cctv->xzero = M_BADTAU - atan2f(ydiff, sqrtf(xdiff * xdiff + zdiff * zdiff));

		if (xdiff || zdiff) {
			// empty
		}

		cctv->seebondtime60 = 0;
	}
}

void setupCreateAutogun(struct autogunobj *autogun, s32 cmdindex)
{
	setupCreateObject(&autogun->base, cmdindex);

	autogun->maxspeed = *(s32 *)&autogun->maxspeed * PALUPF(M_BADTAU) / 65536.0f;
	autogun->aimdist = *(s32 *)&autogun->aimdist * 100.0f / 65536.0f;
	autogun->ymaxleft = *(s32 *)&autogun->ymaxleft * M_BADTAU / 65536.0f;
	autogun->ymaxright = *(s32 *)&autogun->ymaxright * M_BADTAU / 65536.0f;

	autogun->firecount = 0;
	autogun->lastseebond60 = -1;
	autogun->lastaimbond60 = -1;
	autogun->allowsoundframe = -1;
	autogun->yrot = 0;
	autogun->yspeed = 0;
	autogun->yzero = 0;
	autogun->xrot = 0;
	autogun->xspeed = 0;
	autogun->xzero = 0;
	autogun->barrelspeed = 0;
	autogun->barrelrot = 0;
	autogun->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
	autogun->beam->age = -1;
	autogun->firing = false;
	autogun->ammoquantity = 255;
	autogun->shotbondsum = 0;

	if (autogun->targetpad >= 0) {
		u32 stack1;
		f32 xdiff;
		f32 ydiff;
		f32 zdiff;
		u32 stack2;
		struct pad pad;

		padUnpack(autogun->targetpad, PADFIELD_POS, &pad);

		xdiff = pad.pos.x - autogun->base.prop->pos.x;
		ydiff = pad.pos.y - autogun->base.prop->pos.y;
		zdiff = pad.pos.z - autogun->base.prop->pos.z;

		autogun->yzero = atan2f(xdiff, zdiff);
		autogun->xzero = atan2f(ydiff, sqrtf(xdiff * xdiff + zdiff * zdiff));
	} else if (autogun->base.modelnum == MODEL_CETROOFGUN) {
		// Deep Sea roofgun
		autogun->xzero = -1.5705462694168f;
	}
}

void setupCreateHangingMonitors(struct hangingmonitorsobj *monitors, s32 cmdindex)
{
	setupCreateObject(&monitors->base, cmdindex);
}

void setupCreateSingleMonitor(struct singlemonitorobj *monitor, s32 cmdindex)
{
	u32 stack[2];

	monitor->screen = var8009ce98;
	tvscreenSetImageByNum(&monitor->screen, monitor->imagenum);

	// In GE, monitors with a negative pad are hanging TVs which attach to a
	// hangingmonitors object, which is actually just the mount. In PD, hanging
	// monitors do not exist in the setup files so this code is unused.
	if (monitor->base.pad < 0 && (monitor->base.flags & OBJFLAG_INSIDEANOTHEROBJ) == 0) {
		s32 modelnum = monitor->base.modelnum;
		struct defaultobj *owner = (struct defaultobj *)setupGetCmdByIndex(cmdindex + monitor->owneroffset);
		struct prop *prop;
		f32 scale;
		struct coord spa4;
		Mtxf sp64;
		Mtxf sp24;

		setupLoadModeldef(modelnum);

		scale = monitor->base.extrascale * (1.0f / 256.0f);

		if (g_Vars.normmplayerisrunning || g_Vars.lvmpbotlevel) {
			monitor->base.hidden2 |= OBJH2FLAG_CANREGEN;
		}

		prop = objInitWithAutoModel(&monitor->base);
		monitor->base.embedment = embedmentAllocate();

		if (prop && monitor->base.embedment) {
			monitor->base.hidden |= OBJHFLAG_EMBEDDED;
			modelSetScale(monitor->base.model, monitor->base.model->scale * scale);
			monitor->base.model->attachedtomodel = owner->model;

			if (monitor->ownerpart == MODELPART_0000) {
				monitor->base.model->attachedtonode = modelGetPart(owner->model->definition, MODELPART_0000);
			} else if (monitor->ownerpart == MODELPART_0001) {
				monitor->base.model->attachedtonode = modelGetPart(owner->model->definition, MODELPART_0001);
			} else if (monitor->ownerpart == MODELPART_0002) {
				monitor->base.model->attachedtonode = modelGetPart(owner->model->definition, MODELPART_0002);
			} else {
				monitor->base.model->attachedtonode = modelGetPart(owner->model->definition, MODELPART_0003);
			}

			propReparent(prop, owner->prop);
			mtx4LoadXRotation(0.3664608001709f, &sp64);
			mtx00015f04(monitor->base.model->scale / owner->model->scale, &sp64);
			modelGetRootPosition(monitor->base.model, &spa4);

			spa4.x = -spa4.x;
			spa4.y = -spa4.y;
			spa4.z = -spa4.z;

			mtx4LoadTranslation(&spa4, &sp24);
			mtx00015be4(&sp64, &sp24, &monitor->base.embedment->matrix);
		}
	} else {
		setupCreateObject(&monitor->base, cmdindex);
	}

	if (monitor->base.prop && (monitor->base.flags & OBJFLAG_MONITOR_RENDERPOSTBG)) {
		monitor->base.prop->flags |= PROPFLAG_RENDERPOSTBG;
	}
}

void setupCreateMultiMonitor(struct multimonitorobj *monitor, s32 cmdindex)
{
	monitor->screens[0] = var8009ce98;
	tvscreenSetImageByNum(&monitor->screens[0], monitor->imagenums[0]);

	monitor->screens[1] = var8009ce98;
	tvscreenSetImageByNum(&monitor->screens[1], monitor->imagenums[1]);

	monitor->screens[2] = var8009ce98;
	tvscreenSetImageByNum(&monitor->screens[2], monitor->imagenums[2]);

	monitor->screens[3] = var8009ce98;
	tvscreenSetImageByNum(&monitor->screens[3], monitor->imagenums[3]);

	setupCreateObject(&monitor->base, cmdindex);
}

s32 setupGetPortalByPad(s32 padnum)
{
	f32 mult;
	struct coord centre;
	struct coord coord;
	u32 stack;
	struct pad pad;

	padGetCentre(padnum, &centre);
	padUnpack(padnum, PADFIELD_BBOX | PADFIELD_UP, &pad);

	mult = (pad.bbox.ymax - pad.bbox.ymin) * 0.5f + 10;

	coord.x = pad.up.x * mult + centre.x;
	coord.y = pad.up.y * mult + centre.y;
	coord.z = pad.up.z * mult + centre.z;

	centre.x = centre.x - pad.up.x * mult;
	centre.y = centre.y - pad.up.y * mult;
	centre.z = centre.z - pad.up.z * mult;

	return bgFindPortalBetweenPositions(&centre, &coord);
}

s32 setupGetPortalByDoorPad(s32 padnum)
{
	f32 mult;
	struct coord centre;
	struct coord coord;
	u32 stack;
	struct pad pad;

	padGetCentre(padnum, &centre);
	padUnpack(padnum, PADFIELD_BBOX | PADFIELD_NORMAL, &pad);

	mult = (pad.bbox.xmax - pad.bbox.xmin) * 0.5f + 10;

	coord.x = pad.normal.x * mult + centre.x;
	coord.y = pad.normal.y * mult + centre.y;
	coord.z = pad.normal.z * mult + centre.z;

	centre.x = centre.x - pad.normal.x * mult;
	centre.y = centre.y - pad.normal.y * mult;
	centre.z = centre.z - pad.normal.z * mult;

	return bgFindPortalBetweenPositions(&centre, &coord);
}

void setupCreateDoor(struct doorobj *door, s32 cmdindex)
{
	f32 scale;
	s32 modelnum = door->base.modelnum;
	s32 portalnum = -1;
	struct pad pad;

	setupLoadModeldef(modelnum);

	/* B-163: FIX-B.2 returns early when modeldefLoadToNew fails (catalog miss
	 * or missing ROM file), leaving g_ModelStates[modelnum].modeldef NULL.
	 * Without this guard every downstream dereference crashes — doorInit at
	 * minimum.  Skip creation and log so the door shows up in diagnostics. */
	if (g_ModelStates[modelnum].modeldef == NULL) {
		sysLogPrintf(LOG_WARNING,
			"SETUP: door modelnum %d modeldef NULL after load — door skipped",
			modelnum);
		return;
	}

	if (door->doorflags & DOORFLAG_ROTATEDPAD) {
		padRotateForDoor(door->base.pad);
	}

	if (door->base.flags & OBJFLAG_DOOR_HASPORTAL) {
		portalnum = setupGetPortalByDoorPad(door->base.pad);
	}

	padUnpack(door->base.pad, PADFIELD_POS | PADFIELD_LOOK | PADFIELD_UP | PADFIELD_NORMAL | PADFIELD_BBOX | PADFIELD_ROOM, &pad);

	if (g_DoorScale != 1) {
		pad.bbox.xmin *= g_DoorScale;
		pad.bbox.xmax *= g_DoorScale;

		// If the door has a portal, adjust the pad's bbox to match the portal's dimensions
		if (portalnum >= 0) {
			struct portalmetric *ptr = &g_PortalMetrics[portalnum];
			f32 f0 = pad.pos.f[0] * ptr->normal.f[0] + pad.pos.f[1] * ptr->normal.f[1] + pad.pos.f[2] * ptr->normal.f[2];
			f32 min = ptr->min;
			struct coord sp150;
			f0 = (f0 - min) * (g_DoorScale - 1);

			sp150.x = ptr->normal.x * f0;
			sp150.y = ptr->normal.y * f0;
			sp150.z = ptr->normal.z * f0;

			f0 = sp150.f[0] * pad.normal.f[0] + sp150.f[1] * pad.normal.f[1] + sp150.f[2] * pad.normal.f[2];
			pad.bbox.xmin += f0;
			pad.bbox.xmax += f0;

			f0 = sp150.f[0] * pad.up.f[0] + sp150.f[1] * pad.up.f[1] + sp150.f[2] * pad.up.f[2];
			pad.bbox.ymin += f0;
			pad.bbox.ymax += f0;

			f0 = sp150.f[0] * pad.look.f[0] + sp150.f[1] * pad.look.f[1] + sp150.f[2] * pad.look.f[2];
			pad.bbox.zmin += f0;
			pad.bbox.zmax += f0;
		}

		// Write the modified bbox into the pad file data
		padCopyBboxFromPad(door->base.pad, &pad);
	}

	if (pad.room > 0) {
		Mtxf sp110;
		struct prop *prop;
		s32 siblingcmdindex;
		struct coord pos;
		RoomNum rooms[8];
		Mtxf finalmtx;
		struct coord centre;
		Mtxf zrotmtx;
		struct coord sp54;
		f32 xscale;
		f32 yscale;
		f32 zscale;
		struct modelrodata_bbox *bbox;

		bbox = modeldefFindBboxRodata(g_ModelStates[modelnum].modeldef);

		mtx00016d58(&sp110, 0, 0, 0,
				-pad.look.x, -pad.look.y, -pad.look.z,
				pad.up.x, pad.up.y, pad.up.z);
		mtx4LoadXRotation(1.5705462694168f, &finalmtx);
		mtx4LoadZRotation(1.5705462694168f, &zrotmtx);
		mtx4MultMtx4InPlace(&zrotmtx, &finalmtx);
		mtx4MultMtx4InPlace(&sp110, &finalmtx);

		padGetCentre(door->base.pad, &centre);

		/* B-163: bbox can be NULL if the modeldef has no BBOX node (degenerate
		 * or partially-loaded model).  Fall back to identity scale — the door
		 * will render at 1:1 rather than crashing.  Matches the zero-scale
		 * guard directly below. */
		if (bbox != NULL) {
			xscale = (pad.bbox.ymax - pad.bbox.ymin) / (bbox->xmax - bbox->xmin);
			yscale = (pad.bbox.zmax - pad.bbox.zmin) / (bbox->ymax - bbox->ymin);
			zscale = (pad.bbox.xmax - pad.bbox.xmin) / (bbox->zmax - bbox->zmin);

			if (xscale <= 0.000001f || yscale <= 0.000001f || zscale <= 0.000001f) {
				xscale = yscale = zscale = 1;
			}
		} else {
			sysLogPrintf(LOG_WARNING,
				"SETUP: door modelnum %d has no bbox node — using identity scale",
				modelnum);
			xscale = yscale = zscale = 1;
		}

		mtx00015e24(xscale, &finalmtx);
		mtx00015e80(yscale, &finalmtx);
		mtx00015edc(zscale, &finalmtx);

		pos.x = pad.pos.x;
		pos.y = pad.pos.y;
		pos.z = pad.pos.z;

		rooms[0] = pad.room;
		rooms[1] = -1;

		if (door->doortype == DOORTYPE_VERTICAL || door->doortype == DOORTYPE_FALLAWAY) {
			sp54.x = pad.look.f[0] * (pad.bbox.zmax - pad.bbox.zmin);
			sp54.y = pad.look.f[1] * (pad.bbox.zmax - pad.bbox.zmin);
			sp54.z = pad.look.f[2] * (pad.bbox.zmax - pad.bbox.zmin);
		} else {
			sp54.x = pad.up.f[0] * (pad.bbox.ymin - pad.bbox.ymax);
			sp54.y = pad.up.f[1] * (pad.bbox.ymin - pad.bbox.ymax);
			sp54.z = pad.up.f[2] * (pad.bbox.ymin - pad.bbox.ymax);
		}

		// These values are stored in the setup files as integers, but at
		// runtime they are floats. Hence reading a "float" as an integer,
		// converting it to a float and writing it back to the same property.
		door->maxfrac = *(s32 *) &door->maxfrac / 65536.0f;
		door->perimfrac = *(s32 *) &door->perimfrac / 65536.0f;
		door->accel = PALUPF(*(s32 *) &door->accel) / 65536000.0f;
		door->decel = PALUPF(*(s32 *) &door->decel) / 65536000.0f;
		door->maxspeed = PALUPF(*(s32 *) &door->maxspeed) / 65536.0f;

		// The sibling door is stored as a relative command number,
		// but at runtime it's a pointer.
		if (door->sibling) {
			siblingcmdindex = *(s32 *) &door->sibling + cmdindex;
			door->sibling = (struct doorobj *) setupGetCmdByIndex(siblingcmdindex);
		}

		prop = doorInit(door, &pos, &finalmtx, rooms, &sp54, &centre);

		if (door->base.flags & OBJFLAG_DOOR_HASPORTAL) {
			door->portalnum = portalnum;

			if (door->portalnum >= 0 && door->frac == 0) {
				doorDeactivatePortal(door);
			}
		}

		if (door->base.model) {
			scale = xscale;

			if (yscale > scale) {
				scale = yscale;
			}

			if (zscale > scale) {
				scale = zscale;
			}

			modelSetScale(door->base.model, door->base.model->scale * scale);
		}

		propActivate(prop);
		propEnable(prop);
	} else {
		door->base.prop = NULL;
	}
}

void setupCreateHov(struct defaultobj *obj, struct hov *hov)
{
	hov->bobycur = 0;
	hov->bobytarget = 0;
	hov->bobyspeed = 0;
	hov->yrot = atan2f(obj->realrot[2][0], obj->realrot[2][2]);
	hov->bobpitchcur = 0;
	hov->bobpitchtarget = 0;
	hov->bobpitchspeed = 0;
	hov->bobrollcur = 0;
	hov->bobrolltarget = 0;
	hov->bobrollspeed = 0;
	hov->groundpitch = 0;
	hov->y = 0;
	hov->ground = 0;
	hov->prevframe60 = -1;
	hov->prevgroundframe60 = -1;
}

void setupLoadBriefing(s32 stagenum, u8 *buffer, s32 bufferlen, struct briefing *briefing)
{
	if (STAGE_IS_GAMEPLAY(stagenum)) {
		s32 stageindex = stageGetIndex(stagenum);
		struct defaultobj *start;
		u16 setupfilenum;
		s32 setupfilesize;
		struct objective *objective;
		struct briefingobj *briefingobj;
		s32 i;
		u8 *langbuffer;
		s32 langbufferlen;
		struct stagesetup *setup;
		catalog_stage_result_t stage;
		asset_data_handle_t setup_handle = ASSET_HANDLE_NULL_INIT;

		if (stageindex < 0) {
			stageindex = 0;
		}

		catalogGetStageResultByIndex(stageindex, &stage);
		setupfilenum = (u16)stage.setupfileid;
		setup_handle = stage.setup_handle;
		g_LoadType = LOADTYPE_SETUP;

		setupRequireScenarioSourceHandle("briefing setup", &stage, setup_handle);
		if (assetLoadToAddr(setup_handle, FILELOADMETHOD_DEFAULT,
				buffer, (u32)bufferlen) == NULL) {
			sysLogPrintf(LOG_ERROR,
				"SETUP: failed to load briefing setup fileid=%d for stage index=%d",
				setupfilenum, stageindex);
			g_LoadType = LOADTYPE_NONE;
			return;
		}

		setup = (struct stagesetup *)buffer;
		setupfilesize = assetLoadGetLoadedSize(setup_handle);
		if (setupfilesize <= 0 || setupfilesize >= bufferlen) {
			sysLogPrintf(LOG_ERROR,
				"SETUP: invalid briefing setup size=%d buffer=%d fileid=%d",
				setupfilesize, bufferlen, setupfilenum);
			g_LoadType = LOADTYPE_NONE;
			return;
		}
		langbuffer = &buffer[setupfilesize];
		langbufferlen = bufferlen - setupfilesize;

		briefing->langbank = langGetLangBankIndexFromStagenum(stagenum);

		langLoadToAddr(briefing->langbank, langbuffer, langbufferlen);

		start = (struct defaultobj *)((uintptr_t)setup + (uintptr_t)setup->props);

		if (start != NULL) {
			struct defaultobj *obj;
			s32 wanttype = BRIEFINGTYPE_TEXT_PA;

			if (lvGetDifficulty() == DIFF_A) {
				wanttype = BRIEFINGTYPE_TEXT_A;
			}

			if (lvGetDifficulty() == DIFF_SA) {
				wanttype = BRIEFINGTYPE_TEXT_SA;
			}

			for (i = 0; (u32)(i < ARRAYCOUNT(briefing->objectivenames)); i++) {
				briefing->objectivenames[i] = 0;
			}

			briefing->briefingtextnum = L_MISC_042; // "No briefing for this mission"

			obj = start;

			while (obj->type != OBJTYPE_END) {
				if (1);
				switch (obj->type) {
				case OBJTYPE_BRIEFING:
					briefingobj = (struct briefingobj *) obj;

					if (briefingobj->type == BRIEFINGTYPE_TEXT_PA) {
						briefing->briefingtextnum = briefingobj->text;
					}

					if (briefingobj->type == wanttype) {
						briefing->briefingtextnum = briefingobj->text;
					}
					break;
				case OBJTYPE_BEGINOBJECTIVE:
					objective = (struct objective *) obj;

					if (objective->index < 7U) {
						briefing->objectivenames[objective->index] = objective->text;
						briefing->objectivedifficulties[objective->index] = objective->difficulties;
					}
					break;
				}

				obj = (struct defaultobj *)((u32 *)obj + setupGetCmdLength((u32 *)obj));
			}
		}
	}
}

void setupLoadFiles(s32 stagenum)
{
	s32 i;
	s32 j;
	struct ailist tmp;
	s32 numchrs = 0;
	s32 numobjs = 0;
	s32 extra;
	struct stagesetup *setup;
	u16 filenum;
	bool modified;
	catalog_stage_result_t stage;
	asset_data_handle_t setup_handle = ASSET_HANDLE_NULL_INIT;
	s32 setup_loaded_size = 0;
	s32 source_pad_size = 0;
	s32 stage_ailist_capacity = 0;
	s32 stage_ailist_count = 0;

	sysLogPrintf(LOG_NOTE, "LOAD: setupLoadFiles(0x%02x) g_StageIndex=%d normmplay=%d", stagenum, g_StageIndex, g_Vars.normmplayerisrunning);
	g_PadEffects = NULL;
	g_LastPadEffectIndex = -1;

	g_DoorScale = 1;

	for (i = 0; i < NUM_MODELS; i++) {
		g_ModelStates[i].modeldef = NULL;
	}

	if (STAGE_IS_GAMEPLAY(stagenum)) {
		catalogGetStageResultByIndex(g_StageIndex, &stage);

		if (g_Vars.normmplayerisrunning) {
			filenum = (u16)stage.mpsetupfileid;
		} else {
			filenum = (u16)stage.setupfileid;
		}

		g_LoadType = LOADTYPE_SETUP;

		sysLogPrintf(LOG_NOTE, "LOAD: loading setup file id=%d (mp=%d, sp=%d)", filenum, stage.mpsetupfileid, stage.setupfileid);
		setup_handle = g_Vars.normmplayerisrunning ? stage.mpsetup_handle : stage.setup_handle;
		(void)scenarioSourceActivateGraphsForStage(&stage,
			g_Vars.normmplayerisrunning);
		g_GeCreditsData = scenarioSourceLoadSetupForStage(&stage,
			g_Vars.normmplayerisrunning, &setup_loaded_size);
		if (!g_GeCreditsData) {
			setupRequireScenarioSourceHandle(
				g_Vars.normmplayerisrunning ? "mp setup" : "setup",
				&stage, setup_handle);
			g_GeCreditsData = (u8 *)assetLoadToNew(setup_handle, FILELOADMETHOD_DEFAULT, LOADTYPE_SETUP);
			setup_loaded_size = assetLoadGetLoadedSize(setup_handle);
		}
		setup = (struct stagesetup *)g_GeCreditsData;
		{
			s32 stagebank = (s32)langGetLangBankIndexFromStagenum(stagenum);
			langLoad(stagebank);
			langManifestRecordBank(stagebank);
		}

		g_StageSetup.intro = (s32 *)((uintptr_t)setup + (uintptr_t)setup->intro);
		g_StageSetup.props = (u32 *)((uintptr_t)setup + (uintptr_t)setup->props);
		if (setup->paths) {
			void *paths_ptr = NULL;
			if (setupResolvePointerInLoadedSetup(setup, setup_loaded_size,
					setup->paths, sizeof(struct path), 4, &paths_ptr)) {
				g_StageSetup.paths = (struct path *)paths_ptr;
			} else {
				sysLogPrintf(LOG_WARNING,
					"SETUP: invalid paths pointer for stagenum=0x%02x file=%d size=%d raw=%p -- disabling paths",
					stagenum, filenum, setup_loaded_size, (void *)setup->paths);
				g_StageSetup.paths = NULL;
			}
		} else {
			g_StageSetup.paths = NULL;
		}
		if (setup->ailists) {
			void *ailists_ptr = NULL;
			if (setupResolvePointerInLoadedSetup(setup, setup_loaded_size,
					setup->ailists, sizeof(struct ailist), 4, &ailists_ptr)) {
				g_StageSetup.ailists = (struct ailist *)ailists_ptr;
			} else {
				sysLogPrintf(LOG_WARNING,
					"SETUP: invalid ailists pointer for stagenum=0x%02x file=%d size=%d raw=%p -- disabling ailists",
					stagenum, filenum, setup_loaded_size, (void *)setup->ailists);
				g_StageSetup.ailists = NULL;
			}
		} else {
			g_StageSetup.ailists = NULL;
		}

		// PC: Validate intro command data. Mod stages may have setup files where
		// the intro offset doesn't point to valid intro command data (e.g. it lands
		// inside the props section). Reading garbage as intro commands causes crashes
		// in playerReset() and scenarioReset() — OOB writes, pointer chasing, etc.
		//
		// Detection: if the intro pointer is within 64 bytes of the props pointer,
		// they're aliased into the same region and the intro data is garbage.
		// Also reject if the first command word isn't a valid INTROCMD type.
		if (g_StageSetup.intro) {
			uintptr_t introAddr = (uintptr_t)g_StageSetup.intro;
			uintptr_t propsAddr = (uintptr_t)g_StageSetup.props;
			uintptr_t dist = introAddr > propsAddr ? introAddr - propsAddr : propsAddr - introAddr;
			s32 firstCmd = *g_StageSetup.intro;

			// Base-game MP setup files and source-built setup blocks can use
			// compact intro sections next to props. Reject true aliasing by
			// walking the intro command stream and proving it terminates before
			// props instead of using distance alone.
			if ((dist < 64 && !setupIntroCommandsAreValid(setup,
						setup_loaded_size, g_StageSetup.intro,
						g_StageSetup.props)) ||
					firstCmd < 0 || firstCmd > INTROCMD_END) {
				sysLogPrintf(LOG_WARNING, "LOAD: invalid intro data (first cmd=%d, intro=%p, props=%p, dist=%llu), nulling intro",
					firstCmd, (void *)g_StageSetup.intro, (void *)g_StageSetup.props, (unsigned long long)dist);
				g_StageSetup.intro = NULL;
			}
		}

		g_LoadType = LOADTYPE_PADS;

		sysLogPrintf(LOG_NOTE, "LOAD: loading pad file id=%d", stage.padsfileid);
		g_StageSetup.padfiledata = scenarioSourceLoadPadsForStage(&stage,
			g_Vars.normmplayerisrunning, &source_pad_size);
		if (g_StageSetup.padfiledata) {
			setupSetPadFileDataSize(source_pad_size);
		} else {
			setupRequireScenarioSourceHandle("pads", &stage, stage.pads_handle);
			g_StageSetup.padfiledata = assetLoadToNew(stage.pads_handle, FILELOADMETHOD_DEFAULT, LOADTYPE_PADS);
			setupSetPadFileDataSize(g_StageSetup.padfiledata ? assetLoadGetLoadedSize(stage.pads_handle) : 0);
		}
		if (!g_StageSetup.padfiledata) {
			sysLogPrintf(LOG_ERROR, "SETUP: failed to load pads fileid=%d for stage index=%d",
				stage.padsfileid, g_StageIndex);
		}

		g_StageSetup.waypoints = NULL;
		g_StageSetup.waygroups = NULL;
		g_StageSetup.cover = NULL;

		// Convert ailist pointers from file-local to proper pointers
		if (g_StageSetup.ailists) {
			uintptr_t table_addr = (uintptr_t)g_StageSetup.ailists;
			uintptr_t setup_end = (uintptr_t)setup + (uintptr_t)setup_loaded_size;
			stage_ailist_capacity = (s32)((setup_end - table_addr) / sizeof(struct ailist));

			for (i = 0; i < stage_ailist_capacity && g_StageSetup.ailists[i].list != NULL; i++) {
				void *list_ptr = NULL;
				if (!setupResolvePointerInLoadedSetup(setup, setup_loaded_size,
						g_StageSetup.ailists[i].list, 2, 1, &list_ptr)) {
					sysLogPrintf(LOG_WARNING,
						"SETUP: invalid ailist list pointer for stagenum=0x%02x file=%d ailist=%d id=%d raw=%p -- truncating ailists",
						stagenum, filenum, i, g_StageSetup.ailists[i].id,
						(void *)g_StageSetup.ailists[i].list);
					g_StageSetup.ailists[i].list = NULL;
					break;
				}

				g_StageSetup.ailists[i].list = (u8 *)list_ptr;
			}

			if (i >= stage_ailist_capacity) {
				sysLogPrintf(LOG_WARNING,
					"SETUP: ailist table for stagenum=0x%02x file=%d reached setup bounds -- disabling ailists",
					stagenum, filenum);
				g_StageSetup.ailists = NULL;
				stage_ailist_count = 0;
			} else {
				stage_ailist_count = i;
			}
		}

		// Sort the global AI lists by ID asc
		do {
			modified = false;

			for (i = 0; g_GlobalAilists[i + 1].list != NULL; i++) {
				if (g_GlobalAilists[i + 1].id < g_GlobalAilists[i].id) {
					// Swap them
					tmp = g_GlobalAilists[i];
					g_GlobalAilists[i] = g_GlobalAilists[i + 1];
					g_GlobalAilists[i + 1] = tmp;

					modified = true;
				}
			}
		} while (modified);

		// Sort the stage AI lists by ID asc
		if (g_StageSetup.ailists && stage_ailist_count > 1) {
			do {
				modified = false;

				for (i = 0; i + 1 < stage_ailist_count; i++) {
					if (g_StageSetup.ailists[i + 1].id < g_StageSetup.ailists[i].id) {
						// Swap them
						tmp = g_StageSetup.ailists[i];
						g_StageSetup.ailists[i] = g_StageSetup.ailists[i + 1];
						g_StageSetup.ailists[i + 1] = tmp;

						modified = true;
					}
				}
			} while (modified);
		}

		// Count the AI lists
		for (g_NumGlobalAilists = 0; g_GlobalAilists[g_NumGlobalAilists].list != NULL; g_NumGlobalAilists++);
		if (g_StageSetup.ailists) {
			g_NumLvAilists = stage_ailist_count;
		} else {
			g_NumLvAilists = 0;
		}

		// Convert path pad pointers from file-local to proper pointers
		// and calculate the path lengths
		if (g_StageSetup.paths) {
			uintptr_t paths_base = (uintptr_t)g_StageSetup.paths;
			uintptr_t setup_base = (uintptr_t)setup;
			s32 max_paths = (s32)(((uintptr_t)setup + (uintptr_t)setup_loaded_size - paths_base) / sizeof(struct path));

			for (i = 0; i < max_paths && g_StageSetup.paths[i].pads != NULL; i++) {
				void *pads_ptr = NULL;
				if (!setupResolvePointerInLoadedSetup(setup, setup_loaded_size,
						g_StageSetup.paths[i].pads, sizeof(s32), 4, &pads_ptr)) {
					sysLogPrintf(LOG_WARNING,
						"SETUP: invalid path pads pointer for stagenum=0x%02x file=%d path=%d raw=%p -- disabling paths",
						stagenum, filenum, i, (void *)g_StageSetup.paths[i].pads);
					g_StageSetup.paths = NULL;
					break;
				}

				g_StageSetup.paths[i].pads = (s32 *)pads_ptr;

				{
					uintptr_t pads_addr = (uintptr_t)g_StageSetup.paths[i].pads;
					s32 max_pad_words = (s32)((setup_base + (uintptr_t)setup_loaded_size - pads_addr) / sizeof(s32));

					for (j = 0; j < max_pad_words && g_StageSetup.paths[i].pads[j] >= 0; j++);

					if (j >= max_pad_words) {
						sysLogPrintf(LOG_WARNING,
							"SETUP: path pads for stagenum=0x%02x file=%d path=%d reached setup bounds -- disabling paths",
							stagenum, filenum, i);
						g_StageSetup.paths = NULL;
						break;
					}
				}

				g_StageSetup.paths[i].len = j;
			}

			if (g_StageSetup.paths && i >= max_paths) {
				sysLogPrintf(LOG_WARNING,
					"SETUP: path list for stagenum=0x%02x file=%d reached setup bounds -- disabling paths",
					stagenum, filenum);
				g_StageSetup.paths = NULL;
			}
		}

		// Count the number of chrs and objects so enough model slots can be allocated
		numchrs += setupCountCommandType(OBJTYPE_CHR);

		{
			s32 tiny_extra = setupCountTinyModeExtraChrs();
			if (tiny_extra > 0) {
				numchrs += tiny_extra;
				sysLogPrintf(LOG_NOTE,
					"TINYMODE: added %d generic-enemy chr slots for model allocation; numchrs=%d",
					tiny_extra, numchrs);
			}
		}

		if (!g_Vars.normmplayerisrunning && g_MissionConfig.iscoop && g_Vars.numaibuddies > 0) {
			// @bug? The Hotshot buddy has two guns, but only one is counted here.
			numchrs += g_Vars.numaibuddies;
			numobjs += g_Vars.numaibuddies; // the buddy's weapon
		}

		numobjs += setupCountCommandType(OBJTYPE_WEAPON);
		numobjs += setupCountCommandType(OBJTYPE_KEY);
		numobjs += setupCountCommandType(OBJTYPE_HAT);
		numobjs += setupCountCommandType(OBJTYPE_DOOR);
		numobjs += setupCountCommandType(OBJTYPE_CCTV);
		numobjs += setupCountCommandType(OBJTYPE_AUTOGUN);
		numobjs += setupCountCommandType(OBJTYPE_HANGINGMONITORS);
		numobjs += setupCountCommandType(OBJTYPE_SINGLEMONITOR);
		numobjs += setupCountCommandType(OBJTYPE_MULTIMONITOR);
		numobjs += setupCountCommandType(OBJTYPE_SHIELD);
		numobjs += setupCountCommandType(OBJTYPE_BASIC);
		numobjs += setupCountCommandType(OBJTYPE_DEBRIS);
		numobjs += setupCountCommandType(OBJTYPE_GLASS);
		numobjs += setupCountCommandType(OBJTYPE_TINTEDGLASS);
		numobjs += setupCountCommandType(OBJTYPE_SAFE);
		numobjs += setupCountCommandType(OBJTYPE_29);
		numobjs += setupCountCommandType(OBJTYPE_GASBOTTLE);
		numobjs += setupCountCommandType(OBJTYPE_ALARM);
		numobjs += setupCountCommandType(OBJTYPE_AMMOCRATE);
		numobjs += setupCountCommandType(OBJTYPE_MULTIAMMOCRATE);
		numobjs += setupCountCommandType(OBJTYPE_TRUCK);
		numobjs += setupCountCommandType(OBJTYPE_TANK);
		numobjs += setupCountCommandType(OBJTYPE_LIFT);
		numobjs += setupCountCommandType(OBJTYPE_HOVERBIKE);
		numobjs += setupCountCommandType(OBJTYPE_HOVERPROP);
		numobjs += setupCountCommandType(OBJTYPE_FAN);
		numobjs += setupCountCommandType(OBJTYPE_HOVERCAR);
		numobjs += setupCountCommandType(OBJTYPE_CHOPPER);
		numobjs += setupCountCommandType(OBJTYPE_HELI);
		numobjs += setupCountCommandType(OBJTYPE_ESCASTEP);

		if (g_Vars.normmplayerisrunning) {
			numobjs += scenarioNumProps();
		}

		/* PC: Account for simulant bots in model slot allocation.
		 * Original N64 only had 8 total characters so the +10 buffer
		 * in chrmgrConfigure was always enough. With up to 32 bots the
		 * slot pool must be sized to fit all of them (B-12 Phase 3: read
		 * from participant pool). */
		if (g_Vars.normmplayerisrunning && mpHasSimulants()) {
			s32 k;
			for (k = 0; k < MAX_BOTS; k++) {
				if (mpIsParticipantActive(k + MAX_PLAYERS)) {
					numchrs++;
				}
			}
			sysLogPrintf(LOG_NOTE, "MODELMGR: added simulant bot count to numchrs=%d for model slot allocation", numchrs);
		}

		/* S483 (G.1.1): swarm benchmark needs up to 256 chr slots above
		 * whatever the stage already declares. Add the cap into numchrs
		 * BEFORE modelmgrAllocateSlots so both the model slot pool and
		 * g_Vars.maxprops account for the swarm. All swarm Skedars
		 * share one Skedar body model, so NUMTYPE3 (model rwdata
		 * bindings, large-data slots) is unaffected. */
		{
			s32 swarm_extra = testScenarioGetSwarmMaxCount();
			if (swarm_extra > 0) {
				numchrs += swarm_extra;
				sysLogPrintf(LOG_NOTE,
					"TESTSCEN: added %d swarm chr slots for benchmark; numchrs=%d",
					swarm_extra, numchrs);
			}
		}

		sysLogPrintf(LOG_NOTE, "LOAD: model allocation numobjs=%d numchrs=%d", numobjs, numchrs);
		modelmgrAllocateSlots(numobjs, numchrs);
		sysLogPrintf(LOG_NOTE, "LOAD: modelmgrAllocateSlots done");
	} else {
		// cover isn't set to NULL here... I guess it's not important
		g_StageSetup.waypoints = NULL;
		g_StageSetup.waygroups = NULL;
		g_StageSetup.intro = 0;
		g_StageSetup.props = 0;
		g_StageSetup.paths = NULL;
		g_StageSetup.ailists = NULL;
		g_StageSetup.padfiledata = NULL;
		setupSetPadFileDataSize(0);

		modelmgrAllocateSlots(0, 0);
	}

	extra = 60;

	g_Vars.maxprops = numobjs + numchrs + extra + 40;
}

void setupCreateProps(s32 stagenum)
{
	s32 withchrs = !argFindByPrefix(1, "-nochr") && !argFindByPrefix(1, "-noprop");
	s32 withobjs = !argFindByPrefix(1, "-noobj") && !argFindByPrefix(1, "-noprop");
	s32 withhovercars;
	s32 escstepx;
	s32 escstepy;
	struct defaultobj *obj;
	s32 i;
	s32 j;
	s32 mpSpawnFallbackApplied = false;
	s32 desiredPickups = 0;

	s_SetupMpWeaponLocationCount = 0;
	s_SetupMpCreatedWeaponCount = 0;

	withhovercars = !(stagenum == STAGE_EXTRACTION || stagenum == STAGE_DEFECTION)
		|| !(g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0);

	escstepx = 0;
	escstepy = 0;
	g_Vars.textoverrides = NULL;

	for (j = 0; j != ARRAYCOUNT(g_Briefing.objectivenames); j++) {
		g_Briefing.objectivenames[j] = 0;
		g_Briefing.objectivedifficulties[j] = 0;
	}

	g_Briefing.briefingtextnum = L_MISC_042; // "No briefing for this mission"

	if (STAGE_IS_GAMEPLAY(stagenum)) {
		if (g_StageSetup.padfiledata) {
			setupPreparePads();
		}

		setupLoadWaypoints();

		if (withchrs) {
			s32 numchrs = 0;

			numchrs += setupCountCommandType(OBJTYPE_CHR);

			{
				s32 tiny_extra = setupCountTinyModeExtraChrs();
				if (tiny_extra > 0) {
					numchrs += tiny_extra;
					sysLogPrintf(LOG_NOTE,
						"CHRSLOTS: added %d Tiny Mode generic-enemy slots; numchrs=%d",
						tiny_extra, numchrs);
				}
			}

			if (g_Vars.normmplayerisrunning == false
					&& g_MissionConfig.iscoop
					&& g_Vars.numaibuddies > 0) {
				numchrs += g_Vars.numaibuddies;
			}

			/* PC: Account for simulant bots in chr slot allocation.
			 * Original N64 only had 8 total characters so the +10 buffer
			 * in chrmgrConfigure was always enough. With up to 32 bots
			 * the slot pool must be sized to fit all of them (B-12 Phase 3:
			 * read from participant pool). */
			if (g_Vars.normmplayerisrunning && mpHasSimulants()) {
				s32 k;
				for (k = 0; k < MAX_BOTS; k++) {
					if (mpIsParticipantActive(k + MAX_PLAYERS)) {
						numchrs++;
					}
				}
				sysLogPrintf(LOG_NOTE, "CHRSLOTS: added %d simulant slots (total numchrs=%d)",
					numchrs - (s32)setupCountCommandType(OBJTYPE_CHR), numchrs);
			}

			/* S593 follow-up (2026-05-01): swarm benchmark needs up to 256
			 * chr slots in g_ChrSlots[] above whatever the stage and
			 * normal MP simulants declare. The earlier modelmgr-side hook
			 * (line 1575) sized the model/anim/prop pools for 256, but
			 * chrmgrConfigure was missing the same hook -- result was
			 * g_NumChrSlots = PLAYERCOUNT() + 0 + 10 = 11 on a
			 * solo-with-no-simulants swarm session, which capped the cycle
			 * ladder at ~10 chrs and explained Mike's "loops at 8 only"
			 * symptom. Mirror the hook here so chr slot count matches the
			 * model/anim sizing. */
			{
				s32 swarm_extra = testScenarioGetSwarmMaxCount();
				if (swarm_extra > 0) {
					numchrs += swarm_extra;
					sysLogPrintf(LOG_NOTE,
						"CHRSLOTS: added %d swarm chr slots for benchmark; numchrs=%d",
						swarm_extra, numchrs);
				}
			}

			chrmgrConfigure(numchrs);
		} else {
			chrmgrConfigure(0);
		}
		sysLogPrintf(LOG_NOTE, "LOAD: chr slots done");

		for (j = 0; j < PLAYERCOUNT(); j++) {
			if (!g_Vars.players[j]) continue;
			setCurrentPlayerNum(j);
			invInit(setupCountCommandType(OBJTYPE_LINKGUNS));
		}

		if (g_StageSetup.props) {
			u32 diffflag = 0;
			s32 index;

			diffflag |= 1 << (lvGetDifficulty() + 4);

			if (g_Vars.mplayerisrunning) {
				if (PLAYERCOUNT() == 2) {
					diffflag |= OBJFLAG2_EXCLUDE_2P;
				} else if (PLAYERCOUNT() == 3) {
					diffflag |= OBJFLAG2_EXCLUDE_3P;
				} else if (PLAYERCOUNT() == 4) {
					diffflag |= OBJFLAG2_EXCLUDE_4P;
				}
			}

			/* P5 (2026-04-24): SP-stages-in-MP loader gate.  CI Training,
			 * Chicago, Villa and similar SP-class stages ship with setup
			 * blobs that flag lift / escalator props for exclusion in one
			 * or more difficulty or player-count bands (the original author
			 * intent was "skip in MP for perf").  When one of these stages
			 * is hosted as an MP arena we want the transport props to load
			 * so the level is traversable.  `mplift_diffflag` masks the
			 * per-player-count exclusion bits AND the per-difficulty bit
			 * for the identified stages; LIFT/ESCASTEP cases below consult
			 * this masked value instead of the base diffflag.  All other
			 * object types keep the standard filter so SP-only clutter
			 * (desks, decorative chrs, etc.) still stays out.
			 *
			 * Added stages: CITRAINING, CHICAGO, VILLA, INFILTRATION,
			 * G5BUILDING, PELAGIC.  These are the SP missions commonly
			 * hosted as CS arenas today and reported in B-228.  Add more
			 * as the SP-in-MP readiness audit flags them.
			 *
			 * Why this is protocol-safe.  pd-server doesn't run setupLoadStage
			 * (server_stubs.c covers it); each client runs this code locally
			 * with identical stagenum + identical setup blob, so all clients
			 * end up with the same live lift set.  The existing server-auth
			 * lift replication path (liftTick + prop sync) handles position
			 * updates over the wire.  No new messages, no new fields. */
			u32 mptransport_diffflag = diffflag;
			if (g_Vars.mplayerisrunning) {
				switch (g_Vars.stagenum) {
				case STAGE_CITRAINING:
				case STAGE_CHICAGO:
				case STAGE_VILLA:
				case STAGE_INFILTRATION:
				case STAGE_G5BUILDING:
				case STAGE_PELAGIC:
					mptransport_diffflag = 0;
					sysLogPrintf(LOG_NOTE,
							"SETUP.LIFT: SP-in-MP stagenum=0x%02x -- "
							"relaxing LIFT/ESCASTEP exclude filter (diffflag 0x%x -> 0)",
							(u32)g_Vars.stagenum, diffflag);
					break;
				default:
					break;
				}
			}

			botmgrRemoveAll();
			index = 0;

			obj = (struct defaultobj *)g_StageSetup.props;

			while (obj->type != OBJTYPE_END) {
				switch (obj->type) {
				case OBJTYPE_GRENADEPROB:
					{
						struct grenadeprobobj *grenadeprob = (struct grenadeprobobj *)obj;
						u8 probability = grenadeprob->probability;
						struct chrdata *chr = chrFindByLiteralId(grenadeprob->chrnum);

						if (chr && chr->prop && chr->model) {
							chr->grenadeprob = probability;
						}
					}
					break;
				case OBJTYPE_CHR:
					/* B-254 (2026-04-25): canvas mode -- this Grid session
					 * is using the stage geometry as a build canvas;
					 * authored NPCs would patrol / shoot / talk / die,
					 * none of which the user wants while editing. Skip
					 * the chr allocation entirely so the slot pool stays
					 * empty and chrTick never has anything to drive.  The
					 * other slots (door / lift / weapon spawn / monitors)
					 * still load so the geometry + visuals are intact. */
					if (withchrs && !forgeIsCanvasMode()) {
						struct packedchr *packed = (struct packedchr *) obj;
						struct chrdata *chr = bodyAllocateChr(stagenum, packed, index);

						if (chr != NULL) {
							setupCreateTinyModeExtraChrs(stagenum, packed, index, chr);
						}
					}
					break;
				case OBJTYPE_DOOR:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						setupCreateDoor((struct doorobj *)obj, index);
					}
					break;
				case OBJTYPE_DOORSCALE:
					{
						struct doorscaleobj *scale = (struct doorscaleobj *)obj;
						g_DoorScale = scale->scale / 65536.0f;
					}
					break;
				case OBJTYPE_WEAPON:
					if (withchrs && (obj->flags2 & diffflag) == 0) {
						setupPlaceWeapon((struct weaponobj *)obj, index);
					}
					break;
				case OBJTYPE_KEY:
					/* B-254: keys are mission-objective markers (give to
					 * scripted NPC, etc.); skip in canvas mode where the
					 * mission isn't running. */
					if (withchrs && !forgeIsCanvasMode() && (obj->flags2 & diffflag) == 0) {
						setupCreateKey((struct keyobj *)obj, index);
					}
					break;
				case OBJTYPE_HAT:
					/* B-254: hats are NPC equipment; with chr creation
					 * suppressed there is nothing to wear them. */
					if (withchrs && !forgeIsCanvasMode() && (obj->flags2 & diffflag) == 0) {
						setupCreateHat((struct hatobj *)obj, index);
					}
					break;
				case OBJTYPE_CCTV:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						setupCreateCctv((struct cctvobj *)obj, index);
					}
					break;
				case OBJTYPE_AUTOGUN:
					/* B-254: autoguns are active turrets that fire at the
					 * player; skip in canvas mode (the user is observing,
					 * not playing). */
					if (withobjs && !forgeIsCanvasMode() && (obj->flags2 & diffflag) == 0) {
						setupCreateAutogun((struct autogunobj *)obj, index);
					}
					break;
				case OBJTYPE_HANGINGMONITORS:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						setupCreateHangingMonitors((struct hangingmonitorsobj *)obj, index);
					}
					break;
				case OBJTYPE_SINGLEMONITOR:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						setupCreateSingleMonitor((struct singlemonitorobj *)obj, index);
					}
					break;
				case OBJTYPE_MULTIMONITOR:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						setupCreateMultiMonitor((struct multimonitorobj *)obj, index);
					}
					break;
				case OBJTYPE_SHIELD:
					if (withobjs) {
#if VERSION >= VERSION_JPN_FINAL
						if ((obj->flags2 & diffflag) == 0)
#else
						if ((obj->flags2 & diffflag) == 0 || g_Jpn)
#endif
						{
							struct shieldobj *shield = (struct shieldobj *)obj;
							shield->initialamount = *(s32 *)&shield->initialamount / 65536.0f;
							shield->amount = shield->initialamount;
							setupCreateObject(obj, index);
						}
					}
					break;
				case OBJTYPE_TINTEDGLASS:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						if (obj->flags & OBJFLAG_GLASS_HASPORTAL) {
							struct tintedglassobj *glass = (struct tintedglassobj *)obj;
							glass->portalnum = setupGetPortalByPad(obj->pad);
							glass->unk64 = *(s32 *)&glass->unk64 / 65536.0f;
						}

						setupCreateObject(obj, index);
					}
					break;
				case OBJTYPE_LIFT:
					/* P5 (2026-04-24): use the SP-in-MP relaxed filter so
					 * CI / Chicago / Villa lifts load when the stage is
					 * hosted as an MP arena.  For every other stage this
					 * behaves identically to the old `diffflag` path. */
					if (withobjs && (obj->flags2 & mptransport_diffflag) == 0) {
						struct liftobj *lift = (struct liftobj *)obj;
						struct modelstate *modelstate;
						s32 modelnum = obj->modelnum;
						struct prop *prop;
						s32 i;

						lift->accel = PALUPF(*(s32 *)&lift->accel) / 65536.0f;
						lift->maxspeed = PALUPF(*(s32 *)&lift->maxspeed) / 65536.0f;
						lift->dist = 0;
						lift->speed = 0;
						lift->levelcur = 0;
						lift->levelaim = 0;

						for (i = 0; i < ARRAYCOUNT(lift->doors); i++) {
							if (lift->doors[i]) {
								lift->doors[i] = (struct doorobj *)setupGetCmdByIndex(index + *(s32*)&lift->doors[i]);
							}
						}

						obj->geocount = 1;
						setupLoadModeldef(modelnum);
						modelstate = &g_ModelStates[modelnum];

						if (modelstate->modeldef) {
							if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_WALL1)) {
								obj->geocount++;
							}
							if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_WALL2)) {
								obj->geocount++;
							}
							if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_WALL3)) {
								obj->geocount++;
							}
							if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_DOORBLOCK)) {
								obj->geocount++;
							}
							if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_FLOORNONRECT2)) {
								obj->geocount++;
							}
						}

						obj->flags &= ~OBJFLAG_00000100;

						setupCreateObject(obj, index);

						prop = obj->prop;

						if (prop) {
							lift->prevpos.x = prop->pos.x;
							lift->prevpos.y = prop->pos.y;
							lift->prevpos.z = prop->pos.z;

							liftUpdateTiles(lift, true);

							/* F6: auto-register the lift in g_Lifts[] so it
							 * works without an AI script.  In SP the stage
							 * AI list calls aiActivateLift (cmd 0x018d) but
							 * CI arenas (MP) have no AI scripts, so lifts
							 * were silently never registered, which made
							 * liftFindByPad return NULL and players stepping
							 * on a lift pad did nothing.  Auto-register using
							 * the liftnum from the lift's own pads.  An AI
							 * script calling liftActivate later overwrites
							 * with the same pointer -- safe. */
							{
								s32 pi;
								for (pi = 0; pi < (s32)ARRAYCOUNT(lift->pads); pi++) {
									if (lift->pads[pi] < 0) continue;
									struct pad padinfo;
									padUnpack(lift->pads[pi], PADFIELD_LIFT, &padinfo);
									if (padinfo.liftnum > 0 &&
											(u32)padinfo.liftnum <= ARRAYCOUNT(g_Lifts)) {
										liftActivate(prop, padinfo.liftnum);
										break;
									}
								}
							}
						}
					}
					break;
				case OBJTYPE_HOVERPROP:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						struct hoverpropobj *hoverprop = (struct hoverpropobj *)obj;

						setupCreateObject(obj, index);
						setupCreateHov(obj, &hoverprop->hov);
					}
					break;
				case OBJTYPE_HOVERBIKE:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						struct hoverbikeobj *bike = (struct hoverbikeobj *)obj;

						setupCreateObject(obj, index);
						setupCreateHov(obj, &bike->hov);

						bike->speed[0] = 0;
						bike->speed[1] = 0;
						bike->w = 0;
						bike->rels[0] = 0;
						bike->rels[1] = 0;
						bike->exreal = 0;
						bike->ezreal = 0;
						bike->ezreal2 = 0;
						bike->leanspeed = 0;
						bike->leandiff = 0;
						bike->maxspeedtime240 = 0;
						bike->speedabs[0] = 0;
						bike->speedabs[1] = 0;
						bike->speedrel[0] = 0;
						bike->speedrel[1] = 0;
					}
					break;
				case OBJTYPE_FAN:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						struct fanobj *fan = (struct fanobj *)obj;

						fan->yrot = 0;
						fan->ymaxspeed = PALUPF(*(s32 *)&fan->ymaxspeed) / 65536.0f;
						fan->yaccel = PALUPF(*(s32 *)&fan->yaccel) / 65536.0f;

						setupCreateObject(obj, index);
					}
					break;
				case OBJTYPE_GLASS:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						if (obj->flags & OBJFLAG_GLASS_HASPORTAL) {
							struct glassobj *glass = (struct glassobj *)obj;
							glass->portalnum = setupGetPortalByPad(obj->pad);
						}

						setupCreateObject(obj, index);
					}
					break;
				case OBJTYPE_ESCASTEP:
					/* P5 (2026-04-24): same SP-in-MP relax as OBJTYPE_LIFT so
					 * escalator / fire-escape-step props survive MP host. */
					if (withobjs && (obj->flags2 & mptransport_diffflag) == 0) {
						struct escalatorobj *step = (struct escalatorobj *)obj;
						struct prop *prop;

#ifdef AVOID_UB
						Mtxf sp1a8;
#else
						// TODO: There is a stack problem here that should be
						// resolved. sp1a8 is really an Mtxf which doesn't fit
						// in its current location in the stack.
						f32 sp1a8[12];
#endif
						f32 sp184[3][3];

						setupCreateObject(obj, index);

						prop = obj->prop;

						if (prop) {
							step->prevpos.x = prop->pos.x;
							step->prevpos.y = prop->pos.y;
							step->prevpos.z = prop->pos.z;
						}

						if (obj->flags & OBJFLAG_ESCSTEP_ZALIGNED) {
							step->frame = escstepy;
							escstepy += 40;
							mtx4LoadYRotation(4.7116389274597f, (Mtxf *) &sp1a8);
							mtx4ToMtx3((Mtxf *) &sp1a8, sp184);
							mtx00016110(sp184, obj->realrot);
						} else {
							step->frame = escstepx;
							escstepx += 40;
							mtx4LoadYRotation(M_BADPI, (Mtxf *) &sp1a8);
							mtx4ToMtx3((Mtxf *) &sp1a8, sp184);
							mtx00016110(sp184, obj->realrot);
						}
					}
					break;
				case OBJTYPE_BASIC:
				case OBJTYPE_ALARM:
				case OBJTYPE_AMMOCRATE:
				case OBJTYPE_DEBRIS:
				case OBJTYPE_GASBOTTLE:
				case OBJTYPE_29:
				case OBJTYPE_SAFE:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						setupCreateObject(obj, index);
					}
					break;
				case OBJTYPE_MULTIAMMOCRATE:
					{
						struct multiammocrateobj *crate = (struct multiammocrateobj *)obj;
						s32 ammoqty = 1;
						s32 i;

						if (g_Vars.normmplayerisrunning && g_SetupCurMpLocation >= 0) {
							s32 wi = (s32)(mpGetMpWeaponByLocation(g_SetupCurMpLocation) - g_MpWeapons);
							s32 pritype = catalogGetMpWeaponPriAmmoType(wi);
							s32 sectype = catalogGetMpWeaponSecAmmoType(wi);
							ammoqty = catalogGetMpWeaponPriAmmoQty(wi);

							if (pritype > 0 && pritype < 20) {
								crate->slots[pritype - 1].quantity = ammoqty;
							}

							if (sectype > 0 && sectype < 20) {
								crate->slots[sectype - 1].quantity = catalogGetMpWeaponSecAmmoQty(wi);
							}
						}

						if (ammoqty > 0 && withobjs && (obj->flags2 & diffflag) == 0) {
							for (i = 0; i < ARRAYCOUNT(crate->slots); i++) {
								if (crate->slots[i].quantity > 0 && crate->slots[i].modelnum != 0xffff) {
									setupLoadModeldef(crate->slots[i].modelnum);
								}
							}

							setupCreateObject(obj, index);
						}
					}
					break;
				case OBJTYPE_TRUCK:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						struct truckobj *truck = (struct truckobj *)obj;

						setupCreateObject(obj, index);

						if (obj->model) {
							struct modelnode *node = modelGetPart(obj->model->definition, MODELPART_TRUCK_0005);

							if (node) {
								// The truck model doesn't exist in PD, so I'm assuming this is a toggle node
								union modelrwdata *rwdata = modelGetNodeRwData(obj->model, node);
								rwdata->toggle.visible = ((obj->flags & OBJFLAG_DEACTIVATED) == 0);
							}
						}

						truck->speed = 0;
						truck->wheelxrot = 0;
						truck->wheelyrot = 0;
						truck->speedaim = 0;
						truck->speedtime60 = -1;
						truck->turnrot60 = 0;
						truck->roty = 0;
						truck->ailist = ailistFindById((uintptr_t)truck->ailist);
						truck->aioffset = 0;
						truck->aireturnlist = -1;
						truck->path = NULL;
						truck->nextstep = 0;
					}
					break;
				case OBJTYPE_HOVERCAR:
					if (withhovercars && withobjs && (obj->flags2 & diffflag) == 0) {
						struct hovercarobj *car = (struct hovercarobj *)obj;
						struct prop *prop;

						setupCreateObject(obj, index);

						prop = obj->prop;

						car->speed = 0;
						car->speedaim = 0;
						car->turnrot60 = 0;
						car->roty = 0;
						car->rotx = 0;
						car->speedtime60 = -1;
						car->ailist = ailistFindById((uintptr_t)car->ailist);
						car->aioffset = 0;
						car->aireturnlist = -1;
						car->path = NULL;
						car->nextstep = 0;

						if (obj->flags & OBJFLAG_CHOPPER_INACTIVE) {
							prop->pos.y = cdFindFloorYColourTypeAtPos(&prop->pos, prop->rooms, NULL, 0) + 30;
						}

						prop->forcetick = true;
					}
					break;
				case OBJTYPE_CHOPPER:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						struct chopperobj *chopper = (struct chopperobj *)obj;

						setupCreateObject(obj, index);

						obj->flags |= OBJFLAG_CHOPPER_INIT;
						obj->prop->forcetick = true;

						chopper->turnrot60 = 0;
						chopper->roty = 0;
						chopper->rotx = 0;
						chopper->gunroty = 0;
						chopper->gunrotx = 0;
						chopper->barrelrot = 0;
						chopper->barrelrotspeed = 0;
						chopper->ailist = ailistFindById((uintptr_t)chopper->ailist);
						chopper->aioffset = 0;
						chopper->aireturnlist = -1;
						chopper->path = NULL;
						chopper->nextstep = 0;
						chopper->target = -1;
						chopper->targetvisible = false;
						chopper->attackmode = CHOPPERMODE_PATROL;
						chopper->vz = 0;
						chopper->vy = 0;
						chopper->vx = 0;
						chopper->otz = 0;
						chopper->oty = 0;
						chopper->otx = 0;
						chopper->power = 0;
						chopper->bob = 0;
						chopper->bobstrength = 0.05f;
						chopper->timer60 = 0;
						chopper->patroltimer60 = 0;
						chopper->cw = 0;
						chopper->weaponsarmed = true;
						chopper->fireslotthing = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
						chopper->fireslotthing->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
						chopper->fireslotthing->beam->age = -1;
						chopper->fireslotthing->unk08 = -1;
						chopper->fireslotthing->unk00 = 0;
						chopper->fireslotthing->unk01 = 0;
						chopper->fireslotthing->unk0c = 0.85f;
						chopper->fireslotthing->unk10 = 0.2f;
						chopper->fireslotthing->unk14 = 0;
						chopper->dead = false;
					}
					break;
				case OBJTYPE_HELI:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						struct heliobj *heli = (struct heliobj *)obj;

						setupCreateObject(obj, index);

						heli->speed = 0;
						heli->speedaim = 0;
						heli->rotoryrot = 0;
						heli->rotoryspeed = 0;
						heli->rotoryspeedaim = 0;
						heli->yrot = 0;
						heli->speedtime60 = -1;
						heli->rotoryspeedtime = -1;
						heli->ailist = ailistFindById((uintptr_t)heli->ailist);
						heli->aioffset = 0;
						heli->aireturnlist = -1;
						heli->path = NULL;
						heli->nextstep = 0;
					}
					break;
				case OBJTYPE_TAG:
					{
						struct tag *tag = (struct tag *)obj;
						struct defaultobj *taggedobj = setupGetObjByCmdIndex(index + tag->cmdoffset);
						tag->obj = taggedobj;

						if (taggedobj) {
							taggedobj->hidden |= OBJHFLAG_TAGGED;
						}

						tagInsert(tag);
					}
					break;
				case OBJTYPE_RENAMEOBJ:
					{
						struct textoverride *override = (struct textoverride *)obj;
						struct defaultobj *targetobj = setupGetObjByCmdIndex(override->objoffset + index);
						override->obj = targetobj;

						if (targetobj) {
							targetobj->hidden |= OBJHFLAG_HASTEXTOVERRIDE;
						}

						invInsertTextOverride(override);
					}
					break;
				case OBJTYPE_BRIEFING:
					{
						struct briefingobj *briefing = (struct briefingobj *)obj;
						s32 wanttype = BRIEFINGTYPE_TEXT_PA;

						briefingInsert(briefing);

						if (lvGetDifficulty() == DIFF_A) {
							wanttype = BRIEFINGTYPE_TEXT_A;
						}

						if (lvGetDifficulty() == DIFF_SA) {
							wanttype = BRIEFINGTYPE_TEXT_SA;
						}

						if (briefing->type == wanttype) {
							g_Briefing.briefingtextnum = briefing->text;
						}
					}
					break;
				case OBJTYPE_CAMERAPOS:
					{
						struct cameraposobj *camera = (struct cameraposobj *)obj;
						camera->x = *(s32 *)&camera->x / 100.0f;
						camera->y = *(s32 *)&camera->y / 100.0f;
						camera->z = *(s32 *)&camera->z / 100.0f;
						camera->theta = *(s32 *)&camera->theta / 65536.0f;
						camera->verta = *(s32 *)&camera->verta / 65536.0f;
					}
					break;
				case OBJTYPE_BEGINOBJECTIVE:
					{
						struct objective *objective = (struct objective *)obj;

						objectiveInsert(objective);

						if ((u32)objective->index < 7) {
							g_Briefing.objectivenames[objective->index] = objective->text;
							g_Briefing.objectivedifficulties[objective->index] = objective->difficulties;
						}
					}
					break;
				case OBJECTIVETYPE_ENTERROOM:
					objectiveAddRoomEnteredCriteria((struct criteria_roomentered *)obj);
					break;
				case OBJECTIVETYPE_THROWINROOM:
					objectiveAddThrowInRoomCriteria((struct criteria_throwinroom *)obj);
					break;
				case OBJECTIVETYPE_HOLOGRAPH:
					objectiveAddHolographCriteria((struct criteria_holograph *)obj);
					break;
				case OBJTYPE_PADEFFECT:
					{
						struct padeffectobj *padeffect = (struct padeffectobj *)obj;
						if (g_LastPadEffectIndex == -1) {
							g_PadEffects = padeffect;
						}
						g_LastPadEffectIndex++;
					}
					break;
				case OBJTYPE_MINE:
					if (withobjs && (obj->flags2 & diffflag) == 0) {
						setupCreateMine((struct mineobj *)obj, index);
					}
					break;
				}

				obj = (struct defaultobj *)((u32 *)obj + setupGetCmdLength((u32 *)obj));
				index++;
			}

			index = 0;

			if (g_Vars.normmplayerisrunning && mpHasSimulants()) {
				u32 stack[4];
				s32 i;
				s32 slotsdone[MAX_BOTS];
				s32 chrnum = 0;
				s32 maxsimulants;
				s32 slotnum;

				maxsimulants = MAX_BOTS; /* PC: all bot slots available */

				sysLogPrintf(LOG_NOTE, "SIMULANT: spawning started activeBots=%d maxsim=%d",
					mpGetActiveBotCount(), maxsimulants);

				for (i = 0; i < MAX_BOTS; i++) {
					slotsdone[i] = false;
				}

				for (i = 0; i < maxsimulants; i++) {
					slotnum = rngRandom() % maxsimulants;

					while (slotsdone[slotnum]) {
						slotnum = (slotnum + 1) % maxsimulants;
					}

					if (mpIsParticipantActive(slotnum + MAX_PLAYERS)
							&& mpIsSimSlotEnabled(slotnum)) {
						sysLogPrintf(LOG_NOTE, "SIMULANT: allocating chrnum=%d slot=%d", chrnum, slotnum);
						botmgrAllocateBot(chrnum, slotnum);
						chrnum++;
					} else {
						sysLogPrintf(LOG_NOTE, "SIMULANT: SKIP slot=%d active=%d isEnabled=%d",
							slotnum,
							mpIsParticipantActive(slotnum + MAX_PLAYERS) ? 1 : 0,
							mpIsSimSlotEnabled(slotnum));
					}

					slotsdone[slotnum] = true;
				}

				sysLogPrintf(LOG_NOTE, "SIMULANT: spawning done total=%d", chrnum);
			} else {
				sysLogPrintf(LOG_NOTE, "SIMULANT: NOT spawning normmplay=%d hasSimulants=%d activeBots=%d",
					g_Vars.normmplayerisrunning, mpHasSimulants(), mpGetActiveBotCount());
			}

			if (g_Vars.normmplayerisrunning) {
				sysLogPrintf(LOG_NOTE, "SETUP: calling scenarioInitProps");
				scenarioInitProps();
				sysLogPrintf(LOG_NOTE, "SETUP: scenarioInitProps done");
			}

			sysLogPrintf(LOG_NOTE, "SETUP: iterating props (g_StageSetup.props=%p)", (void *)g_StageSetup.props);
			obj = (struct defaultobj *)g_StageSetup.props;

			while (obj->type != OBJTYPE_END) {
				switch (obj->type) {
				case OBJTYPE_BASIC:
				case OBJTYPE_KEY:
				case OBJTYPE_AMMOCRATE:
				case OBJTYPE_WEAPON:
				case OBJTYPE_SINGLEMONITOR:
				case OBJTYPE_DEBRIS:
				case OBJTYPE_MULTIAMMOCRATE:
				case OBJTYPE_SHIELD:
				case OBJTYPE_GASBOTTLE:
				case OBJTYPE_29:
				case OBJTYPE_GLASS:
				case OBJTYPE_SAFE:
				case OBJTYPE_TINTEDGLASS:
					if (obj->prop && (obj->flags & OBJFLAG_INSIDEANOTHEROBJ)) {
						s32 offset = obj->pad;
						struct defaultobj *owner = setupGetObjByCmdIndex(index + offset);

						if (owner && owner->prop) {
							obj->hidden |= OBJHFLAG_HASOWNER;
							modelSetScale(obj->model, obj->model->scale);
							propReparent(obj->prop, owner->prop);
						}
					}
					break;
				case OBJTYPE_LINKGUNS:
					{
						struct linkgunsobj *link = (struct linkgunsobj *)obj;
						s32 gun1index = link->offset1 + index;
						s32 gun2index = link->offset2 + index;
						struct weaponobj *gun1 = (struct weaponobj *)setupGetCmdByIndex(gun1index);
						struct weaponobj *gun2 = (struct weaponobj *)setupGetCmdByIndex(gun2index);

						if (gun1 && gun2
								&& gun1->base.type == OBJTYPE_WEAPON
								&& gun2->base.type == OBJTYPE_WEAPON) {
							scenarioSourceSetupGraphRecordBehaviorLink(
								OBJTYPE_LINKGUNS, index,
								gun1index, gun2index, -1, -1, -1);
							propweaponSetDual(gun1, gun2);
						}
					}
					break;
				case OBJTYPE_LINKLIFTDOOR:
					{
						struct linkliftdoorobj *link = (struct linkliftdoorobj *)obj;
						uintptr_t dooroffset = (uintptr_t)link->door;
						uintptr_t liftoffset = (uintptr_t)link->lift;
						s32 doorindex = index + (s32)dooroffset;
						s32 liftindex = index + (s32)liftoffset;
						struct defaultobj *door = setupGetObjByCmdIndex(doorindex);
						struct defaultobj *lift = setupGetObjByCmdIndex(liftindex);

						if (door && door->prop && lift && lift->prop) {
							scenarioSourceSetupGraphRecordBehaviorLink(
								OBJTYPE_LINKLIFTDOOR, index,
								doorindex, liftindex, -1,
								link->stopnum, -1);
							link->door = door->prop;
							link->lift = lift->prop;

							setupCreateLiftDoor(link);

							door->hidden |= OBJHFLAG_LIFTDOOR;
						}
					}
					break;
				case OBJTYPE_SAFEITEM:
					{
						struct safeitemobj *link = (struct safeitemobj *)obj;
						uintptr_t itemoffset = (uintptr_t)link->item;
						uintptr_t safeoffset = (uintptr_t)link->safe;
						uintptr_t dooroffset = (uintptr_t)link->door;
						s32 itemindex = index + (s32)itemoffset;
						s32 safeindex = index + (s32)safeoffset;
						s32 doorindex = index + (s32)dooroffset;
						struct defaultobj *item = setupGetObjByCmdIndex(itemindex);
						struct defaultobj *safe = setupGetObjByCmdIndex(safeindex);
						struct defaultobj *door = setupGetObjByCmdIndex(doorindex);

						if (item && item->prop
								&& safe && safe->prop && safe->type == OBJTYPE_SAFE
								&& door && door->prop && door->type == OBJTYPE_DOOR) {
							scenarioSourceSetupGraphRecordBehaviorLink(
								OBJTYPE_SAFEITEM, index,
								itemindex, safeindex, doorindex, -1, -1);
							link->item = item;
							link->safe = (struct safeobj *)safe;
							link->door = (struct doorobj *)door;

							setupCreateSafeItem(link);

							item->flags2 |= OBJFLAG2_LINKEDTOSAFE;
							door->flags2 |= OBJFLAG2_LINKEDTOSAFE;
						}
					}
					break;
				case OBJTYPE_PADLOCKEDDOOR:
					{
						struct padlockeddoorobj *link = (struct padlockeddoorobj *)obj;
						uintptr_t dooroffset = (uintptr_t)link->door;
						uintptr_t lockoffset = (uintptr_t)link->lock;
						s32 doorindex = index + (s32)dooroffset;
						s32 lockindex = index + (s32)lockoffset;
						struct defaultobj *door = setupGetObjByCmdIndex(doorindex);
						struct defaultobj *lock = setupGetObjByCmdIndex(lockindex);

						if (door && door->prop && lock && lock->prop
								&& door->type == OBJTYPE_DOOR) {
							scenarioSourceSetupGraphRecordBehaviorLink(
								OBJTYPE_PADLOCKEDDOOR, index,
								doorindex, lockindex, -1, -1, -1);
							link->door = (struct doorobj *)door;
							link->lock = lock;

							setupCreatePadlockedDoor(link);

							door->hidden |= OBJHFLAG_PADLOCKEDDOOR;
						}
					}
					break;
				case OBJTYPE_CONDITIONALSCENERY:
					{
						struct linksceneryobj *link = (struct linksceneryobj *)obj;
						uintptr_t triggeroffset = (uintptr_t)link->trigger;
						uintptr_t unexpoffset = (uintptr_t)link->unexp;
						uintptr_t expoffset = (uintptr_t)link->exp;
						s32 triggerindex = index + (s32)triggeroffset;
						s32 unexpindex = unexpoffset ? index + (s32)unexpoffset : -1;
						s32 expindex = expoffset ? index + (s32)expoffset : -1;
						struct defaultobj *trigger = setupGetObjByCmdIndex(triggerindex);
						struct defaultobj *unexp = NULL;
						struct defaultobj *exp = NULL;
						s32 alwayszero = 0;

						if (unexpoffset) {
							unexp = setupGetObjByCmdIndex(unexpindex);
						}

						if (expoffset) {
							exp = setupGetObjByCmdIndex(expindex);
						}

						if (trigger && trigger->prop
								&& (unexpoffset == 0 || (unexp && unexp->prop))
								&& (expoffset == 0 || (exp && exp->prop))) {
							scenarioSourceSetupGraphRecordBehaviorLink(
								OBJTYPE_CONDITIONALSCENERY, index,
								triggerindex, unexpindex, expindex, -1, -1);
							link->trigger = trigger;
							link->unexp = unexp;
							link->exp = exp;

							setupCreateConditionalScenery(link);

							trigger->hidden |= OBJHFLAG_CONDITIONALSCENERY;

							if (unexpoffset) {
								unexp->hidden |= OBJHFLAG_CONDITIONALSCENERY;
							}

							// This gets optimised out but makes v0 unavailable
							// for storing OBJHFLAG_CONDITIONALSCENERY, which is required
							// for a match. Any function call would work.
							if (alwayszero) {
								rngRandom();
							}

							if (expoffset) {
								exp->hidden |= OBJHFLAG_CONDITIONALSCENERY;
								exp->flags2 |= OBJFLAG2_INVISIBLE;
							}

							if (trigger->hidden & OBJHFLAG_BLOCKEDPATH) {
								objSetBlockedPathUnblocked(trigger, false);
							}
						}
					}
					break;
				case OBJTYPE_BLOCKEDPATH:
					{
						struct blockedpathobj *blockedpath = (struct blockedpathobj *)obj;
						uintptr_t objoffset = (uintptr_t)blockedpath->blocker;
						s32 blockerindex = index + (s32)objoffset;
						struct defaultobj *blocker = setupGetObjByCmdIndex(blockerindex);

						if (blocker && blocker->prop) {
							scenarioSourceSetupGraphRecordBehaviorLink(
								OBJTYPE_BLOCKEDPATH, index,
								blockerindex, -1, -1,
								blockedpath->waypoint1,
								blockedpath->waypoint2);
							blockedpath->blocker = blocker;

							setupCreateBlockedPath(blockedpath);

							blocker->hidden |= OBJHFLAG_BLOCKEDPATH;

							if (blocker->hidden & OBJHFLAG_CONDITIONALSCENERY) {
								objSetBlockedPathUnblocked(blocker, false);
							}
						}
					}
					break;
				}

				obj = (struct defaultobj *)((u32 *)obj + setupGetCmdLength((u32 *)obj));
				index++;
			}
			sysLogPrintf(LOG_NOTE, "SETUP: prop iteration done (%d objects)", index);

			/* B-228 Option E (2026-04-24): SP-in-MP transport-prop overlay.
			 *
			 * For SP-class stages hosted as MP arenas, the MP setup blob
			 * ships with no LIFT or ESCASTEP entries (audited in
			 * context/audits/sp-stage-mp-readiness-2026-04-24.md). The P5
			 * mptransport_diffflag relax (commit 216fd27f) is therefore a
			 * no-op on the current codebase -- nothing to filter relaxed.
			 *
			 * This pass additionally loads the SP setup blob for the same
			 * stage and walks ONLY OBJTYPE_LIFT + OBJTYPE_ESCASTEP from
			 * it, running the same creation logic the main switch uses.
			 * The SP blob stays alive in MEMPOOL_STAGE so that the lift's
			 * doors[i] pointers (resolved against SP-blob doorobj structs
			 * by setupGetCmdByIndex during the overlay pass) remain valid
			 * for the stage lifetime. We do NOT load the SP doors as live
			 * props -- only the doorobj structs in the SP blob. The lift
			 * moves and players ride; door coupling is reduced to the
			 * struct-data fields the lift reads (door coords for tile
			 * extents). The B-228 audit's residual crash class (AI lift-
			 * handle resolution / pathfinding waypoint missing-prop) is
			 * tracked separately as B-228b.
			 *
			 * Stage list: the SP-in-MP class identified by P5 + the 4-23
			 * readiness audit. Add more as future audits flag them. */
			if (g_Vars.mplayerisrunning && (
					stagenum == STAGE_CITRAINING ||
					stagenum == STAGE_CHICAGO ||
					stagenum == STAGE_VILLA ||
					stagenum == STAGE_INFILTRATION ||
					stagenum == STAGE_G5BUILDING ||
					stagenum == STAGE_PELAGIC)) {
				catalog_stage_result_t spStage;
				if (catalogGetStageResultByIndex(g_StageIndex, &spStage) > 0) {
					s32 spSetupSize = 0;
					struct stagesetup *spSetupHdr =
						(struct stagesetup *)scenarioSourceLoadSetupForStage(
							&spStage, 0, &spSetupSize);
					if (spSetupHdr) {
						u32 *spProps = NULL;
						u32 *savedMpProps = g_StageSetup.props;

						s32 spIndex = 0;
						s32 spLifts = 0;
						s32 spEscaSteps = 0;

						if (!setupResolvePropsInLoadedSetup(spSetupHdr, spSetupSize, &spProps)) {
							sysLogPrintf(LOG_WARNING,
								"SETUP.LIFT: SP-in-MP stagenum=0x%02x -- invalid SP setup props (size=%d raw=%p)",
								(u32)stagenum, spSetupSize, (void *)spSetupHdr->props);
							g_StageSetup.props = savedMpProps;
						} else {
							/* Repoint g_StageSetup.props to the SP blob so that
							 * setupGetCmdByIndex (used by the lift door-link
							 * logic) resolves SP-internal cross-references
							 * correctly. Restored at end of this block. */
							g_StageSetup.props = spProps;
							obj = (struct defaultobj *)spProps;

							uintptr_t spSetupBase = (uintptr_t)spSetupHdr;
							uintptr_t spSetupEnd = spSetupBase + (uintptr_t)spSetupSize;
							s32 spGuard = 0;

							while ((uintptr_t)obj + sizeof(u32) <= spSetupEnd && obj->type != OBJTYPE_END) {
								u32 cmdlen = setupGetCmdLength((u32 *)obj);
								uintptr_t nextobj = (uintptr_t)((u32 *)obj + cmdlen);

								if (cmdlen == 0 || nextobj <= (uintptr_t)obj || nextobj > spSetupEnd) {
									sysLogPrintf(LOG_WARNING,
										"SETUP.LIFT: SP-in-MP stagenum=0x%02x -- invalid SP setup command type=%d index=%d len=%u",
										(u32)stagenum, (s32)obj->type, spIndex, cmdlen);
									break;
								}

								if (++spGuard > 2048) {
									sysLogPrintf(LOG_WARNING,
										"SETUP.LIFT: SP-in-MP stagenum=0x%02x -- SP setup command guard tripped",
										(u32)stagenum);
									break;
								}

								if (obj->type == OBJTYPE_LIFT && withobjs) {
								/* SP authors did not set OBJFLAG2_EXCLUDE_*
								 * bits for MP, so the diffflag filter is a
								 * no-op here -- include unconditionally. */
								struct liftobj *lift = (struct liftobj *)obj;
								struct modelstate *modelstate;
								s32 modelnum = obj->modelnum;
								struct prop *prop;
								s32 i;

								lift->accel    = PALUPF(*(s32 *)&lift->accel) / 65536.0f;
								lift->maxspeed = PALUPF(*(s32 *)&lift->maxspeed) / 65536.0f;
								lift->dist     = 0;
								lift->speed    = 0;
								lift->levelcur = 0;
								lift->levelaim = 0;

								for (i = 0; i < (s32)ARRAYCOUNT(lift->doors); i++) {
									if (lift->doors[i]) {
										lift->doors[i] = (struct doorobj *)setupGetCmdByIndex(
												spIndex + *(s32*)&lift->doors[i]);
									}
								}

								obj->geocount = 1;
								setupLoadModeldef(modelnum);
								modelstate = &g_ModelStates[modelnum];
								if (modelstate->modeldef) {
									if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_WALL1))       obj->geocount++;
									if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_WALL2))       obj->geocount++;
									if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_WALL3))       obj->geocount++;
									if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_DOORBLOCK))   obj->geocount++;
									if (modelGetPartRodata(modelstate->modeldef, MODELPART_LIFT_FLOORNONRECT2)) obj->geocount++;
								}

								obj->flags &= ~OBJFLAG_00000100;
								setupCreateObject(obj, spIndex);

								prop = obj->prop;
								if (prop) {
									lift->prevpos.x = prop->pos.x;
									lift->prevpos.y = prop->pos.y;
									lift->prevpos.z = prop->pos.z;
									liftUpdateTiles(lift, true);

									/* Auto-register against g_Lifts[] -- same
									 * rationale as the MP path: SP-authored
									 * lifts had aiActivateLift opcodes in the
									 * SP AI script, but we are not loading
									 * the SP AI list (out of scope). */
									{
										s32 pi;
										for (pi = 0; pi < (s32)ARRAYCOUNT(lift->pads); pi++) {
											if (lift->pads[pi] < 0) continue;
											struct pad padinfo;
											padUnpack(lift->pads[pi], PADFIELD_LIFT, &padinfo);
											if (padinfo.liftnum > 0 &&
													(u32)padinfo.liftnum <= ARRAYCOUNT(g_Lifts)) {
												liftActivate(prop, padinfo.liftnum);
												break;
											}
										}
									}
								}
								spLifts++;
							} else if (obj->type == OBJTYPE_ESCASTEP && withobjs) {
								struct escalatorobj *step = (struct escalatorobj *)obj;
								struct prop *prop;
#ifdef AVOID_UB
								Mtxf sp1a8;
#else
								f32 sp1a8[12];
#endif
								f32 sp184[3][3];

								setupCreateObject(obj, spIndex);
								prop = obj->prop;
								if (prop) {
									step->prevpos.x = prop->pos.x;
									step->prevpos.y = prop->pos.y;
									step->prevpos.z = prop->pos.z;
								}
								if (obj->flags & OBJFLAG_ESCSTEP_ZALIGNED) {
									step->frame = escstepy;
									escstepy += 40;
									mtx4LoadYRotation(4.7116389274597f, (Mtxf *)&sp1a8);
									mtx4ToMtx3((Mtxf *)&sp1a8, sp184);
									mtx00016110(sp184, obj->realrot);
								} else {
									step->frame = escstepx;
									escstepx += 40;
									mtx4LoadYRotation(M_BADPI, (Mtxf *)&sp1a8);
									mtx4ToMtx3((Mtxf *)&sp1a8, sp184);
									mtx00016110(sp184, obj->realrot);
								}
								spEscaSteps++;
							}
								obj = (struct defaultobj *)nextobj;
								spIndex++;
							}

							if ((uintptr_t)obj + sizeof(u32) > spSetupEnd) {
								sysLogPrintf(LOG_WARNING,
									"SETUP.LIFT: SP-in-MP stagenum=0x%02x -- SP setup ended before OBJTYPE_END",
									(u32)stagenum);
							}

						/* Restore the MP props pointer for the rest of
						 * setupCreateProps (spawn-pool computation,
						 * gunmem block, weapon model preload). The SP
						 * blob lives in MEMPOOL_STAGE; not freed here. */
						g_StageSetup.props = savedMpProps;

						sysLogPrintf(LOG_NOTE,
							"SETUP.LIFT: SP-in-MP stagenum=0x%02x lifts=%d escasteps=%d",
							(u32)stagenum, spLifts, spEscaSteps);
						}
					} else {
						sysLogPrintf(LOG_WARNING,
							"SETUP.LIFT: SP-in-MP stagenum=0x%02x -- public setup overlay source unavailable; skipping setup overlay",
							(u32)stagenum);
					}
				}
			}

			if (g_Vars.normmplayerisrunning) {
				spawn_aabb_t aabb;
				f32 span_x = 0;
				f32 span_z = 0;
				f32 dominant_span = 0;
				s32 span_bonus = 0;

				desiredPickups = PLAYERCOUNT() + g_BotCount;
				if (desiredPickups < 4) {
					desiredPickups = 4;
				}

				spawnPoolComputeAABB(&aabb);
				if (aabb.valid) {
					span_x = aabb.max.x - aabb.min.x;
					span_z = aabb.max.z - aabb.min.z;
					dominant_span = span_x > span_z ? span_x : span_z;
					if (dominant_span > 4000.0f) {
						span_bonus = (s32)((dominant_span - 4000.0f) / 3000.0f) + 1;
					}
				}

				desiredPickups += span_bonus;
				if (desiredPickups > 16) {
					desiredPickups = 16;
				}
			}

			if (g_Vars.normmplayerisrunning
					&& s_SetupMpCreatedWeaponCount < desiredPickups) {
				/* B-181 (v2 2026-04-23): Too few (or zero) world pickups for this
				 * map/participant count. Force spawn-with-weapon to keep matches
				 * armed.
				 *
				 * REVISED: DO NOT override an explicit user choice. If the user
				 * picked a specific spawn weapon (e.g. remote mine) it must be
				 * respected even when markers are low. Playtest 2026-04-23: user
				 * selected `base:remotemine` but saw Falcon (Silenced) at spawn,
				 * because the old "always override" branch replaced the choice
				 * with the first valid weapon in the set. Gate the override on
				 * `spawnWeaponNum == 0xFF` (random) or `== 0` (unset) so the
				 * override only fires when the user has no explicit preference.
				 *
				 * Historic concern was "otherwise players see weapons they
				 * never selected" - that concern is real for the random case
				 * (player didn't explicitly select anything, so fall back to
				 * the set). It does NOT apply when the user did select one.
				 *
				 * Resolve from MPWEAPON_* explicitly. Weapon catalog entries
				 * also carry runtime WEAPON_* identity, so generic numeric
				 * lookup is intentionally avoided here. */
				/* INV-4 / Cohort D (player-init-architectural-fixes-2026-04-26):
				 * use matchOptionsForceBit so the bit is recorded in
				 * options_engine_forced AT THE SAME TIME it is OR'd into
				 * options. matchStart() restores user-original options at the
				 * top of each new match by clearing the forced bits, so this
				 * engine override is bounded to the current match and does
				 * not silently mutate the user's persistent menu choice. */
				matchOptionsForceBit(&g_MatchConfig.options,
				                     &g_MatchConfig.options_engine_forced,
				                     MPOPTION_SPAWNWITHWEAPON);
				g_MpSetup.options |= MPOPTION_SPAWNWITHWEAPON;

				/* S483c (2026-04-27, B-263): use the shared predicate so the
				 * FIESTA sentinel (0xFE) is correctly classified as "not a
				 * resolved user pick".  Pre-fix the open-coded `!= 0xFF && != 0`
				 * check let 0xFE flow through as a "user pick" and the matching
				 * model-preload site at line 2870 indexed g_Weapons[254]. */
				const bool userPickedSpawnWeapon =
					(spawnWeaponNumIsResolved((s32)g_MatchConfig.spawnWeaponNum) != 0);

				if (!userPickedSpawnWeapon) {
					s32 setSpawnMpw = -1;
					const char *setSpawnId = NULL;
					for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
						s32 mpw = (s32)g_MpSetup.weapons[i];
						if (mpw == MPWEAPON_NONE
								|| mpw == MPWEAPON_SHIELD
								|| mpw == MPWEAPON_DISABLED) {
							continue;
						}
						setSpawnId = catalogWeaponIdByMpWeaponId(mpw);
						if (setSpawnId && setSpawnId[0]) {
							setSpawnMpw = mpw;
							break;
						}
					}

					if (setSpawnId && setSpawnId[0]) {
						/* Random / unset only: fill spawn weapon from first
						 * valid weapon in the configured set so random-armed
						 * players still see a weapon from the selection. */
						strncpy(g_MatchConfig.spawn_weapon_id, setSpawnId,
							sizeof(g_MatchConfig.spawn_weapon_id) - 1);
						g_MatchConfig.spawn_weapon_id[sizeof(g_MatchConfig.spawn_weapon_id) - 1] = '\0';
						g_MatchConfig.spawnWeaponNum = (u8)catalogGetMpWeaponNum(setSpawnMpw);
						sysLogPrintf(LOG_NOTE,
							"SETUP: random spawn weapon resolved from set: id='%s' num=%d",
							setSpawnId, (s32)g_MatchConfig.spawnWeaponNum);
					}
				} else {
					sysLogPrintf(LOG_NOTE,
						"SETUP: user-picked spawn weapon '%s' num=%d preserved (markers low but choice respected)",
						g_MatchConfig.spawn_weapon_id[0] ? g_MatchConfig.spawn_weapon_id : "(unnamed)",
						(s32)g_MatchConfig.spawnWeaponNum);
				}

				mpSpawnFallbackApplied = true;
			}
		}
	} else {
		chrmgrConfigure(0);
	}

	if (mpSpawnFallbackApplied) {
		sysLogPrintf(LOG_WARNING,
			"SETUP: world pickups %d below target %d (markers=%d); forcing spawn-with-weapon fallback id='%s'",
			s_SetupMpCreatedWeaponCount, desiredPickups, s_SetupMpWeaponLocationCount,
			g_MatchConfig.spawn_weapon_id[0] ? g_MatchConfig.spawn_weapon_id : "(random)");
	}

	/* 2026-04-23 B-219 v3 / B-229 architectural fix: manifest-driven MP weapon
	 * FP model loading. Until now, modelmgrLoadProjectileModeldefs was invoked
	 * only from setupPlaceWeapon at each map pickup marker, so pickup-less
	 * arenas (Chicago CS: markers=0) and random-spawn / random-set matches had
	 * un-loaded first-person models the moment spawn-with-weapon tried to
	 * equip them (player log: "spawned with weapon 2 (Falcon 2)" but empty
	 * hands, no fire). Intent per the manifest contract: the match manifest
	 * already registers every non-empty entry in g_MpSetup.weapons[] (see
	 * port/src/net/netmanifest.c:446-463) and the user-selected spawn weapon
	 * is part of the match config. Load models for every entry here, end of
	 * stage setup, so the FP pipeline is armed regardless of map placement.
	 * Random selection is covered because the full set iterates; a specific
	 * spawn weapon outside the set is covered by the explicit spawnWeaponNum
	 * load at the bottom. Cheap (O(NUM_MPWEAPONSLOTS)) and idempotent:
	 * modelmgrLoadProjectileModeldefs no-ops if the def is already resident. */
	if (g_Vars.normmplayerisrunning || g_Vars.lvmpbotlevel) {
		s32 slot;
		for (slot = 0; slot < NUM_MPWEAPONSLOTS; slot++) {
			s32 mpw = (s32)g_MpSetup.weapons[slot];
			s32 wnum;
			if (mpw == MPWEAPON_NONE
					|| mpw == MPWEAPON_SHIELD
					|| mpw == MPWEAPON_DISABLED) {
				continue;
			}
			wnum = catalogGetMpWeaponNum(mpw);
			if (wnum > 0) {
				modelmgrLoadProjectileModeldefs(wnum);
			}
		}
		/* S483c (2026-04-27, B-263): only preload when spawnWeaponNum is a
		 * resolved real WEAPON_* enum.  Reserved values (0, 0xFF random,
		 * 0xFE FIESTA) all roll per-spawn from the active set or manifest
		 * pool, which the per-slot loop above already preloaded.  Pre-fix
		 * the FIESTA sentinel 0xFE flowed through this guard and indexed
		 * g_Weapons[254] -- AV crash. spawnWeaponNumIsResolved is the
		 * single source of truth in matchsetup.h. */
		if (spawnWeaponNumIsResolved((s32)g_MatchConfig.spawnWeaponNum)) {
			modelmgrLoadProjectileModeldefs((s32)g_MatchConfig.spawnWeaponNum);
		}
		sysLogPrintf(LOG_NOTE,
			"SETUP: MP weapon models preloaded from match config (spawnWeapon=%d random=%d)",
			(s32)g_MatchConfig.spawnWeaponNum,
			g_MatchConfig.spawnWeaponNum == 0xFF ? 1 : 0);
	}

	sysLogPrintf(LOG_NOTE, "SETUP: calling stageAllocateBgChrs");
	stageAllocateBgChrs();
	sysLogPrintf(LOG_NOTE, "SETUP: setupLoadStage complete");
}
