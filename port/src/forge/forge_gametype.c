/**
 * forge_gametype.c -- Custom game type runtime (F5b).
 *
 * Wave spawner + boss state manager + role-mode dispatcher. The game type
 * definition lives in forge_core's s_gametype. This file ticks it forward:
 *
 *  - On SPAWN_WAVE action (via forge_logic), instantiate `count` AI chr
 *    objects inside the wave's spawn zone volume.
 *  - Track wave progression: intermission timer, all-dead detection.
 *  - Boss health + phase transitions fire phase_logic_node_uids via
 *    forgeLogicFireEvent.
 *
 * Full engine integration (actually spawning a live chr that takes damage)
 * depends on the F3 map-instantiate path landing; the functions here log
 * what they would do so the editor UI can exercise the end-to-end flow.
 */

#include "forge/forge_core.h"

#include <stdio.h>
#include <string.h>

#include "system.h"

static s32 s_current_wave_index = -1;
static f32 s_intermission_timer = 0.0f;
static s32 s_wave_active = 0;

s32 forgeGametypeCurrentWaveIndex(void) { return s_current_wave_index; }
s32 forgeGametypeWaveActive(void)       { return s_wave_active; }
f32 forgeGametypeIntermissionTimer(void){ return s_intermission_timer; }

void forgeGametypeReset(void)
{
	s_current_wave_index = -1;
	s_intermission_timer = 0.0f;
	s_wave_active = 0;
}

/* Called from forge_logic when a SPAWN_WAVE action fires. */
void forgeGametypeTriggerWave(s32 wave_index)
{
	forge_gametype_t *gt = forgeGameType();
	if (wave_index < 0 || wave_index >= gt->num_waves) {
		sysLogPrintf(LOG_WARNING, "FORGE.WAVE: invalid index %d", wave_index);
		return;
	}
	const forge_wave_t *w = &gt->waves[wave_index];
	if (!w->in_use) {
		sysLogPrintf(LOG_WARNING, "FORGE.WAVE: index %d not in use", wave_index);
		return;
	}

	s_current_wave_index = wave_index;
	s_wave_active = 1;
	s_intermission_timer = w->intermission_sec;

	/* Resolve spawn zone centre. */
	forge_object_t *zone = forgeObjectFindByUid(w->spawn_zone_uid);
	f32 zx = 0, zy = 0, zz = 0;
	if (zone) {
		zx = zone->pos[0]; zy = zone->pos[1]; zz = zone->pos[2];
	}

	sysLogPrintf(LOG_NOTE,
			"FORGE.WAVE: spawn wave %d count=%d scale=%.2f hp=%.2f at (%.0f,%.0f,%.0f) boss=%d",
			wave_index, w->enemy_count, w->enemy_scale, w->enemy_health_mult,
			zx, zy, zz, w->is_boss);

	/* Instantiate the enemy AI objects -- data model only for now. */
	s32 placed = 0;
	for (s32 i = 0; i < w->enemy_count; ++i) {
		forge_object_t *o = forgeObjectAllocate(FORGE_CAT_AI, w->enemy_catalog_id);
		if (!o) break;
		/* Simple scatter inside the zone (or at zone centre if none). */
		f32 ang = (f32)i / (f32)(w->enemy_count ? w->enemy_count : 1);
		ang *= 6.2831853f;
		f32 r = 128.0f;
		o->pos[0] = zx + r * (f32)(i % 8) * 0.125f * (i & 1 ? 1.0f : -1.0f);
		o->pos[1] = zy;
		o->pos[2] = zz + r * ((i >> 3) & 7) * 0.125f;
		(void)ang;
		o->props.ai.health_mult = w->enemy_health_mult;
		o->props.ai.is_boss = w->is_boss;
		if (w->is_boss) {
			forgeCopyStr(o->props.ai.boss_name, "Wave Boss", FORGE_NAME_LEN);
			o->props.ai.boss_scale = w->enemy_scale;
		}
		++placed;
	}
	if (placed < w->enemy_count) {
		sysLogPrintf(LOG_WARNING, "FORGE.WAVE: only placed %d of %d enemies",
				placed, w->enemy_count);
	}
}

/* Per-tick advance. */
void forgeGametypeTick(f32 dt)
{
	if (!s_wave_active) return;
	if (s_intermission_timer > 0.0f) {
		s_intermission_timer -= dt;
		if (s_intermission_timer < 0.0f) s_intermission_timer = 0.0f;
	}
	/* Full all-dead detection depends on gameplay wire-in; skeleton-only for now. */
}

/* ============================================================
 * Utility: apply modifier flags at match start
 * ============================================================ */

void forgeGametypeApplyModifiers(u32 flags)
{
	if (flags & FORGE_MOD_LOW_GRAVITY)   sysLogPrintf(LOG_NOTE, "FORGE.GT: modifier LOW_GRAVITY");
	if (flags & FORGE_MOD_ONE_HIT_KILLS) sysLogPrintf(LOG_NOTE, "FORGE.GT: modifier ONE_HIT_KILLS");
	if (flags & FORGE_MOD_INFINITE_AMMO) sysLogPrintf(LOG_NOTE, "FORGE.GT: modifier INFINITE_AMMO");
	if (flags & FORGE_MOD_NO_RADAR)      sysLogPrintf(LOG_NOTE, "FORGE.GT: modifier NO_RADAR");
	if (flags & FORGE_MOD_FRIENDLY_FIRE) sysLogPrintf(LOG_NOTE, "FORGE.GT: modifier FRIENDLY_FIRE");
	if (flags & FORGE_MOD_NO_AUTO_AIM)   sysLogPrintf(LOG_NOTE, "FORGE.GT: modifier NO_AUTO_AIM");
	if (flags & FORGE_MOD_FAST_MOVE)     sysLogPrintf(LOG_NOTE, "FORGE.GT: modifier FAST_MOVE");
	if (flags & FORGE_MOD_TEAM_SHUFFLE)  sysLogPrintf(LOG_NOTE, "FORGE.GT: modifier TEAM_SHUFFLE");
}

/* ============================================================
 * Role assignment (VIP / Juggernaut / Infected / etc.)
 * ============================================================ */

const char *forgeGametypeRoleName(forge_gametype_role_t r)
{
	switch (r) {
	case FORGE_ROLE_NONE:       return "None";
	case FORGE_ROLE_INFECTED:   return "Infection";
	case FORGE_ROLE_VIP:        return "VIP";
	case FORGE_ROLE_JUGGERNAUT: return "Juggernaut";
	case FORGE_ROLE_DEFENDER:   return "Horde Defender";
	case FORGE_ROLE_HUNTER:     return "Gun-Game Hunter";
	default:                    return "?";
	}
}
