/**
 * effect_presentation_renderer.c -- production world renderer for generic
 * source-backed effect presentation snapshots.
 */
#include <float.h>
#include <math.h>
#include <string.h>
#include <ultra64.h>

#include "effect_gameplay_runtime.h"
#include "effect_instance_runtime.h"
#include "effect_presentation_runtime.h"
#include "game/camera.h"
#include "game/dlights.h"
#include "game/effect_presentation_renderer.h"
#include "game/gfxmemory.h"
#include "game/tex.h"
#include "lib/mtx.h"
#include "lib/vi.h"
#include "types.h"
#include "system.h"

static u64 s_LastAuditedEffectInstance;

static u8 effectPresentationByte(f32 value)
{
	if (value < 0.0f) value = 0.0f;
	if (value > 1.0f) value = 1.0f;
	return (u8)(value * 255.0f + 0.5f);
}

static Gfx *effectPresentationRenderParticleQuad(Gfx *gdl,
	const effect_gameplay_particle_snapshot_t *particle, f32 scale,
	f32 alpha_scale, u8 texture_width, u8 texture_height)
{
	Mtxf matrix;
	Mtxf *fixed;
	Vtx *vertices;
	Col *colour;
	struct coord position;
	f32 size = particle->size * scale;
	f32 material_color[4];
	u32 word;
	if (!(size > 0.0f) || size > FLT_MAX || !(alpha_scale > 0.0f)) return gdl;

	mtx4LoadIdentity(&matrix);
	matrix.m[0][0] = size;
	matrix.m[1][1] = size;
	matrix.m[2][2] = size;
	position.x = particle->position.x;
	position.y = particle->position.y;
	position.z = particle->position.z;
	mtx4SetTranslation(&position, &matrix);
	mtx00015be0(camGetWorldToScreenMtxf(), &matrix);
	fixed = gfxAllocateMatrix();
	mtxF2L(&matrix, fixed);
	gSPMatrix(gdl++, osVirtualToPhysical(fixed),
		G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

	vertices = gfxAllocateVertices(4);
	memset(vertices, 0, 4 * sizeof(*vertices));
	vertices[0].x = -1; vertices[0].y = -1;
	vertices[1].x =  1; vertices[1].y = -1;
	vertices[2].x =  1; vertices[2].y =  1;
	vertices[3].x = -1; vertices[3].y =  1;
	vertices[0].s = 0;       vertices[0].t = 0;
	vertices[1].s = (s16)(texture_width << 5); vertices[1].t = 0;
	vertices[2].s = (s16)(texture_width << 5);
	vertices[2].t = (s16)(texture_height << 5);
	vertices[3].s = 0;
	vertices[3].t = (s16)(texture_height << 5);
	for (s32 i = 0; i < 4; i++) vertices[i].colour = 0;
	colour = gfxAllocateColours(1);
	effectPresentationMaterialColor(particle->tint,
		particle->material_shading_model, particle->material_roughness,
		particle->material_metallic, particle->material_emissive,
		material_color);
	word = ((u32)effectPresentationByte(material_color[0]) << 24)
		| ((u32)effectPresentationByte(material_color[1]) << 16)
		| ((u32)effectPresentationByte(material_color[2]) << 8)
		| effectPresentationByte(material_color[3] * particle->intensity *
			alpha_scale);
	colour[0].word = PD_BE32(word);
	gSPColor(gdl++, osVirtualToPhysical(colour), 1);
	gSPVertex(gdl++, osVirtualToPhysical(vertices), 4, 0);
	gSP1Triangle(gdl++, 0, 1, 2, 0);
	gSP1Triangle(gdl++, 0, 2, 3, 0);
	return gdl;
}

static Gfx *effectPresentationRenderColourQuad(Gfx *gdl,
	const effect_presentation_command_t *command, f32 scale, f32 alpha_scale)
{
	Mtxf matrix;
	Mtxf *fixed;
	Vtx *vertices;
	Col *colour;
	struct coord position;
	f32 size = command->size * scale;
	f32 material_color[4];
	u32 word;
	if (!(size > 0.0f) || size > FLT_MAX || !(alpha_scale > 0.0f)) return gdl;

	mtx4LoadIdentity(&matrix);
	if (strcmp(command->attachment, "surface") == 0) {
		f32 p_len = sqrtf(command->primary_direction.x * command->primary_direction.x +
			command->primary_direction.y * command->primary_direction.y +
			command->primary_direction.z * command->primary_direction.z);
		f32 s_len = sqrtf(command->secondary_direction.x * command->secondary_direction.x +
			command->secondary_direction.y * command->secondary_direction.y +
			command->secondary_direction.z * command->secondary_direction.z);
		if (!(p_len > 0.0001f) || !(s_len > 0.0001f)) return gdl;
		matrix.m[0][0] = command->primary_direction.x / p_len * size;
		matrix.m[0][1] = command->primary_direction.y / p_len * size;
		matrix.m[0][2] = command->primary_direction.z / p_len * size;
		matrix.m[1][0] = command->secondary_direction.x / s_len * size;
		matrix.m[1][1] = command->secondary_direction.y / s_len * size;
		matrix.m[1][2] = command->secondary_direction.z / s_len * size;
	} else {
		matrix.m[0][0] = size;
		matrix.m[1][1] = size;
		matrix.m[2][2] = size;
	}
	position.x = command->position.x;
	position.y = command->position.y;
	position.z = command->position.z;
	mtx4SetTranslation(&position, &matrix);
	mtx00015be0(camGetWorldToScreenMtxf(), &matrix);
	fixed = gfxAllocateMatrix();
	mtxF2L(&matrix, fixed);
	gSPMatrix(gdl++, osVirtualToPhysical(fixed),
		G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

	vertices = gfxAllocateVertices(4);
	memset(vertices, 0, 4 * sizeof(*vertices));
	vertices[0].x = -1; vertices[0].y = -1;
	vertices[1].x =  1; vertices[1].y = -1;
	vertices[2].x =  1; vertices[2].y =  1;
	vertices[3].x = -1; vertices[3].y =  1;
	for (s32 i = 0; i < 4; i++) vertices[i].colour = 0;
	colour = gfxAllocateColours(1);
	effectPresentationMaterialColor(command->rgba,
		command->material_shading_model, command->material_roughness,
		command->material_metallic, command->material_emissive, material_color);
	word = ((u32)effectPresentationByte(material_color[0]) << 24)
		| ((u32)effectPresentationByte(material_color[1]) << 16)
		| ((u32)effectPresentationByte(material_color[2]) << 8)
		| effectPresentationByte(material_color[3] * command->intensity *
			alpha_scale);
	colour[0].word = PD_BE32(word);
	gSPColor(gdl++, osVirtualToPhysical(colour), 1);
	gSPVertex(gdl++, osVirtualToPhysical(vertices), 4, 0);
	gSP1Triangle(gdl++, 0, 1, 2, 0);
	gSP1Triangle(gdl++, 0, 2, 3, 0);
	return gdl;
}

static Gfx *effectPresentationRenderScreenTexture(Gfx *gdl,
	const effect_presentation_command_t *command)
{
	struct textureconfig texture;
	s32 left = viGetViewLeft();
	s32 top = viGetViewTop();
	s32 width = viGetViewWidth();
	s32 height = viGetViewHeight();
	u8 r;
	f32 material_color[4];
	u8 g;
	u8 b;
	u8 a;
	s32 dsdx;
	s32 dtdy;
	effectPresentationMaterialColor(command->rgba,
		command->material_shading_model, command->material_roughness,
		command->material_metallic, command->material_emissive, material_color);
	r = effectPresentationByte(material_color[0]);
	g = effectPresentationByte(material_color[1]);
	b = effectPresentationByte(material_color[2]);
	a = effectPresentationByte(material_color[3] * command->intensity);
	if (command->texture_num < 0 || width <= 0 || height <= 0) return gdl;
	left += (s32)((1.0f - command->size) * width * 0.5f);
	top += (s32)((1.0f - command->size) * height * 0.5f);
	width = (s32)(width * command->size);
	height = (s32)(height * command->size);
	if (width <= 0 || height <= 0) return gdl;
	memset(&texture, 0, sizeof(texture));
	texture.texturenum = (texnum_t)command->texture_num;
	texLoadFromConfig(&texture);
	if (texture.unk0b != 1 || !texture.textureptr ||
			texture.width == 0 || texture.height == 0) return gdl;
	dsdx = (texture.width * 1024) / width;
	dtdy = (texture.height * 1024) / height;
	if (dsdx < 1) dsdx = 1;
	if (dtdy < 1) dtdy = 1;
	texSelect(&gdl, &texture, 4, 1, 2, true, NULL);
	gDPPipeSync(gdl++);
	gDPSetCycleType(gdl++, G_CYC_1CYCLE);
	gDPSetTexturePersp(gdl++, G_TP_NONE);
	gDPSetTextureFilter(gdl++, G_TF_BILERP);
	gDPSetAlphaCompare(gdl++, G_AC_NONE);
	gDPSetCombineMode(gdl++, G_CC_MODULATEIA_PRIM,
		G_CC_MODULATEIA_PRIM);
	gDPSetPrimColor(gdl++, 0, 0, r, g, b, a);
	gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
	gSPTextureRectangle(gdl++, left << 2, top << 2,
		(left + width) << 2, (top + height) << 2, G_TX_RENDERTILE,
		0, 0, dsdx, dtdy);
	return gdl;
}

static Gfx *effectPresentationRenderBeam(Gfx *gdl,
	const effect_presentation_command_t *command)
{
	Mtxf *fixed;
	Vtx *vertices;
	Col *colour;
	struct coord direction;
	struct coord side;
	f32 length;
	/* World size defaults to 16; width is authored independently and the
	 * product gives both accepted fields a visible beam contribution. */
	f32 width = command->width * 0.5f * (command->size / 16.0f);
	f32 material_color[4];
	f32 pulse;
	u32 word;

	direction.x = command->target_position.x - command->source_position.x;
	direction.y = command->target_position.y - command->source_position.y;
	direction.z = command->target_position.z - command->source_position.z;
	length = sqrtf(direction.x * direction.x + direction.y * direction.y +
		direction.z * direction.z);
	if (!(length > 0.0001f) || !(width > 0.0f)) return gdl;
	/* A stable world ribbon. The alternate axis avoids collapse for vertical
	 * beams; no native weapon beam type or texture is substituted. */
	if (direction.x * direction.x + direction.z * direction.z > 0.0001f) {
		f32 inv = 1.0f / sqrtf(direction.x * direction.x +
			direction.z * direction.z);
		side.x = -direction.z * inv * width;
		side.y = 0.0f;
		side.z = direction.x * inv * width;
	} else {
		side.x = width; side.y = 0.0f; side.z = 0.0f;
	}
	fixed = gfxAllocateMatrix();
	mtxF2L(camGetWorldToScreenMtxf(), fixed);
	gSPMatrix(gdl++, osVirtualToPhysical(fixed),
		G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
	vertices = gfxAllocateVertices(4);
	memset(vertices, 0, 4 * sizeof(*vertices));
#define SET_BEAM_VERTEX(v, p, sign) do { \
	(v).x = (s16)((p).x + side.x * (sign)); \
	(v).y = (s16)((p).y + side.y * (sign)); \
	(v).z = (s16)((p).z + side.z * (sign)); \
	(v).colour = 0; \
} while (0)
	SET_BEAM_VERTEX(vertices[0], command->source_position, -1.0f);
	SET_BEAM_VERTEX(vertices[1], command->source_position,  1.0f);
	SET_BEAM_VERTEX(vertices[2], command->target_position,  1.0f);
	SET_BEAM_VERTEX(vertices[3], command->target_position, -1.0f);
#undef SET_BEAM_VERTEX
	colour = gfxAllocateColours(1);
	effectPresentationMaterialColor(command->rgba,
		command->material_shading_model, command->material_roughness,
		command->material_metallic, command->material_emissive, material_color);
	pulse = 0.75f + 0.25f * sinf(command->time * command->speed * 6.2831853f);
	word = ((u32)effectPresentationByte(material_color[0]) << 24)
		| ((u32)effectPresentationByte(material_color[1]) << 16)
		| ((u32)effectPresentationByte(material_color[2]) << 8)
		| effectPresentationByte(material_color[3] * command->intensity * pulse);
	colour[0].word = PD_BE32(word);
	gSPColor(gdl++, osVirtualToPhysical(colour), 1);
	gSPVertex(gdl++, osVirtualToPhysical(vertices), 4, 0);
	gSP1Triangle(gdl++, 0, 1, 2, 0);
	gSP1Triangle(gdl++, 0, 2, 3, 0);
	return gdl;
}

void effectPresentationApplyLights(void)
{
	const size_t count = effectPresentationRuntimeSnapshotCount();
	for (size_t i = 0; i < count; i++) {
		effect_presentation_command_t command;
		if (!effectPresentationRuntimeSnapshot(i, &command) ||
				command.channel != EFFECT_PRESENTATION_CHANNEL_LIGHT) continue;
		for (s32 room = 0; room < command.room_count; room++) {
			/* Room flash is the production world-light path. The additive halo
			 * below carries the authored material color; room lighting supplies
			 * the actual geometry illumination. */
			f32 authored_light = command.intensity * command.glow;
			s32 increment;
			if (!(authored_light > 0.0f)) continue;
			increment = (s32)(authored_light * 32.0f + 0.5f);
			if (increment < 1) increment = 1;
			if (increment > 255) increment = 255;
			roomFlashLocalLighting(command.rooms[room], increment, 255);
		}
	}
}

static Gfx *effectPresentationRenderCommands(Gfx *gdl)
{
	const size_t count = effectPresentationRuntimeSnapshotCount();
	for (size_t i = 0; i < count; i++) {
		effect_presentation_command_t command;
		if (!effectPresentationRuntimeSnapshot(i, &command) ||
				(command.channel == EFFECT_PRESENTATION_CHANNEL_SCREEN &&
					!command.texture_ref[0])) continue;
		if (effectInstanceRuntimeAuditEnabled() &&
				command.instance_id > s_LastAuditedEffectInstance) {
			s_LastAuditedEffectInstance = command.instance_id;
			sysLogPrintf(LOG_NOTE,
				"EFFECT.PRESENTATION.RENDER.AUDIT: instance=%llu snapshots=%zu channel=%d shader=%s intensity=%.3f",
				(unsigned long long)command.instance_id, count,
				(s32)command.channel, command.shader_id,
				(double)command.intensity);
		}
		if (command.channel == EFFECT_PRESENTATION_CHANNEL_SCREEN) {
			gdl = effectPresentationRenderScreenTexture(gdl, &command);
			continue;
		}
		gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_FOG);
		gDPSetCycleType(gdl++, G_CYC_1CYCLE);
		gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
		if (strcmp(command.shader_id, "needler_pink_burst") == 0 ||
				strcmp(command.material_shading_model, "classic_emissive") == 0) {
			gDPSetRenderMode(gdl++, G_RM_ADD, G_RM_ADD2);
		} else if (strcmp(command.material_shading_model, "classic_alpha") == 0) {
			gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
		} else {
			gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_SURF,
				G_RM_AA_ZB_XLU_SURF2);
		}
		gDPSetAlphaCompare(gdl++, G_AC_NONE);
		if (command.channel == EFFECT_PRESENTATION_CHANNEL_BEAM) {
			gdl = effectPresentationRenderBeam(gdl, &command);
		} else if (command.channel == EFFECT_PRESENTATION_CHANNEL_LIGHT) {
			f32 spread = 1.5f + command.material_roughness;
			f32 highlight = command.material_emissive ? 1.0f :
				(0.35f + command.material_metallic * 0.65f);
			gdl = effectPresentationRenderColourQuad(gdl, &command, spread,
				0.35f * command.glow);
			gdl = effectPresentationRenderColourQuad(gdl, &command, 1.0f,
				highlight * command.glow);
		} else if (command.channel == EFFECT_PRESENTATION_CHANNEL_DECAL) {
			if (strcmp(command.shader_id, "needler_pink_burst") == 0) {
				effect_presentation_command_t outer = command;
				/* The Needler pipeline is intentionally distinct from classic_tint:
				 * an additive outer flare uses the authored secondary tint. */
				memcpy(outer.rgba, command.secondary_rgba, sizeof(outer.rgba));
				gdl = effectPresentationRenderColourQuad(gdl, &outer,
					1.75f, 0.35f * command.glow);
			}
			gdl = effectPresentationRenderColourQuad(gdl, &command, 1.0f, 1.0f);
		}
	}
	return gdl;
}

static Gfx *effectPresentationRenderParticles(Gfx *gdl)
{
	const size_t count = effectGameplayRuntimeParticleCount();
	for (size_t i = 0; i < count; i++) {
		effect_gameplay_particle_snapshot_t particle;
		struct textureconfig texture;
		if (!effectGameplayRuntimeParticleSnapshot(i, &particle) ||
				particle.texture_num < 0 || !particle.texture_id[0] ||
				!effectPresentationShaderSupported("effect.particle",
					particle.shader_id)) continue;
		memset(&texture, 0, sizeof(texture));
		texture.texturenum = (texnum_t)particle.texture_num;
		/* texSelect routes this catalog-owned texnum through the active public
		 * texture provider. There is no built-in spark/smoke texture fallback. */
		texLoadFromConfig(&texture);
		if (texture.unk0b != 1 || !texture.textureptr ||
				texture.width == 0 || texture.height == 0) continue;
		texSelect(&gdl, &texture, 4, 1, 2, true, NULL);
		gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_FOG);
		gDPSetCycleType(gdl++, G_CYC_1CYCLE);
		if (strcmp(particle.material_shading_model, "classic_emissive") == 0) {
			gDPSetRenderMode(gdl++, G_RM_ADD, G_RM_ADD2);
		} else if (strcmp(particle.material_shading_model, "classic_alpha") == 0) {
			gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
		} else {
			gDPSetRenderMode(gdl++, G_RM_AA_ZB_XLU_SURF,
				G_RM_AA_ZB_XLU_SURF2);
		}
		gDPSetAlphaCompare(gdl++, G_AC_NONE);
		if (particle.glow > 0.0f) {
			gdl = effectPresentationRenderParticleQuad(gdl, &particle,
				1.0f + particle.glow + particle.material_roughness,
				particle.glow * (0.35f + particle.material_metallic * 0.65f),
				texture.width, texture.height);
		}
		gdl = effectPresentationRenderParticleQuad(gdl, &particle, 1.0f, 1.0f,
			texture.width, texture.height);
	}
	return gdl;
}

size_t effectPresentationRenderableParticleCount(void)
{
	size_t renderable = 0;
	const size_t count = effectGameplayRuntimeParticleCount();
	for (size_t i = 0; i < count; i++) {
		effect_gameplay_particle_snapshot_t particle;
		if (effectGameplayRuntimeParticleSnapshot(i, &particle) &&
				particle.texture_num >= 0 && particle.texture_id[0] &&
				effectPresentationShaderSupported("effect.particle",
					particle.shader_id)) renderable++;
	}
	return renderable;
}

Gfx *effectPresentationRenderWorld(Gfx *gdl)
{
	gdl = effectPresentationRenderCommands(gdl);
	return effectPresentationRenderParticles(gdl);
}
