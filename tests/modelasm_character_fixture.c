#include <string.h>
#include <ultra64.h>
#include "bss.h"
#include "constants.h"
#include "data.h"
#include "lib/anim.h"
#include "lib/model.h"

/* Compile the production matrix builder without replacing CRT trigonometry
 * for the other tests in this executable. */
#define sinf testModelasmSinf
#define cosf testModelasmCosf
static f32 testModelasmSinf(f32 radians);
static f32 testModelasmCosf(f32 radians);
#include "../src/lib/modelasm_c.c"
#undef cosf
#undef sinf

/* Two valid parts, each with an eight-bit X translation and zero Y and Z. */
static u8 s_TestHeader[] = {
	ANIMFIELD_S16_TRANSLATE, 0, 0, 8, 0, 0, 0, 0, 0, 0,
	ANIMFIELD_S16_TRANSLATE, 0, 0, 8, 0, 0, 0, 0, 0, 0,
};
static u8 s_TestFrame[] = {7, 11};
static struct animtableentry s_TestAnims[2] = {
	[1] = { .numframes = 1, .bytesperframe = sizeof(s_TestFrame), .headerlen = sizeof(s_TestHeader) },
};
static u8 s_TestHeaderSlots[] = {0xff, 0};
static u8 *s_TestHeaders[] = {s_TestHeader};
static u8 *s_TestFrames[] = {s_TestFrame};

struct animtableentry *g_Anims = s_TestAnims;
u8 *g_AnimToHeaderSlot = s_TestHeaderSlots;
u8 **g_AnimHeaderBytes = s_TestHeaders;
u8 **g_AnimFrameBytes = s_TestFrames;
bool g_ModelDistanceDisabled = true;
f32 g_ModelDistanceScale = 1.0f;
void (*g_ModelJointPositionedFunc)(s32 mtxindex, Mtxf *mtx);

s32 animGetTotalCount(void)
{
	return ARRAYCOUNT(s_TestAnims);
}

s32 testModelasmUnboundPosition(s32 flip, f32 matrices[2][4][4])
{
	u8 mirror[][2] = {{0, 1}, {1, 0}};
	struct skeleton skeleton = { .numthings = ARRAYCOUNT(mirror), .things = mirror };
	union modelrodata root_data = { .position = { .part = 0, .mtxindex0 = 0 } };
	union modelrodata child_data = {
		.position = { .pos = { .x = 3.0f, .y = 5.0f, .z = 7.0f }, .part = 0xffff, .mtxindex0 = 1 },
	};
	struct modelnode root = { .type = MODELNODETYPE_POSITION, .rodata = &root_data };
	struct modelnode child = { .type = MODELNODETYPE_POSITION, .rodata = &child_data, .parent = &root };
	root.child = &child;
	struct modeldef definition = { .rootnode = &root, .skel = &skeleton, .nummatrices = 2 };
	struct anim anim = { .animnum = 1, .animscale = 1.0f, .flip = flip != 0 };
	Mtxf result[2] = {0};
	Mtxf parent = { .m = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}} };
	struct modelrenderdata render = { .unk00 = &parent };
	struct model model = { .definition = &definition, .matrices = result, .scale = 1.0f, .anim = &anim };
	if (!modelasm00018680(&render, &model)) return 0;
	memcpy(matrices[0], result[0].m, sizeof(result[0].m));
	memcpy(matrices[1], result[1].m, sizeof(result[1].m));
	return 1;
}
