#include <ultra64.h>
#include "constants.h"
#include "bss.h"
#include "data.h"
#include "lib/anim.h"
#include "lib/model.h"
#include "lib/mtx.h"
#include "system.h"
#include "types.h"
#include "platform.h"

struct t0slot {
	u16 unk00;
	u16 unk02;
	u16 unk04;
	u16 unk06;
};

/* Each decoded animation owns 60 rotation slots followed by 60 translation
 * slots. The merge animation occupies the second 120-slot half. */
#define MODELASM_FRAME_PART_CAPACITY 60
#define MODELASM_FRAME_ANIM_STRIDE (MODELASM_FRAME_PART_CAPACITY * 2)
#define MODELASM_FRAME_SCRATCH_SLOTS (MODELASM_FRAME_ANIM_STRIDE * 2)

struct t0slot *t0slot;
u8 *t2ptr8;
s32 t3;
u8 *t3ptr8;
s32 t4;
s32 v1;
s32 s0;
s32 s1;
s32 s2;
s32 s3;
s32 s4;
s32 sr8;
s32 f0int;
f32 f0;
f32 f1;
f32 f2;
f32 f3;
f32 f4;
f32 f5;

// f12-f23 are used as rotation matrix
f32 f12;
f32 f13;
f32 f14;
f32 f15;
f32 f16;
f32 f17;
f32 f18;
f32 f19;
f32 f20;
f32 f21;
f32 f22;
f32 f23;

enum modelasm_frame_decode_failure {
	MODELASM_FRAME_DECODE_OK = 0,
	MODELASM_FRAME_DECODE_GENERIC_REQUIRED,
	MODELASM_FRAME_DECODE_INVALID_FLAGS,
	MODELASM_FRAME_DECODE_METADATA_INVALID,
	MODELASM_FRAME_DECODE_HEADER_TRUNCATED,
	MODELASM_FRAME_DECODE_FIELD_WIDTH_INVALID,
	MODELASM_FRAME_DECODE_PAYLOAD_TRUNCATED,
	MODELASM_FRAME_DECODE_PART_CAPACITY_EXCEEDED,
	MODELASM_FRAME_DECODE_LAYOUT_MISMATCH,
};

static const u8 *s_ModelasmFrameBytes;
static u32 s_ModelasmFrameByteLen;
static u32 s_ModelasmFrameBitOffset;
static enum modelasm_frame_decode_failure s_ModelasmFrameDecodeFailure;
static s16 s_ModelasmFrameAnimnum;
static s32 s_ModelasmFrameNum;
static u32 s_ModelasmFramePartIndex;
static const u8 *s_ModelasmExpectedPartHeaderEnd;
static u32 s_ModelasmExpectedPartFrameBitEnd;
static s16 s_ModelasmLastRejectedAnimnum = -1;
static s32 s_ModelasmLastRejectedFrame = -1;

static bool modelasmIterateThings1(void);
static bool modelasmIterateThings2(void);
static bool modelasmBuildMatrices(struct modelrenderdata *renderdata,
		struct model *model, struct anim *anim);
static bool modelasmBeginAnimationFrame(s16 animnum, u8 frameslot,
		s32 framenum);
static bool modelasmBeginFrameData(u8 *bytes, u32 bytelen, s16 animnum,
		s32 framenum);
static bool modelasmRequireHeaderBytes(u32 bytecount);
static bool modelasmValidateCurrentPart(u8 flags);
static bool modelasmFinishCurrentPart(void);
static bool modelasmRequireFrameFieldWidth(u8 width, u8 maxwidth);
static bool modelasmRequireFramePartCapacity(void);
static bool modelasmReadFrameData(s32 *value);
static bool modelasmSkipFrameData(u32 bitcount);
static bool modelasmHandleFrameDecodeFailure(
		struct modelrenderdata *renderdata, struct model *model);
static union modelrwdata *modelasmGetNodeRwData(struct model *model, struct modelnode *node, bool is_head);
static void modelasmMathPain1(f32 f30);
static void modelasmMathPain2(void);
static void modelasmPrepareRotMtx180(s32 t2, s32 t3, s32 t4);
static void modelasmPrepareRotMtx360(s32 t2, s32 t3, s32 t4);
static void modelasmMathPain3(void);
static void modelasmMathPain4(void);
static void modelasmMtxMultiply(Mtxf *src, Mtxf *dst);
static Mtxf *modelasmFindNodeMtx(struct model *model, struct modelnode *node);
static f32 modelasmAcosOrAsin(f32 f6);

/**
 * Reads animation data for the given model and applies matrix transformations
 * for each part. It factors merging between two animations too.
 *
 * The game code is able to set g_ModelJointPositionedFunc to a callback,
 * and this function will execute that callback for every positioned joint.
 *
 * Returns true if succeeded, or false if the caller should do some kind of
 * simple/generic calculation.
 */
bool modelasm00018680(struct modelrenderdata *renderdata, struct model *model)
{
	return modelasmBuildMatrices(renderdata, model,
		model != NULL ? model->anim : NULL);
}

static bool modelasmBuildMatrices(struct modelrenderdata *renderdata,
		struct model *model, struct anim *anim)
{
	bool sp7f8 = false;
	f32 sp7e8f32;
	struct t0slot *sp7e8slot;
	f32 sp7f4;
	f32 sp7f0;
	f32 sp7ec;
	bool sp7e4;
	struct t0slot sp00[MODELASM_FRAME_SCRATCH_SLOTS];
	struct modelnode *node;
	struct skeleton *skeleton;
	struct modeldef *modeldef;
	union modelrwdata *rwdata;
	f32 f6;
	f32 f7;
	f32 f8;
	f32 f9;
	f32 f10;
	f32 f30;
	f32 animscale;
	Mtxf *t0mtx;
	Mtxf *t1mtx;
	s32 t1;
	s32 t2;
	u8 *s0ptr8;
	s32 i;
	f32 yrot;

	s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_OK;

	if (renderdata == NULL || model == NULL || model->definition == NULL
			|| model->definition->nummatrices < 0
			|| model->definition->nummatrices > MODELASM_FRAME_ANIM_STRIDE) {
		return false;
	}

	for (i = 0; i < model->definition->nummatrices; i++) {
		sp00[i].unk00 = 0;
	}

	/* An allocated animation object with animnum 0 is the normal bind-pose
	 * state. Treat it exactly like no animation instead of trying to decode the
	 * sentinel table entry and reporting a malformed authored animation. */
	if (anim != NULL && anim->animnum == 0) {
		anim = NULL;
	}
	animscale = anim != NULL ? anim->animscale : 1.0f;

	if (anim) {
		t0slot = &sp00[0];

		if (!modelasmBeginAnimationFrame(anim->animnum, anim->frameslot1,
				anim->framea)) {
			return modelasmHandleFrameDecodeFailure(renderdata, model);
		}

		if (t2ptr8 != t3ptr8) {
			if (!modelasmIterateThings1()) {
				return modelasmHandleFrameDecodeFailure(renderdata, model);
			}

			if (anim->frac != 0.0f) {
				for (i = 0; i < model->definition->nummatrices; i++) {
					sp00[MODELASM_FRAME_ANIM_STRIDE + i].unk00 = 0;
				}

				f0int = anim->frac * 4096;

				t0slot = &sp00[0];

				if (!modelasmBeginAnimationFrame(anim->animnum,
						anim->frameslot2, anim->frameb)) {
					return modelasmHandleFrameDecodeFailure(renderdata, model);
				}

				if (t2ptr8 != t3ptr8) {
					if (!modelasmIterateThings2()) {
						return modelasmHandleFrameDecodeFailure(renderdata, model);
					}
				}
			}
		}

		f30 = anim->fracmerge;

		if (anim->fracmerge != 0.0f) {
			t0slot = &sp00[MODELASM_FRAME_ANIM_STRIDE];

			if (!modelasmBeginAnimationFrame(anim->animnum2,
					anim->frameslot3, anim->frame2a)) {
				return modelasmHandleFrameDecodeFailure(renderdata, model);
			}

			if (t2ptr8 != t3ptr8) {
				if (!modelasmIterateThings1()) {
					return modelasmHandleFrameDecodeFailure(renderdata, model);
				}

				if (anim->frac2 != 0.0f) {
					t0slot = &sp00[MODELASM_FRAME_ANIM_STRIDE];
					f0int = anim->frac2 * 4096;

					if (!modelasmBeginAnimationFrame(anim->animnum2,
							anim->frameslot4, anim->frame2b)) {
						return modelasmHandleFrameDecodeFailure(renderdata, model);
					}

					if (t2ptr8 != t3ptr8) {
						if (!modelasmIterateThings2()) {
							return modelasmHandleFrameDecodeFailure(renderdata, model);
						}
					}
				}
			}
		}
	}

	modeldef = model->definition;
	node = modeldef->rootnode;
	skeleton = modeldef->skel;

	while (node) {
		switch (node->type & 0xff) {
		case MODELNODETYPE_POSITION:
			sp7e4 = false;

			if (model) {
				t1 = node->rodata->position.part;

				if (anim) {
					if (f30 != 0.0f) {
						if (anim->flip) {
							t2ptr8 = skeleton->things[t1];
							t0slot = &sp00[t2ptr8[1]];

							sr8 = t0slot->unk00;
							t4 = t0slot->unk06;
							t3 = t0slot->unk04;
							t2 = t0slot->unk02;

							if (t4 != 0) {
								t4 = (0x10000 - t4) & 0xffff;
							}

							if (t3 != 0) {
								t3 = (0x10000 - t3) & 0xffff;
							}
						} else {
							t0slot = &sp00[t1];

							sr8 = t0slot->unk00;
							t2 = t0slot->unk02;
							t3 = t0slot->unk04;
							t4 = t0slot->unk06;
						}

						if (anim->flip2) {
							s0ptr8 = skeleton->things[t1];
							t0slot = &sp00[s0ptr8[1]];

							s4 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk06;
							s3 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk04;
							s0 = t0slot[0].unk02;
							s2 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk02;

							if (s4 != 0) {
								s4 = (0x10000 - s4) & 0xffff;
							}

							if (s3 != 0) {
								s3 = (0x10000 - s3) & 0xffff;
							}
						} else {
							t0slot = &sp00[t1];

							s2 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk02;
							s3 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk04;
							s4 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk06;
						}

						sp7e8slot = t0slot;
						modelasmPrepareRotMtx360(t2, t3, t4);
						modelasmMathPain2();

						f16 = f0;
						f17 = f1;
						f18 = f2;
						f19 = f3;

						t2 = s2;
						t3 = s3;
						t4 = s4;

						modelasmPrepareRotMtx360(t2, t3, t4);
						modelasmMathPain2();
						modelasmMathPain1(f30);

						sp7e4 = true;
						t0slot = sp7e8slot;

						sp7e8f32 = f0;
						sp7ec = f1;
						sp7f0 = f2;
						sp7f4 = f3;

						modelasmMathPain4();
					} else {
						if (anim->flip) {
							t2ptr8 = skeleton->things[t1];
							t0slot = &sp00[t2ptr8[1]];

							sr8 = t0slot->unk00;
							t4 = t0slot->unk06;
							t3 = t0slot->unk04;
							t2 = t0slot->unk02;

							if (t4 != 0) {
								t4 = (0x10000 - t4) & 0xffff;
							}

							if (t3 != 0) {
								t3 = (0x10000 - t3) & 0xffff;
							}
						} else {
							t0slot = &sp00[t1];

							sr8 = t0slot->unk00;
							t4 = t0slot->unk06;
							t3 = t0slot->unk04;
							t2 = t0slot->unk02;
						}

						s0 = t2;

						if ((sr8 & 1) == 0) {
							f12 = 1;
							f13 = 0;
							f14 = 0;

							f15 = 0;
							f16 = 1;
							f17 = 0;

							f18 = 0;
							f19 = 0;
							f20 = 1;
						} else {
							s0 = t2;
							s1 = t3;
							s2 = t4;

							modelasmPrepareRotMtx180(t2, t3, t4);
							modelasmMathPain3();
						}
					}
				} else {
					f12 = 1;
					f13 = 0;
					f14 = 0;

					f15 = 0;
					f16 = 1;
					f17 = 0;

					f18 = 0;
					f19 = 0;
					f20 = 1;

					t0slot = &sp00[t1];
					sr8 = t0slot->unk00;
				}

				if (sr8 & 2) {
					t2 = *(s16 *) &t0slot[MODELASM_FRAME_PART_CAPACITY].unk00;
					t3 = *(s16 *) &t0slot[MODELASM_FRAME_PART_CAPACITY].unk02;
					t4 = *(s16 *) &t0slot[MODELASM_FRAME_PART_CAPACITY].unk04;

					if (node == modeldef->rootnode) {
						f21 = t2 * animscale;
						f22 = t3 * animscale;
						f23 = t4 * animscale;
					} else {
						f0 = t2 * animscale;
						f1 = t3 * animscale;
						f2 = t4 * animscale;

						f21 = f0 + node->rodata->position.pos.x;
						f22 = f1 + node->rodata->position.pos.y;
						f23 = f2 + node->rodata->position.pos.z;
					}
				} else {
					if (node != modeldef->rootnode) {
						f21 = node->rodata->position.pos.x;
						f22 = node->rodata->position.pos.y;
						f23 = node->rodata->position.pos.z;
					} else {
						f21 = 0;
						f22 = 0;
						f23 = 0;
					}
				}

				t0mtx = node->parent ? modelasmFindNodeMtx(model, node->parent) : renderdata->unk00;
				t1mtx = &model->matrices[node->rodata->position.mtxindex0];

				modelasmMtxMultiply(t0mtx, t1mtx);

				if (g_ModelJointPositionedFunc) {
					g_ModelJointPositionedFunc(node->rodata->position.mtxindex0, &model->matrices[node->rodata->position.mtxindex0]);
				}

				if (node->type & MODELNODETYPE_0100) {
					if (sp7e4) {
						f0 = sp7e8f32;
						f1 = sp7ec;
						f2 = sp7f0;
						f3 = sp7f4;
					} else {
						t2 = s0;
						t3 = s1;
						t4 = s2;
						modelasmPrepareRotMtx360(t2, t3, t4);
						modelasmMathPain2();
					}

					f4 = 0;
					f6 = f0;
					f5 = 1;

					if (f6 < 0.0f) {
						f6 = -f6;
						f5 = -f5;
					}

					if (f6 < -0.99994999170303f) {
						f0 *= 0.5f;
						f4 = f5 * 0.5f;
						f1 *= 0.5f;
						f2 *= 0.5f;
						f3 *= 0.5f;
						f0 -= f4;
					} else if (f6 > 0.99994999170303f) {
						f0 *= 0.5f;
						f1 *= 0.5f;
						f2 *= 0.5f;
						f3 *= 0.5f;
						f4 = f5 * 0.5f;
						f0 += f4;
					} else {
						f7 = modelasmAcosOrAsin(f6);
						f17 = f0;
						f12 = f6 * 0.5f;
						s1 = t1;
						f0 = sinf(f12);
						f3 *= f0;
						f2 *= f0;
						t1 = s1;
						f1 *= f0;
						f5 *= f0;
						f0 *= f17;
						f0 += f5;
					}

					modelasmMathPain4();
					t1mtx = &model->matrices[node->rodata->position.mtxindex1];
					modelasmMtxMultiply(t0mtx, t1mtx);
				}
			}
			break;
		case MODELNODETYPE_DISTANCE:
			t0mtx = modelasmFindNodeMtx(model, node);

			rwdata = modelasmGetNodeRwData(model, node, sp7f8);
			f0 = 0;

			if (!g_ModelDistanceDisabled && t0mtx) {
				f0 = -t0mtx->m[3][2] * g_Vars.currentplayer->c_lodscalez * g_ModelDistanceScale;
			}

			if ((node->rodata->distance.near == 0.0f || f0 > node->rodata->distance.near * model->scale)
					&& f0 <= node->rodata->distance.far * model->scale) {
				rwdata->distance.visible = true;
				node->child = node->rodata->distance.target;
			} else {
				rwdata->distance.visible = false;
				node->child = NULL;
			}
			break;
		case MODELNODETYPE_CHRINFO:
			if (model) {
				t1 = node->rodata->chrinfo.animpart;

				if (anim && anim->animnum) {
					if (f30 != 0.0f) {
						if (anim->flip) {
							t2ptr8 = skeleton->things[t1];
							t0slot = &sp00[t2ptr8[1]];

							sr8 = t0slot->unk00;
							t4 = t0slot->unk06;
							t3 = t0slot->unk04;
							t2 = t0slot->unk02;

							if (t4 != 0) {
								t4 = (0x10000 - t4) & 0xffff;
							}

							if (t3 != 0) {
								t3 = (0x10000 - t3) & 0xffff;
							}
						} else {
							t0slot = &sp00[t1];

							sr8 = t0slot->unk00;
							t2 = t0slot->unk02;
							t3 = t0slot->unk04;
							t4 = t0slot->unk06;
						}

						if (anim->flip2) {
							s0ptr8 = skeleton->things[t1];
							t0slot = &sp00[s0ptr8[1]];

							s4 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk06;
							s3 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk04;
							s0 = t0slot[0].unk02;
							s2 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk02;

							if (s4 != 0) {
								s4 = (0x10000 - s4) & 0xffff;
							}

							if (s3 != 0) {
								s3 = (0x10000 - s3) & 0xffff;
							}
						} else {
							t0slot = &sp00[t1];

							s2 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk02;
							s3 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk04;
							s4 = t0slot[MODELASM_FRAME_ANIM_STRIDE].unk06;
						}

						sp7e8slot = t0slot;
						modelasmPrepareRotMtx360(t2, t3, t4);
						modelasmMathPain2();

						f16 = f0;
						f17 = f1;
						f18 = f2;
						f19 = f3;

						t2 = s2;
						t3 = s3;
						t4 = s4;

						modelasmPrepareRotMtx360(t2, t3, t4);
						modelasmMathPain2();
						modelasmMathPain1(f30);
						t0slot = sp7e8slot;
						modelasmMathPain4();
					} else {
						if (anim->flip) {
							t2ptr8 = skeleton->things[t1];
							t0slot = &sp00[t2ptr8[1]];

							sr8 = t0slot->unk00;
							t4 = t0slot->unk06;
							t3 = t0slot->unk04;
							t2 = t0slot->unk02;

							if (t4 != 0) {
								t4 = (0x10000 - t4) & 0xffff;
							}

							if (t3 != 0) {
								t3 = (0x10000 - t3) & 0xffff;
							}

							modelasmPrepareRotMtx180(t2, t3, t4);
						} else {
							t0slot = &sp00[t1];

							sr8 = t0slot->unk00;
							t4 = t0slot->unk06;
							t3 = t0slot->unk04;
							t2 = t0slot->unk02;

							modelasmPrepareRotMtx180(t2, t3, t4);
						}

						modelasmMathPain3();
					}
				} else {
					f12 = 1;
					f13 = 0;
					f14 = 0;

					f15 = 0;
					f16 = 1;
					f17 = 0;

					f18 = 0;
					f19 = 0;
					f20 = 1;

					t0slot = &sp00[t1];
					sr8 = t0slot->unk00;
				}

				f0 = 0;

				rwdata = modelasmGetNodeRwData(model, node, sp7f8);

				yrot = rwdata->chrinfo.yrot;

				if (rwdata->chrinfo.unk18 != 0.0f) {
					f5 = rwdata->chrinfo.unk1c - rwdata->chrinfo.yrot;

					if (f5 < 0.0f) {
						f5 += M_TAU;
					}

					if (f5 >= M_PI) {
						f5 *= rwdata->chrinfo.unk18;
						yrot += f5;

						if (yrot > M_TAU) {
							yrot -= M_TAU;
						}
					} else {
						f5 = M_TAU - f5;
						f5 *= rwdata->chrinfo.unk18;
						yrot -= f5;

						if (yrot < 0.0f) {
							yrot += M_TAU;
						}
					}
				}

				f1 = sinf(yrot);
				f0 = sinf(yrot + 1.570796251297f);

				f2 = -f1;

				f21 = rwdata->chrinfo.pos.x;
				f22 = rwdata->chrinfo.pos.y;
				f23 = rwdata->chrinfo.pos.z;

				f3 = f0 * f12;
				f4 = f1 * f14;
				f5 = f0 * f15;
				f3 = f3 + f4;
				f6 = f1 * f17;
				f7 = f0 * f18;
				f5 = f5 + f6;
				f8 = f1 * f20;
				f9 = f2 * f12;
				f7 = f7 + f8;
				f10 = f0 * f14;
				f4 = f2 * f15;
				f14 = f9 + f10;
				f6 = f0 * f17;
				f8 = f2 * f18;
				f17 = f4 + f6;
				f10 = f0 * f20;
				f12 = f3;
				f20 = f8 + f10;
				f15 = f5;
				f18 = f7;

				if (model->scale != 1.0f) {
					f12 *= model->scale;
					f13 *= model->scale;
					f14 *= model->scale;
					f15 *= model->scale;
					f16 *= model->scale;
					f17 *= model->scale;
					f18 *= model->scale;
					f19 *= model->scale;
					f20 *= model->scale;
				}

				t0mtx = renderdata->unk00;
				t1mtx = &model->matrices[node->rodata->chrinfo.mtxindex];

				modelasmMtxMultiply(t0mtx, t1mtx);
			}
			break;
		case MODELNODETYPE_HEADSPOT:
			rwdata = modelasmGetNodeRwData(model, node, sp7f8);

			if (rwdata->headspot.headmodeldef) {
				struct modelnode *iternode = rwdata->headspot.headmodeldef->rootnode;
				sp7f8 = true;
				node->child = iternode;

				while (iternode) {
					iternode->parent = node;
					iternode = iternode->next;
				}
			}
			break;
		case MODELNODETYPE_POSITIONHELD:
			t0mtx = node->parent ? modelasmFindNodeMtx(model, node->parent) : renderdata->unk00;

			f12 = 1;
			f13 = 0;
			f14 = 0;

			f15 = 0;
			f16 = 1;
			f17 = 0;

			f18 = 0;
			f19 = 0;
			f20 = 1;

			f21 = node->rodata->positionheld.pos.x;
			f22 = node->rodata->positionheld.pos.y;
			f23 = node->rodata->positionheld.pos.z;

			t1mtx = &model->matrices[node->rodata->positionheld.mtxindex];

			modelasmMtxMultiply(t0mtx, t1mtx);
			break;
		case MODELNODETYPE_REORDER:
			break;
		case MODELNODETYPE_TOGGLE:
			rwdata = modelasmGetNodeRwData(model, node, sp7f8);

			node->child = rwdata->toggle.visible ? node->rodata->toggle.target : NULL;
			break;
		}

		if (node->child) {
			node = node->child;
		} else {
			while (true) {
				if (node->next) {
					node = node->next;
					break;
				} else {
					node = node->parent;

					if (node) {
						if ((node->type & 0xff) == MODELNODETYPE_HEADSPOT) {
							// @bug:
							// sp7f8 tracks whether we are under the head node
							// which allows modelasmGetNodeRwData to avoid
							// searching for the head node if we're already
							// above it. The code here is ascending back out of
							// the head node, so it should be setting it to
							// false here.
							sp7f8 = true;
						}
					} else {
						return true;
					}
				}
			}
		}
	}

	return true;
}

/**
 * Expects:
 * t0 = pointer to stack slots
 * t2 = pointer to anim header bytes
 * t3 = pointer to end of anim header
 * t6 = pointer to anim frame bytes
 */
static bool modelasmIterateThings1(void)
{
	s32 t7;
	u32 v0;

	do {
		t7 = *t2ptr8;
		t2ptr8++;

		if (t7 > 15) {
			/* S32/f32/camera/scale descriptors and the native header tail
			 * intentionally use the generic decoder. The generic path now owns
			 * the same bounded layout admission in animGetRotTranslateScale. */
			s_ModelasmFrameDecodeFailure =
				MODELASM_FRAME_DECODE_GENERIC_REQUIRED;
			return false;
		}

		if (!modelasmValidateCurrentPart((u8)t7)) {
			return false;
		}

		if (!modelasmRequireFramePartCapacity()) {
			return false;
		}

		t0slot->unk00 = t7;

		if (t7 & 2) {
			if (!modelasmRequireHeaderBytes(9)) {
				return false;
			}

			if (!modelasmRequireFrameFieldWidth(t2ptr8[2], 16)
					|| !modelasmRequireFrameFieldWidth(t2ptr8[5], 16)
					|| !modelasmRequireFrameFieldWidth(t2ptr8[8], 16)) {
				return false;
			}

			// 0, 1, 2
			v1 = t2ptr8[2];
			if (!modelasmReadFrameData(&s0)) {
				return false;
			}
			v1 = t2ptr8[2];
			s3 = 1;

			if (v1 > 0 && v1 < 16) {
				v0 = v1 - 1;
				s3 <<= v0;
				s4 = 16;

				if (s0 & s3) {
					s4 -= v1;
					v0 = (1 << s4) - 1;
					v0 <<= v1;
					s0 |= v0;
				}
			}

			s3 = t2ptr8[0] << 8;
			s4 = t2ptr8[1];
			s3 += s4;
			s0 += s3;
			s0 &= 0xffff;

			// 3, 4, 5
			v1 = t2ptr8[5];
			if (!modelasmReadFrameData(&s1)) {
				return false;
			}
			v1 = t2ptr8[5];
			s3 = 1;

			if (v1 > 0 && v1 < 16) {
				v0 = v1 - 1;
				s3 <<= v0;
				s4 = 16;

				if (s1 & s3) {
					s4 -= v1;
					v0 = (1 << s4) - 1;
					v0 <<= v1;
					s1 |= v0;
				}
			}

			s3 = t2ptr8[3] << 8;
			s4 = t2ptr8[4];
			s3 += s4;
			s1 += s3;
			s1 &= 0xffff;

			// 6, 7, 8
			v1 = t2ptr8[8];
			if (!modelasmReadFrameData(&s2)) {
				return false;
			}
			v1 = t2ptr8[8];
			s3 = 1;

			if (v1 > 0 && v1 < 16) {
				v0 = v1 - 1;
				s3 <<= v0;
				s4 = 16;

				if (s2 & s3) {
					s4 -= v1;
					v0 = (1 << s4) - 1;
					v0 <<= v1;
					s2 |= v0;
				}
			}

			s3 = t2ptr8[6] << 8;
			s4 = t2ptr8[7];
			s3 += s4;
			s2 += s3;
			s2 &= 0xffff;

			t2ptr8 += 9;

			t0slot[MODELASM_FRAME_PART_CAPACITY].unk00 = s0;
			t0slot[MODELASM_FRAME_PART_CAPACITY].unk02 = s1;
			t0slot[MODELASM_FRAME_PART_CAPACITY].unk04 = s2;
		} else {
			s0 = 0;
			s1 = 0;
			s2 = 0;

			if (t7 & 8) {
				if (!modelasmRequireHeaderBytes(12)) {
					return false;
				}

				if (!modelasmRequireFrameFieldWidth(t2ptr8[2], 32)
						|| !modelasmRequireFrameFieldWidth(t2ptr8[5], 32)
						|| !modelasmRequireFrameFieldWidth(t2ptr8[8], 32)
						|| !modelasmRequireFrameFieldWidth(t2ptr8[11], 32)) {
					return false;
				}

				v0 = t2ptr8[2] + t2ptr8[5] + t2ptr8[8] + t2ptr8[11];

				if (!modelasmSkipFrameData(v0)) {
					return false;
				}

				t2ptr8 += 12;
			}
		}

		if (t7 & 1) {
			if (!modelasmRequireHeaderBytes(9)) {
				return false;
			}

			if (!modelasmRequireFrameFieldWidth(t2ptr8[2], 16)
					|| !modelasmRequireFrameFieldWidth(t2ptr8[5], 16)
					|| !modelasmRequireFrameFieldWidth(t2ptr8[8], 16)) {
				return false;
			}

			v1 = t2ptr8[2];
			if (!modelasmReadFrameData(&s0)) {
				return false;
			}
			s0 += (t2ptr8[0] << 8) + t2ptr8[1];
			v0 = 16 - t4;
			s0 = (s0 << v0) & 0xffff;

			v1 = t2ptr8[5];
			if (!modelasmReadFrameData(&s1)) {
				return false;
			}
			s1 += (t2ptr8[3] << 8) + t2ptr8[4];
			v0 = 16 - t4;
			s1 = (s1 << v0) & 0xffff;

			v1 = t2ptr8[8];
			if (!modelasmReadFrameData(&s2)) {
				return false;
			}
			s2 += (t2ptr8[6] << 8) + t2ptr8[7];
			v0 = 16 - t4;
			s2 = (s2 << v0) & 0xffff;

			t2ptr8 += 9;
		} else {
			s0 = 0;
			s1 = 0;
			s2 = 0;
		}

		if (!modelasmFinishCurrentPart()) {
			return false;
		}

		t0slot->unk02 = s0;
		t0slot->unk04 = s1;
		t0slot->unk06 = s2;
		t0slot++;
	} while (t2ptr8 < t3ptr8);

	return true;
}

/**
 * Expects:
 * t0 = pointer to stack slots
 * t2 = pointer to anim header bytes
 * t3 = pointer to end of anim header
 * t4 = frame length
 * t6 = pointer to anim frame bytes
 * f0 containing an integer
 */
static bool modelasmIterateThings2(void)
{
	s32 t7;
	s32 s5;
	u32 v0;

	do {
		t7 = t2ptr8[0];
		t2ptr8++;

		if (t7 > 15) {
			s_ModelasmFrameDecodeFailure =
				MODELASM_FRAME_DECODE_GENERIC_REQUIRED;
			return false;
		}

		if (!modelasmValidateCurrentPart((u8)t7)) {
			return false;
		}

		if (!modelasmRequireFramePartCapacity()) {
			return false;
		}

		if ((t7 & 2) == 0) {
			s0 = 0;
			s1 = 0;
			s2 = 0;

			if (t7 & 8) {
				if (!modelasmRequireHeaderBytes(12)) {
					return false;
				}

				if (!modelasmRequireFrameFieldWidth(t2ptr8[2], 32)
						|| !modelasmRequireFrameFieldWidth(t2ptr8[5], 32)
						|| !modelasmRequireFrameFieldWidth(t2ptr8[8], 32)
						|| !modelasmRequireFrameFieldWidth(t2ptr8[11], 32)) {
					return false;
				}

				v0 = t2ptr8[2] + t2ptr8[5] + t2ptr8[8] + t2ptr8[11];

				if (!modelasmSkipFrameData(v0)) {
					return false;
				}

				t2ptr8 += 12;
			}
		} else {
			if (!modelasmRequireHeaderBytes(9)) {
				return false;
			}

			if (!modelasmRequireFrameFieldWidth(t2ptr8[2], 16)
					|| !modelasmRequireFrameFieldWidth(t2ptr8[5], 16)
					|| !modelasmRequireFrameFieldWidth(t2ptr8[8], 16)) {
				return false;
			}

			v0 = t2ptr8[2] + t2ptr8[5] + t2ptr8[8];

			if (!modelasmSkipFrameData(v0)) {
				return false;
			}

			t2ptr8 += 9;
		}

		if ((t7 & 1) == 0) {
			s0 = 0;
			s1 = 0;
			s2 = 0;
		} else {
			if (!modelasmRequireHeaderBytes(9)) {
				return false;
			}

			if (!modelasmRequireFrameFieldWidth(t2ptr8[2], 16)
					|| !modelasmRequireFrameFieldWidth(t2ptr8[5], 16)
					|| !modelasmRequireFrameFieldWidth(t2ptr8[8], 16)) {
				return false;
			}

			v1 = t2ptr8[2];
			if (!modelasmReadFrameData(&s0)) {
				return false;
			}
			s0 += (t2ptr8[0] << 8) + t2ptr8[1];
			v0 = 16 - t4;
			s0 = (s0 << v0) & 0xffff;

			v1 = t2ptr8[5];
			if (!modelasmReadFrameData(&s1)) {
				return false;
			}
			s1 += (t2ptr8[3] << 8) + t2ptr8[4];
			v0 = 16 - t4;
			s1 = (s1 << v0) & 0xffff;

			v1 = t2ptr8[8];
			if (!modelasmReadFrameData(&s2)) {
				return false;
			}
			s2 += (t2ptr8[6] << 8) + t2ptr8[7];
			v0 = 16 - t4;
			s2 = (s2 << v0) & 0xffff;

			t2ptr8 += 9;
		}

		if (!modelasmFinishCurrentPart()) {
			return false;
		}

		s3 = f0int;
		v1 = 0x10000;

		// 02
		s4 = t0slot->unk02;
		s5 = s0 - s4;

		if (s5 < 0) {
			s5 += 0x10000;
		}

		if (s5 < 0x8000) {
			s5 *= s3;
			s5 >>= 12;
			s0 = s4 + s5;

			if (s0 >= 0x10000) {
				s0 -= 0x10000;
			}
		} else {
			s5 = 0x10000 - s5;
			s5 *= s3;
			s5 >>= 12;
			s0 = s4 - s5;

			if (s0 < 0) {
				s0 += 0x10000;
			}
		}

		// 04
		s4 = t0slot->unk04;
		s5 = s1 - s4;

		if (s5 < 0) {
			s5 += 0x10000;
		}

		if (s5 < 0x8000) {
			s5 *= s3;
			s5 >>= 12;
			s1 = s4 + s5;

			if (s1 >= 0x10000) {
				s1 -= 0x10000;
			}
		} else {
			s5 = 0x10000 - s5;
			s5 *= s3;
			s5 >>= 12;
			s1 = s4 - s5;

			if (s1 < 0) {
				s1 += 0x10000;
			}
		}

		// 06
		s4 = t0slot->unk06;
		s5 = s2 - s4;

		if (s5 < 0) {
			s5 += 0x10000;
		}

		if (s5 < 0x8000) {
			s5 *= s3;
			s5 >>= 12;
			s2 = s4 + s5;

			if (s2 >= 0x10000) {
				s2 -= 0x10000;
			}
		} else {
			s5 = 0x10000 - s5;
			s5 *= s3;
			s5 >>= 12;
			s2 = s4 - s5;

			if (s2 < 0) {
				s2 += 0x10000;
			}
		}

		t0slot->unk02 = s0;
		t0slot->unk04 = s1;
		t0slot->unk06 = s2;
		t0slot++;
	} while (t2ptr8 < t3ptr8);

	return true;
}

static bool modelasmBeginAnimationFrame(s16 animnum, u8 frameslot,
		s32 framenum)
{
	u8 headerslot;

	/* Seed diagnostics before touching animation-owned arrays so every metadata
	 * rejection remains typed and can safely fall back to the bind pose. */
	s_ModelasmFrameAnimnum = animnum;
	s_ModelasmFrameNum = framenum;
	s_ModelasmFrameBytes = NULL;
	s_ModelasmFrameByteLen = 0;
	s_ModelasmFrameBitOffset = 0;
	s_ModelasmFramePartIndex = 0;
	s_ModelasmExpectedPartHeaderEnd = NULL;
	s_ModelasmExpectedPartFrameBitEnd = 0;

	if (animnum < 0 || animnum >= animGetTotalCount()
			|| frameslot >= ANIM_FRAME_CACHE_SIZE
			|| g_Anims == NULL || g_AnimToHeaderSlot == NULL
			|| g_AnimHeaderBytes == NULL || g_AnimFrameBytes == NULL) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_METADATA_INVALID;
		return false;
	}

	headerslot = g_AnimToHeaderSlot[animnum];

	if (headerslot == 0xff || headerslot >= ANIM_HEADER_CACHE_SIZE
			|| g_AnimHeaderBytes[headerslot] == NULL) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_METADATA_INVALID;
		return false;
	}

	t2ptr8 = g_AnimHeaderBytes[headerslot];
	t3ptr8 = t2ptr8 + g_Anims[animnum].headerlen;
	t4 = g_Anims[animnum].framelen;

	return modelasmBeginFrameData(g_AnimFrameBytes[frameslot],
		g_Anims[animnum].bytesperframe, animnum, framenum);
}

static bool modelasmBeginFrameData(u8 *bytes, u32 bytelen, s16 animnum,
		s32 framenum)
{
	s_ModelasmFrameBytes = bytes;
	s_ModelasmFrameByteLen = bytelen;
	s_ModelasmFrameBitOffset = 0;
	s_ModelasmFrameAnimnum = animnum;
	s_ModelasmFrameNum = framenum;
	s_ModelasmFramePartIndex = 0;
	s_ModelasmExpectedPartHeaderEnd = NULL;
	s_ModelasmExpectedPartFrameBitEnd = 0;

	if ((bytes == NULL && bytelen != 0) || t4 < 0 || t4 > 16) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_METADATA_INVALID;
		return false;
	}

	return true;
}

static bool modelasmRequireHeaderBytes(u32 bytecount)
{
	if (t2ptr8 > t3ptr8 || (u64)(t3ptr8 - t2ptr8) < (u64)bytecount) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_HEADER_TRUNCATED;
		return false;
	}

	return true;
}

static bool modelasmValidateCurrentPart(u8 flags)
{
	enum anim_frame_layout_result result;
	u32 descriptor_bytes;
	u32 frame_bits;
	u64 frame_end;
	u64 frame_capacity;

	if (t2ptr8 > t3ptr8) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_HEADER_TRUNCATED;
		return false;
	}

	result = animFrameMeasurePartBounded(t2ptr8,
		(u32)(t3ptr8 - t2ptr8), flags, &descriptor_bytes, &frame_bits);

	switch (result) {
	case ANIM_FRAME_LAYOUT_OK:
		break;
	case ANIM_FRAME_LAYOUT_INVALID_FLAGS:
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_INVALID_FLAGS;
		return false;
	case ANIM_FRAME_LAYOUT_HEADER_TRUNCATED:
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_HEADER_TRUNCATED;
		return false;
	case ANIM_FRAME_LAYOUT_FIELD_WIDTH_INVALID:
		s_ModelasmFrameDecodeFailure =
			MODELASM_FRAME_DECODE_FIELD_WIDTH_INVALID;
		return false;
	case ANIM_FRAME_LAYOUT_PAYLOAD_TRUNCATED:
		s_ModelasmFrameDecodeFailure =
			MODELASM_FRAME_DECODE_PAYLOAD_TRUNCATED;
		return false;
	default:
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_METADATA_INVALID;
		return false;
	}

	frame_end = (u64)s_ModelasmFrameBitOffset + (u64)frame_bits;
	frame_capacity = (u64)s_ModelasmFrameByteLen * 8u;

	if (frame_end > frame_capacity) {
		s_ModelasmFrameDecodeFailure =
			MODELASM_FRAME_DECODE_PAYLOAD_TRUNCATED;
		return false;
	}
	if (frame_end > 0xffffffffu) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_METADATA_INVALID;
		return false;
	}

	s_ModelasmExpectedPartHeaderEnd = t2ptr8 + descriptor_bytes;
	s_ModelasmExpectedPartFrameBitEnd = (u32)frame_end;

	return true;
}

static bool modelasmFinishCurrentPart(void)
{
	if (s_ModelasmExpectedPartHeaderEnd == NULL
			|| t2ptr8 != s_ModelasmExpectedPartHeaderEnd
			|| s_ModelasmFrameBitOffset != s_ModelasmExpectedPartFrameBitEnd) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_LAYOUT_MISMATCH;
		return false;
	}

	return true;
}

static bool modelasmRequireFrameFieldWidth(u8 width, u8 maxwidth)
{
	if (width > maxwidth) {
		s_ModelasmFrameDecodeFailure =
			MODELASM_FRAME_DECODE_FIELD_WIDTH_INVALID;
		return false;
	}

	return true;
}

static bool modelasmRequireFramePartCapacity(void)
{
	if (s_ModelasmFramePartIndex >= MODELASM_FRAME_PART_CAPACITY) {
		s_ModelasmFrameDecodeFailure =
			MODELASM_FRAME_DECODE_PART_CAPACITY_EXCEEDED;
		return false;
	}

	s_ModelasmFramePartIndex++;
	return true;
}

static bool modelasmReadFrameData(s32 *value)
{
	u32 decoded;

	if (value == NULL || v1 < 0 || v1 > 32
			|| !animReadBitsBounded(s_ModelasmFrameBytes,
				s_ModelasmFrameByteLen, (u8)v1,
				s_ModelasmFrameBitOffset, &decoded)) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_PAYLOAD_TRUNCATED;
		return false;
	}

	s_ModelasmFrameBitOffset += (u32)v1;
	*value = (s32)decoded;
	return true;
}

static bool modelasmSkipFrameData(u32 bitcount)
{
	u64 endoffset = (u64)s_ModelasmFrameBitOffset + (u64)bitcount;
	u64 bitlength = (u64)s_ModelasmFrameByteLen * 8u;

	if (endoffset > bitlength
			|| (s_ModelasmFrameBytes == NULL && bitcount != 0)) {
		s_ModelasmFrameDecodeFailure = MODELASM_FRAME_DECODE_PAYLOAD_TRUNCATED;
		return false;
	}

	s_ModelasmFrameBitOffset += bitcount;
	return true;
}

static bool modelasmHandleFrameDecodeFailure(
		struct modelrenderdata *renderdata, struct model *model)
{
	const char *reason;

	if (s_ModelasmFrameDecodeFailure == MODELASM_FRAME_DECODE_OK) {
		return false;
	}

	if (s_ModelasmFrameDecodeFailure
			== MODELASM_FRAME_DECODE_GENERIC_REQUIRED) {
		return false;
	}

	switch (s_ModelasmFrameDecodeFailure) {
	case MODELASM_FRAME_DECODE_INVALID_FLAGS:
		reason = "invalid_flags";
		break;
	case MODELASM_FRAME_DECODE_METADATA_INVALID:
		reason = "metadata_invalid";
		break;
	case MODELASM_FRAME_DECODE_HEADER_TRUNCATED:
		reason = "header_truncated";
		break;
	case MODELASM_FRAME_DECODE_FIELD_WIDTH_INVALID:
		reason = "field_width_invalid";
		break;
	case MODELASM_FRAME_DECODE_PAYLOAD_TRUNCATED:
		reason = "payload_truncated";
		break;
	case MODELASM_FRAME_DECODE_PART_CAPACITY_EXCEEDED:
		reason = "part_capacity_exceeded";
		break;
	case MODELASM_FRAME_DECODE_LAYOUT_MISMATCH:
		reason = "layout_mismatch";
		break;
	default:
		reason = "invalid_state";
		break;
	}

	if (s_ModelasmLastRejectedAnimnum != s_ModelasmFrameAnimnum
			|| s_ModelasmLastRejectedFrame != s_ModelasmFrameNum) {
		sysLogPrintf(LOG_ERROR,
			"ANIM.FRAME.DECODE.REJECT: anim=%d frame=%d reason=%s bit_offset=%u bytes=%u model=%p",
			s_ModelasmFrameAnimnum, s_ModelasmFrameNum, reason,
			s_ModelasmFrameBitOffset, s_ModelasmFrameByteLen, (void *)model);
		s_ModelasmLastRejectedAnimnum = s_ModelasmFrameAnimnum;
		s_ModelasmLastRejectedFrame = s_ModelasmFrameNum;
	}

	/* A rejected public frame must not fall through to a generic decoder or
	 * invalidate live animation ownership. Re-enter the same bounded matrix
	 * core with an explicit no-animation input; its bind-pose branch owns every
	 * node type without reading or mutating model->anim. */
	return modelasmBuildMatrices(renderdata, model, NULL);
}

/**
 * Indexed by node type.
 *
 * Each value is the byte offset into the rodata struct where that node type's
 * rwdataindex property can be found, or 0xff is there is none.
 */

#ifdef PLATFORM_64BIT
u8 var8005ef90[] = {
		 0xff, 0x08, 0xff, 0xff,
		 0xff, 0xff, 0xff, 0xff,
		 0x10, 0x2a, 0xff, 0x48,
		 0x24, 0xff, 0xff, 0xff,
		 0xff, 0xff, 0x08, 0xff,
		 0xff, 0xff, 0xff, 0x00,
		 0x24, 0x00,
};
#else
u8 var8005ef90[] = {
	0xff, 0x08, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0x0c, 0x22, 0xff, 0x44,
	0x20, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x04, 0xff,
	0xff, 0xff, 0xff, 0x00,
	0x14, 0x00,
};
#endif

static union modelrwdata *modelasmGetNodeRwData(struct model *model, struct modelnode *node, bool is_head)
{
	u32 index = 0;
	u32 *rwdatas = model->rwdatas;
	u8 type = node->type & 0xff;

	if (type < ARRAYCOUNT(var8005ef90)) {
		if (var8005ef90[type] != 0xff) {
			index = *(u16 *) ((uintptr_t) node->rodata + var8005ef90[type]);
		}
	}

	if (is_head) {
		while (node->parent) {
			node = node->parent;

			if ((node->type & 0xff) == MODELNODETYPE_HEADSPOT) {
				union modelrwdata *tmp = modelasmGetNodeRwData(model, node, false);
				rwdatas = tmp->headspot.rwdatas;
				break;
			}
		}
	}

	return (union modelrwdata *) &rwdatas[index];
}

#if VERSION < VERSION_NTSC_1_0
void *modelGetNodeRwData(struct model *model, struct modelnode *node)
{
	u32 index = 0;
	u32 *rwdatas = model->rwdatas;
	u8 type = node->type & 0xff;

	if (type < ARRAYCOUNT(var8005ef90)) {
		if (var8005ef90[type] != 0xff) {
			index = *(u16 *) ((uintptr_t) node->rodata + var8005ef90[type]);
		}
	}

	if (model->unk00) {
		while (node->parent) {
			node = node->parent;

			if ((node->type & 0xff) == MODELNODETYPE_HEADSPOT) {
				struct modelrwdata_headspot *tmp = modelGetNodeRwData(model, node);
				rwdatas = tmp->rwdatas;
				break;
			}
		}
	}

	return &rwdatas[index];
}
#endif

/**
 * Expects: f0-f3, f16-f22
 */
static void modelasmMathPain1(f32 f30)
{
	f32 f6;
	f32 f7;
	f32 f8;
	f32 f9;

	f4 = 0;

	while (true) {
		f5 = f16 * f0;
		f6 = f17 * f1;
		f7 = f18 * f2;
		f6 = f5 + f6;
		f5 = f19 * f3;
		f6 = f6 + f7;
		f6 = f6 + f5;

		if (f6 >= 0.0f) {
			break;
		}

		f0 = -f0;
		f1 = -f1;
		f2 = -f2;
		f3 = -f3;
	}

	if (f6 < -0.99994999170303f) {
		f6 = f30 * f16;
		f5 = f5 - f30;
		f7 = f5 * f0;
		f0 = f7 - f6;
		f6 = f30 * f17;
		f7 = f5 * f1;
		f8 = f30 * f18;
		f1 = f7 - f6;
		f9 = f5 * f2;
		f6 = f30 * f19;
		f2 = f9 - f8;
		f7 = f5 * f3;
		f2 = f7 - f6;
		return;
	}

	if (f6 <= 0.99994999170303f) {
		f7 = modelasmAcosOrAsin(f6);
		f20 = f0;
		f21 = sinf(f7);
		f22 = sinf((1.0f - f30) * f7);
		f0 = sinf(f7 * f30);

		f5 = f22 / f21;
		f30 = f0 / f21;
		f0 = f20;
	} else {
		f5 = 1.0f - f30;
	}

	f6 = f5 * f16;
	f7 = f30 * f0;
	f8 = f5 * f17;
	f0 = f6 + f7;
	f9 = f30 * f1;
	f6 = f5 * f18;
	f1 = f8 + f9;
	f7 = f30 * f2;
	f8 = f5 * f19;
	f2 = f6 + f7;
	f9 = f30 * f3;
	f3 = f8 + f9;
}

/**
 * Expects: f0 f1 f2 f3 f4 f5
 */
static void modelasmMathPain2(void)
{
	f32 f6;
	f32 f7;
	f32 f8;
	f32 f9;
	f32 f10;
	f32 f11;
	f32 f26;

	f6 = f0 * f2;
	f7 = f0 * f3;
	f8 = f1 * f2;
	f9 = f1 * f3;
	f0 = f6 * f4;
	f1 = f9 * f5;
	f2 = f8 * f4;
	f0 = f0 + f1;
	f1 = f7 * f5;
	f3 = f7 * f5;
	f10 = f7 * f4;
	f1 = f2 - f1;
	f11 = f8 * f5;
	f26 = f6 * f5;
	f2 = f10 + f11;
	f10 = f9 * f4;
	f3 = f26 - f10;
}

static void modelasmPrepareRotMtx180(s32 t2, s32 t3, s32 t4)
{
	// Very close to: 1.0f / (180 * 180 / M_PI)
	f32 f8 = t4 * 0.000095873801910784f;
	f32 f7 = t3 * 0.000095873801910784f;
	f32 f6 = t2 * 0.000095873801910784f;

	f5 = sinf(f8);
	f4 = sinf(f8 + 1.570796251297f);
	f3 = sinf(f7);
	f2 = sinf(f7 + 1.570796251297f);
	f1 = sinf(f6);
	f0 = sinf(f6 + 1.570796251297f);
}

static void modelasmPrepareRotMtx360(s32 t2, s32 t3, s32 t4)
{
	// Very close to: 1.0f / (360 * 180 / M_PI)
	f32 f8 = t4 * 0.000047936900955392f;
	f32 f7 = t3 * 0.000047936900955392f;
	f32 f6 = t2 * 0.000047936900955392f;

	f5 = sinf(f8);
	f4 = sinf(f8 + 1.5707963705063f);
	f3 = sinf(f7);
	f2 = sinf(f7 + 1.5707963705063f);
	f1 = sinf(f6);
	f0 = sinf(f6 + 1.5707963705063f);
}

/**
 * Expects: f0 f1 f2 f3 f4 f5
 */
static void modelasmMathPain3(void)
{
	f32 f6 = f1 * f5;
	f32 f7 = f0 * f5;
	f32 f8 = f1 * f4;
	f32 f9 = f0 * f4;

	f12 = f2 * f4;
	f13 = f2 * f5;
	f14 = -f3;

	f15 = f8 * f3 - f7;
	f16 = f6 * f3 + f9;
	f17 = f1 * f2;

	f18 = f9 * f3 + f6;
	f19 = f7 * f3 - f8;
	f20 = f0 * f2;
}

/**
 * Expects: f0 f1 f2 f3
 */
static void modelasmMathPain4(void)
{
	f32 f6;
	f32 f7;
	f32 f8;
	f32 f9;
	f32 f10;
	f32 f11;
	f32 f24;
	f32 f25;
	f32 f26;
	f32 f27;
	f32 f28;

	f4 = f0 * f0;
	f5 = f1 * f1;
	f6 = f2 * f2;
	f4 = f4 + f5;
	f5 = f3 * f3;
	f4 = f4 + f6;
	f4 = f4 + f5;
	f4 = 2.0f / f4;
	f5 = f1 * f4;
	f6 = f2 * f4;
	f7 = f3 * f4;
	f8 = f0 * f5;
	f9 = f0 * f6;
	f10 = f0 * f7;
	f11 = f1 * f5;
	f24 = f1 * f6;
	f16 = 1.0f - f11;
	f25 = f1 * f7;
	f20 = 1.0f - f11;
	f26 = f2 * f6;
	f13 = f24 + f10;
	f27 = f2 * f7;
	f12 = 1.0f - f26;
	f28 = f3 * f7;
	f14 = f25 - f9;
	f12 = f12 - f28;
	f15 = f24 - f10;
	f16 = f16 - f28;
	f17 = f27 + f8;
	f18 = f25 + f9;
	f19 = f27 - f8;
	f20 = f20 - f26;
}

/**
 * Expects: f12-f23
 */
static void modelasmMtxMultiply(Mtxf *src, Mtxf *dst)
{
	f32 f0;
	f32 f1;
	f32 f2;
	f32 f3;
	f32 f4;
	f32 f5;
	f32 f6;
	f32 f7;
	f32 f8;
	f32 f9;
	f32 f10;
	s32 i;

	for (i = 0; i < 3; i++) {
		f0 = src->m[0][i];
		f1 = src->m[1][i];
		f2 = src->m[2][i];
		f3 = src->m[3][i];

		f4 = f0 * f12;
		f5 = f1 * f13;
		f6 = f2 * f14;

		dst->m[0][i] = f4 + f5 + f6;

		f8 = f0 * f15;
		f9 = f1 * f16;
		f10 = f2 * f17;

		dst->m[1][i] = f8 + f9 + f10;

		f4 = f0 * f18;
		f5 = f1 * f19;
		f6 = f2 * f20;

		dst->m[2][i] = f4 + f5 + f6;

		f8 = f0 * f21;
		f9 = f1 * f22;
		f10 = f2 * f23;

		dst->m[3][i] = f8 + f9 + f10 + f3;
	}

	dst->m[0][3] = 0;
	dst->m[1][3] = 0;
	dst->m[2][3] = 0;
	dst->m[3][3] = 1;
}

static Mtxf *modelasmFindNodeMtx(struct model *model, struct modelnode *node)
{
	do {
		u8 type = node->type & 0xff;

		if (type == MODELNODETYPE_CHRINFO) {
			return &model->matrices[node->rodata->chrinfo.mtxindex];
		}

		if (type == MODELNODETYPE_POSITION) {
			return &model->matrices[node->rodata->position.mtxindex0];
		}

		if (type == MODELNODETYPE_POSITIONHELD) {
			return &model->matrices[node->rodata->positionheld.mtxindex];
		}

		node = node->parent;
	} while (node);

	return NULL;
}

/**
 * See similar function func0f096890.
 */
static f32 modelasmAcosOrAsin(f32 f6)
{
	s32 t2;
	s32 t3;
	u16 *array;
	s32 shiftamount;
	s32 mask;
	s32 s0;
	s32 s1;
	s32 s2;
	s32 s3;
	s32 s4;

	t2 = f6 * 32767.0f;

	if (t2 > 32767) {
		t2 = 32767;
	} else if (t2 < -32767) {
		t2 = -32767;
	}

	t3 = t2;

	if (t3 < 0) {
		t3 = -t3;
	}

	if (t3 >= 32736) {
		array = &var8006ae90[126];
		t3 -= 32736;
		shiftamount = 3;
		mask = 0x07;
	} else if (t3 >= 30720) {
		array = &var8006ae90[62];
		t3 -= 30720;
		shiftamount = 5;
		mask = 0x1f;
	} else {
		array = &var8006ae90[0];
		shiftamount = 9;
		mask = 0x1ff;
	}

	s0 = t3 >> shiftamount;
	array += s0;
	s1 = array[0];
	s2 = array[1];
	s3 = s1 - s2;
	s4 = t3 & mask;
	s3 *= s4;
	s3 >>= shiftamount;
	t3 = s1 - s3;

	if (t2 < 0) {
		t3 = 0xffff - t3;
	}

	return 0.000047937632189132f * t3;
}

f32 cosf(f32 radians)
{
	return sinf(radians + 1.570796251297f);
}

f32 sinf(f32 radians)
{
	f32 f0;
	f32 f13;
	f32 f14;
	f32 f15;
	s32 t0;
	s32 t1;
	f32 ret;

	t0 = *(u32 *) &radians;
	t0 = (t0 >> 22) & 0x1ff;

	if (t0 < 255) {
		if (t0 >= 230) {
			f14 = radians * radians;

			ret = 0.0000026057805371238f;
			ret *= f14;
			ret += -0.0001980960223591f;
			ret *= f14;
			ret += 0.0083330664783716f;
			ret *= f14;
			ret += -0.16666659712791f;
			ret *= f14;
			ret *= radians;
			ret += radians;
		} else {
			ret = radians;
		}
	} else {
		if (t0 < 310) {
			f14 = radians * 0.31830987334251f;

			t1 = (s32) (f14 > 0.0f ? f14 + 0.5f : f14 - 0.5f);
			f14 = t1;

			f15 = M_PI;
			f15 *= f14;
			radians -= f15;

			f15 = 0.000000031786509424592f;
			f15 *= f14;
			radians -= f15;

			f14 = radians * radians;

			ret = 0.0000026057805371238f;
			ret *= f14;
			ret += -0.0001980960223591f;
			ret *= f14;
			ret += 0.0083330664783716f;
			ret *= f14;
			ret += -0.16666659712791f;
			ret *= f14;
			ret *= radians;
			ret += radians;

			if (t1 & 1) {
				ret = -ret;
			}
		} else {
			ret = 0;
		}
	}

	return ret;
}
