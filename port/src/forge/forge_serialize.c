/**
 * forge_serialize.c -- map.json save/load (F3).
 *
 * Writes the full editor state (§10.1 schema) to
 *   mods/Forge Maps/<slug>/mod.json       — mod manifest for the loader
 *   mods/Forge Maps/<slug>/map.json       — map payload
 *
 * JSON emitter is hand-rolled, intentionally simple: we only ever serialise
 * primitive types and flat arrays of our own objects, so a tiny write-helper
 * suffices. Reader is a tolerant lexer that skips whitespace/comments and
 * matches on known key names.
 *
 * NOTE: this module is self-contained -- no dependency on cJSON or on the
 * legacy theme/chrome JSON parsers. It deliberately doesn't validate every
 * field; missing keys keep their defaults from forgeCoreInit().
 */

#include "forge/forge_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "system.h"
#include "fs.h"

/* ============================================================
 * Path helpers
 * ============================================================ */

static void forgeSerializeBuildModDir(const char *slug, char *out, size_t n)
{
	const char *modroot = fsGetModDir();
	if (!modroot) modroot = "mods";
	snprintf(out, n, "%s/Forge Maps/%s", modroot, slug ? slug : "untitled");
}

static s32 forgeSerializeMakeDirsRecursive(const char *path)
{
	if (!path || !*path) return 0;
	char buf[FS_MAXPATH];
	snprintf(buf, sizeof(buf), "%s", path);
	/* Ensure intermediate dirs exist. */
	for (size_t i = 1; i < sizeof(buf) && buf[i]; ++i) {
		if (buf[i] == '/' || buf[i] == '\\') {
			char save = buf[i];
			buf[i] = '\0';
			fsCreateDir(buf);
			buf[i] = save;
		}
	}
	return fsCreateDir(buf);
}

/* ============================================================
 * Writer -- minimal JSON emitter
 * ============================================================ */

typedef struct forge_jw {
	FILE *f;
	s32 indent;
	s32 first_child; /* suppress leading comma */
} forge_jw_t;

static void jwIndent(forge_jw_t *w)
{
	for (s32 i = 0; i < w->indent; ++i) fputc('\t', w->f);
}

static void jwComma(forge_jw_t *w)
{
	if (!w->first_child) fputs(",\n", w->f);
	else                 fputs("\n", w->f);
	w->first_child = 0;
}

static void jwObjectBegin(forge_jw_t *w, const char *key)
{
	jwComma(w);
	jwIndent(w);
	if (key) fprintf(w->f, "\"%s\": {", key);
	else     fputs("{", w->f);
	++w->indent;
	w->first_child = 1;
}

static void jwObjectEnd(forge_jw_t *w)
{
	--w->indent;
	fputs("\n", w->f);
	jwIndent(w);
	fputs("}", w->f);
	w->first_child = 0;
}

static void jwArrayBegin(forge_jw_t *w, const char *key)
{
	jwComma(w);
	jwIndent(w);
	if (key) fprintf(w->f, "\"%s\": [", key);
	else     fputs("[", w->f);
	++w->indent;
	w->first_child = 1;
}

static void jwArrayEnd(forge_jw_t *w)
{
	--w->indent;
	fputs("\n", w->f);
	jwIndent(w);
	fputs("]", w->f);
	w->first_child = 0;
}

static void jwStringEsc(FILE *f, const char *s)
{
	fputc('"', f);
	if (s) {
		for (; *s; ++s) {
			unsigned char c = (unsigned char)*s;
			if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
			else if (c == '\n') fputs("\\n", f);
			else if (c == '\r') fputs("\\r", f);
			else if (c == '\t') fputs("\\t", f);
			else if (c < 0x20) fprintf(f, "\\u%04x", c);
			else fputc(c, f);
		}
	}
	fputc('"', f);
}

static void jwKvString(forge_jw_t *w, const char *k, const char *v)
{
	jwComma(w);
	jwIndent(w);
	fprintf(w->f, "\"%s\": ", k);
	jwStringEsc(w->f, v);
}

static void jwKvInt(forge_jw_t *w, const char *k, s32 v)
{
	jwComma(w);
	jwIndent(w);
	fprintf(w->f, "\"%s\": %d", k, (int)v);
}

static void jwKvUint(forge_jw_t *w, const char *k, u32 v)
{
	jwComma(w);
	jwIndent(w);
	fprintf(w->f, "\"%s\": %u", k, (unsigned)v);
}

static void jwKvFloat(forge_jw_t *w, const char *k, f32 v)
{
	jwComma(w);
	jwIndent(w);
	fprintf(w->f, "\"%s\": %g", k, (double)v);
}

static void jwKvBool(forge_jw_t *w, const char *k, s32 v)
{
	jwComma(w);
	jwIndent(w);
	fprintf(w->f, "\"%s\": %s", k, v ? "true" : "false");
}

static void jwKvVec3(forge_jw_t *w, const char *k, const f32 v[3])
{
	jwComma(w);
	jwIndent(w);
	fprintf(w->f, "\"%s\": [%g, %g, %g]", k, (double)v[0], (double)v[1], (double)v[2]);
}

/* ============================================================
 * Serializers
 * ============================================================ */

static void forgeSerializeWriteObject(forge_jw_t *w, const forge_object_t *o)
{
	jwObjectBegin(w, NULL);
	jwKvUint(w,  "uid",         o->uid);
	jwKvString(w,"catalog_id",  o->catalog_id);
	jwKvInt(w,   "category",    o->category);
	jwKvString(w,"label",       o->label);
	jwKvVec3(w,  "pos",         o->pos);
	jwKvVec3(w,  "rot",         o->rot);
	jwKvVec3(w,  "scale",       o->scale);
	jwKvInt(w,   "collision",   o->collision_mode);
	jwKvInt(w,   "team",        o->team);
	jwKvBool(w,  "visible",     o->visible);
	jwKvBool(w,  "enabled",     o->enabled);
	jwKvString(w,"material",    o->material_id);
	jwKvVec3(w,  "tint",        o->tint);
	jwKvFloat(w, "emissive",    o->emissive);
	jwKvBool(w,  "cast_shadows",o->cast_shadows);
	jwKvInt(w,   "lod_bias",    o->lod_bias);
	/* S313 variant: 0 = author-placed, 1 = from base stage / base map. */
	jwKvInt(w,   "from_base",   o->from_base);

	/* Type-specific props. We emit as a flat "props" object. */
	jwObjectBegin(w, "props");
	switch (o->category) {
	case FORGE_CAT_WEAPON_PAD:
		jwKvString(w,"weapon_id",   o->props.weapon.weapon_id);
		jwKvInt(w,   "ammo",        o->props.weapon.ammo);
		jwKvBool(w,  "dual_wield",  o->props.weapon.dual_wield);
		jwKvInt(w,   "team_lock",   o->props.weapon.team_lock);
		jwKvInt(w,   "respawn_effect",o->props.weapon.respawn_effect);
		jwKvFloat(w, "respawn_sec", o->props.weapon.respawn_sec);
		break;
	case FORGE_CAT_SPAWN_POINT:
		jwKvInt(w,   "type",        o->props.spawn.type);
		jwKvInt(w,   "team",        o->props.spawn.team);
		jwKvInt(w,   "priority",    o->props.spawn.priority);
		jwKvFloat(w, "facing_deg",  o->props.spawn.facing_deg);
		jwKvFloat(w, "radius",      o->props.spawn.radius);
		break;
	case FORGE_CAT_AI:
		jwKvString(w,"body_id",     o->props.ai.body_id);
		jwKvString(w,"head_id",     o->props.ai.head_id);
		jwKvString(w,"weapon_id",   o->props.ai.weapon_id);
		jwKvInt(w,   "behavior",    o->props.ai.behavior);
		jwKvInt(w,   "faction",     o->props.ai.faction);
		jwKvBool(w,  "respawn",     o->props.ai.respawn);
		jwKvBool(w,  "is_boss",     o->props.ai.is_boss);
		jwKvFloat(w, "health_mult", o->props.ai.health_mult);
		jwKvFloat(w, "alert_radius",o->props.ai.alert_radius);
		jwKvFloat(w, "respawn_delay_sec", o->props.ai.respawn_delay_sec);
		jwKvUint(w,  "patrol_path_uid", o->props.ai.patrol_path_uid);
		if (o->props.ai.is_boss) {
			jwKvString(w,"boss_name", o->props.ai.boss_name);
			jwKvFloat(w, "boss_scale",o->props.ai.boss_scale);
			jwKvInt(w,   "num_phase_thresholds", o->props.ai.num_phase_thresholds);
		}
		break;
	case FORGE_CAT_INTERACTABLE:
		if (strncmp(o->catalog_id, "base:door_", 10) == 0) {
			jwKvInt(w,   "open_dir",      o->props.door.open_dir);
			jwKvBool(w,  "auto_close",    o->props.door.auto_close);
			jwKvBool(w,  "locked",        o->props.door.locked);
			jwKvFloat(w, "open_speed_sec",o->props.door.open_speed_sec);
			jwKvFloat(w, "auto_close_delay_sec",o->props.door.auto_close_delay_sec);
			jwKvString(w,"key_id",        o->props.door.key_id);
		} else if (strncmp(o->catalog_id, "base:elevator_", 14) == 0) {
			jwKvInt(w,   "num_stops", o->props.elev.num_stops);
			jwKvBool(w,  "call_button",o->props.elev.call_button);
			jwKvBool(w,  "loop",      o->props.elev.loop);
			jwKvFloat(w, "speed",     o->props.elev.speed_units_per_sec);
			jwKvFloat(w, "wait_time", o->props.elev.wait_time_sec);
			jwArrayBegin(w, "stops_y");
			for (s32 i = 0; i < o->props.elev.num_stops && i < 8; ++i) {
				jwComma(w);
				jwIndent(w);
				fprintf(w->f, "%g", (double)o->props.elev.stops_y[i]);
			}
			jwArrayEnd(w);
		} else if (strncmp(o->catalog_id, "base:switch_", 12) == 0) {
			jwKvInt(w,   "type",        o->props.sw.type);
			jwKvInt(w,   "activation",  o->props.sw.activation);
			jwKvInt(w,   "team_lock",   o->props.sw.team_lock);
			jwKvFloat(w, "cooldown_sec",o->props.sw.cooldown_sec);
			jwKvUint(w,  "target_uid",  o->props.sw.target_uid);
			jwKvString(w,"channel_out", o->props.sw.channel_out);
		}
		break;
	case FORGE_CAT_LIGHT:
		jwKvInt(w,   "type",       o->props.light.type);
		jwKvBool(w,  "cast_shadows",o->props.light.cast_shadows);
		jwKvBool(w,  "night_only", o->props.light.night_only);
		jwKvVec3(w,  "color",      o->props.light.color);
		jwKvFloat(w, "intensity",  o->props.light.intensity);
		jwKvFloat(w, "range",      o->props.light.range);
		jwKvFloat(w, "inner_cone_deg",o->props.light.inner_cone_deg);
		jwKvFloat(w, "outer_cone_deg",o->props.light.outer_cone_deg);
		jwKvFloat(w, "falloff",    o->props.light.falloff);
		jwKvFloat(w, "width",      o->props.light.width);
		jwKvFloat(w, "height",     o->props.light.height);
		jwKvString(w,"cookie_texture",o->props.light.cookie_texture);
		break;
	case FORGE_CAT_ZONE:
		jwKvInt(w,   "type",       o->props.zone.type);
		jwKvInt(w,   "shape",      o->props.zone.shape);
		jwKvInt(w,   "team_filter",o->props.zone.team_filter);
		jwKvBool(w,  "repeating",  o->props.zone.once_or_repeat);
		jwKvVec3(w,  "size",       o->props.zone.size);
		jwKvFloat(w, "trigger_delay_sec", o->props.zone.trigger_delay_sec);
		jwKvFloat(w, "damage_per_sec", o->props.zone.damage_per_sec);
		jwKvFloat(w, "gravity_mult",   o->props.zone.gravity_mult);
		jwKvVec3(w,  "gravity_dir",    o->props.zone.gravity_dir);
		jwKvVec3(w,  "current_dir",    o->props.zone.current_dir);
		jwKvFloat(w, "current_speed",  o->props.zone.current_speed);
		jwKvVec3(w,  "fog_color",      o->props.zone.fog_color);
		jwKvFloat(w, "fog_density",    o->props.zone.fog_density);
		jwKvUint(w,  "teleport_target_uid", o->props.zone.teleport_target_uid);
		jwKvString(w,"sound_loop_id",  o->props.zone.sound_loop_id);
		jwKvString(w,"channel_on_enter",o->props.zone.channel_on_enter);
		jwKvString(w,"channel_on_exit", o->props.zone.channel_on_exit);
		jwKvString(w,"death_message",  o->props.zone.death_message);
		break;
	case FORGE_CAT_EFFECT:
		jwKvInt(w,   "kind",       o->props.effect.kind);
		jwKvBool(w,  "looping",    o->props.effect.looping);
		jwKvFloat(w, "intensity",  o->props.effect.intensity);
		jwKvFloat(w, "range",      o->props.effect.range);
		jwKvString(w,"asset_id",   o->props.effect.asset_id);
		jwKvVec3(w,  "color",      o->props.effect.color);
		break;
	case FORGE_CAT_PICKUP:
		jwKvString(w,"item_id",    o->props.pickup.item_id);
		jwKvInt(w,   "quantity",   o->props.pickup.quantity);
		jwKvFloat(w, "respawn_sec",o->props.pickup.respawn_sec);
		jwKvInt(w,   "team_lock",  o->props.pickup.team_lock);
		break;
	default:
		break;
	}
	jwObjectEnd(w); /* props */
	jwObjectEnd(w); /* object */
}

static void forgeSerializeWriteLogicNode(forge_jw_t *w, const forge_logic_node_t *n)
{
	jwObjectBegin(w, NULL);
	jwKvUint(w,   "uid",          n->uid);
	jwKvInt(w,    "kind",         n->kind);
	jwKvInt(w,    "op",           n->op);
	jwKvString(w, "label",        n->label);
	jwKvFloat(w,  "canvas_x",     n->canvas_x);
	jwKvFloat(w,  "canvas_y",     n->canvas_y);
	jwKvUint(w,   "target_uid_a", n->target_uid_a);
	jwKvUint(w,   "target_uid_b", n->target_uid_b);
	jwKvInt(w,    "param_int_a",  n->param_int_a);
	jwKvInt(w,    "param_int_b",  n->param_int_b);
	jwKvFloat(w,  "param_float_a",n->param_float_a);
	jwKvFloat(w,  "param_float_b",n->param_float_b);
	jwKvString(w, "param_text_a", n->param_text_a);
	jwKvString(w, "param_text_b", n->param_text_b);
	jwObjectEnd(w);
}

s32 forgeSerializeSaveToMod(const char *mod_slug)
{
	if (!mod_slug || !*mod_slug) mod_slug = "untitled-grid-map";

	char dir[FS_MAXPATH];
	forgeSerializeBuildModDir(mod_slug, dir, sizeof(dir));
	forgeSerializeMakeDirsRecursive(dir);

	/* ---- Collect dependencies once (R3) ---- */
	char deps[FORGE_MAX_DEPENDENCIES][FORGE_ID_LEN];
	s32 num_deps = forgeCollectDependencies(deps, FORGE_MAX_DEPENDENCIES);
	if (num_deps > 0) {
		sysLogPrintf(LOG_NOTE, "GRID.SERIALIZE: collected %d mod dependencies", num_deps);
	}

	/* ---- mod.json ---- */
	{
		char modjson_path[FS_MAXPATH];
		snprintf(modjson_path, sizeof(modjson_path), "%s/mod.json", dir);
		FILE *f = fsFileOpenWrite(modjson_path);
		if (!f) {
			sysLogPrintf(LOG_WARNING, "GRID.SERIALIZE: cannot open %s", modjson_path);
			return 0;
		}
		forge_map_settings_t *s = forgeMapSettings();
		forge_jw_t w = { f, 0, 1 };
		fputs("{", f);
		++w.indent; w.first_child = 1;
		jwKvString(&w, "id",          mod_slug);
		jwKvString(&w, "name",        s->map_name);
		jwKvString(&w, "author",      s->author);
		jwKvString(&w, "description", s->description);
		jwKvString(&w, "type",        "forge-map");
		jwKvInt(&w,    "format_version", 1);

		/* R3 -- dependency array at the mod-manifest level, consumed by
		 * the mod distribution pipeline.  Each entry is the catalog ID
		 * of a non-base asset this map requires.  Distribution is
		 * recursive: the resolver walks each dependency mod's own
		 * manifest for its dependencies, etc. */
		jwArrayBegin(&w, "dependencies");
		for (s32 i = 0; i < num_deps; ++i) {
			jwComma(&w);
			jwIndent(&w);
			jwStringEsc(w.f, deps[i]);
		}
		jwArrayEnd(&w);

		--w.indent;
		fputs("\n}\n", f);
		fclose(f);
	}

	/* ---- map.json ---- */
	char mapjson_path[FS_MAXPATH];
	snprintf(mapjson_path, sizeof(mapjson_path), "%s/map.json", dir);
	FILE *f = fsFileOpenWrite(mapjson_path);
	if (!f) {
		sysLogPrintf(LOG_WARNING, "GRID.SERIALIZE: cannot open %s", mapjson_path);
		return 0;
	}

	forge_map_settings_t *s = forgeMapSettings();
	forge_gametype_t *gt = forgeGameType();
	forge_skylight_t *sky = forgeSkylight();
	forge_atmosphere_t *atm = forgeAtmosphere();

	forge_jw_t w = { f, 0, 1 };
	fputs("{", f);
	++w.indent; w.first_child = 1;

	jwKvInt(&w, "format_version", 1);
	jwKvString(&w, "name",   s->map_name);
	jwKvString(&w, "author", s->author);
	jwKvString(&w, "description", s->description);
	jwKvString(&w, "base_stage", s->base_stage_id);
	jwKvBool(&w,   "is_mission", s->is_mission);

	/* R3 -- dependencies mirrored at the payload level so a map.json
	 * pulled on its own (e.g. for preview) still reports its deps. */
	jwArrayBegin(&w, "dependencies");
	for (s32 i = 0; i < num_deps; ++i) {
		jwComma(&w);
		jwIndent(&w);
		jwStringEsc(w.f, deps[i]);
	}
	jwArrayEnd(&w);

	/* settings */
	jwObjectBegin(&w, "settings");
	jwKvUint(&w,  "gamemode_flags", s->gamemode_flags);
	jwKvInt(&w,   "max_players",    s->max_players);
	jwKvInt(&w,   "recommended_players", s->recommended_players);
	jwKvInt(&w,   "map_size",       s->map_size_tag);
	jwKvInt(&w,   "team_spawn_mode",s->team_spawn_mode);
	jwKvBool(&w,  "use_initial_only",s->use_initial_only);
	jwKvInt(&w,   "min_spawn_points",s->min_spawn_points);
	jwKvFloat(&w, "respawn_delay_sec",s->respawn_delay_sec);
	jwKvFloat(&w, "spawn_protection_sec",s->spawn_protection_sec);
	jwKvInt(&w,   "default_time_limit_sec",s->default_time_limit_sec);
	jwKvInt(&w,   "default_score_limit", s->default_score_limit);
	jwKvInt(&w,   "health_setting", s->health_setting);
	jwKvInt(&w,   "radar_setting",  s->radar_setting);
	jwKvBool(&w,  "auto_aim",       s->auto_aim);
	jwKvBool(&w,  "friendly_fire",  s->friendly_fire);
	jwKvBool(&w,  "one_hit_kills",  s->one_hit_kills);
	jwKvVec3(&w,  "bounds_min",     s->bounds_min);
	jwKvVec3(&w,  "bounds_max",     s->bounds_max);
	jwKvFloat(&w, "soft_bounds_timer_sec", s->soft_bounds_timer_sec);
	jwKvFloat(&w, "grid_size",      s->grid_size);
	jwKvFloat(&w, "rotation_snap_deg",s->rotation_snap_deg);
	jwKvBool(&w,  "surface_snap",   s->surface_snap);
	jwKvBool(&w,  "edge_snap",      s->edge_snap);
	/* R2/R4 -- weapon source policy. */
	jwKvInt(&w,   "weapon_source",  s->weapon_source);
	jwKvBool(&w,  "allow_match_override", s->allow_match_override);
	/* S313 -- variant metadata. */
	jwKvInt(&w,    "variant_mode",         s->variant_mode);
	jwKvString(&w, "variant_source_slug",  s->variant_source_slug);
	jwObjectEnd(&w);

	/* S313 -- bot testing settings block (serialised so map-author's
	 * preferred bot count / spawn mode / body choice persists between
	 * editor sessions on the same map). */
	{
		const forge_bot_settings_t *bs = forgeBotSettings();
		jwObjectBegin(&w, "bot_testing");
		jwKvInt(&w,    "spawn_mode",        bs->spawn_mode);
		jwKvInt(&w,    "active_count",      bs->active_count);
		jwKvInt(&w,    "frozen_count",      bs->frozen_count);
		jwKvBool(&w,   "all_frozen",        bs->all_frozen);
		jwKvFloat(&w,  "near_me_radius",    bs->near_me_radius);
		jwKvFloat(&w,  "smart_aggression",  bs->smart_aggression);
		jwKvString(&w, "default_body_id",   bs->default_body_id);
		jwKvString(&w, "default_difficulty",bs->default_difficulty);
		jwObjectEnd(&w);
	}

	/* mission */
	if (s->is_mission) {
		jwObjectBegin(&w, "mission");
		jwKvString(&w, "briefing", s->briefing_text);
		jwKvString(&w, "debrief",  s->debrief_text);
		jwKvBool(&w,   "sequential_objectives", s->sequential_objectives);
		jwArrayBegin(&w, "objectives");
		for (s32 i = 0; i < FORGE_MAX_OBJECTIVES; ++i) {
			forge_objective_t *o = forgeObjectiveGet(i);
			if (!o || !o->in_use) continue;
			jwObjectBegin(&w, NULL);
			jwKvInt(&w,    "kind",   o->kind);
			jwKvInt(&w,    "order",  o->order);
			jwKvString(&w, "desc",   o->description);
			jwKvUint(&w,   "complete_node", o->completion_node_uid);
			jwKvUint(&w,   "fail_node",     o->failure_node_uid);
			jwObjectEnd(&w);
		}
		jwArrayEnd(&w);
		jwObjectEnd(&w);
	}

	/* skylight */
	jwObjectBegin(&w, "skylight");
	jwKvFloat(&w, "yaw_deg",       sky->direction_yaw_deg);
	jwKvFloat(&w, "pitch_deg",     sky->direction_pitch_deg);
	jwKvVec3(&w,  "color",         sky->color);
	jwKvFloat(&w, "intensity",     sky->intensity);
	jwKvBool(&w,  "cast_shadow",   sky->cast_shadow);
	jwKvInt(&w,   "shadow_softness", sky->shadow_softness);
	jwObjectEnd(&w);

	/* atmosphere */
	jwObjectBegin(&w, "atmosphere");
	jwKvBool(&w,  "fog_enable",   atm->fog_enable);
	jwKvBool(&w,  "bloom_enable", atm->bloom_enable);
	jwKvInt(&w,   "color_grade",  atm->color_grade);
	jwKvVec3(&w,  "fog_color",    atm->fog_color);
	jwKvFloat(&w, "fog_near",     atm->fog_near);
	jwKvFloat(&w, "fog_far",      atm->fog_far);
	jwKvFloat(&w, "fog_height",   atm->fog_height);
	jwKvVec3(&w,  "ambient_color",atm->ambient_color);
	jwKvFloat(&w, "ambient_intensity", atm->ambient_intensity);
	jwKvFloat(&w, "exposure",     atm->exposure);
	jwKvFloat(&w, "bloom_strength",atm->bloom_strength);
	jwKvString(&w,"sky_id",       atm->sky_id);
	jwKvInt(&w,   "weather_kind",     atm->weather_kind);
	jwKvInt(&w,   "weather_intensity",atm->weather_intensity);
	jwKvBool(&w,  "tod_enable",   atm->tod_enable);
	jwKvFloat(&w, "tod_cycle_sec",atm->tod_cycle_sec);
	jwObjectEnd(&w);

	/* gametype */
	jwObjectBegin(&w, "gametype");
	jwKvString(&w, "name",         gt->name);
	jwKvString(&w, "description",  gt->description);
	jwKvInt(&w,    "structure",    gt->structure);
	jwKvInt(&w,    "win_condition",gt->win_condition);
	jwKvInt(&w,    "num_rounds",   gt->num_rounds);
	jwKvInt(&w,    "role_mode",    gt->role_mode);
	jwKvInt(&w,    "score_limit",  gt->score_limit);
	jwKvInt(&w,    "time_limit_sec",gt->time_limit_sec);
	jwKvUint(&w,   "modifier_flags",gt->modifier_flags);
	jwKvInt(&w,    "score_per_kill",gt->score_per_kill);
	jwKvInt(&w,    "score_per_headshot",gt->score_per_headshot);
	jwKvInt(&w,    "score_per_objective",gt->score_per_objective);
	jwKvInt(&w,    "score_per_survive_sec",gt->score_per_survive_sec);
	jwKvString(&w, "starting_weapon",gt->starting_weapon);
	jwKvFloat(&w,  "health_mult",  gt->health_mult);
	jwKvBool(&w,   "show_wave_counter",gt->show_wave_counter);
	jwKvBool(&w,   "show_boss_bar",gt->show_boss_bar);
	jwKvBool(&w,   "show_role_indicator",gt->show_role_indicator);
	jwKvBool(&w,   "show_survival_timer",gt->show_survival_timer);
	jwArrayBegin(&w, "waves");
	for (s32 i = 0; i < gt->num_waves && i < FORGE_MAX_WAVES; ++i) {
		const forge_wave_t *wv = &gt->waves[i];
		if (!wv->in_use) continue;
		jwObjectBegin(&w, NULL);
		jwKvInt(&w,    "index",       i);
		jwKvInt(&w,    "count",       wv->enemy_count);
		jwKvFloat(&w,  "scale",       wv->enemy_scale);
		jwKvFloat(&w,  "health_mult", wv->enemy_health_mult);
		jwKvFloat(&w,  "speed_mult",  wv->enemy_speed_mult);
		jwKvFloat(&w,  "spawn_delay_sec",wv->spawn_delay_sec);
		jwKvFloat(&w,  "intermission_sec",wv->intermission_sec);
		jwKvString(&w, "enemy_catalog_id",wv->enemy_catalog_id);
		jwKvUint(&w,   "spawn_zone_uid",wv->spawn_zone_uid);
		jwKvBool(&w,   "is_boss",     wv->is_boss);
		jwKvBool(&w,   "escalate",    wv->escalate);
		jwObjectEnd(&w);
	}
	jwArrayEnd(&w);
	jwObjectEnd(&w);

	/* objects */
	jwArrayBegin(&w, "objects");
	for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
		forge_object_t *o = forgeObjectGet(i);
		if (!o || !o->in_use) continue;
		forgeSerializeWriteObject(&w, o);
	}
	jwArrayEnd(&w);

	/* logic */
	jwObjectBegin(&w, "logic");
	jwArrayBegin(&w, "channels");
	for (s32 i = 0; i < FORGE_MAX_CHANNELS; ++i) {
		forge_channel_t *c = forgeChannelGet(i);
		if (!c || !c->in_use) continue;
		jwObjectBegin(&w, NULL);
		jwKvString(&w, "name",  c->name);
		jwKvBool(&w,   "state", c->state);
		jwObjectEnd(&w);
	}
	jwArrayEnd(&w);
	jwArrayBegin(&w, "nodes");
	for (s32 i = 0; i < FORGE_MAX_LOGIC_NODES; ++i) {
		forge_logic_node_t *n = forgeLogicNodeGet(i);
		if (!n || !n->in_use) continue;
		forgeSerializeWriteLogicNode(&w, n);
	}
	jwArrayEnd(&w);
	jwArrayBegin(&w, "wires");
	for (s32 i = 0; i < FORGE_MAX_LOGIC_WIRES; ++i) {
		forge_logic_wire_t *wi = forgeLogicWireGet(i);
		if (!wi || !wi->in_use) continue;
		jwObjectBegin(&w, NULL);
		jwKvUint(&w, "src",      wi->src_node_uid);
		jwKvUint(&w, "dst",      wi->dst_node_uid);
		jwKvInt(&w,  "src_port", wi->src_port);
		jwKvInt(&w,  "dst_port", wi->dst_port);
		jwObjectEnd(&w);
	}
	jwArrayEnd(&w);
	jwObjectEnd(&w);

	--w.indent;
	fputs("\n}\n", f);
	fclose(f);

	sysLogPrintf(LOG_NOTE, "GRID.SERIALIZE: saved '%s' -> %s (%d objects, %d logic nodes)",
			mod_slug, mapjson_path,
			forgeObjectCount(), forgeLogicNodeCount());
	return 1;
}

/* ============================================================
 * Loader -- tolerant tokeniser
 * ============================================================ */

typedef struct forge_jr {
	const char *p;
	const char *end;
} forge_jr_t;

static void jrSkipWs(forge_jr_t *r)
{
	while (r->p < r->end) {
		unsigned char c = (unsigned char)*r->p;
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') { ++r->p; continue; }
		if (c == '/' && r->p + 1 < r->end && r->p[1] == '/') {
			while (r->p < r->end && *r->p != '\n') ++r->p;
			continue;
		}
		break;
	}
}

static s32 jrMatch(forge_jr_t *r, char ch)
{
	jrSkipWs(r);
	if (r->p < r->end && *r->p == ch) { ++r->p; return 1; }
	return 0;
}

static s32 jrReadString(forge_jr_t *r, char *out, size_t n)
{
	jrSkipWs(r);
	if (r->p >= r->end || *r->p != '"') return 0;
	++r->p;
	size_t i = 0;
	while (r->p < r->end && *r->p != '"') {
		char c = *r->p++;
		if (c == '\\' && r->p < r->end) {
			char e = *r->p++;
			switch (e) {
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case '"': c = '"'; break;
			case '\\': c = '\\'; break;
			case 'u': {
				/* skip 4 hex digits */
				for (s32 k = 0; k < 4 && r->p < r->end; ++k) ++r->p;
				c = '?';
				break;
			}
			default: c = e; break;
			}
		}
		if (i + 1 < n) out[i++] = c;
	}
	if (i < n) out[i] = '\0';
	if (r->p < r->end) ++r->p; /* closing quote */
	return 1;
}

static s32 jrReadNumber(forge_jr_t *r, f64 *out)
{
	jrSkipWs(r);
	char buf[64];
	s32 i = 0;
	while (r->p < r->end && i + 1 < (s32)sizeof(buf)) {
		char c = *r->p;
		if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9') ||
				c == 'e' || c == 'E') {
			buf[i++] = c;
			++r->p;
		} else break;
	}
	buf[i] = '\0';
	if (i == 0) return 0;
	*out = strtod(buf, NULL);
	return 1;
}

static s32 jrReadBool(forge_jr_t *r, s32 *out)
{
	jrSkipWs(r);
	if (r->p + 4 <= r->end && strncmp(r->p, "true", 4) == 0) { r->p += 4; *out = 1; return 1; }
	if (r->p + 5 <= r->end && strncmp(r->p, "false", 5) == 0) { r->p += 5; *out = 0; return 1; }
	/* tolerant: treat integer nonzero as true */
	f64 v;
	if (jrReadNumber(r, &v)) { *out = (v != 0.0) ? 1 : 0; return 1; }
	return 0;
}

/* Skip an arbitrary value (object/array/string/number/bool/null). */
static void jrSkipValue(forge_jr_t *r)
{
	jrSkipWs(r);
	if (r->p >= r->end) return;
	char c = *r->p;
	if (c == '{' || c == '[') {
		char open = c, close = (c == '{') ? '}' : ']';
		s32 depth = 1;
		++r->p;
		while (r->p < r->end && depth > 0) {
			char k = *r->p++;
			if (k == '"') {
				while (r->p < r->end && *r->p != '"') {
					if (*r->p == '\\' && r->p + 1 < r->end) r->p += 2;
					else ++r->p;
				}
				if (r->p < r->end) ++r->p;
			} else if (k == open) ++depth;
			else if (k == close) --depth;
		}
	} else if (c == '"') {
		char tmp[16];
		jrReadString(r, tmp, sizeof(tmp));
	} else {
		f64 v;
		s32 b;
		if (!jrReadNumber(r, &v) && !jrReadBool(r, &b)) {
			if (r->p + 4 <= r->end && strncmp(r->p, "null", 4) == 0) r->p += 4;
		}
	}
}

static s32 jrReadKey(forge_jr_t *r, char *out, size_t n)
{
	if (!jrReadString(r, out, n)) return 0;
	jrSkipWs(r);
	if (r->p < r->end && *r->p == ':') ++r->p;
	return 1;
}

/* Extract an f32 (wraps jrReadNumber). */
static s32 jrReadFloat(forge_jr_t *r, f32 *out)
{
	f64 v;
	if (!jrReadNumber(r, &v)) return 0;
	*out = (f32)v;
	return 1;
}

static s32 jrReadInt(forge_jr_t *r, s32 *out)
{
	f64 v;
	if (!jrReadNumber(r, &v)) return 0;
	*out = (s32)v;
	return 1;
}

static s32 jrReadUint(forge_jr_t *r, u32 *out)
{
	f64 v;
	if (!jrReadNumber(r, &v)) return 0;
	*out = (u32)v;
	return 1;
}

static void jrReadVec3(forge_jr_t *r, f32 out[3])
{
	jrSkipWs(r);
	if (!jrMatch(r, '[')) return;
	for (s32 i = 0; i < 3; ++i) {
		jrReadFloat(r, &out[i]);
	}
	jrMatch(r, ']');
}

/* ============================================================
 * Loader drivers
 * ============================================================ */

static void forgeSerializeReadSettings(forge_jr_t *r)
{
	forge_map_settings_t *s = forgeMapSettings();
	if (!jrMatch(r, '{')) return;
	for (;;) {
		jrSkipWs(r);
		if (jrMatch(r, '}')) return;
		char key[64];
		if (!jrReadKey(r, key, sizeof(key))) return;
		if      (!strcmp(key, "gamemode_flags"))       jrReadUint(r, &s->gamemode_flags);
		else if (!strcmp(key, "max_players"))          { s32 v; if (jrReadInt(r,&v)) s->max_players = (u8)v; }
		else if (!strcmp(key, "recommended_players"))  { s32 v; if (jrReadInt(r,&v)) s->recommended_players = (u8)v; }
		else if (!strcmp(key, "map_size"))             { s32 v; if (jrReadInt(r,&v)) s->map_size_tag = (u8)v; }
		else if (!strcmp(key, "team_spawn_mode"))      { s32 v; if (jrReadInt(r,&v)) s->team_spawn_mode = (u8)v; }
		else if (!strcmp(key, "use_initial_only"))     { s32 v; if (jrReadBool(r,&v)) s->use_initial_only = (u8)v; }
		else if (!strcmp(key, "min_spawn_points"))     jrReadInt(r, &s->min_spawn_points);
		else if (!strcmp(key, "respawn_delay_sec"))    jrReadFloat(r, &s->respawn_delay_sec);
		else if (!strcmp(key, "spawn_protection_sec"))  jrReadFloat(r, &s->spawn_protection_sec);
		else if (!strcmp(key, "default_time_limit_sec")) jrReadInt(r, &s->default_time_limit_sec);
		else if (!strcmp(key, "default_score_limit"))  jrReadInt(r, &s->default_score_limit);
		else if (!strcmp(key, "bounds_min"))           jrReadVec3(r, s->bounds_min);
		else if (!strcmp(key, "bounds_max"))           jrReadVec3(r, s->bounds_max);
		else if (!strcmp(key, "grid_size"))            jrReadFloat(r, &s->grid_size);
		else if (!strcmp(key, "rotation_snap_deg"))    jrReadFloat(r, &s->rotation_snap_deg);
		else if (!strcmp(key, "surface_snap"))         { s32 v; if (jrReadBool(r,&v)) s->surface_snap = (u8)v; }
		else if (!strcmp(key, "edge_snap"))            { s32 v; if (jrReadBool(r,&v)) s->edge_snap = (u8)v; }
		else if (!strcmp(key, "weapon_source"))        { s32 v; if (jrReadInt(r,&v)) s->weapon_source = (u8)v; }
		else if (!strcmp(key, "allow_match_override")) { s32 v; if (jrReadBool(r,&v)) s->allow_match_override = (u8)v; }
		else if (!strcmp(key, "variant_mode"))         { s32 v; if (jrReadInt(r,&v)) s->variant_mode = (u8)v; }
		else if (!strcmp(key, "variant_source_slug")) jrReadString(r, s->variant_source_slug, sizeof(s->variant_source_slug));
		else jrSkipValue(r);
	}
}

static void forgeSerializeReadBotTesting(forge_jr_t *r)
{
	forge_bot_settings_t *bs = forgeBotSettings();
	if (!jrMatch(r, '{')) return;
	for (;;) {
		jrSkipWs(r);
		if (jrMatch(r, '}')) return;
		char key[64];
		if (!jrReadKey(r, key, sizeof(key))) return;
		if      (!strcmp(key, "spawn_mode"))         { s32 v; if (jrReadInt(r,&v)) bs->spawn_mode = (u8)v; }
		else if (!strcmp(key, "active_count"))       { s32 v; if (jrReadInt(r,&v)) bs->active_count = (u8)v; }
		else if (!strcmp(key, "frozen_count"))       { s32 v; if (jrReadInt(r,&v)) bs->frozen_count = (u8)v; }
		else if (!strcmp(key, "all_frozen"))         { s32 v; if (jrReadBool(r,&v)) bs->all_frozen = (u8)v; }
		else if (!strcmp(key, "near_me_radius"))     jrReadFloat(r, &bs->near_me_radius);
		else if (!strcmp(key, "smart_aggression"))   jrReadFloat(r, &bs->smart_aggression);
		else if (!strcmp(key, "default_body_id"))    jrReadString(r, bs->default_body_id, sizeof(bs->default_body_id));
		else if (!strcmp(key, "default_difficulty")) jrReadString(r, bs->default_difficulty, sizeof(bs->default_difficulty));
		else jrSkipValue(r);
	}
}

static void forgeSerializeReadObject(forge_jr_t *r)
{
	forge_object_t stage;
	memset(&stage, 0, sizeof(stage));
	stage.visible = 1;
	stage.enabled = 1;
	stage.scale[0] = stage.scale[1] = stage.scale[2] = 1.0f;
	stage.tint[0] = stage.tint[1] = stage.tint[2] = 1.0f;
	stage.collision_mode = FORGE_COLLISION_SOLID;

	char catalog_id[FORGE_ID_LEN] = "";

	if (!jrMatch(r, '{')) return;
	for (;;) {
		jrSkipWs(r);
		if (jrMatch(r, '}')) break;
		char key[64];
		if (!jrReadKey(r, key, sizeof(key))) return;
		if      (!strcmp(key, "uid"))         jrReadUint(r, &stage.uid);
		else if (!strcmp(key, "catalog_id"))  jrReadString(r, catalog_id, sizeof(catalog_id));
		else if (!strcmp(key, "category"))    { s32 v; if (jrReadInt(r,&v)) stage.category = (u8)v; }
		else if (!strcmp(key, "label"))       jrReadString(r, stage.label, sizeof(stage.label));
		else if (!strcmp(key, "pos"))         jrReadVec3(r, stage.pos);
		else if (!strcmp(key, "rot"))         jrReadVec3(r, stage.rot);
		else if (!strcmp(key, "scale"))       jrReadVec3(r, stage.scale);
		else if (!strcmp(key, "collision"))   { s32 v; if (jrReadInt(r,&v)) stage.collision_mode = (u8)v; }
		else if (!strcmp(key, "team"))        { s32 v; if (jrReadInt(r,&v)) stage.team = (u8)v; }
		else if (!strcmp(key, "visible"))     { s32 v; if (jrReadBool(r,&v)) stage.visible = (u8)v; }
		else if (!strcmp(key, "enabled"))     { s32 v; if (jrReadBool(r,&v)) stage.enabled = (u8)v; }
		else if (!strcmp(key, "material"))    jrReadString(r, stage.material_id, sizeof(stage.material_id));
		else if (!strcmp(key, "tint"))        jrReadVec3(r, stage.tint);
		else if (!strcmp(key, "emissive"))    jrReadFloat(r, &stage.emissive);
		else if (!strcmp(key, "cast_shadows")){ s32 v; if (jrReadBool(r,&v)) stage.cast_shadows = (u8)v; }
		else if (!strcmp(key, "lod_bias"))    { s32 v; if (jrReadInt(r,&v)) stage.lod_bias = (u8)v; }
		else if (!strcmp(key, "from_base"))   { s32 v; if (jrReadInt(r,&v)) stage.from_base = (u8)v; }
		else if (!strcmp(key, "props")) {
			/* Props are category-specific; we defer parse by placing them into
			 * the staged object after it's allocated with defaults, then reading
			 * sub-keys. For simplicity, skip the object and leave defaults --
			 * a future pass can parse these per-category. */
			jrSkipValue(r);
		}
		else jrSkipValue(r);
	}

	forge_object_t *dst = forgeObjectAllocate((forge_category_t)stage.category, catalog_id);
	if (dst) {
		u32 keep_uid = dst->uid;
		forgeCopyStr(dst->catalog_id, catalog_id, FORGE_ID_LEN);
		dst->pos[0] = stage.pos[0]; dst->pos[1] = stage.pos[1]; dst->pos[2] = stage.pos[2];
		dst->rot[0] = stage.rot[0]; dst->rot[1] = stage.rot[1]; dst->rot[2] = stage.rot[2];
		dst->scale[0] = stage.scale[0]; dst->scale[1] = stage.scale[1]; dst->scale[2] = stage.scale[2];
		dst->collision_mode = stage.collision_mode;
		dst->team = stage.team;
		dst->visible = stage.visible;
		dst->enabled = stage.enabled;
		forgeCopyStr(dst->material_id, stage.material_id, FORGE_ID_LEN);
		dst->tint[0] = stage.tint[0]; dst->tint[1] = stage.tint[1]; dst->tint[2] = stage.tint[2];
		dst->emissive = stage.emissive;
		dst->cast_shadows = stage.cast_shadows;
		dst->lod_bias = stage.lod_bias;
		dst->from_base = stage.from_base;
		forgeCopyStr(dst->label, stage.label, FORGE_LABEL_LEN);
		dst->uid = keep_uid; /* keep freshly-generated uid */
	}
}

s32 forgeSerializeLoadFromMod(const char *mod_slug)
{
	if (!mod_slug || !*mod_slug) return 0;
	char dir[FS_MAXPATH];
	forgeSerializeBuildModDir(mod_slug, dir, sizeof(dir));
	char mapjson_path[FS_MAXPATH];
	snprintf(mapjson_path, sizeof(mapjson_path), "%s/map.json", dir);

	u32 size = 0;
	void *data = fsFileLoad(mapjson_path, &size);
	if (!data || size == 0) {
		sysLogPrintf(LOG_WARNING, "GRID.SERIALIZE: failed to load %s", mapjson_path);
		return 0;
	}

	forgeCoreReset();

	forge_jr_t r;
	r.p = (const char *)data;
	r.end = r.p + size;

	if (!jrMatch(&r, '{')) { free(data); return 0; }
	for (;;) {
		jrSkipWs(&r);
		if (jrMatch(&r, '}')) break;
		char key[64];
		if (!jrReadKey(&r, key, sizeof(key))) break;

		if (!strcmp(key, "name"))        jrReadString(&r, forgeMapSettings()->map_name, FORGE_NAME_LEN);
		else if (!strcmp(key, "author")) jrReadString(&r, forgeMapSettings()->author, FORGE_NAME_LEN);
		else if (!strcmp(key, "description")) jrReadString(&r, forgeMapSettings()->description, FORGE_DESC_LEN);
		else if (!strcmp(key, "base_stage"))  jrReadString(&r, forgeMapSettings()->base_stage_id, FORGE_ID_LEN);
		else if (!strcmp(key, "is_mission"))  { s32 v; if (jrReadBool(&r,&v)) forgeMapSettings()->is_mission = (u8)v; }
		else if (!strcmp(key, "settings"))    forgeSerializeReadSettings(&r);
		else if (!strcmp(key, "bot_testing")) forgeSerializeReadBotTesting(&r);
		else if (!strcmp(key, "objects")) {
			if (!jrMatch(&r, '[')) continue;
			for (;;) {
				jrSkipWs(&r);
				if (jrMatch(&r, ']')) break;
				forgeSerializeReadObject(&r);
			}
		}
		else jrSkipValue(&r);
	}

	free(data);
	sysLogPrintf(LOG_NOTE, "GRID.SERIALIZE: loaded '%s' (%d objects)",
			mod_slug, forgeObjectCount());
	return 1;
}
