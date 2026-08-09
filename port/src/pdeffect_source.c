#include "pdeffect_source.h"

#include "types.h"
#include "assetcatalog.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

s32 pdEffectCatalogIdValid(const char *id)
{
	const unsigned char *p = (const unsigned char *)id;
	size_t len;
	#define EFFECT_ASCII_ALPHA(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
	#define EFFECT_ASCII_ALNUM(c) (EFFECT_ASCII_ALPHA(c) || ((c) >= '0' && (c) <= '9'))
	if (!id || !EFFECT_ASCII_ALPHA(*p)) return 0;
	len = strlen(id);
	if (len >= CATALOG_ID_LEN) return 0;
	for (p++; *p && *p != ':'; p++) {
		if (!(EFFECT_ASCII_ALNUM(*p) || *p == '_' || *p == '-' || *p == '.')) return 0;
	}
	if (*p != ':' || !EFFECT_ASCII_ALNUM(p[1])) return 0;
	for (p += 2; *p; p++) {
		if (!(EFFECT_ASCII_ALNUM(*p) || *p == '_' || *p == '-' || *p == '.')) return 0;
	}
	#undef EFFECT_ASCII_ALNUM
	#undef EFFECT_ASCII_ALPHA
	return 1;
}

typedef struct effect_textbuf {
	char *data;
	size_t len;
	size_t cap;
} effect_textbuf_t;

static const char *s_ExplosionProfileIds[] = {
	"none", "bullet_hole", "eyespy", "laptop_sentry", "area_51_table",
	"firing_range_target", "tiny", "compact", "electrical_small",
	"electrical_large", "expanding_small", "medium", "heavy", "rocket",
	"gas_barrel", "impact_flame", "impact_flame_alt", "huge",
	"bond_explode", "sustained_large", "wide_flash", "superdragon_grenade",
	"phoenix", "dragon_bomb_spy", "dense_rocket", "massive"
};

static const char *s_SparkProfileIds[] = {
	"default", "electrical", "blood", "flesh", "flesh_large",
	"high_energy", "low_energy", "short_flash", "short_flash_large",
	"environmental_one", "environmental_two", "environmental_three",
	"environmental_four", "environmental_five", "directional_flash",
	"shallow_water", "projectile", "light_one", "light_two", "light_three",
	"light_four", "fast_streak", "background_hit_orange",
	"background_hit_green", "background_hit_tranquilizer", "paint",
	"deep_water"
};

static const char *s_SmokeProfileIds[] = {
	"none", "electrical", "mini", "pale_large", "small", "medium", "large",
	"bullet_impact", "rocket_tail", "grenade_tail", "long_lived_large",
	"homing_tail", "fine_light", "skedar_corpse", "yellow_corpse",
	"muzzle_pistol", "muzzle_reaper", "muzzle_automatic", "muzzle_shotgun",
	"pinball", "water", "debris", "ufo"
};

static s32 textReserve(effect_textbuf_t *buf, size_t extra)
{
	if (!buf || extra > (size_t)-1 - buf->len - 1) return -1;
	size_t needed = buf->len + extra + 1;
	if (needed <= buf->cap) return 0;
	size_t cap = buf->cap ? buf->cap : 4096;
	while (cap < needed) {
		if (cap > (size_t)-1 / 2) { cap = needed; break; }
		cap *= 2;
	}
	char *grown = (char *)realloc(buf->data, cap);
	if (!grown) return -1;
	buf->data = grown;
	buf->cap = cap;
	return 0;
}

static s32 textAppendf(effect_textbuf_t *buf, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	va_list copy;
	va_copy(copy, ap);
	int count = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);
	if (count < 0 || textReserve(buf, (size_t)count) != 0) {
		va_end(ap);
		return -1;
	}
	vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, ap);
	va_end(ap);
	buf->len += (size_t)count;
	return 0;
}

static s32 textJsonString(effect_textbuf_t *buf, const char *value)
{
	if (textAppendf(buf, "\"") != 0) return -1;
	for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; p++) {
		if (*p == '\\' || *p == '\"') {
			if (textAppendf(buf, "\\%c", *p) != 0) return -1;
		} else if (*p < 0x20) {
			if (textAppendf(buf, "\\u%04x", (unsigned)*p) != 0) return -1;
		} else if (textAppendf(buf, "%c", *p) != 0) {
			return -1;
		}
	}
	return textAppendf(buf, "\"");
}

static s32 beginGraph(effect_textbuf_t *buf, const char *catalog_id,
	const char *profile_kind)
{
	if (textAppendf(buf,
		"{\n  \"schema\": \"pd.effect_graph.v2\",\n  \"catalog_id\": ") != 0 ||
		textJsonString(buf, catalog_id) != 0 ||
		textAppendf(buf,
		",\n  \"program_kind\": \"profile_library\",\n"
		"  \"profile_kind\": \"%s\",\n"
		"  \"base_module_coverage\": {\"native_particle_systems\": [\"explosion\", \"spark\", \"smoke\"], \"absent_independent_tables\": [\"beam\", \"screen\"]},\n"
		"  \"field_provenance\": {\n"
		"    \"stored\": \"values copied field-for-field from the native base table\",\n"
		"    \"source_derived\": \"fixed behavior proven by the native production consumer\",\n"
		"    \"callsite_owned\": [\"target\", \"attachment\", \"priority\", \"effect_enable\", \"owner\", \"scorch_enable\"]\n"
		"  },\n  \"profiles\": [\n", profile_kind) != 0) {
		return -1;
	}
	return 0;
}

static s32 finishGraph(effect_textbuf_t *buf, char **out, size_t *out_len)
{
	if (!out || !out_len || textAppendf(buf, "\n  ]\n}\n") != 0) return -1;
	*out = buf->data;
	*out_len = buf->len;
	buf->data = NULL;
	buf->len = buf->cap = 0;
	return 0;
}

const char *pdEffectSourceExplosionProfileId(size_t index)
{
	return index < sizeof(s_ExplosionProfileIds) / sizeof(s_ExplosionProfileIds[0])
		? s_ExplosionProfileIds[index] : NULL;
}

const char *pdEffectSourceSparkProfileId(size_t index)
{
	return index < sizeof(s_SparkProfileIds) / sizeof(s_SparkProfileIds[0])
		? s_SparkProfileIds[index] : NULL;
}

const char *pdEffectSourceSmokeProfileId(size_t index)
{
	return index < sizeof(s_SmokeProfileIds) / sizeof(s_SmokeProfileIds[0])
		? s_SmokeProfileIds[index] : NULL;
}

s32 pdEffectSourceBuildExplosionGraph(const char *catalog_id,
	const struct explosiontype *rows, size_t row_count,
	pd_effect_sound_id_fn sound_id_fn, char **out, size_t *out_len)
{
	effect_textbuf_t buf = { 0 };
	if (!catalog_id || !rows || !sound_id_fn || !out || !out_len ||
		row_count != sizeof(s_ExplosionProfileIds) / sizeof(s_ExplosionProfileIds[0]) ||
		beginGraph(&buf, catalog_id, "explosion") != 0) goto fail;
	for (size_t i = 0; i < row_count; i++) {
		char sound_id[128] = { 0 };
		const char *smoke_id = pdEffectSourceSmokeProfileId(rows[i].smoketype);
		if (rows[i].sound != 0) {
			sound_id_fn((s32)rows[i].sound, sound_id, sizeof(sound_id));
		}
		if (!smoke_id || (rows[i].sound != 0 && !sound_id[0]) || textAppendf(&buf,
			"%s    {\n      \"id\": \"%s\",\n      \"module\": \"effect.explosion\",\n"
			"      \"stored\": {\"range_h\": %.9g, \"range_v\": %.9g, \"change_rate_h\": %.9g, \"change_rate_v\": %.9g, \"inner_size\": %.9g, \"blast_radius\": %.9g, \"damage_radius\": %.9g, \"duration_ticks\": %d, \"propagation_rate\": %d, \"flare_speed\": %.9g, \"damage\": %.9g},\n"
			"      \"smoke_profile\": \"%s\",\n      \"audio_catalog_id\": ",
			i ? ",\n" : "", s_ExplosionProfileIds[i], rows[i].rangeh, rows[i].rangev,
			rows[i].changerateh, rows[i].changeratev, rows[i].innersize,
			rows[i].blastradius, rows[i].damageradius, (int)rows[i].duration,
			(int)rows[i].propagationrate, rows[i].flarespeed, rows[i].damage,
			smoke_id) != 0 || (sound_id[0]
				? textJsonString(&buf, sound_id)
				: textAppendf(&buf, "null")) != 0 ||
			textAppendf(&buf,
			",\n      \"source_derived\": {"
			"\"renderer\": {\"backend\": \"native_explosion_parts\", \"max_parts\": 40, \"part_size_random_multiplier\": [1.0, 1.5], \"cleanup_flare_speed_multiplier\": 16.0}, "
			"\"light\": {\"mode\": \"room_flash\", \"radius_field\": \"range_h\", \"intensity\": 255}, "
			"\"camera_shake\": {\"duration_ticks\": 12, \"excluded_profiles\": [\"bullet_hole\", \"impact_flame_alt\"], \"intensity_formula\": \"inner_size / distance * 15\"}, "
			"\"smoke_spawn\": {\"gas_barrel_tick\": 15, \"other_profile_tick_formula\": \"duration_ticks - 20\", \"bullet_hole_probability\": 0.5}, "
			"\"scorch\": {\"spawn_tick_formula\": \"duration_ticks / 2\", \"size_formula\": \"min(2 * inner_size, 100) * random(0.8, 1.0)\"}"
			"}\n    }") != 0) {
			goto fail;
		}
	}
	if (finishGraph(&buf, out, out_len) != 0) goto fail;
	return 0;
fail:
	free(buf.data);
	return -1;
}

s32 pdEffectSourceBuildSparkGraph(const char *catalog_id,
	const struct sparktype *rows, size_t row_count, char **out, size_t *out_len)
{
	effect_textbuf_t buf = { 0 };
	if (!catalog_id || !rows || !out || !out_len ||
		row_count != sizeof(s_SparkProfileIds) / sizeof(s_SparkProfileIds[0]) ||
		beginGraph(&buf, catalog_id, "spark") != 0) goto fail;
	for (size_t i = 0; i < row_count; i++) {
		if (textAppendf(&buf,
			"%s    {\n      \"id\": \"%s\",\n      \"module\": \"effect.spark\",\n"
			"      \"stored\": {\"speed_random_range\": %u, \"origin_offset_scale\": %d, \"streak_width_base\": %u, \"streak_length_base\": %u, \"streak_width_growth_per_tick\": %u, \"streak_length_growth_per_tick\": %u, \"gravity_per_tick\": %.9g, \"max_age_ticks\": %u, \"fade_start_tick\": %u, \"spark_count\": %u, \"flags\": %u, \"color_start_rgba\": \"#%08x\", \"color_end_rgba\": \"#%08x\", \"deceleration\": %.9g},\n"
			"      \"source_derived\": {\"renderer\": \"native_spark_streak\", \"motion\": \"native_spark_ballistics\"}\n    }",
			i ? ",\n" : "", s_SparkProfileIds[i], (unsigned)rows[i].unk00,
			(int)rows[i].unk02, (unsigned)rows[i].unk04, (unsigned)rows[i].unk06,
			(unsigned)rows[i].unk08, (unsigned)rows[i].unk0a, rows[i].weight,
			(unsigned)rows[i].maxage, (unsigned)rows[i].unk12,
			(unsigned)rows[i].numsparks, (unsigned)rows[i].unk18,
			(unsigned)rows[i].unk1c, (unsigned)rows[i].unk20, rows[i].decel) != 0) goto fail;
	}
	if (finishGraph(&buf, out, out_len) != 0) goto fail;
	return 0;
fail:
	free(buf.data);
	return -1;
}

s32 pdEffectSourceBuildSmokeGraph(const char *catalog_id,
	const struct smoketype *rows, size_t row_count, char **out, size_t *out_len)
{
	effect_textbuf_t buf = { 0 };
	if (!catalog_id || !rows || !out || !out_len ||
		row_count != sizeof(s_SmokeProfileIds) / sizeof(s_SmokeProfileIds[0]) ||
		beginGraph(&buf, catalog_id, "smoke") != 0) goto fail;
	for (size_t i = 0; i < row_count; i++) {
		if (textAppendf(&buf,
			"%s    {\n      \"id\": \"%s\",\n      \"module\": \"effect.smoke\",\n"
			"      \"stored\": {\"duration_ticks\": %d, \"fade_speed\": %d, \"spread_interval_ticks\": %d, \"initial_size\": %d, \"background_rotation_speed\": %.9g, \"color_rgb\": \"#%02x%02x%02x\", \"foreground_rotation_speed\": %.9g, \"cloud_count\": %d, \"size_growth_per_tick\": %.9g, \"rise_per_tick\": %.9g, \"drift_radius\": %.9g},\n"
			"      \"source_derived\": {\"renderer\": \"native_smoke_billboard\", \"motion\": \"native_smoke_drift\"}\n    }",
			i ? ",\n" : "", s_SmokeProfileIds[i], (int)rows[i].duration,
			(int)rows[i].fadespeed, (int)rows[i].spreadspeed, (int)rows[i].size,
			rows[i].bgrotatespeed, (unsigned)rows[i].r, (unsigned)rows[i].g,
			(unsigned)rows[i].b, rows[i].fgrotatespeed, (int)rows[i].numclouds,
			rows[i].unk18, rows[i].unk1c, rows[i].unk20) != 0) goto fail;
	}
	if (finishGraph(&buf, out, out_len) != 0) goto fail;
	return 0;
fail:
	free(buf.data);
	return -1;
}
