#include <ultra64.h>
#include <string.h>
#include "constants.h"
#include "memsizes.h"
#include "asset_fallback_telemetry.h" /* c3849 Wave 1 */
#include "asset_source_debug.h"
#include "asset_runtime.h"
#include "assetcatalog.h" /* Catalog-owned asset identity and source handles */
#include "assetcatalog_load.h"
#include "assetload.h"    /* Provider-aware asset load bridge */
#include "../lib/naudio/n_sndp.h"
#include "game/bondmove.h"
#include "game/cheats.h"
#include "game/chraction.h"
#include "game/inv.h"
#include "game/game_006900.h"
#include "game/chr.h"
#include "game/prop.h"
#include "game/propsnd.h"
#include "game/game_096360.h"
#include "game/acosfasinf.h"
#include "game/game_096b20.h"
#include "game/quaternion.h"
#include "game/game_097aa0.h"
#include "game/bondgun.h"
#include "game/gunfx.h"
#include "game/game_0b0fd0.h"
#include "game/modeldef.h"
#include "game/modelmgr.h"
#include "game/tex.h"
#include "game/camera.h"
#include "game/player.h"
#include "game/mtxf2lbulk.h"
#include "game/gfxmemory.h"
#include "game/sight.h"
#include "pdgui_weapon_reticle.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "game/smoke.h"
#include "game/game_1531a0.h"
#include "game/file.h"
#include "game/lv.h"
#include "game/texdecompress.h"
#include "game/zbuf.h"
#include "game/training.h"
#include "game/lang.h"
#include "game/mplayer/mplayer.h"
#include "game/pak.h"
#include "game/options.h"
#include "game/propobj.h"
/* S484-followup-3 (2026-05-01): pool-range + canary accessors for the
 * fire-time recoil crash instrumentation in bgun0f0a5550. */
#include "game/objectives.h"
#include "bss.h"
#include "lib/collision.h"
#include "lib/vi.h"
#include "lib/joy.h"
#include "lib/main.h"
#include "lib/model.h"
#include "lib/snd.h"
#include "lib/rng.h"
#include "lib/mtx.h"
#include "lib/anim.h"
#include "lib/lib_317f0.h"
#include "data.h"
#include "types.h"
#include "catalog_model_generation.h"
#include "weapon_graph_runtime.h"
#include "weapon_graph_v2_runtime.h"
#include "platform.h"
#include "game/stagetable.h"
#include "video.h"
#include "net/net.h"
#include "net/netmsg.h"
#include "modasset_compiler.h"
#include "model_source_path.h"
#include "fs.h"
#include "loader_pool.h"
#include "system.h" /* B-246 instrumentation: sysLogPrintf for LOG.WPN.DIAG lines */

#define GUNLOADSTATE_FLUX     0
#define GUNLOADSTATE_MODEL    1
#define GUNLOADSTATE_TEXTURES 2
#define GUNLOADSTATE_DLS      3
#define GUNLOADSTATE_LOADED   4
#define GUNLOADSTATE_FAILED   5

#define MASTERLOADSTATE_FLUX   0
#define MASTERLOADSTATE_HANDS  1
#define MASTERLOADSTATE_GUN    2
#define MASTERLOADSTATE_CARTS  3
#define MASTERLOADSTATE_LOADED 4

// Max downwards pitch when changing guns or reloading a classic gun
#define MAX_PITCH 0.87252569198608f

#if VERSION >= VERSION_PAL_BETA
struct sndstate *g_CasingAudioHandles[2];
s32 var8009d0d8;
u32 fill2;
struct sndstate *g_BgunAudioHandles[MAX_PLAYERS];
s32 var8009d0dc;
u32 fill2_2;
s32 var8009d0f0[3];
u32 var8009d0fc;
u32 var8009d100;
u32 var8009d104;
u32 var8009d108;
u32 var8009d10c;
u32 var8009d110;
u32 var8009d114;
u32 var8009d118;
u32 var8009d11c;
u32 var8009d120;
u32 var8009d124;
u32 var8009d128;
u32 var8009d12c;
u32 var8009d130;
u32 var8009d134;
u32 var8009d138;
u32 var8009d13c;
f32 var8009d140;
struct hand *var8009d144;
s32 var8009d148;
u32 var8009d14c;
struct fireslot g_Fireslots[20];
#elif VERSION >= VERSION_NTSC_1_0
struct sndstate *g_CasingAudioHandles[2];
s32 var8009d0d8;
s32 var8009d0dc;
struct sndstate *g_BgunAudioHandles[MAX_PLAYERS];
s32 var8009d0f0[3];
u32 var8009d0fc;
u32 var8009d100;
u32 var8009d104;
u32 var8009d108;
u32 var8009d10c;
u32 var8009d110;
u32 var8009d114;
u32 var8009d118;
u32 var8009d11c;
u32 var8009d120;
u32 var8009d124;
u32 var8009d128;
u32 var8009d12c;
u32 var8009d130;
u32 var8009d134;
u32 var8009d138;
u32 var8009d13c;
f32 var8009d140;
struct hand *var8009d144;
s32 var8009d148;
u32 var8009d14c;
struct fireslot g_Fireslots[20];
#else
s32 var8009d0dc;
u32 var800a1800nb;
s32 var8009d0f0[3];
u32 var8009d0fc;
u32 var8009d100;
u32 var8009d104;
u32 var8009d108;
u32 var8009d10c;
u32 var8009d110;
u32 var8009d114;
u32 var8009d118;
u32 var8009d11c;
u32 var8009d120;
u32 var8009d124;
u32 var8009d128;
u32 var8009d12c;
u32 var8009d130;
u32 var8009d134;
u32 var8009d138;
u32 var8009d13c;
f32 var8009d140;
struct hand *var8009d144;
s32 var8009d148;
u32 var8009d14c;
struct sndstate *g_CasingAudioHandles[2];
s32 var8009d0d8;
struct sndstate *g_BgunAudioHandles[MAX_PLAYERS];
struct fireslot g_Fireslots[20];
u32 fill2[1];
#endif

Lights1 var80070090 = gdSPDefLights1(0x96, 0x96, 0x96, 0xff, 0xff, 0xff, 0xb2, 0x4d, 0x2e);

#ifdef PLATFORM_64BIT
// On 64-bit PC, gun model data loaded into gunmem contains structs with pointer-sized fields
// that expand from 4 to 8 bytes after preprocessing (similar to bg room data). 2x the N64 size
// is a safe upper bound. Per-stage tuning is still possible via stageGetCurrent()->extragunmem.
u32 g_BgunGunMemBaseSizeDefault = 150 * 1024 * 2;
#else
u32 g_BgunGunMemBaseSizeDefault = 150 * 1024;
#endif

u16 g_CartFileNums[] = {
	FILE_GCARTRIDGE,
	FILE_GCARTRIFLE,
	FILE_GCARTBLUE,
	FILE_GCARTSHELL,
};

u32 var800700b8 = 0x00000000;

char var800700bc[][10] = {
	{ 'i','d','l','e'                     }, // "idle"
	{ 'p','r','e','p','a','r','e'         }, // "prepare"
	{ 'c','a','n','t','u','s','e'         }, // "cantuse"
	{ 'n','o','a','m','m','o'             }, // "noammo"
	{ 'u','s','e','2'                     }, // "use2"
	{ 'c','h','a','n','g','e'             }, // "change"
	{ 'u','p','g','r','a','d','e'         }, // "upgrade"
	{ 'c','h','a','n','g','e','f','n'     }, // "changefn"
	{ 'i','d','l','e','s','t','u','c','k' }, // "idlestuck"
	{ 'x','x','x'                         }, // "xxx"
};

s32 g_BgunGeMuzzleFlashes = false;

/* GGV hand services begin: borrowed current-player/device context. */
#include "gameplay_graph_values.h"
struct bgunhandcontext {
    s32 handnum, weaponnum, contpad1, contpad2;
};
static ggv_value bgunHandRead(void *opaque, ggv_binding binding)
{
    struct bgunhandcontext *context = opaque;
    ggv_value value = {0};
    switch (binding) {
    case GGV_HAND_NUMBER: value.i = context->handnum; break;
    case GGV_HAND_LEFT_NUMBER: value.i = HAND_LEFT; break;
    case GGV_PAD_1: value.i = context->contpad1; break;
    case GGV_PAD_2: value.i = context->contpad2; break;
    case GGV_CONTROL_MODE_21: value.i = CONTROLMODE_21; break;
    case GGV_PAK_RUMBLE_TYPE: value.i = PAKTYPE_RUMBLE; break;
    case GGV_CURRENT_HAND_SLIDEINC:
        value.b = g_Vars.currentplayer->hands[context->handnum].slideinc;
        break;
    default: break;
    }
    return value;
}
static void bgunHandWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunhandcontext *context = opaque;
    if (binding == GGV_CURRENT_HAND_SLIDEINC) {
        g_Vars.currentplayer->hands[context->handnum].slideinc = value.b;
    }
}
static ggv_value bgunHandCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value c)
{
    struct bgunhandcontext *context = opaque;
    ggv_value value = {0};
    (void)b; (void)c;
    switch (service) {
    case GGV_GET_PADS:
        joyGetContpadNumsForPlayer(g_Vars.currentplayernum,
            &context->contpad1, &context->contpad2);
        break;
    case GGV_CONTROL_MODE:
        value.i = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);
        break;
    /* The native s8 API narrowing occurs at the same leaf call boundary. */
    case GGV_PAK_TYPE: value.i = pakGetType(a.i); break;
    case GGV_WEAPON_DUALWIELD:
        value.b = weaponHasFlag(context->weaponnum, WEAPONFLAG_DUALWIELD);
        break;
    default: break;
    }
    return value;
}
static ggv_value bgunHandCall4(void *opaque, ggv_service4 service,
        ggv_value a, ggv_value b, ggv_value c, ggv_value d)
{
    ggv_value value = {0};
    (void)opaque;
    if (service == GGV_PAK_RUMBLE) {
        /* PC VERSION>=NTSC1 device is s32; do not reuse pakGetType's s8. */
        pakRumble(a.i, b.f, c.i, d.i);
    }
    return value;
}
/* GGV hand services end. */

#if !MATCHING || VERSION >= VERSION_NTSC_1_0
void bgunRumble(s32 handnum, s32 weaponnum)
{
#if VERSION >= VERSION_NTSC_1_0
    struct bgunhandcontext context = {0};
    context.handnum = handnum;
    context.weaponnum = weaponnum;
    ggv_host host = { &context, bgunHandRead, bgunHandWrite, bgunHandCall, bgunHandCall4 };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_HAND_RUMBLE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: hand rumble refused immutable program");
    }
#else
	s32 stack1;
	s32 stack2;
	s8 contpad1;
	s8 contpad2;
	bool contpad1hasrumble;
	bool contpad2hasrumble;
	s32 contpadtouse1;
	s32 contpadtouse2;
	s32 controlmode = optionsGetControlMode(g_Vars.currentplayerstats->mpindex);

	if (controlmode >= CONTROLMODE_21 && controlmode < CONTROLMODE_PC) {
		contpad1hasrumble = pakGetType(g_Vars.currentplayernum) == PAKTYPE_RUMBLE;
		contpad2hasrumble = pakGetType(g_Vars.currentplayernum + PLAYERCOUNT()) == PAKTYPE_RUMBLE;

		if (contpad1hasrumble && contpad2hasrumble) {
			contpadtouse1 = g_Vars.currentplayernum;

			if (handnum == HAND_LEFT) {
				contpadtouse1 += PLAYERCOUNT();
			}

			pakRumble(contpadtouse1, 0.2f, 2, 4);
		} else {
			contpadtouse2 = g_Vars.currentplayernum;

			if (contpad2hasrumble) {
				contpadtouse2 += PLAYERCOUNT();
			}

			pakRumble(contpadtouse2, 0.2f, 2, 4);
		}
	} else {
		pakRumble(g_Vars.currentplayernum, 0.2f, 2, 4);
	}
#endif
}
#else
GLOBAL_ASM(
glabel bgunRumble
/*  f095b30:	27bdffd0 */ 	addiu	$sp,$sp,-48
/*  f095b34:	3c08800a */ 	lui	$t0,%hi(g_Vars)
/*  f095b38:	2508e6c0 */ 	addiu	$t0,$t0,%lo(g_Vars)
/*  f095b3c:	8d0e0288 */ 	lw	$t6,0x288($t0)
/*  f095b40:	afbf0014 */ 	sw	$ra,0x14($sp)
/*  f095b44:	afa40030 */ 	sw	$a0,0x30($sp)
/*  f095b48:	afa50034 */ 	sw	$a1,0x34($sp)
/*  f095b4c:	0fc53380 */ 	jal	optionsGetControlMode
/*  f095b50:	8dc40070 */ 	lw	$a0,0x70($t6)
/*  f095b54:	3c08800a */ 	lui	$t0,%hi(g_Vars)
/*  f095b58:	28410004 */ 	slti	$at,$v0,0x4
/*  f095b5c:	1420007c */ 	bnez	$at,.NB0f095d50
/*  f095b60:	2508e6c0 */ 	addiu	$t0,$t0,%lo(g_Vars)
/*  f095b64:	0fc44336 */ 	jal	pakGetType
/*  f095b68:	8104028f */ 	lb	$a0,0x28f($t0)
/*  f095b6c:	3c08800a */ 	lui	$t0,%hi(g_Vars)
/*  f095b70:	2508e6c0 */ 	addiu	$t0,$t0,%lo(g_Vars)
/*  f095b74:	8d0f006c */ 	lw	$t7,0x6c($t0)
/*  f095b78:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f095b7c:	00453026 */ 	xor	$a2,$v0,$a1
/*  f095b80:	11e00003 */ 	beqz	$t7,.NB0f095b90
/*  f095b84:	2cc60001 */ 	sltiu	$a2,$a2,0x1
/*  f095b88:	10000002 */ 	beqz	$zero,.NB0f095b94
/*  f095b8c:	00a05025 */ 	or	$t2,$a1,$zero
.NB0f095b90:
/*  f095b90:	00005025 */ 	or	$t2,$zero,$zero
.NB0f095b94:
/*  f095b94:	8d180068 */ 	lw	$t8,0x68($t0)
/*  f095b98:	00004825 */ 	or	$t1,$zero,$zero
/*  f095b9c:	00001825 */ 	or	$v1,$zero,$zero
/*  f095ba0:	13000003 */ 	beqz	$t8,.NB0f095bb0
/*  f095ba4:	00001025 */ 	or	$v0,$zero,$zero
/*  f095ba8:	10000001 */ 	beqz	$zero,.NB0f095bb0
/*  f095bac:	00a04825 */ 	or	$t1,$a1,$zero
.NB0f095bb0:
/*  f095bb0:	8d190064 */ 	lw	$t9,0x64($t0)
/*  f095bb4:	13200003 */ 	beqz	$t9,.NB0f095bc4
/*  f095bb8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f095bbc:	10000001 */ 	beqz	$zero,.NB0f095bc4
/*  f095bc0:	00a01825 */ 	or	$v1,$a1,$zero
.NB0f095bc4:
/*  f095bc4:	8d0c0070 */ 	lw	$t4,0x70($t0)
/*  f095bc8:	11800003 */ 	beqz	$t4,.NB0f095bd8
/*  f095bcc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f095bd0:	10000001 */ 	beqz	$zero,.NB0f095bd8
/*  f095bd4:	00a01025 */ 	or	$v0,$a1,$zero
.NB0f095bd8:
/*  f095bd8:	8d18028c */ 	lw	$t8,0x28c($t0)
/*  f095bdc:	00436821 */ 	addu	$t5,$v0,$v1
/*  f095be0:	01a97021 */ 	addu	$t6,$t5,$t1
/*  f095be4:	01ca7821 */ 	addu	$t7,$t6,$t2
/*  f095be8:	01f82021 */ 	addu	$a0,$t7,$t8
/*  f095bec:	0004ce00 */ 	sll	$t9,$a0,0x18
/*  f095bf0:	00192603 */ 	sra	$a0,$t9,0x18
/*  f095bf4:	0fc44336 */ 	jal	pakGetType
/*  f095bf8:	afa6001c */ 	sw	$a2,0x1c($sp)
/*  f095bfc:	8fa6001c */ 	lw	$a2,0x1c($sp)
/*  f095c00:	3c08800a */ 	lui	$t0,%hi(g_Vars)
/*  f095c04:	2508e6c0 */ 	addiu	$t0,$t0,%lo(g_Vars)
/*  f095c08:	10c0002a */ 	beqz	$a2,.NB0f095cb4
/*  f095c0c:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f095c10:	14450028 */ 	bne	$v0,$a1,.NB0f095cb4
/*  f095c14:	8fae0030 */ 	lw	$t6,0x30($sp)
/*  f095c18:	15c5001c */ 	bne	$t6,$a1,.NB0f095c8c
/*  f095c1c:	8d0b028c */ 	lw	$t3,0x28c($t0)
/*  f095c20:	8d0f0070 */ 	lw	$t7,0x70($t0)
/*  f095c24:	00005025 */ 	or	$t2,$zero,$zero
/*  f095c28:	00004825 */ 	or	$t1,$zero,$zero
/*  f095c2c:	11e00003 */ 	beqz	$t7,.NB0f095c3c
/*  f095c30:	00001825 */ 	or	$v1,$zero,$zero
/*  f095c34:	10000001 */ 	beqz	$zero,.NB0f095c3c
/*  f095c38:	240a0001 */ 	addiu	$t2,$zero,0x1
.NB0f095c3c:
/*  f095c3c:	8d18006c */ 	lw	$t8,0x6c($t0)
/*  f095c40:	00001025 */ 	or	$v0,$zero,$zero
/*  f095c44:	13000003 */ 	beqz	$t8,.NB0f095c54
/*  f095c48:	00000000 */ 	sll	$zero,$zero,0x0
/*  f095c4c:	10000001 */ 	beqz	$zero,.NB0f095c54
/*  f095c50:	24090001 */ 	addiu	$t1,$zero,0x1
.NB0f095c54:
/*  f095c54:	8d190068 */ 	lw	$t9,0x68($t0)
/*  f095c58:	13200003 */ 	beqz	$t9,.NB0f095c68
/*  f095c5c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f095c60:	10000001 */ 	beqz	$zero,.NB0f095c68
/*  f095c64:	24030001 */ 	addiu	$v1,$zero,0x1
.NB0f095c68:
/*  f095c68:	8d0c0064 */ 	lw	$t4,0x64($t0)
/*  f095c6c:	11800003 */ 	beqz	$t4,.NB0f095c7c
/*  f095c70:	00000000 */ 	sll	$zero,$zero,0x0
/*  f095c74:	10000001 */ 	beqz	$zero,.NB0f095c7c
/*  f095c78:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f095c7c:
/*  f095c7c:	01626821 */ 	addu	$t5,$t3,$v0
/*  f095c80:	01a37021 */ 	addu	$t6,$t5,$v1
/*  f095c84:	01c97821 */ 	addu	$t7,$t6,$t1
/*  f095c88:	01ea5821 */ 	addu	$t3,$t7,$t2
.NB0f095c8c:
/*  f095c8c:	000b2600 */ 	sll	$a0,$t3,0x18
/*  f095c90:	0004c603 */ 	sra	$t8,$a0,0x18
/*  f095c94:	3c053e4c */ 	lui	$a1,0x3e4c
/*  f095c98:	34a5cccd */ 	ori	$a1,$a1,0xcccd
/*  f095c9c:	03002025 */ 	or	$a0,$t8,$zero
/*  f095ca0:	24060002 */ 	addiu	$a2,$zero,0x2
/*  f095ca4:	0fc45e2f */ 	jal	pakRumble
/*  f095ca8:	24070004 */ 	addiu	$a3,$zero,0x4
/*  f095cac:	1000002f */ 	beqz	$zero,.NB0f095d6c
/*  f095cb0:	8fbf0014 */ 	lw	$ra,0x14($sp)
.NB0f095cb4:
/*  f095cb4:	1445001c */ 	bne	$v0,$a1,.NB0f095d28
/*  f095cb8:	8d0b028c */ 	lw	$t3,0x28c($t0)
/*  f095cbc:	8d0c0070 */ 	lw	$t4,0x70($t0)
/*  f095cc0:	00005025 */ 	or	$t2,$zero,$zero
/*  f095cc4:	00004825 */ 	or	$t1,$zero,$zero
/*  f095cc8:	11800003 */ 	beqz	$t4,.NB0f095cd8
/*  f095ccc:	00001825 */ 	or	$v1,$zero,$zero
/*  f095cd0:	10000001 */ 	beqz	$zero,.NB0f095cd8
/*  f095cd4:	240a0001 */ 	addiu	$t2,$zero,0x1
.NB0f095cd8:
/*  f095cd8:	8d0d006c */ 	lw	$t5,0x6c($t0)
/*  f095cdc:	00001025 */ 	or	$v0,$zero,$zero
/*  f095ce0:	11a00003 */ 	beqz	$t5,.NB0f095cf0
/*  f095ce4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f095ce8:	10000001 */ 	beqz	$zero,.NB0f095cf0
/*  f095cec:	24090001 */ 	addiu	$t1,$zero,0x1
.NB0f095cf0:
/*  f095cf0:	8d0e0068 */ 	lw	$t6,0x68($t0)
/*  f095cf4:	11c00003 */ 	beqz	$t6,.NB0f095d04
/*  f095cf8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f095cfc:	10000001 */ 	beqz	$zero,.NB0f095d04
/*  f095d00:	24030001 */ 	addiu	$v1,$zero,0x1
.NB0f095d04:
/*  f095d04:	8d0f0064 */ 	lw	$t7,0x64($t0)
/*  f095d08:	11e00003 */ 	beqz	$t7,.NB0f095d18
/*  f095d0c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f095d10:	10000001 */ 	beqz	$zero,.NB0f095d18
/*  f095d14:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f095d18:
/*  f095d18:	0162c021 */ 	addu	$t8,$t3,$v0
/*  f095d1c:	0303c821 */ 	addu	$t9,$t8,$v1
/*  f095d20:	03296021 */ 	addu	$t4,$t9,$t1
/*  f095d24:	018a5821 */ 	addu	$t3,$t4,$t2
.NB0f095d28:
/*  f095d28:	000b2600 */ 	sll	$a0,$t3,0x18
/*  f095d2c:	00046e03 */ 	sra	$t5,$a0,0x18
/*  f095d30:	3c053e4c */ 	lui	$a1,0x3e4c
/*  f095d34:	34a5cccd */ 	ori	$a1,$a1,0xcccd
/*  f095d38:	01a02025 */ 	or	$a0,$t5,$zero
/*  f095d3c:	24060002 */ 	addiu	$a2,$zero,0x2
/*  f095d40:	0fc45e2f */ 	jal	pakRumble
/*  f095d44:	24070004 */ 	addiu	$a3,$zero,0x4
/*  f095d48:	10000008 */ 	beqz	$zero,.NB0f095d6c
/*  f095d4c:	8fbf0014 */ 	lw	$ra,0x14($sp)
.NB0f095d50:
/*  f095d50:	3c053e4c */ 	lui	$a1,0x3e4c
/*  f095d54:	34a5cccd */ 	ori	$a1,$a1,0xcccd
/*  f095d58:	8104028f */ 	lb	$a0,0x28f($t0)
/*  f095d5c:	24060002 */ 	addiu	$a2,$zero,0x2
/*  f095d60:	0fc45e2f */ 	jal	pakRumble
/*  f095d64:	24070004 */ 	addiu	$a3,$zero,0x4
/*  f095d68:	8fbf0014 */ 	lw	$ra,0x14($sp)
.NB0f095d6c:
/*  f095d6c:	27bd0030 */ 	addiu	$sp,$sp,0x30
/*  f095d70:	03e00008 */ 	jr	$ra
/*  f095d74:	00000000 */ 	sll	$zero,$zero,0x0
);
#endif

/* GGV UnequippedReloadIndex services begin: immutable invocation scalar. */
static ggv_value bgunReloadIndexRead(void *opaque, ggv_binding binding)
{ ggv_value value = {0}; if (binding == GGV_WEAPON_NUMBER) value.i = *(s32 *)opaque; return value; }
static void bgunReloadIndexWrite(void *opaque, ggv_binding binding, ggv_value value) {}
/* GGV UnequippedReloadIndex services end. */
s32 bgunGetUnequippedReloadIndex(s32 weaponnum)
{
    ggv_host host = { &weaponnum, bgunReloadIndexRead, bgunReloadIndexWrite, NULL }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_UNEQUIPPED_RELOAD_INDEX), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: unequipped reload index refused immutable program"); return -1;
    } return result;
}

/**
 * The magnums, shotgun and crossbow are special because that the game remembers
 * how much ammo is loaded in their clips when the weapon is not being used.
 * Their clips are gradually reloaded while the weapon is not in use, and that
 * gradual reloading is handled by this function.
 *
 * The gunroundsspent value is actually a countdown timer,
 * not the number of rounds as the name suggests.
 */
/* GGV Countdown services begin: per-field fresh global owner observation. */
struct bguncountdowncontext { s32 handindex; s32 slotindex; };
static ggv_value bgunCountdownRead(void *opaque, ggv_binding binding)
{
    struct bguncountdowncontext *c = opaque; ggv_value value = {0};
    if (binding == GGV_COUNTDOWN_SPENT) value.u = g_Vars.currentplayer->hands[c->handindex].gunroundsspent[c->slotindex];
    if (binding == GGV_COUNTDOWN_UPDATE60) value.i = g_Vars.lvupdate60;
    return value;
}
static void bgunCountdownWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bguncountdowncontext *c = opaque;
    if (binding == GGV_COUNTDOWN_HAND_INDEX) c->handindex = value.i;
    if (binding == GGV_COUNTDOWN_SLOT_INDEX) c->slotindex = value.i;
    if (binding == GGV_COUNTDOWN_SPENT) g_Vars.currentplayer->hands[c->handindex].gunroundsspent[c->slotindex] = value.u;
}
/* GGV Countdown services end. */
void bgunTickUnequippedReload(void)
{
    struct bguncountdowncontext context = {0};
    ggv_host host = { &context, bgunCountdownRead, bgunCountdownWrite, NULL }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_UNEQUIPPED_COUNTDOWN), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: unequipped countdown refused immutable program");
    }
}

/* GGV GunVisibility services begin: raw command and borrowed hand identity. */
struct bgunvisibilitycontext {
    struct gunviscmd *cmd; struct hand *hand; struct modeldef *modeldef;
    struct weapon *weapon; s32 ammoSlot; s16 partnum; bool visible;
};
static ggv_value bgunVisibilityRead(void *opaque, ggv_binding binding)
{
    struct bgunvisibilitycontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_VIS_COMMAND_TYPE: value.i = c->cmd->type; break;
    case GGV_VIS_CURSOR: value.ref = c->cmd; break;
    case GGV_VIS_OPERATION: value.i = c->cmd->op; break;
    case GGV_VIS_COMMAND_PART: value.i = c->cmd->partnum; break;
    case GGV_VIS_PART: value.i = c->partnum; break;
    case GGV_VIS_MODELDEF: value.ref = c->modeldef; break;
    case GGV_VIS_HAND_MODEL: value.ref = g_Vars.currentplayer->gunctrl.handmodeldef; break;
    case GGV_VIS_VISIBLE: value.b = c->visible; break;
    case GGV_VIS_COMMANDS: value.ref = c->weapon->gunviscmds; break;
    case GGV_VIS_WEAPON_ID: value.i = c->hand->gset.weaponnum; break;
    case GGV_VIS_AMMO: value.ref = c->weapon->ammos[c->ammoSlot]; break;
    case GGV_VIS_AMMO_FLAGS: value.u = c->weapon->ammos[c->ammoSlot]->flags; break;
    case GGV_VIS_CLIP_SIZE: value.i = c->hand->clipsizes[c->ammoSlot]; break;
    case GGV_VIS_LOADED: value.i = c->hand->loadedammo[c->ammoSlot]; break;
    case GGV_VIS_UPGRADES: value.u = c->hand->gset.unk0639; break;
    case GGV_VIS_PARAMETER: value.u = c->cmd->param; break;
    case GGV_VIS_IS_LEFT_HAND: value.b = c->hand == &g_Vars.currentplayer->hands[HAND_LEFT]; break;
    case GGV_VIS_IS_RIGHT_HAND: value.b = c->hand == &g_Vars.currentplayer->hands[HAND_RIGHT]; break;
    default: break;
    } return value;
}
static void bgunVisibilityWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunvisibilitycontext *c = opaque;
    switch (binding) {
    case GGV_VIS_CURSOR: c->cmd = value.ref; break;
    case GGV_VIS_WEAPON: c->weapon = value.ref; break;
    case GGV_VIS_AMMO_SLOT: c->ammoSlot = value.i; break;
    default: break;
    }
}
static ggv_value bgunVisibilityCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunvisibilitycontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_VIS_TEST: value.b = bgunTestGunVisCommand(a.ref, c->hand); break;
    case GGV_VIS_NEXT: value.ref = (struct gunviscmd *)a.ref + 1; break;
    case GGV_VIS_MODEL_PART: value.ref = modelGetPart(a.ref, b.i); break;
    case GGV_VIS_SAVE_HAND:
        c->hand->handsavedata[((struct modelnode *)a.ref)->rodata->toggle.rwdataindex] = b.b; break;
    case GGV_VIS_SAVE_GUN:
        c->hand->unk0a6c[((struct modelnode *)a.ref)->rodata->toggle.rwdataindex] = b.b; break;
    case GGV_VIS_RUN_COMMANDS: bgunExecuteGunVisCommands(c->hand, c->modeldef, a.ref); break;
    case GGV_COMMAND_SET_PART: bgunSetPartVisible(a.i, b.b, c->hand, c->modeldef); break;
    case GGV_QUERY_FIND_WEAPON: value.ref = weaponFindById(a.i); break;
    default: break;
    }
    return value;
}
static void bgunRunVisibility(struct bgunvisibilitycontext *context, ggv_stock_program program)
{
    ggv_host host = { context, bgunVisibilityRead, bgunVisibilityWrite, bgunVisibilityCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(program), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: visibility refused immutable program");
}
/* GGV GunVisibility services end. */
bool bgunTestGunVisCommand(struct gunviscmd *cmd, struct hand *hand)
{
    struct bgunvisibilitycontext context = {0}; context.cmd = cmd; context.hand = hand;
    ggv_host host = { &context, bgunVisibilityRead, bgunVisibilityWrite, NULL }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_GUN_VISIBILITY), &host, &result)) {
        // Native shift counts outside0..31 are not a newly qualified domain.
        sysLogPrintf(LOG_WARNING, "GGV: gun visibility refused immutable program"); return false;
    } return result;
}

void bgunSetPartVisible(s16 partnum, bool visible, struct hand *hand, struct modeldef *modeldef)
{
    struct bgunvisibilitycontext context = {0}; context.partnum = partnum; context.visible = visible;
    context.hand = hand; context.modeldef = modeldef;
    bgunRunVisibility(&context, GGV_PART_VISIBILITY);
}

void bgunExecuteGunVisCommands(struct hand *hand, struct modeldef *modeldef, struct gunviscmd *commands)
{
    struct bgunvisibilitycontext context = {0}; context.cmd = commands; context.hand = hand; context.modeldef = modeldef;
    bgunRunVisibility(&context, GGV_VISIBILITY_COMMANDS);
}

void bgun0f098030(struct hand *hand, struct modeldef *modeldef)
{
    struct bgunvisibilitycontext context = {0}; context.hand = hand; context.modeldef = modeldef;
    bgunRunVisibility(&context, GGV_AMMO_VISIBILITY);
}

struct bgunanimframecontext { struct hand *hand; f32 result; };
static ggv_value bgunAnimationFrameRead(void *opaque, ggv_binding binding)
{
    struct bgunanimframecontext *c = opaque;
    ggv_value value = {0};
    switch (binding) {
    case GGV_EMPTY_ANIM_MODE: value.i = c->hand->animmode; break;
    case GGV_ANIM_FRAME_COMMAND: value.ref = c->hand->unk0ce8; break;
    case GGV_ANIM_FRAME_COMMAND_VALUE: value.i64 = c->hand->unk0ce8->unk04; break;
    default: break;
    }
    return value;
}
static void bgunAnimationFrameWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunanimframecontext *c = opaque;
    if (binding == GGV_ANIM_FRAME_RESULT) c->result = value.f;
}
static ggv_value bgunAnimationFrameCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunanimframecontext *c = opaque; ggv_value value = {0};
    (void)a; (void)b; (void)d;
    switch (service) {
    case GGV_MODEL_ANIM_FRAME_COUNT: value.i = modelGetNumAnimFrames(&c->hand->gunmodel); break;
    case GGV_MODEL_ANIM_CURRENT_FRAME: value.f = modelGetCurAnimFrame(&c->hand->gunmodel); break;
    default: break;
    }
    return value;
}
f32 bgun0f09815c(struct hand *hand)
{
    struct bgunanimframecontext context = { hand, 0.0f };
    ggv_host host = { &context, bgunAnimationFrameRead, bgunAnimationFrameWrite, bgunAnimationFrameCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_FRAME), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation frame refused immutable program");
    }
    return context.result;
}

struct bgunanimsoundcontext {
    struct hand *hand;
    struct guncmd *cursor;
    // Every native f32 and s32 frame bound is exactly representable here.
    double from, to;
};
static ggv_value bgunAnimationSoundRead(void *opaque, ggv_binding binding)
{
    struct bgunanimsoundcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_ANIM_FRAME_COMMAND: value.ref = c->hand->unk0ce8; break;
    case GGV_ANIM_WAIT_CURSOR: value.ref = c->cursor; break;
    case GGV_ANIM_WAIT_TYPE: value.i = c->cursor->type; break;
    case GGV_ANIM_WAIT_KEY: value.i = c->cursor->unk02; break;
    case GGV_ANIM_WAIT_VALUE: value.i64 = c->cursor->unk04; break;
    case GGV_COMMAND_FLOAT_FRAMES: value.b = VERSION >= VERSION_PAL_BETA; break;
    case GGV_COMMAND_FROM_FLOAT: value.f = (f32)c->from; break;
    case GGV_COMMAND_TO_FLOAT: value.f = (f32)c->to; break;
    case GGV_COMMAND_FROM_INT: value.i = (s32)c->from; break;
    case GGV_COMMAND_TO_INT: value.i = (s32)c->to; break;
    case GGV_VERSION_1_OR_LATER: value.b = VERSION >= VERSION_NTSC_1_0; break;
    case GGV_COMMAND_OUTPUT: value.u = c->hand->unk0cc8_04; break;
    default: break;
    }
    return value;
}
static void bgunAnimationSoundWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunanimsoundcontext *c = opaque;
    switch (binding) {
    case GGV_ANIM_WAIT_CURSOR: c->cursor = value.ref; break;
    case GGV_COMMAND_OUTPUT: c->hand->unk0cc8_04 = value.u; break;
    default: break;
    }
}
static ggv_value bgunAnimationSoundCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)d;
    switch (service) {
    case GGV_ANIM_CURSOR_NEXT: value.ref = (struct guncmd *)a.ref + 1; break;
    case GGV_COMMAND_SOUND_MODERN: value.ref = snd00010718(0, 0, AL_VOL_FULL, AL_PAN_CENTER, a.i64, b.f, 1, -1, 1); break;
    case GGV_COMMAND_SOUND_LEGACY: value.ref = sndStart(var80095200, a.i64, NULL, -1, -1, -1, -1, -1); break;
    case GGV_SOUND_EVENT: audioPostEvent(a.ref, AL_SNDP_PITCH_EVT, b.i); break;
    default: break;
    }
    return value;
}
static void bgunTickAnimationSounds(struct hand *hand, double from, double to)
{
    struct bgunanimsoundcontext context = { hand, NULL, from, to };
    ggv_host host = { &context, bgunAnimationSoundRead, bgunAnimationSoundWrite, bgunAnimationSoundCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_SOUND_COMMANDS), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation sound commands refused immutable program");
    }
}

struct bgunanimationpart { s32 number, frame; bool visible; };
struct bgunanimationparts { struct bgunanimationpart *entries; s32 count, index; u32 capacity; };
struct bgunanimationpartcontext { struct guncmd *command; struct bgunanimationparts *parts; };
static bool bgunReserveAnimationParts(struct bgunanimationparts *parts, s32 needed)
{
    const u32 limit = UINT32_MAX / sizeof(*parts->entries);
    u32 capacity;
    struct bgunanimationpart *grown;
    if (needed < 0 || (u32)needed > limit) return false;
    if ((u32)needed <= parts->capacity) return true;
    capacity = parts->capacity ? parts->capacity : 16;
    if (capacity > limit) capacity = limit;
    while (capacity < (u32)needed) capacity = capacity > limit / 2 ? limit : capacity * 2;
    grown = sysMemRealloc(parts->entries, capacity * sizeof(*parts->entries));
    if (!grown) return false;
    parts->entries = grown;
    parts->capacity = capacity;
    return true;
}
static ggv_value bgunAnimationPartRead(void *opaque, ggv_binding binding)
{
    struct bgunanimationpartcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_PART_COUNT: v.i = c->parts->count; break;
    case GGV_PART_NUMBER: v.i = c->parts->entries[c->parts->index].number; break;
    case GGV_PART_FRAME: v.i = c->parts->entries[c->parts->index].frame; break;
    case GGV_ANIM_WAIT_VALUE: v.i64 = c->command->unk04; break;
    case GGV_ANIM_WAIT_KEY: v.i = c->command->unk02; break;
    case GGV_ANIM_WAIT_TYPE: v.i = c->command->type; break;
    default: break;
    }
    return v;
}
static void bgunAnimationPartWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunanimationpartcontext *c = opaque;
    switch (binding) {
    case GGV_PART_COUNT: c->parts->count = v.i; break;
    case GGV_PART_INDEX: c->parts->index = v.i; break;
    case GGV_PART_NUMBER: c->parts->entries[c->parts->index].number = v.i; break;
    case GGV_PART_FRAME: c->parts->entries[c->parts->index].frame = v.i; break;
    case GGV_PART_VISIBLE: c->parts->entries[c->parts->index].visible = v.b; break;
    default: break;
    }
}
static ggv_value bgunAnimationPartCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunanimationpartcontext *c = opaque; ggv_value v = {0}; (void)b; (void)d;
    if (service == GGV_PART_RESERVE) v.b = bgunReserveAnimationParts(c->parts, a.i);
    return v;
}
static bool bgunAccumulateAnimationPart(struct guncmd *command, struct bgunanimationparts *parts)
{
    struct bgunanimationpartcontext context = {command, parts};
    ggv_host host = {&context, bgunAnimationPartRead, bgunAnimationPartWrite, bgunAnimationPartCall};
    int32_t result = -1;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_PART_COMMAND), &host, &result) || result != 0) {
        sysLogPrintf(LOG_WARNING, "GGV: animation part storage failed; invocation stopped");
        return false;
    }
    return true;
}

struct bgunanimationfirstcontext {
    struct hand *hand;
    struct modeldef *modeldef;
    struct bgunanimationparts *parts;
    struct guncmd *cursor;
    double from, to;
};
static ggv_value bgunAnimationFirstRead(void *opaque, ggv_binding binding)
{
    struct bgunanimationfirstcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_COMMAND_FLOAT_FRAMES: v.b = VERSION >= VERSION_PAL_BETA; break;
    case GGV_RELOAD_PAL: v.b = PAL; break;
    case GGV_COMMAND_FROM_FLOAT: v.f = (f32)c->from; break;
    case GGV_COMMAND_TO_FLOAT: v.f = (f32)c->to; break;
    case GGV_COMMAND_FROM_INT: v.i = (s32)c->from; break;
    case GGV_COMMAND_TO_INT: v.i = (s32)c->to; break;
    case GGV_ANIM_FRAME_COMMAND: v.ref = c->hand->unk0ce8; break;
    case GGV_ANIM_WAIT_CURSOR: v.ref = c->cursor; break;
    case GGV_ANIM_WAIT_TYPE: v.i = c->cursor->type; break;
    case GGV_ANIM_WAIT_KEY: v.i = c->cursor->unk02; break;
    case GGV_ANIM_WAIT_VALUE: v.i64 = c->cursor->unk04; break;
    case GGV_ANIMATION_LATCH: v.b = c->hand->unk0cc8_01; break;
    case GGV_ANIM_INCREMENTAL: v.b = c->hand->incrementalreloading; break;
    case GGV_ANIM_WAIT_INCREMENT: v.i = c->hand->animframeinc; break;
    case GGV_ANIM_WAIT_INCREMENT_REAL: v.f = c->hand->animframeincfreal; break;
    case GGV_ANIM_LOOP_COUNT: v.i = c->hand->animloopcount; break;
    case GGV_PART_COUNT: v.i = c->parts->count; break;
    case GGV_PART_NUMBER: v.i = c->parts->entries[c->parts->index].number; break;
    case GGV_PART_VISIBLE: v.b = c->parts->entries[c->parts->index].visible; break;
    default: break;
    }
    return v;
}
static void bgunAnimationFirstWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunanimationfirstcontext *c = opaque;
    switch (binding) {
    case GGV_COMMAND_FROM_FLOAT: c->from = v.f; break;
    case GGV_COMMAND_TO_FLOAT: c->to = v.f; break;
    case GGV_COMMAND_FROM_INT: c->from = v.i; break;
    case GGV_COMMAND_TO_INT: c->to = v.i; break;
    case GGV_ANIM_WAIT_CURSOR: c->cursor = v.ref; break;
    case GGV_ANIM_WAIT_INCREMENT: c->hand->animframeinc = v.i; break;
    case GGV_ANIM_WAIT_INCREMENT_REAL: c->hand->animframeincfreal = v.f; break;
    case GGV_ANIM_LOOP_COUNT: c->hand->animloopcount = v.i; break;
    case GGV_PART_COUNT: c->parts->count = v.i; break;
    case GGV_PART_INDEX: c->parts->index = v.i; break;
    default: break;
    }
}
static ggv_value bgunAnimationFirstCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunanimationfirstcontext *c = opaque; ggv_value v = {0}; (void)d;
    switch (service) {
    case GGV_ANIM_WAIT_FRAME: v.f = bgun0f09815c(c->hand); break;
    case GGV_MODEL_ABS_ANIM_SPEED: v.f = modelGetAbsAnimSpeed(&c->hand->gunmodel); break;
    case GGV_ANIM_CURSOR_NEXT: v.ref = (struct guncmd *)a.ref + 1; break;
    case GGV_COMMAND_ACCUMULATE_PART: v.b = bgunAccumulateAnimationPart(c->cursor, c->parts); break;
    case GGV_COMMAND_SET_FRAME: modelSetAnimFrame(&c->hand->gunmodel, a.f); break;
    case GGV_COMMAND_SET_PART: bgunSetPartVisible(a.i, b.b, c->hand, c->modeldef); break;
    default: break;
    }
    return v;
}
static bool bgunProcessAnimationCommands(struct hand *hand, struct modeldef *modeldef,
        struct bgunanimationparts *parts, double *previousframe)
{
    struct bgunanimationfirstcontext context = {hand, modeldef, parts, NULL, 0, 0};
    ggv_host host = {&context, bgunAnimationFirstRead, bgunAnimationFirstWrite, bgunAnimationFirstCall};
    int32_t result = -1;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_FIRST_COMMANDS), &host, &result) || result != 0) {
        sysLogPrintf(LOG_WARNING, "GGV: animation command arithmetic or storage failed; invocation stopped");
        return false;
    }
    *previousframe = context.from;
    return true;
}

struct bgunanimationdrivercontext {
    struct hand *hand;
    struct modeldef *modeldef;
    struct bgunanimationparts parts;
    double from, to;
};
static ggv_value bgunAnimationDriverRead(void *opaque, ggv_binding binding)
{
    struct bgunanimationdrivercontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_EMPTY_ANIM_MODE: v.i = c->hand->animmode; break;
    case GGV_ANIM_LOAD: v.i = c->hand->animload; break;
    case GGV_ANIM_GANGSTA: v.f = c->hand->gangstarot; break;
    case GGV_COMMAND_FLOAT_FRAMES: v.b = VERSION >= VERSION_PAL_BETA; break;
    case GGV_RELOAD_PAL: v.b = PAL; break;
    case GGV_ANIM_FRAME_COMMAND_VALUE: v.i64 = c->hand->unk0ce8->unk04; break;
    case GGV_ANIM_FRAME_COMMAND: v.ref = c->hand->unk0ce8; break;
    case GGV_ANIM_BIT07: v.b = c->hand->unk0d0e_07; break;
    case GGV_ANIM_ELIGIBILITY_LEFT_INUSE: v.i = g_Vars.currentplayer->hands[HAND_LEFT].inuse; break;
    case GGV_ANIM_WAIT_INCREMENT: v.i = c->hand->animframeinc; break;
    case GGV_ANIM_BIT02: v.b = c->hand->unk0cc8_02; break;
    default: break;
    }
    return v;
}
static void bgunAnimationDriverWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunanimationdrivercontext *c = opaque;
    switch (binding) {
    case GGV_COMMAND_OUTPUT: c->hand->unk0cc8_04 = v.u; break;
    case GGV_EMPTY_ANIM_MODE: c->hand->animmode = v.i; break;
    case GGV_ANIM_LOAD: c->hand->animload = v.i; break;
    case GGV_ANIM_WAIT_INCREMENT: c->hand->animframeinc = v.i; break;
    case GGV_ANIM_WAIT_INCREMENT_REAL: c->hand->animframeincfreal = v.f; break;
    case GGV_COMMAND_TO_FLOAT: c->to = v.f; break;
    case GGV_COMMAND_TO_INT: c->to = v.i; break;
    default: break;
    }
}
static ggv_value bgunAnimationDriverCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunanimationdrivercontext *c = opaque; ggv_value v = {0}; (void)d;
    switch (service) {
    case GGV_ANIM_WAIT_FRAME: v.f = bgun0f09815c(c->hand); break;
    case GGV_MODEL_ANIM_FRAME_COUNT: v.i = modelGetNumAnimFrames(&c->hand->gunmodel); break;
    case GGV_MODEL_ABS_ANIM_SPEED: v.f = modelGetAbsAnimSpeed(&c->hand->gunmodel); break;
    case GGV_RNG_U32: v.u = rngRandom(); break;
    case GGV_COMMAND_MODEL_ANIMATION: modelSetAnimation(&c->hand->gunmodel, a.i, false, 0.0f, b.f, 0.0f); break;
    case GGV_COMMAND_SET_FRAME: modelSetAnimFrame(&c->hand->gunmodel, a.f); break;
    case GGV_COMMAND_FIRST_PASS: v.b = bgunProcessAnimationCommands(c->hand, c->modeldef, &c->parts, &c->from); break;
    case GGV_COMMAND_PLAY_SPEED: modelSetAnimPlaySpeed(&c->hand->gunmodel, a.f, 0); break;
    case GGV_COMMAND_TICK_QUARTER: modelTickAnimQuarterSpeed(&c->hand->gunmodel, a.i, true); break;
#if VERSION < VERSION_PAL_BETA
    case GGV_COMMAND_TICK_LEGACY: modelTickAnim(&c->hand->gunmodel, a.i, true); break;
#endif
    case GGV_COMMAND_SOUND_PASS: bgunTickAnimationSounds(c->hand, c->from, c->to); break;
    default: break;
    }
    return v;
}
void bgun0f0981e8(struct hand *hand, struct modeldef *modeldef)
{
    struct bgunanimationdrivercontext context = {0};
    ggv_host host = {&context, bgunAnimationDriverRead, bgunAnimationDriverWrite, bgunAnimationDriverCall};
    int32_t result = -1;
    context.hand = hand; context.modeldef = modeldef;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_DRIVER), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation driver refused immutable program");
    }
    // Child failures already report their cause. Scratch belongs to this invocation.
    sysMemFree(context.parts.entries);
}


struct bgunanimeligibilitycontext { struct guncmd *cmd; struct gset *gset; };
static ggv_value bgunAnimationEligibilityRead(void *opaque, ggv_binding binding)
{
    struct bgunanimeligibilitycontext *c = opaque;
    ggv_value value = {0};
    switch (binding) {
    case GGV_ANIM_ELIGIBILITY_KIND: value.i = c->cmd->unk01; break;
    case GGV_ANIM_ELIGIBILITY_LEFT_INUSE:
        value.i = g_Vars.currentplayer->hands[HAND_LEFT].inuse; break;
    case GGV_WEAPON_FUNCTION: value.i = c->gset->weaponfunc; break;
    default: break;
    }
    return value;
}
static void bgunAnimationEligibilityWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque; (void)binding; (void)value;
}
bool bgun0f098884(struct guncmd *cmd, struct gset *gset)
{
    struct bgunanimeligibilitycontext context = { cmd, gset };
    ggv_host host = { &context, bgunAnimationEligibilityRead, bgunAnimationEligibilityWrite };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_ELIGIBILITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation eligibility refused immutable program");
        return false;
    }
    return result;
}

/* Recursive invocations borrow the same hand but own independent cursors.
 * Selection, RNG reduction, stopping and store order live in the graph. */
struct bgunanimstartcontext {
    struct guncmd *command;
    struct guncmd *cursor;
    s32 handnum;
    struct hand *hand;
};
static void bgunAnimationWrite(void *opaque, ggv_binding binding, ggv_value value);
static ggv_value bgunAnimationStartRead(void *opaque, ggv_binding binding)
{
    struct bgunanimstartcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_ANIM_START_COMMAND: value.ref = c->command; break;
    case GGV_ANIM_WAIT_CURSOR: value.ref = c->cursor; break;
    case GGV_ANIM_WAIT_TYPE: value.i = c->cursor->type; break;
    case GGV_ANIM_WAIT_KEY: value.i = c->cursor->unk02; break;
    case GGV_ANIM_START_CHILD: value.ref = (struct guncmd *)c->cursor->unk04; break;
    case GGV_ANIM_START_PREVIOUS: value.ref = c->hand->unk0d80; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_PASSED_HAND: value.ref = c->hand; break;
    default: break;
    }
    return value;
}
static void bgunAnimationStartWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunanimstartcontext *c = opaque;
    if (binding == GGV_ANIM_WAIT_CURSOR) c->cursor = value.ref;
    else if (binding == GGV_ANIM_START_PREVIOUS) c->hand->unk0d80 = value.ref;
    else bgunAnimationWrite(c->hand, binding, value);
}
static ggv_value bgunAnimationStartCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunanimstartcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_ANIM_START_NAME: value.ref = (void *)loaderPoolAnimationNameForCmds(a.ref); break;
    case GGV_ANIM_START_SOURCE: value.ref = (void *)loaderPoolAnimationSourceForCmds(a.ref); break;
    case GGV_ANIM_START_LOG_NULL:
        sysLogPrintf(LOG_WARNING,
            "BGUN.ANIM.NULL: bgunStartAnimation called with NULL cmd "
            "(handnum=%d) -- skipping; likely an unresolved INCLUDE/RANDOM "
            "animation reference in the active weapon", (s32)c->handnum);
        break;
    case GGV_ANIM_START_LOG_SOURCE:
        sysLogPrintf(LOG_NOTE, "BGUN.ANIM.SOURCE: hand=%d animation=base:%s source=%s",
            (s32)c->handnum, (const char *)a.ref, (const char *)b.ref);
        break;
    case GGV_RNG_U32: value.u = rngRandom(); break;
    case GGV_ANIM_CURSOR_NEXT: value.ref = (struct guncmd *)a.ref + 1; break;
    case GGV_ANIM_START_ELIGIBILITY: value.b = bgun0f098884(a.ref, &c->hand->gset); break;
    case GGV_HAND_START_ANIMATION: bgunStartAnimation(a.ref, b.i, d.ref); break;
    default: break;
    }
    return value;
}
void bgunStartAnimation(struct guncmd *cmd, s32 handnum, struct hand *hand)
{
    struct bgunanimstartcontext context = { cmd, cmd, handnum, hand };
    ggv_host host = { &context, bgunAnimationStartRead, bgunAnimationStartWrite, bgunAnimationStartCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_START), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation start refused immutable program");
    }
}

/* Each invocation owns its cursor; frame callbacks may mutate hand fields,
 * which are read fresh at the original donor read sites. */
struct bgunanimwaitcontext {
    struct hand *hand;
    struct guncmd *cursor;
    s32 time;
};
static ggv_value bgunAnimationWaitRead(void *opaque, ggv_binding binding)
{
    struct bgunanimwaitcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_EMPTY_ANIM_MODE: value.i = c->hand->animmode; break;
    case GGV_ANIM_LOAD: value.i = c->hand->animload; break;
    case GGV_ANIMATION_LATCH: value.b = c->hand->unk0cc8_01; break;
    case GGV_ANIM_WAIT_TIME: value.i = c->time; break;
    case GGV_ANIM_WAIT_CURSOR: value.ref = c->cursor; break;
    case GGV_ANIM_WAIT_TYPE: value.i = c->cursor->type; break;
    case GGV_ANIM_WAIT_KEY: value.i = c->cursor->unk02; break;
    case GGV_ANIM_WAIT_VALUE: value.i64 = c->cursor->unk04; break;
    case GGV_ANIM_WAIT_INCREMENT: value.i = c->hand->animframeinc; break;
#if VERSION >= VERSION_PAL_BETA
    case GGV_ANIM_WAIT_INCREMENT_REAL: value.f = c->hand->animframeincfreal; break;
    case GGV_RECOVERY_PAL: value.b = true; break;
#else
    case GGV_RECOVERY_PAL: value.b = false; break;
#endif
    default: break;
    }
    return value;
}
static void bgunAnimationWaitWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunanimwaitcontext *c = opaque;
    if (binding == GGV_ANIM_WAIT_CURSOR) c->cursor = value.ref;
}
static ggv_value bgunAnimationWaitCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunanimwaitcontext *c = opaque; ggv_value value = {0};
    (void)b; (void)d;
    switch (service) {
    case GGV_ANIM_CURSOR_NEXT: value.ref = (struct guncmd *)a.ref + 1; break;
    case GGV_ANIM_WAIT_FRAME: value.f = bgun0f09815c(c->hand); break;
    default: break;
    }
    return value;
}
bool bgun0f098a44(struct hand *hand, s32 time)
{
    struct bgunanimwaitcontext context = { hand, hand->unk0ce8, time };
    ggv_host host = { &context, bgunAnimationWaitRead, bgunAnimationWaitWrite, bgunAnimationWaitCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_WAIT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation wait refused immutable program");
    }
    return result;
}
s32 bgun0f098b80(struct hand *hand, s32 arg1)
{
    struct bgunanimwaitcontext context = { hand, hand->unk0ce8, arg1 };
    ggv_host host = { &context, bgunAnimationWaitRead, bgunAnimationWaitWrite, bgunAnimationWaitCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_WAIT_KEY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation wait key refused immutable program");
    }
    return result;
}

/* GGV Animation services begin: borrowed hand fields, no cleanup authority. */
static ggv_value bgunAnimationRead(void *opaque, ggv_binding binding)
{
    struct hand *hand = opaque; ggv_value value = {0};
    if (binding == GGV_EMPTY_ANIM_MODE) value.i = hand->animmode;
    return value;
}
static void bgunAnimationWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct hand *hand = opaque;
    switch (binding) {
    case GGV_ANIM_LOAD: hand->animload = value.i; break;
    case GGV_EMPTY_ANIM_MODE: hand->animmode = value.i; break;
    case GGV_ANIMATION_LATCH: hand->unk0cc8_01 = value.b; break;
    case GGV_ANIM_INCREMENTAL: hand->incrementalreloading = value.b; break;
    case GGV_ANIM_COMMAND: hand->unk0ce8 = value.ref; break;
    case GGV_ANIM_LOOP_COUNT: hand->animloopcount = value.i; break;
    case GGV_ANIM_BIT02: hand->unk0cc8_02 = value.b; break;
    case GGV_ANIM_BIT07: hand->unk0d0e_07 = value.b; break;
    default: break;
    }
}
/* GGV Animation services end. */
bool bgunIsAnimBusy(struct hand *hand)
{
    ggv_host host = { hand, bgunAnimationRead, bgunAnimationWrite, NULL };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_BUSY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation busy predicate refused immutable program");
        return false;
    }
    return result;
}

void bgunResetAnim(struct hand *hand)
{
    ggv_host host = { hand, bgunAnimationRead, bgunAnimationWrite, NULL };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ANIMATION_RESET), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: animation reset refused immutable program");
    }
}

struct bgunweaponinfocontext { struct handweaponinfo *info; s32 handnum; };
static ggv_value bgunWeaponInfoRead(void *opaque, ggv_binding binding)
{
    ggv_value value = {0}; (void)opaque;
    if (binding == GGV_INFO_GUNCTRL) value.ref = &g_Vars.currentplayer->gunctrl;
    return value;
}
static void bgunWeaponInfoWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunweaponinfocontext *c = opaque;
    switch (binding) {
    case GGV_INFO_WEAPON_RESULT: c->info->weaponnum = value.i; break;
    case GGV_INFO_DEFINITION_RESULT: c->info->definition = value.ref; break;
    case GGV_INFO_GUNCTRL: c->info->gunctrl = value.ref; break;
    default: break;
    }
}
static ggv_value bgunWeaponInfoCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunweaponinfocontext *c = opaque; ggv_value value = {0}; (void)b; (void)d;
    switch (service) {
    case GGV_INFO_WEAPON_QUERY: value.i = bgunGetWeaponNum2(c->handnum); break;
    case GGV_INFO_IS_V2: value.b = wgV2RuntimeIsWeapon(a.i); break;
    case GGV_INFO_ENSURE_V2: wgV2RuntimeEnsureHand(g_Vars.currentplayer, c->handnum); break;
    case GGV_INFO_V2_DEFINITION: value.ref = (struct weapon *)wgV2RuntimeGsetWeapon(&g_Vars.currentplayer->hands[c->handnum].gset); break;
    case GGV_QUERY_FIND_WEAPON: value.ref = weaponFindById(a.i); break;
    default: break;
    }
    return value;
}
void bgunGetWeaponInfo(struct handweaponinfo *info, s32 handnum)
{
    struct bgunweaponinfocontext context = { info, handnum };
    ggv_host host = { &context, bgunWeaponInfoRead, bgunWeaponInfoWrite, bgunWeaponInfoCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_WEAPON_INFO), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: weapon info refused immutable program");
    }
}

/**
 * Return values:
 * -1 = gun function doesn't exist or ammo fully depleted
 * 0 = trigger reload
 * 1 = has ammo in clip and ammo in reserve
 * 2 = has ammo in clip but none in reserve
 * 3 = gun doesn't use ammo or clip is full
 */
/* GGV AmmoStatus services begin: borrowed original hand/info/function. */
struct bgunammostatuscontext { s32 funcnum; struct handweaponinfo *info; struct hand *hand; struct weaponfunc *func; s32 ammoindex; };
static ggv_value bgunAmmoStatusRead(void *opaque, ggv_binding binding)
{
    struct bgunammostatuscontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_QUERY_FUNCTION_I32: value.i = c->funcnum; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = c->func->ammoindex; break;
    case GGV_QUERY_EQUIPPED_TYPE: value.i = c->info->gunctrl->ammotypes[c->ammoindex]; break;
    case GGV_LOADED_AMMO: value.i = c->hand->loadedammo[c->ammoindex]; break;
    case GGV_QUERY_CLIP_CAPACITY: value.i = c->hand->clipsizes[c->ammoindex]; break;
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_QUERY_RESERVE: value.i = g_Vars.currentplayer->ammoheldarr[c->info->gunctrl->ammotypes[c->ammoindex]]; break;
    default: break;
    } return value;
}
static void bgunAmmoStatusWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunammostatuscontext *c = opaque;
    if (binding == GGV_AMMO_INDEX) c->ammoindex = value.i;
}
static ggv_value bgunAmmoStatusCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunammostatuscontext *c = opaque; ggv_value value = {0};
    if (service == GGV_QUERY_RESOLVE_FUNCTION) { c->func = weaponGetFunction(&c->hand->gset, a.i); value.b = c->func != NULL; }
    if (service == GGV_MELEE_MIN_CLIP) value.i = bgunGetMinClipQty(a.i, b.i);
    return value;
}
/* GGV AmmoStatus services end. */
s32 bgun0f098ca0(s32 funcnum, struct handweaponinfo *info, struct hand *hand)
{
    struct bgunammostatuscontext context = {0}; context.funcnum = funcnum; context.info = info; context.hand = hand;
    ggv_host host = { &context, bgunAmmoStatusRead, bgunAmmoStatusWrite, bgunAmmoStatusCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_STATUS), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo status refused immutable program"); return -1;
    }
    return result;
}

/* GGV Reload services begin: borrowed hand/info/function; no player snapshot. */
struct bgunreloadcontext { s32 weaponfunc; struct handweaponinfo *info; struct hand *hand; u8 onebullet; u8 checkunequipped; struct weaponfunc *func; s32 ammoindex; s32 reloadindex; s32 *reserve_target; };
static ggv_value bgunReloadRead(void *opaque, ggv_binding binding)
{
    struct bgunreloadcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_RELOAD_FUNCTION: value.i = c->weaponfunc; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = c->func->ammoindex; break;
    case GGV_QUERY_EQUIPPED_TYPE: value.i = c->info->gunctrl->ammotypes[c->ammoindex]; break;
    case GGV_QUERY_CLIP_CAPACITY: value.i = c->hand->clipsizes[c->ammoindex]; break;
    case GGV_LOADED_AMMO: value.i = c->hand->loadedammo[c->ammoindex]; break;
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_RELOAD_VALID_WEAPON: value.b = g_FrIsValidWeapon; break;
    case GGV_RELOAD_CHECK: value.b = c->checkunequipped; break;
    case GGV_RELOAD_ONE: value.b = c->onebullet; break;
    case GGV_RELOAD_ROUNDS: value.u = c->hand->gunroundsspent[c->reloadindex]; break;
    case GGV_RELOAD_TICKS256: value.i = TICKS(256); break;
    case GGV_RELOAD_PAL:
#if VERSION >= VERSION_PAL_BETA
        value.b = true;
#else
        value.b = false;
#endif
        break;
    case GGV_RELOAD_JPN:
#if VERSION >= VERSION_JPN_FINAL
        value.b = true;
#else
        value.b = false;
#endif
        break;
    case GGV_RELOAD_RESERVE:
        // Re-evaluate the global player/type at each original statement. For
        // compound assignment, retain that statement's exact lvalue until write.
        c->reserve_target = &g_Vars.currentplayer->ammoheldarr[c->info->gunctrl->ammotypes[c->ammoindex]];
        value.i = *c->reserve_target; break;
    case GGV_RELOAD_AMMO_FLAGS: value.u = c->info->definition->ammos[c->ammoindex]->flags; break;
    default: break;
    } return value;
}
static void bgunReloadWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunreloadcontext *c = opaque;
    switch (binding) {
    case GGV_AMMO_INDEX: c->ammoindex = value.i; break;
    case GGV_RELOAD_INDEX: c->reloadindex = value.i; break;
    case GGV_LOADED_AMMO: c->hand->loadedammo[c->ammoindex] = value.i; break;
    case GGV_RELOAD_RESERVE: *c->reserve_target = value.i; break;
    case GGV_RELOAD_RESERVE_CLEAR: g_Vars.currentplayer->ammoheldarr[c->info->gunctrl->ammotypes[c->ammoindex]] = value.i; break;
    default: break;
    }
}
static ggv_value bgunReloadCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunreloadcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_QUERY_RESOLVE_FUNCTION: c->func = weaponGetFunction(&c->hand->gset, a.i); value.b = c->func != NULL; break;
    case GGV_RELOAD_INDEX_FOR_WEAPON: value.i = bgunGetUnequippedReloadIndex(a.i); break;
    case GGV_SVC_RELOAD_TRANSFER: bgun0f098df8(a.i, c->info, c->hand, 0, 1); break;
    default: break;
    } return value;
}
/* GGV Reload services end. */
void bgun0f098df8(s32 weaponfunc, struct handweaponinfo *info, struct hand *hand, u8 onebullet, u8 checkunequipped)
{
    struct bgunreloadcontext context = {0}; context.weaponfunc = weaponfunc; context.info = info; context.hand = hand; context.onebullet = onebullet; context.checkunequipped = checkunequipped;
    ggv_host host = { &context, bgunReloadRead, bgunReloadWrite, bgunReloadCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_RELOAD_TRANSFER), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: reload transfer refused immutable program");
    }
    // In most versions of the game, reloading the shotgun while going
    // through a teleport in Deep Sea will cause the shotgun to load
    // more ammo than its capacity. JPN Final fixes this here.
    // The original `if (func);` has no observable effect.
}

void bgun0f098f8c(struct handweaponinfo *info, struct hand *hand)
{
    struct bgunreloadcontext context = {0}; context.info = info; context.hand = hand;
    ggv_host host = { &context, bgunReloadRead, bgunReloadWrite, bgunReloadCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_RELOAD_BOTH), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: both-slot reload refused immutable program");
    }
}

struct bgunanyammocontext { s32 handnum; struct handweaponinfo info; };
static ggv_value bgunAnyAmmoRead(void *opaque, ggv_binding binding)
{
    ggv_value value = {0}; (void)opaque; (void)binding; return value;
}
static void bgunAnyAmmoWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque; (void)binding; (void)value;
}
static ggv_value bgunAnyAmmoCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunanyammocontext *c = opaque; ggv_value value = {0}; (void)b; (void)d;
    switch (service) {
    case GGV_HAND_INFO_CAPTURE: bgunGetWeaponInfo(&c->info, c->handnum); break;
    case GGV_RELOAD_AMMO_STATUS:
        value.i = bgun0f098ca0(a.i, &c->info, &g_Vars.currentplayer->hands[c->handnum]); break;
    default: break;
    }
    return value;
}
bool bgun0f099008(s32 handnum)
{
    struct bgunanyammocontext context = {0}; context.handnum = handnum;
    ggv_host host = { &context, bgunAnyAmmoRead, bgunAnyAmmoWrite, bgunAnyAmmoCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_ANY_AMMO), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: any-ammo predicate refused immutable program");
    }
    return result;
}

/* GGV AmmoEligibility services begin: borrowed metadata, no new index policy. */
struct bgunammoeligibilitycontext { struct weaponfunc *func; struct weapon *weapon; };
static ggv_value bgunAmmoEligibilityRead(void *opaque, ggv_binding binding)
{
    struct bgunammoeligibilitycontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_AMMO_FUNCTION_REF: value.ref = c->func; break;
    case GGV_LEGACY_FUNCTION_TYPE: value.i = c->func->type; break;
    case GGV_SPECIAL_LEGACY_VALUE: value.i = ((struct weaponfunc_special *) c->func)->specialfunc; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = c->func->ammoindex; break;
    case GGV_AMMO_WEAPON_REF: value.ref = c->weapon->ammos[c->func->ammoindex]; break;
    case GGV_AMMO_DEFINITION_TYPE: value.u = c->weapon->ammos[c->func->ammoindex]->type; break;
    default: break;
    } return value;
}
static void bgunAmmoEligibilityWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void) opaque; (void) binding; (void) value; /* Complete predicate has no stores. */
}
static ggv_value bgunAmmoEligibilityCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void) opaque;
    if (service == GGV_AMMO_TOTAL_COUNT) value.i = bgunGetAmmoCount(a.i);
    return value;
}
/* GGV AmmoEligibility services end. */
bool bgun0f0990b0(struct weaponfunc *basefunc, struct weapon *weapon)
{
    struct bgunammoeligibilitycontext context = { basefunc, weapon };
    ggv_host host = { &context, bgunAmmoEligibilityRead, bgunAmmoEligibilityWrite, bgunAmmoEligibilityCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_ELIGIBILITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo exclusion predicate refused immutable program");
        return false;
    }
    return result;
}

struct bgunhandexclusioncontext { struct hand *hand; s32 gunfunc; };
static ggv_value bgunHandExclusionRead(void *opaque, ggv_binding binding)
{
    struct bgunhandexclusioncontext *c = opaque; ggv_value value = {0};
    if (binding == GGV_QUERY_FUNCTION_I32) value.i = c->gunfunc;
    return value;
}
static void bgunHandExclusionWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque; (void)binding; (void)value;
}
static ggv_value bgunHandExclusionCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunhandexclusioncontext *c = opaque; ggv_value value = {0}; (void)d;
    switch (service) {
    case GGV_EXCLUSION_FUNCTION: value.ref = weaponGetFunction(&c->hand->gset, c->gunfunc); break;
    case GGV_EXCLUSION_WEAPON: value.ref = weaponFindById(c->hand->gset.weaponnum); break;
    case GGV_HAND_CURRENT_FUNCTION: value.i = bgunIsUsingSecondaryFunction(); break;
    case GGV_EXCLUSION_CHECK: value.b = bgun0f0990b0(a.ref, b.ref); break;
    default: break;
    }
    return value;
}
bool bgun0f099188(struct hand *hand, s32 gunfunc)
{
    struct bgunhandexclusioncontext context = { hand, gunfunc };
    ggv_host host = { &context, bgunHandExclusionRead, bgunHandExclusionWrite, bgunHandExclusionCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_HAND_EXCLUSION), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: hand exclusion refused immutable program");
    }
    return result;
}

struct bgunidlecontext {
    struct handweaponinfo *info;
    s32 handnum;
    struct hand *hand;
    s32 lvupdate;
    struct hand *otherhand;
    struct weaponfunc *func;
};
static ggv_value bgunIdleRead(void *opaque, ggv_binding binding)
{
    struct bgunidlecontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_STATE_INUSE: value.i = c->hand->inuse; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_WEAPON_FUNCTION: value.i = c->hand->gset.weaponfunc; break;
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    case GGV_ANIM_WAIT_INCREMENT: value.i = c->hand->animframeinc; break;
    case GGV_HAND_MODE_NEXT: value.u = c->hand->modenext; break;
    case GGV_HAND_AUTO_BIT07: value.b = c->hand->unk0cc8_07; break;
    case GGV_HAND_AUTO_BIT08: value.b = c->hand->unk0cc8_08; break;
    case GGV_HAND_ACTIVATE_SECONDARY: value.b = c->hand->activatesecondary; break;
    case GGV_GUNCTRL_WANTAMMO: value.b = c->info->gunctrl->wantammo; break;
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_IDLE_FUNCTION_TYPE: value.u = c->func->type; break;
    case GGV_AUTO_OTHER_INUSE: value.i = c->otherhand->inuse; break;
    case GGV_AUTO_OTHER_TRIGGER: value.b = c->otherhand->triggeron; break;
    case GGV_AUTO_OTHER_BIT08: value.b = c->otherhand->unk0cc8_08; break;
#if VERSION >= VERSION_PAL_BETA
    case GGV_RECOVERY_PAL: value.b = true; break;
#else
    case GGV_RECOVERY_PAL: value.b = false; break;
#endif
    default: break;
    }
    return value;
}
static void bgunIdleWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunidlecontext *c = opaque;
    switch (binding) {
    case GGV_HAND_LASTDIRVALID: c->hand->lastdirvalid = value.b; break;
    case GGV_BURST_BULLETS: c->hand->burstbullets = value.i; break;
#if VERSION < VERSION_PAL_BETA
    case GGV_ANIM_TICK_INCREMENT_LEGACY: c->hand->animframeincfreal = value.u; break;
#endif
    case GGV_REMAINDER: c->hand->shotremainder = value.f; break;
    case GGV_WEAPON_FUNCTION: c->hand->gset.weaponfunc = value.i; break;
    case GGV_RELOAD_MODE: c->hand->mode = value.u; break;
    case GGV_HAND_MODE_NEXT: c->hand->modenext = value.u; break;
    case GGV_RELOAD_COUNT: c->hand->count = value.i; break;
    case GGV_RELOAD_COUNT60: c->hand->count60 = value.i; break;
    case GGV_RELOAD_DOAUTOSELECT: g_Vars.currentplayer->doautoselect = value.i; break;
    case GGV_HAND_AUTO_BIT07: c->hand->unk0cc8_07 = value.b; break;
    case GGV_HAND_AUTO_BIT08: c->hand->unk0cc8_08 = value.b; break;
    case GGV_HAND_ACTIVATE_SECONDARY: c->hand->activatesecondary = value.b; break;
    case GGV_TRIGGER_RELEASED: c->hand->triggerreleased = value.b; break;
    case GGV_ANIMATION_LATCH: c->hand->unk0cc8_01 = value.b; break;
    case GGV_AUTO_OTHER_BIT08: c->otherhand->unk0cc8_08 = value.b; break;
    default: break;
    }
}
static ggv_value bgunIdleCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunidlecontext *c = opaque; ggv_value value = {0};
    (void)d;
    switch (service) {
    case GGV_HAND_CURRENT_FUNCTION: value.i = bgunIsUsingSecondaryFunction(); break;
    case GGV_HAND_READY_SWITCH: value.b = bgunIsReadyToSwitch(c->handnum); break;
    case GGV_HAND_SET_STATE: value.b = bgunSetState(c->handnum, a.i); break;
    case GGV_RELOAD_AMMO_STATUS: value.i = bgun0f098ca0(a.i, c->info, c->hand); break;
    case GGV_TICK_IS_V2: value.b = wgV2RuntimeIsWeapon(c->info->weaponnum); break;
    case GGV_IDLE_V2: value.b = wgV2RuntimeIdle(g_Vars.currentplayer, c->handnum); break;
    case GGV_RELOAD_WEAPON_HAS_FLAG: value.b = weaponHasFlag(a.i, b.u); break;
    /* Keep this at the donor's preprocessing location: the late local
     * FUNCISSEC redefinition below uses a different storage adapter. */
    case GGV_IDLE_STORED_FUNCTION: value.i = FUNCISSEC(); break;
    case GGV_IDLE_ELIGIBILITY: value.b = bgun0f099188(c->hand, a.i); break;
    case GGV_IDLE_CAPTURE_FUNCTION: c->func = weaponGetFunction(&c->hand->gset, a.i); break;
    case GGV_IDLE_CAPTURE_OTHER: c->otherhand = &g_Vars.currentplayer->hands[1] - c->handnum; break;
    case GGV_AUTO_SWITCH_WEAPON: bgunAutoSwitchWeapon(); break;
    default: break;
    }
    return value;
}
s32 bgunTickIncIdle(struct handweaponinfo *info, s32 handnum, struct hand *hand, s32 lvupdate)
{
    struct bgunidlecontext context = { info, handnum, hand, lvupdate, NULL, NULL };
    ggv_host host = { &context, bgunIdleRead, bgunIdleWrite, bgunIdleCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_IDLE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: idle refused immutable program");
    }
    return result;
}

/* Hand lifecycle hosts for the frozen GGV stock programs. Each context is
 * invocation-borrowed: the passed info/hand/handnum/lvupdate, the captured
 * func, the once-selected otherhand and the stack angle live only for one
 * synchronous ggvExecute. Player and audio globals are reread at each binding
 * or service. Animation/wait dispatch graph programs; cheat/catalog boundaries remain native. */
struct bgunreloadtickcontext {
    struct handweaponinfo *info; s32 handnum; struct hand *hand; s32 lvupdate;
    struct weaponfunc *func; struct hand *otherhand;
};

struct bgunchangefunccontext {
    struct handweaponinfo *info; s32 handnum; struct hand *hand; s32 lvupdate;
};

struct bgunarmpitchcontext { struct hand *hand; f32 angle; };

static ggv_value bgunReloadTickRead(void *opaque, ggv_binding binding)
{
    struct bgunreloadtickcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_PASSED_HAND: value.ref = c->hand; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_WEAPON_FUNCTION: value.i = c->hand->gset.weaponfunc; break;
    case GGV_STATE_CYCLES: value.i = c->hand->statecycles; break;
    case GGV_STATE_MINOR: value.i = c->hand->stateminor; break;
    case GGV_STATE_FLAGS: value.u = c->hand->stateflags; break;
    case GGV_EMPTY_ANIM_MODE: value.i = c->hand->animmode; break;
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_RELOAD_PLAYER_DEAD: value.b = g_Vars.currentplayer->isdead; break;
    case GGV_RELOAD_OTHER_STATE: value.i = c->otherhand->state; break;
    case GGV_RELOAD_OTHER_STATEFRAMES: value.i = c->otherhand->stateframes; break;
    case GGV_RELOAD_FUNC_AMMO_INDEX: value.i = c->func->ammoindex; break;
    case GGV_RELOAD_AMMO_ANIMATION: value.ref = c->info->definition->ammos[c->func->ammoindex]->reload_animation; break;
    case GGV_RELOAD_FUNC_AMMO_FLAGS: value.u = c->info->definition->ammos[c->func->ammoindex]->flags; break;
    case GGV_RELOAD_JPN:
#if VERSION >= VERSION_JPN_FINAL
        value.b = true;
#else
        value.b = false;
#endif
        break;
    case GGV_RELOAD_DEVICES_ACTIVE: value.u = g_Vars.currentplayer->devicesactive; break;
    case GGV_RELOAD_DEVICES_INHIBIT: value.u = g_Vars.currentplayer->devicesinhibit; break;
    case GGV_RELOAD_COUNT60: value.i = c->hand->count60; break;
    case GGV_RELOAD_COUNT: value.i = c->hand->count; break;
    case GGV_RELOAD_VISIBLE: value.b = c->hand->visible; break;
    case GGV_RELOAD_PAUSETIME60: value.i = c->hand->pausetime60; break;
    case GGV_RELOAD_LVUPDATE240: value.i = g_Vars.lvupdate240; break;
    case GGV_RELOAD_CAMERA_MODE: value.i = g_Vars.currentplayer->cameramode; break;
    case GGV_RELOAD_PLAYER_INVINCIBLE: value.b = g_PlayerInvincible; break;
    case GGV_RELOAD_WEAPON_BRIEFCASE2: value.i = WEAPON_BRIEFCASE2; break;
    default: break;
    } return value;
}

static void bgunReloadTickWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunreloadtickcontext *c = opaque;
    switch (binding) {
    case GGV_EMPTY_ANIM_MODE: c->hand->animmode = value.i; break;
    case GGV_ANIM_LOAD: c->hand->animload = value.i; break;
    case GGV_RELOAD_GS_INT1: c->hand->gs_int1 = value.i; break;
    case GGV_RELOAD_GS_INT2: c->hand->gs_int2 = value.i; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; break;
    case GGV_STATE_FRAMES: c->hand->stateframes = value.i; break;
    case GGV_STATE_CYCLES: c->hand->statecycles = value.i; break;
    case GGV_STATE_LAST_FRAME: c->hand->statelastframe = value.i; break;
    case GGV_STATE_FLAGS: c->hand->stateflags = value.u; break;
    case GGV_RELOAD_UNK0D0E_07: c->hand->unk0d0e_07 = value.b; break;
    case GGV_RELOAD_INCREMENTAL: c->hand->incrementalreloading = value.b; break;
    case GGV_RELOAD_EJECT_STATE: c->hand->ejectstate = value.i; break;
    case GGV_RELOAD_MODE: c->hand->mode = value.u; break;
    case GGV_RELOAD_PAUSETIME60: c->hand->pausetime60 = value.i; break;
    case GGV_RELOAD_COUNT60: c->hand->count60 = value.i; break;
    case GGV_RELOAD_COUNT: c->hand->count = value.i; break;
    case GGV_RELOAD_UNK0CC8_02: c->hand->unk0cc8_02 = value.b; break;
    case GGV_RELOAD_DOAUTOSELECT: g_Vars.currentplayer->doautoselect = value.i; break;
    default: break;
    }
}

static ggv_value bgunReloadTickCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunreloadtickcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RELOAD_CAPTURE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); value.ref = c->func; break;
    case GGV_RELOAD_SELECT_OTHER_HAND: c->otherhand = &g_Vars.currentplayer->hands[a.i]; break;
    case GGV_EMPTY_SET_STATE: value.b = bgunSetState(a.i, b.i); break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    case GGV_HAND_START_ANIMATION: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_HAND_ANIM_A44: value.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_RELOAD_CHILD_TRANSFER: bgun0f098df8(a.i, c->info, c->hand, (u8)b.i, (u8)d.i); break;
    case GGV_RELOAD_AMMO_STATUS: value.i = bgun0f098ca0(a.i, c->info, c->hand); break;
    case GGV_RELOAD_SET_ARM_PITCH: bgunSetArmPitch(c->hand, a.f); break;
    case GGV_RELOAD_IS_LOADED: value.b = bgunIsLoaded(); break;
    case GGV_RELOAD_SOUND_START: sndStart(var80095200, SFX_RELOAD_DEFAULT, 0, -1, -1, -1, -1, -1); break;
    case GGV_RELOAD_FILENUM2: value.i = weaponGetFileNum2(a.i); break;
    case GGV_RELOAD_WEAPON_HAS_FLAG: value.b = weaponHasFlag(a.i, b.u); break;
    default: break;
    } return value;
}

static ggv_value bgunChangeFuncRead(void *opaque, ggv_binding binding)
{
    struct bgunchangefunccontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_PASSED_HAND: value.ref = c->hand; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    case GGV_STATE_CYCLES: value.i = c->hand->statecycles; break;
    case GGV_WEAPON_FUNCTION: value.i = c->hand->gset.weaponfunc; break;
    case GGV_EMPTY_ANIM_MODE: value.i = c->hand->animmode; break;
    default: break;
    } return value;
}

static void bgunChangeFuncWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunchangefunccontext *c = opaque;
    switch (binding) {
    case GGV_WEAPON_FUNCTION: c->hand->gset.weaponfunc = value.i; break;
    case GGV_RIGHT_HAND_UNK0DD4: g_Vars.currentplayer->hands[HAND_RIGHT].unk0dd4 = value.i; break;
    default: break;
    }
}

static ggv_value bgunChangeFuncCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunchangefunccontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_CHANGEFUNC_PRI_TO_SEC_ANIM: value.ref = gsetGetPriToSecAnim(&c->hand->gset); break;
    case GGV_CHANGEFUNC_SEC_TO_PRI_ANIM: value.ref = gsetGetSecToPriAnim(&c->hand->gset); break;
    case GGV_HAND_START_ANIMATION: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_EMPTY_SET_STATE: value.b = bgunSetState(a.i, b.i); break;
    default: break;
    } return value;
}

static ggv_value bgunArmPitchRead(void *opaque, ggv_binding binding)
{
    struct bgunarmpitchcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_ARM_PITCH_ANGLE: value.f = c->angle; break;
    case GGV_ARM_PITCH_MATRIX: value.ref = &c->hand->posrotmtx; break;
    default: break;
    } return value;
}

static void bgunArmPitchWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunarmpitchcontext *c = opaque;
    switch (binding) {
    case GGV_USE_POSROT: c->hand->useposrot = value.b; break;
    case GGV_ARM_PITCH_M30: c->hand->posrotmtx.m[3][0] = value.f; break;
    case GGV_ARM_PITCH_M31: c->hand->posrotmtx.m[3][1] = value.f; break;
    case GGV_ARM_PITCH_M32: c->hand->posrotmtx.m[3][2] = value.f; break;
    default: break;
    }
}

static ggv_value bgunArmPitchCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)d;
    switch (service) {
    case GGV_ARM_PITCH_X_ROTATION: mtx4LoadXRotation(a.f, b.ref); break;
    case GGV_ARM_PITCH_COSF: value.f = cosf(a.f); break;
    case GGV_ARM_PITCH_SINF: value.f = sinf(a.f); break;
    default: break;
    } return value;
}

static ggv_value bgunIsLoadedRead(void *opaque, ggv_binding binding)
{
    ggv_value value = {0}; (void)opaque;
    switch (binding) {
    case GGV_LOADED_GUNMEMOWNER: value.i = g_Vars.currentplayer->gunctrl.gunmemowner; break;
    case GGV_LOADED_GUNMEMTYPE: value.i = g_Vars.currentplayer->gunctrl.gunmemtype; break;
    case GGV_LOADED_GUNMEMNEW: value.i = g_Vars.currentplayer->gunctrl.gunmemnew; break;
    case GGV_LOADED_MASTERLOADSTATE: value.i = g_Vars.currentplayer->gunctrl.masterloadstate; break;
    default: break;
    } return value;
}

static ggv_value bgunIsReloadingRead(void *opaque, ggv_binding binding)
{
    struct hand *hand = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_STATE_VALUE: value.i = hand->state; break;
    default: break;
    } return value;
}

// The loaded/reloading predicates store nothing, but the executor requires a write callback.
static void bgunHandPredicateWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque; (void)binding; (void)value;
}

void bgunSetArmPitch(struct hand *hand, f32 angle)
{
    struct bgunarmpitchcontext context = {0}; context.hand = hand; context.angle = angle;
    ggv_host host = { &context, bgunArmPitchRead, bgunArmPitchWrite, bgunArmPitchCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SET_ARM_PITCH), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: arm pitch refused immutable program");
    }
}

struct bgunautoswitchcontext {
    struct handweaponinfo *info;
    s32 handnum;
    struct hand *hand;
    s32 lvupdate;
    struct hand *otherhand;
    struct handweaponinfo otherinfo;
};
static ggv_value bgunAutoSwitchRead(void *opaque, ggv_binding binding)
{
    struct bgunautoswitchcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_STATE_INUSE: value.i = c->hand->inuse; break;
    case GGV_STATE_MINOR: value.i = c->hand->stateminor; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_WEAPON_FUNCTION: value.i = c->hand->gset.weaponfunc; break;
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    case GGV_ANIM_WAIT_INCREMENT: value.i = c->hand->animframeinc; break;
    case GGV_HAND_MODE_NEXT: value.u = c->hand->modenext; break;
    case GGV_RELOAD_PAL: value.b = PAL; break;
    case GGV_NORMAL_MULTIPLAYER: value.b = g_Vars.normmplayerisrunning; break;
    case GGV_MULTIPLAYER: value.b = g_Vars.mplayerisrunning; break;
    case GGV_RELOAD_DOAUTOSELECT: value.i = g_Vars.currentplayer->doautoselect; break;
    case GGV_AUTO_OTHER_INUSE: value.i = c->otherhand->inuse; break;
    case GGV_AUTO_OTHER_FUNCTION: value.i = c->otherhand->gset.weaponfunc; break;
    case GGV_RELOAD_OTHER_STATE: value.i = c->otherhand->state; break;
    case GGV_AUTO_FRESH_OTHER_STATE: value.i = g_Vars.currentplayer->hands[1 - c->handnum].state; break;
#if VERSION >= VERSION_PAL_BETA
    case GGV_RECOVERY_PAL: value.b = true; break;
#else
    case GGV_RECOVERY_PAL: value.b = false; break;
#endif
    default: break;
    }
    return value;
}
static void bgunAutoSwitchWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunautoswitchcontext *c = opaque;
    switch (binding) {
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; break;
    case GGV_HAND_LASTDIRVALID: c->hand->lastdirvalid = value.b; break;
#if VERSION < VERSION_PAL_BETA
    case GGV_ANIM_TICK_INCREMENT_LEGACY: c->hand->animframeincfreal = value.u; break;
#endif
    case GGV_REMAINDER: c->hand->shotremainder = value.f; break;
    case GGV_WEAPON_FUNCTION: c->hand->gset.weaponfunc = value.i; break;
    case GGV_RELOAD_MODE: c->hand->mode = value.u; break;
    case GGV_HAND_MODE_NEXT: c->hand->modenext = value.u; break;
    case GGV_RELOAD_COUNT: c->hand->count = value.i; break;
    case GGV_RELOAD_COUNT60: c->hand->count60 = value.i; break;
    case GGV_RELOAD_PAUSETIME60: c->hand->pausetime60 = value.i; break;
    default: break;
    }
}
static ggv_value bgunAutoSwitchCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunautoswitchcontext *c = opaque; ggv_value value = {0};
    (void)b; (void)d;
    switch (service) {
    case GGV_HAND_CURRENT_FUNCTION: value.i = bgunIsUsingSecondaryFunction(); break;
    case GGV_HAND_SET_STATE: value.b = bgunSetState(c->handnum, a.i); break;
    case GGV_HAND_READY_SWITCH: value.b = bgunIsReadyToSwitch(c->handnum); break;
    case GGV_RELOAD_SET_ARM_PITCH: bgunSetArmPitch(c->hand, a.f); break;
    case GGV_HAND_DELETE_WEAPON: playermgrDeleteWeapon(c->handnum); break;
    case GGV_HAND_FREE_ROCKET: bgunFreeHeldRocket(c->handnum); break;
    case GGV_RELOAD_AMMO_STATUS: value.i = bgun0f098ca0(a.i, c->info, c->hand); break;
    case GGV_AUTO_CAPTURE_OTHER:
        c->otherhand = &g_Vars.currentplayer->hands[1 - c->handnum];
        bgunGetWeaponInfo(&c->otherinfo, 1 - c->handnum);
        break;
    case GGV_AUTO_OTHER_AMMO_STATUS: value.i = bgun0f098ca0(a.i, &c->otherinfo, c->otherhand); break;
    case GGV_AUTO_OTHER_ELIGIBILITY: value.b = bgun0f099188(c->otherhand, a.i); break;
    case GGV_AUTO_SWITCH_WEAPON: bgunAutoSwitchWeapon(); break;
    default: break;
    }
    return value;
}
s32 bgunTickIncAutoSwitch(struct handweaponinfo *info, s32 handnum, struct hand *hand, s32 lvupdate)
{
    struct bgunautoswitchcontext context = {0};
    context.info = info; context.handnum = handnum; context.hand = hand; context.lvupdate = lvupdate;
    ggv_host host = { &context, bgunAutoSwitchRead, bgunAutoSwitchWrite, bgunAutoSwitchCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_AUTO_SWITCH), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: autoswitch refused immutable program");
    }
    return result;
}

bool bgunIsReloading(struct hand *hand)
{
    ggv_host host = { hand, bgunIsReloadingRead, bgunHandPredicateWrite }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_IS_RELOADING), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: reloading predicate refused immutable program"); return false;
    }
    return result;
}

s32 bgunTickIncReload(struct handweaponinfo *info, s32 handnum, struct hand *hand, s32 lvupdate)
{
    struct bgunreloadtickcontext context = {0}; context.info = info; context.handnum = handnum; context.hand = hand; context.lvupdate = lvupdate;
    ggv_host host = { &context, bgunReloadTickRead, bgunReloadTickWrite, bgunReloadTickCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_TICK_INC_RELOAD), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: reload tick refused immutable program"); return 0;
    }
    return result;
}

s32 bgunTickIncChangeFunc(struct handweaponinfo *info, s32 handnum, struct hand *hand, s32 lvupdate)
{
    struct bgunchangefunccontext context = {0}; context.info = info; context.handnum = handnum; context.hand = hand; context.lvupdate = lvupdate;
    ggv_host host = { &context, bgunChangeFuncRead, bgunChangeFuncWrite, bgunChangeFuncCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_TICK_INC_CHANGE_FUNC), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: function change tick refused immutable program"); return 0;
    }
    return result;
}

static const weapon_graph_held_function_t *bgunGetHeldGraph(struct gset *gset)
{
	if (wgV2RuntimeIsWeapon(gset->weaponnum)) return wgV2RuntimeGsetHeld(gset);
	return weaponGraphRuntimeGetHeldFunctionForGameplay(gset->weaponnum,
		gset->weaponfunc);
}

static s32 bgunGetFunctionTypeFromGraph(struct weaponfunc *func,
		const weapon_graph_held_function_t *graph)
{
	if (graph && graph->has_function_type_id) {
		return graph->function_type_id;
	}
	return func ? func->type : INVENTORYFUNCTYPE_NONE;
}

static s32 bgunGetAmmoIndexFromGraph(struct weaponfunc *func,
		const weapon_graph_held_function_t *graph)
{
	if (graph && graph->ammo_slot >= 0) {
		return graph->ammo_slot;
	}
	return func ? func->ammoindex : -1;
}

static const weapon_graph_projectile_runtime_t *bgunGetProjectileGraph(
		const weapon_graph_held_function_t *graph)
{
	return weaponGraphRuntimeGetProjectileForHeldFunction(graph);
}

static const weapon_graph_entity_runtime_t *bgunGetEntityGraph(
		const weapon_graph_held_function_t *graph)
{
	return weaponGraphRuntimeGetEntityForHeldFunction(graph);
}

static s32 bgunGraphTicks60To240(s32 ticks60)
{
	if (ticks60 < 0) {
		return -1;
	}
	return TICKS(ticks60 * 4);
}

/* c3849 Unit 1c: weaponobj timer240 is s16; saturate authored timers over
 * ~136 seconds instead of overflowing (mirrors propobj.c's clamp). */
static s32 bgunGraphClampTicks240(s32 ticks240)
{
	if (ticks240 > 32767) {
		return 32767;
	}
	return ticks240;
}

static u32 bgunGetProjectileFlagsFromGraph(u32 fallback,
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile)
{
	u32 flags = graph ? graph->flags : fallback;

	if (projectile) {
		if (projectile->powered) {
			flags |= FUNCFLAG_PROJECTILE_POWERED;
		}
		if (projectile->calculate_trajectory) {
			flags |= FUNCFLAG_CALCULATETRAJECTORY;
		}
		/* c3849 guidance Slice B: a trajectory_correction module implies the
		 * OG trajectory solve (aim_source/solve_velocity are validate-only;
		 * the OG backend always crosshair-aims and velocity-solves). */
		if (projectile->has_trajectory_correction) {
			flags |= FUNCFLAG_CALCULATETRAJECTORY;
		}
		if (projectile->has_homing) {
			flags |= FUNCFLAG_HOMINGROCKET;
		}
		if (projectile->has_fly_by_wire) {
			flags |= FUNCFLAG_FLYBYWIRE;
		}
	}

	return flags;
}

static s32 bgunGetProjectileModelnumFromGraph(
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile,
		s32 fallback)
{
	if (graph && graph->has_projectile_modelnum) {
		return graph->projectile_modelnum;
	}
	if (projectile && projectile->has_projectile_modelnum) {
		return projectile->projectile_modelnum;
	}
	return fallback;
}

static s32 bgunGetThrownModelnumFromGraph(
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile,
		const weapon_graph_entity_runtime_t *entity,
		s32 fallback)
{
	if (graph && graph->has_projectile_modelnum) {
		return graph->projectile_modelnum;
	}
	if (projectile && projectile->has_projectile_modelnum) {
		return projectile->projectile_modelnum;
	}
	if (entity && entity->has_projectile_modelnum) {
		return entity->projectile_modelnum;
	}
	return fallback;
}

static f32 bgunGetProjectileScaleFromGraph(
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile,
		f32 fallback)
{
	if (graph && graph->has_scale) {
		return graph->scale;
	}
	if (projectile && projectile->has_scale) {
		return projectile->scale;
	}
	return fallback;
}

static f32 bgunGetProjectileSpeedFromGraph(
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile,
		f32 fallback)
{
	if (graph && graph->has_speed) {
		return graph->speed;
	}
	if (projectile && projectile->has_speed) {
		return projectile->speed;
	}
	return fallback;
}

static s32 bgunGetProjectileTravelFromGraph(
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile,
		s32 fallback)
{
	if (graph && graph->has_travel_distance) {
		return graph->travel_distance;
	}
	if (projectile && projectile->has_travel_distance) {
		return projectile->travel_distance;
	}
	return fallback;
}

static s32 bgunGetProjectileTimerFromGraph(
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile,
		s32 fallback)
{
	if (graph && graph->has_timer_ticks60) {
		return graph->timer_ticks60;
	}
	if (projectile && projectile->has_timer) {
		return projectile->timer_ticks60;
	}
	if (projectile && projectile->has_timer60) {
		return projectile->timer60;
	}
	return fallback;
}

static s32 bgunGetActivationTimeFromGraph(
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile,
		const weapon_graph_entity_runtime_t *entity,
		s32 fallback)
{
	if (graph && graph->has_activation_time_ticks60) {
		return graph->activation_time_ticks60;
	}
	/* B-916: base timed mines author no timer param, so the field is 0;
	 * an unguarded read arms timer240 = 0 and detonates on the first tick. */
	if (entity && entity->has_timed_detonatable &&
			entity->timed_timer_ticks60 > 0) {
		return entity->timed_timer_ticks60;
	}
	if (entity && entity->has_armed_explosive &&
			entity->arm_delay_ticks60 > 0) {
		return entity->arm_delay_ticks60;
	}
	if (entity && entity->has_activation_time60) {
		return entity->activation_time60;
	}
	if (projectile && projectile->has_activation_time60) {
		return projectile->activation_time60;
	}
	return fallback;
}

static f32 bgunGetProjectileReflectFromGraph(
		const weapon_graph_held_function_t *graph,
		const weapon_graph_projectile_runtime_t *projectile,
		f32 fallback)
{
	if (graph && graph->has_reflect_angle) {
		return graph->reflect_angle;
	}
	if (projectile && projectile->has_reflect_angle) {
		return projectile->reflect_angle;
	}
	return fallback;
}

static s32 bgunGetProjectileSoundFromGraph(
		const weapon_graph_held_function_t *graph,
		s32 fallback)
{
	if (graph && graph->has_soundnum) {
		return graph->soundnum;
	}
	return fallback;
}

static void bgunApplyEntityGraphToWeapon(struct weaponobj *weapon,
		const weapon_graph_entity_runtime_t *entity)
{
	s32 timer60 = 0;
	bool hastimer = false;

	if (!weapon || !entity || !entity->valid) {
		return;
	}

	/* B-916: zero-guard matches bgunGetActivationTimeFromGraph; base timed
	 * mines author no timer param, so 0 must fall through. */
	if (entity->has_timed_detonatable &&
			entity->timed_timer_ticks60 > 0) {
		timer60 = entity->timed_timer_ticks60;
		hastimer = true;
	} else if (entity->has_armed_explosive &&
			entity->arm_delay_ticks60 > 0) {
		timer60 = entity->arm_delay_ticks60;
		hastimer = true;
	} else if (entity->has_activation_time60) {
		timer60 = entity->activation_time60;
		hastimer = true;
	}

	if (hastimer) {
		weapon->timer240 =
			bgunGraphClampTicks240(bgunGraphTicks60To240(timer60));
	}
}

/* Synchronous typed composition. Catalog generation/host/audio witnesses stay
 * borrowed for this invocation only. Public-source profile selection remains
 * a separately owned propagation gate; SLIDE/RUMBLE bodies are unconverted. */
#include "gameplay_graph_values.h"
struct bgunexpressioncontext {
    struct hand *hand;
    struct gunctrl *ctrl;
    struct weaponfunc *func;
    const weapon_graph_held_function_t *graph;
    u32 flags;
    s32 ammoindex;
    bool legacyauto, automatic;
    s32 acceleration, deceleration;
    s32 functype, handnum;
    struct handweaponinfo *info;
    u32 *rontime, *rofftime;
};
static ggv_value bgunExpressionRead(void *opaque, ggv_binding binding)
{
    struct bgunexpressioncontext *context = opaque;
    struct hand *hand = context->hand;
    ggv_value value = {0};
    switch (binding) {
    case GGV_FUNCTION_FLAGS: value.u = context->flags; break;
    case GGV_AMMO_INDEX: value.i = context->ammoindex; break;
    case GGV_AUTOMATIC: value.b = context->automatic; break;
    case GGV_ACCELERATION: value.i = context->acceleration; break;
    case GGV_DECELERATION: value.i = context->deceleration; break;
    case GGV_TRIGGER: value.b = hand->triggeron; break;
    case GGV_STATE_FLAGS: value.u = hand->stateflags; break;
    case GGV_BURST_BULLETS: value.i = hand->burstbullets; break;
    case GGV_AIM: value.b = g_Vars.currentplayer->insightaimmode; break;
    case GGV_LOADED_AMMO: value.i = hand->loadedammo[context->ammoindex]; break;
    case GGV_AMMO_TYPE: value.i = context->ctrl->ammotypes[context->ammoindex]; break;
    case GGV_SHOTGUN: value.b = hand->gset.weaponnum == WEAPON_SHOTGUN; break;
    case GGV_SHOTGUN_DELAY: value.i = TICKS(13); break;
    case GGV_LASER: value.b = hand->gset.weaponnum == WEAPON_LASER; break;
    /* Kept for the separately addressable legacy prefix programs only. */
    case GGV_INITIAL_RPM:
        if (context->graph && context->graph->has_initial_rpm) value.f = context->graph->initial_rpm;
        else if (context->legacyauto) value.f = ((struct weaponfunc_shootauto *)context->func)->initialrpm;
        break;
    case GGV_MAX_RPM:
        if (context->graph && context->graph->has_max_rpm) value.f = context->graph->max_rpm;
        else if (context->legacyauto) value.f = ((struct weaponfunc_shootauto *)context->func)->maxrpm;
        break;
    case GGV_CLOCK: value.f = LVUPDATE60FREAL(); break;
    case GGV_SPOOL: value.f = hand->gs_float1; break;
    case GGV_STATE_FRAMES: value.i = hand->stateframes; break;
    case GGV_SHOTS: value.i = hand->shotstotake; break;
    case GGV_REMAINDER: value.f = hand->shotremainder; break;
    case GGV_FIRING: value.b = hand->firing; break;
    case GGV_GRAPH_FLAGS: value.u = context->graph->flags; break;
    case GGV_LEGACY_FLAGS: value.u = context->func->flags; break;
    case GGV_GRAPH_AMMO_SLOT: value.i = context->graph->ammo_slot; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = context->func ? context->func->ammoindex : -1; break;
    case GGV_GRAPH_HAS_TYPE: value.b = context->graph->has_function_type_id; break;
    case GGV_GRAPH_FUNCTION_TYPE: value.i = context->graph->function_type_id; break;
    case GGV_LEGACY_FUNCTION_TYPE: value.i = context->func ? context->func->type : INVENTORYFUNCTYPE_NONE; break;
    case GGV_FUNCTION_TYPE: value.i = context->functype; break;
    case GGV_LEGACY_AUTOMATIC: value.b = context->legacyauto; break;
    case GGV_GRAPH_HAS_MAX_RPM: value.b = context->graph->has_max_rpm; break;
    case GGV_GRAPH_HAS_ACCEL: value.b = context->graph->has_turret_accel; break;
    case GGV_GRAPH_HAS_DECEL: value.b = context->graph->has_turret_decel; break;
    case GGV_GRAPH_ACCEL: value.i = context->graph->turret_accel; break;
    case GGV_GRAPH_DECEL: value.i = context->graph->turret_decel; break;
    case GGV_LEGACY_ACCEL: value.i = ((struct weaponfunc_shootauto *)context->func)->turretaccel; break;
    case GGV_LEGACY_DECEL: value.i = ((struct weaponfunc_shootauto *)context->func)->turretdecel; break;
    case GGV_GRAPH_HAS_INITIAL_RPM: value.b = context->graph->has_initial_rpm; break;
    case GGV_GRAPH_INITIAL_RPM: value.f = context->graph->initial_rpm; break;
    case GGV_GRAPH_MAX_RPM: value.f = context->graph->max_rpm; break;
    case GGV_LEGACY_INITIAL_RPM: value.f = ((struct weaponfunc_shootauto *)context->func)->initialrpm; break;
    case GGV_LEGACY_MAX_RPM: value.f = ((struct weaponfunc_shootauto *)context->func)->maxrpm; break;
    case GGV_GE_FLASH: value.b = g_BgunGeMuzzleFlashes; break;
    case GGV_FLASH: value.b = hand->flashon; break;
    case GGV_LOAD_SLIDE: value.f = hand->loadslide; break;
    case GGV_STATE_VAR1: value.i = hand->statevar1; break;
    case GGV_ATTACK_TYPE: value.i = hand->attacktype; break;
    case GGV_BIND_ATTACK_SHOOT: value.i = HANDATTACKTYPE_SHOOT; break;
    case GGV_ATTACK_PROJECTILE: value.i = HANDATTACKTYPE_SHOOTPROJECTILE; break;
    case GGV_FUNCTION_SHOOT_LOW: value.i = INVENTORYFUNCTYPE_SHOOT; break;
    case GGV_FUNCTION_SHOOT_SINGLE: value.i = INVENTORYFUNCTYPE_SHOOT_SINGLE; break;
    case GGV_CURRENT_FRAME: value.i = g_Vars.lvframe60; break;
    case GGV_OTHER_LAST_FRAME: value.i = g_Vars.currentplayer->hands[1-context->handnum].lastshootframe60; break;
    case GGV_ALLOW_SHOOT_FRAME: value.i = hand->allowshootframe; break;
    case GGV_LAST_SHOOT_FRAME: value.i = hand->lastshootframe60; break;
    case GGV_AUDIO_SLOT_2: value.ref = hand->audiohandle2; break;
    case GGV_AUDIO_SLOT_3: value.ref = hand->audiohandle3; break;
    case GGV_MAULER: value.b = hand->gset.weaponnum == WEAPON_MAULER; break;
    case GGV_MATMOT: value.f = hand->matmot1; break;
    case GGV_PRIORITY_ENABLED:
#if VERSION >= VERSION_NTSC_1_0
        value.b = true;
#endif
        break;
    case GGV_SOUND_STOPPED: value.i = AL_STOPPED; break;
    case GGV_BINDING_COUNT: break;
    }
    return value;
}
static void bgunExpressionWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunexpressioncontext *context = opaque;
    struct hand *hand = context->hand;
    switch (binding) {
    case GGV_FUNCTION_FLAGS: context->flags = value.u; break;
    case GGV_AMMO_INDEX: context->ammoindex = value.i; break;
    case GGV_FUNCTION_TYPE: context->functype = value.i; break;
    case GGV_LEGACY_AUTOMATIC: context->legacyauto = value.b; break;
    case GGV_AUTOMATIC: context->automatic = value.b; break;
    case GGV_ACCELERATION: context->acceleration = value.i; break;
    case GGV_DECELERATION: context->deceleration = value.i; break;
    case GGV_SPOOL: hand->gs_float1 = value.f; break;
    case GGV_STATE_FRAMES: hand->stateframes = value.i; break;
    case GGV_SHOTS: hand->shotstotake = value.i; break;
    case GGV_REMAINDER: hand->shotremainder = value.f; break;
    case GGV_FIRING: hand->firing = value.b; break;
    case GGV_BURST_BULLETS: hand->burstbullets = value.i; break;
    case GGV_FLASH: hand->flashon = value.b; break;
    case GGV_LOAD_SLIDE: hand->loadslide = value.f; break;
    case GGV_STATE_VAR1: hand->statevar1 = value.i; break;
    case GGV_STATE_FLAGS: hand->stateflags = value.u; break;
    case GGV_LOADED_AMMO: hand->loadedammo[context->ammoindex] = value.i; break;
    case GGV_ATTACK_TYPE: hand->attacktype = value.i; break;
    case GGV_ALLOW_SHOOT_FRAME: hand->allowshootframe = value.i; break;
    case GGV_LAST_SHOOT_FRAME: hand->lastshootframe60 = value.i; break;
    default: break;
    }
}
static ggv_value bgunExpressionCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value c)
{
    struct bgunexpressioncontext *context = opaque;
    struct hand *hand = context->hand;
    ggv_value value = {0};
    (void)c;
    switch (service) {
    case GGV_RESOLVE_GRAPH:
        context->graph = bgunGetHeldGraph(&hand->gset);
        value.b = context->graph != NULL;
        break;
    case GGV_OVERRIDE_ON: mainOverrideVariable("rontime", context->rontime); break;
    case GGV_OVERRIDE_OFF: mainOverrideVariable("rofftime", context->rofftime); break;
    case GGV_START_SLIDE: bgunStartSlide(context->handnum); break;
    case GGV_RUMBLE: bgunRumble(context->handnum, context->info->weaponnum); break;
    case GGV_FIRESLOT_DURATION: value.i = gsetGetFireslotDuration(&hand->gset); break;
    case GGV_SINGLE_SOUND: value.i = gsetGetSingleShootSound(&hand->gset); break;
    case GGV_CURRENT_PRIORITY: value.i = osGetThreadPri(0); break;
    case GGV_AUDIO_PRIORITY: value.i = osGetThreadPri(&g_AudioManager.thread); break;
    case GGV_SET_PRIORITY: osSetThreadPri(0, a.i); break;
    case GGV_SOUND_STATE: value.i = a.ref ? sndGetState(a.ref) : AL_STOPPED; break;
    case GGV_SOUND_STOP: if (a.ref) audioStop(a.ref); break;
    case GGV_SOUND_START:
        if (a.i == 2) value.ref = sndStart(var80095200, b.i, &hand->audiohandle2, -1, -1, -1, -1, -1);
        else if (a.i == 3) value.ref = sndStart(var80095200, b.i, &hand->audiohandle3, -1, -1, -1, -1, -1);
        break;
    case GGV_SOUND_EVENT: if (a.ref) audioPostEvent(a.ref, AL_SNDP_PITCH_EVT, b.i); break;
    case GGV_SERVICE_COUNT: break;
    }
    return value;
}
s32 bgun0f09a3f8(struct hand *hand, struct weaponfunc *func)
{
    struct bgunexpressioncontext context = {0};
    context.hand = hand;
    context.ctrl = &g_Vars.currentplayer->gunctrl;
    context.func = func;
    ggv_host host = { &context, bgunExpressionRead, bgunExpressionWrite, bgunExpressionCall };
    int32_t result = -1;
    /* Immutable preparation and service dependency checks precede effects. */
    if (!ggvExecute(ggvStockProgram(GGV_SHOOT_CONTROL_FULL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: shooting control refused immutable program");
    }
    return result;
}
void bgun0f09a6f8(struct handweaponinfo *info, s32 handnum, struct hand *hand, struct weaponfunc *func)
{
    static u32 rontime = 2;
    static u32 rofftime = 4;
    struct bgunexpressioncontext context = {0};
    context.hand = hand;
    context.func = func;
    context.handnum = handnum;
    context.info = info;
    context.rontime = &rontime;
    context.rofftime = &rofftime;
    ggv_host host = { &context, bgunExpressionRead, bgunExpressionWrite, bgunExpressionCall };
    int32_t usesammo;
    if (!ggvExecute(ggvStockProgram(GGV_SHOT_COMMIT_FULL), &host, &usesammo)) {
        sysLogPrintf(LOG_WARNING, "GGV: shooting commit refused immutable program");
    }
}

/* GGV attack services begin: synchronous borrowed recoil/animation context. */
struct bgunattackcontext {
    struct hand *hand; struct handweaponinfo *info; s32 handnum;
    struct weaponfunc *func; const weapon_graph_held_function_t *graph;
    struct weapon *definition; u32 *gkef; f32 vibration[12];
};
static ggv_value bgunAttackRead(void *opaque, ggv_binding binding)
{
    struct bgunattackcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_RECOVERY_PAL: v.b = PAL; break;
    case GGV_RECOVERY_JPN: v.b = VERSION >= VERSION_JPN_FINAL; break;
    case GGV_GRAPH_RECOIL_24_HAS: v.b = c->graph->has_recoil_anim_unk24; break;
    case GGV_GRAPH_RECOIL_24: v.i = c->graph->recoil_anim_unk24; break;
    case GGV_LEGACY_RECOIL_24: v.i = ((struct weaponfunc_shoot *) c->func)->unk24; break;
    case GGV_GRAPH_RECOIL_25_HAS: v.b = c->graph->has_recoil_anim_unk25; break;
    case GGV_GRAPH_RECOIL_25: v.i = c->graph->recoil_anim_unk25; break;
    case GGV_LEGACY_RECOIL_25: v.i = ((struct weaponfunc_shoot *) c->func)->unk25; break;
    case GGV_GRAPH_RECOIL_26_HAS: v.b = c->graph->has_recoil_anim_unk26; break;
    case GGV_GRAPH_RECOIL_26: v.i = c->graph->recoil_anim_unk26; break;
    case GGV_LEGACY_RECOIL_26: v.i = ((struct weaponfunc_shoot *) c->func)->unk26; break;
    case GGV_GRAPH_RECOIL_27_HAS: v.b = c->graph->has_recoil_anim_unk27; break;
    case GGV_GRAPH_RECOIL_27: v.i = c->graph->recoil_anim_unk27; break;
    case GGV_LEGACY_RECOIL_27: v.i = ((struct weaponfunc_shoot *) c->func)->unk27; break;
    case GGV_GRAPH_RECOIL_TIME_HAS: v.b = c->graph->has_recoverytime_ticks60; break;
    case GGV_GRAPH_RECOIL_TIME: v.i = c->graph->recoverytime_ticks60; break;
    case GGV_LEGACY_RECOIL_TIME: v.i = ((struct weaponfunc_shoot *) c->func)->recoverytime60; break;
    case GGV_GRAPH_RECOIL_DIST_HAS: v.b = c->graph->has_recoildist; break;
    case GGV_GRAPH_RECOIL_DIST: v.f = c->graph->recoildist; break;
    case GGV_LEGACY_RECOIL_DIST: v.f = ((struct weaponfunc_shoot *) c->func)->recoildist; break;
    case GGV_GRAPH_RECOIL_ANGLE_HAS: v.b = c->graph->has_recoilangle; break;
    case GGV_GRAPH_RECOIL_ANGLE: v.f = c->graph->recoilangle; break;
    case GGV_LEGACY_RECOIL_ANGLE: v.f = ((struct weaponfunc_shoot *) c->func)->recoilangle; break;
    case GGV_TRIGGER_RELEASED: v.b = c->hand->triggerreleased; break;
    case GGV_ROTX_START: v.f = c->hand->rotxstart; break;
    case GGV_ROTX_END: v.f = c->hand->rotxend; break;
    case GGV_ROTX_OFFSET: v.f = c->hand->rotxoffset; break;
    case GGV_USE_POSROT: v.b = c->hand->useposrot; break;
    case GGV_STATE_MINOR: v.i = c->hand->stateminor; break;
    case GGV_ANIMATION_LATCH: v.b = c->hand->unk0cc8_01; break;
    case GGV_MATMOT2: v.f = c->hand->matmot2; break;
    case GGV_MATMOT1: v.f = c->hand->matmot1; break;
    case GGV_POS_START_X: v.f = c->hand->posstart.x; break;
    case GGV_POS_START_Y: v.f = c->hand->posstart.y; break;
    case GGV_POS_START_Z: v.f = c->hand->posstart.z; break;
    case GGV_POS_END_X: v.f = c->hand->posend.x; break;
    case GGV_POS_END_Y: v.f = c->hand->posend.y; break;
    case GGV_POS_END_Z: v.f = c->hand->posend.z; break;
    case GGV_POS_OFFSET_X: v.f = c->hand->posoffset.x; break;
    case GGV_POS_OFFSET_Y: v.f = c->hand->posoffset.y; break;
    case GGV_POS_OFFSET_Z: v.f = c->hand->posoffset.z; break;
    case GGV_AIM_POS_X: v.f = c->hand->aimpos.x; break;
    case GGV_AIM_POS_Z: v.f = c->hand->aimpos.z; break;
    case GGV_DEFINITION_POS_Z: v.f = c->definition->posz; break;
    case GGV_RECOIL_PI: v.f = M_PI; break;
    case GGV_RECOIL_BAD_TAU: v.f = M_BADTAU; break;
    case GGV_POS_MATRIX: v.ref = &c->hand->posrotmtx; break;
    case GGV_POS_COORD: v.ref = &c->hand->posoffset; break;
    case GGV_STATE_CYCLES: v.i = c->hand->statecycles; break;
    case GGV_BIND_ANIMATION_BUSY: v.b = c->hand->animmode == HANDANIMMODE_BUSY; break;
    case GGV_FIRE_ANIMATION: v.ref = c->func->fire_animation; break;
    case GGV_PASSED_HAND: v.ref = c->hand; break;
    case GGV_VIBRATION_START: v.ref = ((struct weaponfunc_shootauto *) c->func)->vibrationstart; break;
    case GGV_VIBRATION_MAX: v.ref = ((struct weaponfunc_shootauto *) c->func)->vibrationmax; break;
    case GGV_VIBRATION_SCRATCH: v.ref = c->vibration; break;
    case GGV_VIBRATION_UPGRADE: v.ref = c->hand->upgrademult; break;
    case GGV_VIBRATION_FINAL: v.ref = c->hand->finalmult; break;
    case GGV_TRIGGER: v.b = c->hand->triggeron; break;
    case GGV_STATE_FLAGS: v.u = c->hand->stateflags; break;
    case GGV_STATE_VAR1: v.i = c->hand->statevar1; break;
    case GGV_STATE_FRAMES: v.i = c->hand->stateframes; break;
    case GGV_SPOOL: v.f = c->hand->gs_float1; break;
    case GGV_BURST_BULLETS: v.i = c->hand->burstbullets; break;
    case GGV_LEGACY_FUNCTION_TYPE: v.i = c->func->type; break;
    case GGV_HAND_NUMBER: v.i = c->handnum; break;
    case GGV_SHOTGUN: v.b = c->hand->gset.weaponnum == WEAPON_SHOTGUN; break;
    case GGV_MAULER: v.b = c->hand->gset.weaponnum == WEAPON_MAULER; break;
    default: break;
    } return v;
}
static void bgunAttackWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunattackcontext *c = opaque;
    switch (binding) {
    case GGV_ROTX_START: c->hand->rotxstart = value.f; break;
    case GGV_ROTX_END: c->hand->rotxend = value.f; break;
    case GGV_ROTX_OFFSET: c->hand->rotxoffset = value.f; break;
    case GGV_USE_POSROT: c->hand->useposrot = value.b; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; break;
    case GGV_ANIMATION_LATCH: c->hand->unk0cc8_01 = value.b; break;
    case GGV_MATMOT2: c->hand->matmot2 = value.f; break;
    case GGV_MATMOT1: c->hand->matmot1 = value.f; break;
    case GGV_POS_START_X: c->hand->posstart.x = value.f; break;
    case GGV_POS_START_Y: c->hand->posstart.y = value.f; break;
    case GGV_POS_START_Z: c->hand->posstart.z = value.f; break;
    case GGV_POS_END_X: c->hand->posend.x = value.f; break;
    case GGV_POS_END_Y: c->hand->posend.y = value.f; break;
    case GGV_POS_END_Z: c->hand->posend.z = value.f; break;
    case GGV_POS_OFFSET_X: c->hand->posoffset.x = value.f; break;
    case GGV_POS_OFFSET_Y: c->hand->posoffset.y = value.f; break;
    case GGV_POS_OFFSET_Z: c->hand->posoffset.z = value.f; break;
    case GGV_STATE_FLAGS: c->hand->stateflags = value.u; break;
    case GGV_STATE_VAR1: c->hand->statevar1 = value.i; break;
    case GGV_SPOOL: c->hand->gs_float1 = value.f; break;
    case GGV_BURST_BULLETS: c->hand->burstbullets = value.i; break;
    default: break;
    }
}
static ggv_value bgunAttackCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunattackcontext *c = opaque; ggv_value v = {0};
    switch (service) {
    case GGV_RECOIL_TICKS: v.i = TICKS(a.i); break;
    case GGV_RECOIL_SIN: v.f = sinf(a.f); break;
    case GGV_RECOIL_COS: v.f = cosf(a.f); break;
    case GGV_RECOIL_TWEEN: v.f = modelTweenRotAxis(a.f, b.f, d.f); break;
    case GGV_RECOIL_AIM_X: v.f = func0f0b131c(c->handnum); break;
    case GGV_CAPTURE_DEFINITION: c->definition = c->info->definition; break;
    case GGV_MATRIX_X: mtx4LoadXRotation(a.f, b.ref); break;
    case GGV_MATRIX_TRANSLATION: mtx4SetTranslation(a.ref, b.ref); break;
    case GGV_MATRIX_IDENTITY: mtx4LoadIdentity(a.ref); break;
    case GGV_RESOLVE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); v.b = c->func != NULL; break;
    case GGV_OVERRIDE_GKEF: mainOverrideVariable("gkef", c->gkef); break;
    case GGV_FIRE_ANIMATION_START: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_ATTACK_READY: v.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_ATTACK_CONTROL: v.i = bgun0f09a3f8(c->hand, c->func); break;
    case GGV_ATTACK_COMMIT: bgun0f09a6f8(c->info, c->handnum, c->hand, c->func); break;
    case GGV_ATTACK_RECOVERY: v.b = bgun0f09aba4(c->hand, c->info, c->handnum, (struct weaponfunc_shoot *) c->func); break;
    case GGV_VIBRATION_MULTIPLY: func0f097b40(a.ref, b.ref, d.ref); break;
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); v.b = c->graph != NULL; break;
    default: break;
    } return v;
}
static ggv_value bgunAttackCall4(void *opaque, ggv_service4 service, ggv_value a, ggv_value b, ggv_value c, ggv_value d)
{
    (void) opaque; ggv_value value = {0};
    if (service == GGV_VIBRATION_INTERPOLATE) func0f097b64(a.ref, b.ref, c.f, d.ref);
    return value;
}
/* GGV attack services end. */
bool bgun0f09aba4(struct hand *hand, struct handweaponinfo *info, s32 handnum, struct weaponfunc_shoot *func)
{
    struct bgunattackcontext context = {0};
    context.hand = hand; context.info = info; context.handnum = handnum;
    context.func = (struct weaponfunc *) func;
    ggv_host host = { &context, bgunAttackRead, bgunAttackWrite, bgunAttackCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_RECOIL_RECOVERY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: recoil recovery refused immutable program");
        return false;
    }
    return result;
}

bool bgunTickIncAttackingShoot(struct handweaponinfo *info, s32 handnum, struct hand *hand)
{
    static u32 var80070128 = 99;
    struct bgunattackcontext context = {0};
    context.hand = hand; context.info = info; context.handnum = handnum;
    context.gkef = &var80070128;
    ggv_host host = { &context, bgunAttackRead, bgunAttackWrite, bgunAttackCall, bgunAttackCall4 };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ATTACK_SHOOT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: attack driver refused immutable program");
        return false;
    }
    return result;
}

/* GGV throw services begin: synchronous borrowed throw context. */
struct bgunthrowcontext {
    struct hand *hand; s32 handnum; struct weaponfunc_throw *func;
    const weapon_graph_held_function_t *graph; s32 ammoindex;
};
static ggv_value bgunThrowRead(void *opaque, ggv_binding binding)
{
    struct bgunthrowcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_THROW_GRAPH_HAS_ACTIVATION: v.b = c->graph->has_activation_time_ticks60; break;
    case GGV_THROW_GRAPH_ACTIVATION: v.i = c->graph->activation_time_ticks60; break;
    case GGV_THROW_LEGACY_ACTIVATION: v.i = c->func->activatetime60; break;
    case GGV_THROW_GRAPH_HAS_RECOVERY: v.b = c->graph->has_recovery_time_ticks60; break;
    case GGV_THROW_GRAPH_RECOVERY: v.i = c->graph->recovery_time_ticks60; break;
    case GGV_THROW_LEGACY_RECOVERY: v.i = c->func->recoverytime60; break;
    case GGV_PRIME_TIMER: v.i = c->hand->primetimer60; break;
    case GGV_CURRENT_PLAYER_THROWING: v.b = g_Vars.currentplayer->gunctrl.throwing; break;
    case GGV_WEAPON_NUMBER: v.i = c->hand->gset.weaponnum; break;
    case GGV_WEAPON_FUNCTION: v.i = c->hand->gset.weaponfunc; break;
    case GGV_VERSION_1_OR_LATER: v.b = VERSION >= VERSION_NTSC_1_0; break;
    case GGV_GRAPH_FLAGS: v.u = c->graph->flags; break;
    case GGV_LEGACY_FLAGS: v.u = c->func->base.flags; break;
    case GGV_GRAPH_AMMO_SLOT: v.i = c->graph->ammo_slot; break;
    case GGV_LEGACY_AMMO_INDEX: v.i = c->func->base.ammoindex; break;
    case GGV_AMMO_INDEX: v.i = c->ammoindex; break;
    case GGV_STATE_MINOR: v.i = c->hand->stateminor; break;
    case GGV_STATE_CYCLES: v.i = c->hand->statecycles; break;
    case GGV_STATE_FRAMES: v.i = c->hand->stateframes; break;
    case GGV_FIRE_ANIMATION: v.ref = c->func->base.fire_animation; break;
    case GGV_PASSED_HAND: v.ref = c->hand; break;
    case GGV_HAND_NUMBER: v.i = c->handnum; break;
    case GGV_TRIGGER_RELEASED: v.b = c->hand->triggerreleased; break;
    case GGV_TRIGGER: v.b = c->hand->triggeron; break;
    case GGV_LOADED_AMMO: v.i = c->hand->loadedammo[c->ammoindex]; break;
    default: break;
    } return v;
}
static void bgunThrowWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunthrowcontext *c = opaque;
    switch (binding) {
    case GGV_AMMO_INDEX: c->ammoindex = value.i; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; break;
    case GGV_ANIMATION_LATCH: c->hand->unk0cc8_01 = value.b; break;
    case GGV_FIRING: c->hand->firing = value.b; break;
    case GGV_ATTACK_TYPE: c->hand->attacktype = value.i; break;
    case GGV_LOADED_AMMO: c->hand->loadedammo[c->ammoindex] = value.i; break;
    case GGV_PRIME_TIMER: c->hand->primetimer60 = value.i; break;
    case GGV_CURRENT_PLAYER_THROWING: g_Vars.currentplayer->gunctrl.throwing = value.b; break;
    default: break;
    }
}
static ggv_value bgunThrowCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunthrowcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_FUNCTION: c->func = (struct weaponfunc_throw *) gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); value.b = c->graph != NULL; break;
    case GGV_THROW_REMOVE_ITEM: invRemoveItemByNum(a.i); break;
    case GGV_THROW_SWITCH_PREVIOUS: bgunSwitchToPrevious(); break;
    case GGV_THROW_AUTO_SWITCH: bgunAutoSwitchWeapon(); break;
    case GGV_THROW_RESET_ANIMATION: bgunResetAnim(a.ref); break;
    case GGV_THROW_SECONDARY_VALUE: value.i = bgunIsUsingSecondaryFunction(); break;
    case GGV_FIRE_ANIMATION_START: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_ATTACK_READY: value.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    default: break;
    } return value;
}
/* GGV throw services end. */
bool bgunTickIncAttackingThrow(s32 handnum, struct hand *hand)
{
    struct bgunthrowcontext context = {0};
    context.hand = hand; context.handnum = handnum;
    ggv_host host = { &context, bgunThrowRead, bgunThrowWrite, bgunThrowCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_THROW_CONTROL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: throw driver refused immutable program");
        return false;
    }
    return result;
}

/* GGV min-clip services begin: pure borrowed scalar utility arguments. */
struct bgunminclipcontext { s32 weaponnum; s32 funcnum; };
static ggv_value bgunMinClipRead(void *opaque, ggv_binding binding)
{
    struct bgunminclipcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_WEAPON_NUMBER: value.i = c->weaponnum; break;
    case GGV_WEAPON_FUNCTION: value.i = c->funcnum; break;
    default: break;
    } return value;
}
/* GGV min-clip services end. */
static void bgunMinClipWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void) opaque; (void) binding; (void) value; /* Prepared utility has no stores. */
}
s32 bgunGetMinClipQty(s32 weaponnum, s32 funcnum)
{
    struct bgunminclipcontext context = { weaponnum, funcnum };
    ggv_host host = { &context, bgunMinClipRead, bgunMinClipWrite, NULL };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MIN_CLIP_QTY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: min-clip utility refused immutable program");
        return 0;
    }
    return result;
}

const char var7f1ab8ac[] = "changegunmem type %d CurrentPlayer->gunctrl.gunmemtype %d\n";
const char var7f1ab8e8[] = "LockTimer: %d\n";
const char var7f1ab8f8[] = "BriGun: Releasing gunmem - current gunmemtype %d gunmemnew %d\n";
const char var7f1ab938[] = "GiveMem: %d\n";

u32 var8007012c = 0x00000000;
u32 var80070130 = 0x00000000;

/* GGV melee services begin: synchronous borrowed hand/configuration witnesses. */
struct bgunmeleecontext {
    struct hand *hand; s32 handnum; struct weaponfunc *func;
    const weapon_graph_held_function_t *graph; s32 ammoindex;
};
static ggv_value bgunMeleeRead(void *opaque, ggv_binding binding)
{
    struct bgunmeleecontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_GRAPH_AMMO_SLOT: value.i = c->graph->ammo_slot; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = c->func->ammoindex; break;
    case GGV_AMMO_INDEX: value.i = c->ammoindex; break;
    case GGV_WEAPON_NUMBER: value.i = c->hand->gset.weaponnum; break;
    case GGV_STATE_MINOR: value.i = c->hand->stateminor; break;
    case GGV_STATE_CYCLES: value.i = c->hand->statecycles; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_MATMOT2: value.f = c->hand->matmot2; break;
    case GGV_BURST_BULLETS: value.i = c->hand->burstbullets; break;
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_TRIGGER_RELEASED: value.b = c->hand->triggerreleased; break;
    case GGV_FIRE_ANIMATION: value.ref = c->func->fire_animation; break;
    case GGV_PASSED_HAND: value.ref = c->hand; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_LOADED_AMMO: value.i = c->hand->loadedammo[c->ammoindex]; break;
    default: break;
    } return value;
}
static void bgunMeleeWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunmeleecontext *c = opaque;
    switch (binding) {
    case GGV_AMMO_INDEX: c->ammoindex = value.i; break;
    case GGV_MATMOT2: c->hand->matmot2 = value.f; break;
    case GGV_BURST_BULLETS: c->hand->burstbullets = value.i; break;
    case GGV_FIRING: c->hand->firing = value.b; break;
    case GGV_ATTACK_TYPE: c->hand->attacktype = value.i; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; break;
    case GGV_ANIMATION_LATCH: c->hand->unk0cc8_01 = value.b; break;
    case GGV_LOADED_AMMO: c->hand->loadedammo[c->ammoindex] = value.i; break;
    default: break;
    }
}
static ggv_value bgunMeleeCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmeleecontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); value.b = c->graph != NULL; break;
    case GGV_MELEE_MIN_CLIP: value.i = bgunGetMinClipQty(a.i, b.i); break;
    case GGV_MELEE_CLOCK: value.f = LVUPDATE60FREAL(); break;
    case GGV_MELEE_CHEAT_ACTIVE: value.b = cheatIsActive(a.i); break;
    case GGV_MELEE_ANIM_BUSY: value.b = bgunIsAnimBusy(a.ref); break;
    case GGV_FIRE_ANIMATION_START: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_ATTACK_READY: value.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    default: break;
    } return value;
}
/* GGV melee services end. */
bool bgunTickIncAttackingMelee(s32 handnum, struct hand *hand)
{
    struct bgunmeleecontext context = {0};
    context.hand = hand; context.handnum = handnum;
    ggv_host host = { &context, bgunMeleeRead, bgunMeleeWrite, bgunMeleeCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MELEE_CONTROL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: melee driver refused immutable program");
        return false;
    }
    return result;
}

/* GGV Special services begin: borrowed synchronous context. */
struct bgunspecialcontext { struct hand *hand; struct weaponfunc_special *func; const weapon_graph_held_function_t *graph; s32 ammoindex; };
static ggv_value bgunSpecialRead(void *opaque, ggv_binding binding)
{
    struct bgunspecialcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_SPECIAL_GRAPH_HAS_VALUE: value.b = c->graph->has_specialfunc; break;
    case GGV_SPECIAL_GRAPH_VALUE: value.i = c->graph->specialfunc; break;
    case GGV_SPECIAL_LEGACY_VALUE: value.i = c->func->specialfunc; break;
    case GGV_SPECIAL_GRAPH_HAS_RECOVERY: value.b = c->graph->has_recovery_time_ticks60; break;
    case GGV_SPECIAL_GRAPH_RECOVERY: value.i = c->graph->recovery_time_ticks60; break;
    case GGV_SPECIAL_LEGACY_RECOVERY: value.i = c->func->recoverytime60; break;
    case GGV_GRAPH_AMMO_SLOT: value.i = c->graph->ammo_slot; break;
    case GGV_LEGACY_AMMO_INDEX: value.i = c->func->base.ammoindex; break;
    case GGV_AMMO_INDEX: value.i = c->ammoindex; break;
    case GGV_STATE_MINOR: value.i = c->hand->stateminor; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_LOADED_AMMO: value.i = c->hand->loadedammo[c->ammoindex]; break;
    default: break;
    } return value;
}
static void bgunSpecialWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunspecialcontext *c = opaque;
    switch (binding) {
    case GGV_AMMO_INDEX: c->ammoindex = value.i; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; break;
    case GGV_FIRING: c->hand->firing = value.b; break;
    case GGV_ATTACK_TYPE: c->hand->attacktype = value.i; break;
    case GGV_LOADED_AMMO: c->hand->loadedammo[c->ammoindex] = value.i; break;
    default: break;
    }
}
static ggv_value bgunSpecialCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunspecialcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_FUNCTION: c->func = (struct weaponfunc_special *) gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); value.b = c->graph != NULL; break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    default: break;
    } return value;
}
/* GGV Special services end. */
bool bgunTickIncAttackingSpecial(struct hand *hand)
{
    struct bgunspecialcontext context = {0}; context.hand = hand;
    ggv_host host = { &context, bgunSpecialRead, bgunSpecialWrite, bgunSpecialCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SPECIAL_CONTROL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: special driver refused immutable program");
        return false;
    }
    return result;
}

/* GGV Empty services begin: borrowed synchronous context. */
struct bgunemptycontext { struct handweaponinfo *info; struct hand *hand; s32 handnum; s32 lvupdate; struct weaponfunc *func; };
static ggv_value bgunEmptyRead(void *opaque, ggv_binding binding)
{
    struct bgunemptycontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_STATE_FLAGS: value.u = c->hand->stateflags; break;
    case GGV_EMPTY_ANIM_MODE: value.i = c->hand->animmode; break;
    case GGV_EMPTY_DEFINITION: value.ref = c->info->definition; break;
    case GGV_FIRE_ANIMATION: value.ref = c->func->fire_animation; break;
    case GGV_PASSED_HAND: value.ref = c->hand; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_PRIORITY_ENABLED: value.b = VERSION >= VERSION_NTSC_1_0; break;
    case GGV_EMPTY_WATER_SOUND: value.i = SFX_HIT_WATER; break;
    case GGV_EMPTY_CLICK_SOUND: value.i = SFX_FIREEMPTY; break;
    default: break;
    } return value;
}
static void bgunEmptyWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunemptycontext *c = opaque;
    switch (binding) {
    case GGV_STATE_FRAMES: c->hand->stateframes = value.i; break;
    case GGV_STATE_FLAGS: c->hand->stateflags = value.u; break;
    case GGV_EMPTY_MODE: c->hand->mode = value.u; break;
    case GGV_EMPTY_COUNT60: c->hand->count60 = value.i; break;
    case GGV_EMPTY_COUNT: c->hand->count = value.i; break;
    default: break;
    }
}
static ggv_value bgunEmptyCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunemptycontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    case GGV_EMPTY_RESET_ANIM: bgunResetAnim(a.ref); break;
    case GGV_FIRE_ANIMATION_START: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_ATTACK_READY: value.b = bgun0f098a44(a.ref, b.i); break;
    case GGV_EMPTY_SOUND_START: value.ref = sndStart(var80095200, a.i, NULL, -1, -1, -1, -1, -1); break;
    case GGV_SOUND_EVENT: audioPostEvent(a.ref, AL_SNDP_PITCH_EVT, b.i); break;
    case GGV_CURRENT_PRIORITY: value.i = osGetThreadPri(0); break;
    case GGV_AUDIO_PRIORITY: value.i = osGetThreadPri(&g_AudioManager.thread); break;
    case GGV_SET_PRIORITY: osSetThreadPri(0, a.i); break;
    case GGV_EMPTY_SET_STATE: value.b = bgunSetState(a.i, b.i); break;
    default: break;
    } return value;
}
/* GGV Empty services end. */
s32 bgunTickIncAttackEmpty(struct handweaponinfo *info, s32 handnum, struct hand *hand, s32 lvupdate)
{
    struct bgunemptycontext context = {0};
    context.info = info; context.hand = hand; context.handnum = handnum; context.lvupdate = lvupdate;
    ggv_host host = { &context, bgunEmptyRead, bgunEmptyWrite, bgunEmptyCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_EMPTY_ATTACK), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: empty driver refused immutable program");
        return 0;
    }
    return result;
}

int bgunGraphV2Accept(struct player *player, int handnum)
{
	if (!player || player != g_Vars.currentplayer || handnum < 0 || handnum > 1) return 0;
	struct hand *hand = &player->hands[handnum];
	if (!hand->inuse || hand->state != HANDSTATE_IDLE || !wgV2RuntimeGsetHeld(&hand->gset)) return 0;
	hand->mode = HANDMODE_ATTACK; hand->count = hand->count60 = 0;
	hand->triggerreleased = false; hand->activatesecondary = false; player->doautoselect = false;
	return bgunSetState(handnum, HANDSTATE_ATTACK);
}
int bgunGraphV2Tick(struct player *player, int handnum)
{
	if (!player || player != g_Vars.currentplayer || handnum < 0 || handnum > 1) return 1;
	struct hand *hand = &player->hands[handnum]; struct handweaponinfo info;
	info.weaponnum = hand->gset.weaponnum; info.definition = (struct weapon *)wgV2RuntimeGsetWeapon(&hand->gset);
	info.gunctrl = &player->gunctrl;
	const int before[2] = {hand->loadedammo[0], hand->loadedammo[1]};
	wg_v2_runtime_snapshot observation = {0};
	wgV2RuntimeSnapshot(player, handnum, &observation);
	const int finished = !info.definition || bgunTickIncAttackingShoot(&info, handnum, hand);
	const int after[2] = {hand->loadedammo[0], hand->loadedammo[1]};
	wgV2RuntimeObserveDebit(&observation.delivery, before, after);
	return finished;
}
void bgunGraphV2Cancel(struct player *player, int handnum)
{
	if (!player || handnum < 0 || handnum > 1) return;
	struct hand *hand = &player->hands[handnum];
	bgunResetAnim(hand); hand->anim.animnum = hand->anim.animnum2 = 0;
	hand->firing = hand->flashon = false; hand->shotstotake = 0;
	if (hand->state == HANDSTATE_ATTACK) {
		hand->state = HANDSTATE_IDLE; hand->stateframes = hand->statecycles = 0;
		hand->stateflags = hand->stateminor = hand->statelastframe = 0;
	}
}

/* GGV Dispatch services begin: synchronous borrowed context. */
struct bgundispatchcontext { struct handweaponinfo *info; struct hand *hand; s32 handnum; s32 lvupdate; struct weaponfunc *func; const weapon_graph_held_function_t *graph; };
static ggv_value bgunDispatchRead(void *opaque, ggv_binding binding)
{
    struct bgundispatchcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_EMPTY_DEFINITION: value.ref = c->info->definition; break;
    case GGV_GRAPH_HAS_TYPE: value.b = c->graph->has_function_type_id; break;
    case GGV_GRAPH_FUNCTION_TYPE: value.i = c->graph->function_type_id; break;
    case GGV_LEGACY_FUNCTION_TYPE: value.i = c->func->type; break;
    case GGV_WEAPON_NUMBER: value.i = c->hand->gset.weaponnum; break;
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    default: break;
    } return value;
}
static void bgunDispatchWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgundispatchcontext *c = opaque;
    switch (binding) {
    case GGV_WEAPON_FUNCTION: c->hand->gset.weaponfunc = value.i; break;
    default: break;
    }
}
static ggv_value bgunDispatchCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgundispatchcontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_RESOLVE_GRAPH: c->graph = bgunGetHeldGraph(&c->hand->gset); value.b = c->graph != NULL; break;
    case GGV_RESOLVE_FUNCTION: c->func = gsetGetWeaponFunction(&c->hand->gset); value.b = c->func != NULL; break;
    case GGV_DISPATCH_SHOOT: value.b = bgunTickIncAttackingShoot(c->info, c->handnum, c->hand); break;
    case GGV_DISPATCH_THROW: value.b = bgunTickIncAttackingThrow(c->handnum, c->hand); break;
    case GGV_DISPATCH_MELEE: value.b = bgunTickIncAttackingMelee(c->handnum, c->hand); break;
    case GGV_DISPATCH_SPECIAL: value.b = bgunTickIncAttackingSpecial(c->hand); break;
    case GGV_EMPTY_SET_STATE: value.b = bgunSetState(a.i, b.i); break;
    default: break;
    } return value;
}
/* GGV Dispatch services end. */
s32 bgunTickIncAttack(struct handweaponinfo *info, s32 handnum, struct hand *hand, s32 lvupdate)
{
    struct bgundispatchcontext context = {0};
    context.info = info; context.hand = hand; context.handnum = handnum; context.lvupdate = lvupdate;
    ggv_host host = { &context, bgunDispatchRead, bgunDispatchWrite, bgunDispatchCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ATTACK_DISPATCH), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: attack dispatcher refused immutable program");
        return 0;
    }
    return result;
}

struct bgunreadytoswitchcontext { s32 handnum; struct player *player; };
static ggv_value bgunReadyToSwitchRead(void *opaque, ggv_binding binding)
{
    struct bgunreadytoswitchcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_SWITCH_RANGE_VALID: value.b = g_FrIsValidWeapon; break;
    case GGV_SWITCH_RIGHT_WEAPON: value.i = c->player->hands[HAND_RIGHT].gset.weaponnum; break;
    case GGV_SWITCH_CURRENT_THROWING: value.i = g_Vars.currentplayer->gunctrl.throwing; break;
    case GGV_SWITCH_LEFT_INUSE: value.i = c->player->hands[HAND_LEFT].inuse; break;
    case GGV_SWITCH_LEFT_STATE: value.i = c->player->hands[HAND_LEFT].state; break;
    case GGV_SWITCH_LEFT_MINOR: value.i = c->player->hands[HAND_LEFT].stateminor; break;
    case GGV_SWITCH_PENDING_WEAPON: value.i = c->player->gunctrl.switchtoweaponnum; break;
    case GGV_SWITCH_RIGHT_STATE: value.i = c->player->hands[HAND_RIGHT].state; break;
    case GGV_STATE_INUSE: value.i = c->player->hands[c->handnum].inuse; break;
    case GGV_SWITCH_DUALWIELD: value.b = c->player->gunctrl.dualwielding; break;
    default: break;
    }
    return value;
}
static void bgunReadyToSwitchWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque; (void)binding; (void)value;
}
static ggv_value bgunReadyToSwitchCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)b; (void)d;
    switch (service) {
    case GGV_RANGE_SLOT: value.i = frGetSlot(); break;
    case GGV_RANGE_WEAPON: value.i = frGetWeaponBySlot(a.i); break;
    default: break;
    }
    return value;
}
bool bgunIsReadyToSwitch(s32 handnum)
{
    struct bgunreadytoswitchcontext context = { handnum, g_Vars.currentplayer };
    ggv_host host = { &context, bgunReadyToSwitchRead, bgunReadyToSwitchWrite, bgunReadyToSwitchCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_READY_TO_SWITCH), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: switch readiness refused immutable program");
    }
    return result;
}

/* Issue 1 root cause (2026-04-25, B-246): the legacy `count >= 3` gate
	 * was the actual blocker for the player FP weapon never appearing in
	 * SP and MP. Mike's instrumented playtest log (`019dc325`) showed the
	 * hand state machine reaching CHANGEGUN_LOAD (stateminor=2) cleanly,
	 * but `bgunCanFreeWeapon` returned false because `count` stayed at 0/1
	 * across many frames in LOAD instead of accumulating to >= 3.
	 *
	 * `hand->count` is incremented in `bgunTickInc` (line ~3209) on every
	 * call when `g_Vars.lvupdate240 > 0`, which Mike's BMOVE diag confirmed
	 * was true throughout. The reset sites at `count = 0` are confined to
	 * UNEQUIP / LOWER stateminor handlers (lines 2865 / 2901) and to the
	 * LOAD -> RAISE / RAISE -> EQUIP transitions (lines 2971 / 3163). None
	 * of those should fire while the hand is sitting in LOAD/mode=HANDMODE_6
	 * waiting for the master load to complete.
	 *
	 * Despite that, the runtime log proved `count` was not advancing past
	 * 0/1 in this codepath -- a discrepancy that does not match the static
	 * read. Without a second instrumented round to pin where the reset was
	 * coming from, the safest structural fix is to drop the `count >= 3`
	 * guard altogether: the design intent of the gate is "wait a few frames
	 * after entering LOAD before letting bgunTickSwitch2 swap the weapon
	 * record", but the same throttling is already provided by the LOWER
	 * stateminor's `stateframes >= delay` gate (line 2924) which only
	 * advances to LOAD after the unequip+lower animations finish.
	 *
	 * Removing the count check makes the deferred-switch consumer fire on
	 * the first frame of LOAD, which is the correct moment: by then the
	 * LOWER stateminor has run for `delay` ticks and the hand is settled.
	 * The dual-hand requirement (caller checks both R and L canFree) still
	 * ensures both hands reach LOAD before the swap commits.
	 *
	 * This unblocks the full chain: bgunTickSwitch2 fires -> weaponnum and
	 * gunmemnew set -> bgunTickMasterLoad acquires BONDGUN ownership and
	 * loads the FP model -> bgunIsLoaded() returns true -> hand->mode
	 * advances 6 -> 7 -> EQUIP -> stateminor advances LOAD -> RAISE -> EQUIP
	 * -> hand->visible flips true -> FP weapon renders, fire fires, pickup
	 * works. Bug spans SP and MP because both share this exact gate. */
struct bgunfreeweaponcontext {
    s32 handnum;
    struct player *player;
    s32 *last_right;
    s32 *last_left;
};
static ggv_value bgunFreeWeaponRead(void *opaque, ggv_binding binding)
{
    struct bgunfreeweaponcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_STATE_VALUE: value.i = c->player->hands[c->handnum].state; break;
    case GGV_STATE_MINOR: value.i = c->player->hands[c->handnum].stateminor; break;
    case GGV_FREE_THROWING: value.i = c->player->gunctrl.throwing; break;
    case GGV_STATE_PLAYER_NUMBER: value.i = g_Vars.currentplayernum; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_FREE_LAST_RIGHT: value.i = *c->last_right; break;
    case GGV_FREE_LAST_LEFT: value.i = *c->last_left; break;
    default: break;
    }
    return value;
}
static void bgunFreeWeaponWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunfreeweaponcontext *c = opaque;
    if (binding == GGV_FREE_LAST_RIGHT) *c->last_right = value.i;
    else if (binding == GGV_FREE_LAST_LEFT) *c->last_left = value.i;
}
static ggv_value bgunFreeWeaponCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunfreeweaponcontext *c = opaque; ggv_value value = {0}; (void)b; (void)d;
    if (service == GGV_FREE_LOG) {
        sysLogPrintf(LOG_NOTE,
            "LOG.WPN.DIAG: bgunCanFreeWeapon player=0 hand=%d -> %d state=%d sm=%d throwing=%d",
            (s32)c->handnum, (s32)a.i, (s32)c->player->hands[c->handnum].state,
            (s32)c->player->hands[c->handnum].stateminor, (s32)c->player->gunctrl.throwing);
    }
    return value;
}
bool bgunCanFreeWeapon(s32 handnum)
{
    static s32 s_last_R = -1;
    static s32 s_last_L = -1;
    struct bgunfreeweaponcontext context = { handnum, g_Vars.currentplayer, &s_last_R, &s_last_L };
    ggv_host host = { &context, bgunFreeWeaponRead, bgunFreeWeaponWrite, bgunFreeWeaponCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_CAN_FREE_WEAPON), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: can-free predicate refused immutable program");
    }
    return result;
}

static ggv_value bgunSwitchCompleteRead(void *opaque, ggv_binding binding)
{
    struct bgunreadytoswitchcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_SWITCH_PENDING_WEAPON: value.i = c->player->gunctrl.switchtoweaponnum; break;
    case GGV_SWITCH_DUALWIELD_RAW: value.i = c->player->gunctrl.dualwielding; break;
    case GGV_STATE_INUSE: value.i = c->player->hands[c->handnum].inuse; break;
    case GGV_LOADED_GUNMEMNEW: value.i = c->player->gunctrl.gunmemnew; break;
    case GGV_RELOAD_OTHER_STATE: value.i = c->player->hands[1 - c->handnum].state; break;
    default: break;
    }
    return value;
}
static ggv_value bgunSwitchCompleteCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)a; (void)b; (void)d;
    if (service == GGV_RELOAD_IS_LOADED) value.b = bgunIsLoaded();
    return value;
}
bool bgun0f09bf44(s32 handnum)
{
    struct bgunreadytoswitchcontext context = { handnum, g_Vars.currentplayer };
    ggv_host host = { &context, bgunSwitchCompleteRead, bgunReadyToSwitchWrite, bgunSwitchCompleteCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_SWITCH_COMPLETE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: switch completion refused immutable program");
    }
    return result;
}

struct bgunequipsoundcontext { struct handweaponinfo *info; s32 handnum; };
static ggv_value bgunEquipSoundRead(void *opaque, ggv_binding binding)
{
    struct bgunequipsoundcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_RELOAD_LVUPDATE240: value.i = g_Vars.lvupdate240; break;
    case GGV_RELOAD_CAMERA_MODE: value.i = g_Vars.currentplayer->cameramode; break;
    case GGV_RELOAD_PLAYER_INVINCIBLE: value.b = g_PlayerInvincible != 0; break;
    case GGV_RELOAD_PLAYER_DEAD: value.b = g_Vars.currentplayer->isdead != 0; break;
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_VERSION_1_OR_LATER: value.b = VERSION >= VERSION_NTSC_1_0; break;
    case GGV_EQUIP_SOUND_SCANNER: value.i = SFX_EQUIP_HORIZONSCANNER; break;
    case GGV_EQUIP_SOUND_LASER: value.i = SFX_PICKUP_LASER; break;
    case GGV_EQUIP_SOUND_KNIFE: value.i = SFX_PICKUP_KNIFE; break;
    case GGV_EQUIP_SOUND_MINE: value.i = SFX_PICKUP_MINE; break;
    case GGV_EQUIP_SOUND_GUN: value.i = SFX_PICKUP_GUN; break;
    case GGV_EQUIP_WEAPON_NECKLACE: value.i = WEAPON_NECKLACE; break;
    case GGV_EQUIP_WEAPON_BRIEFCASE2: value.i = WEAPON_BRIEFCASE2; break;
    default: break;
    }
    return value;
}
static void bgunEquipSoundWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque; (void)binding; (void)value;
}
static ggv_value bgunEquipSoundCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)d;
    switch (service) {
    case GGV_RELOAD_IS_LOADED: value.b = bgunIsLoaded(); break;
    case GGV_EMPTY_SOUND_START: value.ref = sndStart(var80095200, a.i, NULL, -1, -1, -1, -1, -1); break;
    case GGV_CURRENT_PRIORITY: value.i = osGetThreadPri(0); break;
    case GGV_AUDIO_PRIORITY: value.i = osGetThreadPri(&g_AudioManager.thread); break;
    case GGV_SET_PRIORITY: osSetThreadPri(0, a.i); break;
    case GGV_SOUND_EVENT: audioPostEvent(a.ref, AL_SNDP_PITCH_EVT, b.i); break;
    default: break;
    }
    return value;
}
static void bgunPlayEquipSound(struct handweaponinfo *info, s32 handnum)
{
    struct bgunequipsoundcontext context = { info, handnum };
    ggv_host host = { &context, bgunEquipSoundRead, bgunEquipSoundWrite, bgunEquipSoundCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_EQUIP_SOUND), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: equip sound policy refused immutable program");
    }
}

struct bgunchangecontext {
    struct handweaponinfo *info;
    struct hand *hand;
    s32 handnum, lvupdate;
    s32 diagloaded, diagswitchto, diaggunmem, diagother, diagresult;
};
static ggv_value bgunChangeRead(void *opaque, ggv_binding binding)
{
    struct bgunchangecontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_CHANGE_DEFINITION: value.ref = c->info->definition; break;
    case GGV_PASSED_HAND: value.ref = c->hand; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    case GGV_STATE_CYCLES: value.i = c->hand->statecycles; break;
    case GGV_STATE_MINOR: value.i = c->hand->stateminor; break;
    case GGV_STATE_INUSE: value.i = c->hand->inuse; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_STATE_FLAGS: value.u = c->hand->stateflags; break;
    case GGV_EMPTY_ANIM_MODE: value.i = c->hand->animmode; break;
    case GGV_RELOAD_EJECT_STATE: value.i = c->hand->ejectstate; break;
    case GGV_CHANGE_EJECT_TYPE: value.i = c->hand->ejecttype; break;
    case GGV_WEAPON_NUMBER: value.i = c->info->weaponnum; break;
    case GGV_AMMO_HAND_WEAPON: value.i = c->hand->gset.weaponnum; break;
    case GGV_NORMAL_MULTIPLAYER: value.b = g_Vars.normmplayerisrunning != 0; break;
    case GGV_MULTIPLAYER: value.b = g_Vars.mplayerisrunning != 0; break;
    case GGV_SWITCH_CURRENT_THROWING: value.i = g_Vars.currentplayer->gunctrl.throwing; break;
    case GGV_STATE_PLAYER_NUMBER: value.i = g_Vars.currentplayernum; break;
    case GGV_CURRENT_FRAME: value.i = g_Vars.lvframenum; break;
    case GGV_CHANGE_SWITCHTO: value.i = g_Vars.currentplayer->gunctrl.switchtoweaponnum; break;
    case GGV_LOADED_GUNMEMNEW: value.i = g_Vars.currentplayer->gunctrl.gunmemnew; break;
    case GGV_RELOAD_OTHER_STATE: value.i = g_Vars.currentplayer->hands[1 - c->handnum].state; break;
    case GGV_CHANGE_PAUSECHANGE: value.i = c->hand->pausechange; break;
    case GGV_RELOAD_PAUSETIME60: value.i = c->hand->pausetime60; break;
    case GGV_RELOAD_COUNT60: value.i = c->hand->count60; break;
    case GGV_RELOAD_COUNT: value.i = c->hand->count; break;
    case GGV_RELOAD_MODE: value.u = c->hand->mode; break;
    default: break;
    }
    return value;
}
static void bgunChangeWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunchangecontext *c = opaque;
    switch (binding) {
    case GGV_RELOAD_PAUSETIME60: c->hand->pausetime60 = value.i; break;
    case GGV_RELOAD_COUNT: c->hand->count = value.i; break;
    case GGV_RELOAD_COUNT60: c->hand->count60 = value.i; break;
    case GGV_STATE_FLAGS: c->hand->stateflags = value.u; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; break;
    case GGV_STATE_FRAMES: c->hand->stateframes = value.i; break;
    case GGV_RELOAD_MODE: c->hand->mode = value.u; break;
    case GGV_FIRING: c->hand->firing = value.b; break;
    case GGV_ATTACK_TYPE: c->hand->attacktype = value.i; break;
    case GGV_WEAPON_FUNCTION: c->hand->gset.weaponfunc = value.i; break;
    case GGV_EMPTY_ANIM_MODE: c->hand->animmode = value.i; break;
    case GGV_RELOAD_UNK0CC8_02: c->hand->unk0cc8_02 = value.b; break;
    case GGV_RELOAD_DOAUTOSELECT: g_Vars.currentplayer->doautoselect = value.i; break;
    case GGV_CHANGE_DIAG_LOADED: c->diagloaded = value.b != 0; break;
    case GGV_CHANGE_DIAG_SWITCHTO: c->diagswitchto = value.b != 0; break;
    case GGV_CHANGE_DIAG_GUNMEM: c->diaggunmem = value.b != 0; break;
    case GGV_CHANGE_DIAG_OTHER: c->diagother = value.b != 0; break;
    case GGV_CHANGE_DIAG_RESULT: c->diagresult = value.i; break;
    default: break;
    }
}
static ggv_value bgunChangeCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunchangecontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_CHANGE_UNEQUIP_ANIM: value.ref = ((struct weapon *)a.ref)->unequip_animation; break;
    case GGV_CHANGE_EQUIP_ANIM: value.ref = ((struct weapon *)a.ref)->equip_animation; break;
    case GGV_RELOAD_WEAPON_HAS_FLAG: value.b = weaponHasFlag(a.i, b.u); break;
    case GGV_RELOAD_AMMO_STATUS: value.i = bgun0f098ca0(a.i, c->info, c->hand); break;
    case GGV_RECOIL_TICKS: value.i = TICKS(a.i); break;
    case GGV_HAND_START_ANIMATION: bgunStartAnimation(a.ref, b.i, d.ref); break;
    case GGV_HAND_DELETE_WEAPON: playermgrDeleteWeapon(c->handnum); break;
    case GGV_HAND_FREE_ROCKET: bgunFreeHeldRocket(c->handnum); break;
    case GGV_RELOAD_SET_ARM_PITCH: bgunSetArmPitch(c->hand, a.f); break;
    case GGV_RELOAD_IS_LOADED: value.b = bgunIsLoaded(); break;
    case GGV_CHANGE_COMPLETE: value.i = bgun0f09bf44(c->handnum); break;
    case GGV_HAND_SET_STATE: value.b = bgunSetState(c->handnum, a.i); break;
    case GGV_CHANGE_CREATE_WEAPON: playermgrCreateWeapon(c->handnum); break;
    case GGV_CHANGE_ACTIVATE: bgun0f098f8c(c->info, c->hand); break;
    case GGV_CHANGE_EQUIP_SOUND: bgunPlayEquipSound(c->info, c->handnum); break;
    case GGV_RELOAD_FILENUM2: value.i = weaponGetFileNum2(a.i); break;
    case GGV_CHANGE_LOG_LOAD:
        sysLogPrintf(LOG_NOTE,
            "LOG.WPN.DIAG: LOAD-mode6 player=0 hand=R frame=%d "
            "hand_mode=%d sm=%d state=%d "
            "cond_loaded=%d cond_switchto_neg=%d cond_gunmemnew_neg=%d cond_other_not_reload=%d "
            "bgun0f09bf44=%d "
            "L_state=%d L_sm=%d L_inuse=%d L_mode=%d "
            "pausechange=%d pausetime60=%d count60=%d",
            g_Vars.lvframenum, (s32)c->hand->mode, (s32)c->hand->stateminor, (s32)c->hand->state,
            c->diagloaded, c->diagswitchto, c->diaggunmem, c->diagother, c->diagresult,
            (s32)g_Vars.currentplayer->hands[HAND_LEFT].state,
            (s32)g_Vars.currentplayer->hands[HAND_LEFT].stateminor,
            (s32)g_Vars.currentplayer->hands[HAND_LEFT].inuse,
            (s32)g_Vars.currentplayer->hands[HAND_LEFT].mode,
            (s32)c->hand->pausechange, (s32)c->hand->pausetime60, (s32)c->hand->count60);
        break;
    default: break;
    }
    return value;
}
s32 bgunTickIncChangeGun(struct handweaponinfo *info, s32 handnum, struct hand *hand, s32 lvupdate)
{
    struct bgunchangecontext context = {0};
    ggv_host host = { &context, bgunChangeRead, bgunChangeWrite, bgunChangeCall };
    int32_t result = 0;
    context.info = info; context.hand = hand; context.handnum = handnum; context.lvupdate = lvupdate;
    if (!ggvExecute(ggvStockProgram(GGV_CHANGE_GUN), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: change-gun driver refused immutable program");
    }
    return result;
}

/**
 * This function may have implemented an early beta feature where the gun could
 * be held at the side of the screen, pointed upwards. The feature was shown in
 * a demo video but doesn't exist in any public version of the game.
 */
s32 bgunTickIncState2(struct handweaponinfo *info, s32 handnum, struct hand *hand, s32 lvupdate)
{
	return 0;
}

struct bgunhandtickcontext {
    struct handweaponinfo *info;
    s32 handnum;
    s32 lvupdate;
    struct hand *hand;
};
static ggv_value bgunHandTickRead(void *opaque, ggv_binding binding)
{
    struct bgunhandtickcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_STATE_VALUE: value.i = c->hand->state; break;
    case GGV_STATE_FRAMES: value.i = c->hand->stateframes; break;
    case GGV_STATE_CYCLES: value.i = c->hand->statecycles; break;
    case GGV_EMPTY_LVUPDATE: value.i = c->lvupdate; break;
    case GGV_RELOAD_LVUPDATE240: value.i = g_Vars.lvupdate240; break;
    case GGV_COUNTDOWN_UPDATE60: value.i = g_Vars.lvupdate60; break;
    case GGV_RELOAD_COUNT60: value.i = c->hand->count60; break;
    case GGV_RELOAD_COUNT: value.i = c->hand->count; break;
    default: break;
    }
    return value;
}
static void bgunHandTickWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunhandtickcontext *c = opaque;
    switch (binding) {
    case GGV_FIRING: c->hand->firing = value.b; break;
    case GGV_FLASH: c->hand->flashon = value.b; break;
    case GGV_STATE_FRAMES: c->hand->stateframes = value.i; break;
    case GGV_STATE_LAST_FRAME: c->hand->statelastframe = value.i; break;
    case GGV_STATE_CYCLES: c->hand->statecycles = value.i; break;
    case GGV_RELOAD_COUNT60: c->hand->count60 = value.i; break;
    case GGV_RELOAD_COUNT: c->hand->count = value.i; break;
    case GGV_USE_POSROT: c->hand->useposrot = value.b; break;
    default: break;
    }
}
static ggv_value bgunHandTickCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunhandtickcontext *c = opaque; ggv_value value = {0};
    (void)a; (void)b; (void)d;
    switch (service) {
    case GGV_TICK_DELIVERY_PENDING: value.b = wgV2RuntimeDeliveryPending(g_Vars.currentplayer, c->handnum); break;
    case GGV_TICK_IS_V2: value.b = wgV2RuntimeIsWeapon(c->info->weaponnum); break;
    case GGV_TICK_ADVANCE_V2: value.i = wgV2RuntimeAdvance(g_Vars.currentplayer, c->handnum); break;
    case GGV_TICK_IDLE: value.i = bgunTickIncIdle(c->info, c->handnum, c->hand, c->lvupdate); break;
    case GGV_TICK_RELOAD: value.i = bgunTickIncReload(c->info, c->handnum, c->hand, c->lvupdate); break;
    case GGV_TICK_ATTACK: value.i = bgunTickIncAttack(c->info, c->handnum, c->hand, c->lvupdate); break;
    case GGV_TICK_CHANGE_GUN: value.i = bgunTickIncChangeGun(c->info, c->handnum, c->hand, c->lvupdate); break;
    case GGV_TICK_EMPTY: value.i = bgunTickIncAttackEmpty(c->info, c->handnum, c->hand, c->lvupdate); break;
    case GGV_TICK_AUTO_SWITCH: value.i = bgunTickIncAutoSwitch(c->info, c->handnum, c->hand, c->lvupdate); break;
    case GGV_TICK_CHANGE_FUNC: value.i = bgunTickIncChangeFunc(c->info, c->handnum, c->hand, c->lvupdate); break;
    default: break;
    }
    return value;
}
s32 bgunTickInc(struct handweaponinfo *info, s32 handnum, s32 lvupdate)
{
    struct bgunhandtickcontext context = { info, handnum, lvupdate, &g_Vars.currentplayer->hands[handnum] };
    ggv_host host = { &context, bgunHandTickRead, bgunHandTickWrite, bgunHandTickCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_HAND_TICK), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: hand tick refused immutable program");
    }
    return result;
}

/* GGV State services begin: synchronous borrowed context. */
struct bgunstatecontext { s32 handnum; s32 state; struct hand *hand; s32 logweapon; s32 loginuse; };
static ggv_value bgunStateRead(void *opaque, ggv_binding binding)
{
    struct bgunstatecontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_STATE_REQUEST: value.i = c->state; break;
    case GGV_STATE_VALUE: value.i = c->hand->state; break;
    case GGV_WEAPON_FUNCTION: value.i = c->hand->gset.weaponfunc; break;
    case GGV_STATE_PLAYER_NUMBER: value.i = g_Vars.currentplayernum; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    case GGV_WEAPON_NUMBER: value.i = c->hand->gset.weaponnum; break;
    case GGV_STATE_INUSE: value.i = c->hand->inuse; break;
    default: break;
    } return value;
}
static void bgunStateWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunstatecontext *c = opaque;
    switch (binding) {
    case GGV_STATE_VALUE: c->hand->state = value.i; break;
    case GGV_STATE_FRAMES: c->hand->stateframes = value.i; break;
    case GGV_STATE_FLAGS: c->hand->stateflags = value.u; break;
    case GGV_STATE_CYCLES: c->hand->statecycles = value.i; break;
    case GGV_STATE_MINOR: c->hand->stateminor = value.i; break;
    case GGV_STATE_LAST_FRAME: c->hand->statelastframe = value.i; break;
    case GGV_STATE_LOG_WEAPON: c->logweapon = value.i; break;
    case GGV_STATE_LOG_INUSE: c->loginuse = value.i; break;
    default: break;
    }
}
static ggv_value bgunStateCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunstatecontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_STATE_SELECT_HAND: c->hand = &g_Vars.currentplayer->hands[c->handnum]; break;
	// Sanity check - don't allow changing function if there is no other
    case GGV_STATE_ALTERNATE_FUNCTION: value.ref = weaponGetFunction(&c->hand->gset, a.i); break;
	/* B-246 round-6 instrumentation: state transitions are the load-bearing
	 * signal for the FP rig lifecycle. Log every accepted transition (and
	 * any rejected one). Player 0 only. Low frequency by construction. */
    case GGV_STATE_LOG: sysLogPrintf(LOG_NOTE, "LOG.WPN.DIAG: bgunSetState player=0 hand=%d %d->%d valid=%d wpn=%d inuse=%d", a.i, b.i, (s32) c->state, (s32) d.b, (s32) c->logweapon, (s32) c->loginuse); break;
    default: break;
    } return value;
}
/* GGV State services end. */
bool bgunSetState(s32 handnum, s32 state)
{
    struct bgunstatecontext context = {0}; context.handnum = handnum; context.state = state;
    ggv_host host = { &context, bgunStateRead, bgunStateWrite, bgunStateCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SET_STATE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: state utility refused immutable program");
        return false;
    }
    return result;
}

struct bguntickhandcontext {
    s32 handnum;
    struct hand *hand;
    struct handweaponinfo info;
};
static ggv_value bgunTickHandRead(void *opaque, ggv_binding binding)
{
    struct bguntickhandcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_COUNTDOWN_UPDATE60: value.i = g_Vars.lvupdate60; break;
    case GGV_ANIM_WAIT_INCREMENT: value.i = c->hand->animframeinc; break;
    case GGV_RELOAD_PAL: value.b = PAL; break;
#if VERSION >= VERSION_PAL_BETA
    case GGV_RECOVERY_PAL: value.b = true; break;
#else
    case GGV_RECOVERY_PAL: value.b = false; break;
    case GGV_ANIM_TICK_INCREMENT_LEGACY: value.u = c->hand->animframeincfreal; break;
#endif
    default: break;
    }
    return value;
}
static void bgunTickHandWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bguntickhandcontext *c = opaque;
    switch (binding) {
    case GGV_ANIM_WAIT_INCREMENT: c->hand->animframeinc = value.i; break;
#if VERSION >= VERSION_PAL_BETA
    case GGV_ANIM_WAIT_INCREMENT_REAL: c->hand->animframeincfreal = value.f; break;
#else
    case GGV_ANIM_TICK_INCREMENT_LEGACY: c->hand->animframeincfreal = value.u; break;
#endif
    default: break;
    }
}
static ggv_value bgunTickHandCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bguntickhandcontext *c = opaque; ggv_value value = {0};
    (void)b; (void)d;
    switch (service) {
    case GGV_HAND_INFO_CAPTURE: bgunGetWeaponInfo(&c->info, c->handnum); break;
    case GGV_MODEL_ABS_ANIM_SPEED: value.f = modelGetAbsAnimSpeed(&c->hand->gunmodel); break;
    case GGV_HAND_TICK_CHILD: value.i = bgunTickInc(&c->info, c->handnum, a.i); break;
    default: break;
    }
    return value;
}
void bgunTickHand(s32 handnum)
{
    struct bguntickhandcontext context = {0};
    context.handnum = handnum; context.hand = &g_Vars.currentplayer->hands[handnum];
    ggv_host host = { &context, bgunTickHandRead, bgunTickHandWrite, bgunTickHandCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_TICK_HAND), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: hand frame refused immutable program");
    }
}

void bgunTickSwitch(void)
{
	bgunTickSwitch2();
}

struct bguninitanimscontext { struct hand *hand; };
static ggv_value bgunInitAnimsRead(void *opaque, ggv_binding binding)
{
    struct bguninitanimscontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_INIT_PLAYER_REF: v.ref = g_Vars.currentplayer; break;
    case GGV_INIT_ANIM_REF: v.ref = &c->hand->anim; break;
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    default: break;
    } return v;
}
static void bgunInitAnimsWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bguninitanimscontext *c = opaque;
    switch (binding) {
    case GGV_INIT_HAND: c->hand = v.ref; break;
    case GGV_ANIM_GANGSTA: c->hand->gangstarot = v.f; break;
    case GGV_STATE_VALUE: c->hand->state = v.i; break;
    case GGV_ANIM_LOAD: c->hand->animload = v.i; break;
    case GGV_EMPTY_ANIM_MODE: c->hand->animmode = v.i; break;
    case GGV_INIT_GUN_ANIM: c->hand->gunmodel.anim = v.ref; break;
    case GGV_INIT_HAND_ANIM: c->hand->handmodel.anim = v.ref; break;
    default: break;
    }
}
static ggv_value bgunInitAnimsCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value v = {0};
    switch (service) {
    case GGV_INIT_PLAYER_BEGIN: wgV2RuntimePlayerBegin(a.ref); break;
    case GGV_INIT_HAND_REF: v.ref = &g_Vars.currentplayer->hands[a.i]; break;
    case GGV_INIT_ANIM: animInit(a.ref); break;
    case GGV_INIT_LOG:
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunInitHandAnims player=0 R(state=%d animmode=%d animload=%d) L(state=%d animmode=%d animload=%d)",
			(s32)g_Vars.currentplayer->hands[HAND_RIGHT].state,
			(s32)g_Vars.currentplayer->hands[HAND_RIGHT].animmode,
			(s32)g_Vars.currentplayer->hands[HAND_RIGHT].animload,
			(s32)g_Vars.currentplayer->hands[HAND_LEFT].state,
			(s32)g_Vars.currentplayer->hands[HAND_LEFT].animmode,
			(s32)g_Vars.currentplayer->hands[HAND_LEFT].animload);
        break;
    default: break;
    } return v;
}
void bgunInitHandAnims(void)
{
    struct bguninitanimscontext context = {0};
    ggv_host host = { &context, bgunInitAnimsRead, bgunInitAnimsWrite, bgunInitAnimsCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_INIT_HAND_ANIMS), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: hand animation initialization refused immutable program");
}

f32 bgunGetNoiseRadius(s32 handnum)
{
	return g_Vars.currentplayer->hands[handnum].noiseradius;
}

void bgunDecreaseNoiseRadius(void)
{
	struct player *player = g_Vars.currentplayer;
	f32 consideramount;
	struct gset gsetleft;
	struct gset gsetright;
	struct noisesettings noisesettingsleft;
	struct noisesettings noisesettingsright;
	f32 subamount;

	gsetPopulateFromCurrentPlayer(HAND_LEFT, &gsetleft);
	gsetPopulateFromCurrentPlayer(HAND_RIGHT, &gsetright);
	uint64_t left_scope = wgV2RuntimeScopeOpen(player, HAND_LEFT, &gsetleft);
	uint64_t right_scope = wgV2RuntimeScopeOpen(player, HAND_RIGHT, &gsetright);
	wg_v2_delivery left_receipt = {0}, right_receipt = {0};

	gsetGetNoiseSettings(&gsetleft, &noisesettingsleft);
	gsetGetNoiseSettings(&gsetright, &noisesettingsright);

	// Right hand
	if (bgunIsFiring(HAND_RIGHT) && (!wgV2RuntimeIsWeapon(gsetright.weaponnum)
			|| (right_scope && wgV2RuntimeNoiseBegin(player, HAND_RIGHT, &right_receipt)))) {
		const float before = player->hands[HAND_RIGHT].noiseradius;
		player->hands[HAND_RIGHT].noiseradius += noisesettingsright.incradius;

		if (player->hands[HAND_RIGHT].noiseradius > noisesettingsright.maxradius) {
			player->hands[HAND_RIGHT].noiseradius = noisesettingsright.maxradius;
		}
		if (wgV2RuntimeIsWeapon(gsetright.weaponnum))
			wgV2RuntimeObserveNoise(&right_receipt, before, player->hands[HAND_RIGHT].noiseradius,
				noisesettingsright.incradius, noisesettingsright.maxradius);
	}

	subamount = g_Vars.lvupdate60freal * noisesettingsright.incradius / (noisesettingsright.decbasespeed * 60.0f);
	consideramount = (player->hands[HAND_RIGHT].noiseradius - noisesettingsright.minradius) * g_Vars.lvupdate60freal / (noisesettingsright.decremspeed * 60.0f);

	if (consideramount > subamount) {
		subamount = consideramount;
	}

	player->hands[HAND_RIGHT].noiseradius -= subamount;

	if (player->hands[HAND_RIGHT].noiseradius < noisesettingsright.minradius) {
		player->hands[HAND_RIGHT].noiseradius = noisesettingsright.minradius;
	}

	// Left hand
	if (bgunIsFiring(HAND_LEFT) && (!wgV2RuntimeIsWeapon(gsetleft.weaponnum)
			|| (left_scope && wgV2RuntimeNoiseBegin(player, HAND_LEFT, &left_receipt)))) {
		const float before = player->hands[HAND_LEFT].noiseradius;
		player->hands[HAND_LEFT].noiseradius += noisesettingsleft.incradius;

		if (player->hands[HAND_LEFT].noiseradius > noisesettingsleft.maxradius) {
			player->hands[HAND_LEFT].noiseradius = noisesettingsleft.maxradius;
		}
		if (wgV2RuntimeIsWeapon(gsetleft.weaponnum))
			wgV2RuntimeObserveNoise(&left_receipt, before, player->hands[HAND_LEFT].noiseradius,
				noisesettingsleft.incradius, noisesettingsleft.maxradius);
	}

	subamount = g_Vars.lvupdate60freal * noisesettingsleft.incradius / (noisesettingsleft.decbasespeed * 60.0f);
	consideramount = (player->hands[HAND_LEFT].noiseradius - noisesettingsleft.minradius) * g_Vars.lvupdate60freal / (noisesettingsleft.decremspeed * 60.0f);

	if (consideramount > subamount) {
		subamount = consideramount;
	}

	player->hands[HAND_LEFT].noiseradius -= subamount;

	if (player->hands[HAND_LEFT].noiseradius < noisesettingsleft.minradius) {
		player->hands[HAND_LEFT].noiseradius = noisesettingsleft.minradius;
	}
	wgV2RuntimeScopeClose(left_scope); wgV2RuntimeScopeClose(right_scope);
	wgV2RuntimeNoiseComplete(&left_receipt); wgV2RuntimeNoiseComplete(&right_receipt);
}

void bgunCalculateBlend(s32 handnum)
{
	s32 sp60[2];
	s32 sp58[2];
	struct weapon *weapon = weaponFindById(bgunGetWeaponNum(handnum));
	f32 sway = weapon->sway;
	struct player *player = g_Vars.currentplayer;

	sp60[handnum] = (player->hands[handnum].curblendpos + 2) % 4;
	sp58[handnum] = (player->hands[handnum].curblendpos + 1) % 4;
	player->hands[handnum].curblendpos = sp58[handnum];

	player->hands[handnum].blendlook[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.08f * sway;
	player->hands[handnum].blendlook[sp60[handnum]].y = (RANDOMFRAC() - 0.5f) * 0.1f * sway;
	player->hands[handnum].blendlook[sp60[handnum]].z = -1;

	player->hands[handnum].blendup[sp60[handnum]].x = (RANDOMFRAC() - 0.5f) * 0.1f * sway;
	player->hands[handnum].blendup[sp60[handnum]].y = 1;
	player->hands[handnum].blendup[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 0.1f * sway;

	player->hands[handnum].blendpos[sp60[handnum]].x = (RANDOMFRAC() * 0.75f) + 1.5f;
	player->hands[handnum].blendpos[sp60[handnum]].y = (2 + RANDOMFRAC()) * player->hands[handnum].blendscale1;
	player->hands[handnum].blendpos[sp60[handnum]].z = (RANDOMFRAC() - 0.5f) * 2.5f;

	if (player->hands[handnum].sideflag < 0) {
		player->hands[handnum].blendpos[sp60[handnum]].x *= -1;

		if (player->hands[handnum].sideflag == -2) {
			player->hands[handnum].sideflag = 1;
		} else {
			player->hands[handnum].sideflag = -2;
		}
	} else {
		if (player->hands[handnum].sideflag == 2) {
			player->hands[handnum].sideflag = -1;
		} else {
			player->hands[handnum].sideflag = 2;
		}
	}

	player->hands[handnum].blendscale1 = -player->hands[handnum].blendscale1;
}

void bgunUpdateBlend(struct hand *hand, s32 handnum)
{
	u32 stack[3];
	s32 i;
	struct coord sp5c = {0, 0, 0};
	struct coord sp50 = {0, 0, -1};
	struct coord sp44 = {0, 1, 0};
	s32 pos = hand->curblendpos;
	struct player *player = g_Vars.currentplayer;

	func0f096b70(&hand->blendpos[(pos + 3) % 4], &hand->blendpos[pos], &hand->blendpos[(pos + 1) % 4], &hand->blendpos[(pos + 2) % 4], hand->dampt, &sp5c);
	func0f096b70(&hand->blendlook[(pos + 3) % 4], &hand->blendlook[pos], &hand->blendlook[(pos + 1) % 4], &hand->blendlook[(pos + 2) % 4], hand->dampt, &sp50);
	func0f096b70(&hand->blendup[(pos + 3) % 4], &hand->blendup[pos], &hand->blendup[(pos + 1) % 4], &hand->blendup[(pos + 2) % 4], hand->dampt, &sp44);

	sp5c.x *= player->gunposamplitude;
	sp5c.y *= player->gunposamplitude;
	sp5c.z *= player->gunposamplitude;

	sp5c.x += hand->adjustdamp.x;
	sp5c.y += hand->adjustdamp.y;

	sp5c.x += handGetXShift(handnum);

	for (i = 0; i < g_Vars.lvupdate240; i++) {
		hand->damppossum.x = (PAL ? 0.9847f : 0.9872f) * hand->damppossum.x + sp5c.f[0];
		hand->damppossum.y = (PAL ? 0.9847f : 0.9872f) * hand->damppossum.y + sp5c.f[1];
		hand->damppossum.z = (PAL ? 0.9847f : 0.9872f) * hand->damppossum.z + sp5c.f[2];

		hand->damplooksum.x = (PAL ? 0.9847f : 0.9872f) * hand->damplooksum.x + sp50.f[0];
		hand->damplooksum.y = (PAL ? 0.9847f : 0.9872f) * hand->damplooksum.y + sp50.f[1];
		hand->damplooksum.z = (PAL ? 0.9847f : 0.9872f) * hand->damplooksum.z + sp50.f[2];

		hand->dampupsum.x = (PAL ? 0.9847f : 0.9872f) * hand->dampupsum.x + sp44.f[0];
		hand->dampupsum.y = (PAL ? 0.9847f : 0.9872f) * hand->dampupsum.y + sp44.f[1];
		hand->dampupsum.z = (PAL ? 0.9847f : 0.9872f) * hand->dampupsum.z + sp44.f[2];
	}

	hand->damppos.x = hand->damppossum.x * (PAL ? 0.01529997587204f : 0.012799978f) * 2;
	hand->damppos.y = hand->damppossum.y * (PAL ? 0.01529997587204f : 0.012799978f) * 2;
	hand->damppos.z = hand->damppossum.z * (PAL ? 0.01529997587204f : 0.012799978f) * 2;

	hand->damplook.x = hand->damplooksum.x * (PAL ? 0.01529997587204f : 0.012799978f);
	hand->damplook.y = hand->damplooksum.y * (PAL ? 0.01529997587204f : 0.012799978f);
	hand->damplook.z = hand->damplooksum.z * (PAL ? 0.01529997587204f : 0.012799978f);

	hand->dampup.x = hand->dampupsum.x * (PAL ? 0.01529997587204f : 0.012799978f);
	hand->dampup.y = hand->dampupsum.y * (PAL ? 0.01529997587204f : 0.012799978f);
	hand->dampup.z = hand->dampupsum.z * (PAL ? 0.01529997587204f : 0.012799978f);
}

u32 var80070158 = 0x04e50764;
u32 var8007015c = 0x05360529;
u32 var80070160 = 0x0531052a;
u32 var80070164 = 0x052b052c;
u32 var80070168 = 0x052c052d;
u32 var8007016c = 0x052b052b;
u32 var80070170 = 0x052e052f;
u32 var80070174 = 0x052f0530;
u32 var80070178 = 0x05310532;
u32 var8007017c = 0x05320533;
u32 var80070180 = 0x05340535;
u32 var80070184 = 0x05360537;
u32 var80070188 = 0x05380530;
u32 var8007018c = 0x0539053a;
u32 var80070190 = 0x0532053b;
u32 var80070194 = 0x05310766;
u32 var80070198 = 0x07670768;
u32 var8007019c = 0x0769076a;
u32 var800701a0 = 0x076b076c;
u32 var800701a4 = 0x076d0000;
u32 var800701a8 = 0x0000ffff;

void bgun0f09d8dc(f32 breathing, f32 arg1, f32 arg2, f32 arg3, f32 arg4)
{
	f32 dampt[2];
	struct player *player = g_Vars.currentplayer;
	u32 stack;
	s32 i;
	f32 sp50 = arg2;
	f32 sp4c;
	u32 stack2;

	if (sp50 < 0.0f) {
		sp50 = -sp50;
	}

	if (arg1 > 0.8f) {
		player->gunposamplitude = 1.0f;
	} else {
		if (arg1 > 0.1f) {
			f32 tmp = 1.0f - cosf((arg1 - 0.1f) * M_BADTAU / 2.8f);
			player->gunposamplitude = 0.8f * tmp + 0.2f;
		} else {
			player->gunposamplitude = 0.1f;
		}
	}

	if (bmoveGetCrouchPos() != CROUCHPOS_SQUAT) {
		if (player->gunposamplitude < 0.3f * g_Vars.currentplayer->bondbreathing) {
			player->gunposamplitude = 0.3f * g_Vars.currentplayer->bondbreathing;
		}
	}

	if (player->gunposamplitude < 0.5f * sp50) {
		player->gunposamplitude = 0.5f * sp50;
	}

	for (i = 0; i < g_Vars.lvupdate240; i++) {
		player->gunampsum = (PAL ? 0.9847f : 0.9872f) * player->gunampsum + player->gunposamplitude;
	}

	player->gunposamplitude = (PAL ? 0.01529997587204f : 0.012799978256226f) * player->gunampsum;

	if (breathing < 0.016666667535901f * sp50) {
		breathing = 0.016666667535901f * sp50;
	}

	for (i = 0; i < g_Vars.lvupdate240; i++) {
		player->cyclesum = (PAL ? 0.9847f : 0.9872f) * player->cyclesum + breathing;
	}

	breathing = player->cyclesum * (PAL ? 0.01529997587204f : 0.012799978256226f);
	sp4c = breathing * g_Vars.lvupdate60freal;
	dampt[0] = player->hands[0].dampt + sp4c;

	while (dampt[0] >= 1.0f) {
		bgunCalculateBlend(HAND_RIGHT);
		dampt[0] -= 1.0f;
		player->syncoffset++;
	}

	player->synccount += g_Vars.lvupdate60freal;

	if (player->synccount > 60.0f) {
		player->synccount = 0.0f;
		player->syncchange = (RANDOMFRAC() - 0.5f) * 0.2f / 60.0f;
	}

	if (player->syncchange + sp4c > 0.0f) {
		player->gunsync += player->syncchange;
	}

	if (player->gunsync > 0.5f) {
		player->gunsync = 0.5f;
	} else if (player->gunsync < -0.5f) {
		player->gunsync = -0.5f;
	} else if (player->gunsync < 0.1f && player->gunsync > -0.1f) {
		if (player->gunsync > 0.0f) {
			player->gunsync = -0.1f;
		} else {
			player->gunsync = 0.1f;
		}
	}

	dampt[1] = dampt[0] + player->syncoffset + player->gunsync;

	while (dampt[1] >= 1.0f) {
		bgunCalculateBlend(HAND_LEFT);
		dampt[1] -= 1.0f;
		player->syncoffset--;
	}

	for (i = 0; i < 2; i++) {
		player->hands[i].dampt = dampt[i];
		player->hands[i].adjustdamp.x = -1.75f * arg3 + -0.8f * arg4;
		player->hands[i].adjustdamp.y = -2.0f * arg2;
	}
}

bool bgunIsLoaded(void)
{
    ggv_host host = { NULL, bgunIsLoadedRead, bgunHandPredicateWrite }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_IS_LOADED), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: loaded predicate refused immutable program"); return false;
    }
    return result;
}

u32 bgunGetGunMemType(void)
{
	return g_Vars.currentplayer->gunctrl.gunmemtype;
}

struct modeldef *bgunGetGunModeldef(void)
{
	return g_Vars.currentplayer->gunctrl.gunmodeldef;
}

u8 *bgunGetGunMem(void)
{
	return g_Vars.currentplayer->gunctrl.gunmem;
}

u32 bgunCalculateGunMemCapacity(void)
{
	if (PLAYERCOUNT() == 1) {
		struct stagetableentry *stage = stageGetCurrent();

		/* B-1032: title/boot/credits are system stages and intentionally have
		 * no stage-table row. ImGui character preview can acquire INVMENU gun
		 * memory while a network room is rendered on STAGE_TITLE, so capacity
		 * calculation must use the safe base allocation when no gameplay stage
		 * owns an extragunmem adjustment. */
		if (stage) {
			return g_BgunGunMemBaseSizeDefault + stage->extragunmem;
		}
	}

	return g_BgunGunMemBaseSizeDefault;
}

struct bgunmemorycontext { struct player *player; s32 weaponnum; };
static ggv_value bgunMemoryRead(void *opaque, ggv_binding binding)
{
    struct bgunmemorycontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    case GGV_MEM_CURRENT_OWNER: v.i = g_Vars.currentplayer->gunctrl.gunmemowner; break;
    case GGV_LOADED_GUNMEMOWNER: v.i = c->player->gunctrl.gunmemowner; break;
    case GGV_EQUIP_REQUEST: v.i = c->weaponnum; break;
    case GGV_MEM_OWNER_FREE: v.i = GUNMEMOWNER_FREE; break;
    case GGV_MEM_OWNER_BOND: v.i = GUNMEMOWNER_BONDGUN; break;
    case GGV_MEM_MASTER_FLUX: v.i = MASTERLOADSTATE_FLUX; break;
    case GGV_MEM_GUN_FLUX: v.i = GUNLOADSTATE_FLUX; break;
    case GGV_RELOAD_LVUPDATE240: v.i = g_Vars.lvupdate240; break;
    default: break;
    } return v;
}
static void bgunMemoryWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmemorycontext *c = opaque;
    switch (binding) {
    case GGV_MEM_CURRENT_OWNER: g_Vars.currentplayer->gunctrl.gunmemowner = v.i; break;
    case GGV_LOADED_MASTERLOADSTATE: c->player->gunctrl.masterloadstate = v.i; break;
    case GGV_MEM_GUNLOADSTATE: c->player->gunctrl.gunloadstate = v.i; break;
    case GGV_LOADED_GUNMEMNEW: c->player->gunctrl.gunmemnew = v.i; break;
    case GGV_MEM_LOCK_TIMER: c->player->gunctrl.gunlocktimer = v.i; break;
    default: break;
    }
}
static ggv_value bgunMemoryCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmemorycontext *c = opaque; ggv_value v = {0};
    struct player *player = c->player; s32 weaponnum = c->weaponnum;
    switch (service) {
    case GGV_MEM_FREE_LOG:
        sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunFreeGunMem enter player=0 owner_was=%d gunmemtype=%d gunmemnew=%d masterload=%d",
			(s32)g_Vars.currentplayer->gunctrl.gunmemowner,
			(s32)g_Vars.currentplayer->gunctrl.gunmemtype,
			(s32)g_Vars.currentplayer->gunctrl.gunmemnew,
			(s32)g_Vars.currentplayer->gunctrl.masterloadstate);
        break;
    case GGV_MEM_SET_LOG_ENTER:
        sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunSetGunMemWeapon enter player=0 wpn=%d owner=%d gunmemnew_was=%d gunmemtype=%d masterload_was=%d gunloadstate_was=%d",
			(s32)weaponnum,
			(s32)player->gunctrl.gunmemowner,
			(s32)player->gunctrl.gunmemnew,
			(s32)player->gunctrl.gunmemtype,
			(s32)player->gunctrl.masterloadstate,
			(s32)player->gunctrl.gunloadstate);
        break;
    case GGV_MEM_SET_LOG_EXIT:
        sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunSetGunMemWeapon exit  player=0 wpn=%d owner=%d gunmemnew=%d masterload=%d gunloadstate=%d gunlocktimer=%d",
			(s32)weaponnum,
			(s32)player->gunctrl.gunmemowner,
			(s32)player->gunctrl.gunmemnew,
			(s32)player->gunctrl.masterloadstate,
			(s32)player->gunctrl.gunloadstate,
			(s32)player->gunctrl.gunlocktimer);
        break;
    case GGV_MEM_TEXTURE_RESET:
        /* Gun memory and its textures are stale. The renderer exposes no
         * address-range eviction, so preserve the full cache reset. */
        videoResetTextureCache(); break;
    case GGV_MEM_MASTER_TICK: bgunTickMasterLoad(); break;
    default: break;
    } return v;
}
static void bgunRunMemory(struct bgunmemorycontext *context, ggv_stock_program program)
{
    ggv_host host = { context, bgunMemoryRead, bgunMemoryWrite, bgunMemoryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(program), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: gun memory control refused immutable program");
}
void bgunFreeGunMem(void)
{
    struct bgunmemorycontext context = {0};
    bgunRunMemory(&context, GGV_FREE_GUN_MEMORY);
}

void bgunSetGunMemWeapon(s32 weaponnum)
{
    struct bgunmemorycontext context = { g_Vars.currentplayer, weaponnum };
    bgunRunMemory(&context, GGV_SET_GUN_MEMORY_WEAPON);
}

struct bgunfluxcontext { s32 casingindex; };
static ggv_value bgunFluxRead(void *opaque, ggv_binding binding)
{
    ggv_value v = {0};
    switch (binding) {
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    case GGV_MEM_MASTER_FLUX: v.i = MASTERLOADSTATE_FLUX; break;
    case GGV_MEM_GUN_FLUX: v.i = GUNLOADSTATE_FLUX; break;
    case GGV_FLUX_CASING_COUNT: v.i = ARRAYCOUNT(g_Casings); break;
    default: break;
    } return v;
}
static void bgunFluxWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunfluxcontext *c = opaque;
    switch (binding) {
    case GGV_FLUX_FILE: g_Vars.currentplayer->gunctrl.handfilenum = v.i; break;
    case GGV_FLUX_CATALOG_HEAD: g_Vars.currentplayer->gunctrl.handcatalogid[0] = v.i; break;
    case GGV_FLUX_GENERATION: g_Vars.currentplayer->gunctrl.handcataloggeneration = v.u; break;
    case GGV_FLUX_HAND_MODEL: g_Vars.currentplayer->gunctrl.handmodeldef = v.ref; break;
    case GGV_FLUX_LOAD_PTR: g_Vars.currentplayer->gunctrl.handmemloadptr = v.ref; break;
    case GGV_FLUX_LOAD_REMAINING: g_Vars.currentplayer->gunctrl.handmemloadremaining = v.i; break;
    case GGV_LOADED_MASTERLOADSTATE: g_Vars.currentplayer->gunctrl.masterloadstate = v.i; break;
    case GGV_MEM_GUNLOADSTATE: g_Vars.currentplayer->gunctrl.gunloadstate = v.i; break;
    case GGV_FLUX_CASING_INDEX: c->casingindex = v.i; break;
    case GGV_FLUX_CASING_MODEL: g_Casings[c->casingindex].modeldef = v.ref; break;
    case GGV_FLUX_CASINGS_ACTIVE: g_CasingsActive = v.b; break;
    default: break;
    }
}
static ggv_value bgunFluxCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value v = {0};
    switch (service) {
    case GGV_FLUX_LOG:
        sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunEnterFlux enter player=0 handfilenum_was=%d handmodeldef_was=%p gunmodeldef_was=%p masterload_was=%d owner=%d",
			(s32)g_Vars.currentplayer->gunctrl.handfilenum,
			(void *)g_Vars.currentplayer->gunctrl.handmodeldef,
			(void *)g_Vars.currentplayer->gunctrl.gunmodeldef,
			(s32)g_Vars.currentplayer->gunctrl.masterloadstate,
			(s32)g_Vars.currentplayer->gunctrl.gunmemowner);
        break;
    case GGV_FLUX_CLEAR_HANDLE:
        /* One typed aggregate store, with no provider lookup or loading. */
        g_Vars.currentplayer->gunctrl.handhandle = (asset_data_handle_t){0}; break;
    default: break;
    } return v;
}
void bgunEnterFlux(void)
{
    struct bgunfluxcontext context = {0};
    ggv_host host = { &context, bgunFluxRead, bgunFluxWrite, bgunFluxCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ENTER_FLUX), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: flux reset refused immutable program");
}

struct bgunmemoryownercontext { struct player *player; s32 newowner, wild; };
/* Same process-lifetime diagnostic throttles as the native donor. */
static s32 g_BgunMemoryLastPair = -1;
static s32 g_BgunMemoryLastWild = 0;
static s32 g_BgunMemoryLastOwner = -2;
static ggv_value bgunMemoryOwnerRead(void *opaque, ggv_binding binding)
{
    struct bgunmemoryownercontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    case GGV_LOADED_GUNMEMOWNER: v.i = c->player->gunctrl.gunmemowner; break;
    case GGV_LOADED_GUNMEMNEW: v.i = c->player->gunctrl.gunmemnew; break;
    case GGV_LOADED_GUNMEMTYPE: v.i = c->player->gunctrl.gunmemtype; break;
    case GGV_MEM_LOCK_TIMER: v.i = c->player->gunctrl.gunlocktimer; break;
    case GGV_MEM_REQUEST_OWNER: v.i = c->newowner; break;
    case GGV_MEM_LAST_PAIR: v.i = g_BgunMemoryLastPair; break;
    case GGV_MEM_LAST_WILD: v.i = g_BgunMemoryLastWild; break;
    case GGV_MEM_LAST_OWNER: v.i = g_BgunMemoryLastOwner; break;
    case GGV_CLEAN_MULTIPLAYER: v.u = g_Vars.mplayerisrunning; break;
    case GGV_MEM_HAS_BODY: v.i = c->player->haschrbody; break;
    case GGV_MEM_REMOVE_BODY_FLAG: v.i = var8009dfc0; break;
    default: break;
    } return v;
}
static void bgunMemoryOwnerWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmemoryownercontext *c = opaque;
    switch (binding) {
    case GGV_LOADED_GUNMEMOWNER: c->player->gunctrl.gunmemowner = v.i; break;
    case GGV_LOADED_GUNMEMNEW: c->player->gunctrl.gunmemnew = v.i; break;
    case GGV_LOADED_GUNMEMTYPE: c->player->gunctrl.gunmemtype = v.i; break;
    case GGV_MEM_LOCK_TIMER: c->player->gunctrl.gunlocktimer = v.i; break;
    case GGV_MEM_LOAD_ALL: c->player->gunctrl.loadall = v.b; break;
    case GGV_MEM_LAST_PAIR: g_BgunMemoryLastPair = v.i; break;
    case GGV_MEM_LAST_WILD: g_BgunMemoryLastWild = v.i; break;
    case GGV_MEM_LAST_OWNER: g_BgunMemoryLastOwner = v.i; break;
    case GGV_MEM_WILD_VALUE: c->wild = v.i; break;
    default: break;
    }
}
static ggv_value bgunMemoryOwnerCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmemoryownercontext *c = opaque; ggv_value v = {0};
    struct player *player = c->player; s32 newowner = c->newowner, gmn = c->wild;
    switch (service) {
    case GGV_MEM_OWNER_LOG_ENTER:
        sysLogPrintf(LOG_NOTE,
				"LOG.WPN.DIAG: bgunChangeGunMem enter player=0 cur_owner=%d new_owner=%d gunlocktimer=%d gunmemnew=%d gunmemtype=%d haschrbody=%d mp=%d",
				(s32)player->gunctrl.gunmemowner,
				(s32)newowner,
				(s32)player->gunctrl.gunlocktimer,
				(s32)player->gunctrl.gunmemnew,
				(s32)player->gunctrl.gunmemtype,
				(s32)player->haschrbody,
				(s32)g_Vars.mplayerisrunning);
        break;
    case GGV_MEM_OWNER_LOG_WILD:
        sysLogPrintf(LOG_NOTE,
					"LOG.WPN.DIAG: gunmemnew RANGE-WATCH player=0 wild_value=%d (0x%08x) site=bgunChangeGunMem cur_owner=%d new_owner=%d gunmemtype=%d gunmem=%p",
					gmn, (u32)gmn,
					(s32)player->gunctrl.gunmemowner, (s32)newowner,
					(s32)player->gunctrl.gunmemtype,
					(void *)player->gunctrl.gunmem);
        break;
    case GGV_MEM_OWNER_LOG_SUCCESS:
        sysLogPrintf(LOG_NOTE,
						"LOG.WPN.DIAG: bgunChangeGunMem exit  player=0 result=1 owner_now=%d gunlocktimer=%d branch=timer_complete",
						(s32)player->gunctrl.gunmemowner,
						(s32)player->gunctrl.gunlocktimer);
        break;
    case GGV_MEM_ENTER_FLUX: bgunEnterFlux(); break;
    case GGV_MEM_REMOVE_BODY: playerRemoveChrBody(); break;
    default: break;
    } return v;
}
bool bgunChangeGunMem(s32 newowner)
{
    struct bgunmemoryownercontext context = { g_Vars.currentplayer, newowner, 0 };
    ggv_host host = { &context, bgunMemoryOwnerRead, bgunMemoryOwnerWrite, bgunMemoryOwnerCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_CHANGE_MEMORY_OWNER), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: memory ownership transition refused immutable program"); return false;
    }
    return result;
}

static asset_data_handle_t bgunNullAssetHandle(void)
{
	asset_data_handle_t handle = ASSET_HANDLE_NULL_INIT;
	return handle;
}

static asset_data_handle_t bgunResolveQueuedModelHandle(s32 filenum)
{
	asset_data_handle_t null_handle = bgunNullAssetHandle();
	asset_data_handle_t handle;

	if (filenum <= 0) {
		return null_handle;
	}

	handle = catalogHandleByModelSourceFilenum(ASSET_NONE, filenum);

	if (!assetHandleIsNull(handle)) {
		return handle;
	}

	if (g_Vars.currentplayernum == 0) {
		static s32 s_last_missing_filenum = -1;

		if (s_last_missing_filenum != filenum) {
			s_last_missing_filenum = filenum;
			sysLogPrintf(LOG_WARNING,
				"CATALOG.MISS: bgun model filenum=%d has no catalog/provider handle",
				filenum);
		}
	}
	/* c3849 Wave 1: outside the log throttle so counts stay true. */
	assetFallbackRecord(ASSET_MODEL, filenum,
		"bgun model has no catalog/provider handle");

	return null_handle;
}

static void bgunQueueModelLoad(struct player *player, u16 filenum, struct modeldef **modeldef,
		u8 **memptr, u32 *memremaining)
{
	player->gunctrl.loadretainedmodel = NULL;
	player->gunctrl.loadfilenum = filenum;
	player->gunctrl.loadhandle = bgunResolveQueuedModelHandle((s32)filenum);
	const char *id = catalogIdBySourceHandle(ASSET_MODEL, player->gunctrl.loadhandle);
	snprintf(player->gunctrl.loadcatalogid, sizeof(player->gunctrl.loadcatalogid), "%s", id ? id : "");
	player->gunctrl.loadbodyhand = false;
	player->gunctrl.loadcataloggeneration = assetCatalogGetGeneration();
	player->gunctrl.loadtomodeldef = modeldef;
	player->gunctrl.loadmemptr = memptr;
	player->gunctrl.loadmemremaining = memremaining;
	player->gunctrl.fileinfo.loadedsize = 0;
	player->gunctrl.fileinfo.allocsize = 0;
}

bool bgunQueueRetainedModelLoad(struct player *player, struct modeldef *source,
        struct modeldef **destination)
{
    catalog_model_generation_t *generation = catalogModelGenerationForModeldef(source);
    const char *id = catalogModelGenerationId(generation);
    if (!player || !destination || !generation || !id || !id[0]
            || strlen(id) >= sizeof(player->gunctrl.loadcatalogid)
            || !catalogModelGenerationPinStage(generation)) return false;
    /* All preflight precedes publication; numeric file slots and mutable
     * provider handles cannot substitute a newer same-ID source here. */
    player->gunctrl.loadretainedmodel = source;
    player->gunctrl.loadfilenum = 0;
    player->gunctrl.loadhandle = bgunNullAssetHandle();
    strcpy(player->gunctrl.loadcatalogid, id);
    player->gunctrl.loadbodyhand = false;
    player->gunctrl.loadcataloggeneration = assetCatalogGetGeneration();
    player->gunctrl.loadtomodeldef = destination;
    player->gunctrl.loadmemptr = NULL;
    player->gunctrl.loadmemremaining = NULL;
    player->gunctrl.fileinfo.loadedsize = player->gunctrl.fileinfo.allocsize = 0;
    player->gunctrl.gunloadstate = GUNLOADSTATE_MODEL;
    return true;
}

static bool bgunSameSourceHandle(asset_data_handle_t a, asset_data_handle_t b)
{
	return a.provider == b.provider && a.opaque[0] == b.opaque[0] && a.opaque[1] == b.opaque[1];
}

bool bgunBodyHandSourceIsCurrent(struct player *player, s32 bodynum)
{
	char id[64];
	asset_data_handle_t handle;
	return player && catalogGetBodyHandSourceChecked(bodynum, id, &handle) &&
		!strcmp(id, player->gunctrl.handcatalogid) &&
		bgunSameSourceHandle(handle, player->gunctrl.handhandle) &&
		player->gunctrl.handcataloggeneration == assetCatalogGetGeneration();
}

bool bgunQueueBodyHandModelLoad(struct player *player, s32 bodynum,
		struct modeldef **modeldef, u8 **memptr, u32 *memremaining)
{
	char id[64];
	asset_data_handle_t handle;
    s32 filenum = 0;
	if (!player || !modeldef) return false;
	*modeldef = NULL;
	player->gunctrl.loadretainedmodel = NULL;
	player->gunctrl.loadcatalogid[0] = 0;
	player->gunctrl.loadhandle = bgunNullAssetHandle();
	player->gunctrl.loadfilenum = 0;
	player->gunctrl.loadbodyhand = false;
	player->gunctrl.loadtomodeldef = modeldef;
	if (!catalogGetBodyHandSourceChecked(bodynum, id, &handle) || !id[0] ||
			!catalogGetBodyHandFilenumChecked(bodynum, &filenum)) {
		player->gunctrl.gunloadstate = GUNLOADSTATE_FAILED;
		player->gunctrl.loadcataloggeneration = assetCatalogGetGeneration();
		return false;
	}
	player->gunctrl.loadfilenum = filenum;
	player->gunctrl.loadhandle = handle;
	strcpy(player->gunctrl.loadcatalogid, id);
	player->gunctrl.loadbodyhand = true;
	player->gunctrl.loadcataloggeneration = assetCatalogGetGeneration();
	player->gunctrl.loadtomodeldef = modeldef;
	player->gunctrl.loadmemptr = memptr;
	player->gunctrl.loadmemremaining = memremaining;
	player->gunctrl.fileinfo.loadedsize = 0;
	player->gunctrl.fileinfo.allocsize = 0;
	player->gunctrl.gunloadstate = GUNLOADSTATE_MODEL;
	return true;
}

static bool bgunQueuedLoadCanUseHandle(struct player *player)
{
	if (assetHandleIsNull(player->gunctrl.loadhandle)) return false;
	if (player->gunctrl.loadcatalogid[0]) {
		catalog_model_result_t resolved;
		return catalogResolveModel(player->gunctrl.loadcatalogid, &resolved) &&
			resolved.entry->source_filenum == player->gunctrl.loadfilenum &&
			player->gunctrl.loadcataloggeneration == assetCatalogGetGeneration() &&
			bgunSameSourceHandle(resolved.handle, player->gunctrl.loadhandle);
	}
	return true;
}

static bool bgunQueuedLoadPassesSourceOnlyCheck(struct player *player, const char *context)
{
	const char *model_id;

	if (!bgunQueuedLoadCanUseHandle(player)) {
		return false;
	}

	model_id = player->gunctrl.loadcatalogid;
	assetSourceDebugFatalHandleFallback(ASSET_MODEL, context, model_id,
		player->gunctrl.loadhandle);

	return !assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL,
		player->gunctrl.loadhandle);
}

static const char *bgunQueuedModelSourcePath(struct player *player)
{
	if (!bgunQueuedLoadCanUseHandle(player)) {
		return NULL;
	}

	return fileProviderPath(player->gunctrl.loadhandle);
}

static bool bgunQueuedLoadUsesExternalModelSource(struct player *player)
{
	char resolved_source_path[FS_MAXPATH + 1];

	return modelSourceResolvePath(bgunQueuedModelSourcePath(player),
		resolved_source_path, sizeof(resolved_source_path), NULL, 0);
}

static struct modeldef *bgunQueuedLoadCatalogModelSource(struct player *player)
{
	const char *source_path = bgunQueuedModelSourcePath(player);
	char resolved_source_path[FS_MAXPATH + 1];
	const char *model_id;
	struct modeldef *modeldef;

	if (!modelSourceResolvePath(source_path, resolved_source_path,
			sizeof(resolved_source_path), NULL, 0)) {
		return NULL;
	}

	model_id = player->gunctrl.loadcatalogid;

	if (!model_id[0]) {
		sysLogPrintf(LOG_WARNING,
			"BONDGUN.SOURCE: external model source filenum=%d path=%s has no ASSET_MODEL catalog id",
			(s32)player->gunctrl.loadfilenum,
			resolved_source_path[0] ? resolved_source_path : (source_path ? source_path : "(null)"));
		return NULL;
	}

	if (!catalogLoadStageAsset(ASSET_MODEL, model_id)) {
		sysLogPrintf(LOG_WARNING,
			"BONDGUN.SOURCE: catalog model source load failed filenum=%d id=%s path=%s",
			(s32)player->gunctrl.loadfilenum,
			model_id,
			resolved_source_path[0] ? resolved_source_path : (source_path ? source_path : "(null)"));
		return NULL;
	}

	modeldef = catalogGetLoadedModeldef(model_id);

	if (modeldef == NULL) {
		sysLogPrintf(LOG_WARNING,
			"BONDGUN.SOURCE: catalog model source produced NULL modeldef filenum=%d id=%s path=%s",
			(s32)player->gunctrl.loadfilenum,
			model_id,
			resolved_source_path[0] ? resolved_source_path : (source_path ? source_path : "(null)"));
		return NULL;
	}

	/* The catalog owns its current row, not every native pointer already
	 * handed to gun/hand/casing models and cached command lists. Keep this
	 * exact compiled source (including textures) until stage teardown even
	 * if a catalog replacement releases the row during play. */
	if (!catalogModelGenerationPinStage(catalogModelGenerationForModeldef(modeldef))) {
		sysLogPrintf(LOG_ERROR, "BONDGUN.SOURCE: model generation could not be retained id=%s", model_id);
		return NULL;
	}
	*player->gunctrl.loadtomodeldef = modeldef;
	player->gunctrl.fileinfo.loadedsize = 0;
	player->gunctrl.fileinfo.allocsize = 0;
	player->gunctrl.nexttexturetoload = 0;
	modelAllocateRwData(modeldef);

	sysLogPrintf(LOG_NOTE,
		"BONDGUN.SOURCE: loaded catalog model source filenum=%d id=%s path=%s",
		(s32)player->gunctrl.loadfilenum,
		model_id,
		resolved_source_path[0] ? resolved_source_path : (source_path ? source_path : "(null)"));

	return modeldef;
}

static void bgunFailQueuedModelLoad(struct player *player)
{
	/* One request gets one attempt.  A permanent source/conversion failure must
	 * not bounce MODEL -> FLUX and reactivate the same catalog dependency every
	 * frame.  The FAILED state remains fail-closed until the caller explicitly
	 * queues a new request or the catalog generation changes. */
	sysLogPrintf(LOG_ERROR,
		"CATALOG_CRITICAL: bgun model filenum=%d failed to load catalog_generation=%u",
		(s32)player->gunctrl.loadfilenum,
		(unsigned)player->gunctrl.loadcataloggeneration);
	assetFallbackRecord(ASSET_MODEL, (s32)player->gunctrl.loadfilenum,
		"bgun model load failed (request latched)");

	if (player->gunctrl.loadtomodeldef) {
		*player->gunctrl.loadtomodeldef = NULL;
	}
	player->gunctrl.loadretainedmodel = NULL;
	player->gunctrl.gunloadstate = GUNLOADSTATE_FAILED;
}

/*
 * Strict-catalog load: no ROM-filenum fallback.
 *
 * The catalog is the sole pipeline for asset resolution. If a weapon's
 * model file is missing from the catalog the load MUST fail loudly --
 * the CATALOG_CRITICAL path at the call site is the loud surface, the
 * CATALOG.MISS warning at bgunResolveQueuedModelHandle is the throttled
 * cause-of-load-failure signal. A defensive ROM fallback would silently
 * recover from registration gaps and remove the pressure to actually
 * close them; that is exactly the no-half-measures violation that the
 * S484-followup-2 revert closed (Mike's directive: "the correct solution
 * is not to fallback to legacy, but to strengthen our initial cataloging
 * to be full, correct, and complete").
 *
 * The proper closure of the original Farsight=907 regression lives in
 * port/src/assetcatalog_base_extended.c::assetCatalogRegisterWeaponModelFiles
 * and the cross-cutting catalog-coverage audit
 * (context/audits/catalog-coverage-audit-2026-05-01.md). Any gap that
 * surfaces here is a bug to register, not a bug to route around.
 */
static s32 bgunQueuedGetInflatedSize(struct player *player)
{
	if (bgunQueuedLoadPassesSourceOnlyCheck(player, "weapon model inflated-size")) {
		return assetLoadGetInflatedSize(player->gunctrl.loadhandle, LOADTYPE_MODEL);
	}
	return 0;
}

static s32 bgunQueuedGetLoadedSize(struct player *player)
{
	if (bgunQueuedLoadPassesSourceOnlyCheck(player, "weapon model loaded-size")) {
		return assetLoadGetLoadedSize(player->gunctrl.loadhandle);
	}
	return 0;
}

static struct modeldef *bgunQueuedLoadToAddr(struct player *player, void *ptr, u32 loadsize)
{
	if (bgunQueuedLoadPassesSourceOnlyCheck(player, "weapon model load-to-addr")) {
		return assetLoadToAddr(player->gunctrl.loadhandle, FILELOADMETHOD_EXTRAMEM, ptr, loadsize);
	}
	return NULL;
}

/**
 * This function loads resources for a gun change.
 *
 * The caller sets properties in the player's gunctrl struct which tell this
 * function what to load, where the gunmem is that it can use, and how much
 * gunmem there is. This function is then called a couple of times on subsequent
 * ticks, loading data incrementally to avoid a significant lag spike.
 *
 * The function keeps track of its progress in the gunloadstate property, and
 * updates the gunmem properties to reflect its usage.
 *
 * The first call loads the model definition from the ROM and decompresses it.
 * The second call loads and decompress to 3 textures.
 * This continues on further calls until all textures are loaded.
 * The final call does some one-off processing on the model's display lists.
 *
 * Although the name contains "Gun", it's used for more than just the gun model.
 * It's used for the hand model, the gun model and the cartridge model.
 */
static ggv_value bgunFailureRead(void *opaque, ggv_binding binding)
{
    struct player *player = opaque; ggv_value v = {0};
    if (binding == GGV_MASTER_LOAD_GENERATION) v.u = player->gunctrl.loadcataloggeneration;
    return v;
}
static void bgunFailureWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct player *player = opaque;
    if (binding == GGV_MEM_GUNLOADSTATE) player->gunctrl.gunloadstate = v.i;
}
static ggv_value bgunFailureCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct player *player = opaque; ggv_value v = {0};
    switch (service) {
    case GGV_MASTER_CATALOG_GENERATION: v.u = assetCatalogGetGeneration(); break;
    case GGV_GUN_FAILURE_LOG:
        sysLogPrintf(LOG_NOTE,
            "BONDGUN.SOURCE: releasing failed model request filenum=%d catalog_generation=%u->%u",
            (s32)player->gunctrl.loadfilenum, (unsigned)player->gunctrl.loadcataloggeneration, (unsigned)a.u); break;
    default: break;
    } return v;
}
static void bgunGunLoadFailed(struct player *player)
{
    ggv_host host = { player, bgunFailureRead, bgunFailureWrite, bgunFailureCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_GUN_FAILURE_RECOVERY), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: gun failure recovery refused immutable program");
}

static ggv_value bgunRetainedRead(void *opaque, ggv_binding binding)
{
    struct player *player = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_GL_RETAINED_MODEL: v.ref = player->gunctrl.loadretainedmodel; break;
    case GGV_GL_TARGET: v.ref = player->gunctrl.loadtomodeldef; break;
    default: break;
    } return v;
}
static void bgunRetainedWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct player *player = opaque;
    switch (binding) {
    case GGV_GL_TARGET_MODEL: *player->gunctrl.loadtomodeldef = v.ref; break;
    case GGV_GL_RETAINED_MODEL: player->gunctrl.loadretainedmodel = v.ref; break;
    case GGV_GL_NEXT_TEXTURE: player->gunctrl.nexttexturetoload = v.u; break;
    case GGV_GL_LOADED_SIZE: player->gunctrl.fileinfo.loadedsize = v.u; break;
    case GGV_GL_ALLOC_SIZE: player->gunctrl.fileinfo.allocsize = v.u; break;
    case GGV_MEM_GUNLOADSTATE: player->gunctrl.gunloadstate = v.i; break;
    default: break;
    }
}
static ggv_value bgunRetainedCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct player *player = opaque; ggv_value v = {0};
    switch (service) {
    case GGV_GL_GENERATION: v.ref = catalogModelGenerationForModeldef(a.ref); break;
    case GGV_GL_GENERATION_ID: v.ref = (void *)catalogModelGenerationId(a.ref); break;
    case GGV_GL_COMPARE_ID: v.i = strcmp(a.ref, player->gunctrl.loadcatalogid); break;
    case GGV_GL_FAIL: bgunFailQueuedModelLoad(player); break;
    case GGV_GL_ALLOCATE_RW: modelAllocateRwData(a.ref); break;
    case GGV_GL_GENERATION_HASH: v.ref = (void *)catalogModelGenerationHash(a.ref); break;
    case GGV_GL_RETAINED_LOG:
        sysLogPrintf(LOG_NOTE, "BONDGUN.SOURCE: consumed retained model id=%s hash=%s", (const char *)a.ref, (const char *)b.ref); break;
    default: break;
    } return v;
}
static void bgunGunLoadRetained(struct player *player)
{
    ggv_host host = { player, bgunRetainedRead, bgunRetainedWrite, bgunRetainedCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_GUN_RETAINED_LOAD), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: retained gun model load refused immutable program");
}

static ggv_value bgunSourceRouteRead(void *opaque, ggv_binding binding)
{
    struct player *player = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_GL_BODY_HAND: v.b = player->gunctrl.loadbodyhand != 0; break;
    case GGV_GL_LOAD_FILE: v.i = player->gunctrl.loadfilenum; break;
    case GGV_GL_PROVIDER: v.ref = (void *)player->gunctrl.loadhandle.provider; break;
    case GGV_MASTER_LOAD_GENERATION: v.u = player->gunctrl.loadcataloggeneration; break;
    default: break;
    } return v;
}
static void bgunSourceRouteWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct player *player = opaque;
    switch (binding) {
    case GGV_GL_LOAD_TYPE: g_LoadType = v.i; break;
    case GGV_MEM_GUNLOADSTATE: player->gunctrl.gunloadstate = v.i; break;
    case GGV_FLUX_FILE: player->gunctrl.handfilenum = v.i; break;
    case GGV_FLUX_GENERATION: player->gunctrl.handcataloggeneration = v.u; break;
    default: break;
    }
}
static ggv_value bgunSourceRouteCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct player *player = opaque; ggv_value v = {0};
    switch (service) {
    case GGV_GL_CAN_USE_HANDLE: v.b = bgunQueuedLoadCanUseHandle(player); break;
    case GGV_GL_EXTERNAL_SOURCE: v.b = bgunQueuedLoadUsesExternalModelSource(player); break;
    case GGV_GL_CATALOG_MODEL: v.ref = bgunQueuedLoadCatalogModelSource(player); break;
    case GGV_GL_FILE_PROVIDER: v.ref = (void *)fileProvider(); break;
    case GGV_GL_COPY_HAND_ID: strcpy(player->gunctrl.handcatalogid, player->gunctrl.loadcatalogid); break;
    case GGV_GL_COPY_HAND_HANDLE: player->gunctrl.handhandle = player->gunctrl.loadhandle; break;
    case GGV_GL_FAIL: bgunFailQueuedModelLoad(player); break;
    default: break;
    } return v;
}
static bool bgunGunLoadSource(struct player *player)
{
    ggv_host host = { player, bgunSourceRouteRead, bgunSourceRouteWrite, bgunSourceRouteCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_GUN_SOURCE_ROUTE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: gun source routing refused immutable program"); return true;
    }
    return result;
}

struct bgunguntexturescontext { struct player *player; struct modeldef *model; s32 index; };
static ggv_value bgunTexturesRead(void *opaque, ggv_binding binding)
{
    struct bgunguntexturescontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_GL_TARGET_MODEL: v.ref = *c->player->gunctrl.loadtomodeldef; break;
    case GGV_GL_NEXT_TEXTURE: v.u = c->player->gunctrl.nexttexturetoload; break;
    case GGV_GL_TEXTURE_INDEX: v.i = c->index; break;
    case GGV_GL_TEXTURE_COUNT: v.i = c->model->numtexconfigs; break;
    case GGV_GL_TEXTURE_WORD: {
        uint64_t word = c->model->texconfigs[c->index].texturenum;
        memcpy(&v.i64, &word, sizeof(word)); break;
    }
    case GGV_GL_TEXTURE_LIMIT: v.i64 = TEXTURE_CUSTOM_END; break;
    default: break;
    } return v;
}
static void bgunTexturesWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunguntexturescontext *c = opaque;
    switch (binding) {
    case GGV_GL_TEXTURE_MODEL: c->model = v.ref; break;
    case GGV_GL_TEXTURE_INDEX: c->index = v.i; break;
    case GGV_GL_NEXT_TEXTURE: c->player->gunctrl.nexttexturetoload = v.u; break;
    case GGV_GL_TEXTURE_MARK: c->model->texconfigs[c->index].unk0b = v.i; break;
    case GGV_MEM_GUNLOADSTATE: c->player->gunctrl.gunloadstate = v.i; break;
    default: break;
    }
}
static ggv_value bgunTexturesCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunguntexturescontext *c = opaque; ggv_value v = {0};
    switch (service) {
    case GGV_GL_TEXTURE_BEGIN: osSyncPrintf("BriGun:  BriGunLoadTick process GUN_LOADSTATE_DECOMPRESS_TEXTURES\n"); break;
    case GGV_GL_TEXTURE_AT: osSyncPrintf("BriGun:  at texture %d\n", a.i); break;
    case GGV_GL_TEXTURE_UNCOMPRESS: osSyncPrintf("BriGun:  Uncompress %d of %d\n", a.i, b.i); break;
    case GGV_GL_TEXTURE_LOAD: texLoad(&c->model->texconfigs[c->index].texturenum, &c->player->gunctrl.texpool, true); break;
    case GGV_GL_TEXTURE_DONE: osSyncPrintf("BriGun:  Set Load State: GUN_LOADSTATE_DECOMPRESS_DLS\n"); break;
    default: break;
    } return v;
}
static void bgunGunLoadTextures(struct player *player)
{
    struct bgunguntexturescontext context = {0}; context.player = player;
    ggv_host host = { &context, bgunTexturesRead, bgunTexturesWrite, bgunTexturesCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_GUN_TEXTURES), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: gun texture stage refused immutable program");
}

void bgunTickGunLoad(void)
{
	u64 remaining;
	s32 padding;
	u64 allocsize;
	u64 loadsize;
	uintptr_t ptr;
	struct player *player = g_Vars.currentplayer;
	struct modeldef *modeldef;
	uintptr_t newvalue;
	uintptr_t end;
	u32 stack;
#if VERSION >= VERSION_NTSC_1_0
	u32 stack2;
#endif

	if (player->gunctrl.gunloadstate == GUNLOADSTATE_FAILED) {
		bgunGunLoadFailed(player);
		return;
	}

	if (player->gunctrl.gunloadstate == GUNLOADSTATE_MODEL) {
		osSyncPrintf("BriGun:  BriGunLoadTick process GUN_LOADSTATE_LOAD_OBJ\n");
        if (player->gunctrl.loadretainedmodel) {
            bgunGunLoadRetained(player);
            return;
        }
		if (bgunGunLoadSource(player)) return;

		ptr = (uintptr_t) *player->gunctrl.loadmemptr;
		remaining = *player->gunctrl.loadmemremaining;

		// Align ptr to the next 16 byte boundary
		if (ptr % 16) {
			padding = 16 - (ptr % 16);
			ptr += padding;
			remaining -= padding;
		}

		*player->gunctrl.loadmemptr = (u8 *) ptr;
		*player->gunctrl.loadmemremaining = remaining;

		loadsize = ALIGN64(bgunQueuedGetInflatedSize(player)) + GUN_MODEL_LOAD_SCRATCH;

		osSyncPrintf("BriGun:  Loading - %s, pMem 0x%08x Size %d\n");

		if (loadsize > remaining) {
			osSyncPrintf("BriGun:  Warning: LoadSize > MemSize, clamping decomp. buffer from %d to %d (%d Bytes)\n", allocsize, remaining, remaining);
			loadsize = remaining;
		}

		// Load the model file to ptr
		g_LoadType = LOADTYPE_GUN;

		osSyncPrintf("BriGun:  obLoadto at 0x%08x, size %d\n", ptr, loadsize);

		modeldef = bgunQueuedLoadToAddr(player, (void *)ptr, loadsize);

		if (modeldef == NULL) {
			g_LoadType = LOADTYPE_NONE;
			bgunFailQueuedModelLoad(player);
			return;
		}

		// Reserve some space for textures
		allocsize = bgunQueuedGetLoadedSize(player) + 0xe00;
#ifdef PLATFORM_64BIT
		allocsize += 0xe00;
#endif

		osSyncPrintf("BriGun:  Used size %d (Ob Size %d)\n");
		osSyncPrintf("BriGun:  block len %d usedsize %d\n");
		osSyncPrintf("BriGun:  obln ram_len %d block_len %d\n");
		osSyncPrintf("BriGun:  new used size %d\n");

		bgunQueuedGetLoadedSize(player);

		player->gunctrl.fileinfo.loadedsize = bgunQueuedGetLoadedSize(player);
		player->gunctrl.fileinfo.allocsize = allocsize;
		end = ALIGN16((uintptr_t)ptr + allocsize);
		allocsize = end - ptr;
		player->gunctrl.fileinfo.allocsize = allocsize;
		if (1);
		remaining -= allocsize;

		osSyncPrintf("BriGun:  Texture Block at 0x%08x size %d, endp 0x%08x\n");

		texInitPool(&player->gunctrl.texpool, (u8 *)end, remaining);

		// Tidy up the model
		modelPromoteTypeToPointer(modeldef);
		modelPromoteOffsetsToPointers(modeldef, 0x05000000, (uintptr_t)modeldef);

		*player->gunctrl.loadtomodeldef = modeldef;

		player->gunctrl.nexttexturetoload = 0;
		osSyncPrintf("BriGun:  Set Load State: GUN_LOADSTATE_DECOMPRESS_TEXTURES\n");
		player->gunctrl.gunloadstate = GUNLOADSTATE_TEXTURES;
		return;
	}

	if (player->gunctrl.gunloadstate == GUNLOADSTATE_TEXTURES) {
		bgunGunLoadTextures(player);
		return;
	}

	if (player->gunctrl.gunloadstate == GUNLOADSTATE_DLS) {
		osSyncPrintf("BriGun:  BriGunLoadTick process GUN_LOADSTATE_DECOMPRESS_DLS\n");

		modeldef = *player->gunctrl.loadtomodeldef;

		modeldefPromoteDisplayListsUsingSizes(
			modeldef,
			(s32)player->gunctrl.loadfilenum,
			(s32)player->gunctrl.fileinfo.allocsize,
			(s32)player->gunctrl.fileinfo.loadedsize,
			0x05000000,
			modeldef,
			&player->gunctrl.texpool,
			false);

		bgunQueuedGetInflatedSize(player);
		bgunQueuedGetLoadedSize(player);
		bgunQueuedGetLoadedSize(player);
		bgunQueuedGetLoadedSize(player);

		modelAllocateRwData(modeldef);

		osSyncPrintf("BriGun:  propgfx_decompress 0x%08x\n");
		osSyncPrintf("BriGun:  DL waste space %d from %d (Used %d, Ramlen %d, ObSize %d)\n");
		osSyncPrintf("Increase GUNSAVESIZE to %d!!!\n");

		newvalue = ALIGN64(texGetPoolLeftPos(&player->gunctrl.texpool));
		remaining = *player->gunctrl.loadmemremaining;
		remaining -= (intptr_t)(newvalue - (uintptr_t) *player->gunctrl.loadmemptr);

		*player->gunctrl.loadmemptr = (u8 *) newvalue;
		*player->gunctrl.loadmemremaining = remaining;

		osSyncPrintf("BriGun:  Set Load State: GUN_LOADSTATE_LOADED\n");
		player->gunctrl.gunloadstate = GUNLOADSTATE_LOADED;

#if PIRACYCHECKS
		{
			s32 *ptr = (s32 *)&tagsReset;
			s32 *end = (s32 *)&tagFindById;
			u32 checksum = 0;

			while (ptr < end) {
				checksum -= ~*ptr;
				ptr++;
			}

			if (checksum != CHECKSUM_PLACEHOLDER) {
				ptr = (s32 *)&tagsReset + 3;

				if (1);
				end = &ptr[7];

				while (ptr < end) {
					*ptr |= 0xff;
					ptr++;
				}
			}
		}
#endif
	}
}

const char var7f1abcd8[] = "need a new gun loading (lock %d gunmemnew %d)\n";
const char var7f1abd08[] = "loading gun file: %d type: %d\n";
const char var7f1abd28[] = "BriGun: Process MASTER_GUN_LOADSTATE_FLUX\n";
const char var7f1abd54[] = "BriGun: Set Master State: MASTER_GUN_LOADSTATE_HANDS\n";
const char var7f1abd8c[] = "BriGun: Process MASTER_GUN_LOADSTATE_HANDS\n";
const char var7f1abdb8[] = "BriGun: Setup Hand Load\n";
const char var7f1abdd4[] = "Hand  : Using cached hands\n";
const char var7f1abdf0[] = "Hand  : Look ma no hands!\n";
const char var7f1abe0c[] = "BriGun: Set Master State: MASTER_GUN_LOADSTATE_GUN\n";
const char var7f1abe40[] = "BriGun: Process MASTER_GUN_LOADSTATE_GUN\n";
const char var7f1abe6c[] = "BriGun: Setup Gun Load\n";
const char var7f1abe84[] = "BriGun: Set Master State: MASTER_GUN_LOADSTATE_CARTS\n";
const char var7f1abebc[] = "BriGun: Process MASTER_GUN_LOADSTATE_CARTS\n";
const char var7f1abee8[] = "BriGun: Cart Loaded setting GUN_LOADSTATE_FLUX\n";
const char var7f1abf18[] = "BriGun: Cart loading - looking for carts\n";
const char var7f1abf44[] = "BriGun: Loading cart %d\n";
const char var7f1abf60[] = "BriGun: Request for cart %d ignored - cart already loaded\n";
const char var7f1abf9c[] = "BriGun: Compile Hand 0x%08x Gun 0x%0x8\n";
const char var7f1abfc4[] = "Gun   : Compiled Gun 0x%08x\n";
const char var7f1abfe4[] = "Gun   : Compiled Size %d\n";
const char var7f1ac000[] = "Hand  : Compiled Hand 0x%08x\n";
const char var7f1ac020[] = "Hand  : Compiled Size %d\n";
const char var7f1ac03c[] = "Gun   : Compile overhead %d bytes\n";
const char var7f1ac060[] = "Hand  : Hand Obj 0x%08x Gun Obj 0x%08x \n";
const char var7f1ac08c[] = "Gun   : After Comp : Base 0x%08x Free %d\n";
const char var7f1ac0b8[] = "Gun   : After Cached Setup : Base 0x%08x Free %d\n";
const char var7f1ac0ec[] = "Gun   : TotalUsed %d, Free %d\n";
const char var7f1ac10c[] = "BriGun: Set Master State: MASTER_GUN_LOADSTATE_LOADED\n";
const char var7f1ac144[] = "GunLockTimer: %d\n";

static ggv_value bgunMasterAdmissionCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value v = {0};
    switch (service) {
    case GGV_MEM_CHANGE_OWNER: v.b = bgunChangeGunMem(a.i); break;
    default: break;
    } return v;
}
static bool bgunMasterLoadReady(struct player *player)
{
    /* Keep the caller's captured player across the ownership child's fresh
     * current-player invocation, matching the native OR/AND expression. */
    struct bgunmemoryownercontext context = { player, 0, 0 };
    ggv_host host = { &context, bgunMemoryOwnerRead, bgunMemoryOwnerWrite, bgunMasterAdmissionCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MASTER_LOAD_ADMISSION), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: master-load admission refused immutable program"); return false;
    }
    return result;
}

struct bgunmastersourcecontext {
    struct player *player;
    s32 weapon, bodynum, headnum, hf32;
    u16 handfile, file;
    char handid[64]; asset_data_handle_t handsource;
};
static ggv_value bgunMasterSourceRead(void *opaque, ggv_binding binding)
{
    struct bgunmastersourcecontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_LOADED_GUNMEMNEW: v.i = c->player->gunctrl.gunmemnew; break;
    case GGV_MASTER_WEAPON: v.i = c->weapon; break;
    case GGV_MASTER_HF32: v.i = c->hf32; break;
    case GGV_LOADED_MASTERLOADSTATE: v.i = c->player->gunctrl.masterloadstate; break;
    case GGV_MASTER_MODEL: v.ref = c->player->gunctrl.gunmodeldef; break;
    default: break;
    } return v;
}
static void bgunMasterSourceWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmastersourcecontext *c = opaque;
    switch (binding) {
    case GGV_MASTER_WEAPON: c->weapon = v.i; break;
    case GGV_MASTER_HF32: c->hf32 = v.i; break;
    case GGV_MASTER_HANDFILE: c->handfile = v.i; break;
    case GGV_MASTER_FILE: c->file = v.i; break;
    default: break;
    }
}
static ggv_value bgunMasterSourceCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmastersourcecontext *c = opaque; ggv_value v = {0};
    switch (service) {
    case GGV_MASTER_CHOOSE_BODY: v.b = playerChooseBodyAndHead(&c->bodynum, &c->headnum, NULL); break;
    case GGV_MASTER_HAND_FILE: v.b = catalogGetBodyHandFilenumChecked(c->bodynum, &c->hf32); break;
    case GGV_MASTER_HAND_SOURCE: v.b = catalogGetBodyHandSourceChecked(c->bodynum, c->handid, &c->handsource); break;
    case GGV_MASTER_SOURCE_ERROR: sysLogPrintf(LOG_ERROR, "PLAYER.INIT.ROLLBACK hands identity player=%d", g_Vars.currentplayernum); break;
    case GGV_MASTER_WEAPON_FILE: v.i = weaponGetFileNum(a.i); break;
    case GGV_INFO_IS_V2: v.b = wgV2RuntimeIsWeapon(a.i); break;
    case GGV_MASTER_RUNTIME_MODEL: v.ref = wgV2RuntimeModel(a.i); break;
    case GGV_MASTER_HAS_HANDS: v.b = weaponHasFlag(a.i, WEAPONFLAG_HASHANDS); break;
    case GGV_MASTER_SOURCE_CURRENT: v.b = bgunBodyHandSourceIsCurrent(c->player, c->bodynum); break;
    case GGV_MEM_ENTER_FLUX: bgunEnterFlux(); break;
    default: break;
    } return v;
}
static bool bgunMasterPrepareSource(struct player *player, s32 *weapon, u16 *handfile, u16 *file, s32 *body, s32 *head)
{
    struct bgunmastersourcecontext context = {0}; context.player = player;
    ggv_host host = { &context, bgunMasterSourceRead, bgunMasterSourceWrite, bgunMasterSourceCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MASTER_SOURCE_PREPARE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: master source preparation refused immutable program"); return false;
    }
    if (!result) return false;
    *weapon = context.weapon; *handfile = context.handfile; *file = context.file; *body = context.bodynum; *head = context.headnum;
    return true;
}

struct bgunmasterfluxcontext { struct player *player; s32 weapon, hashands; u16 handfile; s32 casingindex; };
static ggv_value bgunMasterFluxRead(void *opaque, ggv_binding binding)
{
    struct bgunmasterfluxcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_FLUX_CASING_COUNT: v.i = ARRAYCOUNT(g_Casings); break;
    case GGV_FLUX_CASING_MODEL: v.ref = g_Casings[c->casingindex].modeldef; break;
    case GGV_MASTER_CART_MODEL: v.ref = c->player->gunctrl.cartmodeldef; break;
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    default: break;
    } return v;
}
static void bgunMasterFluxWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmasterfluxcontext *c = opaque;
    switch (binding) {
    case GGV_FLUX_CASING_INDEX: c->casingindex = v.i; break;
    case GGV_FLUX_CASING_MODEL: g_Casings[c->casingindex].modeldef = v.ref; break;
    case GGV_FLUX_CASINGS_ACTIVE: g_CasingsActive = v.b; break;
    case GGV_MASTER_CART_MODEL: c->player->gunctrl.cartmodeldef = v.ref; break;
    case GGV_LOADED_MASTERLOADSTATE: c->player->gunctrl.masterloadstate = v.i; break;
    default: break;
    }
}
static ggv_value bgunMasterFluxCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmasterfluxcontext *c = opaque; ggv_value v = {0};
    struct player *player = c->player; s32 newweaponnum = c->weapon, hashands = c->hashands; u16 handfilenum = c->handfile;
    switch (service) {
    case GGV_MASTER_FLUX_LOG:
        sysLogPrintf(LOG_NOTE,
								"LOG.WPN.DIAG: bgunTickMasterLoad transition player=0 FLUX->HANDS newwpn=%d hashands=%d handfilenum=%d cur_handfilenum=%d",
								(s32)newweaponnum, (s32)hashands, (s32)handfilenum,
								(s32)player->gunctrl.handfilenum);
        break;
    default: break;
    } return v;
}
static void bgunMasterFluxTransition(struct player *player, s32 weapon, bool hashands, u16 handfile)
{
    struct bgunmasterfluxcontext context = { player, weapon, hashands, handfile, 0 };
    ggv_host host = { &context, bgunMasterFluxRead, bgunMasterFluxWrite, bgunMasterFluxCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MASTER_FLUX_TRANSITION), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: master flux transition refused immutable program");
}

struct bgunmasterguncontext { struct player *player; s32 weapon, body; u16 file; };
static ggv_value bgunMasterGunRead(void *opaque, ggv_binding binding)
{
    struct bgunmasterguncontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_MEM_GUNLOADSTATE: v.i = c->player->gunctrl.gunloadstate; break;
    case GGV_FLUX_LOAD_PTR: v.ref = c->player->gunctrl.handmemloadptr; break;
    case GGV_FLUX_LOAD_REMAINING: v.i = c->player->gunctrl.handmemloadremaining; break;
    case GGV_MASTER_WEAPON: v.i = c->weapon; break;
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    default: break;
    } return v;
}
static void bgunMasterGunWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmasterguncontext *c = opaque;
    switch (binding) {
    case GGV_MASTER_MEM_PTR: c->player->gunctrl.memloadptr = v.ref; break;
    case GGV_MASTER_MEM_REMAINING: c->player->gunctrl.memloadremaining = v.u; break;
    case GGV_MEM_GUNLOADSTATE: c->player->gunctrl.gunloadstate = v.i; break;
    case GGV_LOADED_MASTERLOADSTATE: c->player->gunctrl.masterloadstate = v.i; break;
    default: break;
    }
}
static ggv_value bgunMasterGunCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmasterguncontext *c = opaque; ggv_value v = {0};
    struct player *player = c->player; s32 newweaponnum = c->weapon;
    switch (service) {
    case GGV_MASTER_QUEUE_BODY:
        v.b = bgunQueueBodyHandModelLoad(player, c->body, &player->gunctrl.gunmodeldef,
            &player->gunctrl.memloadptr, &player->gunctrl.memloadremaining); break;
    case GGV_INFO_IS_V2: v.b = wgV2RuntimeIsWeapon(a.i); break;
    case GGV_MASTER_RUNTIME_MODEL: v.ref = wgV2RuntimeModel(a.i); break;
    case GGV_MASTER_QUEUE_RETAINED: v.b = bgunQueueRetainedModelLoad(player, a.ref, &player->gunctrl.gunmodeldef); break;
    case GGV_MASTER_QUEUE_STATIC:
        bgunQueueModelLoad(player, c->file, &player->gunctrl.gunmodeldef,
            &player->gunctrl.memloadptr, &player->gunctrl.memloadremaining); break;
    case GGV_MASTER_GUN_TICK: bgunTickGunLoad(); break;
    case GGV_MASTER_GUN_LOG:
        sysLogPrintf(LOG_NOTE,
									"LOG.WPN.DIAG: bgunTickMasterLoad transition player=0 GUN->CARTS newwpn=%d gunmodeldef=%p handmodeldef=%p",
									(s32)newweaponnum,
									(void *)player->gunctrl.gunmodeldef,
									(void *)player->gunctrl.handmodeldef);
        break;
    default: break;
    } return v;
}
static bool bgunMasterGunStage(struct player *player, s32 weapon, s32 body, u16 file)
{
    struct bgunmasterguncontext context = { player, weapon, body, file };
    ggv_host host = { &context, bgunMasterGunRead, bgunMasterGunWrite, bgunMasterGunCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MASTER_GUN_STAGE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: master gun stage refused immutable program"); return false;
    }
    return result;
}

struct bgunmasterhandscontext { struct player *player; s32 weapon, body, hashands; u16 handfile; char handid[64]; asset_data_handle_t handsource; };
static ggv_value bgunMasterHandsRead(void *opaque, ggv_binding binding)
{
    struct bgunmasterhandscontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_MASTER_HAS_HANDS_INPUT: v.b = c->hashands; break;
    case GGV_MASTER_HANDFILE: v.i = c->handfile; break;
    case GGV_FLUX_FILE: v.i = c->player->gunctrl.handfilenum; break;
    case GGV_MASTER_LOAD_GENERATION: v.u = c->player->gunctrl.loadcataloggeneration; break;
    case GGV_MEM_GUNLOADSTATE: v.i = c->player->gunctrl.gunloadstate; break;
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    default: break;
    } return v;
}
static void bgunMasterHandsWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmasterhandscontext *c = opaque;
    switch (binding) {
    case GGV_FLUX_FILE: c->player->gunctrl.handfilenum = v.i; break;
    case GGV_FLUX_CATALOG_HEAD: c->player->gunctrl.handcatalogid[0] = v.i; break;
    case GGV_FLUX_GENERATION: c->player->gunctrl.handcataloggeneration = v.u; break;
    case GGV_FLUX_HAND_MODEL: c->player->gunctrl.handmodeldef = v.ref; break;
    case GGV_FLUX_LOAD_PTR: c->player->gunctrl.handmemloadptr = v.ref; break;
    case GGV_FLUX_LOAD_REMAINING: c->player->gunctrl.handmemloadremaining = v.i; break;
    case GGV_MEM_GUNLOADSTATE: c->player->gunctrl.gunloadstate = v.i; break;
    case GGV_LOADED_MASTERLOADSTATE: c->player->gunctrl.masterloadstate = v.i; break;
    default: break;
    }
}
static ggv_value bgunMasterHandsCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmasterhandscontext *c = opaque; ggv_value v = {0};
    struct player *player = c->player; s32 newweaponnum = c->weapon, hashands = c->hashands; u16 handfilenum = c->handfile;
    switch (service) {
    case GGV_MASTER_SOURCE_CURRENT: v.b = bgunBodyHandSourceIsCurrent(player, c->body); break;
    case GGV_MASTER_HAND_SOURCE: v.b = catalogGetBodyHandSourceChecked(c->body, c->handid, &c->handsource); break;
    case GGV_MASTER_COMPARE_LOAD_ID: v.i = strcmp(player->gunctrl.loadcatalogid, c->handid); break;
    case GGV_MASTER_SAME_LOAD_HANDLE: v.b = bgunSameSourceHandle(player->gunctrl.loadhandle, c->handsource); break;
    case GGV_MASTER_CATALOG_GENERATION: v.u = assetCatalogGetGeneration(); break;
    case GGV_MASTER_MEMORY: v.ref = bgunGetGunMem(); break;
    case GGV_MASTER_CAPACITY: v.u = bgunCalculateGunMemCapacity(); break;
    case GGV_MASTER_QUEUE_HAND:
        v.b = bgunQueueBodyHandModelLoad(player, c->body, &player->gunctrl.handmodeldef,
            &player->gunctrl.handmemloadptr, (u32 *) &player->gunctrl.handmemloadremaining); break;
    case GGV_MASTER_GUN_TICK: bgunTickGunLoad(); break;
    case GGV_MASTER_CLEAR_HAND_HANDLE: player->gunctrl.handhandle = (asset_data_handle_t){0}; break;
    case GGV_MASTER_HANDS_LOG:
        sysLogPrintf(LOG_NOTE,
								"LOG.WPN.DIAG: bgunTickMasterLoad transition player=0 HANDS->GUN newwpn=%d hashands=%d handfilenum=%d handmodeldef=%p",
								(s32)newweaponnum, (s32)hashands, (s32)handfilenum,
								(void *)player->gunctrl.handmodeldef);
        break;
    default: break;
    } return v;
}
static bool bgunMasterHandsStage(struct player *player, s32 weapon, s32 body, bool hashands, u16 handfile)
{
    struct bgunmasterhandscontext context = {0}; context.player = player; context.weapon = weapon;
    context.body = body; context.hashands = hashands; context.handfile = handfile;
    ggv_host host = { &context, bgunMasterHandsRead, bgunMasterHandsWrite, bgunMasterHandsCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MASTER_HANDS_STAGE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: master hands stage refused immutable program"); return false;
    }
    return result;
}

struct bgunmastercartscontext { struct player *player; struct weaponfunc *func; struct weapon *weapon; struct inventory_ammo *ammo; u16 file; };
static ggv_value bgunMasterCartsRead(void *opaque, ggv_binding binding)
{
    struct bgunmastercartscontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_MEM_GUNLOADSTATE: v.i = c->player->gunctrl.gunloadstate; break;
    case GGV_MASTER_CART_MODEL: v.ref = c->player->gunctrl.cartmodeldef; break;
    case GGV_CART_PLAYER_COUNT: v.i = PLAYERCOUNT(); break;
    case GGV_SW_WEAPON: v.i = c->player->gunctrl.weaponnum; break;
    case GGV_CART_TYPE: v.i = c->func->type; break;
    case GGV_CART_AMMO_INDEX: v.i = c->func->ammoindex; break;
    case GGV_CART_EJECT: v.u = c->ammo->casingeject; break;
    default: break;
    } return v;
}
static void bgunMasterCartsWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmastercartscontext *c = opaque;
    switch (binding) {
    case GGV_MEM_GUNLOADSTATE: c->player->gunctrl.gunloadstate = v.i; break;
    case GGV_CART_FUNC: c->func = v.ref; break;
    case GGV_CART_WEAPON: c->weapon = v.ref; break;
    case GGV_CART_AMMO: c->ammo = v.ref; break;
    case GGV_MASTER_FILE: c->file = v.i; break;
    default: break;
    }
}
static ggv_value bgunMasterCartsCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmastercartscontext *c = opaque; ggv_value v = {0}; struct player *player = c->player;
    switch (service) {
    case GGV_CART_FUNCTION: v.ref = gsetGetWeaponFunction2(&player->hands[a.i].gset); break;
    case GGV_CART_FIND_WEAPON: v.ref = weaponFindById(a.i); break;
    case GGV_CART_AMMO_REF: v.ref = c->weapon->ammos[a.i]; break;
    case GGV_CART_FILE: v.i = g_CartFileNums[a.i]; break;
    case GGV_CART_QUEUE:
        bgunQueueModelLoad(player, c->file, &player->gunctrl.cartmodeldef,
            &player->gunctrl.memloadptr, &player->gunctrl.memloadremaining); break;
    case GGV_MASTER_GUN_TICK: bgunTickGunLoad(); break;
    default: break;
    } return v;
}
static bool bgunMasterCartsSelect(struct player *player)
{
    struct bgunmastercartscontext context = {0}; context.player = player;
    ggv_host host = { &context, bgunMasterCartsRead, bgunMasterCartsWrite, bgunMasterCartsCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MASTER_CARTS_SELECT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: master cartridge selection refused immutable program"); return false;
    }
    return result;
}

struct bgunmasterfinalcontext { struct player *player; struct hand *hand; s32 weapon, sum; };
static ggv_value bgunMasterFinalRead(void *opaque, ggv_binding binding)
{
    struct bgunmasterfinalcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_FINAL_SUM: v.i = c->sum; break;
    case GGV_MASTER_WEAPON: v.i = c->weapon; break;
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    case GGV_FINAL_GUN_MODEL: v.ref = &c->hand->gunmodel; break;
    case GGV_FINAL_HAND_MODEL: v.ref = &c->hand->handmodel; break;
    case GGV_FINAL_GUN_DEF: v.ref = c->player->gunctrl.gunmodeldef; break;
    case GGV_FLUX_HAND_MODEL: v.ref = c->player->gunctrl.handmodeldef; break;
    case GGV_FINAL_GUN_SAVE: v.ref = c->hand->unk0a6c; break;
    case GGV_FINAL_HAND_SAVE: v.ref = c->hand->handsavedata; break;
    case GGV_FINAL_GUN_ROOT: v.ref = c->player->gunctrl.gunmodeldef->rootnode; break;
    case GGV_FINAL_HAND_ROOT: v.ref = c->player->gunctrl.handmodeldef->rootnode; break;
    case GGV_MASTER_MEM_PTR: v.ref = c->player->gunctrl.memloadptr; break;
    case GGV_MASTER_MEM_REMAINING: v.u = c->player->gunctrl.memloadremaining; break;
    case GGV_FINAL_MATRIX_BYTES: v.u = 50 * sizeof(Mtxf); break;
    default: break;
    } return v;
}
static void bgunMasterFinalWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmasterfinalcontext *c = opaque;
    switch (binding) {
    case GGV_FINAL_HAND_INDEX: c->hand = &c->player->hands[v.i]; break;
    case GGV_FINAL_SUM: c->sum = v.i; break;
    case GGV_FINAL_GUN_COMMAND: c->hand->unk0dcc = v.ref; break;
    case GGV_FINAL_HAND_COMMAND: c->hand->unk0dd0 = v.ref; break;
    case GGV_FINAL_MATRIX_STATE: c->hand->unk0dd4 = v.i; break;
    case GGV_FINAL_MATRIX_PTR: c->hand->unk0dd8 = v.ref; break;
    case GGV_MASTER_MEM_PTR: c->player->gunctrl.memloadptr = v.ref; break;
    case GGV_MASTER_MEM_REMAINING: c->player->gunctrl.memloadremaining = v.u; break;
    case GGV_LOADED_MASTERLOADSTATE: c->player->gunctrl.masterloadstate = v.i; break;
    case GGV_LOADED_GUNMEMTYPE: c->player->gunctrl.gunmemtype = v.i; break;
    case GGV_LOADED_GUNMEMNEW: c->player->gunctrl.gunmemnew = v.i; break;
    default: break;
    }
}
static ggv_value bgunMasterFinalCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmasterfinalcontext *c = opaque; ggv_value v = {0}; struct player *player = c->player;
    switch (service) {
    case GGV_FINAL_MODEL_INIT: modelInit(a.ref, b.ref, d.ref, false); break;
    case GGV_FINAL_COMMANDS: v.i = bgunCreateModelCmdList(a.ref, b.ref, d.ref); break;
    case GGV_FINAL_PTR_OFFSET: v.ref = (u8 *)a.ref + b.i; break;
    case GGV_MASTER_CAPACITY: v.u = bgunCalculateGunMemCapacity(); break;
    case GGV_FINAL_LOG:
        sysLogPrintf(LOG_NOTE,
            "LOG.WPN.DIAG: bgunTickMasterLoad transition player=0 CARTS->LOADED newwpn=%d gunmodeldef=%p handmodeldef=%p sum=%d unk0dd4=%d unk0dd8=%p",
            (s32)c->weapon, (void *)player->gunctrl.gunmodeldef, (void *)player->gunctrl.handmodeldef,
            (s32)c->sum, (s32)player->hands[0].unk0dd4, (void *)player->hands[0].unk0dd8); break;
    default: break;
    } return v;
}
static void bgunMasterFinalize(struct player *player, s32 weapon)
{
    struct bgunmasterfinalcontext context = {0}; context.player = player; context.weapon = weapon;
    ggv_host host = { &context, bgunMasterFinalRead, bgunMasterFinalWrite, bgunMasterFinalCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MASTER_FINALIZE), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: master finalization refused immutable program");
}

static s32 g_BgunMasterLastLoadKey = -1;
struct bgunmasterparentcontext { struct player *player; s32 weapon, body, head; u16 handfile, file; bool hashands, diag_hands; };
static ggv_value bgunMasterParentRead(void *opaque, ggv_binding binding)
{
    struct bgunmasterparentcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    case GGV_MASTER_WEAPON: v.i = c->weapon; break;
    case GGV_MASTER_FILE: v.i = c->file; break;
    case GGV_MASTER_HANDFILE: v.i = c->handfile; break;
    case GGV_PARENT_LAST_KEY: v.i = g_BgunMasterLastLoadKey; break;
    case GGV_LOADED_MASTERLOADSTATE: v.i = c->player->gunctrl.masterloadstate; break;
    case GGV_LOADED_GUNMEMTYPE: v.i = c->player->gunctrl.gunmemtype; break;
    case GGV_VERSION_1_OR_LATER: v.b = VERSION >= VERSION_NTSC_1_0; break;
    default: break;
    } return v;
}
static void bgunMasterParentWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunmasterparentcontext *c = opaque;
    switch (binding) {
    case GGV_MASTER_FILE: c->file = v.i; break;
    case GGV_MASTER_HANDFILE: c->handfile = v.i; break;
    case GGV_MASTER_HAS_HANDS_INPUT: c->hashands = v.b; break;
    case GGV_PARENT_DIAG_HANDS: c->diag_hands = v.b; break;
    case GGV_PARENT_LAST_KEY: g_BgunMasterLastLoadKey = v.i; break;
    case GGV_LOADED_MASTERLOADSTATE: c->player->gunctrl.masterloadstate = v.i; break;
    case GGV_LOADED_GUNMEMTYPE: c->player->gunctrl.gunmemtype = v.i; break;
    case GGV_LOADED_GUNMEMNEW: c->player->gunctrl.gunmemnew = v.i; break;
    default: break;
    }
}
static ggv_value bgunMasterParentCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunmasterparentcontext *c = opaque; struct player *player = c->player; ggv_value v = {0};
    switch (service) {
    case GGV_PARENT_READY: v.b = bgunMasterLoadReady(player); break;
    case GGV_PARENT_PREPARE: v.b = bgunMasterPrepareSource(player, &c->weapon, &c->handfile, &c->file, &c->body, &c->head); break;
    case GGV_MASTER_HAS_HANDS: v.b = weaponHasFlag(a.i, WEAPONFLAG_HASHANDS) != 0; break;
    case GGV_MASTER_RUNTIME_MODEL: v.ref = wgV2RuntimeModel(a.i); break;
    case GGV_PARENT_FLUX: bgunMasterFluxTransition(player, c->weapon, c->hashands, c->handfile); break;
    case GGV_PARENT_HANDS: v.b = bgunMasterHandsStage(player, c->weapon, c->body, c->hashands, c->handfile); break;
    case GGV_PARENT_GUN: v.b = bgunMasterGunStage(player, c->weapon, c->body, c->file); break;
    case GGV_PARENT_CARTS: v.b = bgunMasterCartsSelect(player); break;
    case GGV_PARENT_FINALIZE: bgunMasterFinalize(player, c->weapon); break;
    case GGV_PARENT_ENTRY_LOG:
        sysLogPrintf(LOG_NOTE,
            "LOG.WPN.DIAG: bgunTickMasterLoad enter player=0 newwpn=%d filenum=%d bodynum=%d handfilenum=%d hashands_flag=%d masterload=%d gunloadstate=%d gunmemowner=%d gunmemtype=%d handmodeldef=%p gunmodeldef=%p",
            (s32)c->weapon, (s32)c->file, (s32)c->body, (s32)c->handfile, (s32)c->diag_hands,
            (s32)player->gunctrl.masterloadstate, (s32)player->gunctrl.gunloadstate,
            (s32)player->gunctrl.gunmemowner, (s32)player->gunctrl.gunmemtype,
            (void *)player->gunctrl.handmodeldef, (void *)player->gunctrl.gunmodeldef); break;
    case GGV_PARENT_SHORTCUT_LOG:
        sysLogPrintf(LOG_NOTE,
            "LOG.WPN.DIAG: bgunTickMasterLoad transition player=0 SHORTCUT->LOADED newwpn=%d (filenum=0 path)", (s32)c->weapon); break;
    default: break;
    } return v;
}
void bgunTickMasterLoad(void)
{
    struct bgunmasterparentcontext context = {0}; context.player = g_Vars.currentplayer;
    ggv_host host = { &context, bgunMasterParentRead, bgunMasterParentWrite, bgunMasterParentCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_MASTER_LOAD_PARENT), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: master loader refused immutable program");
}

void bgunTickLoad(void)
{
    struct bgunmemorycontext context = {0};
    bgunRunMemory(&context, GGV_TICK_LOAD);
}

/**
 * Load all gun data (models, hands etc) again after returning from a different
 * gunmem type. For example, after returning from a cutscene, pause menu or from
 * controlling the eyespy.
 *
 * Gun data is typically loaded over several frames when switching, but that
 * cannot happen here as the gun must be available for the next frame.
 *
 * If the previous gun mem owner cannot release its memory right now,
 * lockscreen will be set. This causes the game to repaint the previous frame.
 *
 * Return false on success, or true if the load all should be retried on the
 * next frame.
 */
static ggv_value bgunLoadAllRead(void *opaque, ggv_binding binding)
{
    ggv_value v = {0};
    switch (binding) {
    case GGV_RELOAD_PAL: v.b = VERSION >= VERSION_PAL_BETA; break;
    case GGV_RELOAD_DEVICES_ACTIVE: v.u = g_Vars.currentplayer->devicesactive; break;
    case GGV_RELOAD_DEVICES_INHIBIT: v.u = g_Vars.currentplayer->devicesinhibit; break;
    case GGV_SW_WEAPON: v.i = g_Vars.currentplayer->gunctrl.weaponnum; break;
    case GGV_SW_NONE: v.i = WEAPON_NONE; break;
    case GGV_LOADED_GUNMEMTYPE: v.i = g_Vars.currentplayer->gunctrl.gunmemtype; break;
    case GGV_LOADED_GUNMEMOWNER: v.i = g_Vars.currentplayer->gunctrl.gunmemowner; break;
    case GGV_MEM_OWNER_BOND: v.i = GUNMEMOWNER_BONDGUN; break;
    default: break;
    } return v;
}
static void bgunLoadAllWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    switch (binding) {
    case GGV_MEM_LOAD_ALL: g_Vars.currentplayer->gunctrl.loadall = v.b; break;
    case GGV_LOADED_GUNMEMNEW: g_Vars.currentplayer->gunctrl.gunmemnew = v.i; break;
    case GGV_LOAD_LOCKSCREEN: g_Vars.lockscreen = v.b; break;
    default: break;
    }
}
static ggv_value bgunLoadAllCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value v = {0};
    switch (service) {
    case GGV_MEM_ENTER_FLUX: bgunEnterFlux(); break;
    case GGV_MEM_CHANGE_OWNER: v.b = bgunChangeGunMem(a.i); break;
    case GGV_MEM_MASTER_TICK: bgunTickMasterLoad(); break;
    case GGV_RELOAD_IS_LOADED: v.b = bgunIsLoaded(); break;
    default: break;
    } return v;
}
bool bgunLoadAll(void)
{
    ggv_host host = { NULL, bgunLoadAllRead, bgunLoadAllWrite, bgunLoadAllCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_LOAD_ALL), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: load-all parent refused immutable program"); return true;
    }
    return result;
}

struct modeldef *bgunGetCartModeldef(void)
{
	return g_Vars.currentplayer->gunctrl.cartmodeldef;
}

void bgun0f09ebcc(struct defaultobj *obj, struct coord *coord, RoomNum *rooms, Mtxf *matrix1, struct coord *velocity, Mtxf *matrix2, struct prop *prop, struct coord *pos)
{
	struct prop *objprop = obj->prop;

	if (objprop) {
		propActivate(objprop);
		propEnable(objprop);
		mtx00015f04(obj->model->scale, matrix1);
		func0f06a580(obj, coord, matrix1, rooms);

		if (obj->type == OBJTYPE_WEAPON && ((struct weaponobj *) obj)->weaponnum == WEAPON_BOLT) {
			s32 beamnum = boltbeamFindByProp(objprop);

			if (beamnum == -1) {
				beamnum = boltbeamCreate(objprop);
			}

			if (beamnum != -1) {
				boltbeamSetHeadPos(beamnum, pos);
				boltbeamSetTailPos(beamnum, pos);
			}
		}

		func0f0685e4(objprop);

		if (obj->hidden & OBJHFLAG_PROJECTILE) {
			obj->projectile->flags |= PROJECTILEFLAG_AIRBORNE;
			obj->projectile->ownerprop = prop;

			projectileSetSticky(objprop);
			mtx4Copy(matrix2, (Mtxf *)&obj->projectile->mtx);

			obj->projectile->speed.x = velocity->x;
			obj->projectile->speed.y = velocity->y;
			obj->projectile->speed.z = velocity->z;
			obj->projectile->obj = obj;
			obj->projectile->unk0d8 = g_Vars.lvframenum;
		}
	}
}

void bgun0f09ed2c(struct defaultobj *obj, struct coord *newpos, Mtxf *arg2, struct coord *velocity, Mtxf *arg4)
{
	struct prop *objprop = obj->prop;
	struct coord pos;
	RoomNum rooms[8];

	if (objprop) {
		struct prop *playerprop = g_Vars.currentplayer->prop;

		pos.x = playerprop->pos.x;
		pos.y = playerprop->pos.y;
		pos.z = playerprop->pos.z;

		roomsCopy(playerprop->rooms, rooms);

		bgun0f09ebcc(obj, &pos, rooms, arg2, velocity, arg4, playerprop, newpos);

		if (obj->hidden & OBJHFLAG_PROJECTILE) {
			obj->projectile->flags |= PROJECTILEFLAG_LAUNCHING;

			obj->projectile->nextsteppos.x = newpos->x;
			obj->projectile->nextsteppos.y = newpos->y;
			obj->projectile->nextsteppos.z = newpos->z;
		}
	}
}

struct defaultobj *bgunCreateThrownProjectile2(struct chrdata *chr, struct gset *gset, struct coord *pos, RoomNum *rooms, Mtxf *arg4, struct coord *velocity)
{
	struct defaultobj *obj = NULL;
	struct weaponfunc *basefunc;
	struct weaponfunc_throw *func;
	struct weapon *weapon = weaponFindById(gset->weaponnum);
	struct weaponobj *weaponobj;
	struct autogunobj *autogun;
	Mtxf mtx;
	s32 playernum;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(gset);
	const weapon_graph_projectile_runtime_t *projectilegraph =
		bgunGetProjectileGraph(graph);
	const weapon_graph_entity_runtime_t *entitygraph =
		bgunGetEntityGraph(graph);
	s32 projectilemodelnum;
	s32 activatetime60;

	if (weapon == NULL) {
		return false;
	}

	basefunc = weapon->functions[gset->weaponfunc];
	func = (struct weaponfunc_throw *) basefunc;

	if (func == NULL) {
		return false;
	}

	projectilemodelnum = bgunGetThrownModelnumFromGraph(graph,
		projectilegraph, entitygraph, func->projectilemodelnum);
	activatetime60 = bgunGetActivationTimeFromGraph(graph,
		projectilegraph, entitygraph, func->activatetime60);

	if (gset->weaponnum == WEAPON_COMBATKNIFE) {
		guRotateF(mtx.m, 90.0f / (RANDOMFRAC() + 12.1f),
				arg4->m[1][0], arg4->m[1][1], arg4->m[1][2]);
	} else {
		mtxLoadRandomRotation(&mtx);
	}

	/* c3849 Wave 5 Unit 7 (impact U5 transition, binding spec B4: the single
	 * :5294 laptop-gate widen). A CUSTOM thrown projectile whose record
	 * authored transition_to_entity routes into the laptopDeploy autogun
	 * backend when the referenced entity authored autogun (first production
	 * caller of weaponGraphRuntimeGetEntityForProjectile). transition_when:
	 * every authored value maps to this OG early instantiation (module spec
	 * sanctions). Base weapons never author transition records; the OG
	 * WEAPON_LAPTOPGUN clause stays first and verbatim. */
	{
		const weapon_graph_entity_runtime_t *transitionentity = NULL;

		if (gset->weaponnum >= WEAPON_CUSTOM_START && projectilegraph != NULL
				&& projectilegraph->has_transition_to_entity) {
			transitionentity =
				weaponGraphRuntimeGetEntityForProjectile(projectilegraph);

			if (transitionentity != NULL && !transitionentity->has_autogun) {
				/* non-autogun transition archetypes are deferred */
				transitionentity = NULL;
			}
		}

		if (gset->weaponnum == WEAPON_LAPTOPGUN || transitionentity != NULL) {
			autogun = laptopDeploy(projectilemodelnum, gset, chr);

			if (autogun != NULL) {
				obj = &autogun->base;
			}
		} else {
			weaponobj = weaponCreateProjectileFromGset(projectilemodelnum, gset, chr);

			if (weaponobj != NULL) {
				obj = &weaponobj->base;

				// Note this timer is converted to 240 time immediately below
				weaponobj->timer240 = activatetime60;

				if (weaponobj->timer240 >= 2) {
					weaponobj->timer240 = TICKS(weaponobj->timer240 * 4);
				}

				if (weaponobj->weaponnum == WEAPON_GRENADE || weaponobj->weaponnum == WEAPON_NBOMB) {
					propSetDangerous(weaponobj->base.prop);
				}

				if (projectilemodelnum == MODEL_CHRREMOTEMINE
						|| projectilemodelnum == MODEL_CHRTIMEDMINE
						|| projectilemodelnum == MODEL_CHRPROXIMITYMINE
						|| projectilemodelnum == MODEL_CHRECMMINE) {
					weaponobj->base.flags3 |= OBJFLAG3_00000008;
				}

				bgunApplyEntityGraphToWeapon(weaponobj, entitygraph);
			}
		}
	}

	if (obj != NULL) {
		bgun0f09ebcc(obj, pos, rooms, arg4, velocity, &mtx, chr->prop, pos);

		obj->hidden &= 0x0fffffff;

		if (g_Vars.normmplayerisrunning) {
			playernum = mpPlayerGetIndex(chr);
		} else {
			playernum = playermgrGetPlayerNumByProp(chr->prop);
		}

		obj->hidden |= playernum << 28;

		if (obj->hidden & OBJHFLAG_PROJECTILE) {
			obj->projectile->flags |= PROJECTILEFLAG_00000002;
			obj->projectile->unk08c = 0.1f;
			obj->projectile->pickuptimer240 = TICKS(240);
			projectileApplyGraphRuntime(obj, projectilegraph);

			psCreate(NULL, obj->prop, SFX_THROW, -1,
					-1, 0, 0, PSTYPE_NONE, NULL, -1, NULL, -1, -1, -1, -1);
		}
	}

	return obj;
}

/**
 * handnum supports some unusual values:
 *
 * 0 = right hand
 * 1 = left hand
 * 2 = fumbling grenade from right hand (due to nbomb)
 * 3 = fumbling grenade from left hand (actually not possible)
 */
struct defaultobj *bgunCreateThrownProjectile(s32 handnum, struct gset *gset)
{
	struct coord velocity = {0, 0, 0};
	Mtxf sp1f4;
	struct coord gunpos;
	struct coord gundir;
	struct prop *playerprop = g_Vars.currentplayer->prop;
	struct coord *prevpos = &g_Vars.currentplayer->bondprevpos;
	struct coord *extrapos = &g_Vars.currentplayer->bondextrapos;
	Mtxf sp190;
	struct defaultobj *obj;
	struct weaponobj *weapon;
	struct coord muzzlepos;
	struct coord spawnpos;
	RoomNum spawnrooms[8];
	bool droppinggrenade = false;
	struct hand *hand;
	struct coord aimpos;
	struct coord sp140;
	f32 frac;
	f32 radians;
	Mtxf spf8;
	Mtxf spb8;
	Mtxf sp78;
	f32 sp68[4];
	f32 sp58[4];
	f32 sp48[4];
	struct trainingdata *data;
	u32 stack;
	const weapon_graph_projectile_runtime_t *projectilegraph =
		bgunGetProjectileGraph(bgunGetHeldGraph(gset));

	// don't do anything if we're not the authority
	if (g_NetMode == NETMODE_CLIENT) {
		return NULL;
	}

	if (handnum >= 2) {
		droppinggrenade = true;
		handnum -= 2;
	}

	hand = g_Vars.currentplayer->hands + handnum;

	muzzlepos.x = g_Vars.currentplayer->hands[handnum].muzzlepos.x;
	muzzlepos.y = g_Vars.currentplayer->hands[handnum].muzzlepos.y;
	muzzlepos.z = g_Vars.currentplayer->hands[handnum].muzzlepos.z;

	mtx4LoadIdentity(&sp1f4);

	if (gset->weaponnum == WEAPON_COMBATKNIFE) {
		mtx4LoadZRotation(4.711639f, &sp1f4);
		mtx4LoadXRotation(3.1410925f, &sp190);
		mtx4MultMtx4InPlace(&sp190, &sp1f4);
	}

	mtx4Copy(&g_Vars.currentplayer->hands[handnum].muzzlemat, &sp190);

	guNormalize(&sp190.m[0][0], &sp190.m[0][1], &sp190.m[0][2]);
	guNormalize(&sp190.m[1][0], &sp190.m[1][1], &sp190.m[1][2]);
	guNormalize(&sp190.m[2][0], &sp190.m[2][1], &sp190.m[2][2]);

	sp190.m[3][0] = 0.0f;
	sp190.m[3][1] = 0.0f;
	sp190.m[3][2] = 0.0f;

	mtx4MultMtx4InPlace(&sp190, &sp1f4);

	playerSetPerimEnabled(playerprop, false);

	if (cdTestLos11(&playerprop->pos, playerprop->rooms, &muzzlepos, spawnrooms, CDTYPE_ALL) != CDRESULT_COLLISION) {
		spawnpos.x = muzzlepos.x;
		spawnpos.y = muzzlepos.y;
		spawnpos.z = muzzlepos.z;
	} else {
		spawnpos.x = playerprop->pos.x;
		spawnpos.y = playerprop->pos.y;
		spawnpos.z = playerprop->pos.z;

		roomsCopy(playerprop->rooms, spawnrooms);
	}

	playerSetPerimEnabled(playerprop, true);

	bgunCalculatePlayerShotSpread(&gunpos, &gundir, handnum, true);
	mtx4RotateVecInPlace(camGetProjectionMtxF(), &gundir);

	if (droppinggrenade) {
		// Dropping a grenade because player is in an nbomb storm
		velocity.x = gundir.x * 1.6666666f;
		velocity.y = gundir.y * 1.6666666f;
		velocity.z = gundir.z * 1.6666666f;
	} else if (gsetHasFunctionFlags(&hand->gset, FUNCFLAG_CALCULATETRAJECTORY)
			|| (projectilegraph && projectilegraph->calculate_trajectory)
			|| (projectilegraph && projectilegraph->has_trajectory_correction)) {
		/* c3849 guidance Slice B: the OG thrown clamp is 20 degrees
		 * (0.34901026f). trajectory_max_angle is authored in DEGREES (B6.2
		 * decision) and converts at use; 0/absent keeps the OG literal. */
		f32 maxclampangle = 0.34901026f;

		if (projectilegraph && projectilegraph->has_trajectory_correction
				&& projectilegraph->trajectory_max_angle > 0.0f) {
			maxclampangle = DEG2RAD(projectilegraph->trajectory_max_angle);
		}

		// Calculate the velocity based on the trajectory to the aimpos
		propFindAimingAt(HAND_RIGHT, false, FINDPROPCONTEXT_QUERY);

		if (hand->hasdotinfo) {
			aimpos.x = hand->dotpos.x;
			aimpos.y = hand->dotpos.y;
			aimpos.z = hand->dotpos.z;

			chrCalculateTrajectory(&spawnpos, 21.666666f, &aimpos, &sp140);

			radians = acosf(gundir.f[0] * sp140.f[0] + gundir.f[1] * sp140.f[1] + gundir.f[2] * sp140.f[2]);

			// Check within 20 degrees (or the authored clamp)
			if (radians > maxclampangle || radians < -maxclampangle) {
				mtx00016b58(&spf8, 0, 0, 0, gundir.x, gundir.y, gundir.z, 0, 1, 0);
				mtx00016b58(&spb8, 0, 0, 0, sp140.x, sp140.y, sp140.z, 0, 1, 0);

				quaternion0f097044(&spf8, sp68);
				quaternion0f097044(&spb8, sp58);
				quaternion0f0976c0(sp68, sp58);

				frac = maxclampangle / radians;

				if (frac < 0.0f) {
					frac = -frac;
				}

				quaternionSlerp(sp68, sp58, frac, sp48);
				quaternionToMtx(sp48, &sp78);

				gundir.x = -sp78.m[2][0];
				gundir.y = -sp78.m[2][1];
				gundir.z = -sp78.m[2][2];
			} else {
				gundir.x = sp140.x;
				gundir.y = sp140.y;
				gundir.z = sp140.z;
			}
		}

		velocity.x = gundir.x * 21.666666f;
		velocity.y = gundir.y * 21.666666f;
		velocity.z = gundir.z * 21.666666f;
	} else {
		// Simple velocity
		velocity.x = gundir.x * 16.666666f;
		velocity.y = gundir.y * 16.666666f;
		velocity.z = gundir.z * 16.666666f;

		if (gset->weaponnum == WEAPON_GRENADE || gset->weaponnum == WEAPON_NBOMB) {
			velocity.y += 1.6666666f;
		} else {
			velocity.y += 5.0f;
		}
	}

	if (gset->weaponnum == WEAPON_LAPTOPGUN) {
		bgunFreeWeapon(handnum);
	}

	// Add player movement to velocity
	if (g_Vars.lvupdate240 > 0) {
		velocity.x += (playerprop->pos.x - prevpos->x + extrapos->x) / g_Vars.lvupdate60freal;
		velocity.y += (playerprop->pos.y - prevpos->y + extrapos->y) / g_Vars.lvupdate60freal;
		velocity.z += (playerprop->pos.z - prevpos->z + extrapos->z) / g_Vars.lvupdate60freal;
	}

	obj = bgunCreateThrownProjectile2(g_Vars.currentplayer->prop->chr, gset, &spawnpos, spawnrooms, &sp1f4, &velocity);

	if (obj) {
		if (obj->type == OBJTYPE_WEAPON) {
			weapon = (struct weaponobj *)obj;

			if (gset->weaponnum == WEAPON_GRENADE && gset->weaponfunc == FUNC_PRIMARY) {
				if (weapon->timer240 < hand->primetimer60 * 4) {
					weapon->timer240 = 0;
				} else {
					weapon->timer240 -= hand->primetimer60 * 4;
				}

				weapon->gunfunc = gset->weaponfunc;
			} else if (gset->weaponnum == WEAPON_ECMMINE && g_Vars.stagenum == STAGE_CITRAINING) {
				data = dtGetData();

				if (data->intraining) {
					data->obj = obj;
				}
			}
		}

		if (obj->hidden & OBJHFLAG_PROJECTILE) {
			obj->projectile->flags |= PROJECTILEFLAG_LAUNCHING;
			obj->projectile->nextsteppos.x = muzzlepos.x;
			obj->projectile->nextsteppos.y = muzzlepos.y;
			obj->projectile->nextsteppos.z = muzzlepos.z;

			if (gset->weaponnum == WEAPON_GRENADE && gset->weaponfunc == FUNC_SECONDARY) {
				obj->projectile->unk08c = 1.0f;
			}

			if (gset->weaponnum == WEAPON_COMBATKNIFE) {
				// In theory, weapon can be uninitialised here,
				// but in practice it's always set.
				weapon->base.projectile->flags |= PROJECTILEFLAG_00000002;
				weapon->base.projectile->unk08c = 0.1f;
				weapon->base.projectile->pickuptimer240 = TICKS(240);
				weapon->base.hidden |= OBJHFLAG_THROWNKNIFE;
			}
		}

		if (obj && g_NetMode == NETMODE_SERVER) {
			netmsgSvcPropSpawnWrite(&g_NetMsgRel, obj->prop);
			netmsgSvcPropMoveWrite(&g_NetMsgRel, obj->prop, NULL);
		}
	}

	return obj;
}

void bgunUpdateHeldRocket(s32 handnum)
{
	struct hand *hand = &g_Vars.currentplayer->hands[handnum];
	struct defaultobj *obj = &hand->rocket->base;

	if (obj) {
		struct prop *objprop = obj->prop;
		Mtxf mtx;

		if (objprop) {
			struct prop *playerprop = g_Vars.currentplayer->prop;
			struct model *model = obj->model;

			if (!hand->firedrocket) {
				mtx4Copy(&hand->posmtx, &mtx);

				mtx.m[3][0] = 0;
				mtx.m[3][1] = 0;
				mtx.m[3][2] = 0;

				mtx00015f04(obj->model->scale, &mtx);
				func0f06a580(obj, &hand->muzzlepos, &mtx, playerprop->rooms);
				propDeregisterRooms(objprop);
			}

			model->matrices = gfxAllocate(model->definition->nummatrices * sizeof(Mtxf));

			mtx4Copy(&hand->muzzlemat, &model->matrices[0]);
			modelUpdateRelationsQuick(model, model->definition->rootnode);

			objprop->flags |= PROPFLAG_ONANYSCREENTHISTICK | PROPFLAG_ONTHISSCREENTHISTICK;
			objprop->z = -model->matrices[0].m[3][2];
		}
	}
}

void bgunCreateHeldRocket(s32 handnum, struct weaponfunc_shootprojectile *func)
{
	struct hand *hand = &g_Vars.currentplayer->hands[handnum];
	struct weaponobj *obj;
	const weapon_graph_held_function_t *graph = bgunGetHeldGraph(&hand->gset);
	const weapon_graph_projectile_runtime_t *projectilegraph =
		bgunGetProjectileGraph(graph);
	s32 projectilemodelnum = bgunGetProjectileModelnumFromGraph(graph,
		projectilegraph, func->projectilemodelnum);

	if (hand->rocket == NULL) {
#if VERSION >= VERSION_NTSC_1_0
		hand->firedrocket = false;
#endif

		obj = weaponCreateProjectileFromWeaponNum(projectilemodelnum, WEAPON_ROCKET, g_Vars.currentplayer->prop->chr);

		if (obj != NULL) {
			hand->rocket = obj;
			hand->firedrocket = false;

			obj->timer240 = 1;
			projectileApplyGraphRuntime(&obj->base, projectilegraph);
#if VERSION >= VERSION_NTSC_1_0
			obj->base.flags |= OBJFLAG_HELDROCKET;
#endif
			obj->base.flags2 |= OBJFLAG2_THROWTHROUGH;
		}
	}
}

static ggv_value bgunHeldRocketRead(void *opaque, ggv_binding binding)
{
    struct hand *hand = opaque; ggv_value value = {0};
    if (binding == GGV_HELD_ROCKET) value.ref = hand->rocket;
    return value;
}
static void bgunHeldRocketWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct hand *hand = opaque;
    if (binding == GGV_HELD_ROCKET) hand->rocket = (struct weaponobj *)value.ref;
}
static ggv_value bgunHeldRocketCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)b; (void)d;
    if (service == GGV_FREE_ROCKET_OBJECT) {
        objFreePermanently(&((struct weaponobj *)a.ref)->base, true);
    }
    return value;
}
void bgunFreeHeldRocket(s32 handnum)
{
    struct hand *hand = &g_Vars.currentplayer->hands[handnum];
    ggv_host host = { hand, bgunHeldRocketRead, bgunHeldRocketWrite, bgunHeldRocketCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_FREE_HELD_ROCKET), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: held rocket release refused immutable program");
    }
}

void bgunCreateFiredProjectile(s32 handnum)
{
	struct weapon *weapondef;
	struct hand *hand;
	Mtxf sp270;
	struct coord sp264;
	f32 sp260;
	f32 sp25c;
	struct coord sp250;
	Mtxf sp210;
	struct coord gunpos;
	struct coord gundir;
	struct prop *playerprop;
	struct coord *prevpos;
	struct coord *extrapos;
	struct coord spawnpos;
	struct weaponobj *weapon;
	struct weaponfunc *tmp;
	struct weaponfunc_shootprojectile *funcdef;
	struct coord aimpos;
	struct coord sp1bc;
	f32 frac;
	f32 radians;
	Mtxf sp174;
	Mtxf sp134;
	Mtxf spf4;
	f32 spe4[4];
	f32 spd4[4];
	f32 spc4[4];
	const weapon_graph_held_function_t *graph;
	u32 funcflags;
	s32 projectilemodelnum;
	f32 projectilescale;
	f32 projectilespeed;
	s32 traveldist;
	s32 timer60;
	f32 reflectangle;
	s32 soundnum;
	const weapon_graph_projectile_runtime_t *projectilegraph;

	if (g_NetMode == NETMODE_CLIENT) {
		return;
	}

	hand = g_Vars.currentplayer->hands + handnum;

	playerprop = g_Vars.currentplayer->prop;
	prevpos = &g_Vars.currentplayer->bondprevpos;
	extrapos = &g_Vars.currentplayer->bondextrapos;

	weapondef = weaponFindById(hand->gset.weaponnum);

	if (weapondef) {
			tmp = weapondef->functions[hand->gset.weaponfunc];

		if (tmp && tmp->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
			funcdef = (struct weaponfunc_shootprojectile *)tmp;
			graph = bgunGetHeldGraph(&hand->gset);
			projectilegraph = bgunGetProjectileGraph(graph);
			funcflags = bgunGetProjectileFlagsFromGraph(
				funcdef->base.base.flags, graph, projectilegraph);
			projectilemodelnum = bgunGetProjectileModelnumFromGraph(
				graph, projectilegraph, funcdef->projectilemodelnum);
			projectilescale = bgunGetProjectileScaleFromGraph(
				graph, projectilegraph, funcdef->scale);
			projectilespeed = bgunGetProjectileSpeedFromGraph(
				graph, projectilegraph, funcdef->speed);
			traveldist = bgunGetProjectileTravelFromGraph(
				graph, projectilegraph, funcdef->traveldist);
			timer60 = bgunGetProjectileTimerFromGraph(
				graph, projectilegraph, funcdef->timer60);
			reflectangle = bgunGetProjectileReflectFromGraph(
				graph, projectilegraph, funcdef->reflectangle);
			soundnum = bgunGetProjectileSoundFromGraph(graph, funcdef->soundnum);

			mtx4LoadIdentity(&sp270);
			bgunCalculatePlayerShotSpread(&gunpos, &gundir, handnum, true);
			mtx4RotateVecInPlace(camGetProjectionMtxF(), &gundir);

			spawnpos.x = hand->muzzlepos.x;
			spawnpos.y = hand->muzzlepos.y;
			spawnpos.z = hand->muzzlepos.z;

			if (hand->gset.weaponnum == WEAPON_SLAYER && hand->gset.weaponfunc == FUNC_SECONDARY) {
				spawnpos.x += 50.0f * gundir.x;
				spawnpos.y += 50.0f * gundir.y;
				spawnpos.z += 50.0f * gundir.z;
			}

			sp260 = projectilespeed * 1.6666666f / 60.0f;
			sp25c = traveldist * 1.6666666f;

			if (gsetHasFunctionFlags(&hand->gset, FUNCFLAG_CALCULATETRAJECTORY)
					|| (funcflags & FUNCFLAG_CALCULATETRAJECTORY)) {
				/* c3849 guidance Slice B: the OG fired clamp is 10 degrees
				 * (0.17450513f). trajectory_max_angle is authored in DEGREES
				 * (B6.2 decision); 0/absent keeps the OG literal. */
				f32 maxclampangle = 0.17450513f;

				if (projectilegraph && projectilegraph->has_trajectory_correction
						&& projectilegraph->trajectory_max_angle > 0.0f) {
					maxclampangle = DEG2RAD(projectilegraph->trajectory_max_angle);
				}

				propFindAimingAt(HAND_RIGHT, false, FINDPROPCONTEXT_QUERY);

				if (hand->hasdotinfo) {
					aimpos.x = hand->dotpos.x;
					aimpos.y = hand->dotpos.y;
					aimpos.z = hand->dotpos.z;

					chrCalculateTrajectory(&spawnpos, sp25c, &aimpos, &sp1bc);

					radians = acosf(gundir.f[0] * sp1bc.f[0] + gundir.f[1] * sp1bc.f[1] + gundir.f[2] * sp1bc.f[2]);

					if (radians > maxclampangle || radians < -maxclampangle) {
						mtx00016b58(&sp174, 0.0f, 0.0f, 0.0f, gundir.x, gundir.y, gundir.z, 0.0f, 1.0f, 0.0f);
						mtx00016b58(&sp134, 0.0f, 0.0f, 0.0f, sp1bc.x, sp1bc.y, sp1bc.z, 0.0f, 1.0f, 0.0f);

						quaternion0f097044(&sp174, spe4);
						quaternion0f097044(&sp134, spd4);
						quaternion0f0976c0(spe4, spd4);

						frac = maxclampangle / radians;

						if (frac < 0.0f) {
							frac = -frac;
						}

						quaternionSlerp(spe4, spd4, frac, spc4);
						quaternionToMtx(spc4, &spf4);

						gundir.x = -spf4.m[2][0];
						gundir.y = -spf4.m[2][1];
						gundir.z = -spf4.m[2][2];
					} else {
						gundir.x = sp1bc.x;
						gundir.y = sp1bc.y;
						gundir.z = sp1bc.z;
					}
				}
			}

			sp250.x = gundir.x * sp260;
			sp250.y = gundir.y * sp260;
			sp250.z = gundir.z * sp260;

			sp264.x = sp250.f[0] * g_Vars.lvupdate60freal + gundir.f[0] * sp25c;
			sp264.y = sp250.f[1] * g_Vars.lvupdate60freal + gundir.f[1] * sp25c;
			sp264.z = sp250.f[2] * g_Vars.lvupdate60freal + gundir.f[2] * sp25c;

			if ((funcflags & FUNCFLAG_FLYBYWIRE) == 0 && g_Vars.lvupdate240 > 0) {
				sp264.x += (playerprop->pos.x - prevpos->x + extrapos->x) / g_Vars.lvupdate60freal;
				sp264.y += (playerprop->pos.y - prevpos->y + extrapos->y) / g_Vars.lvupdate60freal;
				sp264.z += (playerprop->pos.z - prevpos->z + extrapos->z) / g_Vars.lvupdate60freal;
			}

			mtx4Copy(&g_Vars.currentplayer->hands[handnum].posmtx, &sp210);

			sp210.m[3][0] = 0.0f;
			sp210.m[3][1] = 0.0f;
			sp210.m[3][2] = 0.0f;

			if (hand->rocket) {
				hand->firedrocket = true;

				weapon = hand->rocket;
				weapon->base.flags2 &= ~OBJFLAG2_THROWTHROUGH;
#if VERSION >= VERSION_NTSC_1_0
				weapon->base.flags &= ~OBJFLAG_HELDROCKET;
#endif

				if (funcflags & FUNCFLAG_HOMINGROCKET) {
					weapon->weaponnum = WEAPON_HOMINGROCKET;
				}
			} else if (hand->gset.weaponnum == WEAPON_ROCKETLAUNCHER || hand->gset.weaponnum == WEAPON_SLAYER) {
				u32 stack;
				s32 weaponnum = WEAPON_ROCKET;

				if (funcflags & FUNCFLAG_HOMINGROCKET) {
					weaponnum = WEAPON_HOMINGROCKET;
				}

				weapon = weaponCreateProjectileFromWeaponNum(projectilemodelnum, weaponnum, g_Vars.currentplayer->prop->chr);
			} else if (hand->gset.weaponnum == WEAPON_CROSSBOW) {
				weapon = weaponCreateProjectileFromWeaponNum(projectilemodelnum, WEAPON_BOLT, g_Vars.currentplayer->prop->chr);

				if (weapon) {
					weapon->gunfunc = hand->gset.weaponfunc;
				}
			} else if (hand->gset.weaponnum == WEAPON_DEVASTATOR) {
				weapon = weaponCreateProjectileFromWeaponNum(projectilemodelnum, WEAPON_GRENADEROUND, g_Vars.currentplayer->prop->chr);

				if (weapon) {
					weapon->gunfunc = hand->gset.weaponfunc;
				}
			} else if (hand->gset.weaponnum == WEAPON_SUPERDRAGON) {
				weapon = weaponCreateProjectileFromWeaponNum(projectilemodelnum, WEAPON_GRENADEROUND, g_Vars.currentplayer->prop->chr);

				if (weapon) {
					weapon->gunfunc = FUNC_2;
				}
			} else {
				weapon = weaponCreateProjectileFromGset(projectilemodelnum, &hand->gset, g_Vars.currentplayer->prop->chr);
			}

			if (weapon) {
#if VERSION >= VERSION_NTSC_1_0
				bool failed = false;
				Mtxf sp78;
				struct coord sp6c;
				struct coord sp60;

				if (weapon->base.model && weapon->base.model->definition) {
					weapon->timer240 = timer60;

					if (weapon->timer240 != -1) {
						weapon->timer240 = TICKS(weapon->timer240 * 4);
					}

					weapon->base.hidden &= 0x0fffffff;
					weapon->base.hidden |= g_Vars.currentplayernum << 28;

					bgun0f09ed2c(&weapon->base, &spawnpos, &sp210, &sp264, &sp270);

					if (weapon->base.hidden & OBJHFLAG_PROJECTILE) {
						if (funcflags & FUNCFLAG_PROJECTILE_LIGHTWEIGHT) {
							weapon->base.projectile->flags |= PROJECTILEFLAG_LIGHTWEIGHT;
						} else if (funcflags & FUNCFLAG_PROJECTILE_POWERED) {
							weapon->base.projectile->flags |= PROJECTILEFLAG_POWERED;
						}

						weapon->base.projectile->targetprop = g_Vars.currentplayer->trackedprops[0].prop;

						if (projectilescale != 1.0f) {
							weapon->base.model->scale *= projectilescale;

							mtx3ToMtx4(weapon->base.realrot, &sp78);
							mtx00015f04(projectilescale, &sp78);
							mtx4ToMtx3(&sp78, weapon->base.realrot);
						}

						weapon->base.projectile->powerlimit240 = TICKS(1200);
						weapon->base.projectile->unk0a8 = weapon->base.prop->pos.y;
						weapon->base.projectile->unk0ac = weapon->base.projectile->speed.y;
						weapon->base.projectile->unk010 = sp250.x;
						weapon->base.projectile->unk014 = sp250.y;
						weapon->base.projectile->unk018 = sp250.z;
						weapon->base.projectile->pickuptimer240 = TICKS(240);
						weapon->base.projectile->unk08c = reflectangle;
						weapon->base.projectile->unk098 = funcdef->unk50 * 1.6666666f;
						projectileApplyGraphRuntime(&weapon->base, projectilegraph);

						if (soundnum > 0) {
							psCreate(NULL, weapon->base.prop, soundnum, -1, -1, 0, 0, PSTYPE_NONE, 0, -1.0f, 0, -1, -1.0f, -1.0f, -1.0f);
						}

						if (funcflags & FUNCFLAG_FLYBYWIRE) {
							playerLaunchSlayerRocket(weapon);
						}

						if (weapon->base.projectile->flags & PROJECTILEFLAG_LAUNCHING) {
							projectileLaunch(&weapon->base, weapon->base.projectile, &sp6c, &sp60);
						}
					} else {
						failed = true;
					}
				} else {
					failed = true;
				}

				if (failed) {
					weapon->timer240 = -1;

					if (weapon->base.prop) {
						propFree(weapon->base.prop);
					}

					if (weapon->base.model) {
						modelmgrFreeModel(weapon->base.model);
					}

					weapon->base.prop = NULL;
					weapon->base.model = NULL;
				}
				else if (g_NetMode == NETMODE_SERVER) {
					netmsgSvcPropSpawnWrite(&g_NetMsgRel, weapon->base.prop);
					netmsgSvcPropMoveWrite(&g_NetMsgRel, weapon->base.prop, NULL);
				}
#else
				// NTSC beta doesn't have any of the failure checks
				Mtxf sp78;
				struct coord sp6c;
				struct coord sp60;

				weapon->timer240 = timer60;

				if (weapon->timer240 != -1) {
					weapon->timer240 = TICKS(weapon->timer240 * 4);
				}

				weapon->base.hidden &= 0x0fffffff;
				weapon->base.hidden |= g_Vars.currentplayernum << 28;

				bgun0f09ed2c(&weapon->base, &spawnpos, &sp210, &sp264, &sp270);

				if (weapon->base.hidden & OBJHFLAG_PROJECTILE) {
					if (funcflags & FUNCFLAG_PROJECTILE_LIGHTWEIGHT) {
						weapon->base.projectile->flags |= PROJECTILEFLAG_LIGHTWEIGHT;
					} else if (funcflags & FUNCFLAG_PROJECTILE_POWERED) {
						weapon->base.projectile->flags |= PROJECTILEFLAG_POWERED;
					}

					weapon->base.projectile->targetprop = g_Vars.currentplayer->trackedprops[0].prop;

					if (projectilescale != 1.0f) {
						weapon->base.model->scale *= projectilescale;

						mtx3ToMtx4(weapon->base.realrot, &sp78);
						mtx00015f04(projectilescale, &sp78);
						mtx4ToMtx3(&sp78, weapon->base.realrot);
					}

					weapon->base.projectile->powerlimit240 = TICKS(1200);
					weapon->base.projectile->unk0a8 = weapon->base.prop->pos.y;
					weapon->base.projectile->unk0ac = weapon->base.projectile->speed.y;
					weapon->base.projectile->unk010 = sp250.x;
					weapon->base.projectile->unk014 = sp250.y;
					weapon->base.projectile->unk018 = sp250.z;
					weapon->base.projectile->pickuptimer240 = TICKS(240);
					weapon->base.projectile->unk08c = reflectangle;
					weapon->base.projectile->unk098 = funcdef->unk50 * 1.6666666f;
					projectileApplyGraphRuntime(&weapon->base, projectilegraph);

					if (soundnum > 0) {
						psCreate(NULL, weapon->base.prop, soundnum, -1, -1, 0, 0, PSTYPE_NONE, 0, -1.0f, 0, -1, -1.0f, -1.0f, -1.0f);
					}

					if (funcflags & FUNCFLAG_FLYBYWIRE) {
						playerLaunchSlayerRocket(weapon);
					}

					if (weapon->base.projectile->flags & PROJECTILEFLAG_LAUNCHING) {
						projectileLaunch(&weapon->base, weapon->base.projectile, &sp6c, &sp60);
					}
				}
#endif
			}
		}
	}
}

void bgunSwivel(f32 screenx, f32 screeny, f32 crossdamp, f32 aimdamp)
{
	f32 screenwidth = camGetScreenWidth();
	f32 screenheight = camGetScreenHeight();
	struct player *player = g_Vars.currentplayer;
	struct coord aimpos;
	s32 l;
	s32 h;
	f32 x[2];
	f32 y[2];
	bool ignore[2] = {false, false};
	s32 numframes;
	struct hand *hand;
	struct coord sp94;
	f32 sp8c[2];

	x[HAND_RIGHT] = x[HAND_LEFT] = screenx;
	y[HAND_RIGHT] = y[HAND_LEFT] = screeny;

	ignore[HAND_LEFT] = !player->hands[HAND_LEFT].inuse;
	ignore[HAND_RIGHT] = !player->hands[HAND_RIGHT].inuse;

	// If using right hand only and reloading,
	// recentre until the reload animation is almost complete
	if (!player->hands[HAND_LEFT].inuse
			&& player->hands[HAND_RIGHT].state == HANDSTATE_RELOAD
			&& player->hands[HAND_RIGHT].unk0ce8) {
		numframes = 25;

		if (player->hands[HAND_RIGHT].gset.weaponnum == WEAPON_CROSSBOW) {
			numframes = 5;
		}

		if ((s32)bgun0f09815c(&player->hands[HAND_RIGHT]) < modelGetNumAnimFrames(&player->hands[HAND_RIGHT].gunmodel) - numframes) {
			x[HAND_RIGHT] = 0.0f;
			y[HAND_RIGHT] = 0.0f;
			ignore[HAND_RIGHT] = true;
		}
	}

	if (player->hands[HAND_LEFT].gset.weaponnum == WEAPON_REMOTEMINE) {
		x[HAND_LEFT] = g_Vars.currentplayer->speedtheta * 0.3f + g_Vars.currentplayer->gunextraaimx;
		y[HAND_LEFT] = -g_Vars.currentplayer->speedverta * 0.1f + g_Vars.currentplayer->gunextraaimy;
		ignore[HAND_LEFT] = true;
	}

	if (player->hands[HAND_RIGHT].gset.weaponnum == WEAPON_UNARMED) {
		x[HAND_RIGHT] = g_Vars.currentplayer->speedtheta * 0.3f + g_Vars.currentplayer->gunextraaimx;
		y[HAND_RIGHT] = -g_Vars.currentplayer->speedverta * 0.1f + g_Vars.currentplayer->gunextraaimy;
		ignore[HAND_RIGHT] = true;
	}

	if (g_Vars.currentplayer->teleportstate != TELEPORTSTATE_INACTIVE) {
		ignore[HAND_LEFT] = ignore[HAND_RIGHT] = true;
	}

	// This loop only iterates once
	for (h = 0; h < 1; h++) {
		if (!ignore[h]) {
			hand = &player->hands[h];

			if (hand->hasdotinfo && !g_Vars.mplayerisrunning) {
				sp94.x = hand->dotpos.x;
				sp94.y = hand->dotpos.y;
				sp94.z = hand->dotpos.z;

				mtx4TransformVecInPlace(camGetWorldToScreenMtxf(), &sp94);

				if (!(sp94.z < 0.0000001f) || !(sp94.z > -0.0000001f)) {
					if (sp94.z > -6000.0f) {
						cam0f0b4d04(&sp94, sp8c);

						x[h] = sp8c[0];
						y[h] = sp8c[1];

						x[h] = 2.0f * (x[h] / viGetViewWidth()) - 1.0f;
						y[h] = 2.0f * (y[h] / viGetViewHeight()) - 1.0f;
					}
				}
			}
		}
	}

	player->oldcrosspos[0] = player->crosspos[0];
	player->oldcrosspos[1] = player->crosspos[1];

	if (crossdamp != player->guncrossdamp) {
		player->crosspossum[0] = player->crosspossum[0] * (1.0f - player->guncrossdamp) / (1.0f - crossdamp);
		player->crosspossum[1] = player->crosspossum[1] * (1.0f - player->guncrossdamp) / (1.0f - crossdamp);
		player->guncrossdamp = crossdamp;
	}

	if (aimdamp != player->gunaimdamp) {
		player->crosssum2[0] = player->crosssum2[0] * (1.0f - player->gunaimdamp) / (1.0f - aimdamp);
		player->crosssum2[1] = player->crosssum2[1] * (1.0f - player->gunaimdamp) / (1.0f - aimdamp);
		player->gunaimdamp = aimdamp;
	}

	for (l = 0; l < g_Vars.lvupdate240; l++) {
		player->crosspossum[0] = player->crosspossum[0] * crossdamp + screenx;
		player->crosspossum[1] = player->crosspossum[1] * crossdamp + screeny;

		for (h = 0; h < 2; h++) {
			hand = &player->hands[h];
			hand->guncrosspossum[0] = (PAL ? 0.913f : 0.9269697f) * hand->guncrosspossum[0] + x[h];
			hand->guncrosspossum[1] = (PAL ? 0.913f : 0.9269697f) * hand->guncrosspossum[1] + y[h];
		}
	}

	player->crosspos[0] = player->crosspossum[0] * (1.0f - crossdamp) * screenwidth * 0.5f + screenwidth * 0.5f;
	player->crosspos[1] = player->crosspossum[1] * (1.0f - crossdamp) * screenheight * 0.5f + screenheight * 0.5f;

	if (player->crosspos[0] < 3.0f) {
		player->crosspos[0] = 3.0f;
	} else if (player->crosspos[0] > screenwidth - 4.0f) {
		player->crosspos[0] = screenwidth - 4.0f;
	}

	if (player->crosspos[1] < 3.0f) {
		player->crosspos[1] = 3.0f;
	} else if (player->crosspos[1] > screenheight - 4.0f) {
		player->crosspos[1] = screenheight - 4.0f;
	}

	player->crosspos[0] += camGetScreenLeft();
	player->crosspos[1] += camGetScreenTop();

	for (h = 0; h < 2; h++) {
		player->hands[h].crosspos[0] = player->hands[h].guncrosspossum[0] * (PAL ? 0.08700001f : 0.07303029f) * screenwidth * 0.5f + screenwidth * 0.5f;
		player->hands[h].crosspos[1] = player->hands[h].guncrosspossum[1] * (PAL ? 0.08700001f : 0.07303029f) * screenheight * 0.5f + screenheight * 0.5f;

		if (player->hands[h].crosspos[0] < 3.0f) {
			player->hands[h].crosspos[0] = 3.0f;
		} else if (player->hands[h].crosspos[0] > screenwidth - 4.0f) {
			player->hands[h].crosspos[0] = screenwidth - 4.0f;
		}

		if (player->hands[h].crosspos[1] < 3.0f) {
			player->hands[h].crosspos[1] = 3.0f;
		} else if (player->hands[h].crosspos[1] > screenheight - 4.0f) {
			player->hands[h].crosspos[1] = screenheight - 4.0f;
		}

		player->hands[h].crosspos[0] += camGetScreenLeft();
		player->hands[h].crosspos[1] += camGetScreenTop();
	}

	for (l = 0; l < g_Vars.lvupdate240; l++) {
		player->crosssum2[0] = player->crosssum2[0] * aimdamp + screenx;
		player->crosssum2[1] = player->crosssum2[1] * aimdamp + screeny;
	}

	player->crosspos2[0] = player->crosssum2[0] * (1.0f - aimdamp) * screenwidth * 0.5f + screenwidth * 0.5f;
	player->crosspos2[1] = player->crosssum2[1] * (1.0f - aimdamp) * screenheight * 0.5f + screenheight * 0.5f;
	player->crosspos2[0] += camGetScreenLeft();
	player->crosspos2[1] += camGetScreenTop();

	cam0f0b4c3c(player->crosspos2, &aimpos, 1000);

	bgunSetAimPos(&aimpos);
}

/**
 * Swivel the gun towards the given screen coordinates, dampening the movement
 * speed as it reaches the target.
 *
 * This is used for auto aim, the CMP's follow lock-on, and general turning.
 */
void bgunSwivelWithDamp(f32 screenx, f32 screeny, f32 crossdamp)
{
	struct weapon *weapon = weaponFindById(bgunGetWeaponNum(HAND_RIGHT));
	f32 aimdamp = PAL ? weapon->aimsettings->aimdamppal : weapon->aimsettings->aimdamp;

	if (aimdamp < crossdamp) {
		aimdamp = crossdamp;
	}

	bgunSwivel(screenx, screeny, crossdamp, aimdamp);
}

/**
 * Swivel the gun towards the given screen coordinates without slowing the speed
 * speed as it reaches the target.
 *
 * This is used when manual aiming.
 */
void bgunSwivelWithoutDamp(f32 screenx, f32 screeny)
{
	struct weapon *weapon = weaponFindById(bgunGetWeaponNum(HAND_RIGHT));
	f32 aimdamp = PAL ? weapon->aimsettings->aimdamppal : weapon->aimsettings->aimdamp;

	bgunSwivel(screenx, screeny, PAL ? 0.935f : 0.945f, aimdamp);
}

void bgunGetCrossPos(f32 *x, f32 *y)
{
	struct player *player = g_Vars.currentplayer;

	*x = player->crosspos[0];
	*y = player->crosspos[1];
}

void bgun0f0a0c08(struct coord *arg0, struct coord *arg1)
{
	arg0->x = 0;
	arg0->y = 0;
	arg0->z = 0;

	cam0f0b4c3c(g_Vars.currentplayer->crosspos, arg1, 1);
}

void bgun0f0a0c44(s32 handnum, struct coord *arg1, struct coord *arg2)
{
	arg1->x = 0;
	arg1->y = 0;
	arg1->z = 0;

	cam0f0b4c3c(g_Vars.currentplayer->hands[handnum].crosspos, arg2, 1);
}

f32 bgunGetPlayerShotSpreadFromGset(struct gset *accepted, s32 burstbullets)
{
	struct weaponfunc *func = accepted ? gsetGetWeaponFunction(accepted) : NULL;
	struct invaimsettings *aim = accepted ? gsetGetAimSettings(accepted) : NULL;
	if (!func || (func->type & 0xff) != INVENTORYFUNCTYPE_SHOOT || !aim) return -1;
	f32 spread = ((struct weaponfunc_shoot *)func)->spread;
	if ((aim->flags & INVAIMFLAG_ACCURATESINGLESHOT) && burstbullets == 1) spread *= 0.25f;
	return spread;
}

static void bgunCalculatePlayerShotSpreadSource(struct coord *gunpos2d, struct coord *gundir2d,
		s32 handnum, bool dorandom, struct gset *accepted, s32 burstbullets)
{
	f32 crosspos[2];
	f32 spread = accepted ? bgunGetPlayerShotSpreadFromGset(accepted, burstbullets) : 0;
	f32 scaledspread;
	f32 randfactor;
	struct weaponfunc *func = accepted ? NULL : currentPlayerGetWeaponFunction(handnum);
	struct player *player = g_Vars.currentplayer;

	if (func != NULL && (func->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
		struct weaponfunc_shoot *shootfunc = (struct weaponfunc_shoot *) func;
		spread = shootfunc->spread;
	}

	if (!accepted && weaponHasAimFlag(bgunGetWeaponNum2(handnum), INVAIMFLAG_ACCURATESINGLESHOT)
			&& player->hands[handnum].burstbullets == 1) {
		spread *= 0.25f;
	}

	// Decrease spread if double crouched
	if (bmoveGetCrouchPos() == CROUCHPOS_SQUAT) {
		spread *= 0.5f;
	}

	// Increase spread if dual wielding
	if (player->hands[HAND_LEFT].inuse) {
		spread *= 1.5f;
	}

	scaledspread = 120.0f * spread / viGetFovY();

	if (dorandom) {
		randfactor = (RANDOMFRAC() - 0.5f) * RANDOMFRAC();
	} else {
		randfactor = 0;
	}

	crosspos[0] = player->crosspos[0] + randfactor * scaledspread * camGetScreenWidth()
		/ (viGetHeight() * camGetPerspAspect());

	if (dorandom) {
		randfactor = (RANDOMFRAC() - 0.5f) * RANDOMFRAC();
	} else {
		randfactor = 0;
	}

	crosspos[1] = player->crosspos[1] + (randfactor * scaledspread * camGetScreenHeight())
		/ viGetHeight();

	gunpos2d->x = 0;
	gunpos2d->y = 0;
	gunpos2d->z = 0;

	cam0f0b4c3c(crosspos, gundir2d, 1);
}

void bgunCalculatePlayerShotSpread(struct coord *gunpos2d, struct coord *gundir2d, s32 handnum, bool dorandom)
{
	bgunCalculatePlayerShotSpreadSource(gunpos2d, gundir2d, handnum, dorandom, NULL, 0);
}

int bgunCalculatePlayerShotSpreadFromGset(struct coord *gunpos2d, struct coord *gundir2d,
		s32 handnum, bool dorandom, struct gset *accepted, s32 burstbullets)
{
	struct weaponfunc *func = accepted ? gsetGetWeaponFunction(accepted) : NULL;
	if (!func || (func->type & 0xff) != INVENTORYFUNCTYPE_SHOOT || !gsetGetAimSettings(accepted)) return 0;
	bgunCalculatePlayerShotSpreadSource(gunpos2d, gundir2d, handnum, dorandom, accepted, burstbullets);
	return 1;
}

void bgunCalculateBotShotSpread(struct coord *arg0, s32 weaponnum, s32 funcnum, bool arg3, s32 crouchpos, bool dual)
{
	f32 spread = 0.0f;
	f32 radius;
	struct weapon *weapondef = weaponFindById(weaponnum);
	f32 x;
	f32 y;
	Mtxf mtx;
	struct coord sp48;
	u32 stack;

	if (weapondef) {
		struct weaponfunc *funcdef = weapondef->functions[funcnum];

		if (funcdef && (funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
			struct weaponfunc_shoot *shootfunc = (struct weaponfunc_shoot *)funcdef;
			spread = shootfunc->spread;
		}
	}

	if (arg3 && weaponHasAimFlag(weaponnum, INVAIMFLAG_ACCURATESINGLESHOT)) {
		spread *= 0.25f;
	}

	if (crouchpos == CROUCHPOS_SQUAT) {
		spread *= 0.5f;
	}

	if (dual) {
		spread *= 1.5f;
	}

	radius = 120.0f * spread / viGetFovY();
	x = (RANDOMFRAC() - 0.5f) * RANDOMFRAC() * radius;
	y = (RANDOMFRAC() - 0.5f) * RANDOMFRAC() * radius;

	sp48.x = g_Vars.currentplayer->c_scalex * x;
	sp48.y = g_Vars.currentplayer->c_scaley * y;
	sp48.z = -1.0f;

	guNormalize(&sp48.x, &sp48.y, &sp48.z);
	mtx00016b58(&mtx, 0.0f, 0.0f, 0.0f, arg0->x, arg0->y, arg0->z, 0.0f, -1.0f, 0.0f);
	mtx4RotateVec(&mtx, &sp48, arg0);
}

bool bgunGetLastShootInfo(struct coord *pos, struct coord *dir, s32 handnum)
{
	struct hand *hand = &g_Vars.currentplayer->hands[handnum];

	if (!hand->lastdirvalid) {
		return false;
	}

	pos->x = hand->lastshootpos.x;
	pos->y = hand->lastshootpos.y;
	pos->z = hand->lastshootpos.z;

	dir->x = hand->lastshootdir.x;
	dir->y = hand->lastshootdir.y;
	dir->z = hand->lastshootdir.z;

	return true;
}

void bgunSetLastShootInfo(struct coord *pos, struct coord *dir, s32 handnum)
{
	struct hand *hand = &g_Vars.currentplayer->hands[handnum];

	hand->lastdirvalid = true;

	hand->lastshootpos.x = pos->x;
	hand->lastshootpos.y = pos->y;
	hand->lastshootpos.z = pos->z;

	hand->lastshootdir.x = dir->x;
	hand->lastshootdir.y = dir->y;
	hand->lastshootdir.z = dir->z;
}

s32 bgunGetShotsToTake(s32 handnum)
{
	struct hand *hand = &g_Vars.currentplayer->hands[handnum];

	return hand->shotstotake;
}

struct bguncleancontext { struct player *player; struct hand *hand; s32 handnum, slot, reloadIndex; s32 *reserve; };
static ggv_value bgunCleanRead(void *opaque, ggv_binding binding)
{
    struct bguncleancontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    case GGV_CLEAN_INUSE: v.i = c->hand->inuse; break;
    case GGV_CLEAN_AMMO_TYPE: v.i = c->player->gunctrl.ammotypes[c->slot]; break;
    case GGV_VIS_CLIP_SIZE: v.i = c->hand->clipsizes[c->slot]; break;
    case GGV_VIS_LOADED: v.i = c->hand->loadedammo[c->slot]; break;
    case GGV_CLEAN_WEAPON: v.i = c->player->gunctrl.weaponnum; break;
    case GGV_CLEAN_RESERVE: v.i = *(c->reserve = &c->player->ammoheldarr[c->player->gunctrl.ammotypes[c->slot]]); break;
    case GGV_CLEAN_MULTIPLAYER: v.u = g_Vars.mplayerisrunning; break;
    case GGV_SWITCH_HAND_INDEX: v.i = c->handnum; break;
    case GGV_CLEAN_REGION:
#if VERSION >= VERSION_JPN_FINAL
        v.i = 2;
#elif VERSION >= VERSION_PAL_BETA
        v.i = 1;
#else
        v.i = 0;
#endif
        break;
    default: break;
    } return v;
}
static void bgunCleanWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bguncleancontext *c = opaque;
    switch (binding) {
    case GGV_VIS_AMMO_SLOT: c->slot = v.i; break;
    case GGV_CLEAN_RELOAD_INDEX: c->reloadIndex = v.i; break;
    case GGV_CLEAN_ROUNDS: c->hand->gunroundsspent[c->reloadIndex] = v.u; break;
    case GGV_CLEAN_RESERVE: *c->reserve = v.i; break;
    case GGV_VIS_LOADED: c->hand->loadedammo[c->slot] = v.i; break;
    default: break;
    }
}
static ggv_value bgunCleanCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bguncleancontext *c = opaque; ggv_value v = {0}; struct player *player = c->player; s32 handnum = c->handnum;
    switch (service) {
    case GGV_RELOAD_INDEX_FOR_WEAPON: v.i = bgunGetUnequippedReloadIndex(a.i); break;
    case GGV_CLEAN_DELETE: playermgrDeleteWeapon(a.i); break;
    case GGV_CLEAN_ROCKET: bgunFreeHeldRocket(a.i); break;
    case GGV_CLEAN_RETIRE: wgV2RuntimeRetireHand(c->player, a.i, "native weapon freed"); break;
    case GGV_CLEAN_LOG_ENTER:
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunFreeWeapon enter player=0 hand=%d inuse_was=%d state=%d sm=%d wpn=%d gunmemtype=%d",
			(s32)handnum,
			(s32)player->hands[handnum].inuse,
			(s32)player->hands[handnum].state,
			(s32)player->hands[handnum].stateminor,
			(s32)player->hands[handnum].gset.weaponnum,
			(s32)player->gunctrl.gunmemtype);
        break;
    case GGV_CLEAN_LOG_EXIT:
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunFreeWeapon exit  player=0 hand=%d inuse=%d wpn=%d",
			(s32)handnum,
			(s32)player->hands[handnum].inuse,
			(s32)player->hands[handnum].gset.weaponnum);
        break;
    default: break;
    } return v;
}
void bgunFreeWeapon(s32 handnum)
{
    struct player *player = g_Vars.currentplayer;
    struct bguncleancontext context = { player, &player->hands[handnum], handnum, 0, 0, NULL };
    ggv_host host = { &context, bgunCleanRead, bgunCleanWrite, bgunCleanCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_FREE_WEAPON), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: weapon cleanup refused immutable program");
}

struct bgunswitchhandcontext { struct hand *hand; struct gunctrl *ctrl; s32 handnum; };
static ggv_value bgunSwitchHandRead(void *opaque, ggv_binding binding)
{
    struct bgunswitchhandcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_SWITCH_HAND_INDEX: v.i = c->handnum; break;
    case GGV_SWITCH_RESET_WEAPON: v.i = c->ctrl->weaponnum; break;
    case GGV_SWITCH_UPGRADES: v.u = c->ctrl->upgradewant; break;
    case GGV_SWITCH_AUDIO_HANDLE: v.ref = c->hand->audiohandle; break;
    default: break;
    } return v;
}
static void bgunSwitchHandWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct hand *hand = ((struct bgunswitchhandcontext *)opaque)->hand;
    switch (binding) {
    case GGV_RELOAD_EJECT_STATE: hand->ejectstate = v.i; break;
    case GGV_CHANGE_EJECT_TYPE: hand->ejecttype = v.i; break;
    case GGV_SWITCH_RESET_BIT: hand->unk0d0f_02 = v.b; break;
    case GGV_HAND_ACTIVATE_SECONDARY: hand->activatesecondary = v.b; break;
    case GGV_MATMOT1: hand->matmot1 = v.f; break;
    case GGV_MATMOT2: hand->matmot2 = v.f; break;
    case GGV_SWITCH_MATMOT3: hand->matmot3 = v.f; break;
    case GGV_SWITCH_ANGLE_DAMPER: hand->angledamper = v.f; break;
    case GGV_SWITCH_SMOKE_POINT: hand->gunsmokepoint = v.f; break;
    case GGV_BURST_BULLETS: hand->burstbullets = v.i; break;
    case GGV_LOAD_SLIDE: hand->loadslide = v.f; break;
    case GGV_ALLOW_SHOOT_FRAME: hand->allowshootframe = v.i; break;
    case GGV_LAST_SHOOT_FRAME: hand->lastshootframe60 = v.i; break;
    case GGV_WEAPON_FUNCTION: hand->gset.weaponfunc = v.i; break;
    case GGV_SWITCH_HAND_WEAPON: hand->gset.weaponnum = v.i; break;
    case GGV_SWITCH_HAND_UPGRADES: hand->gset.unk0639 = v.u; break;
    case GGV_ANIM_GANGSTA: hand->gangstarot = v.f; break;
    default: break;
    }
}
static ggv_value bgunSwitchHandCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunswitchhandcontext *c = opaque; ggv_value v = {0};
    switch (service) {
    case GGV_SWITCH_CONFIGURE_HAND: bgun0f0abd30(a.i); break;
    case GGV_SWITCH_INIT_ANIM: animInit(&c->hand->anim); break;
    case GGV_SOUND_STATE: v.i = sndGetState(a.ref); break;
    case GGV_SOUND_STOP: audioStop(a.ref); break;
    default: break;
    } return v;
}
static void bgunResetSwitchHand(struct player *player, struct gunctrl *ctrl, s32 handnum)
{
    struct bgunswitchhandcontext context = { &player->hands[handnum], ctrl, handnum };
    ggv_host host = { &context, bgunSwitchHandRead, bgunSwitchHandWrite, bgunSwitchHandCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SWITCH_HAND_RESET), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: switch hand reset refused immutable program");
}

struct bgunswitchcontext {
    struct player *player; struct gunctrl *ctrl; struct chrdata *chr;
    s32 diagRight, diagLeft; s32 *reserve;
};
static ggv_value bgunSwitchRead(void *opaque, ggv_binding binding)
{
    struct bgunswitchcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    case GGV_SW_PENDING: v.i = c->ctrl->switchtoweaponnum; break;
    case GGV_SW_FRAME60: v.i = g_Vars.lvframe60; break;
    case GGV_SW_FRAME: v.i = g_Vars.lvframenum; break;
    case GGV_SW_WEAPON: v.i = c->ctrl->weaponnum; break;
    case GGV_SW_OLD_WEAPON: v.i = c->player->gunctrl.weaponnum; break;
    case GGV_SW_LEFT_INUSE: v.i = c->player->hands[HAND_LEFT].inuse; break;
    case GGV_SW_DUAL: v.i = c->ctrl->dualwielding; break;
    case GGV_SW_PLAYER_DUAL: v.i = c->player->gunctrl.dualwielding; break;
    case GGV_SW_MATMOT: v.f = c->player->hands[HAND_RIGHT].matmot1; break;
    case GGV_SW_RESERVE: v.i = c->player->ammoheldarr[c->ctrl->ammotypes[0]]; break;
    case GGV_SW_RESERVE_TARGET: v.i = *(c->reserve = &c->player->ammoheldarr[c->ctrl->ammotypes[0]]); break;
    case GGV_SW_ZOOM_MAX: v.f = g_Vars.currentplayer->zoomintimemax; break;
    case GGV_SW_FOV_NEW: v.f = g_Vars.currentplayer->zoominfovynew; break;
    case GGV_SW_PLAYER_CHR: v.ref = c->player->prop->chr; break;
    case GGV_SW_CHR_HIDDEN: v.u = c->chr->hidden; break;
    case GGV_SW_STAGE: v.i = g_Vars.stagenum; break;
    case GGV_SW_NONE: v.i = WEAPON_NONE; break;
    case GGV_SW_UNARMED: v.i = WEAPON_UNARMED; break;
    case GGV_SW_PSYCHOSIS: v.i = WEAPON_PSYCHOSISGUN; break;
    case GGV_SW_SCANNER: v.i = WEAPON_HORIZONSCANNER; break;
    case GGV_SW_RCP: v.i = WEAPON_RCP120; break;
    case GGV_SW_REMOTE: v.i = WEAPON_REMOTEMINE; break;
    case GGV_SW_DISGUISE_A: v.i = WEAPON_DISGUISE40; break;
    case GGV_SW_DISGUISE_B: v.i = WEAPON_DISGUISE41; break;
    case GGV_SW_RESCUE: v.i = STAGE_RESCUE; break;
    default: break;
    } return v;
}
static void bgunSwitchWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunswitchcontext *c = opaque;
    switch (binding) {
    case GGV_SW_PENDING: c->ctrl->switchtoweaponnum = v.i; break;
    case GGV_SW_DIAG_RIGHT: c->diagRight = v.i; break;
    case GGV_SW_DIAG_LEFT: c->diagLeft = v.i; break;
    case GGV_SW_WEAPON: c->ctrl->weaponnum = v.i; break;
    case GGV_SW_LEFT_INUSE: c->player->hands[HAND_LEFT].inuse = v.i; break;
    case GGV_SW_RIGHT_INUSE: c->player->hands[HAND_RIGHT].inuse = v.i; break;
    case GGV_SW_DUAL: c->ctrl->dualwielding = v.i; break;
    case GGV_SW_PREV_WEAPON: c->player->gunctrl.prevweaponnum = v.i; break;
    case GGV_SW_PREV_DUAL: c->player->gunctrl.prevwasdualwielding = v.i; break;
    case GGV_SW_CURRENT_AIM: g_Vars.currentplayer->insightaimmode = v.b; break;
    case GGV_SW_RESERVE_TARGET: *c->reserve = v.i; break;
    case GGV_SW_ZOOM_MAX: g_Vars.currentplayer->zoomintimemax = v.f; break;
    case GGV_SW_ZOOM_TIME: g_Vars.currentplayer->zoomintime = v.f; break;
    case GGV_SW_FOV_NEW: g_Vars.currentplayer->zoominfovynew = v.f; break;
    case GGV_SW_FOV: g_Vars.currentplayer->zoominfovy = v.f; break;
    case GGV_SW_CURRENT_INVERT: g_Vars.currentplayer->gunctrl.invertgunfunc = v.i; break;
    case GGV_SW_USE_TIME: g_Vars.currentplayer->usedowntime = v.i; break;
    case GGV_SW_FADER: c->ctrl->fnfader = v.i; break;
    case GGV_SW_CHR: c->chr = v.ref; break;
    case GGV_SW_CURRENT_DISGUISED: g_Vars.currentplayer->disguised = v.b; break;
    case GGV_SW_CHR_HIDDEN: c->chr->hidden = v.u; break;
    case GGV_SW_CURRENT_STRING: c->ctrl->curfnstr = v.i; break;
    case GGV_SW_STRING_TIMER: c->ctrl->fnstrtimer = v.i; break;
    case GGV_SW_THROWING: c->ctrl->throwing = v.i; break;
    default: break;
    }
}
static ggv_value bgunSwitchCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunswitchcontext *c = opaque; ggv_value v = {0};
    struct player *player = c->player; struct gunctrl *ctrl = c->ctrl;
    switch (service) {
    case GGV_SW_CAN_FREE: v.b = bgunCanFreeWeapon(a.i); break;
    case GGV_SW_DEVICE_STATE: v.i = currentPlayerGetDeviceState(a.i); break;
    case GGV_SW_HAS_DOUBLE: v.b = invHasDoubleWeaponIncAllGuns(a.i, b.i); break;
    case GGV_SW_CLEANUP: func0f0d7364(); break;
    case GGV_SW_FREE: bgunFreeWeapon(a.i); break;
    case GGV_SW_SET_MEMORY: bgunSetGunMemWeapon(a.i); break;
    case GGV_SW_RESET_HAND: bgunResetSwitchHand(c->player, c->ctrl, a.i); break;
    case GGV_SW_INVENTORY_INDEX: invCalculateCurrentIndex(); break;
    case GGV_SW_DISGUISE_SOUND: sndStart(var80095200, SFX_DISGUISE_ON, 0, -1, -1, -1, -1, -1); break;
    case GGV_SW_REMOVE_ITEM: invRemoveItemByNum(a.i); break;
    case GGV_SW_CYCLE_BACK: bgunCycleBack(); break;
    case GGV_SW_LOG:
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunTickSwitch2 player=0 frame=%d switchto=%d "
			"R(canFree=%d state=%d sm=%d cnt=%d throwing=%d) "
			"L(canFree=%d state=%d sm=%d cnt=%d)",
			(s32)g_Vars.lvframe60, (s32)ctrl->switchtoweaponnum,
			c->diagRight,
			(s32)player->hands[HAND_RIGHT].state,
			(s32)player->hands[HAND_RIGHT].stateminor,
			(s32)player->hands[HAND_RIGHT].count,
			(s32)ctrl->throwing,
			c->diagLeft,
			(s32)player->hands[HAND_LEFT].state,
			(s32)player->hands[HAND_LEFT].stateminor,
			(s32)player->hands[HAND_LEFT].count);
        break;
    default: break;
    } return v;
}
void bgunTickSwitch2(void)
{
    struct bgunswitchcontext context = {0};
    context.player = g_Vars.currentplayer;
    context.ctrl = &g_Vars.currentplayer->gunctrl;
    ggv_host host = { &context, bgunSwitchRead, bgunSwitchWrite, bgunSwitchCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SWITCH_DRIVER), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: switch parent refused immutable program");
}

struct bgunequipcontext { struct player *player; s32 handnum, weaponnum, diagnostic; };
static ggv_value bgunEquipRead(void *opaque, ggv_binding binding)
{
    struct bgunequipcontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_SW_PLAYER_NUM: v.i = g_Vars.currentplayernum; break;
    case GGV_EQUIP_REQUEST: v.i = c->weaponnum; break;
    case GGV_SWITCH_HAND_INDEX: v.i = c->handnum; break;
    case GGV_SW_WEAPON: v.i = c->player->gunctrl.weaponnum; break;
    case GGV_SW_PENDING: v.i = c->player->gunctrl.switchtoweaponnum; break;
    case GGV_SW_NONE: v.i = WEAPON_NONE; break;
    case GGV_SW_UNARMED: v.i = WEAPON_UNARMED; break;
    case GGV_EQUIP_CUSTOM_END: v.i = WEAPON_CUSTOM_END; break;
    default: break;
    } return v;
}
static void bgunEquipWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunequipcontext *c = opaque;
    switch (binding) {
    case GGV_EQUIP_REQUEST: c->weaponnum = v.i; break;
    case GGV_EQUIP_DIAG_SHORTCUT: c->diagnostic = v.i; break;
    case GGV_SW_PENDING: c->player->gunctrl.switchtoweaponnum = v.i; break;
    case GGV_EQUIP_WANT_AMMO: c->player->gunctrl.wantammo = v.b; break;
    case GGV_EQUIP_CURRENT_DUAL: g_Vars.currentplayer->gunctrl.dualwielding = v.b; break;
    default: break;
    }
}
static ggv_value bgunEquipCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunequipcontext *c = opaque; ggv_value v = {0};
    struct player *player = c->player; s32 weaponnum = c->weaponnum, handnum = c->handnum;
    switch (service) {
    case GGV_EQUIP_CHILD: bgunEquipWeapon(a.i); break;
    case GGV_EQUIP_LOG:
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunEquipWeapon enter player=0 wpn=%d cur_wpn=%d cur_switchto=%d shortcircuit=%d",
			(s32)weaponnum,
			(s32)player->gunctrl.weaponnum,
			(s32)player->gunctrl.switchtoweaponnum,
			c->diagnostic);
        break;
    case GGV_EQUIP2_LOG_ENTER:
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunEquipWeapon2 enter player=0 hand=%d req_wpn=%d "
			"cur_gunctrl_wpn=%d switchto=%d dualwield=%d hand_R_wpn=%d hand_L_wpn=%d",
			handnum, weaponnum,
			(s32)g_Vars.currentplayer->gunctrl.weaponnum,
			(s32)g_Vars.currentplayer->gunctrl.switchtoweaponnum,
			(s32)g_Vars.currentplayer->gunctrl.dualwielding,
			(s32)g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponnum,
			(s32)g_Vars.currentplayer->hands[HAND_LEFT].gset.weaponnum);
        break;
    case GGV_EQUIP2_LOG_EXIT:
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunEquipWeapon2 exit player=0 hand=%d req_wpn=%d "
			"now_gunctrl_wpn=%d switchto=%d dualwield=%d",
			handnum, weaponnum,
			(s32)g_Vars.currentplayer->gunctrl.weaponnum,
			(s32)g_Vars.currentplayer->gunctrl.switchtoweaponnum,
			(s32)g_Vars.currentplayer->gunctrl.dualwielding);
        break;
    default: break;
    } return v;
}
static void bgunRunEquip(struct bgunequipcontext *context, ggv_stock_program program)
{
    ggv_host host = { context, bgunEquipRead, bgunEquipWrite, bgunEquipCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(program), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: equip request refused immutable program");
}
void bgunEquipWeapon(s32 weaponnum)
{
    struct bgunequipcontext context = { g_Vars.currentplayer, 0, weaponnum, 0 };
    bgunRunEquip(&context, GGV_EQUIP_WEAPON);
}

/* GGV CurrentWeapon services begin: fresh global fields, native signed8. */
static ggv_value bgunCurrentWeaponRead(void *opaque, ggv_binding binding)
{
    s32 handnum = *(s32 *)opaque; ggv_value value = {0};
    if (binding == GGV_CURRENT_HAND_INUSE) value.i = g_Vars.currentplayer->hands[handnum].inuse;
    if (binding == GGV_CURRENT_WEAPON_NUMBER) value.i = g_Vars.currentplayer->gunctrl.weaponnum;
    return value;
}
static void bgunCurrentWeaponWrite(void *opaque, ggv_binding binding, ggv_value value) {}
/* GGV CurrentWeapon services end. */
s32 bgunGetWeaponNum(s32 handnum)
{
    ggv_host host = { &handnum, bgunCurrentWeaponRead, bgunCurrentWeaponWrite, NULL }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_CURRENT_WEAPON), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: current weapon query refused immutable program"); return WEAPON_NONE;
    } return result;
}

s32 bgunGetWeaponNum2(s32 handnum)
{
    ggv_host host = { &handnum, bgunCurrentWeaponRead, bgunCurrentWeaponWrite, NULL }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_CURRENT_WEAPON), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: current weapon query refused immutable program"); return WEAPON_NONE;
    } return result;
}

bool bgun0f0a1a10(s32 weaponnum)
{
	if (weaponHasFlag(weaponnum, WEAPONFLAG_00000400)
			&& (bgunGetAmmoTypeForWeapon(weaponnum, FUNC_PRIMARY) == 0 || bgunGetAmmoQtyForWeapon(weaponnum, FUNC_PRIMARY) > 0)) {
		return true;
	}

	return false;
}

s32 bgunGetSwitchToWeapon(s32 handnum)
{
	s32 weaponnum;

	if (g_Vars.currentplayer->gunctrl.switchtoweaponnum >= 0) {
		weaponnum = g_Vars.currentplayer->gunctrl.switchtoweaponnum;
	} else {
		weaponnum = g_Vars.currentplayer->gunctrl.weaponnum;
	}

	if (!g_Vars.currentplayer->gunctrl.dualwielding && handnum == HAND_LEFT) {
		weaponnum = WEAPON_NONE;
	}

	return weaponnum;
}

void bgunSwitchToPrevious(void)
{
	if (g_Vars.tickmode != TICKMODE_CUTSCENE) {
		struct player *player = g_Vars.currentplayer;
		s32 dualweaponnum;

#if VERSION >= VERSION_NTSC_1_0
		if (invHasSingleWeaponIncAllGuns(player->gunctrl.prevweaponnum)) {
			bgunEquipWeapon2(HAND_RIGHT, player->gunctrl.prevweaponnum);

			dualweaponnum = invHasDoubleWeaponIncAllGuns(player->gunctrl.prevweaponnum, player->gunctrl.prevweaponnum)
				* player->gunctrl.prevweaponnum * player->gunctrl.prevwasdualwielding;
			bgunEquipWeapon2(HAND_LEFT, dualweaponnum);
		} else {
			bgunAutoSwitchWeapon();
		}
#else
		bgunEquipWeapon2(HAND_RIGHT, player->gunctrl.prevweaponnum);
		bgunEquipWeapon2(HAND_LEFT, player->gunctrl.prevweaponnum * player->gunctrl.prevwasdualwielding);
#endif
	}
}

void bgunCycleForward(void)
{
	s32 weaponnum1;
	s32 weaponnum2;
	struct player *player = g_Vars.currentplayer;

	if (g_Vars.tickmode != TICKMODE_CUTSCENE) {
		weaponnum1 = bgunGetSwitchToWeapon(HAND_RIGHT);
		weaponnum2 = bgunGetSwitchToWeapon(HAND_LEFT);

		if (weaponnum1 > WEAPON_PSYCHOSISGUN || weaponnum2 > WEAPON_PSYCHOSISGUN) {
			weaponnum1 = player->gunctrl.prevweaponnum;
			weaponnum2 = player->gunctrl.prevweaponnum * player->gunctrl.prevwasdualwielding;
		} else {
			invChooseCycleForwardWeapon(&weaponnum1, &weaponnum2, false);
		}

		if (weaponnum2 != weaponnum1) {
			player->gunctrl.dualwielding = false;
		} else {
			player->gunctrl.dualwielding = true;
		}

		bgunEquipWeapon(weaponnum1);
	}
}

void bgunCycleBack(void)
{
	s32 weaponnum1;
	s32 weaponnum2;
	struct player *player = g_Vars.currentplayer;

	if (g_Vars.tickmode != TICKMODE_CUTSCENE) {
		weaponnum1 = bgunGetSwitchToWeapon(HAND_RIGHT);
		weaponnum2 = bgunGetSwitchToWeapon(HAND_LEFT);

		if (weaponnum2 == WEAPON_REMOTEMINE) {
			weaponnum2 = WEAPON_NONE;
		}

		if (weaponnum1 > WEAPON_PSYCHOSISGUN || weaponnum2 > WEAPON_PSYCHOSISGUN) {
			weaponnum1 = player->gunctrl.prevweaponnum;
			weaponnum2 = player->gunctrl.prevweaponnum * player->gunctrl.prevwasdualwielding;
		} else {
			invChooseCycleBackWeapon(&weaponnum1, &weaponnum2, false);
		}

		if (weaponnum2 == WEAPON_NONE) {
			player->gunctrl.dualwielding = false;
		} else {
			player->gunctrl.dualwielding = true;
		}

		bgunEquipWeapon(weaponnum1);
	}
}

/**
 * Return true if the player has ammo for the given weapon (for either function)
 * or if the weapon doesn't support ammo.
 *
 * Used by the active menu to colour the slots.
 */
bool bgunHasAmmoForWeapon(s32 weaponnum)
{
	bool ammodefexists = false;
	bool hasammo = false;
	struct weapon *weapon = weaponFindById(weaponnum);
	s32 i;

	if (weapon == NULL) {
		return true;
	}

	for (i = 0; i < 2; i++) {
		struct weaponfunc *func = weaponGetFunctionById(weaponnum, i);

		if (func && func->ammoindex >= 0) {
			struct inventory_ammo *ammo = weapon->ammos[func->ammoindex];

			if (ammo) {
				ammodefexists = true;

				if (bgunGetAmmoCount(ammo->type) > 0) {
					hasammo = true;
				}
			}
		}
	}

	if (!ammodefexists) {
		return true;
	}

	if (hasammo == true) {
		return true;
	}

	return false;
}

u8 g_AutoSwitchWeaponsPrimary[] = {
	WEAPON_RCP120,
	WEAPON_SUPERDRAGON, // primary function
	WEAPON_K7AVENGER,
	WEAPON_AR34,
	WEAPON_CALLISTO,
	WEAPON_LAPTOPGUN,
	WEAPON_DRAGON,
	WEAPON_CMP150,
	WEAPON_CYCLONE,
	WEAPON_FARSIGHT,
	WEAPON_SHOTGUN,
	WEAPON_REAPER,
	WEAPON_DY357LX,
	WEAPON_MAULER,
	WEAPON_DY357MAGNUM,
	WEAPON_MAGSEC4,
	WEAPON_PHOENIX,
	WEAPON_FALCON2_SCOPE,
	WEAPON_FALCON2,
	WEAPON_FALCON2_SILENCER,
	WEAPON_SNIPERRIFLE,
	WEAPON_CROSSBOW,
	WEAPON_TRANQUILIZER,
	WEAPON_LASER,
	WEAPON_SUPERDRAGON, // secondary function
	WEAPON_DEVASTATOR,
	WEAPON_ROCKETLAUNCHER,
	WEAPON_SLAYER,
	WEAPON_GRENADE,
	WEAPON_NBOMB,
	WEAPON_PROXIMITYMINE,
	WEAPON_TIMEDMINE,
	WEAPON_REMOTEMINE,
	WEAPON_COMBATKNIFE,
	WEAPON_UNARMED,
};

u8 g_AutoSwitchWeaponsSecondary[] = {
	WEAPON_REAPER,
	WEAPON_DY357LX,
	WEAPON_DY357MAGNUM,
	WEAPON_FALCON2_SCOPE,
	WEAPON_FALCON2,
	WEAPON_FALCON2_SILENCER,
	WEAPON_UNARMED,
};

/**
 * Automatically choose and equip a new weapon after trying to fire a weapon
 * which is out of ammo.
 *
 * The weapon preference order is stored in two arrays; one for weapons which
 * should have their primary functions considered and which require ammo, and
 * another for weapons which should have their secondary functions considered
 * and don't require ammo for those functions. The second is only used if no
 * weapons are usable from the primary array.
 *
 * For the primary array, the weapon must not be out of ammo. If the player's
 * current weapon is not in the primary array then the first available primary
 * will be selected. If the player's current weapon is in the primary array then
 * the last available weapon earlier than their current weapon will be selected.
 * If there are no weapons earlier than their current weapon then the first
 * weapon after their current weapon is selected.
 *
 * In the primary array, the SuperDragon is a special case and appears twice.
 * The first use is for the primary function while the second use is for the
 * secondary function.
 *
 * For the secondary array, the player's current weapon must not be in the
 * array. The first available weapon is selected. The player's "wantammo" flag
 * will be set which will force the weapon onto the second function.
 */
struct bgunautoselectcontext { s32 index; };
static ggv_value bgunAutoSelectRead(void *opaque, ggv_binding binding)
{
    struct bgunautoselectcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_FUNCTION_CONTROL_WEAPON: value.i = g_Vars.currentplayer->gunctrl.weaponnum; break;
    case GGV_STATE_PLAYER_NUMBER: value.i = g_Vars.currentplayernum; break;
    case GGV_SELECT_TICKMODE: value.i = g_Vars.tickmode; break;
    case GGV_SELECT_PRIMARY_WEAPON: value.i = g_AutoSwitchWeaponsPrimary[c->index]; break;
    case GGV_SELECT_SECONDARY_WEAPON: value.i = g_AutoSwitchWeaponsSecondary[c->index]; break;
    case GGV_SELECT_PRIMARY_COUNT: value.i = ARRAYCOUNT(g_AutoSwitchWeaponsPrimary); break;
    case GGV_SELECT_SECONDARY_COUNT: value.i = ARRAYCOUNT(g_AutoSwitchWeaponsSecondary); break;
    default: break;
    }
    return value;
}
static void bgunAutoSelectWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunautoselectcontext *c = opaque;
    switch (binding) {
    case GGV_SELECT_INDEX: c->index = value.i; break;
    case GGV_SWITCH_DUALWIELD: g_Vars.currentplayer->gunctrl.dualwielding = value.b; break;
    case GGV_GUNCTRL_WANTAMMO: g_Vars.currentplayer->gunctrl.wantammo = value.b; break;
    default: break;
    }
}
static ggv_value bgunAutoSelectCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)d;
    switch (service) {
    case GGV_SELECT_LOG:
        sysLogPrintf(LOG_NOTE,
            "LOG.WPN.DIAG: bgunAutoSwitchWeapon enter player=0 cur_wpn=%d switchto=%d tickmode=%d",
            a.i, (s32)g_Vars.currentplayer->gunctrl.switchtoweaponnum, (s32)g_Vars.tickmode); break;
    case GGV_SELECT_HAS_SINGLE: value.b = invHasSingleWeaponIncAllGuns(a.i); break;
    case GGV_SELECT_HAS_DOUBLE: value.b = invHasDoubleWeaponIncAllGuns(a.i, b.i); break;
    case GGV_QUERY_FIND_WEAPON: value.ref = weaponFindById(a.i); break;
    case GGV_SELECT_FIND_FUNCTION: value.ref = weaponGetFunctionById(a.i, b.i); break;
    case GGV_EXCLUSION_CHECK: value.b = bgun0f0990b0(a.ref, b.ref); break;
    case GGV_SELECT_FUNCTION_FLAGS: value.u = ((struct weaponfunc *)a.ref)->flags; break;
    case GGV_SELECT_EQUIP: bgunEquipWeapon(a.i); break;
    default: break;
    }
    return value;
}
void bgunAutoSwitchWeapon(void)
{
    struct bgunautoselectcontext context = {0};
    ggv_host host = { &context, bgunAutoSelectRead, bgunAutoSelectWrite, bgunAutoSelectCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_AUTO_SELECT_WEAPON), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: auto-switch selection refused immutable program");
    }
}

void bgunEquipWeapon2(s32 handnum, s32 weaponnum)
{
    struct bgunequipcontext context = { NULL, handnum, weaponnum, 0 };
    bgunRunEquip(&context, GGV_EQUIP_WEAPON_HAND);
}

s32 bgunIsFiring(s32 handnum)
{
	return g_Vars.currentplayer->hands[handnum].firing;
}

s32 bgunGetAttackType(s32 handnum)
{
	return g_Vars.currentplayer->hands[handnum].attacktype;
}

char *bgunGetName(s32 weaponnum)
{
	struct weapon *weapon = weaponFindById(weaponnum); /* S484 F4 */

	if (weapon) {
		return langGet(weapon->name);
	}

	return "** error\n";
}

u16 bgunGetNameId(s32 weaponnum)
{
	struct weapon *weapon = weaponFindById(weaponnum); /* S484 F4 */

	if (weapon) {
		return weapon->name;
	}

	return 0;
}

char *bgunGetShortName(s32 weaponnum)
{
	struct weapon *weapon = weaponFindById(weaponnum); /* S484 F4 */

	if (weapon) {
		return langGet(weapon->shortname);
	}

	return "** error\n";
}

const char var7f1ac170[] = "wantedfn %d tiggle %d\n";

void bgunReloadIfPossible(s32 handnum)
{
	struct player *player = g_Vars.currentplayer;

	if (bgunGetAmmoTypeForWeapon(bgunGetWeaponNum(handnum), FUNC_PRIMARY)
			&& player->hands[handnum].modenext == HANDMODE_NONE) {
		player->hands[handnum].modenext = HANDMODE_RELOAD;
	}
}

void bgunSetAdjustPos(f32 angle)
{
	struct player *player = g_Vars.currentplayer;

	player->hands[0].adjustpos.z = (1 - cosf(angle)) * 5;
	player->hands[1].adjustpos.z = (1 - cosf(angle)) * 5;
}

void bgunStartSlide(s32 handnum)
{
    struct bgunhandcontext context = {0};
    context.handnum = handnum;
    ggv_host host = { &context, bgunHandRead, bgunHandWrite, bgunHandCall, bgunHandCall4 };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_HAND_START_SLIDE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: hand slide refused immutable program");
    }
}

/**
 * Update the slide on weapons which have them (eg. Falcon 2).
 *
 * The slide moves back and then forward when firing. If the gun no longer has
 * any ammo loaded in it, the slide moves back and remains in the back position.
 */
void bgunUpdateSlide(s32 handnum)
{
	f32 slidemax = 0.0f;
	struct weaponfunc *funcdef = currentPlayerGetWeaponFunction(handnum);
	struct player *player = g_Vars.currentplayer;

	if (funcdef && ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT)) {
		struct weaponfunc_shoot *shootfunc = (struct weaponfunc_shoot *)funcdef;
		slidemax = shootfunc->slidemax;
	}

	if (player->hands[handnum].slideinc) {
		// Slide is moving backwards
		if (player->hands[handnum].slidetrans < slidemax) {
			player->hands[handnum].slidetrans += slidemax * 0.25f * g_Vars.lvupdate60freal;
		}

		if (player->hands[handnum].slidetrans >= slidemax) {
			player->hands[handnum].slidetrans = slidemax;
			player->hands[handnum].slideinc = false;
		}
	} else if (player->hands[handnum].loadedammo[FUNC_PRIMARY] > 0) {
		if (bgun0f098a44(&player->hands[handnum], 3)) {
			// Slide is moving forwards
			if (player->hands[handnum].slidetrans > 0.0f) {
				player->hands[handnum].slidetrans -= slidemax * 0.16666667f * g_Vars.lvupdate60freal;
			}

			if (player->hands[handnum].slidetrans < 0.0f) {
				player->hands[handnum].slidetrans = 0.0f;
			}
		}
	}
}

f32 bgun0f0a2498(f32 arg0, f32 arg1, f32 arg2, f32 arg3)
{
	f32 a = arg0 - arg2;

	return asinf(a / sqrtf(a * a + (arg1 - arg3) * (arg1 - arg3)));
}

void bgun0f0a24f0(struct coord *arg0, s32 handnum)
{
	struct coord b;
	struct coord a;

	bgun0f0a0c44(handnum, &a, &b);

	b.x *= 1000;
	b.y *= 1000;
	b.z *= 1000;

	arg0->x = b.x;
	arg0->y = b.y;
	arg0->z = b.z;
}

/**
 * This function is a callback that is passed to model code.
 */
void bgun0f0a256c(s32 mtxindex, Mtxf *mtx)
{
	Mtxf sp78;
	Mtxf sp38;
	struct coord rot;

	if (mtxindex == var8009d148) {
		if (var8009d144->ejectstate == EJECTSTATE_INIT) {
			var8009d144->unk0d14 = mtx->m[3][0];
			var8009d144->unk0d18 = mtx->m[3][1];
			var8009d144->unk0d1c = mtx->m[3][2];

			var8009d144->unk0d2c[0][0] = mtx->m[0][0];
			var8009d144->unk0d2c[0][1] = mtx->m[0][1];
			var8009d144->unk0d2c[0][2] = mtx->m[0][2];
			var8009d144->unk0d2c[1][0] = mtx->m[1][0];
			var8009d144->unk0d2c[1][1] = mtx->m[1][1];
			var8009d144->unk0d2c[1][2] = mtx->m[1][2];
			var8009d144->unk0d2c[2][0] = mtx->m[2][0];
			var8009d144->unk0d2c[2][1] = mtx->m[2][1];
			var8009d144->unk0d2c[2][2] = mtx->m[2][2];
		} else if (var8009d144->ejectstate >= EJECTSTATE_AIRBORNE) {
			mtx->m[3][0] = var8009d144->unk0d14;
			mtx->m[3][1] = var8009d144->unk0d18;
			mtx->m[3][2] = var8009d144->unk0d1c;

			mtx->m[0][0] = var8009d144->unk0d2c[0][0];
			mtx->m[0][1] = var8009d144->unk0d2c[0][1];
			mtx->m[0][2] = var8009d144->unk0d2c[0][2];
			mtx->m[1][0] = var8009d144->unk0d2c[1][0];
			mtx->m[1][1] = var8009d144->unk0d2c[1][1];
			mtx->m[1][2] = var8009d144->unk0d2c[1][2];
			mtx->m[2][0] = var8009d144->unk0d2c[2][0];
			mtx->m[2][1] = var8009d144->unk0d2c[2][1];
			mtx->m[2][2] = var8009d144->unk0d2c[2][2];
		}
	}

	if (mtxindex == var8009d0dc) {
		rot.x = 0.0f;
		rot.y = 0.0f;
		rot.z = var8009d140;

		mtx4LoadIdentity(&sp78);
		mtx4LoadRotation(&rot, &sp78);
		mtx4MultMtx4(mtx, &sp78, &sp38);
		mtx4Copy(&sp38, mtx);
	}

	if (mtxindex == var8009d0f0[0] || mtxindex == var8009d0f0[1] || mtxindex == var8009d0f0[2]) {
		rot.x = 0.0f;
		rot.y = 0.0f;
		rot.z = 2.0f * -var8009d140;

		mtx4LoadIdentity(&sp78);
		mtx4LoadRotation(&rot, &sp78);
		mtx4MultMtx4(mtx, &sp78, &sp38);
		mtx4Copy(&sp38, mtx);
	}
}

bool bgun0f0a27c8(void)
{
	struct hand *hand;
	struct weaponfunc *func;

	hand = &g_Vars.currentplayer->hands[HAND_RIGHT];
	func = gsetGetWeaponFunction2(&hand->gset);

	if (func
			&& (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE
			&& hand->state == HANDSTATE_ATTACK
			&& hand->unk0ce8 != NULL
			&& hand->animmode == HANDANIMMODE_BUSY
			&& !bgun0f098a44(hand, 2)) {
		return true;
	}

	hand = &g_Vars.currentplayer->hands[HAND_LEFT];

	if (hand->inuse) {
		func = gsetGetWeaponFunction2(&hand->gset);

		if (func
				&& (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE
				&& hand->state == HANDSTATE_ATTACK
				&& hand->unk0ce8 != NULL
				&& hand->animmode == HANDANIMMODE_BUSY
				&& !bgun0f098a44(hand, 2)) {
			return true;
		}
	}

	return false;
}

/**
 * This function is the same as above but it doesn't call bgun0f098a44().
 *
 * This function is unused.
 */
bool bgun0f0a28d8(void)
{
	struct hand *hand;
	struct weaponfunc *func;

	hand = &g_Vars.currentplayer->hands[HAND_RIGHT];
	func = gsetGetWeaponFunction2(&hand->gset);

	if (func
			&& (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE
			&& hand->state == HANDSTATE_ATTACK
			&& hand->unk0ce8 != NULL
			&& hand->animmode == HANDANIMMODE_BUSY) {
		return true;
	}

	hand = &g_Vars.currentplayer->hands[HAND_LEFT];

	if (hand->inuse) {
		func = gsetGetWeaponFunction2(&hand->gset);

		if (func
				&& (func->type & 0xff) == INVENTORYFUNCTYPE_MELEE
				&& hand->state == HANDSTATE_ATTACK
				&& hand->unk0ce8 != NULL
				&& hand->animmode == HANDANIMMODE_BUSY) {
			return true;
		}
	}

	return false;
}

void bgunHandlePlayerDead(void)
{
	struct player *player = g_Vars.currentplayer;
	s32 i;

	/* B-246 round-6 instrumentation: death path clears the weapon. Log
	 * every call so unexpected death-flow entries into the FP rig
	 * lifecycle are visible. */
	if (g_Vars.currentplayernum == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunHandlePlayerDead player=0 wpn=%d switchto=%d R(inuse=%d) L(inuse=%d) gunmemtype=%d",
			(s32)player->gunctrl.weaponnum,
			(s32)player->gunctrl.switchtoweaponnum,
			(s32)player->hands[HAND_RIGHT].inuse,
			(s32)player->hands[HAND_LEFT].inuse,
			(s32)player->gunctrl.gunmemtype);
	}

	if (player->gunctrl.weaponnum != WEAPON_NONE && player->gunctrl.switchtoweaponnum != WEAPON_NONE) {
		// Eject held weapons
		if (player->hands[HAND_LEFT].inuse) {
			player->hands[HAND_LEFT].ejectstate = EJECTSTATE_INIT;
			player->hands[HAND_LEFT].ejecttype = EJECTTYPE_GUN;
		}

		if (player->hands[HAND_RIGHT].inuse) {
			player->hands[HAND_RIGHT].ejectstate = EJECTSTATE_INIT;
			player->hands[HAND_RIGHT].ejecttype = EJECTTYPE_GUN;
		}

		for (i = 0; i < 2; i++) {
			player->hands[i].matmot1 = 0;
			player->hands[i].matmot2 = 0;
			player->hands[i].matmot3 = 0;

			bgunSetState(i, HANDSTATE_IDLE);
		}

		bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE);
		bgunEquipWeapon2(HAND_RIGHT, WEAPON_NONE);
	}
}

#if VERSION >= VERSION_NTSC_1_0
bool bgunIsMissionCritical(s32 weaponnum)
{
	if (weaponnum == WEAPON_TIMEDMINE
			|| weaponnum == WEAPON_REMOTEMINE
			|| weaponnum == WEAPON_ECMMINE
			|| weaponnum == WEAPON_TRACERBUG) {
		return true;
	}

	return false;
}
#endif

void bgunDisarm(struct prop *attackerprop)
{
	struct player *player = g_Vars.currentplayer;
	s32 weaponnum = player->hands[0].gset.weaponnum;
	struct chrdata *chr;
	s32 modelnum;
	s32 i;
	bool drop;
	struct defaultobj *obj;

	/* B-246 round-6 instrumentation: disarm forces a weapon drop. Log so
	 * any in-match disarm event is visible. */
	if (g_Vars.currentplayernum == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunDisarm player=0 wpn=%d switchto=%d net=%d attackerprop=%p",
			(s32)weaponnum,
			(s32)player->gunctrl.switchtoweaponnum,
			(s32)g_NetMode,
			(void *)attackerprop);
	}

	// don't do anything if we're not the authority
	if (g_NetMode == NETMODE_CLIENT) {
		return;
	}

	if (!weaponHasFlag(weaponnum, WEAPONFLAG_UNDROPPABLE) && weaponnum <= WEAPON_MAX_DROPPABLE) {
#if VERSION >= VERSION_NTSC_1_0
		// Coop must not allow player to drop a mission critical weapon
		// because AI lists can fail the mission if the player has zero
		// quantity.
		if (g_Vars.coopplayernum >= 0
				&& (attackerprop == g_Vars.bond->prop || attackerprop == g_Vars.coop->prop)
				&& bgunIsMissionCritical(weaponnum)) {
			return;
		}
#endif

		if (weaponnum <= WEAPON_UNARMED || player->gunctrl.switchtoweaponnum != -1) {
			return;
		}

		if (g_NetMode == NETMODE_SERVER) {
			netmsgSvcChrDisarmWrite(&g_NetMsgRel, player->prop->chr, attackerprop, weaponnum, 0.f, NULL);
		}

		chr = player->prop->chr;
		drop = true;

		// RC-P120 and cloaking device: turn off cloak if active
		if (weaponnum == WEAPON_RCP120) {
			g_Vars.currentplayer->devicesactive &= ~DEVICE_CLOAKRCP120;
		}

		if (weaponnum == WEAPON_CLOAKINGDEVICE) {
			g_Vars.currentplayer->devicesactive &= ~DEVICE_CLOAKDEVICE;
		}

		// Grenade and nbomb: if pin is pulled, throw it?
		// Or drop it at player's feet with the pin pulled maybe...
		if (weaponnum == WEAPON_GRENADE || weaponnum == WEAPON_NBOMB) {
			for (i = 0; i < 2; i++) {
				struct weaponfunc *func = gsetGetWeaponFunction(&player->hands[i].gset);

#ifdef AVOID_UB
				if (func && (func->type & 0xff) == INVENTORYFUNCTYPE_THROW
						&& player->hands[i].state == HANDSTATE_ATTACK
						&& player->hands[i].stateminor == HANDSTATEMINOR_ATTACK_THROW_0) {
#else
				if ((func->type & 0xff) == INVENTORYFUNCTYPE_THROW
						&& player->hands[i].state == HANDSTATE_ATTACK
						&& player->hands[i].stateminor == HANDSTATEMINOR_ATTACK_THROW_0) {
#endif
					drop = false;
					obj = bgunCreateThrownProjectile(i + 2, &player->hands[i].gset);
				}
			}
		}

		weaponDeleteFromChr(chr, HAND_RIGHT);
		weaponDeleteFromChr(chr, HAND_LEFT);

		// Actually drop the weapon
		modelnum = playermgrGetModelOfWeapon(weaponnum);

		if (modelnum >= 0 && drop) {
			struct prop *prop2 = weaponCreateForChr(chr, modelnum, weaponnum, OBJFLAG_WEAPON_AICANNOTUSE, NULL, NULL);

			if (prop2 && prop2->obj) {
				struct defaultobj *obj = prop2->obj;
				objSetDropped(prop2, DROPTYPE_DEFAULT);

				if (obj->hidden & OBJHFLAG_PROJECTILE) {
					obj->projectile->pickuptimer240 = TICKS(240);
					obj->projectile->pickupby = attackerprop;
				}

				objDrop(prop2, true);

				if (g_NetMode == NETMODE_SERVER) {
					netmsgSvcPropSpawnWrite(&g_NetMsgRel, prop2);
					netmsgSvcPropMoveWrite(&g_NetMsgRel, prop2, NULL);
				}
			}
		}

		invRemoveItemByNum(weaponnum);

		player->hands[1].state = HANDSTATE_IDLE;
		player->hands[1].ejectstate = EJECTSTATE_INIT;
		player->hands[1].ejecttype = EJECTTYPE_GUN;
		player->hands[0].ejectstate = EJECTSTATE_INIT;
		player->hands[0].ejecttype = EJECTTYPE_GUN;
		player->hands[0].state = HANDSTATE_IDLE;

		// Exit slayer rocket mode if player was using it
		if (player->visionmode == VISIONMODE_SLAYERROCKET) {
			struct weaponobj *rocket = g_Vars.currentplayer->slayerrocket;

			if (rocket && rocket->base.prop) {
				rocket->timer240 = 0;
			}

			player->visionmode = VISIONMODE_NORMAL;
		}

		bgunEquipWeapon2(HAND_RIGHT, WEAPON_UNARMED);
		bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE);
	}
}

/**
 * Execute some sort of command list that was generated by the function below.
 *
 * With this function stubbed, part of the CMP150 model does not render.
 */
void bgunExecuteModelCmdList(uintptr_t *ptr)
{
	union modelrwdata *rwdata;
	struct modelnode *node;

	if (ptr != NULL) {
		while (*ptr != 6) {
			switch (*ptr) {
			case 0:
				rwdata = (union modelrwdata *)ptr[1];
				node = (struct modelnode *)ptr[2];
				rwdata->distance.visible = false;
				node->child = (struct modelnode *)ptr[3];
				ptr += 4;
				break;
			case 1:
				rwdata = (union modelrwdata *)ptr[1];
				node = (struct modelnode *)ptr[2];
				rwdata->toggle.visible = true;
				node->child = (struct modelnode *)ptr[3];
				ptr += 4;
				break;
			case 2:
				rwdata = (union modelrwdata *)ptr[1];
				rwdata->headspot.headmodeldef = NULL;
				rwdata->headspot.rwdatas = NULL;
				ptr += 2;
				break;
			case 3:
				rwdata = (union modelrwdata *)ptr[1];
				rwdata->type0b.unk00 = 0;
				ptr += 2;
				break;
			case 4:
				rwdata = (union modelrwdata *)ptr[1];
				rwdata->chrgunfire.visible = false;
				ptr += 2;
				break;
			case 5:
				rwdata = (union modelrwdata *)ptr[1];
				rwdata->dl.vertices = (Vtx *)ptr[2];
				rwdata->dl.gdl = (Gfx *)ptr[3];
				rwdata->dl.colours = (Col *)ptr[4];
				ptr += 5;
				break;
			}
		}
	}
}

/**
 * Generate some sort of command list to be executed by the function above.
 *
 * This appears to be a performance optimisation, so the tick code can quickly
 * iterate the command list to update part visibility rather than iterate the
 * full model tree.
 */
s32 bgunCreateModelCmdList(struct model *model, struct modelnode *nodearg, uintptr_t *ptr)
{
	s32 len = 0;
	struct modelnode *node = nodearg;
	union modelrodata *rodata;
	union modelrwdata *rwdata;

	while (node) {
		u32 type = node->type;

		switch ((u8)type) {
		case MODELNODETYPE_DISTANCE:
			rodata = node->rodata;
			rwdata = modelGetNodeRwData(model, node);
			rwdata->distance.visible = false;
			node->child = rodata->distance.target;
			ptr[0] = 0;
			ptr[1] = (uintptr_t)rwdata;
			ptr[2] = (uintptr_t)node;
			ptr[3] = (uintptr_t)rodata->distance.target;
			ptr += 4;
			len += 4 * sizeof(uintptr_t);
			break;
		case MODELNODETYPE_TOGGLE:
			rodata = node->rodata;
			rwdata = modelGetNodeRwData(model, node);
			rwdata->toggle.visible = true;
			node->child = rodata->toggle.target;
			ptr[0] = 1;
			ptr[1] = (uintptr_t)rwdata;
			ptr[2] = (uintptr_t)node;
			ptr[3] = (uintptr_t)rodata->toggle.target;
			ptr += 4;
			len += 4 * sizeof(uintptr_t);
			break;
		case MODELNODETYPE_HEADSPOT:
			rwdata = modelGetNodeRwData(model, node);
			rwdata->headspot.headmodeldef = NULL;
			rwdata->headspot.rwdatas = NULL;
			ptr[0] = 2;
			ptr[1] = (uintptr_t)rwdata;
			ptr += 2;
			len += 2 * sizeof(uintptr_t);
			break;
		case MODELNODETYPE_0B:
			rwdata = modelGetNodeRwData(model, node);
			rwdata->type0b.unk00 = 0;
			ptr[0] = 3;
			ptr[1] = (uintptr_t)rwdata;
			ptr += 2;
			len += 2 * sizeof(uintptr_t);
			break;
		case MODELNODETYPE_CHRGUNFIRE:
			rwdata = modelGetNodeRwData(model, node);
			rwdata->chrgunfire.visible = false;
			ptr[0] = 4;
			ptr[1] = (uintptr_t)rwdata;
			ptr += 2;
			len += 2 * sizeof(uintptr_t);
			break;
		case MODELNODETYPE_DL:
			rodata = node->rodata;
			rwdata = modelGetNodeRwData(model, node);
			rwdata->dl.vertices = rodata->dl.vertices;
			rwdata->dl.gdl = rodata->dl.opagdl;
			rwdata->dl.colours = (void *)ALIGN8((uintptr_t)&rodata->dl.vertices[rodata->dl.numvertices]);
			ptr[0] = 5;
			ptr[1] = (uintptr_t)rwdata;
			ptr[2] = (uintptr_t)rwdata->dl.vertices;
			ptr[3] = (uintptr_t)rwdata->dl.gdl;
			ptr[4] = (uintptr_t)rwdata->dl.colours;
			ptr += 5;
			len += 5 * sizeof(uintptr_t);
			break;
		}

		if (node->child) {
			node = node->child;
		} else {
			while (node) {
				if (node == nodearg->parent) {
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

	*ptr = 6;
	len += sizeof(uintptr_t);

	return len;
}

u32 var800701ec = 0x00000000;
u32 var800701f0 = 0x00000000;
u32 var800701f4 = 0x00000000;
u32 var800701f8 = 0x00000000;
u32 var800701fc = 0x00000000;

struct guncmd var80070200[2] = {
	{ GUNCMD_PLAYANIMATION, 0, ANIM_0434, 10000 },
	{ GUNCMD_END },
};

void bgunStartDetonateAnimation(s32 playernum)
{
	s32 prevplayernum = g_Vars.currentplayernum;
	setCurrentPlayerNum(playernum);

	if (g_Vars.currentplayer->hands[HAND_LEFT].gset.weaponnum == WEAPON_REMOTEMINE) {
		bgunStartAnimation(var80070200, 1, &g_Vars.currentplayer->hands[HAND_LEFT]);
	}

	setCurrentPlayerNum(prevplayernum);
}

/**
 * Update the gangsta-style rotation of the player's gun.
 *
 * When close to an enemy and aiming at them with a pistol, the gun is rotated
 * sideways. The enemy and aiming check is done elsewhere (autoaim code) and
 * sets the gunctrl's gangsta property to true or false based on whether this
 * criteria is met on the current (or previous?) frame.
 *
 * bgunUpdateGangsta uses this property and increments the rotation of the gun
 * accordingly. It also checks that the gun is in a state that allows gangsta
 * rotation (reloading and equip/unequip do not). It also implements a delay on
 * reverting to the normal rotation.
 */
void bgunUpdateGangsta(struct hand *hand, s32 handnum, struct coord *arg2, struct weaponfunc *funcdef, Mtxf *arg4, Mtxf *arg5)
{
	f32 tmp;
	struct coord sp38 = {0, 0, 0};

	if (g_Vars.currentplayer->gunctrl.gangsta
			&& funcdef
			&& (funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT
			&& (hand->state == HANDSTATE_IDLE
				|| hand->state == HANDSTATE_2
				|| hand->state == HANDSTATE_ATTACKEMPTY
				|| hand->state == HANDSTATE_ATTACK)) {
		if (hand->gangstarot < 1.0f) {
			// Rotate into gangsta position
			hand->ispare1 += g_Vars.lvupdate240;

			if (hand->ispare1 > TICKS(60)) {
				hand->gangstarot += LVUPDATE60FREAL() / 30.0f;

				if (hand->gangstarot > 1.0f) {
					hand->gangstarot = 1.0f;
				}
			}
		} else {
			// Already in gangsta position
			hand->ispare1 = 0;
		}
	} else {
		// At this point we don't want the gun to be in the gangsta position.
		// However we don't want it to revert immediately, so a timer is used.
		f32 inversespeed = 30.0f;

		if (hand->animmode == HANDANIMMODE_BUSY) {
			// Revert faster
			inversespeed = 15.0f;
		}

		if (hand->gangstarot > 0.0f) {
			bool revert = false;

			hand->ispare1 += g_Vars.lvupdate240;

			if (hand->gangstarot < 1.0f) {
				hand->ispare1 = TICKS(244);
			}

			if (hand->ispare1 > TICKS(120)) {
				revert = true;
			}

			if (hand->animmode == HANDANIMMODE_BUSY && funcdef && (funcdef->type & 0xff) != INVENTORYFUNCTYPE_SHOOT) {
				revert = true;
			}

			if (hand->state != HANDSTATE_IDLE
					&& hand->state != HANDSTATE_2
					&& hand->state != HANDSTATE_ATTACKEMPTY
					&& hand->state != HANDSTATE_ATTACK) {
				revert = true;
			}

			if (revert) {
				hand->gangstarot -= LVUPDATE60FREAL() / inversespeed;
			}

			if (hand->gangstarot < 0.0f) {
				hand->gangstarot = 0.0f;
			}
		} else {
			// Not rotated
			hand->ispare1 = 0;
		}
	}

	tmp = -cosf(hand->gangstarot * M_PI) * 0.5f + 0.50f;
	sp38.z = (tmp * 66.6f * 0.017453292f) * (handnum != HAND_RIGHT ? 1.0f : -1.0f);

	mtx4LoadRotation(&sp38, arg4);
	mtx00015be0(arg4, arg5);

	arg2->y += 4.0f * hand->gangstarot;
	arg2->x += 2.0f * hand->gangstarot * (handnum != HAND_RIGHT ? 1.0f : -1.0f);
}

/**
 * Check if smoke needs to be created at the muzzle of the current weapon.
 *
 * gunsmokepoint is basically the temperature of the gun. It increases when
 * firing and cools down when idle. It's only used for pistols; automatics will
 * create smoke based on the number of shots in the current burst.
 *
 * createsmoke must be set to create any smoke at all.
 *
 * forcecreatesmoke controls whether smoke should be created while the gun is
 * still firing.
 */
void bgunUpdateSmoke(struct hand *hand, s32 handnum, s32 weaponnum, struct weaponfunc *funcdef)
{
	if (hand->firing) {
		if (weaponnum == WEAPON_DY357MAGNUM || weaponnum == WEAPON_DY357LX) {
			if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
				hand->gunsmokepoint += 0.6f;
			}
		} else {
			hand->gunsmokepoint += 0.2f;
		}
	}

	hand->gunsmokepoint -= LVUPDATE60FREAL() / 120.0f;

	if (hand->gunsmokepoint < 0.0f) {
		hand->gunsmokepoint = 0.0f;
	}

	if (funcdef && (funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
		f32 mult = 1.0f;

		if (g_Vars.currentplayer->hands[HAND_LEFT].inuse) {
			mult = 1.5f;
		}

		hand->forcecreatesmoke = false;

		switch (weaponnum) {
		case WEAPON_FALCON2:
		case WEAPON_FALCON2_SCOPE:
			if (hand->gunsmokepoint * mult > 0.66f) {
				hand->createsmoke = true;
			}
			break;
		case WEAPON_MAGSEC4:
		case WEAPON_MAULER:
			if (hand->gunsmokepoint * mult > 0.75f) {
				hand->createsmoke = true;
			}
			break;
		case WEAPON_DY357MAGNUM:
		case WEAPON_DY357LX:
			if (hand->gunsmokepoint * mult > 0.9f) {
				hand->createsmoke = true;
			}
			break;
		case WEAPON_CMP150:
		case WEAPON_DRAGON:
		case WEAPON_K7AVENGER:
		case WEAPON_AR34:
		case WEAPON_SUPERDRAGON:
			hand->forcecreatesmoke = true;

			if (hand->burstbullets > 14) {
				hand->createsmoke = true;
			}
			break;
		case WEAPON_CYCLONE:
		case WEAPON_LAPTOPGUN:
			if (hand->burstbullets > 20) {
				hand->createsmoke = true;
			}

			hand->forcecreatesmoke = true;
			break;
		case WEAPON_RCP120:
			hand->forcecreatesmoke = true;

			if (hand->burstbullets > 25) {
				hand->createsmoke = true;
			}
			break;
		case WEAPON_REAPER:
			hand->forcecreatesmoke = true;
			// fall-through
		case WEAPON_SHOTGUN:
			if (hand->firing) {
				hand->createsmoke = true;
			}
			break;
		}
	}

	if (hand->createsmoke && (hand->state != HANDSTATE_ATTACK || hand->forcecreatesmoke)) {
		struct coord smokepos;
		RoomNum smokerooms[2];
		s32 smoketype = SMOKETYPE_MUZZLE_AUTOMATIC;

		switch (weaponnum) {
		case WEAPON_FALCON2:
		case WEAPON_FALCON2_SCOPE:
		case WEAPON_MAGSEC4:
		case WEAPON_MAULER:
		case WEAPON_DY357MAGNUM:
		case WEAPON_DY357LX:
			smoketype = SMOKETYPE_MUZZLE_PISTOL;
			break;
		case WEAPON_REAPER:
			smoketype = SMOKETYPE_MUZZLE_REAPER;
			break;
		case WEAPON_SHOTGUN:
			smoketype = SMOKETYPE_MUZZLE_SHOTGUN;
			break;
		}

		smokerooms[0] = g_Vars.currentplayer->cam_room;
		smokerooms[1] = -1;

		smokepos.x = hand->muzzlepos.x;
		smokepos.y = hand->muzzlepos.y;
		smokepos.z = hand->muzzlepos.z;

		hand->gunsmokepoint = 0.0f;

		if (smokeCreateForHand(&smokepos, smokerooms, smoketype, handnum)) {
			hand->createsmoke = false;
		}
	}
}

/**
 * Update the red beam and dot (used by the Falcon 2 and its variants).
 */
static bool bgunShouldRenderLasersight(struct hand *hand)
{
	if (hand == NULL || !hand->visible) {
		return false;
	}

	if (hand->state == HANDSTATE_CHANGEGUN) {
		return false;
	}

	/* Keep the crosshair-anchored beam out of moving root animations; otherwise
	 * the fixed beam end reads as stretched barrel geometry. */
	if (hand->animmode == HANDANIMMODE_BUSY) {
		return false;
	}

	if (hand->state != HANDSTATE_IDLE
			&& hand->state != HANDSTATE_2
			&& hand->state != HANDSTATE_ATTACKEMPTY
			&& hand->state != HANDSTATE_ATTACK) {
		return false;
	}

	return true;
}

static void bgunCullLasersightsBeforeRender(void)
{
	struct player *player = g_Vars.currentplayer;
	s32 i;

	for (i = 0; i < 2; i++) {
		struct hand *hand = &player->hands[i];
		s32 weaponnum = hand->gset.weaponnum;

		if (!bgunShouldRenderLasersight(hand)
				|| weaponnum < WEAPON_FALCON2
				|| weaponnum > WEAPON_FALCON2_SCOPE) {
			lasersightFree(i);
		}
	}
}

void bgunUpdateLasersight(struct hand *hand, struct modeldef *modeldef, s32 handnum, u8 *allocation)
{
	struct modelnode *node;
	struct coord beamfar;
	struct coord dotpos;
	struct coord dotrot;
	struct coord beamnear;
	s32 mtxindex;
	struct coord sp54;
	struct coord sp48;
	struct coord sp3c;
	struct coord sp30;

	if (modeldef == NULL || allocation == NULL) {
		lasersightFree(handnum);
		return;
	}

	node = modelGetPart(modeldef, MODELPART_GUN_LASERSIGHT);

	if (node) {
		mtxindex = modelFindNodeMtxIndex(node, 0);

		if (mtxindex < 0 || mtxindex >= modeldef->nummatrices) {
			lasersightFree(handnum);
			return;
		}

		beamnear.x = ((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][0];
		beamnear.y = ((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][1];
		beamnear.z = ((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][2];

		mtx4TransformVecInPlace(camGetProjectionMtxF(), &beamnear);

		if (hand->useposrot
				|| (g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_XRAYSCANNER)) {
			beamfar.x = 0.0f;
			beamfar.y = 0.0f;
			beamfar.z = 1.0f;

			mtx4RotateVecInPlace(&hand->cammtx, &beamfar);

			sp48.x = beamfar.x;
			sp48.y = beamfar.y;
			sp48.z = beamfar.z;

			sp3c.x = beamnear.x;
			sp3c.y = beamnear.y;
			sp3c.z = beamnear.z;

			mtx4TransformVec(camGetWorldToScreenMtxf(), &sp3c, &sp54);
			mtx4RotateVec(camGetProjectionMtxF(), &sp48, &sp30);

			beamfar.x *= 500.0f;
			beamfar.y *= 500.0f;
			beamfar.z *= 500.0f;

			mtx4RotateVecInPlace(camGetProjectionMtxF(), &beamfar);

			beamfar.x += beamnear.x;
			beamfar.y += beamnear.y;
			beamfar.z += beamnear.z;

			lasersightSetBeam(handnum, 1, &beamnear, &beamfar);
			return;
		}

		beamfar.x = 0.0f;
		beamfar.y = 0.0f;
		beamfar.z = 500.0f;

		mtx4TransformVecInPlace((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)), &beamfar);

		mtx4TransformVecInPlace(camGetProjectionMtxF(), &beamfar);
		lasersightSetBeam(handnum, 1, &beamnear, &beamfar);

		if (handnum == HAND_RIGHT && hand->hasdotinfo) {
			dotpos.x = hand->dotpos.x;
			dotpos.y = hand->dotpos.y;
			dotpos.z = hand->dotpos.z;

			dotrot.x = hand->dotrot.x;
			dotrot.y = hand->dotrot.y;
			dotrot.z = hand->dotrot.z;

			lasersightSetDot(handnum, &dotpos, &dotrot);
		}
	} else {
		lasersightFree(handnum);
	}
}

/**
 * Increment the main barrel spinning, play sounds and (probably) fire shots.
 */
void bgunUpdateReaper(struct hand *hand, struct modeldef *modeldef)
{
	struct modelnode *node;
	f32 f2;
	f32 f12;
	s32 tmp;

	node = modelGetPart(modeldef, MODELPART_REAPER_002C);

	if (hand->matmot3 <= hand->matmot2) {
		if (hand->matmot2 < 0.0f) {
			hand->matmot2 += 0.01f * LVUPDATE60FREAL();

			if (hand->matmot2 > 0.0f) {
				hand->matmot2 = 0.0f;
			}
		}

		hand->matmot3 = hand->matmot2;
	} else {
		f12 = LVUPDATE60FREAL() * 0.005;

		if (hand->matmot2 < 0.0000001f) {
			hand->matmot2 = -0.14f;

			if (hand->matmot3 < 0.15f) {
				f12 *= 4.0f;
			}
		}

		f2 = hand->matmot3 - hand->matmot2;

		if (f12 < f2) {
			f2 = f12;
		}

		hand->matmot3 -= f2;
	}

	if (hand->matmot3 < 0.0f) {
		hand->matmot1 = hand->matmot1 - (1.0f - cosf(hand->matmot3 * M_PI)) * 0.5f * LVUPDATE60FREAL() * 0.2f;
	} else {
		hand->matmot1 = hand->matmot1 + (1.0f - cosf(hand->matmot3 * M_PI)) * 0.5f * LVUPDATE60FREAL() * 0.2f;
	}

	tmp = hand->matmot1 / 6.2831802368164f;
	hand->matmot1 -= tmp * 6.2831802368164f;
	var8009d140 = hand->matmot1;

	if (hand->audiohandle == NULL && hand->matmot3 > 0.1f && g_Vars.lvupdate240 != 0) {
		sndStart(var80095200, SFX_805E, &hand->audiohandle, -1, -1, -1.0f, -1, -1);
	}

	if (hand->audiohandle != NULL) {
		f32 sp34 = hand->matmot3 / 0.50f + 0.4f;
		s32 volume = AL_VOL_FULL;

		if (hand->matmot3 < 0.1f) {
			audioStop(hand->audiohandle);
		} else {
			if (hand->matmot3 < 0.6f) {
				volume = (hand->matmot3 - 0.1f) * AL_VOL_FULL / 0.5f;
			}

			audioPostEvent(hand->audiohandle, AL_SNDP_VOL_EVT, volume);
			audioPostEvent(hand->audiohandle, AL_SNDP_PITCH_EVT, *(s32 *)&sp34);
		}
	}

	if (node) {
		var8009d0dc = modelFindNodeMtxIndex(node, 0);
		g_ModelJointPositionedFunc = bgun0f0a256c;
		var8009d0f0[0] = var8009d0f0[1] = var8009d0f0[2] = -1;
	}

	node = modelGetPart(modeldef, MODELPART_REAPER_002D);

	if (node) {
		var8009d0f0[0] = modelFindNodeMtxIndex(node, 0);
	}

	node = modelGetPart(modeldef, MODELPART_REAPER_002E);

	if (node) {
		var8009d0f0[1] = modelFindNodeMtxIndex(node, 0);
	}

	node = modelGetPart(modeldef, MODELPART_REAPER_002F);

	if (node) {
		var8009d0f0[2] = modelFindNodeMtxIndex(node, 0);
	}
}

/**
 * Move/extend the scope on the gun model when the zoom function is used.
 */
void bgunUpdateSniperRifle(struct modeldef *modeldef, u8 *allocation)
{
	struct modelnode *nodes[4];
	f32 sp88[4] = {0, 0, 0, 0};
	s32 i;
	f32 f26;
	s32 mtxindex;
	struct coord sp70;

	f26 = 1.0f - (currentPlayerGetGunZoomFov() - 2.0f) / 58.0f;

	nodes[0] = modelGetPart(modeldef, MODELPART_SNIPERRIFLE_SCOPE1);
	nodes[1] = modelGetPart(modeldef, MODELPART_SNIPERRIFLE_SCOPE2);
	nodes[2] = modelGetPart(modeldef, MODELPART_SNIPERRIFLE_SCOPE3);
	nodes[3] = modelGetPart(modeldef, MODELPART_SNIPERRIFLE_SCOPE4);

	for (i = 0; i < ARRAYCOUNT(nodes); i++) {
		if (nodes[i]) {
			f32 f20 = f26 * 4.0f;
			mtxindex = modelFindNodeMtxIndex(nodes[i], 0);
			sp88[i] = f20 - i;

			if (f20 < i) {
				sp88[i] = 0.0f;
			}

			sp88[i] *= 100.0f;

			sp70.x = 0.0f;
			sp70.y = 0.0f;
			sp70.z = sp88[i];

			mtx4RotateVecInPlace((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)), &sp70);

			((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][0] += sp70.x;
			((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][1] += sp70.y;
			((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][2] += sp70.z;
		}
	}
}

/**
 * Animate the cartridge slider thing in the Devastator model.
 */
void bgunUpdateDevastator(struct hand *hand, u8 *allocation, struct modeldef *modeldef)
{
	struct modelnode *node = modelGetPart(modeldef, MODELPART_DEVASTATOR_0028);

	if (node) {
		s32 mtxindex = modelFindNodeMtxIndex(node, 0);
		struct coord sp24;

		hand->loadslide += 0.01f * LVUPDATE60FREAL();

		if (hand->loadslide > 1.0f) {
			hand->loadslide = 1.0f;
		}

		sp24.x = hand->loadslide * -10.0f * 1.636f;
		sp24.y = 0.0f;
		sp24.z = 0.0f;

		mtx4RotateVecInPlace((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)), &sp24);

		((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][0] += sp24.x;
		((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][1] += sp24.y;
		((Mtxf *)((uintptr_t)allocation + mtxindex * sizeof(Mtxf)))->m[3][2] += sp24.z;
	}
}

/**
 * Display the shotgun's starburst when appropriate.
 *
 * This logic is different to most guns, likely because most guns display the
 * starburst when the trigger is pressed while the shotgun has the double blast
 * function.
 */
void bgunUpdateShotgun(struct hand *hand, u8 *allocation, bool *arg2, struct modeldef *modeldef)
{
	if (hand->flashon) {
		hand->matmot1 = 1.0f;
	}

	if (hand->matmot1 > 0.0f) {
		hand->matmot1 -= LVUPDATE60FREAL() / 6.0f;

		if (hand->matmot1 < 0.01f) {
			hand->matmot1 = 0.0f;
		}
	}

	if (hand->matmot1 > 0.0f) {
		s32 sp34;
		s32 sp28[3] = {0, 0, 0};
		struct modelnode *node = modelGetPart(modeldef, MODELPART_SHOTGUN_0050);

		*arg2 = true;

		if (node) {
			sp34 = modelFindNodeMtxIndex(node, 0);

			mtx00015ea8((1.0f - hand->matmot1) * 8.0f + 0.5f, (Mtxf *)((uintptr_t)allocation + sp34 * sizeof(Mtxf)));
			mtx00015df0((1.0f - hand->matmot1) * 3.0f + 1.0f, (Mtxf *)((uintptr_t)allocation + sp34 * sizeof(Mtxf)));
			mtx00015e4c((1.0f - hand->matmot1) * 3.0f + 1.0f, (Mtxf *)((uintptr_t)allocation + sp34 * sizeof(Mtxf)));
		}
	}
}

void bgunUpdateLaser(struct hand *hand)
{
	if (hand->firing && hand->gset.weaponfunc == FUNC_SECONDARY) {
		if (hand->audiohandle == NULL && g_Vars.lvupdate240 != 0) {
			sndStart(var80095200, SFX_LASER_STREAM, &hand->audiohandle, -1, -1, -1, -1, -1);
		}

		hand->matmot1 = 1;
		return;
	}

	if (hand->matmot1 > 0) {
		hand->matmot1 -= LVUPDATE60FREAL() / 10.0f;
	} else if (hand->audiohandle != NULL && sndGetState(hand->audiohandle) != AL_STOPPED) {
		audioStop(hand->audiohandle);
	}
}

/**
 * Create ammo casing so they can be ejected during reload.
 */
void bgunUpdateMagnum(struct hand *hand, s32 handnum, struct modeldef *modeldef, Mtxf *mtx)
{
	f32 ground = g_Vars.currentplayer->vv_ground;
	s32 i;

	if (modeldef != NULL) {
		for (i = 0; i < hand->unk0cc8_04; i++) {
			struct modelnode *node = modelGetPart(modeldef, 0x0a + rngRandom() % 6);

			if (node) {
				s32 index = modelFindNodeMtxIndex(node, 0);
				Mtxf *tmp = mtx;
				Mtxf sp4c;

				tmp += index;

				mtx4Copy(tmp, &sp4c);
				mtx00015f04(9.999999f, &sp4c);
				mtx4MultMtx4InPlace(camGetProjectionMtxF(), &sp4c);

				casingCreateForHand(handnum, ground, &sp4c);
			}
		}
	}
}

/**
 * Create and/or update the rocket prop that sits inside the rocket launcher.
 */
void bgunUpdateRocketLauncher(struct hand *hand, s32 handnum, struct weaponfunc_shootprojectile *func)
{
	if (hand->rocket == NULL && hand->loadedammo[0] > 0) {
		bgunCreateHeldRocket(handnum, func);
	}

	if (hand->rocket) {
		bgunUpdateHeldRocket(handnum);
	}
}

void bgun0f0a45d0(struct hand *hand, struct modeldef *modeldef, bool isdetonator)
{
	struct modelnode *node = NULL;

	switch (hand->ejecttype) {
	case EJECTTYPE_GUN:
		if (isdetonator) {
			node = modelGetPart(modeldef, 0x2a);
		} else {
			node = modelGetPart(modeldef, 0x37);
		}
		break;
	case EJECTTYPE_GRENADEPIN:
		node = modelGetPart(modeldef, 0x2b);
		break;
	case EJECTTYPE_TRANQCASE:
		node = modelGetPart(modeldef, 0x2b);
		break;
	}

	if (node) {
		var8009d148 = modelFindNodeMtxIndex(node, 0);
		g_ModelJointPositionedFunc = bgun0f0a256c;
	} else {
		var8009d148 = -1;
	}
}

/**
 * With this function stubbed, the tranquilizer's spent ammo does not detach
 * when reloading, and the pulled pin on grenades and nbombs appears to move
 * with the model rather than detaching properly.
 */
void bgunTickEject(struct hand *hand, struct modeldef *modeldef, bool isdetonator)
{
	f32 lvupdate;
	struct coord spd0;
	Mtxf sp90;
	struct coord sp84;
	Mtxf sp44;
	s32 i;
	f32 newval;
	f32 mult = 3;

	switch (hand->ejectstate) {
	case EJECTSTATE_INIT:
		switch (hand->ejecttype) {
		case EJECTTYPE_GUN:
			hand->unk0d20.f[0] = (RANDOMFRAC() - 0.5f) * 0.5333333f * 0.0625f + 0.5333333f;
			hand->unk0d20.f[1] = RANDOMFRAC() * 2.5f * 0.0625f + 2.5f;
			hand->unk0d20.f[2] = 0.0f;
#if VERSION >= VERSION_PAL_BETA
			spd0.f[0] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 184.0f - 0.03414231f;
			spd0.f[1] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 184.0f - 0.03414231f;
			spd0.f[2] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 184.0f - 0.03414231f;
#else
			spd0.f[0] = RANDOMFRAC() * 2.0f * M_BADTAU / 184.0f - 0.03414231f;
			spd0.f[1] = RANDOMFRAC() * 2.0f * M_BADTAU / 184.0f - 0.03414231f;
			spd0.f[2] = RANDOMFRAC() * 2.0f * M_BADTAU / 184.0f - 0.03414231f;
#endif
			break;
		case EJECTTYPE_GRENADEPIN:
			hand->unk0d20.f[0] = -((RANDOMFRAC() - 0.5f) * 0.5333333f * 0.0625f + mult * 0.5333333f);
			hand->unk0d20.f[1] = RANDOMFRAC() * 2.5f * 0.125f + 2.5f;
			hand->unk0d20.f[2] = -(RANDOMFRAC() + 1.0f);
			spd0.f[0] = (RANDOMFRAC() + 3.0f) * PALUPF(M_BADTAU) / 208.0f;
#if VERSION >= VERSION_PAL_BETA
			spd0.f[1] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 544.0f - 0.0115481345f;
			spd0.f[2] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 544.0f - 0.0115481345f;
#else
			spd0.f[1] = RANDOMFRAC() * 2.0f * M_BADTAU / 544.0f - 0.0115481345f;
			spd0.f[2] = RANDOMFRAC() * 2.0f * M_BADTAU / 544.0f - 0.0115481345f;
#endif
			break;
		case EJECTTYPE_TRANQCASE:
			hand->unk0d20.f[0] = 0.0f;
			hand->unk0d20.f[1] = RANDOMFRAC() * 2.5f * 0.125f + 2.5f;
			hand->unk0d20.f[2] = (RANDOMFRAC() + 1.0f) * 0.25f;
			spd0.f[0] = (RANDOMFRAC() + 3.0f) * PALUPF(M_BADTAU) / 368.0f;
#if VERSION >= VERSION_PAL_BETA
			spd0.f[1] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 944.0f - 0.006654857f;
			spd0.f[2] = RANDOMFRAC() * PALUPF(2.0f * M_BADTAU) / 944.0f - 0.006654857f;
#else
			spd0.f[1] = RANDOMFRAC() * 2.0f * M_BADTAU / 944.0f - 0.006654857f;
			spd0.f[2] = RANDOMFRAC() * 2.0f * M_BADTAU / 944.0f - 0.006654857f;
#endif
			break;
		}

		hand->unk0d10 = hand->unk0d14 - 200.0f;

		mtx4LoadRotation(&spd0, &sp90);
		mtx4ToMtx3(&sp90, hand->unk0d50);

		if (g_Vars.lvupdate240 > 0 && hand->ejecttype != EJECTTYPE_GUN) {
			sp84.f[0] = (hand->posmtx.m[3][0] - hand->prevmtx.m[3][0]) / g_Vars.lvupdate60freal;
			sp84.f[1] = (hand->posmtx.m[3][1] - hand->prevmtx.m[3][1]) / g_Vars.lvupdate60freal;
			sp84.f[2] = (hand->posmtx.m[3][2] - hand->prevmtx.m[3][2]) / g_Vars.lvupdate60freal;

			mtx00017588(hand->posmtx.m, sp44.m);
			mtx4RotateVecInPlace(&sp44, &sp84);

			hand->unk0d20.f[0] += sp84.f[0] * 0.3f;
			hand->unk0d20.f[1] += sp84.f[1] * 0.3f;
			hand->unk0d20.f[2] += sp84.f[2] * 0.3f;
		}

		hand->ejectstate = EJECTSTATE_AIRBORNE;
		break;
	case EJECTSTATE_AIRBORNE:
		lvupdate = g_Vars.lvupdate60freal;

		if (g_Vars.currentplayer->isdead && lvupdate > 1.5f) {
			lvupdate = 1.5f;
		}

		newval = hand->unk0d20.f[1] - lvupdate * 0.2777778f;

		if (hand->unk0d18 < hand->unk0d10) {
			hand->ejectstate = EJECTSTATE_FINISHED;
			break;
		}

		hand->unk0d18 += lvupdate * 0.5f * (hand->unk0d20.f[1] + newval);
		hand->unk0d14 += lvupdate * hand->unk0d20.f[0];
		hand->unk0d1c += lvupdate * hand->unk0d20.f[2];

		hand->unk0d20.f[1] = newval;

		for (i = 0; i < g_Vars.lvupdate240; i++) {
			mtx00016110(hand->unk0d50, hand->unk0d2c);
		}

		break;
	}
}

void bgun0f0a4e44(struct hand *hand, struct weapon *weapondef, struct modeldef *modeldef,
		struct weaponfunc *funcdef, s32 maxburst, u8 *allocation, s32 weaponnum,
		bool **arg7, s32 mtxindex, Mtxf *arg9, Mtxf *arg10)
{
	Mtxf spd8;
	s32 index;
	s32 shotstotake;
	bool spc4[3] = {false, false, false};
	Mtxf *mtx;
	s32 i;
	s32 partnum;
	f32 spb4;
	f32 muzzlez;
	Mtxf sp70;

	index = hand->burstbullets % maxburst;
	shotstotake = hand->shotstotake;

	spb4 = RANDOMFRAC() * 0.25f + 1.0f;
	muzzlez = weapondef->muzzlez;

	mtx4LoadIdentity(&spd8);

	if (funcdef && (funcdef->flags & FUNCFLAG_00000001)) {
		mtx4LoadZRotation(RANDOMFRAC() * M_BADTAU, &spd8);
	}

	mtx4LoadZRotation((RANDOMFRAC() * 0.3 - 0.15), &spd8);

	mtx = (Mtxf *)allocation;
	mtx += mtxindex;

	mtx4MultMtx4InPlace(mtx, &spd8);
	mtx00015f04(spb4, &spd8);
	mtx00015ea8(muzzlez, &spd8);
	mtx4Copy(&spd8, mtx);

	if (shotstotake == 0 && weaponnum != WEAPON_REAPER) {
		shotstotake++;
	}

	for (i = 0; i < shotstotake; i++) {
		spc4[index] = true;
		index++;

		if (index >= maxburst) {
			index = 0;
		}
	}

	for (i = 0; i < maxburst; i++) {
		if (spc4[i] && arg7[i] != NULL) {
			*arg7[i] = true;
		}
	}

	for (partnum = 0x50; partnum <= 0x52; partnum++) {
		struct modelnode *node = modelGetPart(modeldef, partnum);
		struct coord sp60;

		if (node && weaponnum != WEAPON_REAPER && weaponnum != WEAPON_SHOTGUN) {
			struct modelrodata_position *rodata = &node->rodata->position;
			s32 mtxindex = modelFindNodeMtxIndex(node, 0);

			sp60.x = rodata->pos.x * spd8.m[0][0] + rodata->pos.y * spd8.m[1][0] + rodata->pos.z * spd8.m[2][0] + spd8.m[3][0];
			sp60.y = rodata->pos.x * spd8.m[0][1] + rodata->pos.y * spd8.m[1][1] + rodata->pos.z * spd8.m[2][1] + spd8.m[3][1];
			sp60.z = rodata->pos.x * spd8.m[0][2] + rodata->pos.y * spd8.m[1][2] + rodata->pos.z * spd8.m[2][2] + spd8.m[3][2];

			mtx4LoadIdentity(&sp70);
			mtx4Align(sp70.m, RANDOMFRAC() * M_BADTAU, -sp60.x, -sp60.y, -sp60.z);
			mtx00015f04(0.10000001f * spb4, &sp70);

			mtx = (Mtxf *)allocation;

			mtx00016e98(arg10->m, 0, mtx->m[3][0] - hand->aimpos.x, mtx->m[3][1] - hand->aimpos.y, mtx->m[3][2] - hand->aimpos.z);
			mtx4MultMtx4InPlace(arg10, &sp70);
			mtx00016710(muzzlez, sp70.m);
			mtx4MultMtx4InPlace(arg9, &sp70);
			mtx4SetTranslation(&sp60, &sp70);

			mtx = (Mtxf *)allocation;
			mtx += mtxindex;

			mtx4Copy(&sp70, mtx);
		}
	}
}

/**
 * Create casing and beam for a fired weapon,
 * and uncloak if the weapon is a throwable or fired projectile.
 */
void bgunCreateFx(struct hand *hand, s32 handnum, struct weaponfunc *funcdef, s32 weaponnum, struct modeldef *modeldef, u8 *allocation)
{
	f32 ground;
	bool createbeam = true;

	g_Vars.currentplayer->gunctrl.throwing = false;

	if (funcdef) {
		ground = g_Vars.currentplayer->vv_ground;

		if (modeldef && weaponnum != WEAPON_DY357MAGNUM && weaponnum != WEAPON_DY357LX) {
			s32 partnum = MODELPART_GUN_CARTEJECTPOS;
			struct modelnode *node;

			if (weaponnum == WEAPON_REAPER) {
				partnum = (hand->burstbullets & 1) == 1 ? MODELPART_REAPER_CARTEJECTPOS1 : MODELPART_REAPER_CARTEJECTPOS2;
			}

			node = modelGetPart(modeldef, partnum);

			if (node) {
				Mtxf *mtx = (Mtxf *)allocation;
				Mtxf sp24;

				mtx += modelFindNodeMtxIndex(node, 0);

				mtx4Copy(mtx, &sp24);
				mtx00015f04(9.999999f, &sp24);
				mtx4MultMtx4InPlace(camGetProjectionMtxF(), &sp24);

				casingCreateForHand(handnum, ground, &sp24);
			} else {
				casingCreateForHand(handnum, ground, &hand->posmtx);
			}

			bgunSetPartVisible(MODELPART_GUN_CARTFLAPCLOSED, false, hand, modeldef);
			bgunSetPartVisible(MODELPART_GUN_CARTFLAPOPEN, true, hand, modeldef);
		}

		if (funcdef->type == INVENTORYFUNCTYPE_SHOOT_PROJECTILE) {
			chrUncloakTemporarily(g_Vars.currentplayer->prop->chr);
		} else if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_THROW) {
			chrUncloakTemporarily(g_Vars.currentplayer->prop->chr);
		}
	}

	if (funcdef) {
		if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_MELEE || (funcdef->type & INVENTORYFUNCTYPE_0200)) {
			createbeam = false;
		}

		if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_SPECIAL) {
			createbeam = false;
		}

		if ((funcdef->type & 0xff) == INVENTORYFUNCTYPE_THROW) {
			createbeam = false;
		}
	}

	if (createbeam) {
		switch (weaponnum) {
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
		case WEAPON_K7AVENGER:
		case WEAPON_AR34:
		case WEAPON_SUPERDRAGON:
		case WEAPON_REAPER:
		case WEAPON_SNIPERRIFLE:
		case WEAPON_FARSIGHT:
		case WEAPON_TRANQUILIZER:
			beamCreateForHand(handnum);
			hand->numfires++;
			return;
		case WEAPON_LASER:
			hand->numfires++;
			beamCreateForHand(handnum);
			break;
		}
	}
}


// offset calculation from NeonNyan/perfect-dark

static inline f32 bgunGetFovOffsetZ(void)
{
	return (PLAYER_DEFAULT_FOV - 60.f) / 3.f;
}

static inline f32 bgunGetFovOffsetY(void)
{
	return (PLAYER_DEFAULT_FOV - 60.f) / (2.75f * 4.f);
}


void bgun0f0a5550(s32 handnum)
{
	u8 *mtxallocation;
	Mtxf sp2c4;
	Mtxf sp284;
	struct modeldef *modeldef = NULL;
	struct coord sp274 = {0, 0, 0};
	Mtxf sp234;
	Mtxf sp1f4;
	union modelrodata *rodata;
	bool *sp1e4[3] = {NULL, NULL, NULL};
	s32 sp1e0 = 0;
	struct modelnode *node;
	struct player *player = g_Vars.currentplayer;
	struct hand *hand = player->hands + handnum;
	struct weaponfunc *funcdef;
	struct weaponfunc_shoot *shootfunc = NULL;
	s32 i;
	s32 weaponnum = bgunGetWeaponNum2(handnum);
	struct weapon *weapondef;
	Mtxf *mtx;
	bool isdetonator = false;
	f32 fspare1;
	f32 fspare2;
	struct coord sp1a4;

	/* B-246 round-6 instrumentation: matrix-jump-on-fire diagnosis. The
	 * idle vs fire matrix paths diverge at the a0 decision near line 8090
	 * (idle uses cached unk0dd8; fire calls modelSetMatricesWithAnim
	 * fresh). Logging useposrot, animmode, posoffset, damplook, dampup at
	 * function entry gives the inputs to that decision. Throttled: every
	 * 30 frames OR on animmode change OR on state change (RIGHT hand only
	 * because that's where the firing weapon lives). */
	if (g_Vars.currentplayernum == 0 && handnum == HAND_RIGHT) {
		static s32 s_last_log_frame = -1;
		static s32 s_last_animmode = -1;
		static s32 s_last_state = -1;
		s32 cur_animmode = (s32)hand->animmode;
		s32 cur_state = (s32)hand->state;
		bool periodic = ((g_Vars.lvframenum % 30) == 11 && (s32)g_Vars.lvframenum != s_last_log_frame);
		bool changed = (cur_animmode != s_last_animmode) || (cur_state != s_last_state);
		if (periodic || changed) {
			s_last_log_frame = (s32)g_Vars.lvframenum;
			s_last_animmode = cur_animmode;
			s_last_state = cur_state;
			sysLogPrintf(LOG_NOTE,
				"LOG.WPN.DIAG: bgun0f0a5550 enter player=0 hand=R wpn=%d frame=%d state=%d sm=%d animmode=%d useposrot=%d visible=%d inuse=%d posoffset=(%.2f,%.2f,%.2f) rotxoffset=%.3f damplook=(%.3f,%.3f,%.3f) dampup=(%.3f,%.3f,%.3f) unk0dd4=%d",
				(s32)weaponnum,
				(s32)g_Vars.lvframenum,
				cur_state,
				(s32)hand->stateminor,
				cur_animmode,
				(s32)hand->useposrot,
				(s32)hand->visible,
				(s32)hand->inuse,
				hand->posoffset.x, hand->posoffset.y, hand->posoffset.z,
				hand->rotxoffset,
				hand->damplook.x, hand->damplook.y, hand->damplook.z,
				hand->dampup.x, hand->dampup.y, hand->dampup.z,
				(s32)player->hands[HAND_RIGHT].unk0dd4);
		}
	}
	Mtxf sp164;
	Mtxf sp124;
	struct coord sp118;
	s32 j;

	weapondef = weaponFindById(weaponnum);

	if (handnum == HAND_LEFT && weaponnum == WEAPON_REMOTEMINE) {
		isdetonator = true;
	}

	funcdef = gsetGetWeaponFunction2(&hand->gset);

	if (funcdef && (funcdef->type & 0xff) == INVENTORYFUNCTYPE_SHOOT) {
		shootfunc = (struct weaponfunc_shoot *)funcdef;
	}

	bgunUpdateBlend(hand, handnum);

	if (handnum == HAND_RIGHT) {
		if (weaponHasFlag(bgunGetWeaponNum2(HAND_LEFT), WEAPONFLAG_00000040)) {
			hand->xshift += 2.0f * g_Vars.lvupdate60freal / 240.0f;

			if (hand->xshift > 2.0f) {
				hand->xshift = 2.0f;
			}
		} else {
			hand->xshift -= 2.0f * g_Vars.lvupdate60freal / 240.0f;

			if (hand->xshift < 0.0f) {
				hand->xshift = 0.0f;
			}
		}
	} else {
		if (weaponHasFlag(bgunGetWeaponNum2(HAND_RIGHT), WEAPONFLAG_00000040)) {
			hand->xshift -= 2.0f * g_Vars.lvupdate60freal / 240.0f;

			if (hand->xshift < -2.0f) {
				hand->xshift = -2.0f;
			}
		} else {
			hand->xshift += 2.0f * g_Vars.lvupdate60freal / 240.0f;

			if (hand->xshift > 0.0f) {
				hand->xshift = 0.0f;
			}
		}
	}

	if (handnum == HAND_RIGHT) {
		sp274.x = func0f0b131c(handnum) + hand->damppos.f[0] + hand->adjustpos.f[0];
		sp274.y = weapondef->posy + hand->damppos.f[1] + hand->adjustpos.f[1];
		sp274.z = weapondef->posz + hand->damppos.f[2] + hand->adjustpos.f[2];
	} else if (isdetonator) {
		sp274.x = 6.5f + hand->damppos.f[0] - hand->adjustpos.f[0];
		sp274.y = -16.5f + hand->damppos.f[1] + hand->adjustpos.f[1];
		sp274.z = -16.0f + hand->damppos.f[2] + hand->adjustpos.f[2];
	} else {
		sp274.x = func0f0b131c(handnum) + hand->damppos.f[0] - hand->adjustpos.f[0];
		sp274.y = weapondef->posy + hand->damppos.f[1] + hand->adjustpos.f[1];
		sp274.z = weapondef->posz + hand->damppos.f[2] + hand->adjustpos.f[2];
	}

	sp274.y += player->guncloseroffset * 5.0f / -90.0f * 50.0f;
	sp274.z -= player->guncloseroffset * 15.0f / -90.0f * 50.0f;

	// adjust viewmodel position for different FOVs
	sp274.y -= bgunGetFovOffsetY();
	sp274.z += bgunGetFovOffsetZ();

	if (hand->firing && shootfunc && g_Vars.lvupdate240 != 0 && shootfunc->recoilsettings != NULL) {
		sp274.x += (RANDOMFRAC() - 0.5f) * shootfunc->recoilsettings->xrange * hand->finalmult[0];
		sp274.y += (RANDOMFRAC() - 0.5f) * shootfunc->recoilsettings->yrange * hand->finalmult[0];
		sp274.z += (RANDOMFRAC() - 0.5f) * shootfunc->recoilsettings->zrange * hand->finalmult[0];
	}

	hand->fspare1 = (player->crosspos2[0] - camGetScreenLeft() - camGetScreenWidth() * 0.5f) * weapondef->aimsettings->guntransside / (camGetScreenWidth() * 0.5f);

	if (player->crosspos2[1] - camGetScreenTop() > camGetScreenHeight() * 0.5f) {
		hand->fspare2 = (player->crosspos2[1] - camGetScreenTop() - camGetScreenHeight() * 0.5f) * weapondef->aimsettings->guntransdown / (camGetScreenHeight() * 0.5f);
	} else {
		hand->fspare2 = (player->crosspos2[1] - camGetScreenTop() - camGetScreenHeight() * 0.5f) * weapondef->aimsettings->guntransup / (camGetScreenHeight() * 0.5f);
	}

	fspare1 = hand->fspare1;
	fspare2 = hand->fspare2;

	sp274.f[0] += fspare1;
	sp274.f[1] -= fspare2;

	hand->visible = true;

	/* Issue 1 root-cause fix (2026-04-25, B-246): the legacy gate blocked
	 * visibility whenever `hand->mode == HANDMODE_6` or `HANDMODE_7`. Those
	 * modes are intermediate values set during the gun-change transition
	 * (HANDMODE_6 set on LOWER -> LOAD at bondgun.c:2967, HANDMODE_7 set in
	 * LOAD when `bgun0f09bf44` returns true at bondgun.c:2991). The original
	 * design intent was to hide the FP weapon during the lower+raise
	 * animations, which corresponds to the LOWER and RAISE stateminor stages
	 * inside the CHANGEGUN state.
	 *
	 * Mike's instrumented playtest log (`019dc344`, build `0dc0173d`) showed
	 * the player's right hand stuck with `hand_mode=6` despite all other
	 * "weapon ready" indicators passing: `gunmemowner=BONDGUN`, `gunmemtype=1`
	 * (UNARMED loaded), `gunmemnew=-1` (no pending load), `masterloadstate=
	 * MASTERLOADSTATE_LOADED`, `inuse=1`, weapon flags clean. The `mode=6`
	 * gate fired and forced `visible=false` even though the weapon was fully
	 * loaded and the master state machine had completed.
	 *
	 * Static analysis says the LOAD/HANDMODE_6 path (bondgun.c:2989) should
	 * advance mode to HANDMODE_7 every frame because `bgun0f09bf44`'s
	 * conditions all match Mike's data: bgunIsLoaded TRUE, switchtoweaponnum
	 * == -1, gunmemnew < 0, no LEFT-hand-mismatch, hands[1].state probably
	 * not RELOAD. Yet runtime evidence proved mode never transitioned. Without
	 * a fourth instrumented round to fingerprint why bgun0f09bf44 is
	 * effectively returning false (or why bgunTickGameplay's per-frame state
	 * tick is not executing despite tickmode=NORMAL), the structural fix is
	 * to refactor the visibility gate so the mode check is scoped to the
	 * actual lower/raise transition stateminors -- which is what the original
	 * design intent was.
	 *
	 * New gate: hide on mode=6/7 ONLY when state == CHANGEGUN AND stateminor
	 * is LOWER or RAISE (the two stateminors where the gun is genuinely off
	 * screen / mid-transition). LOAD and EQUIP are stateminors where the gun
	 * model is on-screen / settled and should render. If the state machine
	 * gets stuck in LOAD/HANDMODE_6 (Mike's case), the new gate lets the
	 * weapon render because LOAD is a "settled" stateminor, not a transition
	 * animation. The other gates (bgunIsLoaded, inuse, gunmemtype) still
	 * guarantee the weapon model is actually loaded and bound to the hand.
	 *
	 * Bot path is unaffected: bots don't go through this rendering path at
	 * all (they have a separate third-person attach pipeline in
	 * playerTickChrBody). */
	const bool inHideTransition =
			(hand->state == HANDSTATE_CHANGEGUN)
			&& (hand->stateminor == HANDSTATEMINOR_CHANGEGUN_LOWER
				|| hand->stateminor == HANDSTATEMINOR_CHANGEGUN_RAISE);

	if (!weaponHasFlag(weaponnum, WEAPONFLAG_00000040)
			|| weaponHasFlag(weaponnum, WEAPONFLAG_00000080)
			|| (inHideTransition
					&& (hand->mode == HANDMODE_6 || hand->mode == HANDMODE_7))
			|| !bgunIsLoaded()
			|| hand->inuse == false
			|| bgunGetGunMemType() == 0
			|| player->gunctrl.gunmodeldef == NULL) {
		hand->visible = false;
		/* B-246 round-3 instrumentation: log which visibility gate(s) fired
		 * for player 0. Captured every ~120 ticks while visible-gate fires.
		 * If multiple gates are always wedged, that's the diagnosis. */
		if (g_Vars.currentplayernum == 0
				&& (g_Vars.lvframenum % 120) == 11) {
			s32 gate_no_flag40 = !weaponHasFlag(weaponnum, WEAPONFLAG_00000040) ? 1 : 0;
			s32 gate_flag80 = weaponHasFlag(weaponnum, WEAPONFLAG_00000080) ? 1 : 0;
			s32 gate_mode6 = (hand->mode == HANDMODE_6) ? 1 : 0;
			s32 gate_mode7 = (hand->mode == HANDMODE_7) ? 1 : 0;
			s32 gate_notloaded = !bgunIsLoaded() ? 1 : 0;
			s32 gate_notinuse = (hand->inuse == false) ? 1 : 0;
			s32 gate_memtype0 = (bgunGetGunMemType() == 0) ? 1 : 0;
			s32 gate_nomodel = (player->gunctrl.gunmodeldef == NULL) ? 1 : 0;
			sysLogPrintf(LOG_NOTE,
				"LOG.WPN.DIAG: visibility-gate-fail player=0 hand=%d wpn=%d frame=%d "
				"gates: noFlag40=%d flag80=%d mode6=%d mode7=%d notLoaded=%d notInuse=%d memType0=%d noModel=%d "
				"raw: hand_mode=%d gunmemowner=%d gunmemtype=%d gunmemnew=%d masterload=%d",
				handnum, (s32)weaponnum, g_Vars.lvframenum,
				gate_no_flag40, gate_flag80, gate_mode6, gate_mode7,
				gate_notloaded, gate_notinuse, gate_memtype0, gate_nomodel,
				(s32)hand->mode,
				(s32)g_Vars.currentplayer->gunctrl.gunmemowner,
				(s32)g_Vars.currentplayer->gunctrl.gunmemtype,
				(s32)g_Vars.currentplayer->gunctrl.gunmemnew,
				(s32)g_Vars.currentplayer->gunctrl.masterloadstate);
		}
	}

	if (hand->visible) {
		modeldef = player->gunctrl.gunmodeldef;
		mtxallocation = gfxAllocate(modeldef->nummatrices * sizeof(Mtxf));

		if (weaponHasFlag(weaponnum, WEAPONFLAG_02000000)) {
			for (i = 0; i < modeldef->nummatrices; i++) {
				mtx = (Mtxf *)(mtxallocation + i * sizeof(Mtxf));
				mtx4LoadIdentity(mtx);
			}
		}

		bgunExecuteModelCmdList(hand->unk0dcc);

		if (player->gunctrl.handmodeldef != NULL) {
			bgunExecuteModelCmdList(hand->unk0dd0);
		}

		bgun0f098030(hand, modeldef);

		if (weaponHasFlag(weaponnum, WEAPONFLAG_00002000)) {
			bgun0f0981e8(hand, modeldef);
		}
	}

	mtx4LoadIdentity(&sp234);

	if (PLAYERCOUNT() == 1 && weaponHasFlag(weaponnum, WEAPONFLAG_GANGSTA)) {
		bgunUpdateGangsta(hand, handnum, &sp274, funcdef, &sp284, &sp234);
	}

	if (hand->useposrot) {
		sp274.f[0] += hand->posrotmtx.m[3][0];
		sp274.f[1] += hand->posrotmtx.m[3][1];
		sp274.f[2] += hand->posrotmtx.m[3][2];

		mtx00015be0(&hand->posrotmtx, &sp234);

		sp234.m[3][0] = 0.0f;
		sp234.m[3][1] = 0.0f;
		sp234.m[3][2] = 0.0f;
	} else {
		hand->rotxoffset = 0.0f;
		hand->posoffset.x = 0.0f;
		hand->posoffset.y = 0.0f;
		hand->posoffset.z = 0.0f;
	}

	mtx00016d58(&sp284, 0.0f, 0.0f, 0.0f,
			hand->damplook.x, hand->damplook.y, hand->damplook.z,
			hand->dampup.x, hand->dampup.y, hand->dampup.z);

	mtx00015be0(&sp284, &sp234);

	sp1a4.x = 0.0f;
	sp1a4.y = M_PI;
	sp1a4.z = 0.0f;

	mtx4LoadRotation(&sp1a4, &sp164);

	sp1a4.y = 0.0f;

	bgun0f0a24f0(&sp118, handnum);

	sp1a4.y = -bgun0f0a2498(sp118.x, sp118.z, sp274.f[0], sp274.f[2]);
	sp1a4.x = bgun0f0a2498(sp118.y, sp118.z, sp274.f[1], sp274.f[2]);

	hand->lastrotangx = sp1a4.f[0];
	hand->lastrotangy = sp1a4.f[1];

	mtx4LoadRotation(&sp1a4, &sp124);
	mtx4MultMtx4(&sp124, &sp164, &sp284);
	mtx4MultMtx4InPlace(&sp284, &sp234);
	mtx4Copy(&sp234, &sp2c4);
	mtx4SetTranslation(&sp274, &sp2c4);

	mtx4Copy(&sp2c4, &hand->cammtx);
	mtx4Copy(&hand->posmtx, &hand->prevmtx);

	mtx00015be4(camGetProjectionMtxF(), &hand->cammtx, &hand->posmtx);

	if (hand->visible) {
		for (j = 0x5a; j < 0x5d; j++) {
			node = modelGetPart(modeldef, j);

			if (node) {
				rodata = node->rodata;
				sp1e4[sp1e0] = (bool *)&hand->unk0a6c[rodata->toggle.rwdataindex];
				sp1e0++;
			}
		}

		hand->gunmodel.matrices = (Mtxf *)mtxallocation;
		hand->handmodel.matrices = (Mtxf *)mtxallocation;

		if (weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP) && handnum == HAND_LEFT) {
			mtx00015e24(-1, &sp2c4);
		}

		mtx00015f04(0.10000001f, &sp2c4);

		mtx4Copy(&sp2c4, (Mtxf *)mtxallocation);

		if (hand->unk0cc8_04 > 0) {
			switch (weaponnum) {
			case WEAPON_GRENADE:
			case WEAPON_NBOMB:
				hand->ejectstate = EJECTSTATE_INIT;
				hand->ejecttype = EJECTTYPE_GRENADEPIN;
				break;
			case WEAPON_TRANQUILIZER:
				hand->ejectstate = EJECTSTATE_INIT;
				hand->ejecttype = EJECTTYPE_TRANQCASE;
				break;
			}
		}

		var8009d144 = hand;

		if (hand->ejectstate > EJECTSTATE_INACTIVE) {
			bgun0f0a45d0(hand, modeldef, isdetonator);
		}

		var8009d0dc = -1;
		var8009d0f0[0] = var8009d0f0[1] = var8009d0f0[2] = -1;

		switch (weaponnum) {
		case WEAPON_LASER:
			bgunUpdateLaser(hand);
			break;
		case WEAPON_REAPER:
			bgunUpdateReaper(hand, modeldef);
			break;
		}

		{
			struct modelrenderdata renderdata = {NULL, true, 3};
#if VERSION >= VERSION_PAL_BETA
			bool a3 = false;
#endif
			u32 sp80;
			struct coord sp74;
			s32 stack;
			s32 sp6c;
			struct modeldef *gunmodeldef = hand->gunmodel.definition;
			bool use_static_source_matrices = gunmodeldef != NULL
				&& gunmodeldef->skel == NULL
				&& modAssetCompilerModeldefIsGenerated(gunmodeldef);

			renderdata.unk00 = &sp2c4;
			renderdata.unk10 = hand->gunmodel.matrices;

#if VERSION >= VERSION_PAL_BETA
			switch (modelGetAnimNum(&hand->gunmodel)) {
			case ANIM_GUN_CROSSBOW_EQUIP:
			case ANIM_GUN_LAPTOP_EQUIP:
			case ANIM_GUN_LAPTOP_UNEQUIP:
			case ANIM_GUN_LAPTOP_RELOAD:
			case ANIM_GUN_FALCON2_RELOAD:
			case ANIM_GUN_CMP150_RELOAD:
			case ANIM_GUN_FARSIGHT_SHOOT:
			case ANIM_GUN_SHOTGUN_SHOOT_SINGLE:
			case ANIM_GUN_REAPER_SHOOT:
			case ANIM_GUN_MAGSEC4_RELOAD:
			case ANIM_GUN_CYCLONE_RELOAD:
			case ANIM_GUN_SNIPER_RELOAD:
			case ANIM_GUN_PHOENIX_RELOAD:
			case ANIM_GUN_FALCON2_RELOAD_SCOPE:
			case ANIM_GUN_REMOTEMINE_EQUIP:
				a3 = 1;
				break;
			}
#endif

			/* B-246 round-10 Phase B (cache drop): the per-hand matrix
			 * cache `unk0dd8` was filled exactly once at weapon-load
			 * time, when the gun's animation was at anim 0 frame 0
			 * (model T-pose), and reused for every IDLE render frame
			 * via the prior `if (a0)` branch. PD2 / AllInOne idle
			 * authoring uses different anim tracks (e.g. anim 236
			 * frame 17 for FALCON2) so the cache encoded a stale
			 * T-pose vs the live anim state. Idle frames rendered the
			 * gun at its model-local origin instead of where the IDLE
			 * animation's keyframes place it. Fire and reload were
			 * unaffected because they bypassed the cache via the
			 * fresh-anim path.
			 *
			 * Phase B drops the cache entirely: every render frame now
			 * runs `modelSetMatricesWithAnim` against the gun's
			 * current anim state, identical to what fire and reload
			 * already did. `unk0dd4` and `unk0dd8` in struct hand
			 * remain allocated as dead state for minimum diff; the
			 * Phase A predicate `bgunMatrixCacheIsStale` at
			 * port/src/bondgun_cache.c stays in the codebase with
			 * its [bondgun][matrix-cache] pd-tests cases pinning the
			 * spec for any future reintroduction. */
#if VERSION >= VERSION_PAL_BETA
			var8005efd8_2 = true;

			if (a3) {
				var8005efb0_2 = true;
			}

			if (use_static_source_matrices) {
				hand->gunmodel.matrices = renderdata.unk10;
				renderdata.unk10 += hand->gunmodel.definition->nummatrices;
				modelUpdateMatrices(&renderdata, &hand->gunmodel);
			} else {
				modelSetMatricesWithAnim(&renderdata, &hand->gunmodel);
			}

			var8005efd8_2 = false;

			if (a3) {
				var8005efb0_2 = false;
			}
#else
			if (use_static_source_matrices) {
				hand->gunmodel.matrices = renderdata.unk10;
				renderdata.unk10 += hand->gunmodel.definition->nummatrices;
				modelUpdateMatrices(&renderdata, &hand->gunmodel);
			} else {
				modelSetMatricesWithAnim(&renderdata, &hand->gunmodel);
			}
#endif

			/* B-246 round-8 instrumentation: bone-snapshot at IDLE<->FIRE
			 * state transitions. Discriminates between four LIVE candidates
			 * for the idle-vs-animation positional asymmetry Mike observed:
			 *   H-MTX-A2     (cache faithfully reproduces wrong IDLE rest pose)
			 *   H-IDLE-POSE-A (idle anim data positions gun root wrong)
			 *   H-HAND-RIG-A (hand rig idle pose places hand bone wrong;
			 *                 gun rides along via shared matrix buffer per
			 *                 bondgun.c:11371)
			 *   H-PARENT-A   (gun attached to wrong bone; fire anim absolute
			 *                 writes mask the wrong parent inheritance)
			 *
			 * Pure observational. Throttle: one sample on first IDLE-state
			 * frame after FIRE, one on first FIRE-state frame after IDLE.
			 * RIGHT hand only. RIGHT hand is where the firing weapon lives
			 * in this codebase; HAND_LEFT only matters for dual-wield which
			 * Mike's playtests do not exercise.
			 *
			 * Read interpretation (in next playtest log):
			 *   - gun_root_t differs significantly between IDLE and FIRE
			 *     phases -> the FIRE animation is producing a different
			 *     gun-root translation than IDLE; H-IDLE-POSE-A or H-PARENT-A
			 *     consistent
			 *   - hand_root_t shifts in lockstep with gun_root_t -> hand
			 *     rig itself is moving; H-HAND-RIG-A consistent
			 *   - hand_root_t stable while gun_root_t shifts -> gun-only
			 *     issue; H-HAND-RIG-A demoted, H-IDLE-POSE-A or H-PARENT-A
			 *     consistent
			 *   - mtx_alias=1 confirms gun and hand share the matrix buffer
			 *     (expected per bondgun.c:11371). If gun_root_t != hand_root_t
			 *     while alias=1, they index into different matrix slots
			 *     within the shared buffer
			 *   - gun_hand_idx + gun_hand_t captures the gun model's
			 *     MODELPART_HAND_RIGHT node matrix -- the gun's expected
			 *     attachment to the hand bone. If this matrix's translation
			 *     differs between IDLE and FIRE, the parent is being
			 *     repositioned by animation
			 *   - useposrot, animmode, animnum captured for cross-reference
			 *     against the existing per-frame samples */
			if (g_Vars.currentplayernum == 0 && handnum == HAND_RIGHT && hand->visible) {
				static s32 s_last_phase = -1;
				s32 cur_phase;
				if ((s32)hand->state == HANDSTATE_IDLE) {
					cur_phase = 0;
				} else if ((s32)hand->state == HANDSTATE_ATTACK) {
					cur_phase = 1;
				} else {
					cur_phase = -1;
				}

				if (cur_phase >= 0 && cur_phase != s_last_phase) {
					struct modeldef *gd = hand->gunmodel.definition;
					struct modeldef *hd = hand->handmodel.definition;
					Mtxf *gun_mtx = hand->gunmodel.matrices;
					Mtxf *hand_mtx = hand->handmodel.matrices;
					struct modelnode *gun_hand_node = (gd != NULL) ? modelGetPart(gd, MODELPART_HAND_RIGHT) : NULL;
					s32 gun_hand_mtxidx = (gun_hand_node != NULL) ? modelFindNodeMtxIndex(gun_hand_node, 0) : -1;
					Mtxf *gun_hand_mtx = NULL;
					if (gun_hand_mtxidx >= 0 && gd != NULL && gun_hand_mtxidx < (s32)gd->nummatrices) {
						gun_hand_mtx = &gun_mtx[gun_hand_mtxidx];
					}

					s_last_phase = cur_phase;

					if (gun_mtx != NULL) {
						const char *phase_str = (cur_phase == 1) ? "FIRE" : "IDLE";
						sysLogPrintf(LOG_NOTE,
							"LOG.WPN.DIAG: bone-snapshot phase=%s wpn=%d "
							"gun_root_t=[%.3f,%.3f,%.3f] gun_root_r0=[%.3f,%.3f,%.3f,%.3f] "
							"hand_root_t=[%.3f,%.3f,%.3f] hand_root_r0=[%.3f,%.3f,%.3f,%.3f] "
							"gun_hand_idx=%d gun_hand_t=[%.3f,%.3f,%.3f] "
							"gun_def=%p hand_def=%p mtx_alias=%d nummtx_gun=%d nummtx_hand=%d "
							"useposrot=%d animmode=%d animnum=%d animframe=%.2f frame=%d",
							phase_str, (s32)weaponnum,
							gun_mtx[0].m[3][0], gun_mtx[0].m[3][1], gun_mtx[0].m[3][2],
							gun_mtx[0].m[0][0], gun_mtx[0].m[0][1], gun_mtx[0].m[0][2], gun_mtx[0].m[0][3],
							hand_mtx ? hand_mtx[0].m[3][0] : 0.0f,
							hand_mtx ? hand_mtx[0].m[3][1] : 0.0f,
							hand_mtx ? hand_mtx[0].m[3][2] : 0.0f,
							hand_mtx ? hand_mtx[0].m[0][0] : 0.0f,
							hand_mtx ? hand_mtx[0].m[0][1] : 0.0f,
							hand_mtx ? hand_mtx[0].m[0][2] : 0.0f,
							hand_mtx ? hand_mtx[0].m[0][3] : 0.0f,
							gun_hand_mtxidx,
							gun_hand_mtx ? gun_hand_mtx->m[3][0] : 0.0f,
							gun_hand_mtx ? gun_hand_mtx->m[3][1] : 0.0f,
							gun_hand_mtx ? gun_hand_mtx->m[3][2] : 0.0f,
							(void *)gd, (void *)hd,
							(s32)(gun_mtx == hand_mtx ? 1 : 0),
							(s32)(gd ? gd->nummatrices : 0),
							(s32)(hd ? hd->nummatrices : 0),
							(s32)hand->useposrot, (s32)hand->animmode,
							(s32)modelGetAnimNum(&hand->gunmodel),
							modelGetCurAnimFrame(&hand->gunmodel),
							(s32)g_Vars.lvframenum);
					}
				}
			}

			g_ModelJointPositionedFunc = 0;

			node = modelGetPart(modeldef, MODELPART_GUN_SLIDE);

			if (node) {
				sp80 = modelFindNodeMtxIndex(node, 0);

				bgunUpdateSlide(handnum);

				sp74.f[0] = 0.0f;
				sp74.f[1] = 0.0f;
				sp74.f[2] = -hand->slidetrans;

				mtx = (Mtxf *)mtxallocation;
				mtx += sp80;

				mtx4RotateVecInPlace(mtx, &sp74);

				mtx->m[3][0] += sp74.f[0];
				mtx->m[3][1] += sp74.f[1];
				mtx->m[3][2] += sp74.f[2];
			}

			if (sp1e4[0] != NULL) {
				*sp1e4[0] = false;
			}

			if (sp1e4[1] != NULL) {
				*sp1e4[1] = false;
			}

			if (sp1e4[2] != NULL) {
				*sp1e4[2] = false;
			}

			switch (weaponnum) {
			case WEAPON_SNIPERRIFLE:
				bgunUpdateSniperRifle(modeldef, mtxallocation);
				break;
			case WEAPON_DEVASTATOR:
				bgunUpdateDevastator(hand, mtxallocation, modeldef);
				break;
			case WEAPON_SHOTGUN:
				bgunUpdateShotgun(hand, mtxallocation, sp1e4[0], modeldef);
				break;
			}

			node = modelGetPart(modeldef, MODELPART_GUN_MUZZLEPOS);

			if (weaponnum == WEAPON_REAPER) {
				if (hand->flashon || hand->firing) {
					node = modelGetPart(modeldef, MODELPART_REAPER_001E + (hand->burstbullets % 3));
				} else {
					node = modelGetPart(modeldef, MODELPART_REAPER_001E + (g_Vars.lvframenum % 3));
				}
			}

			if (node) {
				sp6c = modelFindNodeMtxIndex(node, 0);

				mtx = (Mtxf *)mtxallocation;
				mtx += sp6c;

				hand->muzzlepos.f[0] = mtx->m[3][0];
				hand->muzzlepos.f[1] = mtx->m[3][1];
				hand->muzzlepos.f[2] = mtx->m[3][2];

				mtx4Copy(mtx, &hand->muzzlemat);
				mtx4TransformVecInPlace(camGetProjectionMtxF(), &hand->muzzlepos);

				hand->muzzlez = -((Mtxf *)((uintptr_t)mtxallocation + sp6c * sizeof(Mtxf)))->m[3][2];

				if (hand->flashon && sp1e0 > 0 && weaponnum != WEAPON_SHOTGUN && g_Vars.lvupdate240 != 0) {
					bgun0f0a4e44(hand, weapondef, modeldef, funcdef, sp1e0, mtxallocation, weaponnum, sp1e4, sp6c, &sp234, &sp1f4);
				}
			} else if (weaponnum == WEAPON_GRENADE
					|| weaponnum == WEAPON_TIMEDMINE
					|| weaponnum == WEAPON_REMOTEMINE
					|| weaponnum == WEAPON_PROXIMITYMINE
					|| weaponnum == WEAPON_NBOMB) {
				sp6c = modelFindNodeMtxIndex(modelGetPart(modeldef, MODELPART_GUN_HOLDPOS), 0);

				mtx = (Mtxf *)mtxallocation;
				mtx += sp6c;

				hand->muzzlepos.x = mtx->m[3][0];
				hand->muzzlepos.y = mtx->m[3][1];
				hand->muzzlepos.z = mtx->m[3][2];

				mtx4Copy(mtx, &hand->muzzlemat);
				mtx4TransformVecInPlace(camGetProjectionMtxF(), &hand->muzzlepos);

				hand->muzzlez = -((Mtxf *)((uintptr_t)mtxallocation + sp6c * sizeof(Mtxf)))->m[3][2];
			} else {
				hand->muzzlepos.x = hand->posmtx.m[3][0];
				hand->muzzlepos.y = hand->posmtx.m[3][1];
				hand->muzzlepos.z = hand->posmtx.m[3][2];

				mtx4Copy(&hand->posmtx, &hand->muzzlemat);

				hand->muzzlez = -hand->cammtx.m[3][2];
			}
		}
	} else {
		hand->muzzlepos.x = hand->posmtx.m[3][0];
		hand->muzzlepos.y = hand->posmtx.m[3][1];
		hand->muzzlepos.z = hand->posmtx.m[3][2];

		mtx4Copy(&hand->posmtx, &hand->muzzlemat);

		hand->muzzlez = -hand->cammtx.m[3][2];
	}

	switch (weaponnum) {
	case WEAPON_ROCKETLAUNCHER:
		bgunUpdateRocketLauncher(hand, handnum, (struct weaponfunc_shootprojectile *)funcdef);
		break;
	case WEAPON_DY357MAGNUM:
	case WEAPON_DY357LX:
		if (hand->unk0cc8_04 > 0) {
			bgunUpdateMagnum(hand, handnum, modeldef, (Mtxf *)mtxallocation);
		}
		break;
	}

	if (hand->firing && g_Vars.lvupdate240 != 0) {
		bgunCreateFx(hand, handnum, funcdef, weaponnum, modeldef, mtxallocation);
	}

	if (PLAYERCOUNT() == 1 && g_Vars.lvupdate240 != 0) {
		bgunUpdateSmoke(hand, handnum, weaponnum, funcdef);
	}

	if (hand->ejectstate > EJECTSTATE_INACTIVE) {
		bgunTickEject(hand, modeldef, isdetonator);
	}

	if (PLAYERCOUNT() == 1 && bgunShouldRenderLasersight(hand)
			&& weaponnum >= WEAPON_FALCON2 && weaponnum <= WEAPON_FALCON2_SCOPE) {
		bgunUpdateLasersight(hand, modeldef, handnum, mtxallocation);
	} else {
		lasersightFree(handnum);
	}

	hand->animframeinc = 0;

#if VERSION >= VERSION_PAL_BETA
	hand->animframeincfreal = 0;
#endif
}

void bgunTickMaulerCharge(void)
{
	struct player *player = g_Vars.currentplayer;
	s32 i;

	for (i = 0; i < 2; i++) {
		struct hand *hand = &player->hands[i];
		u32 charging = false;

		if (hand->inuse) {
			if (bgunIsReloading(hand)) {
				// Reloading - reset charge amount
				hand->matmot1 = 0;
			} else if (hand->gset.weaponfunc == FUNC_SECONDARY) {
				// Charging or fully charged
				s32 oldvalue = hand->matmot1;
				s32 newvalue;

				if (hand->loadedammo[0] >= 2 && hand->matmot1 < 5) {
					charging = true;
					hand->matmot1 += g_Vars.lvupdate60freal * 0.05f;
				}

				if (hand->matmot1 > 5) {
					hand->matmot1 = 5;
				}

				newvalue = hand->matmot1;

				if (oldvalue != newvalue && hand->loadedammo[0] >= 2) {
					hand->loadedammo[0]--;
				}
			} else {
				// Using primary function - make the charge wear off slowly
				hand->matmot1 -= g_Vars.lvupdate60freal * 0.005f;

				if (hand->matmot1 < 0) {
					hand->matmot1 = 0;
				}
			}

			/**
			 * Probable @bug: In other places where audio is started and then
			 * its speed is adjusted, the game raises the priority of the main
			 * thread (this thread) to above the audio thread's priority so that
			 * the audio thread cannot execute and start playing the audio
			 * between the calls to sndStart and audioPostEvent. But this pattern
			 * is not done here.
			 *
			 * There is a known issue where the Mauler charge sound is played
			 * correctly then the game proceeds to play other unrelated sound
			 * effects before eventually crashing. It's suspected that this lack
			 * of thread priority adjusting is the root cause. Perhaps a race
			 * condition exists where the audio thread does something with the
			 * sound while this thread is in the middle of reconfiguring it.
			 * This is not yet confirmed.
			 */
			if (hand->audiohandle == NULL
					&& hand->matmot1 > 0.1f
					&& charging
					&& g_Vars.lvupdate240 != 0) {
				sndStart(var80095200, SFX_MAULER_CHARGE, &hand->audiohandle, -1, -1, -1, -1, -1);
			}

			if (hand->audiohandle) {
				f32 speed = 0.5f + hand->matmot1 / 3.0f + sinf(g_20SecIntervalFrac * M_PI * 32.0f) * 0.03f;

				if (hand->matmot1 < 0.1f || !charging) {
					audioStop(hand->audiohandle);
				} else {
					audioPostEvent(hand->audiohandle, AL_SNDP_PITCH_EVT, *(s32 *)&speed);
				}
			}
		}
	}
}

void bgunTickGameplay2(void)
{
	struct player *player = g_Vars.currentplayer;
	struct hand *hand;
	u32 stack[3];
	s32 i;

#if VERSION >= VERSION_NTSC_1_0
	if (g_Vars.currentplayernum == 0) {
		projectilesDebug();
	}
#endif

	if (player->gunctrl.loadall) {
		// empty
	} else {
		bgunTickLoad();
	}

	// Return control to Jo if eyespy has been deselected
	if ((g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_EYESPY) == 0
			&& player->eyespy) {
		player->eyespy->active = false;
	}

	if ((g_Vars.currentplayer->devicesactive & ~g_Vars.currentplayer->devicesinhibit & DEVICE_XRAYSCANNER)
			&& (bgunGetWeaponNum(HAND_RIGHT) != WEAPON_FARSIGHT || player->gunsightoff)) {
		// Using normal xray scanner (not Farsight zoom)
		if (player->visionmode != VISIONMODE_XRAY) {
			player->erasertime = 0;
		} else {
			player->erasertime += g_Vars.lvupdate240;
		}

		player->visionmode = VISIONMODE_XRAY;
		player->ecol_1 = 24;
		player->ecol_2 = 8;
		player->ecol_3 = 24;
		player->epcol_0 = 2;
		player->epcol_1 = 0;
		player->epcol_2 = 1;
	} else {
		if (player->gunsightoff == 0) {
			if (player->hands[HAND_RIGHT].gset.weaponnum == WEAPON_FARSIGHT) {
				// Aiming with the Farsight
				if (player->visionmode != VISIONMODE_XRAY) {
					player->erasertime = 0;
				} else {
					player->erasertime += g_Vars.lvupdate240;
				}

				player->visionmode = VISIONMODE_XRAY;
				player->ecol_1 = 16;
				player->ecol_2 = 24;
				player->ecol_3 = 8;
				player->epcol_0 = 0;
				player->epcol_1 = 1;
				player->epcol_2 = 2;
			} else {
				/* c3849 Wave 5f Unit 9: graph camera_effect clause, AFTER the
				 * OG Farsight clause (OG first and verbatim). The mode is an
				 * s32 latched at parse (no per-tick strcmp); the gate is the
				 * toggle-gated ForGameplay accessor so the clause is dormant
				 * when the runtime is off. xray while aiming takes the
				 * VISIONMODE_XRAY path with the Farsight constants. */
				const weapon_graph_held_function_t *camgraph =
					weaponGraphRuntimeGetHeldFunctionForGameplay(
						player->hands[HAND_RIGHT].gset.weaponnum,
						player->hands[HAND_RIGHT].gset.weaponfunc);

				if (camgraph && camgraph->camera_effect_mode == WEAPON_GRAPH_CAMERA_EFFECT_XRAY) {
					if (player->visionmode != VISIONMODE_XRAY) {
						player->erasertime = 0;
					} else {
						player->erasertime += g_Vars.lvupdate240;
					}

					player->visionmode = VISIONMODE_XRAY;
					player->ecol_1 = 16;
					player->ecol_2 = 24;
					player->ecol_3 = 8;
					player->epcol_0 = 0;
					player->epcol_1 = 1;
					player->epcol_2 = 2;
				} else {
					// Aiming with non-Farsight
					if (player->visionmode != VISIONMODE_SLAYERROCKET) {
						player->visionmode = VISIONMODE_NORMAL;
					}
				}
			}
		} else {
			// Not aiming
			if (player->visionmode != VISIONMODE_SLAYERROCKET) {
				player->visionmode = VISIONMODE_NORMAL;
			}
		}
	}

	if (player->gunctrl.weaponnum == WEAPON_MAULER) {
		bgunTickMaulerCharge();
	}

	if (g_Vars.lvupdate240 == 0) {
		for (i = 0; i < 2; i++) {
			hand = &player->hands[i];

			if (hand->audiohandle) {
				audioStop(hand->audiohandle);
			}
		}
	}

	if (g_Vars.currentplayer->devicesactive &
			~g_Vars.currentplayer->devicesinhibit & DEVICE_CLOAKRCP120) {
		if (player->gunctrl.weaponnum == WEAPON_RCP120) {
			struct chrdata *chr = player->prop->chr;

			// Handle RCP120 cloak ammo usage
			if ((chr->hidden & CHRHFLAG_CLOAKED) && chr->cloakfadefinished == true) {
				hand = &player->hands[HAND_RIGHT];
				hand->matmot1 += LVUPDATE60FREAL() * 0.4f;

				if (hand->matmot1 > 1.0f) {
					s32 usedqty = hand->matmot1;

					if (usedqty > hand->loadedammo[0]) {
						usedqty = hand->loadedammo[0];
					}

					hand->matmot1 -= usedqty;
					hand->loadedammo[0] -= usedqty;

					// If out of ammo, turn off cloak
					if (hand->loadedammo[0] == 0 && hand->state != HANDSTATE_RELOAD) {
						s32 stilltogo = hand->matmot1;

						if (stilltogo > player->ammoheldarr[player->gunctrl.ammotypes[0]]) {
							g_Vars.currentplayer->devicesactive &= ~DEVICE_CLOAKRCP120;
						}
					}
				}
			}
		} else {
			// No longer using RCP120, so turn off cloak
			player->devicesactive &= ~DEVICE_CLOAKRCP120;
		}
	} else if (player->gunctrl.weaponnum == WEAPON_RCP120) {
		hand = &player->hands[HAND_RIGHT];

		// I think this is handling situations where the player has turned off
		// RCP120 cloak but there's still a bit of ammo to be subtracted on
		// this tick.
		if (hand->matmot1 > 1.0f) {
			s32 usedqty = hand->matmot1;

			if (usedqty > hand->loadedammo[0]) {
				usedqty = hand->loadedammo[0];
			}

			hand->matmot1 -= usedqty;
			hand->loadedammo[0] -= usedqty;

			if (hand->matmot1 > 1.0f) {
				s32 usedqty = hand->matmot1;

				if (usedqty > player->ammoheldarr[player->gunctrl.ammotypes[0]]) {
					usedqty = player->ammoheldarr[player->gunctrl.ammotypes[0]];
				}

				player->ammoheldarr[player->gunctrl.ammotypes[0]] -= usedqty;
				hand->matmot1 = 0;
			}
		}
	}

	bgunTickUnequippedReload();
	bgun0f0a5550(HAND_RIGHT);

	if (player->hands[HAND_LEFT].inuse) {
		bgun0f0a5550(HAND_LEFT);
	} else {
		player->hands[HAND_LEFT].ejectstate = EJECTSTATE_INACTIVE;
	}

	bgunIsUsingSecondaryFunction();
}

s8 bgunFreeFireslotWrapper(s32 slotnum)
{
#if VERSION < VERSION_NTSC_1_0
	if (slotnum >= 0) {
		if (g_Fireslots[slotnum].unk04nb && sndGetState(g_Fireslots[slotnum].unk04nb) != AL_STOPPED) {
			audioStop(g_Fireslots[slotnum].unk04nb);
		}

		if (g_Fireslots[slotnum].unk08nb && sndGetState(g_Fireslots[slotnum].unk08nb) != AL_STOPPED) {
			audioStop(g_Fireslots[slotnum].unk08nb);
		}
	}
#endif

	return bgunFreeFireslot(slotnum);
}

s8 bgunFreeFireslot(s32 fireslot_id)
{
#if VERSION >= VERSION_NTSC_1_0
	if (fireslot_id >= 0 && fireslot_id < ARRAYCOUNT(g_Fireslots)) {
		g_Fireslots[fireslot_id].endlvframe = -1;
	}
#else
	if (fireslot_id >= 0) {
		g_Fireslots[fireslot_id].endlvframe = -1;
	}
#endif

	return -1;
}

s32 bgunAllocateFireslot(void)
{
	s32 index = -1;
	s32 i;

	for (i = 0; i < ARRAYCOUNT(g_Fireslots); i++) {
		if (g_Fireslots[i].endlvframe < 0) {
			g_Fireslots[i].endlvframe = 0;

#if VERSION < VERSION_NTSC_1_0
			g_Fireslots[i].unk04nb = 0;
			g_Fireslots[i].unk08nb = 0;
#endif

			g_Fireslots[i].beam.age = -1;
			index = i;
			break;
		}
	}

	return index;
}

u32 var8007029c = 0x00000000;
u32 var800702a0 = 0x00000001;
u32 var800702a4 = 0x00000003;
u32 var800702a8 = 0x00000000;
u32 var800702ac = 0x00000000;
u32 var800702b0 = 0x00000000;
u32 var800702b4 = 0x00000000;
u32 var800702b8 = 0x00000000;
u32 var800702bc = 0x00000000;
u32 var800702c0 = 0x00000000;
u32 var800702c4 = 0x00000000;
u32 var800702c8 = 0x00000000;
u32 var800702cc = 0x00000000;
u32 var800702d0 = 0x00000000;
u32 var800702d4 = 0x00000000;
u32 var800702d8 = 0x00000000;
u32 var800702dc = 0x00000001;

#if MATCHING
#if PAL
GLOBAL_ASM(
glabel bgunRender
.late_rodata
glabel var7f1aca8c
.word 0x3faaaaab
glabel var7f1aca90
.word 0x3f3ebebf
.text
/*  f0a7138:	27bdfeb0 */ 	addiu	$sp,$sp,-336
/*  f0a713c:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f0a7140:	afb50030 */ 	sw	$s5,0x30($sp)
/*  f0a7144:	afb4002c */ 	sw	$s4,0x2c($sp)
/*  f0a7148:	afb30028 */ 	sw	$s3,0x28($sp)
/*  f0a714c:	afb20024 */ 	sw	$s2,0x24($sp)
/*  f0a7150:	afb10020 */ 	sw	$s1,0x20($sp)
/*  f0a7154:	afb0001c */ 	sw	$s0,0x1c($sp)
/*  f0a7158:	afa40150 */ 	sw	$a0,0x150($sp)
/*  f0a715c:	8c8f0000 */ 	lw	$t7,0x0($a0)
/*  f0a7160:	3c198007 */ 	lui	$t9,%hi(var8007029c)
/*  f0a7164:	3c11800a */ 	lui	$s1,%hi(g_Vars)
/*  f0a7168:	2739029c */ 	addiu	$t9,$t9,%lo(var8007029c)
/*  f0a716c:	26319fc0 */ 	addiu	$s1,$s1,%lo(g_Vars)
/*  f0a7170:	272a003c */ 	addiu	$t2,$t9,0x3c
/*  f0a7174:	27b8010c */ 	addiu	$t8,$sp,0x10c
/*  f0a7178:	afaf014c */ 	sw	$t7,0x14c($sp)
.L0f0a717c:
/*  f0a717c:	8f210000 */ 	lw	$at,0x0($t9)
/*  f0a7180:	2739000c */ 	addiu	$t9,$t9,0xc
/*  f0a7184:	2718000c */ 	addiu	$t8,$t8,0xc
/*  f0a7188:	af01fff4 */ 	sw	$at,-0xc($t8)
/*  f0a718c:	8f21fff8 */ 	lw	$at,-0x8($t9)
/*  f0a7190:	af01fff8 */ 	sw	$at,-0x8($t8)
/*  f0a7194:	8f21fffc */ 	lw	$at,-0x4($t9)
/*  f0a7198:	172afff8 */ 	bne	$t9,$t2,.L0f0a717c
/*  f0a719c:	af01fffc */ 	sw	$at,-0x4($t8)
/*  f0a71a0:	8f210000 */ 	lw	$at,0x0($t9)
/*  f0a71a4:	af010000 */ 	sw	$at,0x0($t8)
/*  f0a71a8:	8e330284 */ 	lw	$s3,0x284($s1)
/*  f0a71ac:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a71b0:	966b0010 */ 	lhu	$t3,0x10($s3)
/*  f0a71b4:	1561000d */ 	bne	$t3,$at,.L0f0a71ec
/*  f0a71b8:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a71bc:	24040f48 */ 	addiu	$a0,$zero,0xf48
/*  f0a71c0:	8e2c0284 */ 	lw	$t4,0x284($s1)
.L0f0a71c4:
/*  f0a71c4:	01821821 */ 	addu	$v1,$t4,$v0
/*  f0a71c8:	8c6d0854 */ 	lw	$t5,0x854($v1)
/*  f0a71cc:	244207a4 */ 	addiu	$v0,$v0,0x7a4
/*  f0a71d0:	11a00002 */ 	beqz	$t5,.L0f0a71dc
/*  f0a71d4:	00000000 */ 	nop
/*  f0a71d8:	ac600850 */ 	sw	$zero,0x850($v1)
.L0f0a71dc:
/*  f0a71dc:	5444fff9 */ 	bnel	$v0,$a0,.L0f0a71c4
/*  f0a71e0:	8e2c0284 */ 	lw	$t4,0x284($s1)
/*  f0a71e4:	100002d5 */ 	b	.L0f0a7d3c
/*  f0a71e8:	8fbf0034 */ 	lw	$ra,0x34($sp)
.L0f0a71ec:
/*  f0a71ec:	0fc5d9ad */ 	jal	zbufDrawArtifactsOffscreen
/*  f0a71f0:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a71f4:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a71f8:	0c002ca0 */ 	jal	viPrepareZbuf
/*  f0a71fc:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a7200:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a7204:	0c002c74 */ 	jal	vi0000b1d0
/*  f0a7208:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a720c:	244e0008 */ 	addiu	$t6,$v0,0x8
/*  f0a7210:	afae014c */ 	sw	$t6,0x14c($sp)
/*  f0a7214:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7218:	0040a825 */ 	or	$s5,$v0,$zero
/*  f0a721c:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a7220:	00107c03 */ 	sra	$t7,$s0,0x10
/*  f0a7224:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7228:	01e08025 */ 	or	$s0,$t7,$zero
/*  f0a722c:	44822000 */ 	mtc1	$v0,$f4
/*  f0a7230:	44908000 */ 	mtc1	$s0,$f16
/*  f0a7234:	3c014080 */ 	lui	$at,0x4080
/*  f0a7238:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a723c:	44810000 */ 	mtc1	$at,$f0
/*  f0a7240:	3c01ed00 */ 	lui	$at,0xed00
/*  f0a7244:	468084a0 */ 	cvt.s.w	$f18,$f16
/*  f0a7248:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a724c:	00000000 */ 	nop
/*  f0a7250:	46009102 */ 	mul.s	$f4,$f18,$f0
/*  f0a7254:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a7258:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a725c:	44085000 */ 	mfc1	$t0,$f10
/*  f0a7260:	440b3000 */ 	mfc1	$t3,$f6
/*  f0a7264:	310a0fff */ 	andi	$t2,$t0,0xfff
/*  f0a7268:	0141c825 */ 	or	$t9,$t2,$at
/*  f0a726c:	316c0fff */ 	andi	$t4,$t3,0xfff
/*  f0a7270:	000c6b00 */ 	sll	$t5,$t4,0xc
/*  f0a7274:	032d7025 */ 	or	$t6,$t9,$t5
/*  f0a7278:	0c002f22 */ 	jal	viGetViewWidth
/*  f0a727c:	aeae0000 */ 	sw	$t6,0x0($s5)
/*  f0a7280:	00029400 */ 	sll	$s2,$v0,0x10
/*  f0a7284:	00127c03 */ 	sra	$t7,$s2,0x10
/*  f0a7288:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a728c:	01e09025 */ 	or	$s2,$t7,$zero
/*  f0a7290:	0002a400 */ 	sll	$s4,$v0,0x10
/*  f0a7294:	00144c03 */ 	sra	$t1,$s4,0x10
/*  f0a7298:	0c002f44 */ 	jal	viGetViewTop
/*  f0a729c:	0120a025 */ 	or	$s4,$t1,$zero
/*  f0a72a0:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a72a4:	00104403 */ 	sra	$t0,$s0,0x10
/*  f0a72a8:	0c002f26 */ 	jal	viGetViewHeight
/*  f0a72ac:	01008025 */ 	or	$s0,$t0,$zero
/*  f0a72b0:	00505021 */ 	addu	$t2,$v0,$s0
/*  f0a72b4:	448a4000 */ 	mtc1	$t2,$f8
/*  f0a72b8:	0292c821 */ 	addu	$t9,$s4,$s2
/*  f0a72bc:	44992000 */ 	mtc1	$t9,$f4
/*  f0a72c0:	468042a0 */ 	cvt.s.w	$f10,$f8
/*  f0a72c4:	3c014080 */ 	lui	$at,0x4080
/*  f0a72c8:	44810000 */ 	mtc1	$at,$f0
/*  f0a72cc:	3c053fc0 */ 	lui	$a1,0x3fc0
/*  f0a72d0:	3c06447a */ 	lui	$a2,0x447a
/*  f0a72d4:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a72d8:	46005402 */ 	mul.s	$f16,$f10,$f0
/*  f0a72dc:	00000000 */ 	nop
/*  f0a72e0:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a72e4:	4600848d */ 	trunc.w.s	$f18,$f16
/*  f0a72e8:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a72ec:	440b9000 */ 	mfc1	$t3,$f18
/*  f0a72f0:	440e5000 */ 	mfc1	$t6,$f10
/*  f0a72f4:	316c0fff */ 	andi	$t4,$t3,0xfff
/*  f0a72f8:	31cf0fff */ 	andi	$t7,$t6,0xfff
/*  f0a72fc:	000f4b00 */ 	sll	$t1,$t7,0xc
/*  f0a7300:	01894025 */ 	or	$t0,$t4,$t1
/*  f0a7304:	aea80004 */ 	sw	$t0,0x4($s5)
/*  f0a7308:	0c002b29 */ 	jal	vi0000aca4
/*  f0a730c:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a7310:	8e2a0284 */ 	lw	$t2,0x284($s1)
/*  f0a7314:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a7318:	91581bfc */ 	lbu	$t8,0x1bfc($t2)
/*  f0a731c:	53000016 */ 	beqzl	$t8,.L0f0a7378
/*  f0a7320:	8e2b006c */ 	lw	$t3,0x6c($s1)
/*  f0a7324:	0fc54bc7 */ 	jal	optionsGetScreenRatio
/*  f0a7328:	00000000 */ 	nop
/*  f0a732c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a7330:	14410008 */ 	bne	$v0,$at,.L0f0a7354
/*  f0a7334:	00000000 */ 	nop
/*  f0a7338:	0fc2f4d6 */ 	jal	player0f0bd358
/*  f0a733c:	00000000 */ 	nop
/*  f0a7340:	3c017f1b */ 	lui	$at,%hi(var7f1aca8c)
/*  f0a7344:	c430ca8c */ 	lwc1	$f16,%lo(var7f1aca8c)($at)
/*  f0a7348:	46100082 */ 	mul.s	$f2,$f0,$f16
/*  f0a734c:	10000005 */ 	b	.L0f0a7364
/*  f0a7350:	44061000 */ 	mfc1	$a2,$f2
.L0f0a7354:
/*  f0a7354:	0fc2f4d6 */ 	jal	player0f0bd358
/*  f0a7358:	00000000 */ 	nop
/*  f0a735c:	46000086 */ 	mov.s	$f2,$f0
/*  f0a7360:	44061000 */ 	mfc1	$a2,$f2
.L0f0a7364:
/*  f0a7364:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a7368:	0c002c3a */ 	jal	vi0000b0e8
/*  f0a736c:	3c054270 */ 	lui	$a1,0x4270
/*  f0a7370:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a7374:	8e2b006c */ 	lw	$t3,0x6c($s1)
.L0f0a7378:
/*  f0a7378:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a737c:	51600004 */ 	beqzl	$t3,.L0f0a7390
/*  f0a7380:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a7384:	10000002 */ 	b	.L0f0a7390
/*  f0a7388:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f0a738c:	00002825 */ 	or	$a1,$zero,$zero
.L0f0a7390:
/*  f0a7390:	8e390068 */ 	lw	$t9,0x68($s1)
/*  f0a7394:	53200004 */ 	beqzl	$t9,.L0f0a73a8
/*  f0a7398:	00002025 */ 	or	$a0,$zero,$zero
/*  f0a739c:	10000002 */ 	b	.L0f0a73a8
/*  f0a73a0:	24040001 */ 	addiu	$a0,$zero,0x1
/*  f0a73a4:	00002025 */ 	or	$a0,$zero,$zero
.L0f0a73a8:
/*  f0a73a8:	8e2d0064 */ 	lw	$t5,0x64($s1)
/*  f0a73ac:	51a00004 */ 	beqzl	$t5,.L0f0a73c0
/*  f0a73b0:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a73b4:	10000002 */ 	b	.L0f0a73c0
/*  f0a73b8:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f0a73bc:	00001025 */ 	or	$v0,$zero,$zero
.L0f0a73c0:
/*  f0a73c0:	8e2e0070 */ 	lw	$t6,0x70($s1)
/*  f0a73c4:	51c00004 */ 	beqzl	$t6,.L0f0a73d8
/*  f0a73c8:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a73cc:	10000002 */ 	b	.L0f0a73d8
/*  f0a73d0:	24030001 */ 	addiu	$v1,$zero,0x1
/*  f0a73d4:	00001825 */ 	or	$v1,$zero,$zero
.L0f0a73d8:
/*  f0a73d8:	00627821 */ 	addu	$t7,$v1,$v0
/*  f0a73dc:	01e46021 */ 	addu	$t4,$t7,$a0
/*  f0a73e0:	01854821 */ 	addu	$t1,$t4,$a1
/*  f0a73e4:	15210008 */ 	bne	$t1,$at,.L0f0a7408
/*  f0a73e8:	3c088009 */ 	lui	$t0,%hi(g_Is4Mb)
/*  f0a73ec:	91080af0 */ 	lbu	$t0,%lo(g_Is4Mb)($t0)
/*  f0a73f0:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a73f4:	51010005 */ 	beql	$t0,$at,.L0f0a740c
/*  f0a73f8:	0000a025 */ 	or	$s4,$zero,$zero
/*  f0a73fc:	0fc2be93 */ 	jal	lasersightRenderBeam
/*  f0a7400:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a7404:	afa2014c */ 	sw	$v0,0x14c($sp)
.L0f0a7408:
/*  f0a7408:	0000a025 */ 	or	$s4,$zero,$zero
.L0f0a740c:
/*  f0a740c:	26700638 */ 	addiu	$s0,$s3,0x638
/*  f0a7410:	8fb500e4 */ 	lw	$s5,0xe4($sp)
/*  f0a7414:	24120019 */ 	addiu	$s2,$zero,0x1e
.L0f0a7418:
/*  f0a7418:	0fc2867c */ 	jal	bgunGetWeaponNum2
/*  f0a741c:	02802025 */ 	or	$a0,$s4,$zero
/*  f0a7420:	afa200ec */ 	sw	$v0,0xec($sp)
/*  f0a7424:	820a0007 */ 	lb	$t2,0x7($s0)
/*  f0a7428:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a742c:	260501dc */ 	addiu	$a1,$s0,0x1dc
/*  f0a7430:	114001f2 */ 	beqz	$t2,.L0f0a7bfc
/*  f0a7434:	00003025 */ 	or	$a2,$zero,$zero
/*  f0a7438:	26180384 */ 	addiu	$t8,$s0,0x384
/*  f0a743c:	afb8003c */ 	sw	$t8,0x3c($sp)
/*  f0a7440:	0fc2b2e4 */ 	jal	beamRender
/*  f0a7444:	00003825 */ 	or	$a3,$zero,$zero
/*  f0a7448:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a744c:	92040000 */ 	lbu	$a0,0x0($s0)
/*  f0a7450:	0fc2c5f0 */ 	jal	weaponHasFlag
/*  f0a7454:	34058000 */ 	dli	$a1,0x8000
/*  f0a7458:	10400030 */ 	beqz	$v0,.L0f0a751c
/*  f0a745c:	8fab014c */ 	lw	$t3,0x14c($sp)
/*  f0a7460:	25790008 */ 	addiu	$t9,$t3,0x8
/*  f0a7464:	afb9014c */ 	sw	$t9,0x14c($sp)
/*  f0a7468:	3c0dbc00 */ 	lui	$t5,0xbc00
/*  f0a746c:	3c0e8000 */ 	lui	$t6,0x8000
/*  f0a7470:	35ce0040 */ 	ori	$t6,$t6,0x40
/*  f0a7474:	35ad0002 */ 	ori	$t5,$t5,0x2
/*  f0a7478:	ad6d0000 */ 	sw	$t5,0x0($t3)
/*  f0a747c:	ad6e0004 */ 	sw	$t6,0x4($t3)
/*  f0a7480:	8faf014c */ 	lw	$t7,0x14c($sp)
/*  f0a7484:	3c090386 */ 	lui	$t1,0x386
/*  f0a7488:	3c088007 */ 	lui	$t0,%hi(var80070090+0x08)
/*  f0a748c:	25ec0008 */ 	addiu	$t4,$t7,0x8
/*  f0a7490:	afac014c */ 	sw	$t4,0x14c($sp)
/*  f0a7494:	25080098 */ 	addiu	$t0,$t0,%lo(var80070090+0x08)
/*  f0a7498:	35290010 */ 	ori	$t1,$t1,0x10
/*  f0a749c:	ade90000 */ 	sw	$t1,0x0($t7)
/*  f0a74a0:	ade80004 */ 	sw	$t0,0x4($t7)
/*  f0a74a4:	8faa014c */ 	lw	$t2,0x14c($sp)
/*  f0a74a8:	3c0b0388 */ 	lui	$t3,0x388
/*  f0a74ac:	3c198007 */ 	lui	$t9,%hi(var80070090)
/*  f0a74b0:	25580008 */ 	addiu	$t8,$t2,0x8
/*  f0a74b4:	afb8014c */ 	sw	$t8,0x14c($sp)
/*  f0a74b8:	27390090 */ 	addiu	$t9,$t9,%lo(var80070090)
/*  f0a74bc:	356b0010 */ 	ori	$t3,$t3,0x10
/*  f0a74c0:	ad4b0000 */ 	sw	$t3,0x0($t2)
/*  f0a74c4:	ad590004 */ 	sw	$t9,0x4($t2)
/*  f0a74c8:	8fad014c */ 	lw	$t5,0x14c($sp)
/*  f0a74cc:	3c0f0384 */ 	lui	$t7,0x384
/*  f0a74d0:	35ef0010 */ 	ori	$t7,$t7,0x10
/*  f0a74d4:	25ae0008 */ 	addiu	$t6,$t5,0x8
/*  f0a74d8:	afae014c */ 	sw	$t6,0x14c($sp)
/*  f0a74dc:	adaf0000 */ 	sw	$t7,0x0($t5)
/*  f0a74e0:	0fc2d5ea */ 	jal	camGetLookAt
/*  f0a74e4:	afad00d4 */ 	sw	$t5,0xd4($sp)
/*  f0a74e8:	8fa500d4 */ 	lw	$a1,0xd4($sp)
/*  f0a74ec:	3c080382 */ 	lui	$t0,0x382
/*  f0a74f0:	35080010 */ 	ori	$t0,$t0,0x10
/*  f0a74f4:	aca20004 */ 	sw	$v0,0x4($a1)
/*  f0a74f8:	8fac014c */ 	lw	$t4,0x14c($sp)
/*  f0a74fc:	25890008 */ 	addiu	$t1,$t4,0x8
/*  f0a7500:	afa9014c */ 	sw	$t1,0x14c($sp)
/*  f0a7504:	ad880000 */ 	sw	$t0,0x0($t4)
/*  f0a7508:	0fc2d5ea */ 	jal	camGetLookAt
/*  f0a750c:	afac00d0 */ 	sw	$t4,0xd0($sp)
/*  f0a7510:	8fa300d0 */ 	lw	$v1,0xd0($sp)
/*  f0a7514:	244a0010 */ 	addiu	$t2,$v0,0x10
/*  f0a7518:	ac6a0004 */ 	sw	$t2,0x4($v1)
.L0f0a751c:
/*  f0a751c:	8fb8014c */ 	lw	$t8,0x14c($sp)
/*  f0a7520:	3c19bc00 */ 	lui	$t9,0xbc00
/*  f0a7524:	3739000e */ 	ori	$t9,$t9,0xe
/*  f0a7528:	270b0008 */ 	addiu	$t3,$t8,0x8
/*  f0a752c:	afab014c */ 	sw	$t3,0x14c($sp)
/*  f0a7530:	3c014396 */ 	lui	$at,0x4396
/*  f0a7534:	44817000 */ 	mtc1	$at,$f14
/*  f0a7538:	44806000 */ 	mtc1	$zero,$f12
/*  f0a753c:	af190000 */ 	sw	$t9,0x0($t8)
/*  f0a7540:	0c005b73 */ 	jal	mtx00016dcc
/*  f0a7544:	afb800cc */ 	sw	$t8,0xcc($sp)
/*  f0a7548:	8fa300cc */ 	lw	$v1,0xcc($sp)
/*  f0a754c:	24050010 */ 	addiu	$a1,$zero,0x10
/*  f0a7550:	ac620004 */ 	sw	$v0,0x4($v1)
/*  f0a7554:	0c006a47 */ 	jal	modelGetPart
/*  f0a7558:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a755c:	10400014 */ 	beqz	$v0,.L0f0a75b0
/*  f0a7560:	afa200e8 */ 	sw	$v0,0xe8($sp)
/*  f0a7564:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a7568:	0c006a47 */ 	jal	modelGetPart
/*  f0a756c:	24050011 */ 	addiu	$a1,$zero,0x11
/*  f0a7570:	8fa4003c */ 	lw	$a0,0x3c($sp)
/*  f0a7574:	0c006a87 */ 	jal	modelGetNodeRwData
/*  f0a7578:	00402825 */ 	or	$a1,$v0,$zero
/*  f0a757c:	10400003 */ 	beqz	$v0,.L0f0a758c
/*  f0a7580:	3c06800a */ 	lui	$a2,%hi(var8009cf88)
/*  f0a7584:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f0a7588:	ac4d0000 */ 	sw	$t5,0x0($v0)
.L0f0a758c:
/*  f0a758c:	240e0001 */ 	addiu	$t6,$zero,0x1
/*  f0a7590:	afae0014 */ 	sw	$t6,0x14($sp)
/*  f0a7594:	8fa4003c */ 	lw	$a0,0x3c($sp)
/*  f0a7598:	8fa500e8 */ 	lw	$a1,0xe8($sp)
/*  f0a759c:	24c6cf88 */ 	addiu	$a2,$a2,%lo(var8009cf88)
/*  f0a75a0:	8fa7014c */ 	lw	$a3,0x14c($sp)
/*  f0a75a4:	0fc1fefe */ 	jal	tvscreenRender
/*  f0a75a8:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f0a75ac:	afa2014c */ 	sw	$v0,0x14c($sp)
.L0f0a75b0:
/*  f0a75b0:	8faf014c */ 	lw	$t7,0x14c($sp)
/*  f0a75b4:	8e250284 */ 	lw	$a1,0x284($s1)
/*  f0a75b8:	240c0004 */ 	addiu	$t4,$zero,0x4
/*  f0a75bc:	afac013c */ 	sw	$t4,0x13c($sp)
/*  f0a75c0:	afaf0118 */ 	sw	$t7,0x118($sp)
/*  f0a75c4:	8ca300d8 */ 	lw	$v1,0xd8($a1)
/*  f0a75c8:	3c078007 */ 	lui	$a3,%hi(g_InCutscene)
/*  f0a75cc:	14600013 */ 	bnez	$v1,.L0f0a761c
/*  f0a75d0:	00000000 */ 	nop
/*  f0a75d4:	8ce70764 */ 	lw	$a3,%lo(g_InCutscene)($a3)
/*  f0a75d8:	14e00010 */ 	bnez	$a3,.L0f0a761c
/*  f0a75dc:	00000000 */ 	nop
/*  f0a75e0:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a75e4:	50400007 */ 	beqzl	$v0,.L0f0a7604
/*  f0a75e8:	8caa1c54 */ 	lw	$t2,0x1c54($a1)
/*  f0a75ec:	1040000b */ 	beqz	$v0,.L0f0a761c
/*  f0a75f0:	00000000 */ 	nop
/*  f0a75f4:	80490037 */ 	lb	$t1,0x37($v0)
/*  f0a75f8:	15200008 */ 	bnez	$t1,.L0f0a761c
/*  f0a75fc:	00000000 */ 	nop
/*  f0a7600:	8caa1c54 */ 	lw	$t2,0x1c54($a1)
.L0f0a7604:
/*  f0a7604:	8ca800c4 */ 	lw	$t0,0xc4($a1)
/*  f0a7608:	0140c027 */ 	nor	$t8,$t2,$zero
/*  f0a760c:	01185824 */ 	and	$t3,$t0,$t8
/*  f0a7610:	31790001 */ 	andi	$t9,$t3,0x1
/*  f0a7614:	57200016 */ 	bnezl	$t9,.L0f0a7670
/*  f0a7618:	92681615 */ 	lbu	$t0,0x1615($s3)
.L0f0a761c:
/*  f0a761c:	14600078 */ 	bnez	$v1,.L0f0a7800
/*  f0a7620:	3c078007 */ 	lui	$a3,%hi(g_InCutscene)
/*  f0a7624:	8ce70764 */ 	lw	$a3,%lo(g_InCutscene)($a3)
/*  f0a7628:	54e00076 */ 	bnezl	$a3,.L0f0a7804
/*  f0a762c:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a7630:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a7634:	50400007 */ 	beqzl	$v0,.L0f0a7654
/*  f0a7638:	8caf1c54 */ 	lw	$t7,0x1c54($a1)
/*  f0a763c:	50400071 */ 	beqzl	$v0,.L0f0a7804
/*  f0a7640:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a7644:	804d0037 */ 	lb	$t5,0x37($v0)
/*  f0a7648:	55a0006e */ 	bnezl	$t5,.L0f0a7804
/*  f0a764c:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a7650:	8caf1c54 */ 	lw	$t7,0x1c54($a1)
.L0f0a7654:
/*  f0a7654:	8cae00c4 */ 	lw	$t6,0xc4($a1)
/*  f0a7658:	01e06027 */ 	nor	$t4,$t7,$zero
/*  f0a765c:	01cc4824 */ 	and	$t1,$t6,$t4
/*  f0a7660:	312a0008 */ 	andi	$t2,$t1,0x8
/*  f0a7664:	51400067 */ 	beqzl	$t2,.L0f0a7804
/*  f0a7668:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a766c:	92681615 */ 	lbu	$t0,0x1615($s3)
.L0f0a7670:
/*  f0a7670:	92781614 */ 	lbu	$t8,0x1614($s3)
/*  f0a7674:	26641614 */ 	addiu	$a0,$s3,0x1614
/*  f0a7678:	0118082a */ 	slt	$at,$t0,$t8
/*  f0a767c:	50200009 */ 	beqzl	$at,.L0f0a76a4
/*  f0a7680:	90820002 */ 	lbu	$v0,0x2($a0)
/*  f0a7684:	90830000 */ 	lbu	$v1,0x0($a0)
/*  f0a7688:	908b0002 */ 	lbu	$t3,0x2($a0)
/*  f0a768c:	0163082a */ 	slt	$at,$t3,$v1
/*  f0a7690:	50200004 */ 	beqzl	$at,.L0f0a76a4
/*  f0a7694:	90820002 */ 	lbu	$v0,0x2($a0)
/*  f0a7698:	1000000a */ 	b	.L0f0a76c4
/*  f0a769c:	00601025 */ 	or	$v0,$v1,$zero
/*  f0a76a0:	90820002 */ 	lbu	$v0,0x2($a0)
.L0f0a76a4:
/*  f0a76a4:	90860001 */ 	lbu	$a2,0x1($a0)
/*  f0a76a8:	00401825 */ 	or	$v1,$v0,$zero
/*  f0a76ac:	0046082a */ 	slt	$at,$v0,$a2
/*  f0a76b0:	10200003 */ 	beqz	$at,.L0f0a76c0
/*  f0a76b4:	00000000 */ 	nop
/*  f0a76b8:	10000001 */ 	b	.L0f0a76c0
/*  f0a76bc:	00c01825 */ 	or	$v1,$a2,$zero
.L0f0a76c0:
/*  f0a76c0:	00601025 */ 	or	$v0,$v1,$zero
.L0f0a76c4:
/*  f0a76c4:	90890003 */ 	lbu	$t1,0x3($a0)
/*  f0a76c8:	0002ce00 */ 	sll	$t9,$v0,0x18
/*  f0a76cc:	00026c00 */ 	sll	$t5,$v0,0x10
/*  f0a76d0:	032d7825 */ 	or	$t7,$t9,$t5
/*  f0a76d4:	00027200 */ 	sll	$t6,$v0,0x8
/*  f0a76d8:	01ee6025 */ 	or	$t4,$t7,$t6
/*  f0a76dc:	012c5021 */ 	addu	$t2,$t1,$t4
/*  f0a76e0:	afaa0140 */ 	sw	$t2,0x140($sp)
/*  f0a76e4:	8ca300d8 */ 	lw	$v1,0xd8($a1)
/*  f0a76e8:	14600021 */ 	bnez	$v1,.L0f0a7770
/*  f0a76ec:	00000000 */ 	nop
/*  f0a76f0:	14e0001f */ 	bnez	$a3,.L0f0a7770
/*  f0a76f4:	00000000 */ 	nop
/*  f0a76f8:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a76fc:	50400007 */ 	beqzl	$v0,.L0f0a771c
/*  f0a7700:	8cab1c54 */ 	lw	$t3,0x1c54($a1)
/*  f0a7704:	1040001a */ 	beqz	$v0,.L0f0a7770
/*  f0a7708:	00000000 */ 	nop
/*  f0a770c:	80480037 */ 	lb	$t0,0x37($v0)
/*  f0a7710:	15000017 */ 	bnez	$t0,.L0f0a7770
/*  f0a7714:	00000000 */ 	nop
/*  f0a7718:	8cab1c54 */ 	lw	$t3,0x1c54($a1)
.L0f0a771c:
/*  f0a771c:	8cb800c4 */ 	lw	$t8,0xc4($a1)
/*  f0a7720:	3c02800a */ 	lui	$v0,%hi(var8009caec+0x3)
/*  f0a7724:	0160c827 */ 	nor	$t9,$t3,$zero
/*  f0a7728:	03196824 */ 	and	$t5,$t8,$t9
/*  f0a772c:	31af0001 */ 	andi	$t7,$t5,0x1
/*  f0a7730:	11e0000f */ 	beqz	$t7,.L0f0a7770
/*  f0a7734:	00000000 */ 	nop
/*  f0a7738:	9042caef */ 	lbu	$v0,%lo(var8009caec+0x3)($v0)
/*  f0a773c:	3c06800a */ 	lui	$a2,%hi(var8009caf0)
/*  f0a7740:	90c6caf0 */ 	lbu	$a2,%lo(var8009caf0)($a2)
/*  f0a7744:	00027600 */ 	sll	$t6,$v0,0x18
/*  f0a7748:	00024c00 */ 	sll	$t1,$v0,0x10
/*  f0a774c:	01c96025 */ 	or	$t4,$t6,$t1
/*  f0a7750:	00025200 */ 	sll	$t2,$v0,0x8
/*  f0a7754:	018a4025 */ 	or	$t0,$t4,$t2
/*  f0a7758:	afa200b0 */ 	sw	$v0,0xb0($sp)
/*  f0a775c:	afa200b4 */ 	sw	$v0,0xb4($sp)
/*  f0a7760:	afa200b8 */ 	sw	$v0,0xb8($sp)
/*  f0a7764:	00c8a821 */ 	addu	$s5,$a2,$t0
/*  f0a7768:	1000001f */ 	b	.L0f0a77e8
/*  f0a776c:	afa600bc */ 	sw	$a2,0xbc($sp)
.L0f0a7770:
/*  f0a7770:	5460001e */ 	bnezl	$v1,.L0f0a77ec
/*  f0a7774:	8fb900ec */ 	lw	$t9,0xec($sp)
/*  f0a7778:	54e0001c */ 	bnezl	$a3,.L0f0a77ec
/*  f0a777c:	8fb900ec */ 	lw	$t9,0xec($sp)
/*  f0a7780:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a7784:	50400007 */ 	beqzl	$v0,.L0f0a77a4
/*  f0a7788:	8cb91c54 */ 	lw	$t9,0x1c54($a1)
/*  f0a778c:	50400017 */ 	beqzl	$v0,.L0f0a77ec
/*  f0a7790:	8fb900ec */ 	lw	$t9,0xec($sp)
/*  f0a7794:	804b0037 */ 	lb	$t3,0x37($v0)
/*  f0a7798:	55600014 */ 	bnezl	$t3,.L0f0a77ec
/*  f0a779c:	8fb900ec */ 	lw	$t9,0xec($sp)
/*  f0a77a0:	8cb91c54 */ 	lw	$t9,0x1c54($a1)
.L0f0a77a4:
/*  f0a77a4:	8cb800c4 */ 	lw	$t8,0xc4($a1)
/*  f0a77a8:	240200ff */ 	addiu	$v0,$zero,0xff
/*  f0a77ac:	03206827 */ 	nor	$t5,$t9,$zero
/*  f0a77b0:	030d7824 */ 	and	$t7,$t8,$t5
/*  f0a77b4:	31ee0008 */ 	andi	$t6,$t7,0x8
/*  f0a77b8:	11c0000b */ 	beqz	$t6,.L0f0a77e8
/*  f0a77bc:	24050080 */ 	addiu	$a1,$zero,0x80
/*  f0a77c0:	00024e00 */ 	sll	$t1,$v0,0x18
/*  f0a77c4:	00006400 */ 	sll	$t4,$zero,0x10
/*  f0a77c8:	012c5025 */ 	or	$t2,$t1,$t4
/*  f0a77cc:	00004200 */ 	sll	$t0,$zero,0x8
/*  f0a77d0:	01485825 */ 	or	$t3,$t2,$t0
/*  f0a77d4:	00aba821 */ 	addu	$s5,$a1,$t3
/*  f0a77d8:	afa200a0 */ 	sw	$v0,0xa0($sp)
/*  f0a77dc:	afa000a4 */ 	sw	$zero,0xa4($sp)
/*  f0a77e0:	afa000a8 */ 	sw	$zero,0xa8($sp)
/*  f0a77e4:	afa500ac */ 	sw	$a1,0xac($sp)
.L0f0a77e8:
/*  f0a77e8:	8fb900ec */ 	lw	$t9,0xec($sp)
.L0f0a77ec:
/*  f0a77ec:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a77f0:	5721003d */ 	bnel	$t9,$at,.L0f0a78e8
/*  f0a77f4:	8e6f00bc */ 	lw	$t7,0xbc($s3)
/*  f0a77f8:	1000003a */ 	b	.L0f0a78e4
/*  f0a77fc:	afb50140 */ 	sw	$s5,0x140($sp)
.L0f0a7800:
/*  f0a7800:	926d1614 */ 	lbu	$t5,0x1614($s3)
.L0f0a7804:
/*  f0a7804:	92781617 */ 	lbu	$t8,0x1617($s3)
/*  f0a7808:	92691615 */ 	lbu	$t1,0x1615($s3)
/*  f0a780c:	92681616 */ 	lbu	$t0,0x1616($s3)
/*  f0a7810:	000d7e00 */ 	sll	$t7,$t5,0x18
/*  f0a7814:	030f7025 */ 	or	$t6,$t8,$t7
/*  f0a7818:	00096400 */ 	sll	$t4,$t1,0x10
/*  f0a781c:	01cc5025 */ 	or	$t2,$t6,$t4
/*  f0a7820:	00085a00 */ 	sll	$t3,$t0,0x8
/*  f0a7824:	014bc825 */ 	or	$t9,$t2,$t3
/*  f0a7828:	afb90140 */ 	sw	$t9,0x140($sp)
/*  f0a782c:	920d0000 */ 	lbu	$t5,0x0($s0)
/*  f0a7830:	24010006 */ 	addiu	$at,$zero,0x6
/*  f0a7834:	0320a825 */ 	or	$s5,$t9,$zero
/*  f0a7838:	15a1002a */ 	bne	$t5,$at,.L0f0a78e4
/*  f0a783c:	3c04ff00 */ 	lui	$a0,0xff00
/*  f0a7840:	3c014248 */ 	lui	$at,0x4248
/*  f0a7844:	44812000 */ 	mtc1	$at,$f4
/*  f0a7848:	c612023c */ 	lwc1	$f18,0x23c($s0)
/*  f0a784c:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f0a7850:	3c014f00 */ 	lui	$at,0x4f00
/*  f0a7854:	46049182 */ 	mul.s	$f6,$f18,$f4
/*  f0a7858:	3484007f */ 	ori	$a0,$a0,0x7f
/*  f0a785c:	4458f800 */ 	cfc1	$t8,$31
/*  f0a7860:	44c6f800 */ 	ctc1	$a2,$31
/*  f0a7864:	00000000 */ 	nop
/*  f0a7868:	46003224 */ 	cvt.w.s	$f8,$f6
/*  f0a786c:	4446f800 */ 	cfc1	$a2,$31
/*  f0a7870:	00000000 */ 	nop
/*  f0a7874:	30c60078 */ 	andi	$a2,$a2,0x78
/*  f0a7878:	50c00013 */ 	beqzl	$a2,.L0f0a78c8
/*  f0a787c:	44064000 */ 	mfc1	$a2,$f8
/*  f0a7880:	44814000 */ 	mtc1	$at,$f8
/*  f0a7884:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f0a7888:	46083201 */ 	sub.s	$f8,$f6,$f8
/*  f0a788c:	44c6f800 */ 	ctc1	$a2,$31
/*  f0a7890:	00000000 */ 	nop
/*  f0a7894:	46004224 */ 	cvt.w.s	$f8,$f8
/*  f0a7898:	4446f800 */ 	cfc1	$a2,$31
/*  f0a789c:	00000000 */ 	nop
/*  f0a78a0:	30c60078 */ 	andi	$a2,$a2,0x78
/*  f0a78a4:	14c00005 */ 	bnez	$a2,.L0f0a78bc
/*  f0a78a8:	00000000 */ 	nop
/*  f0a78ac:	44064000 */ 	mfc1	$a2,$f8
/*  f0a78b0:	3c018000 */ 	lui	$at,0x8000
/*  f0a78b4:	10000007 */ 	b	.L0f0a78d4
/*  f0a78b8:	00c13025 */ 	or	$a2,$a2,$at
.L0f0a78bc:
/*  f0a78bc:	10000005 */ 	b	.L0f0a78d4
/*  f0a78c0:	2406ffff */ 	addiu	$a2,$zero,-1
/*  f0a78c4:	44064000 */ 	mfc1	$a2,$f8
.L0f0a78c8:
/*  f0a78c8:	00000000 */ 	nop
/*  f0a78cc:	04c0fffb */ 	bltz	$a2,.L0f0a78bc
/*  f0a78d0:	00000000 */ 	nop
.L0f0a78d4:
/*  f0a78d4:	44d8f800 */ 	ctc1	$t8,$31
/*  f0a78d8:	0fc01a40 */ 	jal	colourBlend
/*  f0a78dc:	03202825 */ 	or	$a1,$t9,$zero
/*  f0a78e0:	afa20140 */ 	sw	$v0,0x140($sp)
.L0f0a78e4:
/*  f0a78e4:	8e6f00bc */ 	lw	$t7,0xbc($s3)
.L0f0a78e8:
/*  f0a78e8:	0fc08af9 */ 	jal	chrGetCloakAlpha
/*  f0a78ec:	8de40004 */ 	lw	$a0,0x4($t7)
/*  f0a78f0:	284100ff */ 	slti	$at,$v0,0xff
/*  f0a78f4:	1020000f */ 	beqz	$at,.L0f0a7934
/*  f0a78f8:	240c0001 */ 	addiu	$t4,$zero,0x1
/*  f0a78fc:	44825000 */ 	mtc1	$v0,$f10
/*  f0a7900:	3c017f1b */ 	lui	$at,%hi(var7f1aca90)
/*  f0a7904:	c432ca90 */ 	lwc1	$f18,%lo(var7f1aca90)($at)
/*  f0a7908:	46805420 */ 	cvt.s.w	$f16,$f10
/*  f0a790c:	8fa40140 */ 	lw	$a0,0x140($sp)
/*  f0a7910:	240e0005 */ 	addiu	$t6,$zero,0x5
/*  f0a7914:	afae013c */ 	sw	$t6,0x13c($sp)
/*  f0a7918:	afa40144 */ 	sw	$a0,0x144($sp)
/*  f0a791c:	46128102 */ 	mul.s	$f4,$f16,$f18
/*  f0a7920:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a7924:	44033000 */ 	mfc1	$v1,$f6
/*  f0a7928:	00000000 */ 	nop
/*  f0a792c:	24750041 */ 	addiu	$s5,$v1,0x41
/*  f0a7930:	afb50140 */ 	sw	$s5,0x140($sp)
.L0f0a7934:
/*  f0a7934:	0c0059d8 */ 	jal	mtx00016760
/*  f0a7938:	afac0110 */ 	sw	$t4,0x110($sp)
/*  f0a793c:	8e020218 */ 	lw	$v0,0x218($s0)
/*  f0a7940:	50400017 */ 	beqzl	$v0,.L0f0a79a0
/*  f0a7944:	8fa400ec */ 	lw	$a0,0xec($sp)
/*  f0a7948:	8c460018 */ 	lw	$a2,0x18($v0)
/*  f0a794c:	afa00094 */ 	sw	$zero,0x94($sp)
/*  f0a7950:	50c00013 */ 	beqzl	$a2,.L0f0a79a0
/*  f0a7954:	8fa400ec */ 	lw	$a0,0xec($sp)
/*  f0a7958:	8cc80008 */ 	lw	$t0,0x8($a2)
/*  f0a795c:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f0a7960:	27a4010c */ 	addiu	$a0,$sp,0x10c
/*  f0a7964:	1100000d */ 	beqz	$t0,.L0f0a799c
/*  f0a7968:	00c02825 */ 	or	$a1,$a2,$zero
/*  f0a796c:	afaa0094 */ 	sw	$t2,0x94($sp)
/*  f0a7970:	0c0087bd */ 	jal	modelRender
/*  f0a7974:	afa60098 */ 	sw	$a2,0x98($sp)
/*  f0a7978:	8fa60098 */ 	lw	$a2,0x98($sp)
/*  f0a797c:	8ccb0008 */ 	lw	$t3,0x8($a2)
/*  f0a7980:	8cc4000c */ 	lw	$a0,0xc($a2)
/*  f0a7984:	0fc30cfc */ 	jal	mtxF2LBulk
/*  f0a7988:	8565000e */ 	lh	$a1,0xe($t3)
/*  f0a798c:	8e0d021c */ 	lw	$t5,0x21c($s0)
/*  f0a7990:	51a00003 */ 	beqzl	$t5,.L0f0a79a0
/*  f0a7994:	8fa400ec */ 	lw	$a0,0xec($sp)
/*  f0a7998:	ae000218 */ 	sw	$zero,0x218($s0)
.L0f0a799c:
/*  f0a799c:	8fa400ec */ 	lw	$a0,0xec($sp)
.L0f0a79a0:
/*  f0a79a0:	0fc2c5f0 */ 	jal	weaponHasFlag
/*  f0a79a4:	24050020 */ 	addiu	$a1,$zero,0x20
/*  f0a79a8:	1040000e */ 	beqz	$v0,.L0f0a79e4
/*  f0a79ac:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a79b0:	8fb80118 */ 	lw	$t8,0x118($sp)
/*  f0a79b4:	3c0fb600 */ 	lui	$t7,0xb600
/*  f0a79b8:	24093000 */ 	addiu	$t1,$zero,0x3000
/*  f0a79bc:	27190008 */ 	addiu	$t9,$t8,0x8
/*  f0a79c0:	afb90118 */ 	sw	$t9,0x118($sp)
/*  f0a79c4:	af090004 */ 	sw	$t1,0x4($t8)
/*  f0a79c8:	16800004 */ 	bnez	$s4,.L0f0a79dc
/*  f0a79cc:	af0f0000 */ 	sw	$t7,0x0($t8)
/*  f0a79d0:	240e0003 */ 	addiu	$t6,$zero,0x3
/*  f0a79d4:	10000003 */ 	b	.L0f0a79e4
/*  f0a79d8:	afae0148 */ 	sw	$t6,0x148($sp)
.L0f0a79dc:
/*  f0a79dc:	240c0002 */ 	addiu	$t4,$zero,0x2
/*  f0a79e0:	afac0148 */ 	sw	$t4,0x148($sp)
.L0f0a79e4:
/*  f0a79e4:	8e28006c */ 	lw	$t0,0x6c($s1)
/*  f0a79e8:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a79ec:	00002025 */ 	or	$a0,$zero,$zero
/*  f0a79f0:	11000003 */ 	beqz	$t0,.L0f0a7a00
/*  f0a79f4:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a79f8:	10000001 */ 	b	.L0f0a7a00
/*  f0a79fc:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f0a7a00:
/*  f0a7a00:	8e2a0068 */ 	lw	$t2,0x68($s1)
/*  f0a7a04:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a7a08:	11400003 */ 	beqz	$t2,.L0f0a7a18
/*  f0a7a0c:	00000000 */ 	nop
/*  f0a7a10:	10000001 */ 	b	.L0f0a7a18
/*  f0a7a14:	24040001 */ 	addiu	$a0,$zero,0x1
.L0f0a7a18:
/*  f0a7a18:	8e2b0064 */ 	lw	$t3,0x64($s1)
/*  f0a7a1c:	11600003 */ 	beqz	$t3,.L0f0a7a2c
/*  f0a7a20:	00000000 */ 	nop
/*  f0a7a24:	10000001 */ 	b	.L0f0a7a2c
/*  f0a7a28:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f0a7a2c:
/*  f0a7a2c:	8e2d0070 */ 	lw	$t5,0x70($s1)
/*  f0a7a30:	11a00003 */ 	beqz	$t5,.L0f0a7a40
/*  f0a7a34:	00000000 */ 	nop
/*  f0a7a38:	10000001 */ 	b	.L0f0a7a40
/*  f0a7a3c:	24030001 */ 	addiu	$v1,$zero,0x1
.L0f0a7a40:
/*  f0a7a40:	0062c021 */ 	addu	$t8,$v1,$v0
/*  f0a7a44:	0304c821 */ 	addu	$t9,$t8,$a0
/*  f0a7a48:	03257821 */ 	addu	$t7,$t9,$a1
/*  f0a7a4c:	15e10036 */ 	bne	$t7,$at,.L0f0a7b28
/*  f0a7a50:	24050041 */ 	addiu	$a1,$zero,0x41
/*  f0a7a54:	0c006a47 */ 	jal	modelGetPart
/*  f0a7a58:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a7a5c:	50400033 */ 	beqzl	$v0,.L0f0a7b2c
/*  f0a7a60:	27a4010c */ 	addiu	$a0,$sp,0x10c
/*  f0a7a64:	8c440004 */ 	lw	$a0,0x4($v0)
/*  f0a7a68:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a7a6c:	84890010 */ 	lh	$t1,0x10($a0)
/*  f0a7a70:	5920002e */ 	blezl	$t1,.L0f0a7b2c
/*  f0a7a74:	27a4010c */ 	addiu	$a0,$sp,0x10c
/*  f0a7a78:	8e2a0034 */ 	lw	$t2,0x34($s1)
.L0f0a7a7c:
/*  f0a7a7c:	8c8e000c */ 	lw	$t6,0xc($a0)
/*  f0a7a80:	00056080 */ 	sll	$t4,$a1,0x2
/*  f0a7a84:	01520019 */ 	multu	$t2,$s2
/*  f0a7a88:	01856023 */ 	subu	$t4,$t4,$a1
/*  f0a7a8c:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f0a7a90:	01cc1021 */ 	addu	$v0,$t6,$t4
/*  f0a7a94:	8448000a */ 	lh	$t0,0xa($v0)
/*  f0a7a98:	0005c880 */ 	sll	$t9,$a1,0x2
/*  f0a7a9c:	0325c823 */ 	subu	$t9,$t9,$a1
/*  f0a7aa0:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f0a7aa4:	24420002 */ 	addiu	$v0,$v0,0x2
/*  f0a7aa8:	00005812 */ 	mflo	$t3
/*  f0a7aac:	010b6823 */ 	subu	$t5,$t0,$t3
/*  f0a7ab0:	a44d0008 */ 	sh	$t5,0x8($v0)
/*  f0a7ab4:	8c98000c */ 	lw	$t8,0xc($a0)
/*  f0a7ab8:	03197821 */ 	addu	$t7,$t8,$t9
/*  f0a7abc:	85e9000a */ 	lh	$t1,0xa($t7)
/*  f0a7ac0:	2921a000 */ 	slti	$at,$t1,-24576
/*  f0a7ac4:	50200014 */ 	beqzl	$at,.L0f0a7b18
/*  f0a7ac8:	84980010 */ 	lh	$t8,0x10($a0)
/*  f0a7acc:	848e0010 */ 	lh	$t6,0x10($a0)
/*  f0a7ad0:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a7ad4:	59c00010 */ 	blezl	$t6,.L0f0a7b18
/*  f0a7ad8:	84980010 */ 	lh	$t8,0x10($a0)
/*  f0a7adc:	8c8c000c */ 	lw	$t4,0xc($a0)
.L0f0a7ae0:
/*  f0a7ae0:	00035080 */ 	sll	$t2,$v1,0x2
/*  f0a7ae4:	01435023 */ 	subu	$t2,$t2,$v1
/*  f0a7ae8:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f0a7aec:	018a1021 */ 	addu	$v0,$t4,$t2
/*  f0a7af0:	8448000a */ 	lh	$t0,0xa($v0)
/*  f0a7af4:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f0a7af8:	24420002 */ 	addiu	$v0,$v0,0x2
/*  f0a7afc:	250b2000 */ 	addiu	$t3,$t0,0x2000
/*  f0a7b00:	a44b0008 */ 	sh	$t3,0x8($v0)
/*  f0a7b04:	848d0010 */ 	lh	$t5,0x10($a0)
/*  f0a7b08:	006d082a */ 	slt	$at,$v1,$t5
/*  f0a7b0c:	5420fff4 */ 	bnezl	$at,.L0f0a7ae0
/*  f0a7b10:	8c8c000c */ 	lw	$t4,0xc($a0)
/*  f0a7b14:	84980010 */ 	lh	$t8,0x10($a0)
.L0f0a7b18:
/*  f0a7b18:	24a50001 */ 	addiu	$a1,$a1,0x1
/*  f0a7b1c:	00b8082a */ 	slt	$at,$a1,$t8
/*  f0a7b20:	5420ffd6 */ 	bnezl	$at,.L0f0a7a7c
/*  f0a7b24:	8e2a0034 */ 	lw	$t2,0x34($s1)
.L0f0a7b28:
/*  f0a7b28:	27a4010c */ 	addiu	$a0,$sp,0x10c
.L0f0a7b2c:
/*  f0a7b2c:	0c0087bd */ 	jal	modelRender
/*  f0a7b30:	8fa5003c */ 	lw	$a1,0x3c($sp)
/*  f0a7b34:	8e791594 */ 	lw	$t9,0x1594($s3)
/*  f0a7b38:	3c0f8007 */ 	lui	$t7,%hi(var800702dc)
/*  f0a7b3c:	53200013 */ 	beqzl	$t9,.L0f0a7b8c
/*  f0a7b40:	8fac0118 */ 	lw	$t4,0x118($sp)
/*  f0a7b44:	8def02dc */ 	lw	$t7,%lo(var800702dc)($t7)
/*  f0a7b48:	8fa90140 */ 	lw	$t1,0x140($sp)
/*  f0a7b4c:	51e0000f */ 	beqzl	$t7,.L0f0a7b8c
/*  f0a7b50:	8fac0118 */ 	lw	$t4,0x118($sp)
/*  f0a7b54:	afa9007c */ 	sw	$t1,0x7c($sp)
/*  f0a7b58:	8e0e0390 */ 	lw	$t6,0x390($s0)
/*  f0a7b5c:	26050534 */ 	addiu	$a1,$s0,0x534
/*  f0a7b60:	00a02025 */ 	or	$a0,$a1,$zero
/*  f0a7b64:	ae0e0540 */ 	sw	$t6,0x540($s0)
/*  f0a7b68:	0c007308 */ 	jal	modelUpdateRelations
/*  f0a7b6c:	afa50054 */ 	sw	$a1,0x54($sp)
/*  f0a7b70:	8fa50054 */ 	lw	$a1,0x54($sp)
/*  f0a7b74:	afb50140 */ 	sw	$s5,0x140($sp)
/*  f0a7b78:	0c0087bd */ 	jal	modelRender
/*  f0a7b7c:	27a4010c */ 	addiu	$a0,$sp,0x10c
/*  f0a7b80:	8fa4007c */ 	lw	$a0,0x7c($sp)
/*  f0a7b84:	afa40140 */ 	sw	$a0,0x140($sp)
/*  f0a7b88:	8fac0118 */ 	lw	$t4,0x118($sp)
.L0f0a7b8c:
/*  f0a7b8c:	8fa400ec */ 	lw	$a0,0xec($sp)
/*  f0a7b90:	24050020 */ 	addiu	$a1,$zero,0x20
/*  f0a7b94:	0fc2c5f0 */ 	jal	weaponHasFlag
/*  f0a7b98:	afac014c */ 	sw	$t4,0x14c($sp)
/*  f0a7b9c:	10400007 */ 	beqz	$v0,.L0f0a7bbc
/*  f0a7ba0:	8faa014c */ 	lw	$t2,0x14c($sp)
/*  f0a7ba4:	25480008 */ 	addiu	$t0,$t2,0x8
/*  f0a7ba8:	afa8014c */ 	sw	$t0,0x14c($sp)
/*  f0a7bac:	3c0bb600 */ 	lui	$t3,0xb600
/*  f0a7bb0:	240d3000 */ 	addiu	$t5,$zero,0x3000
/*  f0a7bb4:	ad4d0004 */ 	sw	$t5,0x4($t2)
/*  f0a7bb8:	ad4b0000 */ 	sw	$t3,0x0($t2)
.L0f0a7bbc:
/*  f0a7bbc:	8e18038c */ 	lw	$t8,0x38c($s0)
/*  f0a7bc0:	8e040390 */ 	lw	$a0,0x390($s0)
/*  f0a7bc4:	0fc30cfc */ 	jal	mtxF2LBulk
/*  f0a7bc8:	8705000e */ 	lh	$a1,0xe($t8)
/*  f0a7bcc:	0c0059e1 */ 	jal	mtx00016784
/*  f0a7bd0:	00000000 */ 	nop
/*  f0a7bd4:	8fb9014c */ 	lw	$t9,0x14c($sp)
/*  f0a7bd8:	3c09bc00 */ 	lui	$t1,0xbc00
/*  f0a7bdc:	3529000e */ 	ori	$t1,$t1,0xe
/*  f0a7be0:	272f0008 */ 	addiu	$t7,$t9,0x8
/*  f0a7be4:	afaf014c */ 	sw	$t7,0x14c($sp)
/*  f0a7be8:	af290000 */ 	sw	$t1,0x0($t9)
/*  f0a7bec:	0c002adb */ 	jal	viGetPerspScale
/*  f0a7bf0:	afb90074 */ 	sw	$t9,0x74($sp)
/*  f0a7bf4:	8fa30074 */ 	lw	$v1,0x74($sp)
/*  f0a7bf8:	ac620004 */ 	sw	$v0,0x4($v1)
.L0f0a7bfc:
/*  f0a7bfc:	26940001 */ 	addiu	$s4,$s4,0x1
/*  f0a7c00:	24010002 */ 	addiu	$at,$zero,0x2
/*  f0a7c04:	1681fe04 */ 	bne	$s4,$at,.L0f0a7418
/*  f0a7c08:	261007a4 */ 	addiu	$s0,$s0,0x7a4
/*  f0a7c0c:	afb500e4 */ 	sw	$s5,0xe4($sp)
/*  f0a7c10:	0fc2baf8 */ 	jal	casingsRender
/*  f0a7c14:	27a4014c */ 	addiu	$a0,$sp,0x14c
/*  f0a7c18:	0fc5d8a6 */ 	jal	zbufSwap
/*  f0a7c1c:	00000000 */ 	nop
/*  f0a7c20:	0fc5d8ab */ 	jal	zbufConfigureRdp
/*  f0a7c24:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a7c28:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a7c2c:	0c002c74 */ 	jal	vi0000b1d0
/*  f0a7c30:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a7c34:	244e0008 */ 	addiu	$t6,$v0,0x8
/*  f0a7c38:	afae014c */ 	sw	$t6,0x14c($sp)
/*  f0a7c3c:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7c40:	00408825 */ 	or	$s1,$v0,$zero
/*  f0a7c44:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a7c48:	00106403 */ 	sra	$t4,$s0,0x10
/*  f0a7c4c:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7c50:	01808025 */ 	or	$s0,$t4,$zero
/*  f0a7c54:	44824000 */ 	mtc1	$v0,$f8
/*  f0a7c58:	44902000 */ 	mtc1	$s0,$f4
/*  f0a7c5c:	3c014080 */ 	lui	$at,0x4080
/*  f0a7c60:	468042a0 */ 	cvt.s.w	$f10,$f8
/*  f0a7c64:	44810000 */ 	mtc1	$at,$f0
/*  f0a7c68:	3c01ed00 */ 	lui	$at,0xed00
/*  f0a7c6c:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a7c70:	46005402 */ 	mul.s	$f16,$f10,$f0
/*  f0a7c74:	00000000 */ 	nop
/*  f0a7c78:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a7c7c:	4600848d */ 	trunc.w.s	$f18,$f16
/*  f0a7c80:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a7c84:	44089000 */ 	mfc1	$t0,$f18
/*  f0a7c88:	44195000 */ 	mfc1	$t9,$f10
/*  f0a7c8c:	310b0fff */ 	andi	$t3,$t0,0xfff
/*  f0a7c90:	01616825 */ 	or	$t5,$t3,$at
/*  f0a7c94:	332f0fff */ 	andi	$t7,$t9,0xfff
/*  f0a7c98:	000f4b00 */ 	sll	$t1,$t7,0xc
/*  f0a7c9c:	01a97025 */ 	or	$t6,$t5,$t1
/*  f0a7ca0:	0c002f22 */ 	jal	viGetViewWidth
/*  f0a7ca4:	ae2e0000 */ 	sw	$t6,0x0($s1)
/*  f0a7ca8:	00029400 */ 	sll	$s2,$v0,0x10
/*  f0a7cac:	00126403 */ 	sra	$t4,$s2,0x10
/*  f0a7cb0:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7cb4:	01809025 */ 	or	$s2,$t4,$zero
/*  f0a7cb8:	0002a400 */ 	sll	$s4,$v0,0x10
/*  f0a7cbc:	00145403 */ 	sra	$t2,$s4,0x10
/*  f0a7cc0:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7cc4:	0140a025 */ 	or	$s4,$t2,$zero
/*  f0a7cc8:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a7ccc:	00104403 */ 	sra	$t0,$s0,0x10
/*  f0a7cd0:	0c002f26 */ 	jal	viGetViewHeight
/*  f0a7cd4:	01008025 */ 	or	$s0,$t0,$zero
/*  f0a7cd8:	00505821 */ 	addu	$t3,$v0,$s0
/*  f0a7cdc:	448b8000 */ 	mtc1	$t3,$f16
/*  f0a7ce0:	02926821 */ 	addu	$t5,$s4,$s2
/*  f0a7ce4:	448d5000 */ 	mtc1	$t5,$f10
/*  f0a7ce8:	468084a0 */ 	cvt.s.w	$f18,$f16
/*  f0a7cec:	3c014080 */ 	lui	$at,0x4080
/*  f0a7cf0:	44812000 */ 	mtc1	$at,$f4
/*  f0a7cf4:	46805420 */ 	cvt.s.w	$f16,$f10
/*  f0a7cf8:	46049182 */ 	mul.s	$f6,$f18,$f4
/*  f0a7cfc:	44819000 */ 	mtc1	$at,$f18
/*  f0a7d00:	00000000 */ 	nop
/*  f0a7d04:	46128102 */ 	mul.s	$f4,$f16,$f18
/*  f0a7d08:	4600320d */ 	trunc.w.s	$f8,$f6
/*  f0a7d0c:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a7d10:	44194000 */ 	mfc1	$t9,$f8
/*  f0a7d14:	440e3000 */ 	mfc1	$t6,$f6
/*  f0a7d18:	332f0fff */ 	andi	$t7,$t9,0xfff
/*  f0a7d1c:	31cc0fff */ 	andi	$t4,$t6,0xfff
/*  f0a7d20:	000c5300 */ 	sll	$t2,$t4,0xc
/*  f0a7d24:	01ea4025 */ 	or	$t0,$t7,$t2
/*  f0a7d28:	ae280004 */ 	sw	$t0,0x4($s1)
/*  f0a7d2c:	8fb80150 */ 	lw	$t8,0x150($sp)
/*  f0a7d30:	8fab014c */ 	lw	$t3,0x14c($sp)
/*  f0a7d34:	af0b0000 */ 	sw	$t3,0x0($t8)
/*  f0a7d38:	8fbf0034 */ 	lw	$ra,0x34($sp)
.L0f0a7d3c:
/*  f0a7d3c:	8fb0001c */ 	lw	$s0,0x1c($sp)
/*  f0a7d40:	8fb10020 */ 	lw	$s1,0x20($sp)
/*  f0a7d44:	8fb20024 */ 	lw	$s2,0x24($sp)
/*  f0a7d48:	8fb30028 */ 	lw	$s3,0x28($sp)
/*  f0a7d4c:	8fb4002c */ 	lw	$s4,0x2c($sp)
/*  f0a7d50:	8fb50030 */ 	lw	$s5,0x30($sp)
/*  f0a7d54:	03e00008 */ 	jr	$ra
/*  f0a7d58:	27bd0150 */ 	addiu	$sp,$sp,0x150
);
#elif VERSION >= VERSION_NTSC_1_0
GLOBAL_ASM(
glabel bgunRender
.late_rodata
glabel var7f1aca8c
.word 0x3faaaaab
glabel var7f1aca90
.word 0x3f3ebebf
.text
/*  f0a7138:	27bdfeb0 */ 	addiu	$sp,$sp,-336
/*  f0a713c:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f0a7140:	afb50030 */ 	sw	$s5,0x30($sp)
/*  f0a7144:	afb4002c */ 	sw	$s4,0x2c($sp)
/*  f0a7148:	afb30028 */ 	sw	$s3,0x28($sp)
/*  f0a714c:	afb20024 */ 	sw	$s2,0x24($sp)
/*  f0a7150:	afb10020 */ 	sw	$s1,0x20($sp)
/*  f0a7154:	afb0001c */ 	sw	$s0,0x1c($sp)
/*  f0a7158:	afa40150 */ 	sw	$a0,0x150($sp)
/*  f0a715c:	8c8f0000 */ 	lw	$t7,0x0($a0)
/*  f0a7160:	3c198007 */ 	lui	$t9,%hi(var8007029c)
/*  f0a7164:	3c11800a */ 	lui	$s1,%hi(g_Vars)
/*  f0a7168:	2739029c */ 	addiu	$t9,$t9,%lo(var8007029c)
/*  f0a716c:	26319fc0 */ 	addiu	$s1,$s1,%lo(g_Vars)
/*  f0a7170:	272a003c */ 	addiu	$t2,$t9,0x3c
/*  f0a7174:	27b8010c */ 	addiu	$t8,$sp,0x10c
/*  f0a7178:	afaf014c */ 	sw	$t7,0x14c($sp)
.L0f0a717c:
/*  f0a717c:	8f210000 */ 	lw	$at,0x0($t9)
/*  f0a7180:	2739000c */ 	addiu	$t9,$t9,0xc
/*  f0a7184:	2718000c */ 	addiu	$t8,$t8,0xc
/*  f0a7188:	af01fff4 */ 	sw	$at,-0xc($t8)
/*  f0a718c:	8f21fff8 */ 	lw	$at,-0x8($t9)
/*  f0a7190:	af01fff8 */ 	sw	$at,-0x8($t8)
/*  f0a7194:	8f21fffc */ 	lw	$at,-0x4($t9)
/*  f0a7198:	172afff8 */ 	bne	$t9,$t2,.L0f0a717c
/*  f0a719c:	af01fffc */ 	sw	$at,-0x4($t8)
/*  f0a71a0:	8f210000 */ 	lw	$at,0x0($t9)
/*  f0a71a4:	af010000 */ 	sw	$at,0x0($t8)
/*  f0a71a8:	8e330284 */ 	lw	$s3,0x284($s1)
/*  f0a71ac:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a71b0:	966b0010 */ 	lhu	$t3,0x10($s3)
/*  f0a71b4:	1561000d */ 	bne	$t3,$at,.L0f0a71ec
/*  f0a71b8:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a71bc:	24040f48 */ 	addiu	$a0,$zero,0xf48
/*  f0a71c0:	8e2c0284 */ 	lw	$t4,0x284($s1)
.L0f0a71c4:
/*  f0a71c4:	01821821 */ 	addu	$v1,$t4,$v0
/*  f0a71c8:	8c6d0854 */ 	lw	$t5,0x854($v1)
/*  f0a71cc:	244207a4 */ 	addiu	$v0,$v0,0x7a4
/*  f0a71d0:	11a00002 */ 	beqz	$t5,.L0f0a71dc
/*  f0a71d4:	00000000 */ 	nop
/*  f0a71d8:	ac600850 */ 	sw	$zero,0x850($v1)
.L0f0a71dc:
/*  f0a71dc:	5444fff9 */ 	bnel	$v0,$a0,.L0f0a71c4
/*  f0a71e0:	8e2c0284 */ 	lw	$t4,0x284($s1)
/*  f0a71e4:	100002d5 */ 	b	.L0f0a7d3c
/*  f0a71e8:	8fbf0034 */ 	lw	$ra,0x34($sp)
.L0f0a71ec:
/*  f0a71ec:	0fc5d9ad */ 	jal	zbufDrawArtifactsOffscreen
/*  f0a71f0:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a71f4:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a71f8:	0c002ca0 */ 	jal	viPrepareZbuf
/*  f0a71fc:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a7200:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a7204:	0c002c74 */ 	jal	vi0000b1d0
/*  f0a7208:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a720c:	244e0008 */ 	addiu	$t6,$v0,0x8
/*  f0a7210:	afae014c */ 	sw	$t6,0x14c($sp)
/*  f0a7214:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7218:	0040a825 */ 	or	$s5,$v0,$zero
/*  f0a721c:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a7220:	00107c03 */ 	sra	$t7,$s0,0x10
/*  f0a7224:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7228:	01e08025 */ 	or	$s0,$t7,$zero
/*  f0a722c:	44822000 */ 	mtc1	$v0,$f4
/*  f0a7230:	44908000 */ 	mtc1	$s0,$f16
/*  f0a7234:	3c014080 */ 	lui	$at,0x4080
/*  f0a7238:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a723c:	44810000 */ 	mtc1	$at,$f0
/*  f0a7240:	3c01ed00 */ 	lui	$at,0xed00
/*  f0a7244:	468084a0 */ 	cvt.s.w	$f18,$f16
/*  f0a7248:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a724c:	00000000 */ 	nop
/*  f0a7250:	46009102 */ 	mul.s	$f4,$f18,$f0
/*  f0a7254:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a7258:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a725c:	44085000 */ 	mfc1	$t0,$f10
/*  f0a7260:	440b3000 */ 	mfc1	$t3,$f6
/*  f0a7264:	310a0fff */ 	andi	$t2,$t0,0xfff
/*  f0a7268:	0141c825 */ 	or	$t9,$t2,$at
/*  f0a726c:	316c0fff */ 	andi	$t4,$t3,0xfff
/*  f0a7270:	000c6b00 */ 	sll	$t5,$t4,0xc
/*  f0a7274:	032d7025 */ 	or	$t6,$t9,$t5
/*  f0a7278:	0c002f22 */ 	jal	viGetViewWidth
/*  f0a727c:	aeae0000 */ 	sw	$t6,0x0($s5)
/*  f0a7280:	00029400 */ 	sll	$s2,$v0,0x10
/*  f0a7284:	00127c03 */ 	sra	$t7,$s2,0x10
/*  f0a7288:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a728c:	01e09025 */ 	or	$s2,$t7,$zero
/*  f0a7290:	0002a400 */ 	sll	$s4,$v0,0x10
/*  f0a7294:	00144c03 */ 	sra	$t1,$s4,0x10
/*  f0a7298:	0c002f44 */ 	jal	viGetViewTop
/*  f0a729c:	0120a025 */ 	or	$s4,$t1,$zero
/*  f0a72a0:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a72a4:	00104403 */ 	sra	$t0,$s0,0x10
/*  f0a72a8:	0c002f26 */ 	jal	viGetViewHeight
/*  f0a72ac:	01008025 */ 	or	$s0,$t0,$zero
/*  f0a72b0:	00505021 */ 	addu	$t2,$v0,$s0
/*  f0a72b4:	448a4000 */ 	mtc1	$t2,$f8
/*  f0a72b8:	0292c821 */ 	addu	$t9,$s4,$s2
/*  f0a72bc:	44992000 */ 	mtc1	$t9,$f4
/*  f0a72c0:	468042a0 */ 	cvt.s.w	$f10,$f8
/*  f0a72c4:	3c014080 */ 	lui	$at,0x4080
/*  f0a72c8:	44810000 */ 	mtc1	$at,$f0
/*  f0a72cc:	3c053fc0 */ 	lui	$a1,0x3fc0
/*  f0a72d0:	3c06447a */ 	lui	$a2,0x447a
/*  f0a72d4:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a72d8:	46005402 */ 	mul.s	$f16,$f10,$f0
/*  f0a72dc:	00000000 */ 	nop
/*  f0a72e0:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a72e4:	4600848d */ 	trunc.w.s	$f18,$f16
/*  f0a72e8:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a72ec:	440b9000 */ 	mfc1	$t3,$f18
/*  f0a72f0:	440e5000 */ 	mfc1	$t6,$f10
/*  f0a72f4:	316c0fff */ 	andi	$t4,$t3,0xfff
/*  f0a72f8:	31cf0fff */ 	andi	$t7,$t6,0xfff
/*  f0a72fc:	000f4b00 */ 	sll	$t1,$t7,0xc
/*  f0a7300:	01894025 */ 	or	$t0,$t4,$t1
/*  f0a7304:	aea80004 */ 	sw	$t0,0x4($s5)
/*  f0a7308:	0c002b29 */ 	jal	vi0000aca4
/*  f0a730c:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a7310:	8e2a0284 */ 	lw	$t2,0x284($s1)
/*  f0a7314:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a7318:	91581bfc */ 	lbu	$t8,0x1bfc($t2)
/*  f0a731c:	53000016 */ 	beqzl	$t8,.L0f0a7378
/*  f0a7320:	8e2b006c */ 	lw	$t3,0x6c($s1)
/*  f0a7324:	0fc54bc7 */ 	jal	optionsGetScreenRatio
/*  f0a7328:	00000000 */ 	nop
/*  f0a732c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a7330:	14410008 */ 	bne	$v0,$at,.L0f0a7354
/*  f0a7334:	00000000 */ 	nop
/*  f0a7338:	0fc2f4d6 */ 	jal	player0f0bd358
/*  f0a733c:	00000000 */ 	nop
/*  f0a7340:	3c017f1b */ 	lui	$at,%hi(var7f1aca8c)
/*  f0a7344:	c430ca8c */ 	lwc1	$f16,%lo(var7f1aca8c)($at)
/*  f0a7348:	46100082 */ 	mul.s	$f2,$f0,$f16
/*  f0a734c:	10000005 */ 	b	.L0f0a7364
/*  f0a7350:	44061000 */ 	mfc1	$a2,$f2
.L0f0a7354:
/*  f0a7354:	0fc2f4d6 */ 	jal	player0f0bd358
/*  f0a7358:	00000000 */ 	nop
/*  f0a735c:	46000086 */ 	mov.s	$f2,$f0
/*  f0a7360:	44061000 */ 	mfc1	$a2,$f2
.L0f0a7364:
/*  f0a7364:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a7368:	0c002c3a */ 	jal	vi0000b0e8
/*  f0a736c:	3c054270 */ 	lui	$a1,0x4270
/*  f0a7370:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a7374:	8e2b006c */ 	lw	$t3,0x6c($s1)
.L0f0a7378:
/*  f0a7378:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a737c:	51600004 */ 	beqzl	$t3,.L0f0a7390
/*  f0a7380:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a7384:	10000002 */ 	b	.L0f0a7390
/*  f0a7388:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f0a738c:	00002825 */ 	or	$a1,$zero,$zero
.L0f0a7390:
/*  f0a7390:	8e390068 */ 	lw	$t9,0x68($s1)
/*  f0a7394:	53200004 */ 	beqzl	$t9,.L0f0a73a8
/*  f0a7398:	00002025 */ 	or	$a0,$zero,$zero
/*  f0a739c:	10000002 */ 	b	.L0f0a73a8
/*  f0a73a0:	24040001 */ 	addiu	$a0,$zero,0x1
/*  f0a73a4:	00002025 */ 	or	$a0,$zero,$zero
.L0f0a73a8:
/*  f0a73a8:	8e2d0064 */ 	lw	$t5,0x64($s1)
/*  f0a73ac:	51a00004 */ 	beqzl	$t5,.L0f0a73c0
/*  f0a73b0:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a73b4:	10000002 */ 	b	.L0f0a73c0
/*  f0a73b8:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f0a73bc:	00001025 */ 	or	$v0,$zero,$zero
.L0f0a73c0:
/*  f0a73c0:	8e2e0070 */ 	lw	$t6,0x70($s1)
/*  f0a73c4:	51c00004 */ 	beqzl	$t6,.L0f0a73d8
/*  f0a73c8:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a73cc:	10000002 */ 	b	.L0f0a73d8
/*  f0a73d0:	24030001 */ 	addiu	$v1,$zero,0x1
/*  f0a73d4:	00001825 */ 	or	$v1,$zero,$zero
.L0f0a73d8:
/*  f0a73d8:	00627821 */ 	addu	$t7,$v1,$v0
/*  f0a73dc:	01e46021 */ 	addu	$t4,$t7,$a0
/*  f0a73e0:	01854821 */ 	addu	$t1,$t4,$a1
/*  f0a73e4:	15210008 */ 	bne	$t1,$at,.L0f0a7408
/*  f0a73e8:	3c088009 */ 	lui	$t0,%hi(g_Is4Mb)
/*  f0a73ec:	91080af0 */ 	lbu	$t0,%lo(g_Is4Mb)($t0)
/*  f0a73f0:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a73f4:	51010005 */ 	beql	$t0,$at,.L0f0a740c
/*  f0a73f8:	0000a025 */ 	or	$s4,$zero,$zero
/*  f0a73fc:	0fc2be93 */ 	jal	lasersightRenderBeam
/*  f0a7400:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a7404:	afa2014c */ 	sw	$v0,0x14c($sp)
.L0f0a7408:
/*  f0a7408:	0000a025 */ 	or	$s4,$zero,$zero
.L0f0a740c:
/*  f0a740c:	26700638 */ 	addiu	$s0,$s3,0x638
/*  f0a7410:	8fb500e4 */ 	lw	$s5,0xe4($sp)
/*  f0a7414:	24120019 */ 	addiu	$s2,$zero,0x19
.L0f0a7418:
/*  f0a7418:	0fc2867c */ 	jal	bgunGetWeaponNum2
/*  f0a741c:	02802025 */ 	or	$a0,$s4,$zero
/*  f0a7420:	afa200ec */ 	sw	$v0,0xec($sp)
/*  f0a7424:	820a0007 */ 	lb	$t2,0x7($s0)
/*  f0a7428:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a742c:	260501dc */ 	addiu	$a1,$s0,0x1dc
/*  f0a7430:	114001f2 */ 	beqz	$t2,.L0f0a7bfc
/*  f0a7434:	00003025 */ 	or	$a2,$zero,$zero
/*  f0a7438:	26180384 */ 	addiu	$t8,$s0,0x384
/*  f0a743c:	afb8003c */ 	sw	$t8,0x3c($sp)
/*  f0a7440:	0fc2b2e4 */ 	jal	beamRender
/*  f0a7444:	00003825 */ 	or	$a3,$zero,$zero
/*  f0a7448:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a744c:	92040000 */ 	lbu	$a0,0x0($s0)
/*  f0a7450:	0fc2c5f0 */ 	jal	weaponHasFlag
/*  f0a7454:	34058000 */ 	dli	$a1,0x8000
/*  f0a7458:	10400030 */ 	beqz	$v0,.L0f0a751c
/*  f0a745c:	8fab014c */ 	lw	$t3,0x14c($sp)
/*  f0a7460:	25790008 */ 	addiu	$t9,$t3,0x8
/*  f0a7464:	afb9014c */ 	sw	$t9,0x14c($sp)
/*  f0a7468:	3c0dbc00 */ 	lui	$t5,0xbc00
/*  f0a746c:	3c0e8000 */ 	lui	$t6,0x8000
/*  f0a7470:	35ce0040 */ 	ori	$t6,$t6,0x40
/*  f0a7474:	35ad0002 */ 	ori	$t5,$t5,0x2
/*  f0a7478:	ad6d0000 */ 	sw	$t5,0x0($t3)
/*  f0a747c:	ad6e0004 */ 	sw	$t6,0x4($t3)
/*  f0a7480:	8faf014c */ 	lw	$t7,0x14c($sp)
/*  f0a7484:	3c090386 */ 	lui	$t1,0x386
/*  f0a7488:	3c088007 */ 	lui	$t0,%hi(var80070090+0x08)
/*  f0a748c:	25ec0008 */ 	addiu	$t4,$t7,0x8
/*  f0a7490:	afac014c */ 	sw	$t4,0x14c($sp)
/*  f0a7494:	25080098 */ 	addiu	$t0,$t0,%lo(var80070090+0x08)
/*  f0a7498:	35290010 */ 	ori	$t1,$t1,0x10
/*  f0a749c:	ade90000 */ 	sw	$t1,0x0($t7)
/*  f0a74a0:	ade80004 */ 	sw	$t0,0x4($t7)
/*  f0a74a4:	8faa014c */ 	lw	$t2,0x14c($sp)
/*  f0a74a8:	3c0b0388 */ 	lui	$t3,0x388
/*  f0a74ac:	3c198007 */ 	lui	$t9,%hi(var80070090)
/*  f0a74b0:	25580008 */ 	addiu	$t8,$t2,0x8
/*  f0a74b4:	afb8014c */ 	sw	$t8,0x14c($sp)
/*  f0a74b8:	27390090 */ 	addiu	$t9,$t9,%lo(var80070090)
/*  f0a74bc:	356b0010 */ 	ori	$t3,$t3,0x10
/*  f0a74c0:	ad4b0000 */ 	sw	$t3,0x0($t2)
/*  f0a74c4:	ad590004 */ 	sw	$t9,0x4($t2)
/*  f0a74c8:	8fad014c */ 	lw	$t5,0x14c($sp)
/*  f0a74cc:	3c0f0384 */ 	lui	$t7,0x384
/*  f0a74d0:	35ef0010 */ 	ori	$t7,$t7,0x10
/*  f0a74d4:	25ae0008 */ 	addiu	$t6,$t5,0x8
/*  f0a74d8:	afae014c */ 	sw	$t6,0x14c($sp)
/*  f0a74dc:	adaf0000 */ 	sw	$t7,0x0($t5)
/*  f0a74e0:	0fc2d5ea */ 	jal	camGetLookAt
/*  f0a74e4:	afad00d4 */ 	sw	$t5,0xd4($sp)
/*  f0a74e8:	8fa500d4 */ 	lw	$a1,0xd4($sp)
/*  f0a74ec:	3c080382 */ 	lui	$t0,0x382
/*  f0a74f0:	35080010 */ 	ori	$t0,$t0,0x10
/*  f0a74f4:	aca20004 */ 	sw	$v0,0x4($a1)
/*  f0a74f8:	8fac014c */ 	lw	$t4,0x14c($sp)
/*  f0a74fc:	25890008 */ 	addiu	$t1,$t4,0x8
/*  f0a7500:	afa9014c */ 	sw	$t1,0x14c($sp)
/*  f0a7504:	ad880000 */ 	sw	$t0,0x0($t4)
/*  f0a7508:	0fc2d5ea */ 	jal	camGetLookAt
/*  f0a750c:	afac00d0 */ 	sw	$t4,0xd0($sp)
/*  f0a7510:	8fa300d0 */ 	lw	$v1,0xd0($sp)
/*  f0a7514:	244a0010 */ 	addiu	$t2,$v0,0x10
/*  f0a7518:	ac6a0004 */ 	sw	$t2,0x4($v1)
.L0f0a751c:
/*  f0a751c:	8fb8014c */ 	lw	$t8,0x14c($sp)
/*  f0a7520:	3c19bc00 */ 	lui	$t9,0xbc00
/*  f0a7524:	3739000e */ 	ori	$t9,$t9,0xe
/*  f0a7528:	270b0008 */ 	addiu	$t3,$t8,0x8
/*  f0a752c:	afab014c */ 	sw	$t3,0x14c($sp)
/*  f0a7530:	3c014396 */ 	lui	$at,0x4396
/*  f0a7534:	44817000 */ 	mtc1	$at,$f14
/*  f0a7538:	44806000 */ 	mtc1	$zero,$f12
/*  f0a753c:	af190000 */ 	sw	$t9,0x0($t8)
/*  f0a7540:	0c005b73 */ 	jal	mtx00016dcc
/*  f0a7544:	afb800cc */ 	sw	$t8,0xcc($sp)
/*  f0a7548:	8fa300cc */ 	lw	$v1,0xcc($sp)
/*  f0a754c:	24050010 */ 	addiu	$a1,$zero,0x10
/*  f0a7550:	ac620004 */ 	sw	$v0,0x4($v1)
/*  f0a7554:	0c006a47 */ 	jal	modelGetPart
/*  f0a7558:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a755c:	10400014 */ 	beqz	$v0,.L0f0a75b0
/*  f0a7560:	afa200e8 */ 	sw	$v0,0xe8($sp)
/*  f0a7564:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a7568:	0c006a47 */ 	jal	modelGetPart
/*  f0a756c:	24050011 */ 	addiu	$a1,$zero,0x11
/*  f0a7570:	8fa4003c */ 	lw	$a0,0x3c($sp)
/*  f0a7574:	0c006a87 */ 	jal	modelGetNodeRwData
/*  f0a7578:	00402825 */ 	or	$a1,$v0,$zero
/*  f0a757c:	10400003 */ 	beqz	$v0,.L0f0a758c
/*  f0a7580:	3c06800a */ 	lui	$a2,%hi(var8009cf88)
/*  f0a7584:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f0a7588:	ac4d0000 */ 	sw	$t5,0x0($v0)
.L0f0a758c:
/*  f0a758c:	240e0001 */ 	addiu	$t6,$zero,0x1
/*  f0a7590:	afae0014 */ 	sw	$t6,0x14($sp)
/*  f0a7594:	8fa4003c */ 	lw	$a0,0x3c($sp)
/*  f0a7598:	8fa500e8 */ 	lw	$a1,0xe8($sp)
/*  f0a759c:	24c6cf88 */ 	addiu	$a2,$a2,%lo(var8009cf88)
/*  f0a75a0:	8fa7014c */ 	lw	$a3,0x14c($sp)
/*  f0a75a4:	0fc1fefe */ 	jal	tvscreenRender
/*  f0a75a8:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f0a75ac:	afa2014c */ 	sw	$v0,0x14c($sp)
.L0f0a75b0:
/*  f0a75b0:	8faf014c */ 	lw	$t7,0x14c($sp)
/*  f0a75b4:	8e250284 */ 	lw	$a1,0x284($s1)
/*  f0a75b8:	240c0004 */ 	addiu	$t4,$zero,0x4
/*  f0a75bc:	afac013c */ 	sw	$t4,0x13c($sp)
/*  f0a75c0:	afaf0118 */ 	sw	$t7,0x118($sp)
/*  f0a75c4:	8ca300d8 */ 	lw	$v1,0xd8($a1)
/*  f0a75c8:	3c078007 */ 	lui	$a3,%hi(g_InCutscene)
/*  f0a75cc:	14600013 */ 	bnez	$v1,.L0f0a761c
/*  f0a75d0:	00000000 */ 	nop
/*  f0a75d4:	8ce70764 */ 	lw	$a3,%lo(g_InCutscene)($a3)
/*  f0a75d8:	14e00010 */ 	bnez	$a3,.L0f0a761c
/*  f0a75dc:	00000000 */ 	nop
/*  f0a75e0:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a75e4:	50400007 */ 	beqzl	$v0,.L0f0a7604
/*  f0a75e8:	8caa1c54 */ 	lw	$t2,0x1c54($a1)
/*  f0a75ec:	1040000b */ 	beqz	$v0,.L0f0a761c
/*  f0a75f0:	00000000 */ 	nop
/*  f0a75f4:	80490037 */ 	lb	$t1,0x37($v0)
/*  f0a75f8:	15200008 */ 	bnez	$t1,.L0f0a761c
/*  f0a75fc:	00000000 */ 	nop
/*  f0a7600:	8caa1c54 */ 	lw	$t2,0x1c54($a1)
.L0f0a7604:
/*  f0a7604:	8ca800c4 */ 	lw	$t0,0xc4($a1)
/*  f0a7608:	0140c027 */ 	nor	$t8,$t2,$zero
/*  f0a760c:	01185824 */ 	and	$t3,$t0,$t8
/*  f0a7610:	31790001 */ 	andi	$t9,$t3,0x1
/*  f0a7614:	57200016 */ 	bnezl	$t9,.L0f0a7670
/*  f0a7618:	92681615 */ 	lbu	$t0,0x1615($s3)
.L0f0a761c:
/*  f0a761c:	14600078 */ 	bnez	$v1,.L0f0a7800
/*  f0a7620:	3c078007 */ 	lui	$a3,%hi(g_InCutscene)
/*  f0a7624:	8ce70764 */ 	lw	$a3,%lo(g_InCutscene)($a3)
/*  f0a7628:	54e00076 */ 	bnezl	$a3,.L0f0a7804
/*  f0a762c:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a7630:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a7634:	50400007 */ 	beqzl	$v0,.L0f0a7654
/*  f0a7638:	8caf1c54 */ 	lw	$t7,0x1c54($a1)
/*  f0a763c:	50400071 */ 	beqzl	$v0,.L0f0a7804
/*  f0a7640:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a7644:	804d0037 */ 	lb	$t5,0x37($v0)
/*  f0a7648:	55a0006e */ 	bnezl	$t5,.L0f0a7804
/*  f0a764c:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a7650:	8caf1c54 */ 	lw	$t7,0x1c54($a1)
.L0f0a7654:
/*  f0a7654:	8cae00c4 */ 	lw	$t6,0xc4($a1)
/*  f0a7658:	01e06027 */ 	nor	$t4,$t7,$zero
/*  f0a765c:	01cc4824 */ 	and	$t1,$t6,$t4
/*  f0a7660:	312a0008 */ 	andi	$t2,$t1,0x8
/*  f0a7664:	51400067 */ 	beqzl	$t2,.L0f0a7804
/*  f0a7668:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a766c:	92681615 */ 	lbu	$t0,0x1615($s3)
.L0f0a7670:
/*  f0a7670:	92781614 */ 	lbu	$t8,0x1614($s3)
/*  f0a7674:	26641614 */ 	addiu	$a0,$s3,0x1614
/*  f0a7678:	0118082a */ 	slt	$at,$t0,$t8
/*  f0a767c:	50200009 */ 	beqzl	$at,.L0f0a76a4
/*  f0a7680:	90820002 */ 	lbu	$v0,0x2($a0)
/*  f0a7684:	90830000 */ 	lbu	$v1,0x0($a0)
/*  f0a7688:	908b0002 */ 	lbu	$t3,0x2($a0)
/*  f0a768c:	0163082a */ 	slt	$at,$t3,$v1
/*  f0a7690:	50200004 */ 	beqzl	$at,.L0f0a76a4
/*  f0a7694:	90820002 */ 	lbu	$v0,0x2($a0)
/*  f0a7698:	1000000a */ 	b	.L0f0a76c4
/*  f0a769c:	00601025 */ 	or	$v0,$v1,$zero
/*  f0a76a0:	90820002 */ 	lbu	$v0,0x2($a0)
.L0f0a76a4:
/*  f0a76a4:	90860001 */ 	lbu	$a2,0x1($a0)
/*  f0a76a8:	00401825 */ 	or	$v1,$v0,$zero
/*  f0a76ac:	0046082a */ 	slt	$at,$v0,$a2
/*  f0a76b0:	10200003 */ 	beqz	$at,.L0f0a76c0
/*  f0a76b4:	00000000 */ 	nop
/*  f0a76b8:	10000001 */ 	b	.L0f0a76c0
/*  f0a76bc:	00c01825 */ 	or	$v1,$a2,$zero
.L0f0a76c0:
/*  f0a76c0:	00601025 */ 	or	$v0,$v1,$zero
.L0f0a76c4:
/*  f0a76c4:	90890003 */ 	lbu	$t1,0x3($a0)
/*  f0a76c8:	0002ce00 */ 	sll	$t9,$v0,0x18
/*  f0a76cc:	00026c00 */ 	sll	$t5,$v0,0x10
/*  f0a76d0:	032d7825 */ 	or	$t7,$t9,$t5
/*  f0a76d4:	00027200 */ 	sll	$t6,$v0,0x8
/*  f0a76d8:	01ee6025 */ 	or	$t4,$t7,$t6
/*  f0a76dc:	012c5021 */ 	addu	$t2,$t1,$t4
/*  f0a76e0:	afaa0140 */ 	sw	$t2,0x140($sp)
/*  f0a76e4:	8ca300d8 */ 	lw	$v1,0xd8($a1)
/*  f0a76e8:	14600021 */ 	bnez	$v1,.L0f0a7770
/*  f0a76ec:	00000000 */ 	nop
/*  f0a76f0:	14e0001f */ 	bnez	$a3,.L0f0a7770
/*  f0a76f4:	00000000 */ 	nop
/*  f0a76f8:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a76fc:	50400007 */ 	beqzl	$v0,.L0f0a771c
/*  f0a7700:	8cab1c54 */ 	lw	$t3,0x1c54($a1)
/*  f0a7704:	1040001a */ 	beqz	$v0,.L0f0a7770
/*  f0a7708:	00000000 */ 	nop
/*  f0a770c:	80480037 */ 	lb	$t0,0x37($v0)
/*  f0a7710:	15000017 */ 	bnez	$t0,.L0f0a7770
/*  f0a7714:	00000000 */ 	nop
/*  f0a7718:	8cab1c54 */ 	lw	$t3,0x1c54($a1)
.L0f0a771c:
/*  f0a771c:	8cb800c4 */ 	lw	$t8,0xc4($a1)
/*  f0a7720:	3c02800a */ 	lui	$v0,%hi(var8009caec+0x3)
/*  f0a7724:	0160c827 */ 	nor	$t9,$t3,$zero
/*  f0a7728:	03196824 */ 	and	$t5,$t8,$t9
/*  f0a772c:	31af0001 */ 	andi	$t7,$t5,0x1
/*  f0a7730:	11e0000f */ 	beqz	$t7,.L0f0a7770
/*  f0a7734:	00000000 */ 	nop
/*  f0a7738:	9042caef */ 	lbu	$v0,%lo(var8009caec+0x3)($v0)
/*  f0a773c:	3c06800a */ 	lui	$a2,%hi(var8009caf0)
/*  f0a7740:	90c6caf0 */ 	lbu	$a2,%lo(var8009caf0)($a2)
/*  f0a7744:	00027600 */ 	sll	$t6,$v0,0x18
/*  f0a7748:	00024c00 */ 	sll	$t1,$v0,0x10
/*  f0a774c:	01c96025 */ 	or	$t4,$t6,$t1
/*  f0a7750:	00025200 */ 	sll	$t2,$v0,0x8
/*  f0a7754:	018a4025 */ 	or	$t0,$t4,$t2
/*  f0a7758:	afa200b0 */ 	sw	$v0,0xb0($sp)
/*  f0a775c:	afa200b4 */ 	sw	$v0,0xb4($sp)
/*  f0a7760:	afa200b8 */ 	sw	$v0,0xb8($sp)
/*  f0a7764:	00c8a821 */ 	addu	$s5,$a2,$t0
/*  f0a7768:	1000001f */ 	b	.L0f0a77e8
/*  f0a776c:	afa600bc */ 	sw	$a2,0xbc($sp)
.L0f0a7770:
/*  f0a7770:	5460001e */ 	bnezl	$v1,.L0f0a77ec
/*  f0a7774:	8fb900ec */ 	lw	$t9,0xec($sp)
/*  f0a7778:	54e0001c */ 	bnezl	$a3,.L0f0a77ec
/*  f0a777c:	8fb900ec */ 	lw	$t9,0xec($sp)
/*  f0a7780:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a7784:	50400007 */ 	beqzl	$v0,.L0f0a77a4
/*  f0a7788:	8cb91c54 */ 	lw	$t9,0x1c54($a1)
/*  f0a778c:	50400017 */ 	beqzl	$v0,.L0f0a77ec
/*  f0a7790:	8fb900ec */ 	lw	$t9,0xec($sp)
/*  f0a7794:	804b0037 */ 	lb	$t3,0x37($v0)
/*  f0a7798:	55600014 */ 	bnezl	$t3,.L0f0a77ec
/*  f0a779c:	8fb900ec */ 	lw	$t9,0xec($sp)
/*  f0a77a0:	8cb91c54 */ 	lw	$t9,0x1c54($a1)
.L0f0a77a4:
/*  f0a77a4:	8cb800c4 */ 	lw	$t8,0xc4($a1)
/*  f0a77a8:	240200ff */ 	addiu	$v0,$zero,0xff
/*  f0a77ac:	03206827 */ 	nor	$t5,$t9,$zero
/*  f0a77b0:	030d7824 */ 	and	$t7,$t8,$t5
/*  f0a77b4:	31ee0008 */ 	andi	$t6,$t7,0x8
/*  f0a77b8:	11c0000b */ 	beqz	$t6,.L0f0a77e8
/*  f0a77bc:	24050080 */ 	addiu	$a1,$zero,0x80
/*  f0a77c0:	00024e00 */ 	sll	$t1,$v0,0x18
/*  f0a77c4:	00006400 */ 	sll	$t4,$zero,0x10
/*  f0a77c8:	012c5025 */ 	or	$t2,$t1,$t4
/*  f0a77cc:	00004200 */ 	sll	$t0,$zero,0x8
/*  f0a77d0:	01485825 */ 	or	$t3,$t2,$t0
/*  f0a77d4:	00aba821 */ 	addu	$s5,$a1,$t3
/*  f0a77d8:	afa200a0 */ 	sw	$v0,0xa0($sp)
/*  f0a77dc:	afa000a4 */ 	sw	$zero,0xa4($sp)
/*  f0a77e0:	afa000a8 */ 	sw	$zero,0xa8($sp)
/*  f0a77e4:	afa500ac */ 	sw	$a1,0xac($sp)
.L0f0a77e8:
/*  f0a77e8:	8fb900ec */ 	lw	$t9,0xec($sp)
.L0f0a77ec:
/*  f0a77ec:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a77f0:	5721003d */ 	bnel	$t9,$at,.L0f0a78e8
/*  f0a77f4:	8e6f00bc */ 	lw	$t7,0xbc($s3)
/*  f0a77f8:	1000003a */ 	b	.L0f0a78e4
/*  f0a77fc:	afb50140 */ 	sw	$s5,0x140($sp)
.L0f0a7800:
/*  f0a7800:	926d1614 */ 	lbu	$t5,0x1614($s3)
.L0f0a7804:
/*  f0a7804:	92781617 */ 	lbu	$t8,0x1617($s3)
/*  f0a7808:	92691615 */ 	lbu	$t1,0x1615($s3)
/*  f0a780c:	92681616 */ 	lbu	$t0,0x1616($s3)
/*  f0a7810:	000d7e00 */ 	sll	$t7,$t5,0x18
/*  f0a7814:	030f7025 */ 	or	$t6,$t8,$t7
/*  f0a7818:	00096400 */ 	sll	$t4,$t1,0x10
/*  f0a781c:	01cc5025 */ 	or	$t2,$t6,$t4
/*  f0a7820:	00085a00 */ 	sll	$t3,$t0,0x8
/*  f0a7824:	014bc825 */ 	or	$t9,$t2,$t3
/*  f0a7828:	afb90140 */ 	sw	$t9,0x140($sp)
/*  f0a782c:	920d0000 */ 	lbu	$t5,0x0($s0)
/*  f0a7830:	24010006 */ 	addiu	$at,$zero,0x6
/*  f0a7834:	0320a825 */ 	or	$s5,$t9,$zero
/*  f0a7838:	15a1002a */ 	bne	$t5,$at,.L0f0a78e4
/*  f0a783c:	3c04ff00 */ 	lui	$a0,0xff00
/*  f0a7840:	3c014248 */ 	lui	$at,0x4248
/*  f0a7844:	44812000 */ 	mtc1	$at,$f4
/*  f0a7848:	c612023c */ 	lwc1	$f18,0x23c($s0)
/*  f0a784c:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f0a7850:	3c014f00 */ 	lui	$at,0x4f00
/*  f0a7854:	46049182 */ 	mul.s	$f6,$f18,$f4
/*  f0a7858:	3484007f */ 	ori	$a0,$a0,0x7f
/*  f0a785c:	4458f800 */ 	cfc1	$t8,$31
/*  f0a7860:	44c6f800 */ 	ctc1	$a2,$31
/*  f0a7864:	00000000 */ 	nop
/*  f0a7868:	46003224 */ 	cvt.w.s	$f8,$f6
/*  f0a786c:	4446f800 */ 	cfc1	$a2,$31
/*  f0a7870:	00000000 */ 	nop
/*  f0a7874:	30c60078 */ 	andi	$a2,$a2,0x78
/*  f0a7878:	50c00013 */ 	beqzl	$a2,.L0f0a78c8
/*  f0a787c:	44064000 */ 	mfc1	$a2,$f8
/*  f0a7880:	44814000 */ 	mtc1	$at,$f8
/*  f0a7884:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f0a7888:	46083201 */ 	sub.s	$f8,$f6,$f8
/*  f0a788c:	44c6f800 */ 	ctc1	$a2,$31
/*  f0a7890:	00000000 */ 	nop
/*  f0a7894:	46004224 */ 	cvt.w.s	$f8,$f8
/*  f0a7898:	4446f800 */ 	cfc1	$a2,$31
/*  f0a789c:	00000000 */ 	nop
/*  f0a78a0:	30c60078 */ 	andi	$a2,$a2,0x78
/*  f0a78a4:	14c00005 */ 	bnez	$a2,.L0f0a78bc
/*  f0a78a8:	00000000 */ 	nop
/*  f0a78ac:	44064000 */ 	mfc1	$a2,$f8
/*  f0a78b0:	3c018000 */ 	lui	$at,0x8000
/*  f0a78b4:	10000007 */ 	b	.L0f0a78d4
/*  f0a78b8:	00c13025 */ 	or	$a2,$a2,$at
.L0f0a78bc:
/*  f0a78bc:	10000005 */ 	b	.L0f0a78d4
/*  f0a78c0:	2406ffff */ 	addiu	$a2,$zero,-1
/*  f0a78c4:	44064000 */ 	mfc1	$a2,$f8
.L0f0a78c8:
/*  f0a78c8:	00000000 */ 	nop
/*  f0a78cc:	04c0fffb */ 	bltz	$a2,.L0f0a78bc
/*  f0a78d0:	00000000 */ 	nop
.L0f0a78d4:
/*  f0a78d4:	44d8f800 */ 	ctc1	$t8,$31
/*  f0a78d8:	0fc01a40 */ 	jal	colourBlend
/*  f0a78dc:	03202825 */ 	or	$a1,$t9,$zero
/*  f0a78e0:	afa20140 */ 	sw	$v0,0x140($sp)
.L0f0a78e4:
/*  f0a78e4:	8e6f00bc */ 	lw	$t7,0xbc($s3)
.L0f0a78e8:
/*  f0a78e8:	0fc08af9 */ 	jal	chrGetCloakAlpha
/*  f0a78ec:	8de40004 */ 	lw	$a0,0x4($t7)
/*  f0a78f0:	284100ff */ 	slti	$at,$v0,0xff
/*  f0a78f4:	1020000f */ 	beqz	$at,.L0f0a7934
/*  f0a78f8:	240c0001 */ 	addiu	$t4,$zero,0x1
/*  f0a78fc:	44825000 */ 	mtc1	$v0,$f10
/*  f0a7900:	3c017f1b */ 	lui	$at,%hi(var7f1aca90)
/*  f0a7904:	c432ca90 */ 	lwc1	$f18,%lo(var7f1aca90)($at)
/*  f0a7908:	46805420 */ 	cvt.s.w	$f16,$f10
/*  f0a790c:	8fa40140 */ 	lw	$a0,0x140($sp)
/*  f0a7910:	240e0005 */ 	addiu	$t6,$zero,0x5
/*  f0a7914:	afae013c */ 	sw	$t6,0x13c($sp)
/*  f0a7918:	afa40144 */ 	sw	$a0,0x144($sp)
/*  f0a791c:	46128102 */ 	mul.s	$f4,$f16,$f18
/*  f0a7920:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a7924:	44033000 */ 	mfc1	$v1,$f6
/*  f0a7928:	00000000 */ 	nop
/*  f0a792c:	24750041 */ 	addiu	$s5,$v1,0x41
/*  f0a7930:	afb50140 */ 	sw	$s5,0x140($sp)
.L0f0a7934:
/*  f0a7934:	0c0059d8 */ 	jal	mtx00016760
/*  f0a7938:	afac0110 */ 	sw	$t4,0x110($sp)
/*  f0a793c:	8e020218 */ 	lw	$v0,0x218($s0)
/*  f0a7940:	50400017 */ 	beqzl	$v0,.L0f0a79a0
/*  f0a7944:	8fa400ec */ 	lw	$a0,0xec($sp)
/*  f0a7948:	8c460018 */ 	lw	$a2,0x18($v0)
/*  f0a794c:	afa00094 */ 	sw	$zero,0x94($sp)
/*  f0a7950:	50c00013 */ 	beqzl	$a2,.L0f0a79a0
/*  f0a7954:	8fa400ec */ 	lw	$a0,0xec($sp)
/*  f0a7958:	8cc80008 */ 	lw	$t0,0x8($a2)
/*  f0a795c:	240a0001 */ 	addiu	$t2,$zero,0x1
/*  f0a7960:	27a4010c */ 	addiu	$a0,$sp,0x10c
/*  f0a7964:	1100000d */ 	beqz	$t0,.L0f0a799c
/*  f0a7968:	00c02825 */ 	or	$a1,$a2,$zero
/*  f0a796c:	afaa0094 */ 	sw	$t2,0x94($sp)
/*  f0a7970:	0c0087bd */ 	jal	modelRender
/*  f0a7974:	afa60098 */ 	sw	$a2,0x98($sp)
/*  f0a7978:	8fa60098 */ 	lw	$a2,0x98($sp)
/*  f0a797c:	8ccb0008 */ 	lw	$t3,0x8($a2)
/*  f0a7980:	8cc4000c */ 	lw	$a0,0xc($a2)
/*  f0a7984:	0fc30cfc */ 	jal	mtxF2LBulk
/*  f0a7988:	8565000e */ 	lh	$a1,0xe($t3)
/*  f0a798c:	8e0d021c */ 	lw	$t5,0x21c($s0)
/*  f0a7990:	51a00003 */ 	beqzl	$t5,.L0f0a79a0
/*  f0a7994:	8fa400ec */ 	lw	$a0,0xec($sp)
/*  f0a7998:	ae000218 */ 	sw	$zero,0x218($s0)
.L0f0a799c:
/*  f0a799c:	8fa400ec */ 	lw	$a0,0xec($sp)
.L0f0a79a0:
/*  f0a79a0:	0fc2c5f0 */ 	jal	weaponHasFlag
/*  f0a79a4:	24050020 */ 	addiu	$a1,$zero,0x20
/*  f0a79a8:	1040000e */ 	beqz	$v0,.L0f0a79e4
/*  f0a79ac:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a79b0:	8fb80118 */ 	lw	$t8,0x118($sp)
/*  f0a79b4:	3c0fb600 */ 	lui	$t7,0xb600
/*  f0a79b8:	24093000 */ 	addiu	$t1,$zero,0x3000
/*  f0a79bc:	27190008 */ 	addiu	$t9,$t8,0x8
/*  f0a79c0:	afb90118 */ 	sw	$t9,0x118($sp)
/*  f0a79c4:	af090004 */ 	sw	$t1,0x4($t8)
/*  f0a79c8:	16800004 */ 	bnez	$s4,.L0f0a79dc
/*  f0a79cc:	af0f0000 */ 	sw	$t7,0x0($t8)
/*  f0a79d0:	240e0003 */ 	addiu	$t6,$zero,0x3
/*  f0a79d4:	10000003 */ 	b	.L0f0a79e4
/*  f0a79d8:	afae0148 */ 	sw	$t6,0x148($sp)
.L0f0a79dc:
/*  f0a79dc:	240c0002 */ 	addiu	$t4,$zero,0x2
/*  f0a79e0:	afac0148 */ 	sw	$t4,0x148($sp)
.L0f0a79e4:
/*  f0a79e4:	8e28006c */ 	lw	$t0,0x6c($s1)
/*  f0a79e8:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a79ec:	00002025 */ 	or	$a0,$zero,$zero
/*  f0a79f0:	11000003 */ 	beqz	$t0,.L0f0a7a00
/*  f0a79f4:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a79f8:	10000001 */ 	b	.L0f0a7a00
/*  f0a79fc:	24050001 */ 	addiu	$a1,$zero,0x1
.L0f0a7a00:
/*  f0a7a00:	8e2a0068 */ 	lw	$t2,0x68($s1)
/*  f0a7a04:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a7a08:	11400003 */ 	beqz	$t2,.L0f0a7a18
/*  f0a7a0c:	00000000 */ 	nop
/*  f0a7a10:	10000001 */ 	b	.L0f0a7a18
/*  f0a7a14:	24040001 */ 	addiu	$a0,$zero,0x1
.L0f0a7a18:
/*  f0a7a18:	8e2b0064 */ 	lw	$t3,0x64($s1)
/*  f0a7a1c:	11600003 */ 	beqz	$t3,.L0f0a7a2c
/*  f0a7a20:	00000000 */ 	nop
/*  f0a7a24:	10000001 */ 	b	.L0f0a7a2c
/*  f0a7a28:	24020001 */ 	addiu	$v0,$zero,0x1
.L0f0a7a2c:
/*  f0a7a2c:	8e2d0070 */ 	lw	$t5,0x70($s1)
/*  f0a7a30:	11a00003 */ 	beqz	$t5,.L0f0a7a40
/*  f0a7a34:	00000000 */ 	nop
/*  f0a7a38:	10000001 */ 	b	.L0f0a7a40
/*  f0a7a3c:	24030001 */ 	addiu	$v1,$zero,0x1
.L0f0a7a40:
/*  f0a7a40:	0062c021 */ 	addu	$t8,$v1,$v0
/*  f0a7a44:	0304c821 */ 	addu	$t9,$t8,$a0
/*  f0a7a48:	03257821 */ 	addu	$t7,$t9,$a1
/*  f0a7a4c:	15e10036 */ 	bne	$t7,$at,.L0f0a7b28
/*  f0a7a50:	24050041 */ 	addiu	$a1,$zero,0x41
/*  f0a7a54:	0c006a47 */ 	jal	modelGetPart
/*  f0a7a58:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a7a5c:	50400033 */ 	beqzl	$v0,.L0f0a7b2c
/*  f0a7a60:	27a4010c */ 	addiu	$a0,$sp,0x10c
/*  f0a7a64:	8c440004 */ 	lw	$a0,0x4($v0)
/*  f0a7a68:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a7a6c:	84890010 */ 	lh	$t1,0x10($a0)
/*  f0a7a70:	5920002e */ 	blezl	$t1,.L0f0a7b2c
/*  f0a7a74:	27a4010c */ 	addiu	$a0,$sp,0x10c
/*  f0a7a78:	8e2a0034 */ 	lw	$t2,0x34($s1)
.L0f0a7a7c:
/*  f0a7a7c:	8c8e000c */ 	lw	$t6,0xc($a0)
/*  f0a7a80:	00056080 */ 	sll	$t4,$a1,0x2
/*  f0a7a84:	01520019 */ 	multu	$t2,$s2
/*  f0a7a88:	01856023 */ 	subu	$t4,$t4,$a1
/*  f0a7a8c:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f0a7a90:	01cc1021 */ 	addu	$v0,$t6,$t4
/*  f0a7a94:	8448000a */ 	lh	$t0,0xa($v0)
/*  f0a7a98:	0005c880 */ 	sll	$t9,$a1,0x2
/*  f0a7a9c:	0325c823 */ 	subu	$t9,$t9,$a1
/*  f0a7aa0:	0019c880 */ 	sll	$t9,$t9,0x2
/*  f0a7aa4:	24420002 */ 	addiu	$v0,$v0,0x2
/*  f0a7aa8:	00005812 */ 	mflo	$t3
/*  f0a7aac:	010b6823 */ 	subu	$t5,$t0,$t3
/*  f0a7ab0:	a44d0008 */ 	sh	$t5,0x8($v0)
/*  f0a7ab4:	8c98000c */ 	lw	$t8,0xc($a0)
/*  f0a7ab8:	03197821 */ 	addu	$t7,$t8,$t9
/*  f0a7abc:	85e9000a */ 	lh	$t1,0xa($t7)
/*  f0a7ac0:	2921a000 */ 	slti	$at,$t1,-24576
/*  f0a7ac4:	50200014 */ 	beqzl	$at,.L0f0a7b18
/*  f0a7ac8:	84980010 */ 	lh	$t8,0x10($a0)
/*  f0a7acc:	848e0010 */ 	lh	$t6,0x10($a0)
/*  f0a7ad0:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a7ad4:	59c00010 */ 	blezl	$t6,.L0f0a7b18
/*  f0a7ad8:	84980010 */ 	lh	$t8,0x10($a0)
/*  f0a7adc:	8c8c000c */ 	lw	$t4,0xc($a0)
.L0f0a7ae0:
/*  f0a7ae0:	00035080 */ 	sll	$t2,$v1,0x2
/*  f0a7ae4:	01435023 */ 	subu	$t2,$t2,$v1
/*  f0a7ae8:	000a5080 */ 	sll	$t2,$t2,0x2
/*  f0a7aec:	018a1021 */ 	addu	$v0,$t4,$t2
/*  f0a7af0:	8448000a */ 	lh	$t0,0xa($v0)
/*  f0a7af4:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f0a7af8:	24420002 */ 	addiu	$v0,$v0,0x2
/*  f0a7afc:	250b2000 */ 	addiu	$t3,$t0,0x2000
/*  f0a7b00:	a44b0008 */ 	sh	$t3,0x8($v0)
/*  f0a7b04:	848d0010 */ 	lh	$t5,0x10($a0)
/*  f0a7b08:	006d082a */ 	slt	$at,$v1,$t5
/*  f0a7b0c:	5420fff4 */ 	bnezl	$at,.L0f0a7ae0
/*  f0a7b10:	8c8c000c */ 	lw	$t4,0xc($a0)
/*  f0a7b14:	84980010 */ 	lh	$t8,0x10($a0)
.L0f0a7b18:
/*  f0a7b18:	24a50001 */ 	addiu	$a1,$a1,0x1
/*  f0a7b1c:	00b8082a */ 	slt	$at,$a1,$t8
/*  f0a7b20:	5420ffd6 */ 	bnezl	$at,.L0f0a7a7c
/*  f0a7b24:	8e2a0034 */ 	lw	$t2,0x34($s1)
.L0f0a7b28:
/*  f0a7b28:	27a4010c */ 	addiu	$a0,$sp,0x10c
.L0f0a7b2c:
/*  f0a7b2c:	0c0087bd */ 	jal	modelRender
/*  f0a7b30:	8fa5003c */ 	lw	$a1,0x3c($sp)
/*  f0a7b34:	8e791594 */ 	lw	$t9,0x1594($s3)
/*  f0a7b38:	3c0f8007 */ 	lui	$t7,%hi(var800702dc)
/*  f0a7b3c:	53200013 */ 	beqzl	$t9,.L0f0a7b8c
/*  f0a7b40:	8fac0118 */ 	lw	$t4,0x118($sp)
/*  f0a7b44:	8def02dc */ 	lw	$t7,%lo(var800702dc)($t7)
/*  f0a7b48:	8fa90140 */ 	lw	$t1,0x140($sp)
/*  f0a7b4c:	51e0000f */ 	beqzl	$t7,.L0f0a7b8c
/*  f0a7b50:	8fac0118 */ 	lw	$t4,0x118($sp)
/*  f0a7b54:	afa9007c */ 	sw	$t1,0x7c($sp)
/*  f0a7b58:	8e0e0390 */ 	lw	$t6,0x390($s0)
/*  f0a7b5c:	26050534 */ 	addiu	$a1,$s0,0x534
/*  f0a7b60:	00a02025 */ 	or	$a0,$a1,$zero
/*  f0a7b64:	ae0e0540 */ 	sw	$t6,0x540($s0)
/*  f0a7b68:	0c007308 */ 	jal	modelUpdateRelations
/*  f0a7b6c:	afa50054 */ 	sw	$a1,0x54($sp)
/*  f0a7b70:	8fa50054 */ 	lw	$a1,0x54($sp)
/*  f0a7b74:	afb50140 */ 	sw	$s5,0x140($sp)
/*  f0a7b78:	0c0087bd */ 	jal	modelRender
/*  f0a7b7c:	27a4010c */ 	addiu	$a0,$sp,0x10c
/*  f0a7b80:	8fa4007c */ 	lw	$a0,0x7c($sp)
/*  f0a7b84:	afa40140 */ 	sw	$a0,0x140($sp)
/*  f0a7b88:	8fac0118 */ 	lw	$t4,0x118($sp)
.L0f0a7b8c:
/*  f0a7b8c:	8fa400ec */ 	lw	$a0,0xec($sp)
/*  f0a7b90:	24050020 */ 	addiu	$a1,$zero,0x20
/*  f0a7b94:	0fc2c5f0 */ 	jal	weaponHasFlag
/*  f0a7b98:	afac014c */ 	sw	$t4,0x14c($sp)
/*  f0a7b9c:	10400007 */ 	beqz	$v0,.L0f0a7bbc
/*  f0a7ba0:	8faa014c */ 	lw	$t2,0x14c($sp)
/*  f0a7ba4:	25480008 */ 	addiu	$t0,$t2,0x8
/*  f0a7ba8:	afa8014c */ 	sw	$t0,0x14c($sp)
/*  f0a7bac:	3c0bb600 */ 	lui	$t3,0xb600
/*  f0a7bb0:	240d3000 */ 	addiu	$t5,$zero,0x3000
/*  f0a7bb4:	ad4d0004 */ 	sw	$t5,0x4($t2)
/*  f0a7bb8:	ad4b0000 */ 	sw	$t3,0x0($t2)
.L0f0a7bbc:
/*  f0a7bbc:	8e18038c */ 	lw	$t8,0x38c($s0)
/*  f0a7bc0:	8e040390 */ 	lw	$a0,0x390($s0)
/*  f0a7bc4:	0fc30cfc */ 	jal	mtxF2LBulk
/*  f0a7bc8:	8705000e */ 	lh	$a1,0xe($t8)
/*  f0a7bcc:	0c0059e1 */ 	jal	mtx00016784
/*  f0a7bd0:	00000000 */ 	nop
/*  f0a7bd4:	8fb9014c */ 	lw	$t9,0x14c($sp)
/*  f0a7bd8:	3c09bc00 */ 	lui	$t1,0xbc00
/*  f0a7bdc:	3529000e */ 	ori	$t1,$t1,0xe
/*  f0a7be0:	272f0008 */ 	addiu	$t7,$t9,0x8
/*  f0a7be4:	afaf014c */ 	sw	$t7,0x14c($sp)
/*  f0a7be8:	af290000 */ 	sw	$t1,0x0($t9)
/*  f0a7bec:	0c002adb */ 	jal	viGetPerspScale
/*  f0a7bf0:	afb90074 */ 	sw	$t9,0x74($sp)
/*  f0a7bf4:	8fa30074 */ 	lw	$v1,0x74($sp)
/*  f0a7bf8:	ac620004 */ 	sw	$v0,0x4($v1)
.L0f0a7bfc:
/*  f0a7bfc:	26940001 */ 	addiu	$s4,$s4,0x1
/*  f0a7c00:	24010002 */ 	addiu	$at,$zero,0x2
/*  f0a7c04:	1681fe04 */ 	bne	$s4,$at,.L0f0a7418
/*  f0a7c08:	261007a4 */ 	addiu	$s0,$s0,0x7a4
/*  f0a7c0c:	afb500e4 */ 	sw	$s5,0xe4($sp)
/*  f0a7c10:	0fc2baf8 */ 	jal	casingsRender
/*  f0a7c14:	27a4014c */ 	addiu	$a0,$sp,0x14c
/*  f0a7c18:	0fc5d8a6 */ 	jal	zbufSwap
/*  f0a7c1c:	00000000 */ 	nop
/*  f0a7c20:	0fc5d8ab */ 	jal	zbufConfigureRdp
/*  f0a7c24:	8fa4014c */ 	lw	$a0,0x14c($sp)
/*  f0a7c28:	afa2014c */ 	sw	$v0,0x14c($sp)
/*  f0a7c2c:	0c002c74 */ 	jal	vi0000b1d0
/*  f0a7c30:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a7c34:	244e0008 */ 	addiu	$t6,$v0,0x8
/*  f0a7c38:	afae014c */ 	sw	$t6,0x14c($sp)
/*  f0a7c3c:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7c40:	00408825 */ 	or	$s1,$v0,$zero
/*  f0a7c44:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a7c48:	00106403 */ 	sra	$t4,$s0,0x10
/*  f0a7c4c:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7c50:	01808025 */ 	or	$s0,$t4,$zero
/*  f0a7c54:	44824000 */ 	mtc1	$v0,$f8
/*  f0a7c58:	44902000 */ 	mtc1	$s0,$f4
/*  f0a7c5c:	3c014080 */ 	lui	$at,0x4080
/*  f0a7c60:	468042a0 */ 	cvt.s.w	$f10,$f8
/*  f0a7c64:	44810000 */ 	mtc1	$at,$f0
/*  f0a7c68:	3c01ed00 */ 	lui	$at,0xed00
/*  f0a7c6c:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a7c70:	46005402 */ 	mul.s	$f16,$f10,$f0
/*  f0a7c74:	00000000 */ 	nop
/*  f0a7c78:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a7c7c:	4600848d */ 	trunc.w.s	$f18,$f16
/*  f0a7c80:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a7c84:	44089000 */ 	mfc1	$t0,$f18
/*  f0a7c88:	44195000 */ 	mfc1	$t9,$f10
/*  f0a7c8c:	310b0fff */ 	andi	$t3,$t0,0xfff
/*  f0a7c90:	01616825 */ 	or	$t5,$t3,$at
/*  f0a7c94:	332f0fff */ 	andi	$t7,$t9,0xfff
/*  f0a7c98:	000f4b00 */ 	sll	$t1,$t7,0xc
/*  f0a7c9c:	01a97025 */ 	or	$t6,$t5,$t1
/*  f0a7ca0:	0c002f22 */ 	jal	viGetViewWidth
/*  f0a7ca4:	ae2e0000 */ 	sw	$t6,0x0($s1)
/*  f0a7ca8:	00029400 */ 	sll	$s2,$v0,0x10
/*  f0a7cac:	00126403 */ 	sra	$t4,$s2,0x10
/*  f0a7cb0:	0c002f40 */ 	jal	viGetViewLeft
/*  f0a7cb4:	01809025 */ 	or	$s2,$t4,$zero
/*  f0a7cb8:	0002a400 */ 	sll	$s4,$v0,0x10
/*  f0a7cbc:	00145403 */ 	sra	$t2,$s4,0x10
/*  f0a7cc0:	0c002f44 */ 	jal	viGetViewTop
/*  f0a7cc4:	0140a025 */ 	or	$s4,$t2,$zero
/*  f0a7cc8:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a7ccc:	00104403 */ 	sra	$t0,$s0,0x10
/*  f0a7cd0:	0c002f26 */ 	jal	viGetViewHeight
/*  f0a7cd4:	01008025 */ 	or	$s0,$t0,$zero
/*  f0a7cd8:	00505821 */ 	addu	$t3,$v0,$s0
/*  f0a7cdc:	448b8000 */ 	mtc1	$t3,$f16
/*  f0a7ce0:	02926821 */ 	addu	$t5,$s4,$s2
/*  f0a7ce4:	448d5000 */ 	mtc1	$t5,$f10
/*  f0a7ce8:	468084a0 */ 	cvt.s.w	$f18,$f16
/*  f0a7cec:	3c014080 */ 	lui	$at,0x4080
/*  f0a7cf0:	44812000 */ 	mtc1	$at,$f4
/*  f0a7cf4:	46805420 */ 	cvt.s.w	$f16,$f10
/*  f0a7cf8:	46049182 */ 	mul.s	$f6,$f18,$f4
/*  f0a7cfc:	44819000 */ 	mtc1	$at,$f18
/*  f0a7d00:	00000000 */ 	nop
/*  f0a7d04:	46128102 */ 	mul.s	$f4,$f16,$f18
/*  f0a7d08:	4600320d */ 	trunc.w.s	$f8,$f6
/*  f0a7d0c:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a7d10:	44194000 */ 	mfc1	$t9,$f8
/*  f0a7d14:	440e3000 */ 	mfc1	$t6,$f6
/*  f0a7d18:	332f0fff */ 	andi	$t7,$t9,0xfff
/*  f0a7d1c:	31cc0fff */ 	andi	$t4,$t6,0xfff
/*  f0a7d20:	000c5300 */ 	sll	$t2,$t4,0xc
/*  f0a7d24:	01ea4025 */ 	or	$t0,$t7,$t2
/*  f0a7d28:	ae280004 */ 	sw	$t0,0x4($s1)
/*  f0a7d2c:	8fb80150 */ 	lw	$t8,0x150($sp)
/*  f0a7d30:	8fab014c */ 	lw	$t3,0x14c($sp)
/*  f0a7d34:	af0b0000 */ 	sw	$t3,0x0($t8)
/*  f0a7d38:	8fbf0034 */ 	lw	$ra,0x34($sp)
.L0f0a7d3c:
/*  f0a7d3c:	8fb0001c */ 	lw	$s0,0x1c($sp)
/*  f0a7d40:	8fb10020 */ 	lw	$s1,0x20($sp)
/*  f0a7d44:	8fb20024 */ 	lw	$s2,0x24($sp)
/*  f0a7d48:	8fb30028 */ 	lw	$s3,0x28($sp)
/*  f0a7d4c:	8fb4002c */ 	lw	$s4,0x2c($sp)
/*  f0a7d50:	8fb50030 */ 	lw	$s5,0x30($sp)
/*  f0a7d54:	03e00008 */ 	jr	$ra
/*  f0a7d58:	27bd0150 */ 	addiu	$sp,$sp,0x150
);
#else
GLOBAL_ASM(
glabel bgunRender
.late_rodata
glabel var7f1aca8c
.word 0x3faaaaab
glabel var7f1aca90
.word 0x3f3ebebf
.text
/*  f0a4e84:	27bdfeb8 */ 	addiu	$sp,$sp,-328
/*  f0a4e88:	afbf0034 */ 	sw	$ra,0x34($sp)
/*  f0a4e8c:	afb50030 */ 	sw	$s5,0x30($sp)
/*  f0a4e90:	afb4002c */ 	sw	$s4,0x2c($sp)
/*  f0a4e94:	afb30028 */ 	sw	$s3,0x28($sp)
/*  f0a4e98:	afb20024 */ 	sw	$s2,0x24($sp)
/*  f0a4e9c:	afb10020 */ 	sw	$s1,0x20($sp)
/*  f0a4ea0:	afb0001c */ 	sw	$s0,0x1c($sp)
/*  f0a4ea4:	afa40148 */ 	sw	$a0,0x148($sp)
/*  f0a4ea8:	8c8f0000 */ 	lw	$t7,0x0($a0)
/*  f0a4eac:	3c198007 */ 	lui	$t9,%hi(var8007029c)
/*  f0a4eb0:	3c11800a */ 	lui	$s1,%hi(g_Vars)
/*  f0a4eb4:	2739295c */ 	addiu	$t9,$t9,%lo(var8007029c)
/*  f0a4eb8:	2631e6c0 */ 	addiu	$s1,$s1,%lo(g_Vars)
/*  f0a4ebc:	272a003c */ 	addiu	$t2,$t9,0x3c
/*  f0a4ec0:	27b80104 */ 	addiu	$t8,$sp,0x104
/*  f0a4ec4:	afaf0144 */ 	sw	$t7,0x144($sp)
.NB0f0a4ec8:
/*  f0a4ec8:	8f210000 */ 	lw	$at,0x0($t9)
/*  f0a4ecc:	2739000c */ 	addiu	$t9,$t9,0xc
/*  f0a4ed0:	2718000c */ 	addiu	$t8,$t8,0xc
/*  f0a4ed4:	af01fff4 */ 	sw	$at,-0xc($t8)
/*  f0a4ed8:	8f21fff8 */ 	lw	$at,-0x8($t9)
/*  f0a4edc:	af01fff8 */ 	sw	$at,-0x8($t8)
/*  f0a4ee0:	8f21fffc */ 	lw	$at,-0x4($t9)
/*  f0a4ee4:	172afff8 */ 	bne	$t9,$t2,.NB0f0a4ec8
/*  f0a4ee8:	af01fffc */ 	sw	$at,-0x4($t8)
/*  f0a4eec:	8f210000 */ 	lw	$at,0x0($t9)
/*  f0a4ef0:	af010000 */ 	sw	$at,0x0($t8)
/*  f0a4ef4:	8e330284 */ 	lw	$s3,0x284($s1)
/*  f0a4ef8:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a4efc:	966b0010 */ 	lhu	$t3,0x10($s3)
/*  f0a4f00:	1561000d */ 	bne	$t3,$at,.NB0f0a4f38
/*  f0a4f04:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a4f08:	24040f48 */ 	addiu	$a0,$zero,0xf48
/*  f0a4f0c:	8e2c0284 */ 	lw	$t4,0x284($s1)
.NB0f0a4f10:
/*  f0a4f10:	01821821 */ 	addu	$v1,$t4,$v0
/*  f0a4f14:	8c6d0854 */ 	lw	$t5,0x854($v1)
/*  f0a4f18:	244207a4 */ 	addiu	$v0,$v0,0x7a4
/*  f0a4f1c:	11a00002 */ 	beqz	$t5,.NB0f0a4f28
/*  f0a4f20:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a4f24:	ac600850 */ 	sw	$zero,0x850($v1)
.NB0f0a4f28:
/*  f0a4f28:	5444fff9 */ 	bnel	$v0,$a0,.NB0f0a4f10
/*  f0a4f2c:	8e2c0284 */ 	lw	$t4,0x284($s1)
/*  f0a4f30:	100002cd */ 	beqz	$zero,.NB0f0a5a68
/*  f0a4f34:	8fbf0034 */ 	lw	$ra,0x34($sp)
.NB0f0a4f38:
/*  f0a4f38:	0fc5c4d5 */ 	jal	zbufDrawArtifactsOffscreen
/*  f0a4f3c:	8fa40144 */ 	lw	$a0,0x144($sp)
/*  f0a4f40:	afa20144 */ 	sw	$v0,0x144($sp)
/*  f0a4f44:	0c002d00 */ 	jal	viPrepareZbuf
/*  f0a4f48:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a4f4c:	afa20144 */ 	sw	$v0,0x144($sp)
/*  f0a4f50:	0c002cd4 */ 	jal	vi0000b1d0
/*  f0a4f54:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a4f58:	244e0008 */ 	addiu	$t6,$v0,0x8
/*  f0a4f5c:	afae0144 */ 	sw	$t6,0x144($sp)
/*  f0a4f60:	0c002fb5 */ 	jal	viGetViewLeft
/*  f0a4f64:	0040a825 */ 	or	$s5,$v0,$zero
/*  f0a4f68:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a4f6c:	00107c03 */ 	sra	$t7,$s0,0x10
/*  f0a4f70:	0c002fb9 */ 	jal	viGetViewTop
/*  f0a4f74:	01e08025 */ 	or	$s0,$t7,$zero
/*  f0a4f78:	44822000 */ 	mtc1	$v0,$f4
/*  f0a4f7c:	44908000 */ 	mtc1	$s0,$f16
/*  f0a4f80:	3c014080 */ 	lui	$at,0x4080
/*  f0a4f84:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a4f88:	44810000 */ 	mtc1	$at,$f0
/*  f0a4f8c:	3c01ed00 */ 	lui	$at,0xed00
/*  f0a4f90:	468084a0 */ 	cvt.s.w	$f18,$f16
/*  f0a4f94:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a4f98:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a4f9c:	46009102 */ 	mul.s	$f4,$f18,$f0
/*  f0a4fa0:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a4fa4:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a4fa8:	44085000 */ 	mfc1	$t0,$f10
/*  f0a4fac:	440b3000 */ 	mfc1	$t3,$f6
/*  f0a4fb0:	310a0fff */ 	andi	$t2,$t0,0xfff
/*  f0a4fb4:	0141c825 */ 	or	$t9,$t2,$at
/*  f0a4fb8:	316c0fff */ 	andi	$t4,$t3,0xfff
/*  f0a4fbc:	000c6b00 */ 	sll	$t5,$t4,0xc
/*  f0a4fc0:	032d7025 */ 	or	$t6,$t9,$t5
/*  f0a4fc4:	0c002f97 */ 	jal	viGetViewWidth
/*  f0a4fc8:	aeae0000 */ 	sw	$t6,0x0($s5)
/*  f0a4fcc:	00029400 */ 	sll	$s2,$v0,0x10
/*  f0a4fd0:	00127c03 */ 	sra	$t7,$s2,0x10
/*  f0a4fd4:	0c002fb5 */ 	jal	viGetViewLeft
/*  f0a4fd8:	01e09025 */ 	or	$s2,$t7,$zero
/*  f0a4fdc:	0002a400 */ 	sll	$s4,$v0,0x10
/*  f0a4fe0:	00144c03 */ 	sra	$t1,$s4,0x10
/*  f0a4fe4:	0c002fb9 */ 	jal	viGetViewTop
/*  f0a4fe8:	0120a025 */ 	or	$s4,$t1,$zero
/*  f0a4fec:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a4ff0:	00104403 */ 	sra	$t0,$s0,0x10
/*  f0a4ff4:	0c002f9b */ 	jal	viGetViewHeight
/*  f0a4ff8:	01008025 */ 	or	$s0,$t0,$zero
/*  f0a4ffc:	00505021 */ 	addu	$t2,$v0,$s0
/*  f0a5000:	448a4000 */ 	mtc1	$t2,$f8
/*  f0a5004:	0292c821 */ 	addu	$t9,$s4,$s2
/*  f0a5008:	44992000 */ 	mtc1	$t9,$f4
/*  f0a500c:	468042a0 */ 	cvt.s.w	$f10,$f8
/*  f0a5010:	3c014080 */ 	lui	$at,0x4080
/*  f0a5014:	44810000 */ 	mtc1	$at,$f0
/*  f0a5018:	3c053fc0 */ 	lui	$a1,0x3fc0
/*  f0a501c:	3c06447a */ 	lui	$a2,0x447a
/*  f0a5020:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a5024:	46005402 */ 	mul.s	$f16,$f10,$f0
/*  f0a5028:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a502c:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a5030:	4600848d */ 	trunc.w.s	$f18,$f16
/*  f0a5034:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a5038:	440b9000 */ 	mfc1	$t3,$f18
/*  f0a503c:	440e5000 */ 	mfc1	$t6,$f10
/*  f0a5040:	316c0fff */ 	andi	$t4,$t3,0xfff
/*  f0a5044:	31cf0fff */ 	andi	$t7,$t6,0xfff
/*  f0a5048:	000f4b00 */ 	sll	$t1,$t7,0xc
/*  f0a504c:	01894025 */ 	or	$t0,$t4,$t1
/*  f0a5050:	aea80004 */ 	sw	$t0,0x4($s5)
/*  f0a5054:	0c002b89 */ 	jal	vi0000aca4
/*  f0a5058:	8fa40144 */ 	lw	$a0,0x144($sp)
/*  f0a505c:	8e2a0284 */ 	lw	$t2,0x284($s1)
/*  f0a5060:	afa20144 */ 	sw	$v0,0x144($sp)
/*  f0a5064:	91581bfc */ 	lbu	$t8,0x1bfc($t2)
/*  f0a5068:	53000016 */ 	beqzl	$t8,.NB0f0a50c4
/*  f0a506c:	8e2b006c */ 	lw	$t3,0x6c($s1)
/*  f0a5070:	0fc53582 */ 	jal	optionsGetScreenRatio
/*  f0a5074:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5078:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a507c:	14410008 */ 	bne	$v0,$at,.NB0f0a50a0
/*  f0a5080:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5084:	0fc2ebf0 */ 	jal	player0f0bd358
/*  f0a5088:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a508c:	3c017f1a */ 	lui	$at,%hi(var7f1aca8c)
/*  f0a5090:	c4306dd4 */ 	lwc1	$f16,%lo(var7f1aca8c)($at)
/*  f0a5094:	46100082 */ 	mul.s	$f2,$f0,$f16
/*  f0a5098:	10000005 */ 	beqz	$zero,.NB0f0a50b0
/*  f0a509c:	44061000 */ 	mfc1	$a2,$f2
.NB0f0a50a0:
/*  f0a50a0:	0fc2ebf0 */ 	jal	player0f0bd358
/*  f0a50a4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a50a8:	46000086 */ 	mov.s	$f2,$f0
/*  f0a50ac:	44061000 */ 	mfc1	$a2,$f2
.NB0f0a50b0:
/*  f0a50b0:	8fa40144 */ 	lw	$a0,0x144($sp)
/*  f0a50b4:	0c002c9a */ 	jal	vi0000b0e8
/*  f0a50b8:	3c054270 */ 	lui	$a1,0x4270
/*  f0a50bc:	afa20144 */ 	sw	$v0,0x144($sp)
/*  f0a50c0:	8e2b006c */ 	lw	$t3,0x6c($s1)
.NB0f0a50c4:
/*  f0a50c4:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a50c8:	51600004 */ 	beqzl	$t3,.NB0f0a50dc
/*  f0a50cc:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a50d0:	10000002 */ 	beqz	$zero,.NB0f0a50dc
/*  f0a50d4:	24050001 */ 	addiu	$a1,$zero,0x1
/*  f0a50d8:	00002825 */ 	or	$a1,$zero,$zero
.NB0f0a50dc:
/*  f0a50dc:	8e390068 */ 	lw	$t9,0x68($s1)
/*  f0a50e0:	53200004 */ 	beqzl	$t9,.NB0f0a50f4
/*  f0a50e4:	00002025 */ 	or	$a0,$zero,$zero
/*  f0a50e8:	10000002 */ 	beqz	$zero,.NB0f0a50f4
/*  f0a50ec:	24040001 */ 	addiu	$a0,$zero,0x1
/*  f0a50f0:	00002025 */ 	or	$a0,$zero,$zero
.NB0f0a50f4:
/*  f0a50f4:	8e2d0064 */ 	lw	$t5,0x64($s1)
/*  f0a50f8:	51a00004 */ 	beqzl	$t5,.NB0f0a510c
/*  f0a50fc:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a5100:	10000002 */ 	beqz	$zero,.NB0f0a510c
/*  f0a5104:	24020001 */ 	addiu	$v0,$zero,0x1
/*  f0a5108:	00001025 */ 	or	$v0,$zero,$zero
.NB0f0a510c:
/*  f0a510c:	8e2e0070 */ 	lw	$t6,0x70($s1)
/*  f0a5110:	51c00004 */ 	beqzl	$t6,.NB0f0a5124
/*  f0a5114:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a5118:	10000002 */ 	beqz	$zero,.NB0f0a5124
/*  f0a511c:	24030001 */ 	addiu	$v1,$zero,0x1
/*  f0a5120:	00001825 */ 	or	$v1,$zero,$zero
.NB0f0a5124:
/*  f0a5124:	00627821 */ 	addu	$t7,$v1,$v0
/*  f0a5128:	01e46021 */ 	addu	$t4,$t7,$a0
/*  f0a512c:	01854821 */ 	addu	$t1,$t4,$a1
/*  f0a5130:	15210008 */ 	bne	$t1,$at,.NB0f0a5154
/*  f0a5134:	3c088009 */ 	lui	$t0,%hi(g_Is4Mb)
/*  f0a5138:	910830e0 */ 	lbu	$t0,%lo(g_Is4Mb)($t0)
/*  f0a513c:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a5140:	51010005 */ 	beql	$t0,$at,.NB0f0a5158
/*  f0a5144:	0000a025 */ 	or	$s4,$zero,$zero
/*  f0a5148:	0fc2b5eb */ 	jal	lasersightRenderBeam
/*  f0a514c:	8fa40144 */ 	lw	$a0,0x144($sp)
/*  f0a5150:	afa20144 */ 	sw	$v0,0x144($sp)
.NB0f0a5154:
/*  f0a5154:	0000a025 */ 	or	$s4,$zero,$zero
.NB0f0a5158:
/*  f0a5158:	26700638 */ 	addiu	$s0,$s3,0x638
/*  f0a515c:	8fb500dc */ 	lw	$s5,0xdc($sp)
/*  f0a5160:	24120019 */ 	addiu	$s2,$zero,0x19
.NB0f0a5164:
/*  f0a5164:	0fc27de9 */ 	jal	bgunGetWeaponNum2
/*  f0a5168:	02802025 */ 	or	$a0,$s4,$zero
/*  f0a516c:	afa200e4 */ 	sw	$v0,0xe4($sp)
/*  f0a5170:	820a0007 */ 	lb	$t2,0x7($s0)
/*  f0a5174:	8fa40144 */ 	lw	$a0,0x144($sp)
/*  f0a5178:	260501dc */ 	addiu	$a1,$s0,0x1dc
/*  f0a517c:	114001ea */ 	beqz	$t2,.NB0f0a5928
/*  f0a5180:	00003025 */ 	or	$a2,$zero,$zero
/*  f0a5184:	26180384 */ 	addiu	$t8,$s0,0x384
/*  f0a5188:	afb80038 */ 	sw	$t8,0x38($sp)
/*  f0a518c:	0fc2aa3c */ 	jal	beamRender
/*  f0a5190:	00003825 */ 	or	$a3,$zero,$zero
/*  f0a5194:	afa20144 */ 	sw	$v0,0x144($sp)
/*  f0a5198:	92040000 */ 	lbu	$a0,0x0($s0)
/*  f0a519c:	0fc2bd48 */ 	jal	weaponHasFlag
/*  f0a51a0:	34058000 */ 	dli	$a1,0x8000
/*  f0a51a4:	10400030 */ 	beqz	$v0,.NB0f0a5268
/*  f0a51a8:	8fab0144 */ 	lw	$t3,0x144($sp)
/*  f0a51ac:	25790008 */ 	addiu	$t9,$t3,0x8
/*  f0a51b0:	afb90144 */ 	sw	$t9,0x144($sp)
/*  f0a51b4:	3c0dbc00 */ 	lui	$t5,0xbc00
/*  f0a51b8:	3c0e8000 */ 	lui	$t6,0x8000
/*  f0a51bc:	35ce0040 */ 	ori	$t6,$t6,0x40
/*  f0a51c0:	35ad0002 */ 	ori	$t5,$t5,0x2
/*  f0a51c4:	ad6d0000 */ 	sw	$t5,0x0($t3)
/*  f0a51c8:	ad6e0004 */ 	sw	$t6,0x4($t3)
/*  f0a51cc:	8faf0144 */ 	lw	$t7,0x144($sp)
/*  f0a51d0:	3c090386 */ 	lui	$t1,0x386
/*  f0a51d4:	3c088007 */ 	lui	$t0,%hi(var80070090+0x8)
/*  f0a51d8:	25ec0008 */ 	addiu	$t4,$t7,0x8
/*  f0a51dc:	afac0144 */ 	sw	$t4,0x144($sp)
/*  f0a51e0:	25082758 */ 	addiu	$t0,$t0,%lo(var80070090+0x8)
/*  f0a51e4:	35290010 */ 	ori	$t1,$t1,0x10
/*  f0a51e8:	ade90000 */ 	sw	$t1,0x0($t7)
/*  f0a51ec:	ade80004 */ 	sw	$t0,0x4($t7)
/*  f0a51f0:	8faa0144 */ 	lw	$t2,0x144($sp)
/*  f0a51f4:	3c0b0388 */ 	lui	$t3,0x388
/*  f0a51f8:	3c198007 */ 	lui	$t9,%hi(var80070090)
/*  f0a51fc:	25580008 */ 	addiu	$t8,$t2,0x8
/*  f0a5200:	afb80144 */ 	sw	$t8,0x144($sp)
/*  f0a5204:	27392750 */ 	addiu	$t9,$t9,%lo(var80070090)
/*  f0a5208:	356b0010 */ 	ori	$t3,$t3,0x10
/*  f0a520c:	ad4b0000 */ 	sw	$t3,0x0($t2)
/*  f0a5210:	ad590004 */ 	sw	$t9,0x4($t2)
/*  f0a5214:	8fad0144 */ 	lw	$t5,0x144($sp)
/*  f0a5218:	3c0f0384 */ 	lui	$t7,0x384
/*  f0a521c:	35ef0010 */ 	ori	$t7,$t7,0x10
/*  f0a5220:	25ae0008 */ 	addiu	$t6,$t5,0x8
/*  f0a5224:	afae0144 */ 	sw	$t6,0x144($sp)
/*  f0a5228:	adaf0000 */ 	sw	$t7,0x0($t5)
/*  f0a522c:	0fc2cd42 */ 	jal	camGetLookAt
/*  f0a5230:	afad00cc */ 	sw	$t5,0xcc($sp)
/*  f0a5234:	8fa500cc */ 	lw	$a1,0xcc($sp)
/*  f0a5238:	3c080382 */ 	lui	$t0,0x382
/*  f0a523c:	35080010 */ 	ori	$t0,$t0,0x10
/*  f0a5240:	aca20004 */ 	sw	$v0,0x4($a1)
/*  f0a5244:	8fac0144 */ 	lw	$t4,0x144($sp)
/*  f0a5248:	25890008 */ 	addiu	$t1,$t4,0x8
/*  f0a524c:	afa90144 */ 	sw	$t1,0x144($sp)
/*  f0a5250:	ad880000 */ 	sw	$t0,0x0($t4)
/*  f0a5254:	0fc2cd42 */ 	jal	camGetLookAt
/*  f0a5258:	afac00c8 */ 	sw	$t4,0xc8($sp)
/*  f0a525c:	8fa300c8 */ 	lw	$v1,0xc8($sp)
/*  f0a5260:	244a0010 */ 	addiu	$t2,$v0,0x10
/*  f0a5264:	ac6a0004 */ 	sw	$t2,0x4($v1)
.NB0f0a5268:
/*  f0a5268:	8fb80144 */ 	lw	$t8,0x144($sp)
/*  f0a526c:	3c19bc00 */ 	lui	$t9,0xbc00
/*  f0a5270:	3739000e */ 	ori	$t9,$t9,0xe
/*  f0a5274:	270b0008 */ 	addiu	$t3,$t8,0x8
/*  f0a5278:	afab0144 */ 	sw	$t3,0x144($sp)
/*  f0a527c:	3c014396 */ 	lui	$at,0x4396
/*  f0a5280:	44817000 */ 	mtc1	$at,$f14
/*  f0a5284:	44806000 */ 	mtc1	$zero,$f12
/*  f0a5288:	af190000 */ 	sw	$t9,0x0($t8)
/*  f0a528c:	0c005f57 */ 	jal	mtx00016dcc
/*  f0a5290:	afb800c4 */ 	sw	$t8,0xc4($sp)
/*  f0a5294:	8fa300c4 */ 	lw	$v1,0xc4($sp)
/*  f0a5298:	24050010 */ 	addiu	$a1,$zero,0x10
/*  f0a529c:	ac620004 */ 	sw	$v0,0x4($v1)
/*  f0a52a0:	0c006ea3 */ 	jal	modelGetPart
/*  f0a52a4:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a52a8:	10400014 */ 	beqz	$v0,.NB0f0a52fc
/*  f0a52ac:	afa200e0 */ 	sw	$v0,0xe0($sp)
/*  f0a52b0:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a52b4:	0c006ea3 */ 	jal	modelGetPart
/*  f0a52b8:	24050011 */ 	addiu	$a1,$zero,0x11
/*  f0a52bc:	8fa40038 */ 	lw	$a0,0x38($sp)
/*  f0a52c0:	0c006bab */ 	jal	modelGetNodeRwData
/*  f0a52c4:	00402825 */ 	or	$a1,$v0,$zero
/*  f0a52c8:	10400003 */ 	beqz	$v0,.NB0f0a52d8
/*  f0a52cc:	3c06800a */ 	lui	$a2,%hi(var8009cf88)
/*  f0a52d0:	240d0001 */ 	addiu	$t5,$zero,0x1
/*  f0a52d4:	ac4d0000 */ 	sw	$t5,0x0($v0)
.NB0f0a52d8:
/*  f0a52d8:	240e0001 */ 	addiu	$t6,$zero,0x1
/*  f0a52dc:	afae0014 */ 	sw	$t6,0x14($sp)
/*  f0a52e0:	8fa40038 */ 	lw	$a0,0x38($sp)
/*  f0a52e4:	8fa500e0 */ 	lw	$a1,0xe0($sp)
/*  f0a52e8:	24c616b8 */ 	addiu	$a2,$a2,%lo(var8009cf88)
/*  f0a52ec:	8fa70144 */ 	lw	$a3,0x144($sp)
/*  f0a52f0:	0fc1f99c */ 	jal	tvscreenRender
/*  f0a52f4:	afa00010 */ 	sw	$zero,0x10($sp)
/*  f0a52f8:	afa20144 */ 	sw	$v0,0x144($sp)
.NB0f0a52fc:
/*  f0a52fc:	8faf0144 */ 	lw	$t7,0x144($sp)
/*  f0a5300:	8e250284 */ 	lw	$a1,0x284($s1)
/*  f0a5304:	240c0004 */ 	addiu	$t4,$zero,0x4
/*  f0a5308:	afac0134 */ 	sw	$t4,0x134($sp)
/*  f0a530c:	afaf0110 */ 	sw	$t7,0x110($sp)
/*  f0a5310:	8ca300d8 */ 	lw	$v1,0xd8($a1)
/*  f0a5314:	3c078007 */ 	lui	$a3,%hi(g_InCutscene)
/*  f0a5318:	14600013 */ 	bnez	$v1,.NB0f0a5368
/*  f0a531c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5320:	8ce72e24 */ 	lw	$a3,%lo(g_InCutscene)($a3)
/*  f0a5324:	14e00010 */ 	bnez	$a3,.NB0f0a5368
/*  f0a5328:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a532c:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a5330:	50400007 */ 	beqzl	$v0,.NB0f0a5350
/*  f0a5334:	8caa1c54 */ 	lw	$t2,0x1c54($a1)
/*  f0a5338:	1040000b */ 	beqz	$v0,.NB0f0a5368
/*  f0a533c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5340:	80490037 */ 	lb	$t1,0x37($v0)
/*  f0a5344:	15200008 */ 	bnez	$t1,.NB0f0a5368
/*  f0a5348:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a534c:	8caa1c54 */ 	lw	$t2,0x1c54($a1)
.NB0f0a5350:
/*  f0a5350:	8ca800c4 */ 	lw	$t0,0xc4($a1)
/*  f0a5354:	0140c027 */ 	nor	$t8,$t2,$zero
/*  f0a5358:	01185824 */ 	and	$t3,$t0,$t8
/*  f0a535c:	31790001 */ 	andi	$t9,$t3,0x1
/*  f0a5360:	57200016 */ 	bnezl	$t9,.NB0f0a53bc
/*  f0a5364:	92681615 */ 	lbu	$t0,0x1615($s3)
.NB0f0a5368:
/*  f0a5368:	14600078 */ 	bnez	$v1,.NB0f0a554c
/*  f0a536c:	3c078007 */ 	lui	$a3,%hi(g_InCutscene)
/*  f0a5370:	8ce72e24 */ 	lw	$a3,%lo(g_InCutscene)($a3)
/*  f0a5374:	54e00076 */ 	bnezl	$a3,.NB0f0a5550
/*  f0a5378:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a537c:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a5380:	50400007 */ 	beqzl	$v0,.NB0f0a53a0
/*  f0a5384:	8caf1c54 */ 	lw	$t7,0x1c54($a1)
/*  f0a5388:	50400071 */ 	beqzl	$v0,.NB0f0a5550
/*  f0a538c:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a5390:	804d0037 */ 	lb	$t5,0x37($v0)
/*  f0a5394:	55a0006e */ 	bnezl	$t5,.NB0f0a5550
/*  f0a5398:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a539c:	8caf1c54 */ 	lw	$t7,0x1c54($a1)
.NB0f0a53a0:
/*  f0a53a0:	8cae00c4 */ 	lw	$t6,0xc4($a1)
/*  f0a53a4:	01e06027 */ 	nor	$t4,$t7,$zero
/*  f0a53a8:	01cc4824 */ 	and	$t1,$t6,$t4
/*  f0a53ac:	312a0008 */ 	andi	$t2,$t1,0x8
/*  f0a53b0:	51400067 */ 	beqzl	$t2,.NB0f0a5550
/*  f0a53b4:	926d1614 */ 	lbu	$t5,0x1614($s3)
/*  f0a53b8:	92681615 */ 	lbu	$t0,0x1615($s3)
.NB0f0a53bc:
/*  f0a53bc:	92781614 */ 	lbu	$t8,0x1614($s3)
/*  f0a53c0:	26641614 */ 	addiu	$a0,$s3,0x1614
/*  f0a53c4:	0118082a */ 	slt	$at,$t0,$t8
/*  f0a53c8:	50200009 */ 	beqzl	$at,.NB0f0a53f0
/*  f0a53cc:	90820002 */ 	lbu	$v0,0x2($a0)
/*  f0a53d0:	90830000 */ 	lbu	$v1,0x0($a0)
/*  f0a53d4:	908b0002 */ 	lbu	$t3,0x2($a0)
/*  f0a53d8:	0163082a */ 	slt	$at,$t3,$v1
/*  f0a53dc:	50200004 */ 	beqzl	$at,.NB0f0a53f0
/*  f0a53e0:	90820002 */ 	lbu	$v0,0x2($a0)
/*  f0a53e4:	1000000a */ 	beqz	$zero,.NB0f0a5410
/*  f0a53e8:	00601025 */ 	or	$v0,$v1,$zero
/*  f0a53ec:	90820002 */ 	lbu	$v0,0x2($a0)
.NB0f0a53f0:
/*  f0a53f0:	90860001 */ 	lbu	$a2,0x1($a0)
/*  f0a53f4:	00401825 */ 	or	$v1,$v0,$zero
/*  f0a53f8:	0046082a */ 	slt	$at,$v0,$a2
/*  f0a53fc:	10200003 */ 	beqz	$at,.NB0f0a540c
/*  f0a5400:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5404:	10000001 */ 	beqz	$zero,.NB0f0a540c
/*  f0a5408:	00c01825 */ 	or	$v1,$a2,$zero
.NB0f0a540c:
/*  f0a540c:	00601025 */ 	or	$v0,$v1,$zero
.NB0f0a5410:
/*  f0a5410:	90890003 */ 	lbu	$t1,0x3($a0)
/*  f0a5414:	0002ce00 */ 	sll	$t9,$v0,0x18
/*  f0a5418:	00026c00 */ 	sll	$t5,$v0,0x10
/*  f0a541c:	032d7825 */ 	or	$t7,$t9,$t5
/*  f0a5420:	00027200 */ 	sll	$t6,$v0,0x8
/*  f0a5424:	01ee6025 */ 	or	$t4,$t7,$t6
/*  f0a5428:	012c5021 */ 	addu	$t2,$t1,$t4
/*  f0a542c:	afaa0138 */ 	sw	$t2,0x138($sp)
/*  f0a5430:	8ca300d8 */ 	lw	$v1,0xd8($a1)
/*  f0a5434:	14600021 */ 	bnez	$v1,.NB0f0a54bc
/*  f0a5438:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a543c:	14e0001f */ 	bnez	$a3,.NB0f0a54bc
/*  f0a5440:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5444:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a5448:	50400007 */ 	beqzl	$v0,.NB0f0a5468
/*  f0a544c:	8cab1c54 */ 	lw	$t3,0x1c54($a1)
/*  f0a5450:	1040001a */ 	beqz	$v0,.NB0f0a54bc
/*  f0a5454:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5458:	80480037 */ 	lb	$t0,0x37($v0)
/*  f0a545c:	15000017 */ 	bnez	$t0,.NB0f0a54bc
/*  f0a5460:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5464:	8cab1c54 */ 	lw	$t3,0x1c54($a1)
.NB0f0a5468:
/*  f0a5468:	8cb800c4 */ 	lw	$t8,0xc4($a1)
/*  f0a546c:	3c02800a */ 	lui	$v0,%hi(var8009caef)
/*  f0a5470:	0160c827 */ 	nor	$t9,$t3,$zero
/*  f0a5474:	03196824 */ 	and	$t5,$t8,$t9
/*  f0a5478:	31af0001 */ 	andi	$t7,$t5,0x1
/*  f0a547c:	11e0000f */ 	beqz	$t7,.NB0f0a54bc
/*  f0a5480:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5484:	9042122f */ 	lbu	$v0,%lo(var8009caef)($v0)
/*  f0a5488:	3c06800a */ 	lui	$a2,%hi(var8009caf0)
/*  f0a548c:	90c61230 */ 	lbu	$a2,%lo(var8009caf0)($a2)
/*  f0a5490:	00027600 */ 	sll	$t6,$v0,0x18
/*  f0a5494:	00024c00 */ 	sll	$t1,$v0,0x10
/*  f0a5498:	01c96025 */ 	or	$t4,$t6,$t1
/*  f0a549c:	00025200 */ 	sll	$t2,$v0,0x8
/*  f0a54a0:	018a4025 */ 	or	$t0,$t4,$t2
/*  f0a54a4:	afa200a8 */ 	sw	$v0,0xa8($sp)
/*  f0a54a8:	afa200ac */ 	sw	$v0,0xac($sp)
/*  f0a54ac:	afa200b0 */ 	sw	$v0,0xb0($sp)
/*  f0a54b0:	00c8a821 */ 	addu	$s5,$a2,$t0
/*  f0a54b4:	1000001f */ 	beqz	$zero,.NB0f0a5534
/*  f0a54b8:	afa600b4 */ 	sw	$a2,0xb4($sp)
.NB0f0a54bc:
/*  f0a54bc:	5460001e */ 	bnezl	$v1,.NB0f0a5538
/*  f0a54c0:	8fb900e4 */ 	lw	$t9,0xe4($sp)
/*  f0a54c4:	54e0001c */ 	bnezl	$a3,.NB0f0a5538
/*  f0a54c8:	8fb900e4 */ 	lw	$t9,0xe4($sp)
/*  f0a54cc:	8ca20480 */ 	lw	$v0,0x480($a1)
/*  f0a54d0:	50400007 */ 	beqzl	$v0,.NB0f0a54f0
/*  f0a54d4:	8cb91c54 */ 	lw	$t9,0x1c54($a1)
/*  f0a54d8:	50400017 */ 	beqzl	$v0,.NB0f0a5538
/*  f0a54dc:	8fb900e4 */ 	lw	$t9,0xe4($sp)
/*  f0a54e0:	804b0037 */ 	lb	$t3,0x37($v0)
/*  f0a54e4:	55600014 */ 	bnezl	$t3,.NB0f0a5538
/*  f0a54e8:	8fb900e4 */ 	lw	$t9,0xe4($sp)
/*  f0a54ec:	8cb91c54 */ 	lw	$t9,0x1c54($a1)
.NB0f0a54f0:
/*  f0a54f0:	8cb800c4 */ 	lw	$t8,0xc4($a1)
/*  f0a54f4:	240200ff */ 	addiu	$v0,$zero,0xff
/*  f0a54f8:	03206827 */ 	nor	$t5,$t9,$zero
/*  f0a54fc:	030d7824 */ 	and	$t7,$t8,$t5
/*  f0a5500:	31ee0008 */ 	andi	$t6,$t7,0x8
/*  f0a5504:	11c0000b */ 	beqz	$t6,.NB0f0a5534
/*  f0a5508:	24050080 */ 	addiu	$a1,$zero,0x80
/*  f0a550c:	00024e00 */ 	sll	$t1,$v0,0x18
/*  f0a5510:	00006400 */ 	sll	$t4,$zero,0x10
/*  f0a5514:	012c5025 */ 	or	$t2,$t1,$t4
/*  f0a5518:	00004200 */ 	sll	$t0,$zero,0x8
/*  f0a551c:	01485825 */ 	or	$t3,$t2,$t0
/*  f0a5520:	00aba821 */ 	addu	$s5,$a1,$t3
/*  f0a5524:	afa20098 */ 	sw	$v0,0x98($sp)
/*  f0a5528:	afa0009c */ 	sw	$zero,0x9c($sp)
/*  f0a552c:	afa000a0 */ 	sw	$zero,0xa0($sp)
/*  f0a5530:	afa500a4 */ 	sw	$a1,0xa4($sp)
.NB0f0a5534:
/*  f0a5534:	8fb900e4 */ 	lw	$t9,0xe4($sp)
.NB0f0a5538:
/*  f0a5538:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a553c:	5721003d */ 	bnel	$t9,$at,.NB0f0a5634
/*  f0a5540:	8e6f00bc */ 	lw	$t7,0xbc($s3)
/*  f0a5544:	1000003a */ 	beqz	$zero,.NB0f0a5630
/*  f0a5548:	afb50138 */ 	sw	$s5,0x138($sp)
.NB0f0a554c:
/*  f0a554c:	926d1614 */ 	lbu	$t5,0x1614($s3)
.NB0f0a5550:
/*  f0a5550:	92781617 */ 	lbu	$t8,0x1617($s3)
/*  f0a5554:	92691615 */ 	lbu	$t1,0x1615($s3)
/*  f0a5558:	92681616 */ 	lbu	$t0,0x1616($s3)
/*  f0a555c:	000d7e00 */ 	sll	$t7,$t5,0x18
/*  f0a5560:	030f7025 */ 	or	$t6,$t8,$t7
/*  f0a5564:	00096400 */ 	sll	$t4,$t1,0x10
/*  f0a5568:	01cc5025 */ 	or	$t2,$t6,$t4
/*  f0a556c:	00085a00 */ 	sll	$t3,$t0,0x8
/*  f0a5570:	014bc825 */ 	or	$t9,$t2,$t3
/*  f0a5574:	afb90138 */ 	sw	$t9,0x138($sp)
/*  f0a5578:	920d0000 */ 	lbu	$t5,0x0($s0)
/*  f0a557c:	24010006 */ 	addiu	$at,$zero,0x6
/*  f0a5580:	0320a825 */ 	or	$s5,$t9,$zero
/*  f0a5584:	15a1002a */ 	bne	$t5,$at,.NB0f0a5630
/*  f0a5588:	3c04ff00 */ 	lui	$a0,0xff00
/*  f0a558c:	3c014248 */ 	lui	$at,0x4248
/*  f0a5590:	44812000 */ 	mtc1	$at,$f4
/*  f0a5594:	c612023c */ 	lwc1	$f18,0x23c($s0)
/*  f0a5598:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f0a559c:	3c014f00 */ 	lui	$at,0x4f00
/*  f0a55a0:	46049182 */ 	mul.s	$f6,$f18,$f4
/*  f0a55a4:	3484007f */ 	ori	$a0,$a0,0x7f
/*  f0a55a8:	4458f800 */ 	cfc1	$t8,$31
/*  f0a55ac:	44c6f800 */ 	ctc1	$a2,$31
/*  f0a55b0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a55b4:	46003224 */ 	cvt.w.s	$f8,$f6
/*  f0a55b8:	4446f800 */ 	cfc1	$a2,$31
/*  f0a55bc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a55c0:	30c60078 */ 	andi	$a2,$a2,0x78
/*  f0a55c4:	50c00013 */ 	beqzl	$a2,.NB0f0a5614
/*  f0a55c8:	44064000 */ 	mfc1	$a2,$f8
/*  f0a55cc:	44814000 */ 	mtc1	$at,$f8
/*  f0a55d0:	24060001 */ 	addiu	$a2,$zero,0x1
/*  f0a55d4:	46083201 */ 	sub.s	$f8,$f6,$f8
/*  f0a55d8:	44c6f800 */ 	ctc1	$a2,$31
/*  f0a55dc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a55e0:	46004224 */ 	cvt.w.s	$f8,$f8
/*  f0a55e4:	4446f800 */ 	cfc1	$a2,$31
/*  f0a55e8:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a55ec:	30c60078 */ 	andi	$a2,$a2,0x78
/*  f0a55f0:	14c00005 */ 	bnez	$a2,.NB0f0a5608
/*  f0a55f4:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a55f8:	44064000 */ 	mfc1	$a2,$f8
/*  f0a55fc:	3c018000 */ 	lui	$at,0x8000
/*  f0a5600:	10000007 */ 	beqz	$zero,.NB0f0a5620
/*  f0a5604:	00c13025 */ 	or	$a2,$a2,$at
.NB0f0a5608:
/*  f0a5608:	10000005 */ 	beqz	$zero,.NB0f0a5620
/*  f0a560c:	2406ffff */ 	addiu	$a2,$zero,-1
/*  f0a5610:	44064000 */ 	mfc1	$a2,$f8
.NB0f0a5614:
/*  f0a5614:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5618:	04c0fffb */ 	bltz	$a2,.NB0f0a5608
/*  f0a561c:	00000000 */ 	sll	$zero,$zero,0x0
.NB0f0a5620:
/*  f0a5620:	44d8f800 */ 	ctc1	$t8,$31
/*  f0a5624:	0fc01990 */ 	jal	colourBlend
/*  f0a5628:	03202825 */ 	or	$a1,$t9,$zero
/*  f0a562c:	afa20138 */ 	sw	$v0,0x138($sp)
.NB0f0a5630:
/*  f0a5630:	8e6f00bc */ 	lw	$t7,0xbc($s3)
.NB0f0a5634:
/*  f0a5634:	0fc089b4 */ 	jal	chrGetCloakAlpha
/*  f0a5638:	8de40004 */ 	lw	$a0,0x4($t7)
/*  f0a563c:	284100ff */ 	slti	$at,$v0,0xff
/*  f0a5640:	1020000f */ 	beqz	$at,.NB0f0a5680
/*  f0a5644:	240c0001 */ 	addiu	$t4,$zero,0x1
/*  f0a5648:	44825000 */ 	mtc1	$v0,$f10
/*  f0a564c:	3c017f1a */ 	lui	$at,%hi(var7f1aca90)
/*  f0a5650:	c4326dd8 */ 	lwc1	$f18,%lo(var7f1aca90)($at)
/*  f0a5654:	46805420 */ 	cvt.s.w	$f16,$f10
/*  f0a5658:	8fa40138 */ 	lw	$a0,0x138($sp)
/*  f0a565c:	240e0005 */ 	addiu	$t6,$zero,0x5
/*  f0a5660:	afae0134 */ 	sw	$t6,0x134($sp)
/*  f0a5664:	afa4013c */ 	sw	$a0,0x13c($sp)
/*  f0a5668:	46128102 */ 	mul.s	$f4,$f16,$f18
/*  f0a566c:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a5670:	44033000 */ 	mfc1	$v1,$f6
/*  f0a5674:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5678:	24750041 */ 	addiu	$s5,$v1,0x41
/*  f0a567c:	afb50138 */ 	sw	$s5,0x138($sp)
.NB0f0a5680:
/*  f0a5680:	0c005dbc */ 	jal	mtx00016760
/*  f0a5684:	afac0108 */ 	sw	$t4,0x108($sp)
/*  f0a5688:	8e020218 */ 	lw	$v0,0x218($s0)
/*  f0a568c:	5040000f */ 	beqzl	$v0,.NB0f0a56cc
/*  f0a5690:	8fa400e4 */ 	lw	$a0,0xe4($sp)
/*  f0a5694:	8c450018 */ 	lw	$a1,0x18($v0)
/*  f0a5698:	27a40104 */ 	addiu	$a0,$sp,0x104
/*  f0a569c:	0c008be3 */ 	jal	modelRender
/*  f0a56a0:	afa50090 */ 	sw	$a1,0x90($sp)
/*  f0a56a4:	8fa60090 */ 	lw	$a2,0x90($sp)
/*  f0a56a8:	8cc80008 */ 	lw	$t0,0x8($a2)
/*  f0a56ac:	8cc4000c */ 	lw	$a0,0xc($a2)
/*  f0a56b0:	0fc303f0 */ 	jal	mtxF2LBulk
/*  f0a56b4:	8505000e */ 	lh	$a1,0xe($t0)
/*  f0a56b8:	8e0a021c */ 	lw	$t2,0x21c($s0)
/*  f0a56bc:	51400003 */ 	beqzl	$t2,.NB0f0a56cc
/*  f0a56c0:	8fa400e4 */ 	lw	$a0,0xe4($sp)
/*  f0a56c4:	ae000218 */ 	sw	$zero,0x218($s0)
/*  f0a56c8:	8fa400e4 */ 	lw	$a0,0xe4($sp)
.NB0f0a56cc:
/*  f0a56cc:	0fc2bd48 */ 	jal	weaponHasFlag
/*  f0a56d0:	24050020 */ 	addiu	$a1,$zero,0x20
/*  f0a56d4:	1040000e */ 	beqz	$v0,.NB0f0a5710
/*  f0a56d8:	24010001 */ 	addiu	$at,$zero,0x1
/*  f0a56dc:	8fab0110 */ 	lw	$t3,0x110($sp)
/*  f0a56e0:	3c18b600 */ 	lui	$t8,0xb600
/*  f0a56e4:	24193000 */ 	addiu	$t9,$zero,0x3000
/*  f0a56e8:	256d0008 */ 	addiu	$t5,$t3,0x8
/*  f0a56ec:	afad0110 */ 	sw	$t5,0x110($sp)
/*  f0a56f0:	ad790004 */ 	sw	$t9,0x4($t3)
/*  f0a56f4:	16800004 */ 	bnez	$s4,.NB0f0a5708
/*  f0a56f8:	ad780000 */ 	sw	$t8,0x0($t3)
/*  f0a56fc:	240f0003 */ 	addiu	$t7,$zero,0x3
/*  f0a5700:	10000003 */ 	beqz	$zero,.NB0f0a5710
/*  f0a5704:	afaf0140 */ 	sw	$t7,0x140($sp)
.NB0f0a5708:
/*  f0a5708:	24090002 */ 	addiu	$t1,$zero,0x2
/*  f0a570c:	afa90140 */ 	sw	$t1,0x140($sp)
.NB0f0a5710:
/*  f0a5710:	8e2e006c */ 	lw	$t6,0x6c($s1)
/*  f0a5714:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a5718:	00002025 */ 	or	$a0,$zero,$zero
/*  f0a571c:	11c00003 */ 	beqz	$t6,.NB0f0a572c
/*  f0a5720:	00001025 */ 	or	$v0,$zero,$zero
/*  f0a5724:	10000001 */ 	beqz	$zero,.NB0f0a572c
/*  f0a5728:	24050001 */ 	addiu	$a1,$zero,0x1
.NB0f0a572c:
/*  f0a572c:	8e2c0068 */ 	lw	$t4,0x68($s1)
/*  f0a5730:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a5734:	11800003 */ 	beqz	$t4,.NB0f0a5744
/*  f0a5738:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a573c:	10000001 */ 	beqz	$zero,.NB0f0a5744
/*  f0a5740:	24040001 */ 	addiu	$a0,$zero,0x1
.NB0f0a5744:
/*  f0a5744:	8e280064 */ 	lw	$t0,0x64($s1)
/*  f0a5748:	11000003 */ 	beqz	$t0,.NB0f0a5758
/*  f0a574c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5750:	10000001 */ 	beqz	$zero,.NB0f0a5758
/*  f0a5754:	24020001 */ 	addiu	$v0,$zero,0x1
.NB0f0a5758:
/*  f0a5758:	8e2a0070 */ 	lw	$t2,0x70($s1)
/*  f0a575c:	11400003 */ 	beqz	$t2,.NB0f0a576c
/*  f0a5760:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5764:	10000001 */ 	beqz	$zero,.NB0f0a576c
/*  f0a5768:	24030001 */ 	addiu	$v1,$zero,0x1
.NB0f0a576c:
/*  f0a576c:	00625821 */ 	addu	$t3,$v1,$v0
/*  f0a5770:	01646821 */ 	addu	$t5,$t3,$a0
/*  f0a5774:	01a5c021 */ 	addu	$t8,$t5,$a1
/*  f0a5778:	17010036 */ 	bne	$t8,$at,.NB0f0a5854
/*  f0a577c:	24050041 */ 	addiu	$a1,$zero,0x41
/*  f0a5780:	0c006ea3 */ 	jal	modelGetPart
/*  f0a5784:	8e04038c */ 	lw	$a0,0x38c($s0)
/*  f0a5788:	50400033 */ 	beqzl	$v0,.NB0f0a5858
/*  f0a578c:	27a40104 */ 	addiu	$a0,$sp,0x104
/*  f0a5790:	8c440004 */ 	lw	$a0,0x4($v0)
/*  f0a5794:	00002825 */ 	or	$a1,$zero,$zero
/*  f0a5798:	84990010 */ 	lh	$t9,0x10($a0)
/*  f0a579c:	5b20002e */ 	blezl	$t9,.NB0f0a5858
/*  f0a57a0:	27a40104 */ 	addiu	$a0,$sp,0x104
/*  f0a57a4:	8e2c0034 */ 	lw	$t4,0x34($s1)
.NB0f0a57a8:
/*  f0a57a8:	8c8f000c */ 	lw	$t7,0xc($a0)
/*  f0a57ac:	00054880 */ 	sll	$t1,$a1,0x2
/*  f0a57b0:	01920019 */ 	multu	$t4,$s2
/*  f0a57b4:	01254823 */ 	subu	$t1,$t1,$a1
/*  f0a57b8:	00094880 */ 	sll	$t1,$t1,0x2
/*  f0a57bc:	01e91021 */ 	addu	$v0,$t7,$t1
/*  f0a57c0:	844e000a */ 	lh	$t6,0xa($v0)
/*  f0a57c4:	00056880 */ 	sll	$t5,$a1,0x2
/*  f0a57c8:	01a56823 */ 	subu	$t5,$t5,$a1
/*  f0a57cc:	000d6880 */ 	sll	$t5,$t5,0x2
/*  f0a57d0:	24420002 */ 	addiu	$v0,$v0,0x2
/*  f0a57d4:	00004012 */ 	mflo	$t0
/*  f0a57d8:	01c85023 */ 	subu	$t2,$t6,$t0
/*  f0a57dc:	a44a0008 */ 	sh	$t2,0x8($v0)
/*  f0a57e0:	8c8b000c */ 	lw	$t3,0xc($a0)
/*  f0a57e4:	016dc021 */ 	addu	$t8,$t3,$t5
/*  f0a57e8:	8719000a */ 	lh	$t9,0xa($t8)
/*  f0a57ec:	2b21a000 */ 	slti	$at,$t9,-24576
/*  f0a57f0:	50200014 */ 	beqzl	$at,.NB0f0a5844
/*  f0a57f4:	848b0010 */ 	lh	$t3,0x10($a0)
/*  f0a57f8:	848f0010 */ 	lh	$t7,0x10($a0)
/*  f0a57fc:	00001825 */ 	or	$v1,$zero,$zero
/*  f0a5800:	59e00010 */ 	blezl	$t7,.NB0f0a5844
/*  f0a5804:	848b0010 */ 	lh	$t3,0x10($a0)
/*  f0a5808:	8c89000c */ 	lw	$t1,0xc($a0)
.NB0f0a580c:
/*  f0a580c:	00036080 */ 	sll	$t4,$v1,0x2
/*  f0a5810:	01836023 */ 	subu	$t4,$t4,$v1
/*  f0a5814:	000c6080 */ 	sll	$t4,$t4,0x2
/*  f0a5818:	012c1021 */ 	addu	$v0,$t1,$t4
/*  f0a581c:	844e000a */ 	lh	$t6,0xa($v0)
/*  f0a5820:	24630001 */ 	addiu	$v1,$v1,0x1
/*  f0a5824:	24420002 */ 	addiu	$v0,$v0,0x2
/*  f0a5828:	25c82000 */ 	addiu	$t0,$t6,0x2000
/*  f0a582c:	a4480008 */ 	sh	$t0,0x8($v0)
/*  f0a5830:	848a0010 */ 	lh	$t2,0x10($a0)
/*  f0a5834:	006a082a */ 	slt	$at,$v1,$t2
/*  f0a5838:	5420fff4 */ 	bnezl	$at,.NB0f0a580c
/*  f0a583c:	8c89000c */ 	lw	$t1,0xc($a0)
/*  f0a5840:	848b0010 */ 	lh	$t3,0x10($a0)
.NB0f0a5844:
/*  f0a5844:	24a50001 */ 	addiu	$a1,$a1,0x1
/*  f0a5848:	00ab082a */ 	slt	$at,$a1,$t3
/*  f0a584c:	5420ffd6 */ 	bnezl	$at,.NB0f0a57a8
/*  f0a5850:	8e2c0034 */ 	lw	$t4,0x34($s1)
.NB0f0a5854:
/*  f0a5854:	27a40104 */ 	addiu	$a0,$sp,0x104
.NB0f0a5858:
/*  f0a5858:	0c008be3 */ 	jal	modelRender
/*  f0a585c:	8fa50038 */ 	lw	$a1,0x38($sp)
/*  f0a5860:	8e6d1594 */ 	lw	$t5,0x1594($s3)
/*  f0a5864:	3c188007 */ 	lui	$t8,%hi(var800702dc)
/*  f0a5868:	51a00013 */ 	beqzl	$t5,.NB0f0a58b8
/*  f0a586c:	8fa90110 */ 	lw	$t1,0x110($sp)
/*  f0a5870:	8f18299c */ 	lw	$t8,%lo(var800702dc)($t8)
/*  f0a5874:	8fb90138 */ 	lw	$t9,0x138($sp)
/*  f0a5878:	5300000f */ 	beqzl	$t8,.NB0f0a58b8
/*  f0a587c:	8fa90110 */ 	lw	$t1,0x110($sp)
/*  f0a5880:	afb90078 */ 	sw	$t9,0x78($sp)
/*  f0a5884:	8e0f0390 */ 	lw	$t7,0x390($s0)
/*  f0a5888:	26050534 */ 	addiu	$a1,$s0,0x534
/*  f0a588c:	00a02025 */ 	or	$a0,$a1,$zero
/*  f0a5890:	ae0f0540 */ 	sw	$t7,0x540($s0)
/*  f0a5894:	0c007728 */ 	jal	modelUpdateRelations
/*  f0a5898:	afa50050 */ 	sw	$a1,0x50($sp)
/*  f0a589c:	8fa50050 */ 	lw	$a1,0x50($sp)
/*  f0a58a0:	afb50138 */ 	sw	$s5,0x138($sp)
/*  f0a58a4:	0c008be3 */ 	jal	modelRender
/*  f0a58a8:	27a40104 */ 	addiu	$a0,$sp,0x104
/*  f0a58ac:	8fa40078 */ 	lw	$a0,0x78($sp)
/*  f0a58b0:	afa40138 */ 	sw	$a0,0x138($sp)
/*  f0a58b4:	8fa90110 */ 	lw	$t1,0x110($sp)
.NB0f0a58b8:
/*  f0a58b8:	8fa400e4 */ 	lw	$a0,0xe4($sp)
/*  f0a58bc:	24050020 */ 	addiu	$a1,$zero,0x20
/*  f0a58c0:	0fc2bd48 */ 	jal	weaponHasFlag
/*  f0a58c4:	afa90144 */ 	sw	$t1,0x144($sp)
/*  f0a58c8:	10400007 */ 	beqz	$v0,.NB0f0a58e8
/*  f0a58cc:	8fac0144 */ 	lw	$t4,0x144($sp)
/*  f0a58d0:	258e0008 */ 	addiu	$t6,$t4,0x8
/*  f0a58d4:	afae0144 */ 	sw	$t6,0x144($sp)
/*  f0a58d8:	3c08b600 */ 	lui	$t0,0xb600
/*  f0a58dc:	240a3000 */ 	addiu	$t2,$zero,0x3000
/*  f0a58e0:	ad8a0004 */ 	sw	$t2,0x4($t4)
/*  f0a58e4:	ad880000 */ 	sw	$t0,0x0($t4)
.NB0f0a58e8:
/*  f0a58e8:	8e0b038c */ 	lw	$t3,0x38c($s0)
/*  f0a58ec:	8e040390 */ 	lw	$a0,0x390($s0)
/*  f0a58f0:	0fc303f0 */ 	jal	mtxF2LBulk
/*  f0a58f4:	8565000e */ 	lh	$a1,0xe($t3)
/*  f0a58f8:	0c005dc5 */ 	jal	mtx00016784
/*  f0a58fc:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5900:	8fad0144 */ 	lw	$t5,0x144($sp)
/*  f0a5904:	3c19bc00 */ 	lui	$t9,0xbc00
/*  f0a5908:	3739000e */ 	ori	$t9,$t9,0xe
/*  f0a590c:	25b80008 */ 	addiu	$t8,$t5,0x8
/*  f0a5910:	afb80144 */ 	sw	$t8,0x144($sp)
/*  f0a5914:	adb90000 */ 	sw	$t9,0x0($t5)
/*  f0a5918:	0c002b3b */ 	jal	viGetPerspScale
/*  f0a591c:	afad0070 */ 	sw	$t5,0x70($sp)
/*  f0a5920:	8fa30070 */ 	lw	$v1,0x70($sp)
/*  f0a5924:	ac620004 */ 	sw	$v0,0x4($v1)
.NB0f0a5928:
/*  f0a5928:	26940001 */ 	addiu	$s4,$s4,0x1
/*  f0a592c:	24010002 */ 	addiu	$at,$zero,0x2
/*  f0a5930:	1681fe0c */ 	bne	$s4,$at,.NB0f0a5164
/*  f0a5934:	261007a4 */ 	addiu	$s0,$s0,0x7a4
/*  f0a5938:	afb500dc */ 	sw	$s5,0xdc($sp)
/*  f0a593c:	0fc2b250 */ 	jal	casingsRender
/*  f0a5940:	27a40144 */ 	addiu	$a0,$sp,0x144
/*  f0a5944:	0fc5c3ce */ 	jal	zbufSwap
/*  f0a5948:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a594c:	0fc5c3d3 */ 	jal	zbufConfigureRdp
/*  f0a5950:	8fa40144 */ 	lw	$a0,0x144($sp)
/*  f0a5954:	afa20144 */ 	sw	$v0,0x144($sp)
/*  f0a5958:	0c002cd4 */ 	jal	vi0000b1d0
/*  f0a595c:	00402025 */ 	or	$a0,$v0,$zero
/*  f0a5960:	244f0008 */ 	addiu	$t7,$v0,0x8
/*  f0a5964:	afaf0144 */ 	sw	$t7,0x144($sp)
/*  f0a5968:	0c002fb5 */ 	jal	viGetViewLeft
/*  f0a596c:	00408825 */ 	or	$s1,$v0,$zero
/*  f0a5970:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a5974:	00104c03 */ 	sra	$t1,$s0,0x10
/*  f0a5978:	0c002fb9 */ 	jal	viGetViewTop
/*  f0a597c:	01208025 */ 	or	$s0,$t1,$zero
/*  f0a5980:	44824000 */ 	mtc1	$v0,$f8
/*  f0a5984:	44902000 */ 	mtc1	$s0,$f4
/*  f0a5988:	3c014080 */ 	lui	$at,0x4080
/*  f0a598c:	468042a0 */ 	cvt.s.w	$f10,$f8
/*  f0a5990:	44810000 */ 	mtc1	$at,$f0
/*  f0a5994:	3c01ed00 */ 	lui	$at,0xed00
/*  f0a5998:	468021a0 */ 	cvt.s.w	$f6,$f4
/*  f0a599c:	46005402 */ 	mul.s	$f16,$f10,$f0
/*  f0a59a0:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a59a4:	46003202 */ 	mul.s	$f8,$f6,$f0
/*  f0a59a8:	4600848d */ 	trunc.w.s	$f18,$f16
/*  f0a59ac:	4600428d */ 	trunc.w.s	$f10,$f8
/*  f0a59b0:	440e9000 */ 	mfc1	$t6,$f18
/*  f0a59b4:	440d5000 */ 	mfc1	$t5,$f10
/*  f0a59b8:	31c80fff */ 	andi	$t0,$t6,0xfff
/*  f0a59bc:	01015025 */ 	or	$t2,$t0,$at
/*  f0a59c0:	31b80fff */ 	andi	$t8,$t5,0xfff
/*  f0a59c4:	0018cb00 */ 	sll	$t9,$t8,0xc
/*  f0a59c8:	01597825 */ 	or	$t7,$t2,$t9
/*  f0a59cc:	0c002f97 */ 	jal	viGetViewWidth
/*  f0a59d0:	ae2f0000 */ 	sw	$t7,0x0($s1)
/*  f0a59d4:	00029400 */ 	sll	$s2,$v0,0x10
/*  f0a59d8:	00124c03 */ 	sra	$t1,$s2,0x10
/*  f0a59dc:	0c002fb5 */ 	jal	viGetViewLeft
/*  f0a59e0:	01209025 */ 	or	$s2,$t1,$zero
/*  f0a59e4:	0002a400 */ 	sll	$s4,$v0,0x10
/*  f0a59e8:	00146403 */ 	sra	$t4,$s4,0x10
/*  f0a59ec:	0c002fb9 */ 	jal	viGetViewTop
/*  f0a59f0:	0180a025 */ 	or	$s4,$t4,$zero
/*  f0a59f4:	00028400 */ 	sll	$s0,$v0,0x10
/*  f0a59f8:	00107403 */ 	sra	$t6,$s0,0x10
/*  f0a59fc:	0c002f9b */ 	jal	viGetViewHeight
/*  f0a5a00:	01c08025 */ 	or	$s0,$t6,$zero
/*  f0a5a04:	00504021 */ 	addu	$t0,$v0,$s0
/*  f0a5a08:	44888000 */ 	mtc1	$t0,$f16
/*  f0a5a0c:	02925021 */ 	addu	$t2,$s4,$s2
/*  f0a5a10:	448a5000 */ 	mtc1	$t2,$f10
/*  f0a5a14:	468084a0 */ 	cvt.s.w	$f18,$f16
/*  f0a5a18:	3c014080 */ 	lui	$at,0x4080
/*  f0a5a1c:	44812000 */ 	mtc1	$at,$f4
/*  f0a5a20:	46805420 */ 	cvt.s.w	$f16,$f10
/*  f0a5a24:	46049182 */ 	mul.s	$f6,$f18,$f4
/*  f0a5a28:	44819000 */ 	mtc1	$at,$f18
/*  f0a5a2c:	00000000 */ 	sll	$zero,$zero,0x0
/*  f0a5a30:	46128102 */ 	mul.s	$f4,$f16,$f18
/*  f0a5a34:	4600320d */ 	trunc.w.s	$f8,$f6
/*  f0a5a38:	4600218d */ 	trunc.w.s	$f6,$f4
/*  f0a5a3c:	440d4000 */ 	mfc1	$t5,$f8
/*  f0a5a40:	440f3000 */ 	mfc1	$t7,$f6
/*  f0a5a44:	31b80fff */ 	andi	$t8,$t5,0xfff
/*  f0a5a48:	31e90fff */ 	andi	$t1,$t7,0xfff
/*  f0a5a4c:	00096300 */ 	sll	$t4,$t1,0xc
/*  f0a5a50:	030c7025 */ 	or	$t6,$t8,$t4
/*  f0a5a54:	ae2e0004 */ 	sw	$t6,0x4($s1)
/*  f0a5a58:	8fab0148 */ 	lw	$t3,0x148($sp)
/*  f0a5a5c:	8fa80144 */ 	lw	$t0,0x144($sp)
/*  f0a5a60:	ad680000 */ 	sw	$t0,0x0($t3)
/*  f0a5a64:	8fbf0034 */ 	lw	$ra,0x34($sp)
.NB0f0a5a68:
/*  f0a5a68:	8fb0001c */ 	lw	$s0,0x1c($sp)
/*  f0a5a6c:	8fb10020 */ 	lw	$s1,0x20($sp)
/*  f0a5a70:	8fb20024 */ 	lw	$s2,0x24($sp)
/*  f0a5a74:	8fb30028 */ 	lw	$s3,0x28($sp)
/*  f0a5a78:	8fb4002c */ 	lw	$s4,0x2c($sp)
/*  f0a5a7c:	8fb50030 */ 	lw	$s5,0x30($sp)
/*  f0a5a80:	03e00008 */ 	jr	$ra
/*  f0a5a84:	27bd0148 */ 	addiu	$sp,$sp,0x148
);
#endif
#else
// Mismatch: Goal uses different codegen for accessing vertices
void bgunRender(Gfx **gdlptr)
{
	Gfx *gdl = *gdlptr;
	struct modelrenderdata renderdata = {NULL, true, 3}; // 10c
	struct player *player;
	s32 i;

	static bool renderhand = true; // var800702dc

	player = g_Vars.currentplayer;

	/* B-246 round-6 instrumentation: log FP render entry on the first 5
	 * ticks of a stage AND on every 60th tick thereafter. Round-5 showed the
	 * round-4 first-5-frames-only window closed before stage 4's fp_render
	 * branch became active (cameramode was THIRDPERSON during those frames),
	 * leaving zero bgunRender entry log lines in the entire reproduction
	 * window. Widening the throttle gives stable per-second visibility into
	 * the gunmodeldef and handmodeldef pointers across the whole match. */
	if (g_Vars.currentplayernum == 0) {
		static s32 s_LastReportedFrame = -1;
		bool first_window = (g_Vars.lvframe60 < 5 && (s32)g_Vars.lvframe60 != s_LastReportedFrame);
		bool periodic = ((g_Vars.lvframenum % 60) == 47 && (s32)g_Vars.lvframenum != s_LastReportedFrame);
		if (first_window || periodic) {
			s_LastReportedFrame = first_window ? (s32)g_Vars.lvframe60 : (s32)g_Vars.lvframenum;
			sysLogPrintf(LOG_NOTE,
				"LOG.WPN.DIAG: bgunRender enter player=0 frame=%d lvframe60=%d visionmode=%d "
				"R(wpn=%d visible=%d inuse=%d state=%d sm=%d) L(wpn=%d visible=%d inuse=%d state=%d sm=%d) "
				"gunctrl_wpn=%d switchto=%d passive=%d gunmodeldef=%p handmodeldef=%p R_handmodel_def=%p R_gunmodel_def=%p masterload=%d gunmemowner=%d",
				(s32)g_Vars.lvframenum, (s32)g_Vars.lvframe60, (s32)player->visionmode,
				(s32)player->hands[HAND_RIGHT].gset.weaponnum,
				(s32)player->hands[HAND_RIGHT].visible,
				(s32)player->hands[HAND_RIGHT].inuse,
				(s32)player->hands[HAND_RIGHT].state,
				(s32)player->hands[HAND_RIGHT].stateminor,
				(s32)player->hands[HAND_LEFT].gset.weaponnum,
				(s32)player->hands[HAND_LEFT].visible,
				(s32)player->hands[HAND_LEFT].inuse,
				(s32)player->hands[HAND_LEFT].state,
				(s32)player->hands[HAND_LEFT].stateminor,
				(s32)player->gunctrl.weaponnum,
				(s32)player->gunctrl.switchtoweaponnum,
				(s32)player->gunctrl.passivemode,
				(void *)player->gunctrl.gunmodeldef,
				(void *)player->gunctrl.handmodeldef,
				(void *)player->hands[HAND_RIGHT].handmodel.definition,
				(void *)player->hands[HAND_RIGHT].gunmodel.definition,
				(s32)player->gunctrl.masterloadstate,
				(s32)player->gunctrl.gunmemowner);
		}
	}

	if (player->visionmode == VISIONMODE_XRAY) {
		for (i = 0; i < 2; i++) {
			if (g_Vars.currentplayer->hands[i].firedrocket) {
				g_Vars.currentplayer->hands[i].rocket = NULL;
			}
		}
		return;
	}

	gdl = viPrepareZbuf(gdl);
	gdl = vi0000b1d0(gdl);

	gDPSetScissor(gdl++, G_SC_NON_INTERLACE, viGetViewLeft(), viGetViewTop(),
			viGetViewLeft() + viGetViewWidth(), viGetViewTop() + viGetViewHeight());

	gdl = vi0000aca4(gdl, 1.5, 1000);

	if (g_Vars.currentplayer->teleportstate != TELEPORTSTATE_INACTIVE) {
		f32 f2;

		f2 = player0f0bd358();

		gdl = vi0000b0e8(gdl, 60, f2);
	}

	if (PLAYERCOUNT() == 1) {
		bgunCullLasersightsBeforeRender();
		gdl = lasersightRenderBeam(gdl);
	}

	for (i = 0; i < 2; i++) {
		struct hand *hand;
		s32 j;
		s32 alpha;
		s32 weaponnum; // ec
		struct modelnode *node; // e8
		u32 colour; // e4

		hand = player->hands + i;

		weaponnum = bgunGetWeaponNum2(i);

		if (hand->visible) {
			gdl = beamRender(gdl, &hand->beam, 0, 0);

			if (weaponHasFlag(hand->gset.weaponnum, WEAPONFLAG_00008000)) {
				gSPSetLights1(gdl++, var80070090);
				gSPLookAt(gdl++, camGetLookAt());
			}

			gSPPerspNormalize(gdl++, mtx00016dcc(0, 300));

			// There is support for guns having a TV screen on them
			// but no guns have this model part so it's not used.
			node = modelGetPart(hand->gunmodel.definition, MODELPART_0010);

			if (node) {
				union modelrwdata *rwdata = modelGetNodeRwData(&hand->gunmodel, modelGetPart(hand->gunmodel.definition, MODELPART_0011));

				if (rwdata) {
					rwdata->toggle.visible = true;
				}

				gdl = tvscreenRender(&hand->gunmodel, node, &var8009cf88, gdl, 0, 1);
			}

			renderdata.gdl = gdl;
			renderdata.unk30 = 4;

			if (USINGDEVICE(DEVICE_NIGHTVISION) || USINGDEVICE(DEVICE_IRSCANNER)) {
				// 67c
				u8 *col = player->gunshadecol;
				u32 shade;
				s32 spb0[4];
				s32 spa0[4];

				if (col[0] > col[1] && col[0] > col[2]) {
					shade = col[0];
				} else if (col[1] > col[2]) {
					shade = col[1];
				} else {
					shade = col[2];
				}

				renderdata.envcolour = (shade << 24 | shade << 16 | shade << 8) + col[3];

				if (USINGDEVICE(DEVICE_NIGHTVISION)) {
					spb0[0] = var8009caef;
					spb0[1] = var8009caef;
					spb0[2] = var8009caef;
					spb0[3] = var8009caf0;

					colour = (spb0[0] << 24 | spb0[1] << 16 | spb0[2] << 8) + spb0[3];
				} else if (USINGDEVICE(DEVICE_IRSCANNER)) {
					spa0[0] = 0xff;
					spa0[1] = 0;
					spa0[2] = 0;
					spa0[3] = 0x80;

					colour = (spa0[0] << 24 | spa0[1] << 16 | spa0[2] << 8) + spa0[3];
				}

				if (weaponnum == WEAPON_UNARMED) {
					renderdata.envcolour = colour;
				}
			} else {
				renderdata.envcolour = player->gunshadecol[0] << 24 | player->gunshadecol[1] << 16 | player->gunshadecol[2] << 8 | player->gunshadecol[3];
				colour = renderdata.envcolour;

				// 838
				if (hand->gset.weaponnum == WEAPON_MAULER) {
					u32 weight = hand->matmot1 * 50.0f;
					renderdata.envcolour = colourBlend(0xff00007f, renderdata.envcolour, weight);
				}
			}

			// Apply transparency based on player's cloak
			alpha = chrGetCloakAlpha(player->prop->chr);

			if (alpha < 255) {
				colour = (s32) (alpha * 0.74509805f) + 0x41;
				renderdata.unk30 = 5;
				renderdata.fogcolour = renderdata.envcolour;
				renderdata.envcolour = colour;
			}

			renderdata.zbufferenabled = true;

			if (hand->gunmodel.definition
					&& hand->gunmodel.definition->skel == NULL
					&& modAssetCompilerModeldefIsGenerated(hand->gunmodel.definition)) {
				renderdata.zbufferenabled = false;
			}

			mtx00016760();

			// Render rocket launcher's rocket if it's in Jo's hand or in the launcher
			if (hand->rocket) {
				struct model *rocketmodel = hand->rocket->base.model; // 98
				bool sp94 = false;

#if VERSION >= VERSION_NTSC_1_0
				if (rocketmodel && rocketmodel->definition) {
					sp94 = true;

					modelRender(&renderdata, rocketmodel);

					mtxF2LBulk(rocketmodel->matrices, rocketmodel->definition->nummatrices);

					if (hand->firedrocket) {
						hand->rocket = NULL;
					}
				}

				if (sp94);
#else
				modelRender(&renderdata, rocketmodel);

				mtxF2LBulk(rocketmodel->matrices, rocketmodel->definition->nummatrices);

				if (hand->firedrocket) {
					hand->rocket = NULL;
				}
#endif
			}

			if (weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP)) {
				gSPClearGeometryMode(renderdata.gdl++, G_CULL_BOTH);

				if (i == HAND_RIGHT) {
					renderdata.cullmode = CULLMODE_BACK;
				} else {
					renderdata.cullmode = CULLMODE_FRONT;
				}
			}

			// Slide the laser's liquid texture
			if (PLAYERCOUNT() == 1) {
				node = modelGetPart(hand->gunmodel.definition, MODELPART_GUN_LASERLIQUID);

				// a5c
				if (node) {
					struct modelrodata_gundl *rodata;
					rodata = &node->rodata->gundl;

					for (j = 0; j < rodata->numvertices; j++) {
						// a7c
						s32 stack[2];
						s32 k;

						(rodata->vertices + j)->t -= g_Vars.lvupdate240 * PALUP(25);

						if ((rodata->vertices + j)->t < -0x6000) {
							for (k = 0; k < rodata->numvertices; k++) {
								(rodata->vertices + k)->t += 0x2000;
							}
						}
					}
				}
			}

			// Render the gun
			modelRender(&renderdata, &hand->gunmodel);
			modAssetCompilerTraceGeneratedModeldefRenderStep(
				hand->gunmodel.definition, NULL, "bgun-after-gun-render",
				NULL, NULL, NULL, NULL, 0, 0);

			// Render the hand
			if (player->gunctrl.handmodeldef && renderhand) {
				s32 prevcolour = renderdata.envcolour; // 7c

				hand->handmodel.matrices = hand->gunmodel.matrices;

				modelUpdateRelations(&hand->handmodel);

				renderdata.envcolour = colour;
				modelRender(&renderdata, &hand->handmodel);
				modAssetCompilerTraceGeneratedModeldefRenderStep(
					hand->handmodel.definition, NULL, "bgun-after-hand-render",
					NULL, NULL, NULL, NULL, 0, 0);
				renderdata.envcolour = prevcolour;
			}

			// Clean up
			gdl = renderdata.gdl;

			if (weaponHasFlag(weaponnum, WEAPONFLAG_DUALFLIP)) {
				gSPClearGeometryMode(gdl++, G_CULL_BOTH);
			}

			mtxF2LBulk(hand->gunmodel.matrices, hand->gunmodel.definition->nummatrices);
			modAssetCompilerTraceGeneratedModeldefRenderStep(
				hand->gunmodel.definition, NULL, "bgun-after-mtxF2L",
				NULL, NULL, NULL, NULL, 0, 0);
			mtx00016784();

			gSPPerspNormalize(gdl++, viGetPerspScale());
		}
	}

	casingsRender(&gdl);
	zbufSwap();

	gdl = zbufConfigureRdp(gdl);
	gdl = vi0000b1d0(gdl);

	gDPSetScissor(gdl++, G_SC_NON_INTERLACE, viGetViewLeft(), viGetViewTop(),
			viGetViewLeft() + viGetViewWidth(), viGetViewTop() + viGetViewHeight());

	*gdlptr = gdl;
}
#endif

/**
 * Find and return an available audio handle out of a pool of four.
 */
struct sndstate **bgunAllocateAudioHandle(void)
{
	s32 i;

	for (i = 0; i < ARRAYCOUNT(g_BgunAudioHandles); i++) {
		if (g_BgunAudioHandles[i] == NULL) {
			return &g_BgunAudioHandles[i];
		}
	}

	return NULL;
}

void bgunPlayPropHitSound(struct gset *gset, struct prop *prop, s32 texturenum)
{
#if VERSION >= VERSION_NTSC_1_0
	u32 rand1 = rngRandom();
	u32 rand2 = rngRandom();
	struct sndstate **handle;

	if (g_Vars.lvupdate240 <= 0) {
		return;
	}

	if (texturenum >= 0 && texturenum < TEXTURE_CUSTOM_END
			&& g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->numsounds == 0) {
		return;
	}

	if (gset->weaponnum == WEAPON_REMOTEMINE
			|| gset->weaponnum == WEAPON_PROXIMITYMINE
			|| gset->weaponnum == WEAPON_TIMEDMINE
			|| gset->weaponnum == WEAPON_COMMSRIDER
			|| gset->weaponnum == WEAPON_TRACERBUG
			|| gset->weaponnum == WEAPON_TARGETAMPLIFIER
			|| gset->weaponnum == WEAPON_ECMMINE) {
		psCreate(NULL, prop, SFX_80AA, -1, -1, 0, 0, PSTYPE_NONE, NULL, -1, NULL, -1, -1, -1, -1);
		return;
	}

	handle = bgunAllocateAudioHandle();

	if (handle) {
		if (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) {
			struct chrdata *chr = prop->chr;
			s16 soundnum = -1;
			bool overridden = false;
			s32 vol;
			s32 pan;

			if (chrGetShield(chr) > 0) {
				soundnum = SFX_SHIELD_DAMAGE;
			} else if (gset->weaponnum == WEAPON_COMBATKNIFE
					|| gset->weaponnum == WEAPON_COMBATKNIFE // duplicate
					|| gset->weaponnum == WEAPON_BOLT) {
				soundnum = SFX_05F6;
				overridden = true;
			} else if (gset->weaponnum == WEAPON_UNARMED
					|| (gset->weaponfunc == FUNC_SECONDARY
						&& (gset->weaponnum == WEAPON_FALCON2
							|| gset->weaponnum == WEAPON_FALCON2_SILENCER
							|| gset->weaponnum == WEAPON_FALCON2_SCOPE
							|| gset->weaponnum == WEAPON_DY357MAGNUM
							|| gset->weaponnum == WEAPON_DY357LX))) {
				s16 sounds[] = { SFX_002F, SFX_0030, SFX_0031 };
				soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
			} else {
				s16 sounds[] = { SFX_HIT_CHR, SFX_HIT_CHR };
				soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
			}

			if (soundnum != -1) {
				psGetTheoreticalVolPan(&prop->pos, prop->rooms, soundnum, &vol, &pan);

				if (vol) {
					sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);

					if (*handle) {
						sndAdjust(handle, 0, vol, pan, soundnum, 1, 1, -1, 1);
					}
				}
			}

			if (overridden) {
				return;
			}
		} else {
			s16 soundnum = -1;
			bool overridden = false;
			s32 vol;
			s32 pan;
			u32 stack;

			if (texturenum == TEXTURE_HIT_GLASS) {
				soundnum = SFX_SHIELD_DAMAGE;
			} else if (gset->weaponnum == WEAPON_LASER) {
				if (gset->weaponfunc == FUNC_PRIMARY || ((gset->unk063a % 4) == 0 && (rngRandom() % 2))) {
					if ((rngRandom() % 2) == 0) {
						soundnum = SFX_CLOAK_ON;
					} else {
						soundnum = SFX_CLOAK_OFF;
					}
				}

				overridden = true;
			} else {
				if (gset->weaponnum == WEAPON_COMBATKNIFE || gset->weaponnum == WEAPON_BOLT) {
					soundnum = SFX_HIT_METAL_8079;
					overridden = true;
				} else {
					s16 sounds[] = {
						SFX_001B, SFX_001C, SFX_001D, SFX_001E,
						SFX_001B, SFX_001C, SFX_001D, SFX_001E,
						SFX_001B, SFX_001C, SFX_001D, SFX_001E,
						SFX_0023, SFX_0024, SFX_0025, SFX_0026,
						SFX_0027, SFX_0028, SFX_0029, SFX_002A,
					};

					soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
				}
			}

			if (soundnum != -1) {
				psGetTheoreticalVolPan(&prop->pos, prop->rooms, soundnum, &vol, &pan);

				if (vol) {
					sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);

					if (*handle) {
						sndAdjust(handle, 0, vol, pan, soundnum, 1, 1, -1, 1);
					}
				}
			}

			if (overridden) {
				return;
			}
		}
	}

	if (texturenum >= 0 && texturenum < TEXTURE_CUSTOM_END && g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]) {
		s16 soundnum = -1;

		handle = bgunAllocateAudioHandle();

		if (handle) {
			if (g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->numsounds > 0) {
				s32 index = rand2 % g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->numsounds;
				soundnum = g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->sounds[index];

				if (soundnum != -1) {
					sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
				}
			}

			if (*handle && soundnum != -1) {
				psApplyVolPan(*handle, &prop->pos, 400, 2500, 3000, prop->rooms, soundnum, AL_VOL_FULL, 0);
			}
		}
	}
#else
	u32 rand1 = rngRandom();
	u32 rand2 = rngRandom();
	struct sndstate **handle;

	if (g_Vars.lvupdate240 <= 0) {
		return;
	}

	if (texturenum >= 0 && texturenum < TEXTURE_CUSTOM_END
			&& g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->numsounds == 0) {
		return;
	}

	if (gset->weaponnum == WEAPON_REMOTEMINE
			|| gset->weaponnum == WEAPON_PROXIMITYMINE
			|| gset->weaponnum == WEAPON_TIMEDMINE
			|| gset->weaponnum == WEAPON_COMMSRIDER
			|| gset->weaponnum == WEAPON_TRACERBUG
			|| gset->weaponnum == WEAPON_TARGETAMPLIFIER
			|| gset->weaponnum == WEAPON_ECMMINE) {
		psCreate(NULL, prop, SFX_80AA, -1, -1, 0, 0, PSTYPE_NONE, NULL, -1, NULL, -1, -1, -1, -1);
		return;
	}

	handle = bgunAllocateAudioHandle();

	if (handle) {
		if (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) {
			struct chrdata *chr = prop->chr;
			s16 soundnum;
			bool overridden = false;

			if (chrGetShield(chr) > 0) {
				sndStart(var80095200, SFX_SHIELD_DAMAGE, handle, -1, -1, -1, -1, -1);
				soundnum = SFX_SHIELD_DAMAGE;
			} else if (gset->weaponnum == WEAPON_COMBATKNIFE
					|| gset->weaponnum == WEAPON_COMBATKNIFE // duplicate
					|| gset->weaponnum == WEAPON_BOLT) {
				sndStart(var80095200, SFX_05F6, handle, -1, -1, -1, -1, -1);
				soundnum = SFX_05F6;
				overridden = true;
			} else if (gset->weaponnum == WEAPON_UNARMED
					|| (gset->weaponfunc == FUNC_SECONDARY
						&& (gset->weaponnum == WEAPON_FALCON2
							|| gset->weaponnum == WEAPON_FALCON2_SILENCER
							|| gset->weaponnum == WEAPON_FALCON2_SCOPE
							|| gset->weaponnum == WEAPON_DY357MAGNUM
							|| gset->weaponnum == WEAPON_DY357LX))) {
				s16 sounds[] = { SFX_002F, SFX_0030, SFX_0031 };
				soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
				sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			} else {
				s16 sounds[] = { SFX_HIT_CHR, SFX_HIT_CHR };
				soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
				sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			}

			if (*handle) {
				psApplyVolPan(*handle, &prop->pos, 400, 2500, 3000, prop->rooms, soundnum, AL_VOL_FULL, 0);
			}

			if (overridden) {
				return;
			}
		} else {
			s16 soundnum;
			bool overridden = false;
			u32 stack;

			if (texturenum == TEXTURE_HIT_GLASS) {
				sndStart(var80095200, SFX_SHIELD_DAMAGE, handle, -1, -1, -1, -1, -1);
				soundnum = SFX_SHIELD_DAMAGE;
			} else if (gset->weaponnum == WEAPON_LASER) {
				if (gset->weaponfunc == FUNC_PRIMARY || (gset->unk063a % 8) == 0) {
					if ((rngRandom() % 2) == 0) {
						soundnum = SFX_CLOAK_ON;
					} else {
						soundnum = SFX_CLOAK_OFF;
					}

					sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
					overridden = true;
				}
			} else {
				if (gset->weaponnum == WEAPON_COMBATKNIFE || gset->weaponnum == WEAPON_BOLT) {
					soundnum = SFX_HIT_METAL_8079;
					sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
					overridden = true;
				} else {
					s16 sounds[] = {
						SFX_001B, SFX_001C, SFX_001D, SFX_001E,
						SFX_001B, SFX_001C, SFX_001D, SFX_001E,
						SFX_001B, SFX_001C, SFX_001D, SFX_001E,
						SFX_0023, SFX_0024, SFX_0025, SFX_0026,
						SFX_0027, SFX_0028, SFX_0029, SFX_002A,
					};

					soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
					sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
				}
			}

			if (*handle) {
				psApplyVolPan(*handle, &prop->pos, 400, 2500, 3000, prop->rooms, soundnum, AL_VOL_FULL, 0);
			}

			if (overridden) {
				return;
			}
		}
	}

	if (texturenum >= 0 && texturenum < TEXTURE_CUSTOM_END && g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]) {
		s16 soundnum = -1;

		handle = bgunAllocateAudioHandle();

		if (handle) {
			if (g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->numsounds > 0) {
				s32 index = rand2 % g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->numsounds;
				soundnum = g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->sounds[index];

				sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			}

			if (*handle) {
				psApplyVolPan(*handle, &prop->pos, 400, 2500, 3000, prop->rooms, soundnum, AL_VOL_FULL, 0);
			}
		}
	}
#endif
}

void bgunPlayGlassHitSound(struct coord *pos, RoomNum *rooms, s32 texturenum)
{
	if (g_Vars.lvupdate240 > 0) {
		struct sndstate **handle = bgunAllocateAudioHandle();

		if (handle) {
			sndStart(var80095200, SFX_HIT_GLASS, handle, -1, -1, -1, -1, -1);

			if (*handle) {
				psApplyVolPan(*handle, pos, 400, 2500, 3000, rooms, SFX_HIT_GLASS, AL_VOL_FULL, 0);
			}
		}
	}
}

void bgunPlayBgHitSound(struct gset *gset, struct coord *hitpos, s32 texturenum, RoomNum *rooms)
{
#if VERSION >= VERSION_NTSC_1_0
	struct sndstate **handle;
	u32 rand1 = rngRandom();
	u32 rand2 = rngRandom();
	bool playdefault;
	s16 soundnum;
	bool overridden;

	if (g_Vars.lvupdate240 <= 0) {
		return;
	}

	if (texturenum >= 0 && texturenum < TEXTURE_CUSTOM_END && g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->numsounds == 0) {
		return;
	}

	playdefault = true;
	handle = bgunAllocateAudioHandle();

	if (handle) {
		soundnum = -1;
		overridden = false;

		if (gset->weaponnum == WEAPON_LASER) {
			playdefault = false;

			if (gset->weaponfunc == FUNC_PRIMARY || ((gset->unk063a % 4) == 0 && (rngRandom() % 2))) {
				// Laser sounds
				s16 sounds[] = {SFX_CLOAK_ON, SFX_CLOAK_OFF};
				soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
				sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
				overridden = true;
			}
		} else if (gset->weaponnum == WEAPON_COMBATKNIFE || gset->weaponnum == WEAPON_BOLT) {
			// Knives and bolts make a metal sound
			soundnum = SFX_HIT_METAL_8079;
			sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			overridden = true;
		} else if (gset->weaponnum == WEAPON_REMOTEMINE
				|| gset->weaponnum == WEAPON_PROXIMITYMINE
				|| gset->weaponnum == WEAPON_TIMEDMINE
				|| gset->weaponnum == WEAPON_COMMSRIDER
				|| gset->weaponnum == WEAPON_TRACERBUG
				|| gset->weaponnum == WEAPON_TARGETAMPLIFIER
				|| gset->weaponnum == WEAPON_ECMMINE) {
			// Mine landing/activation sound
			soundnum = SFX_80AA;
			sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			overridden = true;
		} else {
			// Ricochet sounds
			s16 sounds[] = {
				SFX_0013, SFX_0014, SFX_0015, SFX_0016,
				SFX_0017, SFX_0018, SFX_0019, SFX_001A,
				SFX_0017, SFX_0018, SFX_0019, SFX_001A,
				SFX_0017, SFX_0018, SFX_0019, SFX_001A,
				SFX_001F, SFX_0020, SFX_0020, SFX_0021,
				SFX_001F, SFX_0020, SFX_0020, SFX_0021,
				SFX_001F, SFX_0020, SFX_0020, SFX_0021,
				SFX_0023, SFX_0024, SFX_0025, SFX_0026,
				SFX_0027, SFX_0028, SFX_0029, SFX_002A,
			};

			soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
			sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			overridden = false;
		}

		if (*handle != NULL) {
			psApplyVolPan(*handle, hitpos, 400, 2500, 3000, rooms, soundnum, AL_VOL_FULL, 0);
		}

		if (overridden) {
			return;
		}
	}

	if (playdefault) {
		handle = bgunAllocateAudioHandle();

		if (handle != NULL && texturenum >= 0 && texturenum < TEXTURE_CUSTOM_END) {
			s16 soundnum;
			struct surfacetype *type = g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype];

			if (type->numsounds > 0) {
				soundnum = -1;

				if (type != NULL) {
					s32 index = rand2 % type->numsounds;
					soundnum = type->sounds[index];
					sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
				}

				if (*handle != NULL) {
					psApplyVolPan(*handle, hitpos, 400, 2500, 3000, rooms, soundnum, AL_VOL_FULL, 0);
				}
			}
		}
	}
#else
	struct sndstate **handle;
	u32 rand1 = rngRandom();
	u32 rand2 = rngRandom();
	s16 soundnum;
	bool overridden;

	if (g_Vars.lvupdate240 <= 0) {
		return;
	}

	if (texturenum >= 0 && texturenum < TEXTURE_CUSTOM_END && g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype]->numsounds == 0) {
		return;
	}

	handle = bgunAllocateAudioHandle();

	if (handle) {
		overridden = false;

		if (gset->weaponnum == WEAPON_LASER) {
			if (gset->weaponfunc == FUNC_PRIMARY || (gset->unk063a % 8) == 0) {
				// Laser sounds
				s16 sounds[] = {SFX_CLOAK_ON, SFX_CLOAK_OFF};
				soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
				sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
				overridden = true;
			}
		} else if (gset->weaponnum == WEAPON_COMBATKNIFE || gset->weaponnum == WEAPON_BOLT) {
			// Knives and bolts make a metal sound
			soundnum = SFX_HIT_METAL_8079;
			sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			overridden = true;
		} else if (gset->weaponnum == WEAPON_REMOTEMINE
				|| gset->weaponnum == WEAPON_PROXIMITYMINE
				|| gset->weaponnum == WEAPON_TIMEDMINE
				|| gset->weaponnum == WEAPON_COMMSRIDER
				|| gset->weaponnum == WEAPON_TRACERBUG
				|| gset->weaponnum == WEAPON_TARGETAMPLIFIER
				|| gset->weaponnum == WEAPON_ECMMINE) {
			// Mine landing/activation sound
			sndStart(var80095200, SFX_80AA, handle, -1, -1, -1, -1, -1);
			overridden = true;
		} else {
			// Ricochet sounds
			s16 sounds[] = {
				SFX_0013, SFX_0014, SFX_0015, SFX_0016,
				SFX_0017, SFX_0018, SFX_0019, SFX_001A,
				SFX_0017, SFX_0018, SFX_0019, SFX_001A,
				SFX_0017, SFX_0018, SFX_0019, SFX_001A,
				SFX_001F, SFX_0020, SFX_0020, SFX_0021,
				SFX_001F, SFX_0020, SFX_0020, SFX_0021,
				SFX_001F, SFX_0020, SFX_0020, SFX_0021,
				SFX_0023, SFX_0024, SFX_0025, SFX_0026,
				SFX_0027, SFX_0028, SFX_0029, SFX_002A,
			};

			soundnum = sounds[rand1 % ARRAYCOUNT(sounds)];
			sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			overridden = false;
		}

		if (*handle != NULL) {
			psApplyVolPan(*handle, hitpos, 400, 2500, 3000, rooms, soundnum, AL_VOL_FULL, 0);
		}

		if (overridden) {
			return;
		}
	}

	// Play default surface hit sound
	handle = bgunAllocateAudioHandle();

	if (handle != NULL && texturenum >= 0 && texturenum < TEXTURE_CUSTOM_END) {
		s16 soundnum;
		struct surfacetype *type = g_SurfaceTypes[texGetDefinition(texturenum)->soundsurfacetype];

		if (type->numsounds > 0) {
			soundnum = -1;

			if (type != NULL) {
				s32 index = rand2 % type->numsounds;
				soundnum = type->sounds[index];
				sndStart(var80095200, soundnum, handle, -1, -1, -1, -1, -1);
			}

			if (*handle != NULL) {
				psApplyVolPan(*handle, hitpos, 400, 2500, 3000, rooms, soundnum, AL_VOL_FULL, 0);
			}
		}
	}
#endif
}

/* GGV Trigger services begin: raw BOOL32 stores then existing GraphV2 boundary. */
struct bguntriggercontext { s32 handnum; bool on; struct hand *hand; };
static ggv_value bgunTriggerRead(void *opaque, ggv_binding binding)
{
    struct bguntriggercontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_TRIGGER: value.b = c->hand->triggeron; break;
    case GGV_TRIGGER_PREVIOUS: value.b = c->hand->triggerprev; break;
    case GGV_TRIGGER_REQUEST: value.b = c->on; break;
    case GGV_TRIGGER_PLAYER: value.ref = g_Vars.currentplayer; break;
    case GGV_HAND_NUMBER: value.i = c->handnum; break;
    default: break;
    } return value;
}
static void bgunTriggerWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bguntriggercontext *c = opaque;
    switch (binding) {
    case GGV_TRIGGER_PREVIOUS: c->hand->triggerprev = value.b; break;
    case GGV_TRIGGER: c->hand->triggeron = value.b; break;
    case GGV_TRIGGER_RELEASED: c->hand->triggerreleased = value.b; break;
    default: break;
    }
}
static ggv_value bgunTriggerCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bguntriggercontext *c = opaque; ggv_value value = {0};
    if (service == GGV_STATE_SELECT_HAND) c->hand = &g_Vars.currentplayer->hands[c->handnum];
    return value;
}
static ggv_value bgunTriggerCall4(void *opaque, ggv_service4 service, ggv_value a, ggv_value b, ggv_value c, ggv_value d)
{
    ggv_value value = {0}; (void) opaque;
    if (service == GGV_TRIGGER_V2_INPUT) wgV2RuntimeInput(a.ref, b.i, c.b, d.b);
    return value;
}
/* GGV Trigger services end. */
void bgunSetTriggerOn(s32 handnum, bool on)
{
    struct bguntriggercontext context = {0}; context.handnum = handnum; context.on = on;
    ggv_host host = { &context, bgunTriggerRead, bgunTriggerWrite, bgunTriggerCall, bgunTriggerCall4 };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_TRIGGER_INPUT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: trigger input refused immutable program");
    }
}

_Static_assert(WEAPON_CUSTOM_COUNT <= 32,
		"custom weapon function selections must fit the runtime bitset");


struct bgunstoredfunctioncontext {
    s32 weaponnum;
    u32 byteindex;
};
static ggv_value bgunStoredFunctionRead(void *opaque, ggv_binding binding)
{
    struct bgunstoredfunctioncontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_QUERY_WEAPON_I32: value.i = c->weaponnum; break;
    case GGV_FUNCTION_NATIVE_FIRST: value.i = WEAPON_UNARMED; break;
    case GGV_FUNCTION_NATIVE_LAST: value.i = WEAPON_COMBATBOOST; break;
    case GGV_FUNCTION_CUSTOM_FIRST: value.i = WEAPON_CUSTOM_START; break;
    case GGV_FUNCTION_CUSTOM_END: value.i = WEAPON_CUSTOM_END; break;
    case GGV_FUNCTION_NATIVE_BITS:
        value.u = g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].gunfuncs[c->byteindex]; break;
    case GGV_FUNCTION_CUSTOM_BITS: value.u = g_Vars.currentplayer->gunctrl.customgunfuncs; break;
    default: break;
    }
    return value;
}
static void bgunStoredFunctionWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunstoredfunctioncontext *c = opaque;
    if (binding == GGV_FUNCTION_BYTE_INDEX) c->byteindex = value.u;
}
static bool bgunCanStoreFunctionSelection(s32 weaponnum)
{
    struct bgunstoredfunctioncontext context = { weaponnum, 0 };
    ggv_host host = { &context, bgunStoredFunctionRead, bgunStoredFunctionWrite, NULL };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_CAN_STORE_FUNCTION), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: function selection eligibility refused immutable program");
    }
    return result;
}

static bool bgunStoredFunctionIsSecondary(s32 weaponnum)
{
    struct bgunstoredfunctioncontext context = { weaponnum, 0 };
    ggv_host host = { &context, bgunStoredFunctionRead, bgunStoredFunctionWrite, NULL };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_STORED_FUNCTION_SECONDARY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: stored function query refused immutable program");
    }
    return result;
}

struct bgunstorefunctioncontext {
    struct bgunstoredfunctioncontext query;
    bool secondary;
    u8 *nativebits;
    u32 *custombits;
};
static ggv_value bgunStoreFunctionRead(void *opaque, ggv_binding binding)
{
    struct bgunstorefunctioncontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_FUNCTION_SECONDARY_INPUT: value.b = c->secondary; break;
    case GGV_FUNCTION_NATIVE_BITS: value.u = c->nativebits[c->query.byteindex]; break;
    case GGV_FUNCTION_CUSTOM_BITS: value.u = *c->custombits; break;
    default: return bgunStoredFunctionRead(&c->query, binding);
    }
    return value;
}
static void bgunStoreFunctionWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunstorefunctioncontext *c = opaque;
    switch (binding) {
    case GGV_FUNCTION_BYTE_INDEX: c->query.byteindex = value.u; break;
    case GGV_FUNCTION_NATIVE_BITS: c->nativebits[c->query.byteindex] = (u8)value.u; break;
    case GGV_FUNCTION_CUSTOM_BITS: *c->custombits = value.u; break;
    default: break;
    }
}
static ggv_value bgunStoreFunctionCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunstorefunctioncontext *c = opaque; ggv_value value = {0}; (void)a; (void)b; (void)d;
    if (service == GGV_FUNCTION_CAPTURE_NATIVE) {
        c->nativebits = g_PlayerConfigsArray[g_Vars.currentplayerstats->mpindex].gunfuncs;
    } else if (service == GGV_FUNCTION_CAPTURE_CUSTOM) {
        c->custombits = &g_Vars.currentplayer->gunctrl.customgunfuncs;
    }
    return value;
}
static void bgunStoreFunctionSelection(s32 weaponnum, bool secondary)
{
    struct bgunstorefunctioncontext context = { { weaponnum, 0 }, secondary, NULL, NULL };
    ggv_host host = { &context, bgunStoreFunctionRead, bgunStoreFunctionWrite, bgunStoreFunctionCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_STORE_FUNCTION_SELECTION), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: function selection store refused immutable program");
    }
}

#define FUNCISSEC() bgunStoredFunctionIsSecondary(g_Vars.currentplayer->gunctrl.weaponnum)
#define SETFUNCPRI() bgunStoreFunctionSelection(g_Vars.currentplayer->gunctrl.weaponnum, false)
#define SETFUNCSEC() bgunStoreFunctionSelection(g_Vars.currentplayer->gunctrl.weaponnum, true)

/**
 * This is called once B has been held for 25 ticks, or earlier if pressing B+Z.
 *
 * The function may choose whether to change the gun function,
 * or activate the secondary function without switching to it.
 *
 * Return the following;
 * - USETIMER_CONTINUE if the B button timer should continue incrementing.
 * - USETIMER_STOP if the B button timer should stop (ie. the B press is consumed)
 * - USETIMER_REPEAT if this function should be called again on each frame until B is released.
 */
struct bgunfunctiontogglecontext {
    s32 usedowntime;
    bool trigpressed;
    bool fromactivemenu;
    bool fromdedicatedbutton;
};
static ggv_value bgunFunctionToggleRead(void *opaque, ggv_binding binding)
{
    struct bgunfunctiontogglecontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_FUNCTION_EXT_CONTROLS: value.b = PLAYER_EXTCFG().extcontrols != 0; break;
    case GGV_FUNCTION_REMOTE_PLAYER: value.b = g_Vars.currentplayer->isremote != 0; break;
    case GGV_FUNCTION_USE_TIME: value.i = c->usedowntime; break;
    case GGV_TRIGGER: value.b = c->trigpressed; break;
    case GGV_FUNCTION_ACTIVE_MENU: value.b = c->fromactivemenu; break;
    case GGV_FUNCTION_DEDICATED_BUTTON: value.b = c->fromdedicatedbutton; break;
    case GGV_FUNCTION_HOLD_THRESHOLD: value.i = TICKS(50); break;
    case GGV_FUNCTION_CONTROL_INVERT: value.i = g_Vars.currentplayer->gunctrl.invertgunfunc; break;
    case GGV_FUNCTION_CONTROL_WEAPON: value.i = g_Vars.currentplayer->gunctrl.weaponnum; break;
    case GGV_RELOAD_DEVICES_ACTIVE: value.u = g_Vars.currentplayer->devicesactive; break;
    default: break;
    }
    return value;
}
static void bgunFunctionToggleWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque;
    switch (binding) {
    case GGV_FUNCTION_CONTROL_INVERT: g_Vars.currentplayer->gunctrl.invertgunfunc = value.i; break;
    case GGV_HAND_ACTIVATE_SECONDARY: g_Vars.currentplayer->hands[HAND_RIGHT].activatesecondary = value.b; break;
    case GGV_RELOAD_DEVICES_ACTIVE: g_Vars.currentplayer->devicesactive = value.u; break;
    default: break;
    }
}
static ggv_value bgunFunctionToggleCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)d;
    switch (service) {
    case GGV_INFO_WEAPON_QUERY: value.i = bgunGetWeaponNum(HAND_RIGHT); break;
    case GGV_HAND_CURRENT_FUNCTION: value.i = g_Vars.currentplayer->hands[HAND_RIGHT].gset.weaponfunc; break;
    case GGV_FUNCTION_CAN_STORE: value.b = bgunCanStoreFunctionSelection(a.i); break;
    case GGV_STORED_FUNCTION: value.b = bgunStoredFunctionIsSecondary(a.i); break;
    case GGV_FUNCTION_STORE: bgunStoreFunctionSelection(a.i, b.b); break;
    case GGV_FUNCTION_IS_SECONDARY: value.i = bgunIsUsingSecondaryFunction(); break;
    default: break;
    }
    return value;
}
s32 bgunConsiderToggleGunFunction(s32 usedowntime, bool trigpressed, bool fromactivemenu, bool fromdedicatedbutton)
{
    struct bgunfunctiontogglecontext context = { usedowntime, trigpressed, fromactivemenu, fromdedicatedbutton };
    ggv_host host = { &context, bgunFunctionToggleRead, bgunFunctionToggleWrite, bgunFunctionToggleCall };
    int32_t result = USETIMER_CONTINUE;
    if (!ggvExecute(ggvStockProgram(GGV_CONSIDER_FUNCTION_TOGGLE), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: function toggle refused immutable program");
    }
    return result;
}

static ggv_value bgunResetFunctionRead(void *opaque, ggv_binding binding)
{
    ggv_value value = {0}; (void)opaque;
    switch (binding) {
    case GGV_FUNCTION_EXT_CONTROLS: value.b = PLAYER_EXTCFG().extcontrols != 0; break;
    case GGV_FUNCTION_REMOTE_PLAYER: value.b = g_Vars.currentplayer->isremote != 0; break;
    case GGV_HAND_ACTIVATE_SECONDARY: value.b = g_Vars.currentplayer->hands[HAND_RIGHT].activatesecondary; break;
    default: break;
    }
    return value;
}
static void bgunResetFunctionWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque;
    if (binding == GGV_FUNCTION_CONTROL_INVERT) g_Vars.currentplayer->gunctrl.invertgunfunc = value.i;
}
static ggv_value bgunResetFunctionCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)a; (void)b; (void)d;
    if (service == GGV_INFO_WEAPON_QUERY) value.i = bgunGetWeaponNum(HAND_RIGHT);
    return value;
}
void bgun0f0a8c50(void)
{
    ggv_host host = { NULL, bgunResetFunctionRead, bgunResetFunctionWrite, bgunResetFunctionCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_RESET_FUNCTION_INVERSION), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: function inversion reset refused immutable program");
    }
}

static ggv_value bgunUsingSecondaryRead(void *opaque, ggv_binding binding)
{
    struct player *player = opaque; ggv_value value = {0};
    if (binding == GGV_FUNCTION_CONTROL_WEAPON) value.i = player->gunctrl.weaponnum;
    else if (binding == GGV_FUNCTION_CONTROL_INVERT) value.i = player->gunctrl.invertgunfunc;
    return value;
}
static void bgunUsingSecondaryWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void)opaque; (void)binding; (void)value;
}
static ggv_value bgunUsingSecondaryCall(void *opaque, ggv_service service,
        ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value value = {0}; (void)opaque; (void)b; (void)d;
    if (service == GGV_STORED_FUNCTION) value.b = bgunStoredFunctionIsSecondary(a.i);
    return value;
}
bool bgunIsUsingSecondaryFunction(void)
{
    ggv_host host = { g_Vars.currentplayer, bgunUsingSecondaryRead, bgunUsingSecondaryWrite, bgunUsingSecondaryCall };
    int32_t result = 0;
    if (!ggvExecute(ggvStockProgram(GGV_USING_SECONDARY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: secondary predicate refused immutable program");
    }
    return result;
}

/**
 * Tick gun-related things during first-person gameplay.
 *
 * This function is not called during cutscenes.
 */
void bgunTickGameplay(bool triggeron)
{
	s32 gunsfiring[2] = {false, false};
	struct player *player = g_Vars.currentplayer;
	s32 i;

	/* B-246 round-2 instrumentation: log entry every ~120 frames for player 0,
	 * AND on the first 30 ticks of every match. Tells us whether bgunTickGameplay
	 * is even being called, what tickmode is, and whether the inner gate
	 * (tickmode==NORMAL && lvupdate240>0) is passing. The first round-1 fire-
	 * handler log was gated on `triggeron && !playertriggeron` (rising edge);
	 * if triggeron is always false the rising edge never fires and the log
	 * never emitted. This entry log is unconditional within the first 30 ticks
	 * so we always see whether the function runs. */
	if (g_Vars.currentplayernum == 0
			&& (g_Vars.lvframe60 < 30 || (g_Vars.lvframenum % 120) == 7)) {
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: bgunTickGameplay enter player=0 frame=%d tickmode=%d "
			"lvupdate240=%d triggeron_in=%d gunctrl_wpn=%d switchto=%d "
			"R(state=%d sm=%d cnt=%d inuse=%d) L(state=%d sm=%d cnt=%d inuse=%d)",
			(s32)g_Vars.lvframe60, (s32)g_Vars.tickmode,
			(s32)g_Vars.lvupdate240, (s32)triggeron,
			(s32)player->gunctrl.weaponnum, (s32)player->gunctrl.switchtoweaponnum,
			(s32)player->hands[HAND_RIGHT].state,
			(s32)player->hands[HAND_RIGHT].stateminor,
			(s32)player->hands[HAND_RIGHT].count,
			(s32)player->hands[HAND_RIGHT].inuse,
			(s32)player->hands[HAND_LEFT].state,
			(s32)player->hands[HAND_LEFT].stateminor,
			(s32)player->hands[HAND_LEFT].count,
			(s32)player->hands[HAND_LEFT].inuse);
	}

	/* B-246 instrumentation: log fire handler entry on first frame after the
	 * trigger toggles ON (rising edge). Captures the gate state at the exact
	 * moment a Mike-pressed FirePrimary action would have armed `triggeron`.
	 * Player 0 only; emit only on rising edge to avoid flooding the log. */
	if (g_Vars.currentplayernum == 0 && triggeron && !player->playertriggeron) {
		s32 hr_wpn = (s32)player->hands[HAND_RIGHT].gset.weaponnum;
		s32 hl_wpn = (s32)player->hands[HAND_LEFT].gset.weaponnum;
		s32 hr_inuse = (s32)player->hands[HAND_RIGHT].inuse;
		s32 hl_inuse = (s32)player->hands[HAND_LEFT].inuse;
		s32 hr_visible = (s32)player->hands[HAND_RIGHT].visible;
		s32 hl_visible = (s32)player->hands[HAND_LEFT].visible;
		s32 hr_state = (s32)player->hands[HAND_RIGHT].state;
		s32 hl_state = (s32)player->hands[HAND_LEFT].state;
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: fire handler enter player=0 triggeron=%d gunctrl_wpn=%d switchto=%d "
			"passive=%d tickmode=%d bondmovemode=%d ctrl=%d",
			(s32)triggeron,
			(s32)player->gunctrl.weaponnum,
			(s32)player->gunctrl.switchtoweaponnum,
			(s32)player->gunctrl.passivemode,
			(s32)g_Vars.tickmode,
			(s32)player->bondmovemode,
			(s32)g_PlayersWithControl[0]);
		sysLogPrintf(LOG_NOTE,
			"LOG.WPN.DIAG: fire handler hands player=0 R(wpn=%d inuse=%d visible=%d state=%d) "
			"L(wpn=%d inuse=%d visible=%d state=%d)",
			hr_wpn, hr_inuse, hr_visible, hr_state,
			hl_wpn, hl_inuse, hl_visible, hl_state);
	}

	// Remove weapons if in passive mode
	if (g_Vars.currentplayer->gunctrl.passivemode) {
		struct chrdata *chr = g_Vars.currentplayer->prop->chr;
		triggeron = false;

		if (invGetCount() > 1) {
			invClear();
			invGiveSingleWeapon(WEAPON_UNARMED);
		}

		if (g_Vars.currentplayer->gunctrl.weaponnum != WEAPON_UNARMED
				&& g_Vars.currentplayer->gunctrl.switchtoweaponnum != WEAPON_UNARMED) {
			bgunEquipWeapon(WEAPON_UNARMED);
		}

		g_Vars.currentplayer->gunctrl.dualwielding = false;
		g_Vars.currentplayer->devicesactive = 0;

		chr->cloakpause = 0;
		chr->cloakfadefrac = 0;
		chr->cloakfadefinished = false;
		chr->hidden &= ~CHRHFLAG_CLOAKED;
	}

	// Remove throwable items from inventory if there's no more left
	for (i = 0; i < invGetCount(); i++) {
		struct weapon *weapon;
		s32 weaponnum = invGetWeaponNumByIndex(i);
		s32 equippedweaponnum;

		switch (weaponnum) {
		case WEAPON_COMBATKNIFE:
		case WEAPON_GRENADE:
		case WEAPON_NBOMB:
		case WEAPON_COMBATBOOST:
		case WEAPON_CLOAKINGDEVICE:
		case WEAPON_ECMMINE:
		case WEAPON_COMMSRIDER:
		case WEAPON_TRACERBUG:
		case WEAPON_TARGETAMPLIFIER:
			weapon = weaponFindById(weaponnum);

			if (weapon && weapon->ammos[0]
					&& bgunGetAmmoCount(weapon->ammos[0]->type) == 0) {
				equippedweaponnum = bgunGetWeaponNum(HAND_RIGHT);
				invRemoveItemByNum(weaponnum);

				if (weaponnum == equippedweaponnum && !invHasSingleWeaponIncAllGuns(weaponnum)) {
					invCalculateCurrentIndex();
					bgunEquipWeapon(invGetWeaponNumByIndex(g_Vars.currentplayer->equipcuritem));
				}
			}
		}
	}

	if (g_Vars.tickmode == TICKMODE_CUTSCENE) {
		triggeron = false;
		g_Vars.currentplayer->hands[HAND_LEFT].firing = false;
		g_Vars.currentplayer->hands[HAND_RIGHT].firing = false;
	}

	player->playertriggerprev = player->playertriggeron;
	player->playertriggeron = triggeron;

	if (triggeron == false && player->playertriggerprev) {
		// Releasing trigger
		player->doautoselect = true;
	}

	// Handle gun firing - particularly alternating
	// between left and right if dual wielding
	if (player->playertriggeron) {
		player->playertrigtime240 += g_Vars.lvupdate240;

		if (player->hands[HAND_LEFT].inuse
				&& player->hands[HAND_RIGHT].inuse
				&& player->gunctrl.weaponnum != WEAPON_REMOTEMINE) {
			if (player->playertrigtime240 > TICKS(80)) {
				gunsfiring[player->curguntofire] = 1;

				if (bgun0f099008(1 - player->curguntofire)
						|| player->hands[1 - player->curguntofire].triggeron) {
					gunsfiring[1 - player->curguntofire] = 1;
				}
			} else {
				if (player->playertriggerprev == false &&
						(bgun0f099008(1 - player->curguntofire) || !bgun0f099008(player->curguntofire))) {
					player->curguntofire = 1 - player->curguntofire;
				}

				gunsfiring[player->curguntofire] = 1;
				gunsfiring[1 - player->curguntofire] = 0;
			}
		} else {
			if (!player->hands[player->curguntofire].inuse
					&& player->hands[1 - player->curguntofire].inuse) {
				player->curguntofire = 1 - player->curguntofire;
			}

			if (player->gunctrl.weaponnum == WEAPON_REMOTEMINE) {
				player->curguntofire = 0;
			}

			gunsfiring[player->curguntofire] = 1;
			gunsfiring[1 - player->curguntofire] = 0;
		}
	} else {
		player->playertrigtime240 = 0;
	}

	bgunSetTriggerOn(HAND_RIGHT, gunsfiring[0]);
	bgunSetTriggerOn(HAND_LEFT, gunsfiring[1]);

	if (g_Vars.tickmode == TICKMODE_NORMAL && g_Vars.lvupdate240 > 0) {
		/* B-246 root cause (2026-04-25): tick the master gun-memory loader
		 * here (move-side) in addition to the legacy render-side call in
		 * `bgunTickGameplay2`.  Mike's playtest log of `21010fbd` confirmed
		 * the FP weapon never appears in MP because the render-side path
		 * (playerRenderHud -> bgunTickGameplay2 -> bgunTickLoad) is being
		 * skipped for player 0 in some MP path -- the `LOG.WPN.DIAG:
		 * bgunRender enter` and `LOG.WPN.DIAG: visibility-gate-fail` diags
		 * (added in commits 6a9a23d8 / 568930c9) are SILENT across the
		 * entire 47 MB log, while the move-side `bgunTickGameplay enter`
		 * diag fires every frame.  The state machine wedges in
		 * CHANGEGUN/LOAD/MODE_6 because `bgun0f09bf44` returns false
		 * (`cond_loaded=0` and `cond_gunmemnew_neg=0`), which means
		 * `gunmemnew >= 0` -- a load is pending -- but the master loader
		 * is never ticked to clear it.
		 *
		 * Driving the master loader from the move-side guarantees one
		 * tick per frame whenever `tickmode == TICKMODE_NORMAL`, regardless
		 * of which render-side gates suppress `playerRenderHud`.  When the
		 * render-side path DOES run (SP, normal stages with no menu overlay)
		 * the loader gets a second tick per frame -- harmless because
		 * `bgunTickMasterLoad` is idempotent on its state-machine
		 * transitions and just advances slightly faster.  `loadall` gates
		 * the call exactly as the render-side does. */
		if (!g_Vars.currentplayer->gunctrl.loadall) {
			bgunTickLoad();
		}

		bgunTickHand(HAND_RIGHT);
		bgunTickHand(HAND_LEFT);
		bgunTickSwitch();

		if (cheatIsActive(CHEAT_UNLIMITEDAMMONORELOADS)) {
			s32 i;
			struct weapon *weapon;
			struct hand *lhand = &g_Vars.currentplayer->hands[HAND_LEFT];
			struct hand *rhand = &g_Vars.currentplayer->hands[HAND_RIGHT];

			weapon = weaponFindById(rhand->gset.weaponnum);

			for (i = 0; i != 2; i++) {
				if (weapon && weapon->ammos[i] &&
						bgunAmmotypeAllowsUnlimitedAmmo(weapon->ammos[i]->type)) {
					rhand->loadedammo[i] = rhand->clipsizes[i];
					lhand->loadedammo[i] = lhand->clipsizes[i];
				}
			}

			bgunGiveMaxAmmo(false);
		} else if (cheatIsActive(CHEAT_UNLIMITEDAMMO)) {
			bgunGiveMaxAmmo(false);
		}
	}

	bgunDecreaseNoiseRadius();
	if (player->isdead) {
		wgV2RuntimeRetireHand(player, HAND_RIGHT, "native owner died");
		wgV2RuntimeRetireHand(player, HAND_LEFT, "native owner died");
	}

	if (player->resetshadecol) {
		propCalculateShadeColour(g_Vars.currentplayer->prop, player->gunshadecol, player->floorcol);
		player->resetshadecol = 0;
	} else {
		u8 shadecol[4];
		propCalculateShadeColour(g_Vars.currentplayer->prop, shadecol, player->floorcol);
		colourTween(player->gunshadecol, shadecol);
	}

	invIncrementHeldTime(bgunGetWeaponNum(HAND_RIGHT), bgunGetWeaponNum(HAND_LEFT));
}

void bgunSetPassiveMode(bool enable)
{
	s32 i;
	bool was_p0 = (g_Vars.players[0] != NULL) ? (bool)g_Vars.players[0]->gunctrl.passivemode : false;

	for (i = 0; i < PLAYERCOUNT(); i++) {
		if (!g_Vars.players[i]) continue;
		g_Vars.players[i]->gunctrl.passivemode = enable;
	}

	/* B-246 round-6 instrumentation: passive mode disables fire and forces
	 * weapon switch to UNARMED. Log every call so any unexpected entry into
	 * passive mode during stage-4 firing is visible. */
	sysLogPrintf(LOG_NOTE,
		"LOG.WPN.DIAG: bgunSetPassiveMode players_p0_was=%d enable=%d",
		(s32)was_p0, (s32)enable);
}

void bgunSetAimType(u32 aimtype)
{
	g_Vars.currentplayer->aimtype = aimtype;
}

void bgunSetAimPos(struct coord *coord)
{
	struct player *player = g_Vars.currentplayer;

	player->hands[HAND_RIGHT].aimpos.x = handGetXShift(HAND_RIGHT) + coord->x;
	player->hands[HAND_RIGHT].aimpos.y = coord->y;
	player->hands[HAND_RIGHT].aimpos.z = coord->z;

	player->hands[HAND_LEFT].aimpos.x = handGetXShift(HAND_LEFT) + coord->x;
	player->hands[HAND_LEFT].aimpos.y = coord->y;
	player->hands[HAND_LEFT].aimpos.z = coord->z;
}

void bgunSetHitPos(struct coord *coord)
{
	struct player *player = g_Vars.currentplayer;

	player->hands[HAND_LEFT].hitpos.x = player->hands[HAND_RIGHT].hitpos.x = coord->x;
	player->hands[HAND_LEFT].hitpos.y = player->hands[HAND_RIGHT].hitpos.y = coord->y;
	player->hands[HAND_LEFT].hitpos.z = player->hands[HAND_RIGHT].hitpos.z = coord->z;
}

void bgun0f0a9494(u32 operation)
{
	switch (operation) {
	case 0:
		g_Vars.currentplayer->hands[HAND_LEFT].hasdotinfo = g_Vars.currentplayer->hands[HAND_RIGHT].hasdotinfo = false;
		break;
	case 1:
		break;
	}
}

void bgun0f0a94d0(u32 operation, struct coord *pos, struct coord *rot)
{
	struct player *player = g_Vars.currentplayer;

	switch (operation) {
	case 0:
		if (pos->x > -100000.0f && pos->x < 100000.0f
				&& pos->y > -100000.0f && pos->y < 100000.0f
				&& pos->z > -100000.0f && pos->z < 100000.0f) {
			player->hands[HAND_RIGHT].hasdotinfo = true;
			player->hands[HAND_LEFT].hasdotinfo = true;

			player->hands[HAND_LEFT].dotpos.x = player->hands[HAND_RIGHT].dotpos.x = pos->x;
			player->hands[HAND_LEFT].dotpos.y = player->hands[HAND_RIGHT].dotpos.y = pos->y;
			player->hands[HAND_LEFT].dotpos.z = player->hands[HAND_RIGHT].dotpos.z = pos->z;

			player->hands[HAND_LEFT].dotrot.x = player->hands[HAND_RIGHT].dotrot.x = rot->x;
			player->hands[HAND_LEFT].dotrot.y = player->hands[HAND_RIGHT].dotrot.y = rot->y;
			player->hands[HAND_LEFT].dotrot.z = player->hands[HAND_RIGHT].dotrot.z = rot->z;
		}
		break;
	case 1:
	case 2:
		lasersightSetDot(operation - 1, pos, rot);
		break;
	}
}

void bgunSetGunAmmoVisible(u32 reason, bool enable)
{
	if (enable) {
		g_Vars.currentplayer->gunammooff &= ~reason;
	} else {
		g_Vars.currentplayer->gunammooff |= reason;
	}
}

struct ammotype g_AmmoTypes[] = {
	{ 0,            0, 0  },
	{ 800,          0, 0  }, // AMMOTYPE_PISTOL
	{ 800,          0, 0  }, // AMMOTYPE_SMG
	{ 69,           0, 0  }, // AMMOTYPE_CROSSBOW
	{ 400,          0, -2 }, // AMMOTYPE_RIFLE
	{ 100,          0, 0  }, // AMMOTYPE_SHOTGUN
	{ 100,          0, 0  }, // AMMOTYPE_FARSIGHT
	{ 12,           0, 0  }, // AMMOTYPE_GRENADE
	{ 3,            0, -2 }, // AMMOTYPE_ROCKET
	{ 10,           0, 0  }, // AMMOTYPE_KNIFE
	{ 200,          0, 0  }, // AMMOTYPE_MAGNUM
	{ 40,           0, 0  }, // AMMOTYPE_DEVASTATOR
	{ 10,           0, 1  }, // AMMOTYPE_REMOTE_MINE
	{ 10,           0, 1  }, // AMMOTYPE_PROXY_MINE
	{ 10,           0, 1  }, // AMMOTYPE_TIMED_MINE
	{ 800,          0, 0  }, // AMMOTYPE_REAPER
	{ 15,           0, -2 }, // AMMOTYPE_HOMINGROCKET
	{ 50,           0, 0  }, // AMMOTYPE_DART
	{ 10,           0, 0  }, // AMMOTYPE_NBOMB
	{ 200,          0, 0  }, // AMMOTYPE_SEDATIVE
	{ TICKS(18000), 0, 0  }, // AMMOTYPE_CLOAK
	{ 4,            0, 0  }, // AMMOTYPE_BOOST
	{ 200,          0, 0  }, // AMMOTYPE_PSYCHOSIS
	{ 2,            0, 0  }, // AMMOTYPE_17
	{ 10,           0, 0  }, // AMMOTYPE_BUG
	{ 10,           0, 0  }, // AMMOTYPE_MICROCAMERA
	{ 10,           0, 0  }, // AMMOTYPE_PLASTIQUE
	{ 1000,         0, 0  }, // AMMOTYPE_1B
	{ 10,           0, 0  }, // AMMOTYPE_1C
	{ 50,           0, -1 }, // AMMOTYPE_1D
	{ 1,            0, 0  }, // AMMOTYPE_TOKEN
	{ 200,          0, 0  }, // AMMOTYPE_1F
	{ 10,           0, 0  }, // AMMOTYPE_ECM_MINE
};

/* GGV AmmoQuantity services begin: captured player, borrowed metadata. */
struct bgunquantitycontext { s32 ammotype; s32 quantity; s32 function; u32 weaponnum; u32 funcnum; u32 rawquantity; struct player *player; struct weapon *weapon; struct inventory_ammo *ammo; };
static ggv_value bgunQuantityRead(void *opaque, ggv_binding binding)
{
    struct bgunquantitycontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_QUANTITY_AMMO_REQUEST: value.i = c->ammotype; break;
    case GGV_QUANTITY_REQUEST: value.i = c->quantity; break;
    case GGV_QUANTITY_REQUEST_U32: value.u = c->rawquantity; break;
    case GGV_QUANTITY_LOADED_RIGHT: value.i = c->player->hands[HAND_RIGHT].loadedammo[c->function]; break;
    case GGV_QUANTITY_LOADED_LEFT: value.i = c->player->hands[HAND_LEFT].loadedammo[c->function]; break;
    case GGV_QUANTITY_CLIP_RIGHT: value.i = c->player->hands[HAND_RIGHT].clipsizes[c->function]; break;
    case GGV_QUANTITY_CAPACITY: value.i = g_AmmoTypes[c->ammotype].capacity; break;
    case GGV_QUERY_WEAPON_U32: value.u = c->weaponnum; break;
    case GGV_QUERY_AMMO_REF_U32: value.ref = c->weapon->ammos[c->funcnum]; break;
    case GGV_QUERY_CACHED_TYPE: value.u = c->ammo->type; break;
    default: break;
    } return value;
}
static void bgunQuantityWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunquantitycontext *c = opaque;
    switch (binding) {
    case GGV_QUANTITY_FUNCTION: c->function = value.i; break;
    case GGV_QUANTITY_LOADED_RIGHT: c->player->hands[HAND_RIGHT].loadedammo[c->function] = value.i; break;
    case GGV_QUANTITY_RESERVE: c->player->ammoheldarr[c->ammotype] = value.i; break;
    case GGV_QUERY_CACHED_AMMO: c->ammo = value.ref; break;
    default: break;
    }
}
static ggv_value bgunQuantityCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunquantitycontext *c = opaque; ggv_value value = {0};
    switch (service) {
    case GGV_AMMO_SELECT_PLAYER: c->player = g_Vars.currentplayer; break;
    case GGV_QUANTITY_CURRENT_WEAPON: value.i = bgunGetWeaponNum(a.i); break;
    case GGV_QUANTITY_AMMO_TYPE: value.u = bgunGetAmmoTypeForWeapon(a.u, b.u); break;
    case GGV_AMMO_HAS_FLAG: value.b = weaponHasAmmoFlag(a.i, b.i, d.u); break;
    case GGV_QUERY_FIND_WEAPON: c->weapon = weaponFindById(a.i); value.ref = c->weapon; break;
    case GGV_QUANTITY_APPLY: bgunSetAmmoQuantity(a.i, b.i); break;
    default: break;
    } return value;
}
/* GGV AmmoQuantity services end. */
void bgunSetAmmoQuantity(s32 ammotype, s32 quantity)
{
    struct bgunquantitycontext context = {0}; context.ammotype = ammotype; context.quantity = quantity;
    ggv_host host = { &context, bgunQuantityRead, bgunQuantityWrite, bgunQuantityCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SET_AMMO_QUANTITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo quantity refused immutable program");
    }
    // Check if this ammo type applies to the player's equipped weapon.
    // For cloak and combat boost, ammo cannot be held outside of the weapon.
    // So just add it to the loaded clip.
    // For throwable items, the capacity applies to reserve + loaded.
}

/* GGV AmmoCount services begin: fixed local traversal, borrowed player. */
struct bgunammocountcontext { s32 ammotype; struct player *player; s32 handindex; s32 slotindex; };
static ggv_value bgunAmmoCountRead(void *opaque, ggv_binding binding)
{
    struct bgunammocountcontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_AMMO_RESERVE: value.i = g_Vars.currentplayer->ammoheldarr[c->ammotype]; break;
    case GGV_AMMO_REQUEST: value.i = c->ammotype; break;
    case GGV_AMMO_HAND_INUSE: value.b = c->player->hands[c->handindex].inuse; break;
    case GGV_AMMO_EQUIPPED_TYPE: value.i = c->player->gunctrl.ammotypes[c->slotindex]; break;
    case GGV_AMMO_HAND_WEAPON: value.i = c->player->hands[c->handindex].gset.weaponnum; break;
    case GGV_AMMO_LOADED: value.i = c->player->hands[c->handindex].loadedammo[c->slotindex]; break;
    default: break;
    } return value;
}
static void bgunAmmoCountWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunammocountcontext *c = opaque;
    if (binding == GGV_AMMO_HAND_INDEX) c->handindex = value.i;
    if (binding == GGV_AMMO_SLOT_INDEX) c->slotindex = value.i;
}
static ggv_value bgunAmmoCountCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunammocountcontext *c = opaque; ggv_value value = {0};
    if (service == GGV_AMMO_SELECT_PLAYER) c->player = g_Vars.currentplayer;
    if (service == GGV_AMMO_HAS_FLAG) value.b = weaponHasAmmoFlag(a.i, b.i, d.u);
    return value;
}
/* GGV AmmoCount services end. */
s32 bgunGetReservedAmmoCount(s32 ammotype)
{
    struct bgunammocountcontext context = {0}; context.ammotype = ammotype;
    ggv_host host = { &context, bgunAmmoCountRead, bgunAmmoCountWrite, bgunAmmoCountCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_RESERVED_AMMO_COUNT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo count refused immutable program");
        return 0;
    }
    return result;
}

s32 bgunGetAmmoCount(s32 ammotype)
{
    struct bgunammocountcontext context = {0}; context.ammotype = ammotype;
    ggv_host host = { &context, bgunAmmoCountRead, bgunAmmoCountWrite, bgunAmmoCountCall };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_COUNT), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo count refused immutable program");
        return 0;
    }
    return result;
}

/* GGV AmmoQuery services begin: declared after the native capacity table. */
struct bgunammoquerycontext { s32 ammotype; u32 weaponnum; s32 signedweapon; u32 funcnum; s32 signedfunc; struct weapon *weapon; struct inventory_ammo *ammo; };
static ggv_value bgunAmmoQueryRead(void *opaque, ggv_binding binding)
{
    struct bgunammoquerycontext *c = opaque; ggv_value value = {0};
    switch (binding) {
    case GGV_QUERY_CAPACITY: value.i = g_AmmoTypes[c->ammotype].capacity; break;
    case GGV_QUERY_WEAPON_U32: value.u = c->weaponnum; break;
    case GGV_QUERY_WEAPON_I32: value.i = c->signedweapon; break;
    case GGV_QUERY_AMMO_REF_U32: value.ref = c->weapon->ammos[c->funcnum]; break;
    case GGV_QUERY_AMMO_REF_I32: value.ref = c->weapon->ammos[c->signedfunc]; break;
    case GGV_QUERY_AMMO_TYPE: value.u = c->weapon->ammos[c->funcnum]->type; break;
    case GGV_QUERY_CACHED_TYPE: value.u = c->ammo->type; break;
    case GGV_QUERY_CACHED_CAPACITY: value.i = g_AmmoTypes[c->ammo->type].capacity; break;
    default: break;
    } return value;
}
static void bgunAmmoQueryWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    struct bgunammoquerycontext *c = opaque;
    if (binding == GGV_QUERY_CACHED_AMMO) c->ammo = value.ref;
}
static ggv_value bgunAmmoQueryCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    struct bgunammoquerycontext *c = opaque; ggv_value value = {0};
    if (service == GGV_QUERY_FIND_WEAPON) { c->weapon = weaponFindById(a.i); value.ref = c->weapon; }
    if (service == GGV_QUERY_RESERVED_COUNT) value.i = bgunGetReservedAmmoCount(a.i);
    return value;
}
/* GGV AmmoQuery services end. */
s32 bgunGetCapacityByAmmotype(s32 ammotype)
{
    struct bgunammoquerycontext context = {0}; context.ammotype = ammotype;
    ggv_host host = { &context, bgunAmmoQueryRead, bgunAmmoQueryWrite, bgunAmmoQueryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_CAPACITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo query refused immutable program"); return 0;
    }
    return result;
}

/* GGV UnlimitedAmmo services begin: scalar input, lazy actual stage. */
static ggv_value bgunUnlimitedAmmoRead(void *opaque, ggv_binding binding)
{
    u32 *ammotype = opaque; ggv_value value = {0};
    if (binding == GGV_AMMO_REQUEST_U32) value.u = *ammotype;
    if (binding == GGV_AMMO_STAGE) value.i = g_Vars.stagenum;
    return value;
}
static void bgunUnlimitedAmmoWrite(void *opaque, ggv_binding binding, ggv_value value)
{
    (void) opaque; (void) binding; (void) value; /* Policy has no stores. */
}
/* GGV UnlimitedAmmo services end. */
bool bgunAmmotypeAllowsUnlimitedAmmo(u32 ammotype)
{
    ggv_host host = { &ammotype, bgunUnlimitedAmmoRead, bgunUnlimitedAmmoWrite, NULL };
    int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_UNLIMITED_AMMO), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: unlimited ammo policy refused immutable program");
        return false;
    }
    return result;
}

void bgunGiveMaxAmmo(bool force)
{
	s32 i;

	for (i = 0; i < ARRAYCOUNT(g_AmmoTypes); i++) {
		bool give = true;

		if (!force) {
			give = bgunAmmotypeAllowsUnlimitedAmmo(i);
		}

		if (give) {
			bgunSetAmmoQuantity(i, g_AmmoTypes[i].capacity);
		}
	}
}

u32 bgunGetAmmoTypeForWeapon(u32 weaponnum, u32 func)
{
    struct bgunammoquerycontext context = {0}; context.weaponnum = weaponnum; context.funcnum = func;
    ggv_host host = { &context, bgunAmmoQueryRead, bgunAmmoQueryWrite, bgunAmmoQueryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_TYPE_QUERY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo query refused immutable program"); return 0;
    }
    return result;
}

s32 bgunGetAmmoQtyForWeapon(u32 weaponnum, u32 func)
{
    struct bgunammoquerycontext context = {0}; context.weaponnum = weaponnum; context.funcnum = func;
    ggv_host host = { &context, bgunAmmoQueryRead, bgunAmmoQueryWrite, bgunAmmoQueryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_QTY_QUERY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo query refused immutable program"); return 0;
    }
    return result;
}

void bgunSetAmmoQtyForWeapon(u32 weaponnum, u32 func, u32 quantity)
{
    struct bgunquantitycontext context = {0}; context.weaponnum = weaponnum; context.funcnum = func; context.rawquantity = quantity;
    ggv_host host = { &context, bgunQuantityRead, bgunQuantityWrite, bgunQuantityCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_SET_WEAPON_AMMO_QUANTITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: weapon ammo quantity refused immutable program");
    }
}

s32 bgunGetAmmoCapacityForWeapon(s32 weaponnum, s32 func)
{
    struct bgunammoquerycontext context = {0}; context.signedweapon = weaponnum; context.signedfunc = func;
    ggv_host host = { &context, bgunAmmoQueryRead, bgunAmmoQueryWrite, bgunAmmoQueryCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_AMMO_WEAPON_CAPACITY), &host, &result)) {
        sysLogPrintf(LOG_WARNING, "GGV: ammo query refused immutable program"); return 0;
    }
    return result;
}

Gfx *bgunDrawHudString(Gfx *gdl, char *text, s32 x, bool halign, s32 y, s32 valign, u32 colour)
{
	s32 x1 = 0;
	s32 y1 = 0;
	s32 x2 = 0;
	s32 y2 = 0;
	s32 textheight;
	s32 textwidth;

	textwidth = 0;
	textheight = 0;

#if VERSION >= VERSION_JPN_FINAL
	textMeasure(&textheight, &textwidth, text, g_CharsNumeric, g_FontNumeric, -1);
#else
	textMeasure(&textheight, &textwidth, text, g_CharsNumeric, g_FontNumeric, 0);
#endif

	if (halign == HUDHALIGN_LEFT) { // left
		x2 = x + textwidth;
		x1 = x;
	} else if (halign == HUDHALIGN_RIGHT) { // right
		x1 = x - textwidth;
		x2 = x;
	} else if (halign == HUDHALIGN_MIDDLE) { // middle
		x2 = x + textwidth / 2;
		x1 = x2 - textwidth;
	}

	if (valign == HUDVALIGN_TOP) { // top
		y2 = y + textheight;
		y1 = y;
	} else if (valign == HUDVALIGN_BOTTOM) { // bottom
		y1 = y - textheight;
		y2 = y;
	} else if (valign == HUDVALIGN_MIDDLE) { // middle
		y2 = y + textheight / 2;
		y1 = y2 - textheight;
	}

	gdl = text0f153858(gdl, &x1, &y1, &x2, &y2);
	gdl = textRender(gdl, &x1, &y1, text, g_CharsNumeric, g_FontNumeric, colour, 0x000000a0, viGetWidth(), viGetHeight(), 0, 0);

	return gdl;
}

Gfx *bgunDrawHudInteger(Gfx *gdl, s32 value, s32 x, bool halign, s32 y, s32 valign, u32 colour)
{
	char buffer[12];

	snprintf(buffer, sizeof(buffer), "%d\n", value);
	gdl = bgunDrawHudString(gdl, buffer, x, halign, y, valign, colour);

	return gdl;
}

static ggv_value bgunAbmagRead(void *opaque, ggv_binding binding)
{ ggv_value v = {0}; return v; }
static void bgunAbmagWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct abmag *abmag = opaque;
    switch (binding) {
    case GGV_ABMAG_LOADED: abmag->loadedammo = v.i; break;
    case GGV_ABMAG_CHANGE: abmag->change = v.i; break;
    case GGV_ABMAG_REF: abmag->ref = v.i; break;
    case GGV_ABMAG_TIMER: abmag->timer60 = v.i; break;
    default: break;
    }
}
void bgunResetAbmag(struct abmag *abmag)
{
    ggv_host host = { abmag, bgunAbmagRead, bgunAbmagWrite, NULL }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_ABMAG_RESET), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: ammo display reset refused immutable program");
}

void bgun0f0a9da8(struct abmag *mag, s32 remaining, s32 capacity, s32 height)
{
	s32 newchange;

	if (capacity > 20) {
		s32 newremaining = height * remaining / capacity;

		if (remaining > 0 && newremaining < 1) {
			newremaining = 1;
		}

		capacity = height;

		if (newremaining == mag->ref && mag->loadedammo > remaining) {
			mag->ref++;
		}

		mag->loadedammo = remaining;
		remaining = newremaining;
	}

	newchange = remaining - mag->ref;

	if ((mag->change < 0 && newchange > 0) || (mag->change > 0 && newchange < 0)) {
		mag->ref += mag->change;
		mag->change = 0;
		mag->timer60 = 0;

		newchange = remaining - mag->ref;
	}

	if (mag->change < 0 && mag->change > newchange) {
		if (mag->timer60 > -mag->change * TICKS(64)) {
			mag->timer60 = -mag->change * TICKS(64);
		}
	}

	mag->change = newchange;

	if (mag->change > 0) {
		height = capacity;

		if (capacity < 6) {
			height = 6;
		}
	} else {
		height = 8;

		if (mag->change < -3) {
			height += -mag->change * 2;
		}
	}

	if (mag->change != 0) {
		mag->timer60 += (s16)g_Vars.lvupdate60 * height;

		if (mag->timer60 > TICKS(255)) {
			if (mag->change > 0) {
				while (mag->timer60 > TICKS(255) && mag->change > 0) {
					mag->change--;
					mag->ref++;
					mag->timer60 -= TICKS(64);
				}
			} else {
				while (mag->timer60 > TICKS(255) && mag->change < 0) {
					mag->change++;
					mag->ref--;
					mag->timer60 -= TICKS(64);
				}
			}
		}
	} else {
		mag->timer60 = 0;
	}
}

/**
 * Render an ammo gauge on the HUD.
 *
 * Ammo gauges can be displayed in two ways. If the capacity is less than 20,
 * each bullet is displayed as a separate block with a gap between each. If the
 * capacity is 20 or more, a single block covers the whole gauge and the block
 * is partitioned into two (filled and empty).
 *
 * For the separated mode, a unit refers to a single bullet/block.
 * For the merged mode, a unit refers to a single 1px high line in the gauge.
 */
Gfx *bgunDrawHudGauge(Gfx *gdl, s32 x1, s32 y1, s32 x2, s32 y2, struct abmag *abmag, s32 remaining, s32 capacity, u32 emptycolour, u32 filledcolour, bool flip)
{
	s32 gaugeheight = y2 - y1;
	s32 unitheight;
	s32 remainder1;
	s32 remainder2;
	s32 gaugetop;
	f32 ref;
	s32 numunits = capacity;
	s32 i;

	bgun0f0a9da8(abmag, remaining, numunits, gaugeheight);

	if (numunits > 20) {
		// Use a single merged bar
		ref = abmag->ref;
		unitheight = 1;
		numunits = gaugeheight;
		gaugetop = y2 - gaugeheight;
	} else {
		// Use a separate block for each bullet
		ref = abmag->ref;
		unitheight = gaugeheight / numunits;
		remainder1 = unitheight * numunits - gaugeheight;
		remainder2 = (unitheight + 1) * numunits - gaugeheight;

		if (remainder1 < 0) {
			remainder1 = -remainder1;
		}

		if (remainder2 < 0) {
			remainder2 = -remainder2;
		}

		if (remainder2 < remainder1) {
			unitheight++;
		}

		gaugetop = y2 - unitheight * capacity + 1;

		if (unitheight <= 2) {
			gaugetop--;
		}
	}

	if (unitheight == 0) {
		/**
		 * Using separate blocks, but the clip capacity is more than the gauge
		 * height meaning each block is less than 1px. This is impossible
		 * because the gauge switches modes away from separate blocks at 20,
		 * therefore this code is unreachable.
		 *
		 * This code renders the gauge in the merged style, but uses 1px per
		 * bullet and truncates the gauge at the gaugetop if needed. This is
		 * clearly an early revision of the code, as it is visually misleading
		 * and also lacks the transition effect.
		 */
		s32 partitiony;
		s32 tmp;

		gaugeheight = y2 - gaugetop;
		partitiony = y2 - gaugeheight * ref / numunits;
		tmp = y2;

		if (partitiony > gaugetop) {
			// Render empty partition
			gdl = textSetPrimColour(gdl, emptycolour);

			if (flip) {
				gDPFillRectangleScaled(gdl++, x1, y2 - partitiony + y1, x2, gaugeheight + y1);
			} else {
				gDPFillRectangleScaled(gdl++, x1, gaugetop, x2, partitiony);
			}

			gdl = text0f153838(gdl);
		}

		// Render filled partition
		gdl = textSetPrimColour(gdl, filledcolour);

		if (flip) {
			gDPFillRectangleScaled(gdl++, x1, y2 - tmp + y1, x2, y2 - partitiony + y1);
		} else {
			gDPFillRectangleScaled(gdl++, x1, partitiony, x2, y2);
		}
	} else {
		u32 colour;
		s32 unittop;
		s32 unitbottom;

		gdl = textSetPrimColour(gdl, emptycolour);

		unittop = gaugetop;
		unitbottom = -1;

		for (i = 0; i < numunits; i++) {
			bool newstate = false;
			u32 weight;

			if (1);

			if (abmag->change > 0) {
				// Loading or reloading
				if (i >= numunits - (s32)ref - abmag->change && i < numunits - (s32)ref) {
					// Unit is potentially unsettled
					s32 fadeamount = abmag->timer60 - (numunits - (s32)ref - i - 1) * TICKS(64);

					if (fadeamount >= 0) {
						if (fadeamount >= TICKS(64)) {
							// Unit is transitioning to filled
#if PAL
							weight = (fadeamount * 4 - TICKS(250)) / 3;
#else
							weight = (fadeamount * 4 - TICKS(252)) / 3;
#endif
							weight = PALUP(weight);

							if (weight > 255) {
								weight = 255;
							}

							colour = colourBlend(filledcolour, 0xffffffbf, weight);
						} else {
							// Unit is bright and has not started transitioning to filled yet
							weight = fadeamount * 4;
							weight = PALUP(weight);

#if VERSION >= VERSION_PAL_BETA
							if (weight > 255) {
								weight = 255;
							}
#endif

							colour = colourBlend(0xffffffbf, emptycolour, weight);
						}

						newstate = true;
					}
				}
			} else if (abmag->change < 0) {
				// Firing
				if (i < numunits - (s32)ref - abmag->change && i >= numunits - (s32) ref) {
					s32 fadeamount = abmag->timer60 - (i - numunits + (s32) ref) * TICKS(64);

					if (fadeamount >= 0) {
						weight = PALUP(fadeamount);

						if (weight > 255) {
							colour = emptycolour;
						} else {
							// Unit was recently emptied
							colour = colourBlend(emptycolour, filledcolour | 0xff, weight);
						}

						newstate = true;
					}
				}
			}

			// Special case for units which are one after the last one being
			// faded. I think their colour is calculated incorrectly by the code
			// above and this is resetting them to the normal filled colour.
			if (abmag->change < 0) {
				// Firing
				if (i == numunits - (s32) ref - abmag->change) {
					colour = filledcolour;
					newstate = true;
				}
			} else {
				if (i == numunits - (s32) ref) {
					colour = filledcolour;
					newstate = true;
				}
			}

			// Calculate unittop and unitbottom. For merged gauges keep unittop
			// as it is if possible, so the empty and filled partitions can be
			// drawn whenever the state is changed in order to save gfx calls.
			if (unitheight <= 2) {
				if (newstate) {
					if (unitbottom >= 0) {
						// Render empty or transitioning unit of merged gauge
						if (flip) {
							gDPFillRectangleScaled(gdl++, x1, y2 - unitbottom + y1, x2, y2 - unittop + y1);
						} else {
							gDPFillRectangleScaled(gdl++, x1, unittop, x2, unitbottom);
						}
					}

					unittop = gaugetop + i * unitheight;
				}

				unitbottom = gaugetop + i * unitheight + unitheight;
			} else {
				// Separate blocks - reduce unitbottom by 1 to make a gap
				unittop = gaugetop + i * unitheight;
				unitbottom = gaugetop + i * unitheight + unitheight - 1;
			}

			if (newstate) {
				gDPSetPrimColorViaWord(gdl++, 0, 0, colour);
			}

			// For separate blocks, clip the unit bottom to the bottom of the gauge
			if (unitbottom >= y2 - 1 && unitheight >= 2) {
				unitbottom = y2;
			}

			// Render separated blocks
			if (unitheight >= 3) {
				if (flip) {
					gDPFillRectangleScaled(gdl++, x1, y2 - unitbottom + y1, x2, y2 - unittop + y1);
				} else {
					gDPFillRectangleScaled(gdl++, x1, unittop, x2, unitbottom);
				}
			}
		} // end loop

		// For merged gauges, render the final partition
		if (unitheight <= 2) {
			s32 stack;

			if (flip) {
				gDPFillRectangleScaled(gdl++, x1, y2 - unitbottom + y1, x2, y2 - unittop + y1);
			} else {
				gDPFillRectangleScaled(gdl++, x1, unittop, x2, unitbottom);
			}
		}
	}

	gdl = text0f153838(gdl);

	gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

	return gdl;
}

Gfx *bgunDrawHud(Gfx *gdl)
{
	if (!assetRuntimeHudElementEnabled(HUD_ELEM_AMMO)) {
		return gdl;
	}
	struct player *player = g_Vars.currentplayer;
	s32 bottom = viGetViewTop() + viGetViewHeight() - 13;
	s32 playercount = LOCALPLAYERCOUNT();
	s32 playernum = g_Vars.currentplayernum;
	struct gunctrl *ctrl;
	s32 secs60;
	s32 speedpilltime;
	s32 ammoindex = 0;
	s32 barwidth = 9;
	s32 reserveheight = 36;
	s32 clipheight = 57;
	s32 xpos;
	s32 displayweaponnum = player->gunctrl.weaponnum;
	struct weapon *weapon = weaponFindById(displayweaponnum);
	u32 alpha;
	u32 fncolour;
	s32 funcnum;
	s32 fnfaderinc;
	s32 currentindex;
	s32 currentindexweaponnum;
#if VERSION >= VERSION_NTSC_1_0
	s32 tmpfuncnum;
	struct handweaponinfo info;
#endif
	struct hand *hand = &player->hands[HAND_RIGHT];
	char *str;
	u32 colour;
	s32 x;
	s32 y;
	s32 textheight;
	s32 textwidth;
	struct weaponfunc *func;
	u16 nameid;
	struct hand *lefthand = &player->hands[HAND_LEFT];

	ctrl = &player->gunctrl;

	if (player->isdead) {
		return gdl;
	}

	if (g_Vars.currentplayer->gunctrl.passivemode) {
		return gdl;
	}

	if (g_Vars.lvframenum < 5) {
		return gdl;
	}

#if PAL
	g_ScaleX = 1;
#else
	g_ScaleX = g_ViRes == VIRES_HI ? 2 : 1;
#endif

	gdl = text0f153628(gdl);

	if (playercount < 2 || (playercount == 2 && optionsGetScreenSplit() == SCREENSPLIT_HORIZONTAL)) {
		gSPExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT, g_HudAlignModeR);
	}

	if (playercount >= 2) {
		barwidth = 5;
		reserveheight = 26;
		clipheight = 47;

		if (playercount == 2) {
			if (optionsGetScreenSplit() != SCREENSPLIT_VERTICAL && playernum == 0) {
				bottom += 10;
			} else {
				bottom += 2;
			}
		} else if (playercount >= 3) {
			if (playernum < 2) {
				bottom += 10;
			} else {
				bottom += 2;
			}
		}
	} else if (optionsGetEffectiveScreenSize() != SCREENSIZE_FULL) {
		bottom += 8;
	}

	fncolour = 0xff000040;
	funcnum = hand->gset.weaponfunc;
	fnfaderinc = PALUP(g_Vars.lvupdate240 * 2);

#if VERSION >= VERSION_NTSC_1_0
	bgunGetWeaponInfo(&info, HAND_RIGHT);
	tmpfuncnum = bgunIsUsingSecondaryFunction();

	if (bgun0f098ca0(tmpfuncnum, &info, hand) >= 0) {
		funcnum = tmpfuncnum;
	}
#endif

	xpos = (viGetViewLeft() + viGetViewWidth()) / g_ScaleX - barwidth - 24;

	if (playercount == 2 && (optionsGetScreenSplit() == SCREENSPLIT_VERTICAL) && playernum == 0) {
		xpos += 15;
	} else if (playercount >= 3 && (playernum % 2) == 0) {
		xpos += 15;
	}

	// Draw function square
	if (funcnum == FUNC_SECONDARY && ctrl->fnfader < 255) {
		if (ctrl->fnfader < 128) {
			ctrl->fnfader = 128;
		}

		if (ctrl->fnfader + fnfaderinc > 255) {
			ctrl->fnfader = 255;
		} else {
			ctrl->fnfader += fnfaderinc;
		}
	}

	if (funcnum == FUNC_PRIMARY && ctrl->fnfader > 0) {
		if (ctrl->fnfader - fnfaderinc < 0) {
			ctrl->fnfader = 0;
		} else {
			ctrl->fnfader -= fnfaderinc;
		}
	}

	if (ctrl->fnfader > 128) {
		fncolour = ((ctrl->fnfader * 2) - 256) << 16 | 0xff000040;
	}

	gdl = textSetPrimColour(gdl, fncolour);

	gDPFillRectangleScaled(gdl++, xpos - 13, bottom - 11, xpos - 2, bottom);

	gdl = text0f153838(gdl);

	// Draw weapon name and function name
	if (optionsGetShowGunFunction(g_Vars.currentplayerstats->mpindex)) {
		func = weaponGetFunctionById(displayweaponnum, funcnum);
		currentindex = invGetCurrentIndex();
		currentindexweaponnum = invGetWeaponNumByIndex(currentindex);
		if (currentindexweaponnum == displayweaponnum) {
			nameid = invGetNameIdByIndex(currentindex);
		} else {
			nameid = bgunGetNameId(displayweaponnum);
		}
		str = langGet(nameid);

		if (ctrl->curgunstr != nameid) {
			ctrl->guntypetimer = 0;
			ctrl->curgunstr = nameid;
			ctrl->fnstrtimer = 0;
			ctrl->curfnstr = 0;
		}

		if (ctrl->guntypetimer < 255) {
			colour = 0x55ffffff;

			if (ctrl->guntypetimer);

			if (ctrl->guntypetimer + g_Vars.lvupdate60 > 255) {
				ctrl->guntypetimer = 255;
			} else {
				ctrl->guntypetimer += (u16) g_Vars.lvupdate60;
			}

			textMeasure(&textheight, &textwidth, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0);
			textwidth += 2;

			if (textwidth > ctrl->guntypetimer * 3) {
				textwidth = ctrl->guntypetimer * 3;
			}

			if (playercount >= 2) {
				x = xpos - textwidth - 13;
			} else {
				x = xpos - textwidth - 2;
			}

#if VERSION == VERSION_JPN_FINAL
			y = bottom - textheight - 10;
#else
			y = bottom - textheight - 15;
#endif

			if (ctrl->guntypetimer > 192) {
				alpha = 255 - (ctrl->guntypetimer - 192) * 255 / 63U;
				colour = (colour & 0xffffff00) | alpha;
				if (0xffffff00);
			}

			gdl = textSetPrimColour(gdl, 0);

			gDPFillRectangleScaled(gdl++, x - 1, y - 1, xpos - 11, bottom);

			gdl = text0f153838(gdl);
			textSetWaveBlend(g_20SecIntervalFrac * 50.0f, 0, 50);
			textSetWaveColours(0xffffffff, 0xffffffff);
			gdl = textRenderProjected(gdl, &x, &y, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, colour, textwidth, 1000, 0, 0);
			textResetBlends();
		}

		if (func) {
			langGet(func->name);

			colour = 0xff5555ff;

			if (ctrl->curfnstr != func->name) {
				ctrl->fnstrtimer = 0;
				ctrl->curfnstr = func->name;
			}

			str = langGet(ctrl->curfnstr);

			if (ctrl->fnstrtimer < 255) {
				if (ctrl->fnstrtimer + g_Vars.lvupdate60 > 255) {
					ctrl->fnstrtimer = 255;
				} else {
					ctrl->fnstrtimer += (u16) g_Vars.lvupdate60;
				}

#if VERSION >= VERSION_NTSC_1_0
				if (funcnum == FUNC_SECONDARY && func->name == ctrl->curfnstr) {
					colour |= 0x00ff0000;
				}

				if (funcnum == FUNC_PRIMARY && func->name != ctrl->curfnstr) {
					colour |= 0x00ff0000;
				}
#else
				if (hand->gset.weaponfunc == FUNC_SECONDARY && func->name == ctrl->curfnstr) {
					colour |= 0x00ff0000;
				}

				if (hand->gset.weaponfunc == FUNC_PRIMARY && func->name != ctrl->curfnstr) {
					colour |= 0x00ff0000;
				}
#endif

				textMeasure(&textheight, &textwidth, str, g_CharsHandelGothicXs, g_FontHandelGothicXs, 0);
				textwidth += 2;

				if (textwidth > ctrl->fnstrtimer * 3) {
					textwidth = ctrl->fnstrtimer * 3;
				}

				x = xpos - textwidth - 13;
#if VERSION == VERSION_JPN_FINAL
				y = bottom - textheight + 3;
#else
				y = bottom - textheight - 1;
#endif

				if (ctrl->fnstrtimer > 192) {
					alpha = 255 - (ctrl->fnstrtimer - 192) * 255 / 63U;
					colour = (colour & 0xffffff00) | alpha;
				}

				gdl = textSetPrimColour(gdl, 0);

				gDPFillRectangleScaled(gdl++, x - 1, y - 1, xpos - 11, bottom + 3);

				gdl = text0f153838(gdl);

				textSetWaveBlend(g_20SecIntervalFrac * 50.0f, 0, 50);
				textSetWaveColours(0xffffffff, 0xffffffff);

				gdl = textRenderProjected(gdl, &x, &y, str,
						g_CharsHandelGothicXs, g_FontHandelGothicXs, colour, textwidth,
						1000, 0, 0);

				textResetBlends();
			}
		}
	}

	if (weapon && weapon->functions[hand->gset.weaponfunc] != NULL) {
		ammoindex = ((struct weaponfunc *)(weapon->functions[hand->gset.weaponfunc]))->ammoindex;
	}

	if (ammoindex == -1) {
		if (weapon->functions[1 - hand->gset.weaponfunc] != NULL) {
			ammoindex = ((struct weaponfunc *)(weapon->functions[1 - hand->gset.weaponfunc]))->ammoindex;
		}

		if (ammoindex == -1) {
			gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT);
			gdl = text0f153780(gdl);
			g_ScaleX = 1;
			return gdl;
		}
	}

	if (ammoindex != ctrl->lastmag) {
		bgunResetAbmag(&player->hands[HAND_LEFT].abmag);
		bgunResetAbmag(&hand->abmag);
		bgunResetAbmag(&ctrl->abmag);
		ctrl->lastmag = ammoindex;
	}

	// Left hand - mag
	if (lefthand->inuse
			&& weapon->ammos[ammoindex] != NULL
			&& lefthand->gset.weaponnum != WEAPON_REMOTEMINE) {
		xpos = viGetViewLeft() / g_ScaleX + 24;

		if (playercount == 2 && (optionsGetScreenSplit() == SCREENSPLIT_VERTICAL) && playernum == 1) {
			xpos -= 14;
		} else if (playercount >= 3 && (playernum & 1) == 1) {
			xpos -= 14;
		}

		if (playercount < 2 || (playercount == 2 && optionsGetScreenSplit() == SCREENSPLIT_HORIZONTAL)) {
			gSPExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT, g_HudAlignModeL);
		}

		if (lefthand->clipsizes[ammoindex] > 0 && (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) == 0) {
			gdl = bgunDrawHudGauge(gdl,
					xpos, bottom - reserveheight - clipheight - 3, xpos + barwidth, bottom - reserveheight - 3,
					&lefthand->abmag, lefthand->loadedammo[ammoindex], lefthand->clipsizes[ammoindex],
					0x00300080, 0x00ff0040, false);
			gdl = bgunDrawHudInteger(gdl, lefthand->loadedammo[ammoindex], xpos + barwidth + 2, true,
					bottom - reserveheight - 8, 0, 0x00ff00a0);
		}
	}

	// Right hand - mag, reserve and combat boost timer
	if (hand->inuse && ctrl->ammotypes[ammoindex] >= 0) {
		s32 ammotype;
		s32 ammoheld;
		s32 ammototal;

		ammotype = player->gunctrl.ammotypes[ammoindex];

#if VERSION >= VERSION_NTSC_1_0
		xpos = (viGetViewLeft() + viGetViewWidth()) / g_ScaleX - barwidth - 24;
#else
		// NTSC Beta omits the brackets here. This would normally cause the
		// ammo info to be misaligned for players on the right side of the
		// screen and when using hi-res, but I'm not sure if hi-res can even be
		// active when using multiple players...
		xpos = viGetViewLeft() + viGetViewWidth() / g_ScaleX - barwidth - 24;
#endif

		if (playercount == 2 && (optionsGetScreenSplit() == SCREENSPLIT_VERTICAL) && playernum == 0) {
			xpos += 15;
		} else if (playercount >= 3 && (playernum % 2) == 0) {
			xpos += 15;
		}

		if (playercount < 2 || (playercount == 2 && optionsGetScreenSplit() == SCREENSPLIT_HORIZONTAL)) {
			gSPExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT, g_HudAlignModeR);
		}

		// Mag
		ammoheld = player->ammoheldarr[ammotype];

		if (hand->clipsizes[ammoindex] > 0
				&& weapon->ammos[ammoindex] != NULL
				&& (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) == 0) {
			gdl = bgunDrawHudGauge(gdl, xpos, bottom - reserveheight - clipheight - 3, xpos + barwidth,
					bottom - reserveheight - 3, &hand->abmag, hand->loadedammo[ammoindex], hand->clipsizes[ammoindex],
					0x00300080, 0x00ff0040, false);
			gdl = bgunDrawHudInteger(gdl, hand->loadedammo[ammoindex], xpos - 2, false,
					bottom - reserveheight - 8, 0, 0x00ff00a0);
		}

		// Reserve
		if (g_AmmoTypes[ammotype].capacity > 0
				&& (weapon->ammos[ammoindex]->flags & AMMOFLAG_NORESERVE) == 0) {
			ammototal = ammoheld;

			if (weapon->ammos[ammoindex]->flags & AMMOFLAG_EQUIPPEDISRESERVE) {
				if (hand->clipsizes[ammoindex] > 0) {
					ammototal += hand->loadedammo[ammoindex];
				}

				if (lefthand->clipsizes[ammoindex] > 0) {
					ammototal += lefthand->loadedammo[ammoindex];
				}
			}

			gdl = bgunDrawHudGauge(gdl, xpos, bottom - reserveheight, xpos + barwidth,
					bottom, &ctrl->abmag, ammototal, g_AmmoTypes[ammotype].capacity,
					0x00403080, 0x00ffc040, true);
			gdl = bgunDrawHudInteger(gdl, ammototal, xpos - 2, false, bottom - reserveheight + 1, 0, 0x00ffc0a0);
		}

		// Combat boost timer
		if (hand->gset.weaponnum == WEAPON_COMBATBOOST) {
			s32 mins;
			char text[32];

			speedpilltime = g_Vars.speedpilltime;
			mins = speedpilltime / TICKS(3600);
			secs60 = speedpilltime - mins * TICKS(3600);

			if (mins >= 1) {
				snprintf(text, sizeof(text), "%02d:%02d:%02d\n", mins, secs60 / TICKS(60), (secs60 - (secs60 / TICKS(60)) * TICKS(60)) * 100 / TICKS(60));
			} else {
				snprintf(text, sizeof(text), "%02d:%02d\n", secs60 / TICKS(60), (secs60 - (secs60 / TICKS(60)) * TICKS(60)) * 100 / TICKS(60));
			}

			gdl = bgunDrawHudString(gdl, text, xpos + barwidth - 2, false, bottom - reserveheight + 1, 0, 0x00ffc0a0);
		}
	}

	gSPClearExtraGeometryModeEXT(gdl++, G_ASPECT_MODE_EXT);

	gdl = text0f153780(gdl);

	g_ScaleX = 1;

	return gdl;
}

void bgunAddBoost(s32 amount)
{
	g_Vars.speedpilltime += amount;

	if (g_Vars.speedpilltime > 5 * 60 * TICKS(60)) { // 5 minutes
		g_Vars.speedpilltime = 5 * 60 * TICKS(60);
	}

	if (!g_Vars.speedpillwant) {
		u32 sound = lvGetSlowMotionType() ? SFX_ARGH_JO_02AD : SFX_JO_BOOST_ACTIVATE;

		sndStart(var80095200, sound, 0, -1, -1, -1, -1, -1);
	}

	g_Vars.speedpillwant = true;
}

void bgunSubtractBoost(s32 amount)
{
	g_Vars.speedpilltime -= amount;

	if (g_Vars.speedpilltime <= 0) {
		g_Vars.speedpilltime = 0;
		g_Vars.speedpillwant = false;
	}
}

void bgunApplyBoost(void)
{
	if (lvGetSlowMotionType() != SLOWMOTION_OFF) {
		bgunSubtractBoost(TICKS(1200));
	} else {
		bgunAddBoost(TICKS(600));
	}
}

void bgunRevertBoost(void)
{
	if (lvGetSlowMotionType() != SLOWMOTION_OFF) {
		bgunAddBoost(TICKS(1200));
	} else {
		bgunSubtractBoost(TICKS(600));
	}
}

/**
 * The main tick function as called from lvTick.
 *
 * This function doesn't do much because it's called during both cutscenes and
 * gameplay, while most gun tick operations happen during gameplay only.
 * See bgunTickGameplay for that.
 */
void bgunTickBoost(void)
{
	if (g_Vars.speedpillon && g_Vars.speedpilltime > 0 && !playerCurrentCutsceneInProgress()) {
		g_Vars.speedpilltime -= g_Vars.lvupdate60;

		if (g_Vars.speedpilltime <= 0) {
			g_Vars.speedpilltime = 0;
			g_Vars.speedpillwant = false;
		}
	}
}

/**
 * gunsightoff is 0 if the full sight is visible, ie. player is holding R.
 *
 * Otherwise, gunsightoff holds bit values for reasons why the sight is off.
 * This is typically 2, which is GUNSIGHTREASON_NOTAIMING.
 *
 * If the visible argument is true, it removes the reason from the field, thus
 * making the sight visible if there are no other reasons.
 */
void bgunSetSightVisible(u32 reason, bool visible)
{
	if (visible) {
		g_Vars.currentplayer->gunsightoff &= ~reason;
		return;
	}

	g_Vars.currentplayer->gunsightoff |= reason;
}

Gfx *bgunDrawSight(Gfx *gdl)
{
	pdguiWeaponReticleClear();
	if (!assetRuntimeHudElementEnabled(HUD_ELEM_CROSSHAIR)) {
		return gdl;
	}
	if (g_Vars.currentplayer->gunsightoff == 0 && !g_Vars.currentplayer->mpmenuon) {
		// Player is aiming with R
		gdl = sightDraw(gdl, true, currentPlayerGetSight());
	} else {
		gdl = sightDraw(gdl, false, currentPlayerGetSight());
	}

	return gdl;
}

struct bgunconfigurecontext { struct hand *hand; struct gunctrl *ctrl; struct weapon *weapon; s32 handnum, slot; };
static ggv_value bgunConfigureRead(void *opaque, ggv_binding binding)
{
    struct bgunconfigurecontext *c = opaque; ggv_value v = {0};
    switch (binding) {
    case GGV_VIS_WEAPON_ID: v.i = c->hand->gset.weaponnum; break;
    case GGV_VIS_WEAPON: v.ref = c->weapon; break;
    case GGV_VIS_AMMO: v.ref = c->weapon->ammos[c->slot]; break;
    case GGV_SWITCH_HAND_INDEX: v.i = c->handnum; break;
    case GGV_CONFIG_AMMO_TYPE: v.u = c->weapon->ammos[c->slot]->type; break;
    case GGV_CONFIG_AMMO_CLIP: v.i = c->weapon->ammos[c->slot]->clipsize; break;
    case GGV_SW_REMOTE: v.i = WEAPON_REMOTEMINE; break;
    case GGV_CONFIG_FIRST_AMMO: v.i = c->ctrl->ammotypes[0]; break;
    case GGV_CONFIG_HAND_ABMAG: v.ref = &c->hand->abmag; break;
    case GGV_CONFIG_CTRL_ABMAG: v.ref = &c->ctrl->abmag; break;
    default: break;
    } return v;
}
static void bgunConfigureWrite(void *opaque, ggv_binding binding, ggv_value v)
{
    struct bgunconfigurecontext *c = opaque;
    switch (binding) {
    case GGV_VIS_WEAPON: c->weapon = v.ref; break;
    case GGV_VIS_AMMO_SLOT: c->slot = v.i; break;
    case GGV_CONFIG_CTRL_AMMO: c->ctrl->ammotypes[c->slot] = v.i; break;
    case GGV_VIS_CLIP_SIZE: c->hand->clipsizes[c->slot] = v.i; break;
    case GGV_VIS_LOADED: c->hand->loadedammo[c->slot] = v.i; break;
    case GGV_CONFIG_UPGRADE0: c->hand->upgrademult[0] = v.f; break;
    case GGV_CONFIG_UPGRADE1: c->hand->upgrademult[1] = v.f; break;
    case GGV_CONFIG_FINAL0: c->hand->finalmult[0] = v.f; break;
    case GGV_CONFIG_FINAL1: c->hand->finalmult[1] = v.f; break;
    case GGV_CONFIG_LASTMAG: c->ctrl->lastmag = v.i; break;
    default: break;
    }
}
static ggv_value bgunConfigureCall(void *opaque, ggv_service service, ggv_value a, ggv_value b, ggv_value d)
{
    ggv_value v = {0};
    switch (service) {
    case GGV_QUERY_FIND_WEAPON: v.ref = weaponFindById(a.i); break;
    case GGV_CONFIG_RESET_ABMAG: bgunResetAbmag(a.ref); break;
    default: break;
    } return v;
}
void bgun0f0abd30(s32 handnum)
{
    struct player *player = g_Vars.currentplayer;
    struct bgunconfigurecontext context = { &player->hands[handnum], &player->gunctrl, NULL, handnum, 0 };
    ggv_host host = { &context, bgunConfigureRead, bgunConfigureWrite, bgunConfigureCall }; int32_t result;
    if (!ggvExecute(ggvStockProgram(GGV_HAND_CONFIGURE), &host, &result))
        sysLogPrintf(LOG_WARNING, "GGV: hand configuration refused immutable program");
}
