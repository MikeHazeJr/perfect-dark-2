/*
 * assetcatalog_scanner.c -- D3R-4: Component scanner + INI loader
 *
 * Scans mod component directories and registers assets in the catalog.
 * Two-pass approach:
 *   1. Enumerate mod directories under mods/
 *   2. For each mod, scan category subdirectories (maps, characters, textures)
 *   3. For each component folder, parse the .ini manifest
 *   4. Register a catalog entry based on the INI type and fields
 *
 * The INI parser is minimal but sufficient for the component .ini format:
 *   - First section names the asset type: [map], [weapon], [head], etc.
 *   - Later sections may group settings for modders; keys share one flat map.
 *   - Key = value pairs, one per line
 *   - Hash and semicolon comments, blank lines ignored
 *   - No multiline values, no escaping
 *
 * Auto-discovered by CMake glob. No build system changes needed.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include "types.h"
#include "constants.h"
#include "asset_archive_policy.h"
#include "assetcatalog.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_scanner.h"
#include "modarchive.h"
#include "romdata.h"
#include "system.h"
#include "fs.h"

/* ========================================================================
 * INI Parser
 * ======================================================================== */

/**
 * Trim leading and trailing whitespace from a string in-place.
 * Returns pointer to first non-whitespace character.
 */
static char *trimWhitespace(char *str)
{
	/* leading */
	while (*str && isspace((u8)*str)) {
		str++;
	}

	if (*str == '\0') {
		return str;
	}

	/* trailing */
	char *end = str + strlen(str) - 1;
	while (end > str && isspace((u8)*end)) {
		*end = '\0';
		end--;
	}

	return str;
}

s32 iniParseBuffer(const char *label, const char *data, u32 len, ini_section_t *out)
{
	if (!data || !out) {
		return 0;
	}

	memset(out, 0, sizeof(*out));

	char *buf = (char *)malloc((size_t)len + 1);
	if (!buf) {
		sysLogPrintf(LOG_ERROR,
			"assetcatalog_scanner: out of memory parsing INI '%s'",
			label ? label : "(memory)");
		return 0;
	}
	memcpy(buf, data, len);
	buf[len] = '\0';

	s32 in_section = 0;
	char *line = buf;

	while (line && *line) {
		char *next = strpbrk(line, "\r\n");
		if (next) {
			char eol = *next;
			*next++ = '\0';
			if (eol == '\r' && *next == '\n') {
				next++;
			}
		}

		char *p = trimWhitespace(line);

		/* skip blank lines and comments */
		if (*p == '\0' || *p == '#' || *p == ';') {
			line = next;
			continue;
		}

		/* section header: [type] */
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) {
				*end = '\0';
				if (out->type[0] == '\0') {
					strncpy(out->type, p + 1, sizeof(out->type) - 1);
				}
				in_section = 1;
			}
			line = next;
			continue;
		}

		/* key = value */
		if (in_section) {
			char *eq = strchr(p, '=');
			if (!eq) {
				continue;
			}

			*eq = '\0';
			char *key = trimWhitespace(p);
			char *val = trimWhitespace(eq + 1);

			if (out->count < INI_MAX_PAIRS) {
				strncpy(out->pairs[out->count].key, key,
					sizeof(out->pairs[0].key) - 1);
				strncpy(out->pairs[out->count].value, val,
					sizeof(out->pairs[0].value) - 1);
				out->count++;
			}
		}

		line = next;
	}

	free(buf);
	return (out->type[0] != '\0') ? 1 : 0;
}

s32 iniParse(const char *filepath, ini_section_t *out)
{
	FILE *fp = fopen(filepath, "rb");
	if (!fp) {
		return 0;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return 0;
	}

	long size = ftell(fp);
	if (size < 0) {
		fclose(fp);
		return 0;
	}
	rewind(fp);

	char *buf = (char *)malloc((size_t)size + 1);
	if (!buf) {
		fclose(fp);
		return 0;
	}

	size_t got = fread(buf, 1, (size_t)size, fp);
	fclose(fp);

	s32 ok = 0;
	if (got == (size_t)size) {
		ok = iniParseBuffer(filepath, buf, (u32)size, out);
	}

	free(buf);
	return ok;
}

static s32 iniWriteAppend(char *out, u32 out_cap, u32 *used, const char *text)
{
	if (!out || !used || !text) {
		return 0;
	}

	u32 len = (u32)strlen(text);
	if (*used + len >= out_cap) {
		return 0;
	}

	memcpy(out + *used, text, len);
	*used += len;
	out[*used] = '\0';
	return 1;
}

s32 iniWriteBuffer(const ini_section_t *ini, char *out, u32 out_cap, u32 *out_len)
{
	if (!ini || !ini->type[0] || !out || out_cap == 0) {
		return 0;
	}

	u32 used = 0;
	out[0] = '\0';

	char line[384];
	snprintf(line, sizeof(line), "[%s]\n", ini->type);
	if (!iniWriteAppend(out, out_cap, &used, line)) {
		return 0;
	}

	for (s32 i = 0; i < ini->count; i++) {
		snprintf(line, sizeof(line), "%s = %s\n",
			ini->pairs[i].key, ini->pairs[i].value);
		if (!iniWriteAppend(out, out_cap, &used, line)) {
			return 0;
		}
	}

	if (out_len) {
		*out_len = used;
	}
	return 1;
}

s32 iniWriteFile(const char *filepath, const ini_section_t *ini)
{
	if (!filepath || !filepath[0] || !ini) {
		return 0;
	}

	char buf[16384];
	u32 len = 0;
	if (!iniWriteBuffer(ini, buf, sizeof(buf), &len)) {
		return 0;
	}

	FILE *fp = fopen(filepath, "wb");
	if (!fp) {
		return 0;
	}

	size_t wrote = fwrite(buf, 1, len, fp);
	fclose(fp);
	return wrote == len ? 1 : 0;
}

const char *modiniTemplateForKind(const char *kind)
{
	if (!kind) {
		return NULL;
	}

	if (strcmp(kind, "weapon") == 0) {
		return
			"; weapon.ini - external weapon metadata\n"
			"; The folder name is the catalog id unless an importer overrides it.\n"
			"[weapon]\n"
			"; catalog_id = mod:weapon_catalog_name\n"
			"name = New Weapon\n"
			"dual_wieldable = 0\n"
			"; requirefeature = 0\n"
			"\n"
			"[models]\n"
			"model_file = model.gltf\n"
			"; lo_model_file = model_lod.gltf\n"
			"\n"
			"[animations]\n"
			"; equip_animation = equip.gltf\n"
			"; unequip_animation = unequip.gltf\n"
			"; pritosec_animation = primary_to_secondary.gltf\n"
			"; sectopri_animation = secondary_to_primary.gltf\n"
			"\n"
			"[gameplay]\n"
			"; ammo_type = default\n"
			"; fire_mode = semiauto\n"
			"; muzzle_z = 0.0\n";
	}

	if (strcmp(kind, "projectile") == 0) {
		return
			"; projectile.ini - physical projectile behavior metadata\n"
			"[projectile]\n"
			"; catalog_id = mod:projectile_id\n"
			"name = New Projectile\n"
			"model_file = model.gltf\n"
			"behavior_graph = behavior.graph.json\n"
			"; entity_ref = mod:deployed_entity\n"
			"\n"
			"[motion]\n"
			"; launch_speed = 0\n"
			"; gravity = 0\n"
			"; guidance = none\n"
			"; lifetime_ticks = 0\n"
			"\n"
			"[impact]\n"
			"; on_impact = detonate\n";
	}

	if (strcmp(kind, "entity") == 0) {
		return
			"; entity.ini - deployed/stuck behavior archetype metadata\n"
			"[entity]\n"
			"; catalog_id = mod:entity_id\n"
			"name = New Entity\n"
			"archetype = deployed_object\n"
			"model_file = model.gltf\n"
			"behavior_graph = behavior.graph.json\n"
			"\n"
			"[behavior]\n"
			"; owner_policy = owner_team\n"
			"; activation = armed\n"
			"; cleanup = owner_or_lifetime\n";
	}

	if (strcmp(kind, "material") == 0) {
		return
			"; material.ini - reusable render/surface material metadata\n"
			"[material]\n"
			"; catalog_id = mod:material_id\n"
			"name = New Material\n"
			"material_file = material.json\n"
			"; texture_archive = dependencies/assets/texture/texture.pdtexture\n"
			"; effect_archive = dependencies/assets/effect/effect.pdeffect\n";
	}

	if (strcmp(kind, "texture") == 0) {
		return
			"; texture.ini - reusable texture metadata\n"
			"[texture]\n"
			"; catalog_id = mod:texture_id\n"
			"name = New Texture\n"
			"file_path = texture.png\n"
			"; texture_file = texture.tga\n";
	}

	if (strcmp(kind, "skin") == 0) {
		return
			"; skin.ini - character or surface skin metadata\n"
			"[skin]\n"
			"; catalog_id = mod:skin_id\n"
			"name = New Skin\n"
			"; target = base:character\n"
			"texture_file = texture.tga\n"
			"; material_archive = dependencies/assets/material/material.pdmaterial\n";
	}

	if (strcmp(kind, "effect") == 0) {
		return
			"; effect.ini - reusable visual/feedback effect metadata\n"
			"[effect]\n"
			"; catalog_id = mod:effect_id\n"
			"name = New Effect\n"
			"effect_file = effect.graph.json\n"
			"; texture_archive = dependencies/assets/texture/texture.pdtexture\n"
			"; audio_archive = dependencies/assets/audio/effect.pdsfx\n";
	}

	if (strcmp(kind, "prop") == 0) {
		return
			"; prop.ini - reusable spawnable prop metadata\n"
			"[prop]\n"
			"; catalog_id = mod:prop_id\n"
			"name = New Prop\n"
			"prop_key = object\n"
			"model_file = model.gltf\n"
			"health = 100\n"
			"; flags = 0\n"
			"; behavior_graph = behavior.graph.json\n";
	}

	if (strcmp(kind, "vehicle") == 0) {
		return
			"; vehicle.ini - reusable vehicle metadata\n"
			"[vehicle]\n"
			"; catalog_id = mod:vehicle_id\n"
			"name = New Vehicle\n"
			"model_file = model.gltf\n"
			"; physics_file = physics.json\n"
			"; behavior_graph = behavior.graph.json\n";
	}

	if (strcmp(kind, "mission") == 0) {
		return
			"; mission.ini - mission wrapper metadata\n"
			"[mission]\n"
			"; catalog_id = mod:mission_id\n"
			"name = New Mission\n"
			"; scenario_archive = dependencies/assets/scenario/mission.pdscenario\n"
			"mission_graph_file = mission.graph.json\n"
			"; objectives_file = objectives.tsv\n"
			"; briefing_file = briefing.tsv\n";
	}

	if (strcmp(kind, "head") == 0) {
		return
			"; head.ini - external multiplayer head metadata\n"
			"[head]\n"
			"; catalog_id = mod:head_id\n"
			"headnum = -1\n"
			"rig_class = human_male_neck_standard\n"
			"; requirefeature = 0\n"
			"\n"
			"[model]\n"
			"model_file = model.gltf\n"
			"; scale = 1.0\n"
			"; animscale = 1.0\n"
			"\n"
			"[dependencies]\n"
			"; deps = mod:shared_texture, mod:blink_animation\n";
	}

	if (strcmp(kind, "body") == 0) {
		return
			"; body.ini - external multiplayer body metadata\n"
			"[body]\n"
			"; catalog_id = mod:body_id\n"
			"display_name = New Body\n"
			"bodynum = -1\n"
			"headnum = -1\n"
			"rig_class = human_male_neck_standard\n"
			"; name_langid = 0\n"
			"; requirefeature = 0\n"
			"\n"
			"[model]\n"
			"model_file = model.gltf\n"
			"; hand_model_file = hand.gltf\n"
			"; scale = 1.0\n"
			"; animscale = 1.0\n"
			"\n"
			"[dependencies]\n"
			"; deps = mod:walk_animation, mod:body_texture\n";
	}

	if (strcmp(kind, "arena") == 0) {
		return
			"; arena.ini - external map/arena metadata\n"
			"[arena]\n"
			"; catalog_id = mod:arena_id\n"
			"load_mode = 0\n"
			"; name_langid = 0\n"
			"; requirefeature = 0\n"
			"\n"
			"[geometry]\n"
			"geometry_file = geometry.obj\n"
			"; collision_file = collision.obj\n"
			"; pads_file = pads.ini\n"
			"; setup_file = setup.ini\n"
			"\n"
			"[music]\n"
			"; music_file = audio/music/track/music.ini\n";
	}

	if (strcmp(kind, "scenario") == 0) {
		return
			"; scenario.ini - external scenario source metadata\n"
			"[scenario]\n"
			"; catalog_id = mod:scenario_id\n"
			"name = New Scenario\n"
			"mode = mp|solo\n"
			"\n"
			"[source]\n"
			"scene_file = scene.glb\n"
			"scene_format = GLB\n"
			"runtime_source_file = scene.glb\n"
			"; collision_file = collision.obj\n"
			"collision_fallback = override\n"
			"\n"
			"[setup]\n"
			"pads_file = pads.tsv\n"
			"spawns_file = spawns.tsv\n"
			"volumes_file = volumes.tsv\n"
			"objects_file = objects.tsv\n"
			"setup_fields_file = setup.fields.tsv\n"
			"objectives_file = objectives.tsv\n"
			"navigation_file = navigation.ini\n"
			"level_graph_file = level.graph.json\n"
			"; rooms_file = rooms.obj\n";
	}

	if (strcmp(kind, "gamemode") == 0) {
		return
			"; gamemode.ini - Combat Simulator/custom rules metadata\n"
			"[gamemode]\n"
			"; catalog_id = mod:gamemode_id\n"
			"name = New Game Mode\n"
			"mode_key = custom\n"
			"min_players = 2\n"
			"max_players = 8\n"
			"team_based = 0\n"
			"rules_file = rules.json\n"
			"; scenario_tags = combat,classic\n"
			"; requirefeature = 0\n";
	}

	if (strcmp(kind, "botprofile") == 0 || strcmp(kind, "bot_profile") == 0) {
		return
			"; botprofile.ini - reusable bot skill/personality metadata\n"
			"[bot_profile]\n"
			"; catalog_id = mod:bot_profile_id\n"
			"type_key = general\n"
			"difficulty_key = normal\n"
			"profile_file = profile.json\n"
			"; target_body = base:body_id\n"
			"; name_langid = 0\n"
			"; requirefeature = 0\n"
			"; character_archive = dependencies/assets/character/bot.pdcharacter\n";
	}

	if (strcmp(kind, "hud") == 0) {
		return
			"; hud.ini - reusable gameplay HUD composition metadata\n"
			"[hud]\n"
			"; catalog_id = mod:hud_id\n"
			"name = New HUD\n"
			"hud_key = crosshair\n"
			"texture_file = texture.png\n"
			"; font_archive = dependencies/assets/font/body.pdfont\n";
	}

	if (strcmp(kind, "theme") == 0) {
		return
			"; theme.ini - menu/UI theme bundle metadata\n"
			"[theme]\n"
			"; catalog_id = mod:theme_id\n"
			"name = New Theme\n"
			"theme_file = theme.json\n"
			"; ui_archive = dependencies/assets/ui/chrome.pdui\n"
			"; font_archive = dependencies/assets/font/body.pdfont\n";
	}

	if (strcmp(kind, "animation") == 0) {
		return
			"; animation.ini - external animation metadata\n"
			"[animation]\n"
			"; catalog_id = mod:animation_id\n"
			"name = New Animation\n"
			"anim_id = -1\n"
			"frame_count = 0\n"
			"; target_body = mod:body_id\n"
			"\n"
			"[source]\n"
			"animation_file = animation.gltf\n"
			"; category = weapon_animation\n";
	}

	if (strcmp(kind, "voice") == 0) {
		return
			"; voice.ini - external voice metadata\n"
			"[audio]\n"
			"name = New Voice Line\n"
			"sound_id = -1\n"
			"audio_category = voice\n"
			"duration_ms = 0\n"
			"\n"
			"[source]\n"
			"; Voice authoring uses WAV.\n"
			"file_path = sample.wav\n"
			"\n"
			"[voice]\n"
			"; actor = unknown\n"
			"; transcript =\n"
			"; language =\n"
			"; context =\n";
	}

	if (strcmp(kind, "music") == 0) {
		return
			"; music.ini - external music metadata\n"
			"[audio]\n"
			"name = New Music Track\n"
			"sound_id = -1\n"
			"audio_category = music\n"
			"duration_ms = 0\n"
			"\n"
			"[source]\n"
			"; Music authoring supports OGG, MP3, or WAV.\n"
			"file_path = track.ogg\n"
			"; file_path = track.mp3\n"
			"; file_path = track.wav\n"
			"; midi_file = track.mid\n";
	}

	if (strcmp(kind, "sfx") == 0 || strcmp(kind, "audio") == 0) {
		return
			"; sound.ini - external SFX metadata\n"
			"[audio]\n"
			"name = New Audio\n"
			"sound_id = -1\n"
			"audio_category = sfx\n"
			"duration_ms = 0\n"
			"\n"
			"[source]\n"
			"; SFX authoring uses WAV.\n"
			"file_path = sample.wav\n"
			"\n"
			"[voice]\n"
			"; actor = unknown\n"
			"; transcript =\n"
			"; language =\n"
			"; context =\n";
	}

	if (strcmp(kind, "ui") == 0) {
		return
			"; ui.ini - external UI texture metadata\n"
			"[ui]\n"
			"name = New UI Texture\n"
			"; catalog_id = mod:ui_texture\n"
			"\n"
			"[source]\n"
			"; UI texture authoring supports PNG or TGA.\n"
			"texture_file = texture.png\n"
			"; texture_file = texture.tga\n"
			"\n"
			"[nine_slice]\n"
			"; left = 0\n"
			"; right = 0\n"
			"; top = 0\n"
			"; bottom = 0\n"
			"; edge_mode = stretch\n"
			"; center_mode = stretch\n";
	}

	if (strcmp(kind, "font") == 0) {
		return
			"; font.ini - external UI font metadata\n"
			"[font]\n"
			"name = New Font\n"
			"; catalog_id = mod:font_name\n"
			"\n"
			"[source]\n"
			"; Font authoring supports TTF or OTF.\n"
			"font_file = font.ttf\n"
			"; font_file = font.otf\n"
			"\n"
			"[rendering]\n"
			"size_px = 24\n"
			"; oversample_v = 2\n"
			"; shadow = false\n"
			"; glow = false\n";
	}

	if (strcmp(kind, "lang") == 0 || strcmp(kind, "language") == 0) {
		return
			"; lang.ini - external UTF-8 language-bank metadata\n"
			"[lang]\n"
			"; LANGBANK_* slot to override or provide.\n"
			"bank_id = -1\n"
			"; locale = en\n"
			"\n"
			"[source]\n"
			"; strings.tsv is UTF-8 tab-separated text: index<TAB>text.\n"
			"strings_file = strings.tsv\n"
			"; The loader also accepts strings = strings.tsv for compatibility.\n"
			"; strings = strings.tsv\n";
	}

	return NULL;
}

const char *iniGet(const ini_section_t *ini, const char *key, const char *defval)
{
	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) == 0) {
			return ini->pairs[i].value;
		}
	}
	return defval;
}

s32 iniGetInt(const ini_section_t *ini, const char *key, s32 defval)
{
	const char *val = iniGet(ini, key, NULL);
	if (!val) {
		return defval;
	}
	return (s32)strtol(val, NULL, 0);
}

f32 iniGetFloat(const ini_section_t *ini, const char *key, f32 defval)
{
	const char *val = iniGet(ini, key, NULL);
	if (!val) {
		return defval;
	}
	return strtof(val, NULL);
}

/* ========================================================================
 * Map Mode Parser
 * ======================================================================== */

/**
 * Parse a pipe-separated mode string from an INI "mode" key into a MAP_MODE_*
 * bitmask. Recognised tokens: "mp", "solo", "coop". Unknown tokens are ignored.
 * Returns 0 if val is NULL or empty (caller treats 0 as "all modes").
 *
 * Examples:
 *   "mp"         -> MAP_MODE_MP
 *   "solo"       -> MAP_MODE_SOLO
 *   "mp|solo"    -> MAP_MODE_MP | MAP_MODE_SOLO
 *   "mp|coop"    -> MAP_MODE_MP | MAP_MODE_COOP
 */
static s32 parseModeString(const char *val)
{
	if (!val || !val[0]) {
		return 0;
	}

	s32 mode = 0;

	/* work on a local copy since we mutate it */
	char buf[64];
	strncpy(buf, val, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *tok = buf;
	while (tok) {
		char *pipe = strchr(tok, '|');
		if (pipe) {
			*pipe = '\0';
		}

		char *t = trimWhitespace(tok);
		if (strcmp(t, "mp") == 0) {
			mode |= MAP_MODE_MP;
		} else if (strcmp(t, "solo") == 0) {
			mode |= MAP_MODE_SOLO;
		} else if (strcmp(t, "coop") == 0) {
			mode |= MAP_MODE_COOP;
		}

		tok = pipe ? pipe + 1 : NULL;
	}

	return mode;
}

/* ========================================================================
 * Category Handling
 * ======================================================================== */

/**
 * Map a category directory name to an asset type.
 */
static asset_type_e categoryToType(const char *dirname)
{
	if (strcmp(dirname, "maps") == 0)        return ASSET_MAP;
	if (strcmp(dirname, "characters") == 0)  return ASSET_CHARACTER;
	if (strcmp(dirname, "skins") == 0)       return ASSET_SKIN;
	if (strcmp(dirname, "bot_variants") == 0) return ASSET_BOT_VARIANT;
	if (strcmp(dirname, "weapons") == 0)     return ASSET_WEAPON;
	if (strcmp(dirname, "projectiles") == 0) return ASSET_PROJECTILE;
	if (strcmp(dirname, "entities") == 0)    return ASSET_ENTITY;
	if (strcmp(dirname, "materials") == 0)   return ASSET_MATERIAL;
	if (strcmp(dirname, "textures") == 0)    return ASSET_TEXTURES;
	if (strcmp(dirname, "texture") == 0)     return ASSET_TEXTURE;
	if (strcmp(dirname, "sfx") == 0)         return ASSET_SFX;
	if (strcmp(dirname, "music") == 0)       return ASSET_MUSIC;
	if (strcmp(dirname, "props") == 0)       return ASSET_PROP;
	if (strcmp(dirname, "vehicles") == 0)    return ASSET_VEHICLE;
	if (strcmp(dirname, "missions") == 0)    return ASSET_MISSION;
	if (strcmp(dirname, "ui") == 0)          return ASSET_UI;
	if (strcmp(dirname, "fonts") == 0)       return ASSET_FONT;
	if (strcmp(dirname, "tools") == 0)       return ASSET_TOOL;
	if (strcmp(dirname, "animations") == 0)  return ASSET_ANIMATION;
	if (strcmp(dirname, "hud") == 0)         return ASSET_HUD;
	if (strcmp(dirname, "gamemodes") == 0)   return ASSET_GAMEMODE;
	if (strcmp(dirname, "scenarios") == 0)   return ASSET_SCENARIO;
	if (strcmp(dirname, "audio") == 0)       return ASSET_AUDIO;
	if (strcmp(dirname, "lang_banks") == 0)  return ASSET_LANG;
	if (strcmp(dirname, "lang") == 0)        return ASSET_LANG;
	if (strcmp(dirname, "arenas") == 0)      return ASSET_ARENA;
	if (strcmp(dirname, "bodies") == 0)      return ASSET_BODY;
	if (strcmp(dirname, "heads") == 0)       return ASSET_HEAD;
	if (strcmp(dirname, "effects") == 0)     return ASSET_EFFECT;
	if (strcmp(dirname, "botprofiles") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(dirname, "bot_profiles") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(dirname, "themes") == 0)      return ASSET_THEME;
	return ASSET_NONE;
}

/**
 * Map an INI section type string to an asset type.
 */
static asset_type_e sectionToType(const char *section)
{
	if (strcmp(section, "map") == 0)          return ASSET_MAP;
	if (strcmp(section, "character") == 0)    return ASSET_CHARACTER;
	if (strcmp(section, "skin") == 0)         return ASSET_SKIN;
	if (strcmp(section, "bot_variant") == 0)  return ASSET_BOT_VARIANT;
	if (strcmp(section, "weapon") == 0)       return ASSET_WEAPON;
	if (strcmp(section, "projectile") == 0)   return ASSET_PROJECTILE;
	if (strcmp(section, "entity") == 0)       return ASSET_ENTITY;
	if (strcmp(section, "material") == 0)     return ASSET_MATERIAL;
	if (strcmp(section, "textures") == 0)     return ASSET_TEXTURES;
	if (strcmp(section, "sfx") == 0)          return ASSET_SFX;
	if (strcmp(section, "music") == 0)        return ASSET_MUSIC;
	if (strcmp(section, "prop") == 0)         return ASSET_PROP;
	if (strcmp(section, "vehicle") == 0)      return ASSET_VEHICLE;
	if (strcmp(section, "mission") == 0)      return ASSET_MISSION;
	if (strcmp(section, "ui") == 0)           return ASSET_UI;
	if (strcmp(section, "font") == 0)         return ASSET_FONT;
	if (strcmp(section, "tool") == 0)         return ASSET_TOOL;
	if (strcmp(section, "animation") == 0)    return ASSET_ANIMATION;
	if (strcmp(section, "hud") == 0)          return ASSET_HUD;
	if (strcmp(section, "gamemode") == 0)     return ASSET_GAMEMODE;
	if (strcmp(section, "scenario") == 0)     return ASSET_SCENARIO;
	if (strcmp(section, "audio") == 0)        return ASSET_AUDIO;
	if (strcmp(section, "texture") == 0)      return ASSET_TEXTURE;
	if (strcmp(section, "lang_bank") == 0)    return ASSET_LANG;
	if (strcmp(section, "lang") == 0)         return ASSET_LANG;
	if (strcmp(section, "arena") == 0)        return ASSET_ARENA;
	if (strcmp(section, "body") == 0)         return ASSET_BODY;
	if (strcmp(section, "head") == 0)         return ASSET_HEAD;
	if (strcmp(section, "mesh") == 0)         return ASSET_MODEL;
	if (strcmp(section, "model") == 0)        return ASSET_MODEL;
	if (strcmp(section, "effect") == 0)       return ASSET_EFFECT;
	if (strcmp(section, "botprofile") == 0)   return ASSET_BOT_PROFILE;
	if (strcmp(section, "bot_profile") == 0)  return ASSET_BOT_PROFILE;
	if (strcmp(section, "theme") == 0)        return ASSET_THEME;
	return ASSET_NONE;
}

static s32 pathEndsWithNoCase(const char *s, const char *suffix)
{
	if (!s || !suffix) {
		return 0;
	}
	size_t n = strlen(s);
	size_t m = strlen(suffix);
	if (m > n) {
		return 0;
	}
	const char *tail = s + n - m;
	for (size_t i = 0; i < m; i++) {
		if (tolower((u8)tail[i]) != tolower((u8)suffix[i])) {
			return 0;
		}
	}
	return 1;
}

static asset_type_e typedPdContentTypeForPath(const char *path)
{
	return assetArchiveTypeForPath(path);
}

static const char *typedPdArchiveDescriptorLeaf(const char *path)
{
	return assetArchiveDescriptorForPath(path);
}

static const char *pathLeafAnySeparator(const char *path)
{
	const char *slash = path ? strrchr(path, '/') : NULL;
	const char *backslash = path ? strrchr(path, '\\') : NULL;
	if (backslash && (!slash || backslash > slash)) {
		slash = backslash;
	}
	return slash ? slash + 1 : path;
}

static void pathDirnameAnySeparator(const char *path, char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return;
	}
	out[0] = '\0';
	if (!path) {
		return;
	}

	const char *slash = strrchr(path, '/');
	const char *backslash = strrchr(path, '\\');
	if (backslash && (!slash || backslash > slash)) {
		slash = backslash;
	}
	if (!slash) {
		return;
	}

	size_t len = (size_t)(slash - path);
	if (len >= outsz) {
		len = outsz - 1;
	}
	memcpy(out, path, len);
	out[len] = '\0';
}

static void typedPdDescriptorComponentDir(const char *descriptor_path,
                                          char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return;
	}
	out[0] = '\0';
	if (!descriptor_path) {
		return;
	}

	char dir[FS_MAXPATH];
	pathDirnameAnySeparator(descriptor_path, dir, sizeof(dir));

	const char *leaf = pathLeafAnySeparator(descriptor_path);
	char stem[128];
	strncpy(stem, leaf ? leaf : "", sizeof(stem) - 1);
	stem[sizeof(stem) - 1] = '\0';
	char *dot = strrchr(stem, '.');
	if (dot) {
		*dot = '\0';
	}

	if (!stem[0]) {
		snprintf(out, outsz, "%s", dir);
	} else if (dir[0]) {
		snprintf(out, outsz, "%s/%s", dir, stem);
	} else {
		snprintf(out, outsz, "%s", stem);
	}
	out[outsz - 1] = '\0';
}

/* ========================================================================
 * External Source Path Handling
 * ======================================================================== */

static s32 sourcePathNeedsComponentPrefix(const char *value)
{
	if (!value || !value[0]) {
		return 0;
	}
	if (strstr(value, "::")) {
		return 0;
	}
	if (value[0] == '/' || value[0] == '\\') {
		return 0;
	}
	if (isalpha((u8)value[0]) && value[1] == ':') {
		return 0;
	}
	if (strchr(value, '/') || strchr(value, '\\')) {
		return 0;
	}
	return 1;
}

static void qualifyIniSourcePath(ini_section_t *ini, const char *key,
                                 const char *component_dir)
{
	if (!ini || !key || !component_dir || !component_dir[0]) {
		return;
	}

	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) {
			continue;
		}
		if (!sourcePathNeedsComponentPrefix(ini->pairs[i].value)) {
			continue;
		}

		char full[sizeof(ini->pairs[i].value)];
		snprintf(full, sizeof(full), "%s/%s", component_dir, ini->pairs[i].value);
		strncpy(ini->pairs[i].value, full, sizeof(ini->pairs[i].value) - 1);
		ini->pairs[i].value[sizeof(ini->pairs[i].value) - 1] = '\0';
	}
}

static void qualifyIniSourcePaths(ini_section_t *ini, const char *component_dir)
{
	static const char *keys[] = {
		"bodyfile",
		"headfile",
		"model_file",
		"model",
		"lo_model_file",
		"hand_model_file",
		"scene_file",
		"scene",
		"runtime_source_file",
		"geometry_file",
		"geometry",
		"blender_scene_file",
		"blender_scene",
		"visual_scene_file",
		"visual_scene",
		"visual_material_file",
		"visual_materials_file",
		"collision_file",
		"collision_source_file",
		"collision_source",
		"tiles_file",
		"pads_file",
		"spawns_file",
		"volumes_file",
		"objects_file",
		"setup_fields_file",
		"setup_file",
		"mpsetup_file",
		"rooms_file",
		"rooms",
		"material_file",
		"props_file",
		"props",
		"objectives_file",
		"objectives",
		"navigation_file",
		"level_graph_file",
		"mission_graph_file",
		"rules_file",
		"profile_file",
		"visual_source_file",
		"texture_manifest_file",
		"music_file",
		"midi_file",
		"file_path",
		"theme_file",
		"animation_file",
		"texture_file",
		"texture",
		"font_file",
		"font",
		"strings_file",
		"strings",
		"strings_tsv",
		NULL
	};

	for (s32 i = 0; keys[i]; i++) {
		qualifyIniSourcePath(ini, keys[i], component_dir);
	}
}

static s32 archiveInnerPathIsSafe(const char *value)
{
	if (!value || !value[0]) {
		return 0;
	}
	if (strstr(value, "::")) {
		return 0;
	}
	if (value[0] == '/' || value[0] == '\\') {
		return 0;
	}
	if (isalpha((u8)value[0]) && value[1] == ':') {
		return 0;
	}
	const char *p = value;
	while (*p) {
		const char *end = p;
		while (*end && *end != '/' && *end != '\\') {
			end++;
		}
		if ((end - p) == 2 && p[0] == '.' && p[1] == '.') {
			return 0;
		}
		p = (*end) ? end + 1 : end;
	}
	return 1;
}

static void qualifyTypedArchiveSourcePath(ini_section_t *ini, const char *key,
                                          const char *archive_ref)
{
	if (!ini || !key || !archive_ref || !archive_ref[0]) {
		return;
	}

	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) {
			continue;
		}
		if (!archiveInnerPathIsSafe(ini->pairs[i].value)) {
			continue;
		}

		char full[sizeof(ini->pairs[i].value)];
		snprintf(full, sizeof(full), "%s::%s", archive_ref, ini->pairs[i].value);
		strncpy(ini->pairs[i].value, full, sizeof(ini->pairs[i].value) - 1);
		ini->pairs[i].value[sizeof(ini->pairs[i].value) - 1] = '\0';
	}
}

static void qualifyTypedArchiveSourcePaths(ini_section_t *ini,
                                           const char *archive_ref)
{
	static const char *keys[] = {
		"bodyfile",
		"headfile",
		"model_file",
		"model",
		"lo_model_file",
		"hand_model_file",
		"scene_file",
		"scene",
		"runtime_source_file",
		"geometry_file",
		"geometry",
		"blender_scene_file",
		"blender_scene",
		"visual_scene_file",
		"visual_scene",
		"visual_material_file",
		"visual_materials_file",
		"collision_file",
		"collision_source_file",
		"collision_source",
		"tiles_file",
		"pads_file",
		"spawns_file",
		"volumes_file",
		"objects_file",
		"setup_fields_file",
		"setup_file",
		"mpsetup_file",
		"rooms_file",
		"rooms",
		"material_file",
		"props_file",
		"props",
		"objectives_file",
		"objectives",
		"navigation_file",
		"level_graph_file",
		"mission_graph_file",
		"rules_file",
		"profile_file",
		"visual_source_file",
		"texture_manifest_file",
		"music_file",
		"midi_file",
		"file_path",
		"animation_file",
		"texture_file",
		"texture",
		"font_file",
		"font",
		"strings_file",
		"strings",
		"strings_tsv",
		NULL
	};

	for (s32 i = 0; keys[i]; i++) {
		qualifyTypedArchiveSourcePath(ini, keys[i], archive_ref);
	}
}

/* ========================================================================
 * Component Registration
 * ======================================================================== */

static s32 parseAudioCategoryValue(const char *value, s32 default_category)
{
	if (!value || !value[0]) {
		return default_category;
	}

	/* Numeric form: 0/1/2 */
	if (((u8)value[0] >= '0' && (u8)value[0] <= '9') || value[0] == '-' || value[0] == '+') {
		s32 n = (s32)strtol(value, NULL, 10);
		if (n >= AUDIO_CAT_SFX && n <= AUDIO_CAT_VOICE) {
			return n;
		}
		return default_category;
	}

	/* Text form: music/sfx/voice (+ aliases) */
	char lower[32];
	s32 i = 0;
	while (value[i] && i < (s32)sizeof(lower) - 1) {
		lower[i] = (char)tolower((u8)value[i]);
		i++;
	}
	lower[i] = '\0';

	if (strcmp(lower, "music") == 0 || strcmp(lower, "track") == 0) {
		return AUDIO_CAT_MUSIC;
	}
	if (strcmp(lower, "sfx") == 0 || strcmp(lower, "sound") == 0 || strcmp(lower, "soundfx") == 0) {
		return AUDIO_CAT_SFX;
	}
	if (strcmp(lower, "voice") == 0 || strcmp(lower, "dialog") == 0 || strcmp(lower, "dialogue") == 0) {
		return AUDIO_CAT_VOICE;
	}

	return default_category;
}

static void lowerKey(const char *value, char *out, size_t out_n)
{
	if (!out || out_n == 0) {
		return;
	}
	out[0] = '\0';
	if (!value) {
		return;
	}

	size_t i = 0;
	while (value[i] && i + 1 < out_n) {
		char c = (char)tolower((u8)value[i]);
		if (c == '-' || c == ' ') {
			c = '_';
		}
		out[i] = c;
		i++;
	}
	out[i] = '\0';
}

static s32 parseNamedIntValue(const char *value, s32 default_value,
                              const char *const *keys,
                              const s32 *values, s32 count)
{
	if (!value || !value[0]) {
		return default_value;
	}
	if (((u8)value[0] >= '0' && (u8)value[0] <= '9')
			|| value[0] == '-' || value[0] == '+') {
		return (s32)strtol(value, NULL, 10);
	}

	char lower[64];
	lowerKey(value, lower, sizeof(lower));
	for (s32 i = 0; i < count; i++) {
		if (strcmp(lower, keys[i]) == 0) {
			return values[i];
		}
	}
	return default_value;
}

static s32 parseModeKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"combat",
		"hold_the_briefcase",
		"hacker_central",
		"pop_a_cap",
		"king_of_the_hill",
		"capture_the_case",
	};
	static const s32 values[] = { 0, 1, 2, 3, 4, 5 };
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseBotTypeKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"general", "peace", "shield", "rocket", "kaze", "fist",
		"prey", "coward", "judge", "feud", "speed", "turtle", "venge",
	};
	static const s32 values[] = {
		BOTTYPE_GENERAL, BOTTYPE_PEACE, BOTTYPE_SHIELD, BOTTYPE_ROCKET,
		BOTTYPE_KAZE, BOTTYPE_FIST, BOTTYPE_PREY, BOTTYPE_COWARD,
		BOTTYPE_JUDGE, BOTTYPE_FEUD, BOTTYPE_SPEED, BOTTYPE_TURTLE,
		BOTTYPE_VENGE,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseBotDifficultyKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"meat", "easy", "normal", "hard", "perfect", "dark",
	};
	static const s32 values[] = {
		BOTDIFF_MEAT, BOTDIFF_EASY, BOTDIFF_NORMAL, BOTDIFF_HARD,
		BOTDIFF_PERFECT, BOTDIFF_DARK,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseHudElementKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"crosshair", "ammo", "radar", "health", "timer", "score",
	};
	static const s32 values[] = {
		HUD_ELEM_CROSSHAIR, HUD_ELEM_AMMO, HUD_ELEM_RADAR,
		HUD_ELEM_HEALTH, HUD_ELEM_TIMER, HUD_ELEM_SCORE,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parsePropKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"object", "door", "character", "weapon_pickup",
		"eyespy", "player", "explosion", "smoke",
	};
	static const s32 values[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseEffectTypeKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"tint", "glow", "shimmer", "darken", "screen", "particle",
	};
	static const s32 values[] = {
		EFFECT_TYPE_TINT, EFFECT_TYPE_GLOW, EFFECT_TYPE_SHIMMER,
		EFFECT_TYPE_DARKEN, EFFECT_TYPE_SCREEN, EFFECT_TYPE_PARTICLE,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static s32 parseEffectTargetKeyValue(const char *value, s32 default_value)
{
	static const char *const keys[] = {
		"scene", "player", "character", "prop", "weapon", "level",
	};
	static const s32 values[] = {
		EFFECT_TARGET_SCENE, EFFECT_TARGET_PLAYER, EFFECT_TARGET_CHR,
		EFFECT_TARGET_PROP, EFFECT_TARGET_WEAPON, EFFECT_TARGET_LEVEL,
	};
	return parseNamedIntValue(value, default_value, keys, values,
		(s32)(sizeof(values) / sizeof(values[0])));
}

static void registerDependencyList(const char *owner_id, const char *deps,
                                   s32 is_bundled)
{
	if (!owner_id || !owner_id[0] || !deps || !deps[0]) {
		return;
	}

	char buf[512];
	strncpy(buf, deps, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *tok = buf;
	while (tok) {
		char *next = strpbrk(tok, ",|");
		if (next) {
			*next++ = '\0';
		}

		char *dep = trimWhitespace(tok);
		if (dep[0]) {
			catalogDepRegister(owner_id, dep, is_bundled);
		}

		tok = next;
	}
}

/**
 * Register a single component from a parsed INI section.
 *
 * @param ini       Parsed INI section
 * @param dirpath   Absolute path to the component directory
 * @param mod_id    Mod identifier (e.g., "my_mod")
 * @return 1 on success, 0 on failure
 */
static s32 registerComponent(const ini_section_t *ini, const char *dirpath,
                              const char *mod_id)
{
	ini_section_t local_ini;
	if (ini) {
		local_ini = *ini;
		qualifyIniSourcePaths(&local_ini, dirpath);
		ini = &local_ini;
	}

	asset_type_e type = sectionToType(ini->type);
	if (type == ASSET_NONE) {
		sysLogPrintf(LOG_WARNING, "assetcatalog_scanner: unknown section type '%s' in %s",
			ini->type, dirpath);
		return 0;
	}

	/* Build the asset ID. The INI "name" field is display-only; the
	 * folder name is the default ID, and catalog_id/id can override it
	 * when canonical layouts have repeated leaf names such as animation
	 * categories using "idle" in more than one family. */
	const char *category = iniGet(ini, "category", mod_id);
	const char *explicit_id = iniGet(ini, "catalog_id", iniGet(ini, "id", ""));

	/* Extract folder name from dirpath (last path component) */
	const char *folder = strrchr(dirpath, '/');
	folder = folder ? folder + 1 : dirpath;

	/* Build ID: use category + folder name, or just folder if category matches */
	char idbuf[CATALOG_ID_LEN];
	snprintf(idbuf, sizeof(idbuf), "%s", explicit_id[0] ? explicit_id : folder);

	s32 preserved_weapon_id = -1;
	s32 preserved_runtime_index = -1;
	s16 preserved_mp_index = -1;
	u8 preserved_weapon_requirefeature = 0;
	s32 preserved_dual_wieldable = 0;
	char preserved_weapon_name[64];
	preserved_weapon_name[0] = '\0';
	if (type == ASSET_WEAPON) {
		const asset_entry_t *existing = assetCatalogResolve(idbuf);
		if (existing && existing->type == ASSET_WEAPON) {
			preserved_weapon_id = existing->ext.weapon.weapon_id;
			preserved_runtime_index = existing->runtime_index;
			preserved_mp_index = existing->mp_index;
			preserved_weapon_requirefeature = existing->ext.weapon.requirefeature;
			preserved_dual_wieldable = existing->ext.weapon.dual_wieldable;
			strncpy(preserved_weapon_name, existing->ext.weapon.name,
				sizeof(preserved_weapon_name) - 1);
			preserved_weapon_name[sizeof(preserved_weapon_name) - 1] = '\0';
		}
	}

	/* Register the base entry */
	asset_entry_t *e = assetCatalogRegister(idbuf, type);
	if (!e) {
		sysLogPrintf(LOG_ERROR, "assetcatalog_scanner: failed to register '%s'", idbuf);
		return 0;
	}

	/* Common fields */
	strncpy(e->category, category, CATALOG_CATEGORY_LEN - 1);
	strncpy(e->dirpath, dirpath, FS_MAXPATH - 1);
	e->model_scale = iniGetFloat(ini, "model_scale", 1.0f);
	e->enabled = iniGetInt(ini, "enabled", 1);
	e->bundled = iniGetInt(ini, "bundled", 0);
	e->temporary = 0;
	e->runtime_index = -1;  /* assigned later during callsite migration */

	/* Type-specific fields */
	switch (type) {
	case ASSET_MAP:
		e->ext.map.stagenum = iniGetInt(ini, "stagenum", -1);
		e->ext.map.mode = parseModeString(iniGet(ini, "mode", ""));
		{
			const char *mf = iniGet(ini, "music_file", "");
			if (mf[0]) {
				strncpy(e->ext.map.music_file, mf, FS_MAXPATH - 1);
			}
		}
		break;

	case ASSET_CHARACTER:
		{
			const char *bf = iniGet(ini, "bodyfile", "");
			const char *hf = iniGet(ini, "headfile", "");
			strncpy(e->ext.character.bodyfile, bf, FS_MAXPATH - 1);
			strncpy(e->ext.character.headfile, hf, FS_MAXPATH - 1);

			/* C-2-ext: resolve bodyfile basename to ROM filenum for reverse-index.
			 * INI path is like "files/Cbond_bodyZ" — basename matches fileSlots[n].name. */
			if (bf[0]) {
				const char *bn = strrchr(bf, '/');
				bn = bn ? bn + 1 : bf;
				s32 fnum = romdataFileGetNumForName(bn);
				if (fnum > 0) {
					e->source_filenum = fnum;
				}
				/* Asset Provider: mod characters are served by FileProvider
				 * from a loose file on disk. The bodyfile path is relative
				 * to the FS base dir (fsFileLoad resolves it). */
				catalogSetPrimaryFile(e, bf);
			}
		}
		break;

	case ASSET_SKIN:
		{
			const char *target = iniGet(ini, "target", "");
			const char *tf = iniGet(ini, "texture_file",
				iniGet(ini, "texture", ""));
			const char *sf = iniGet(ini, "skin_file",
				iniGet(ini, "file_path", ""));
			strncpy(e->ext.skin.target_id, target, CATALOG_ID_LEN - 1);
			strncpy(e->ext.skin.skin_file, sf, sizeof(e->ext.skin.skin_file) - 1);
			e->ext.skin.skin_file[sizeof(e->ext.skin.skin_file) - 1] = '\0';
			strncpy(e->ext.skin.texture_file, tf, sizeof(e->ext.skin.texture_file) - 1);
			e->ext.skin.texture_file[sizeof(e->ext.skin.texture_file) - 1] = '\0';
			if (e->ext.skin.texture_file[0]) {
				catalogSetPrimaryFile(e, e->ext.skin.texture_file);
			} else if (e->ext.skin.skin_file[0]) {
				catalogSetPrimaryFile(e, e->ext.skin.skin_file);
			}
		}
		break;

	case ASSET_BOT_VARIANT:
		{
			const char *bt = iniGet(ini, "base_type", "NormalSim");
			strncpy(e->ext.bot_variant.base_type, bt, 31);
			e->ext.bot_variant.accuracy = iniGetFloat(ini, "accuracy", 0.5f);
			e->ext.bot_variant.reaction_time = iniGetFloat(ini, "reaction_time", 0.5f);
			e->ext.bot_variant.aggression = iniGetFloat(ini, "aggression", 0.5f);
		}
		break;

	case ASSET_ARENA:
		e->ext.arena.stagenum = iniGetInt(ini, "stagenum", -1);
		e->ext.arena.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
		e->ext.arena.name_langid = iniGetInt(ini, "name_langid", 0);
		e->ext.arena.load_mode = iniGetInt(ini, "load_mode", ARENA_LOADMODE_PLAYABLE);
		{
			const char *gf = iniGet(ini, "geometry_file",
				iniGet(ini, "geometry", ""));
			if (gf[0]) {
				catalogSetPrimaryFile(e, gf);
			}
		}
		break;

	case ASSET_BODY:
		e->ext.body.bodynum = (s16)iniGetInt(ini, "bodynum", -1);
		e->ext.body.name_langid = (s16)iniGetInt(ini, "name_langid", 0);
		e->ext.body.headnum = (s16)iniGetInt(ini, "headnum", -1);
		e->ext.body.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
		catalogSetBodyDisplayName(e, iniGet(ini, "display_name",
			iniGet(ini, "name", "")));
		catalogSetBodyRigClass(e, iniGet(ini, "rig_class", ""));
		{
			const char *mf = iniGet(ini, "model_file",
				iniGet(ini, "model", ""));
			if (mf[0]) {
				catalogSetPrimaryFile(e, mf);
			}
		}
		break;

	case ASSET_HEAD:
		e->ext.head.headnum = (s16)iniGetInt(ini, "headnum", -1);
		e->ext.head.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
		catalogSetHeadRigClass(e, iniGet(ini, "rig_class", ""));
		{
			const char *mf = iniGet(ini, "model_file",
				iniGet(ini, "model", ""));
			if (mf[0]) {
				catalogSetPrimaryFile(e, mf);
			}
		}
		break;

	case ASSET_MODEL:
		{
			const char *pf = iniGet(ini, "model_file",
				iniGet(ini, "model",
				iniGet(ini, "geometry_file",
				iniGet(ini, "geometry",
				iniGet(ini, "file_path", "")))));
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_WEAPON:
		e->ext.weapon.weapon_id = iniGetInt(ini, "weapon_id", preserved_weapon_id);
		if (e->ext.weapon.weapon_id >= 0
				&& e->ext.weapon.weapon_id < NUM_MPWEAPONS) {
			e->mp_index = (s16)e->ext.weapon.weapon_id;
			e->runtime_index = catalogGetMpWeaponNum(e->ext.weapon.weapon_id);
		} else {
			e->mp_index = preserved_mp_index;
			e->runtime_index = preserved_runtime_index;
		}
		{
			const char *weapon_name = iniGet(ini, "name", "");
			strncpy(e->ext.weapon.name,
				weapon_name[0] ? weapon_name : preserved_weapon_name,
				sizeof(e->ext.weapon.name) - 1);
		}
		strncpy(e->ext.weapon.model_file, iniGet(ini, "model_file", ""), sizeof(e->ext.weapon.model_file) - 1);
		if (e->ext.weapon.model_file[0]) {
			catalogSetPrimaryFile(e, e->ext.weapon.model_file);
		}
		/* S484 F9 / Mike I.2 (2026-04-27): damage/fire_rate/ammo_type
		 * shadow fields dropped from ext.weapon. The catalog manager
		 * is the single source of truth for those gameplay numbers
		 * (catalogManagerGetWeaponByIndex(weapon_num)->...). The mod
		 * INI parser ignores those keys silently if present. */
		e->ext.weapon.dual_wieldable = iniGetInt(ini, "dual_wieldable",
			preserved_dual_wieldable);
		e->ext.weapon.requirefeature = preserved_weapon_requirefeature;
		break;

	case ASSET_PROJECTILE:
		strncpy(e->ext.projectile.name, iniGet(ini, "name", ""), sizeof(e->ext.projectile.name) - 1);
		strncpy(e->ext.projectile.model_file, iniGet(ini, "model_file",
			iniGet(ini, "model", "")), sizeof(e->ext.projectile.model_file) - 1);
		strncpy(e->ext.projectile.behavior_graph, iniGet(ini, "behavior_graph",
			iniGet(ini, "graph", "")), sizeof(e->ext.projectile.behavior_graph) - 1);
		strncpy(e->ext.projectile.entity_ref, iniGet(ini, "entity_ref",
			iniGet(ini, "transition_entity", "")), sizeof(e->ext.projectile.entity_ref) - 1);
		if (e->ext.projectile.behavior_graph[0]) {
			catalogSetPrimaryFile(e, e->ext.projectile.behavior_graph);
		} else if (e->ext.projectile.model_file[0]) {
			catalogSetPrimaryFile(e, e->ext.projectile.model_file);
		}
		break;

	case ASSET_ENTITY:
		strncpy(e->ext.entity.name, iniGet(ini, "name", ""), sizeof(e->ext.entity.name) - 1);
		strncpy(e->ext.entity.archetype, iniGet(ini, "archetype", ""), sizeof(e->ext.entity.archetype) - 1);
		strncpy(e->ext.entity.model_file, iniGet(ini, "model_file",
			iniGet(ini, "model", "")), sizeof(e->ext.entity.model_file) - 1);
		strncpy(e->ext.entity.behavior_graph, iniGet(ini, "behavior_graph",
			iniGet(ini, "graph", "")), sizeof(e->ext.entity.behavior_graph) - 1);
		if (e->ext.entity.behavior_graph[0]) {
			catalogSetPrimaryFile(e, e->ext.entity.behavior_graph);
		} else if (e->ext.entity.model_file[0]) {
			catalogSetPrimaryFile(e, e->ext.entity.model_file);
		}
		break;

	case ASSET_PROP:
		e->ext.prop.prop_type = parsePropKeyValue(
			iniGet(ini, "prop_key", iniGet(ini, "prop_type", "")), 0);
		strncpy(e->ext.prop.name, iniGet(ini, "name", ""), sizeof(e->ext.prop.name) - 1);
		strncpy(e->ext.prop.prop_file, iniGet(ini, "prop_file",
			iniGet(ini, "file_path", "")), sizeof(e->ext.prop.prop_file) - 1);
		strncpy(e->ext.prop.model_file, iniGet(ini, "model_file", ""), sizeof(e->ext.prop.model_file) - 1);
		if (e->ext.prop.model_file[0]) {
			catalogSetPrimaryFile(e, e->ext.prop.model_file);
		} else if (e->ext.prop.prop_file[0]) {
			catalogSetPrimaryFile(e, e->ext.prop.prop_file);
		}
		e->ext.prop.flags = (u32)iniGetInt(ini, "flags", 0);
		e->ext.prop.health = iniGetFloat(ini, "health", 100.0f);
		break;

	case ASSET_ANIMATION:
		e->ext.anim.anim_id = iniGetInt(ini, "anim_id", -1);
		if (e->ext.anim.anim_id >= 0) {
			e->source_animnum = e->ext.anim.anim_id;
		}
		strncpy(e->ext.anim.name, iniGet(ini, "name", ""), sizeof(e->ext.anim.name) - 1);
		e->ext.anim.frame_count = iniGetInt(ini, "frame_count", 0);
		strncpy(e->ext.anim.target_body, iniGet(ini, "target_body", ""), sizeof(e->ext.anim.target_body) - 1);
		{
			const char *af = iniGet(ini, "animation_file",
				iniGet(ini, "file_path", ""));
			if (af[0]) {
				catalogSetPrimaryFile(e, af);
			}
		}
		break;

	case ASSET_TEXTURE:
		e->ext.texture.texture_id = iniGetInt(ini, "texture_id", -1);
		if (e->ext.texture.texture_id >= 0) {
			e->source_texnum = e->ext.texture.texture_id;
		}
		e->ext.texture.width = iniGetInt(ini, "width", 0);
		e->ext.texture.height = iniGetInt(ini, "height", 0);
		e->ext.texture.format = iniGetInt(ini, "format", 0);
		strncpy(e->ext.texture.file_path, iniGet(ini, "file_path",
			iniGet(ini, "texture_file", "")), sizeof(e->ext.texture.file_path) - 1);
		if (e->ext.texture.file_path[0]) {
			catalogSetPrimaryFile(e, e->ext.texture.file_path);
		}
		break;

	case ASSET_MATERIAL:
		{
			const char *pf = iniGet(ini, "material_file",
				iniGet(ini, "file_path",
				iniGet(ini, "texture_archive",
				iniGet(ini, "texture_file", ""))));
			strncpy(e->ext.material.material_file,
				iniGet(ini, "material_file", iniGet(ini, "file_path", "")),
				sizeof(e->ext.material.material_file) - 1);
			strncpy(e->ext.material.texture_archive,
				iniGet(ini, "texture_archive", ""),
				sizeof(e->ext.material.texture_archive) - 1);
			strncpy(e->ext.material.effect_archive,
				iniGet(ini, "effect_archive", ""),
				sizeof(e->ext.material.effect_archive) - 1);
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_GAMEMODE:
		e->ext.gamemode.mode_id = parseModeKeyValue(
			iniGet(ini, "mode_key", iniGet(ini, "mode_id", "")), -1);
		strncpy(e->ext.gamemode.name, iniGet(ini, "name", ""), sizeof(e->ext.gamemode.name) - 1);
		strncpy(e->ext.gamemode.description, iniGet(ini, "description", ""), sizeof(e->ext.gamemode.description) - 1);
		e->ext.gamemode.min_players = iniGetInt(ini, "min_players", 2);
		e->ext.gamemode.max_players = iniGetInt(ini, "max_players", 4);
		e->ext.gamemode.team_based = iniGetInt(ini, "team_based", 0);
		e->ext.gamemode.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
		{
			const char *rf = iniGet(ini, "rules_file",
				iniGet(ini, "file_path", ""));
			strncpy(e->ext.gamemode.rules_file, rf,
				sizeof(e->ext.gamemode.rules_file) - 1);
			if (rf[0]) {
				catalogSetPrimaryFile(e, rf);
			}
		}
		break;

	case ASSET_SCENARIO:
		e->ext.scenario.stagenum = iniGetInt(ini, "stagenum", -1);
		e->ext.scenario.mode = parseModeString(iniGet(ini, "mode", ""));
		{
			const char *sf = iniGet(ini, "scene_file",
				iniGet(ini, "scene",
				iniGet(ini, "runtime_source_file",
				iniGet(ini, "blender_scene_file",
				iniGet(ini, "visual_scene_file", "")))));
			const char *cf = iniGet(ini, "collision_file",
				iniGet(ini, "collision_source_file",
				iniGet(ini, "collision_source", "")));
			const char *rf = iniGet(ini, "rooms_file",
				iniGet(ini, "rooms",
				iniGet(ini, "geometry_file",
				iniGet(ini, "geometry", ""))));
			strncpy(e->ext.scenario.scene_file, sf,
				sizeof(e->ext.scenario.scene_file) - 1);
			strncpy(e->ext.scenario.collision_file, cf,
				sizeof(e->ext.scenario.collision_file) - 1);
			strncpy(e->ext.scenario.rooms_file, rf,
				sizeof(e->ext.scenario.rooms_file) - 1);
			strncpy(e->ext.scenario.pads_file,
				iniGet(ini, "pads_file", ""),
				sizeof(e->ext.scenario.pads_file) - 1);
			strncpy(e->ext.scenario.spawns_file,
				iniGet(ini, "spawns_file", ""),
				sizeof(e->ext.scenario.spawns_file) - 1);
			strncpy(e->ext.scenario.volumes_file,
				iniGet(ini, "volumes_file", ""),
				sizeof(e->ext.scenario.volumes_file) - 1);
			strncpy(e->ext.scenario.objects_file,
				iniGet(ini, "objects_file",
				iniGet(ini, "props_file", "")),
				sizeof(e->ext.scenario.objects_file) - 1);
			strncpy(e->ext.scenario.setup_fields_file,
				iniGet(ini, "setup_fields_file", ""),
				sizeof(e->ext.scenario.setup_fields_file) - 1);
			strncpy(e->ext.scenario.objectives_file,
				iniGet(ini, "objectives_file", ""),
				sizeof(e->ext.scenario.objectives_file) - 1);
			strncpy(e->ext.scenario.navigation_file,
				iniGet(ini, "navigation_file", ""),
				sizeof(e->ext.scenario.navigation_file) - 1);
			strncpy(e->ext.scenario.level_graph_file,
				iniGet(ini, "level_graph_file",
				iniGet(ini, "level_graph", "")),
				sizeof(e->ext.scenario.level_graph_file) - 1);
			if (sf[0]) {
				catalogSetPrimaryFile(e, sf);
			} else if (rf[0]) {
				catalogSetPrimaryFile(e, rf);
			}
		}
		break;

	case ASSET_AUDIO:
		e->ext.audio.sound_id = iniGetInt(ini, "sound_id", -1);
		strncpy(e->ext.audio.name, iniGet(ini, "name", ""), sizeof(e->ext.audio.name) - 1);
		e->ext.audio.category = parseAudioCategoryValue(
			iniGet(ini, "audio_category",
				iniGet(ini, "kind",
				iniGet(ini, "category", ""))), AUDIO_CAT_SFX);
		e->ext.audio.duration_ms = iniGetInt(ini, "duration_ms", 0);
		strncpy(e->ext.audio.file_path, iniGet(ini, "file_path", ""), sizeof(e->ext.audio.file_path) - 1);
		if (e->ext.audio.file_path[0]) {
			catalogSetPrimaryFile(e, e->ext.audio.file_path);
		}
		break;

	case ASSET_HUD:
		e->ext.hud.element_type = parseHudElementKeyValue(
			iniGet(ini, "hud_key",
				iniGet(ini, "element",
				iniGet(ini, "element_type", ""))),
			HUD_ELEM_CROSSHAIR);
		e->ext.hud.hud_id = iniGetInt(ini, "hud_id",
			e->ext.hud.element_type);
		strncpy(e->ext.hud.name, iniGet(ini, "name", ""), sizeof(e->ext.hud.name) - 1);
		strncpy(e->ext.hud.texture_file, iniGet(ini, "texture_file", ""), sizeof(e->ext.hud.texture_file) - 1);
		strncpy(e->ext.hud.layout_file, iniGet(ini, "layout_file",
			iniGet(ini, "file_path", "")), sizeof(e->ext.hud.layout_file) - 1);
		if (e->ext.hud.texture_file[0]) {
			catalogSetPrimaryFile(e, e->ext.hud.texture_file);
		} else if (e->ext.hud.layout_file[0]) {
			catalogSetPrimaryFile(e, e->ext.hud.layout_file);
		}
		break;

	case ASSET_EFFECT:
		strncpy(e->ext.effect.name, iniGet(ini, "name", ""),
			sizeof(e->ext.effect.name) - 1);
		e->ext.effect.effect_type = parseEffectTypeKeyValue(
			iniGet(ini, "effect_key", iniGet(ini, "effect_type", "")),
			EFFECT_TYPE_PARTICLE);
		e->ext.effect.target = parseEffectTargetKeyValue(
			iniGet(ini, "target_key", iniGet(ini, "target", "")),
			EFFECT_TARGET_SCENE);
		strncpy(e->ext.effect.effect_file, iniGet(ini, "effect_file",
			iniGet(ini, "behavior_graph",
			iniGet(ini, "file_path", ""))),
			sizeof(e->ext.effect.effect_file) - 1);
		strncpy(e->ext.effect.shader_id, iniGet(ini, "shader_id", ""),
			sizeof(e->ext.effect.shader_id) - 1);
		e->ext.effect.intensity = iniGetFloat(ini, "intensity", 1.0f);
		{
			const char *pf = e->ext.effect.effect_file;
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_UI:
		{
			const char *pf = iniGet(ini, "file_path",
				iniGet(ini, "texture_file",
				iniGet(ini, "texture",
				iniGet(ini, "ui_file", ""))));
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_FONT:
		{
			const char *pf = iniGet(ini, "font_file",
				iniGet(ini, "glyphs_file",
				iniGet(ini, "file_path",
				iniGet(ini, "font", ""))));
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_LANG:
		/* bank_id: the LANGBANK_* slot this mod lang bank occupies.
		 * Mod declares an integer bank_id (0-68) in its component INI. */
		e->ext.lang.bank_id = iniGetInt(ini, "bank_id", -1);
		{
			const char *sf = iniGet(ini, "strings_file",
				iniGet(ini, "strings",
				iniGet(ini, "strings_tsv",
				iniGet(ini, "file_path", ""))));
			strncpy(e->ext.lang.strings_file, sf, sizeof(e->ext.lang.strings_file) - 1);
			if (sf[0]) {
				catalogSetPrimaryFile(e, sf);
			}
		}
		break;

	case ASSET_BOT_PROFILE:
		e->ext.bot_profile.type = parseBotTypeKeyValue(
			iniGet(ini, "type_key", iniGet(ini, "type", "")),
			BOTTYPE_GENERAL);
		e->ext.bot_profile.difficulty = parseBotDifficultyKeyValue(
			iniGet(ini, "difficulty_key", iniGet(ini, "difficulty", "")),
			BOTDIFF_NORMAL);
		e->ext.bot_profile.body = (s16)iniGetInt(ini, "body", -1);
		e->ext.bot_profile.name_langid = (s16)iniGetInt(ini, "name_langid", 0);
		e->ext.bot_profile.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
		strncpy(e->ext.bot_profile.target_body,
			iniGet(ini, "target_body",
			iniGet(ini, "body_id",
			iniGet(ini, "body_ref", ""))),
			sizeof(e->ext.bot_profile.target_body) - 1);
		strncpy(e->ext.bot_profile.profile_file, iniGet(ini, "profile_file",
			iniGet(ini, "file_path", "")), sizeof(e->ext.bot_profile.profile_file) - 1);
		if (e->ext.bot_profile.profile_file[0]) {
			catalogSetPrimaryFile(e, e->ext.bot_profile.profile_file);
		}
		break;

	case ASSET_VEHICLE:
		{
			strncpy(e->ext.vehicle.model_file,
				iniGet(ini, "model_file", iniGet(ini, "file_path", "")),
				sizeof(e->ext.vehicle.model_file) - 1);
			strncpy(e->ext.vehicle.physics_file,
				iniGet(ini, "physics_file", ""),
				sizeof(e->ext.vehicle.physics_file) - 1);
			strncpy(e->ext.vehicle.behavior_graph,
				iniGet(ini, "behavior_graph", ""),
				sizeof(e->ext.vehicle.behavior_graph) - 1);
			const char *pf = e->ext.vehicle.model_file[0] ?
				e->ext.vehicle.model_file :
				(e->ext.vehicle.behavior_graph[0] ?
					e->ext.vehicle.behavior_graph : e->ext.vehicle.physics_file);
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_MISSION:
		{
			strncpy(e->ext.mission.scenario_archive,
				iniGet(ini, "scenario_archive", iniGet(ini, "file_path", "")),
				sizeof(e->ext.mission.scenario_archive) - 1);
			strncpy(e->ext.mission.objectives_file,
				iniGet(ini, "objectives_file", ""),
				sizeof(e->ext.mission.objectives_file) - 1);
			strncpy(e->ext.mission.briefing_file,
				iniGet(ini, "briefing_file", ""),
				sizeof(e->ext.mission.briefing_file) - 1);
			strncpy(e->ext.mission.mission_graph_file,
				iniGet(ini, "mission_graph_file",
				iniGet(ini, "graph", "")),
				sizeof(e->ext.mission.mission_graph_file) - 1);
			const char *pf = e->ext.mission.mission_graph_file[0] ?
				e->ext.mission.mission_graph_file :
				(e->ext.mission.scenario_archive[0] ?
					e->ext.mission.scenario_archive :
				(e->ext.mission.objectives_file[0] ?
					e->ext.mission.objectives_file : e->ext.mission.briefing_file));
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	case ASSET_THEME:
		{
			strncpy(e->ext.theme.theme_file,
				iniGet(ini, "theme_file", iniGet(ini, "file_path", "")),
				sizeof(e->ext.theme.theme_file) - 1);
			strncpy(e->ext.theme.ui_archive,
				iniGet(ini, "ui_archive", ""),
				sizeof(e->ext.theme.ui_archive) - 1);
			strncpy(e->ext.theme.font_archive,
				iniGet(ini, "font_archive", ""),
				sizeof(e->ext.theme.font_archive) - 1);
			const char *pf = e->ext.theme.theme_file[0] ?
				e->ext.theme.theme_file :
				(e->ext.theme.ui_archive[0] ?
					e->ext.theme.ui_archive : e->ext.theme.font_archive);
			if (pf[0]) {
				catalogSetPrimaryFile(e, pf);
			}
		}
		break;

	default:
		/* ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC, ASSET_TOOL -- no extra fields needed */
		break;
	}

	registerDependencyList(e->id, iniGet(ini, "deps", ""), e->bundled);
	if (type == ASSET_PROJECTILE && e->ext.projectile.entity_ref[0]) {
		catalogDepRegister(e->id, e->ext.projectile.entity_ref, e->bundled);
	}
	if (type == ASSET_ANIMATION && e->ext.anim.target_body[0]) {
		catalogDepRegister(e->ext.anim.target_body, e->id, e->bundled);
	}

	return 1;
}

/* ========================================================================
 * Directory Scanning
 * ======================================================================== */

/**
 * Check if a path is a directory.
 */
static s32 isDirectory(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/**
 * Find and parse the .ini file in a component directory.
 * Looks for any file ending in .ini (there should be exactly one).
 */
static s32 findAndParseIni(const char *component_dir, ini_section_t *out)
{
	DIR *dp = opendir(component_dir);
	if (!dp) {
		return 0;
	}

	char inibuf[FS_MAXPATH];
	s32 found = 0;
	struct dirent *ent;

	while ((ent = readdir(dp)) != NULL) {
		const char *name = ent->d_name;
		s32 len = (s32)strlen(name);

		if (len > 4 && strcmp(name + len - 4, ".ini") == 0) {
			snprintf(inibuf, sizeof(inibuf), "%s/%s", component_dir, name);
			found = iniParse(inibuf, out);
			break;
		}
	}

	closedir(dp);
	return found;
}

static s32 isRegularFile(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static s32 registerComponentIniFile(const char *component_dir,
                                    const char *ini_path,
                                    asset_type_e expected,
                                    const char *label,
                                    const char *mod_id)
{
	ini_section_t ini;
	if (!iniParse(ini_path, &ini)) {
		sysLogPrintf(LOG_WARNING,
			"assetcatalog_scanner: invalid external-layout INI '%s'",
			ini_path);
		return 0;
	}

	asset_type_e ini_type = sectionToType(ini.type);
	if (expected != ASSET_NONE && ini_type != expected) {
		sysLogPrintf(LOG_WARNING,
			"assetcatalog_scanner: external-layout type mismatch in '%s': "
			"expected %d, got [%s]=%d",
			label ? label : ini_path, expected, ini.type, ini_type);
	}

	return registerComponent(&ini, component_dir, mod_id) ? 1 : 0;
}

static s32 scanExternalDescriptorChildren(const char *base_dir,
                                          const char *leaf,
                                          asset_type_e expected,
                                          const char *mod_id)
{
	if (!isDirectory(base_dir)) {
		return 0;
	}

	DIR *dp = opendir(base_dir);
	if (!dp) {
		return 0;
	}

	s32 count = 0;
	struct dirent *ent;
	char component_dir[FS_MAXPATH];
	char ini_path[FS_MAXPATH];
	char label[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		if (ent->d_name[0] == '.') {
			continue;
		}

		snprintf(component_dir, sizeof(component_dir), "%s/%s",
			base_dir, ent->d_name);
		if (!isDirectory(component_dir)) {
			continue;
		}

		snprintf(ini_path, sizeof(ini_path), "%s/%s", component_dir, leaf);
		if (!isRegularFile(ini_path)) {
			continue;
		}

		snprintf(label, sizeof(label), "%s/%s", ent->d_name, leaf);
		count += registerComponentIniFile(component_dir, ini_path, expected,
			label, mod_id);
	}

	closedir(dp);
	return count;
}

static s32 scanExternalDescriptorPath(const char *mod_dir,
                                      const char *relative_dir,
                                      const char *leaf,
                                      asset_type_e expected,
                                      const char *mod_id)
{
	char base_dir[FS_MAXPATH];
	snprintf(base_dir, sizeof(base_dir), "%s/%s", mod_dir, relative_dir);
	return scanExternalDescriptorChildren(base_dir, leaf, expected, mod_id);
}

static s32 registerTypedPdDescriptorFile(const char *descriptor_path,
                                         asset_type_e expected,
                                         const char *mod_id)
{
	ini_section_t ini;
	if (!iniParse(descriptor_path, &ini)) {
		const char *descriptor_leaf = typedPdArchiveDescriptorLeaf(descriptor_path);
		if (!descriptor_leaf) {
			return 0;
		}

		mod_archive_t *arc = modArchiveOpen(descriptor_path);
		if (!arc) {
			/* Legacy JSON .pd* files are still valid through their existing
			 * loaders; this scanner consumes readable INI descriptors and
			 * zip-openable typed asset archives. */
			return 0;
		}

		const char *found_descriptor = NULL;
		s32 idx = assetArchiveFindDescriptorEntry(arc, descriptor_path,
			ASSET_ARCHIVE_VALIDATE_MIGRATION, &found_descriptor);
		if (idx < 0) {
			modArchiveClose(arc);
			return 0;
		}

		u32 ini_size = 0;
		char *ini_bytes = (char *)modArchiveExtractAlloc(arc, idx, &ini_size);
		modArchiveClose(arc);
		if (!ini_bytes) {
			return 0;
		}

		s32 ok = iniParseBuffer(found_descriptor ? found_descriptor : descriptor_leaf,
			ini_bytes, ini_size, &ini);
		free(ini_bytes);
		if (!ok) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: invalid typed archive descriptor '%s' in '%s'",
				descriptor_leaf, descriptor_path);
			return 0;
		}

		qualifyTypedArchiveSourcePaths(&ini, descriptor_path);
	}

	asset_type_e ini_type = sectionToType(ini.type);
	if (expected != ASSET_NONE && ini_type != expected) {
		sysLogPrintf(LOG_WARNING,
			"assetcatalog_scanner: typed .pd* descriptor type mismatch in '%s': "
			"expected %d, got [%s]=%d",
			descriptor_path, expected, ini.type, ini_type);
	}

	char component_dir[FS_MAXPATH];
	typedPdDescriptorComponentDir(descriptor_path, component_dir, sizeof(component_dir));
	if (!isDirectory(component_dir)) {
		pathDirnameAnySeparator(descriptor_path, component_dir, sizeof(component_dir));
	}
	if (!component_dir[0]) {
		return 0;
	}

	return registerComponent(&ini, component_dir, mod_id) ? 1 : 0;
}

static s32 scanTypedPdDescriptorsRecurse(const char *root_dir,
                                         const char *rel_dir,
                                         const char *mod_id)
{
	char abs_dir[FS_MAXPATH];
	if (rel_dir && rel_dir[0]) {
		snprintf(abs_dir, sizeof(abs_dir), "%s/%s", root_dir, rel_dir);
	} else {
		snprintf(abs_dir, sizeof(abs_dir), "%s", root_dir);
	}
	abs_dir[sizeof(abs_dir) - 1] = '\0';

	DIR *dp = opendir(abs_dir);
	if (!dp) {
		return 0;
	}

	s32 count = 0;
	struct dirent *ent;
	while ((ent = readdir(dp)) != NULL) {
		if (!ent->d_name || ent->d_name[0] == '.') {
			continue;
		}

		char child_rel[FS_MAXPATH];
		if (rel_dir && rel_dir[0]) {
			snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_dir, ent->d_name);
		} else {
			snprintf(child_rel, sizeof(child_rel), "%s", ent->d_name);
		}
		child_rel[sizeof(child_rel) - 1] = '\0';

		char child_abs[FS_MAXPATH];
		snprintf(child_abs, sizeof(child_abs), "%s/%s", root_dir, child_rel);
		child_abs[sizeof(child_abs) - 1] = '\0';

		if (isDirectory(child_abs)) {
			count += scanTypedPdDescriptorsRecurse(root_dir, child_rel, mod_id);
			continue;
		}

		if (!isRegularFile(child_abs)) {
			continue;
		}

		asset_type_e expected = typedPdContentTypeForPath(child_abs);
		if (expected == ASSET_NONE) {
			continue;
		}

		count += registerTypedPdDescriptorFile(child_abs, expected, mod_id);
	}

	closedir(dp);
	return count;
}

/**
 * Scan a single category directory within a mod's _components/ folder.
 * e.g., mods/my_mod/_components/maps/
 *
 * @param category_dir  Full path to the category directory
 * @param category_name Directory name ("maps", "characters", etc.)
 * @param mod_id        Mod identifier
 * @return Number of components registered
 */
static s32 scanCategoryDir(const char *category_dir, const char *category_name,
                            const char *mod_id)
{
	asset_type_e expected = categoryToType(category_name);
	if (expected == ASSET_NONE) {
		sysLogPrintf(LOG_NOTE, "assetcatalog_scanner: skipping unknown category '%s'",
			category_name);
		return 0;
	}

	DIR *dp = opendir(category_dir);
	if (!dp) {
		return 0;
	}

	s32 count = 0;
	struct dirent *ent;
	char pathbuf[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		/* skip . and .. */
		if (ent->d_name[0] == '.') {
			continue;
		}

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", category_dir, ent->d_name);

		if (!isDirectory(pathbuf)) {
			continue;
		}

		/* Parse the .ini in this component directory */
		ini_section_t ini;
		if (!findAndParseIni(pathbuf, &ini)) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: no valid .ini in component '%s/%s'",
				category_name, ent->d_name);
			continue;
		}

		/* Verify the INI section type matches the category */
		asset_type_e ini_type = sectionToType(ini.type);
		if (ini_type != expected) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: type mismatch in '%s/%s': "
				"expected %d, got [%s]=%d",
				category_name, ent->d_name, expected, ini.type, ini_type);
			/* Register anyway -- the INI section type takes precedence */
		}

		if (registerComponent(&ini, pathbuf, mod_id)) {
			count++;
		}
	}

	closedir(dp);
	return count;
}

/**
 * Scan a single mod directory for _components/ subdirectory.
 *
 * @param mod_dir  Full path to the mod directory (e.g., mods/my_mod)
 * @param mod_id   Mod identifier (e.g., "my_mod")
 * @return Number of components registered
 */
static s32 scanModDir(const char *mod_dir, const char *mod_id)
{
	char components_dir[FS_MAXPATH];
	snprintf(components_dir, sizeof(components_dir), "%s/_components", mod_dir);

	if (!isDirectory(components_dir)) {
		return 0;  /* no _components/ directory -- legacy mod, skip */
	}

	DIR *dp = opendir(components_dir);
	if (!dp) {
		return 0;
	}

	s32 count = 0;
	struct dirent *ent;
	char pathbuf[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		if (ent->d_name[0] == '.') {
			continue;
		}

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", components_dir, ent->d_name);

		if (!isDirectory(pathbuf)) {
			continue;
		}

		s32 n = scanCategoryDir(pathbuf, ent->d_name, mod_id);
		if (n > 0) {
			sysLogPrintf(LOG_NOTE, "assetcatalog_scanner: %s/%s: %d components",
				mod_id, ent->d_name, n);
		}
		count += n;
	}

	closedir(dp);
	return count;
}

s32 assetCatalogScanExternalLayoutFolder(const char *mod_id, const char *mod_dir)
{
	if (!mod_id || !mod_id[0] || !mod_dir || !mod_dir[0]) {
		return 0;
	}

	s32 total = 0;

	total += scanExternalDescriptorPath(mod_dir, "weapons",
		"weapon.ini", ASSET_WEAPON, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "projectiles",
		"projectile.ini", ASSET_PROJECTILE, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "entities",
		"entity.ini", ASSET_ENTITY, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "materials",
		"material.ini", ASSET_MATERIAL, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "textures",
		"texture.ini", ASSET_TEXTURE, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "skins",
		"skin.ini", ASSET_SKIN, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "characters",
		"character.ini", ASSET_CHARACTER, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "characters/heads",
		"head.ini", ASSET_HEAD, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "characters/bodies",
		"body.ini", ASSET_BODY, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "maps",
		"arena.ini", ASSET_ARENA, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "maps",
		"map.ini", ASSET_MAP, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "scenarios",
		"scenario.ini", ASSET_SCENARIO, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "props",
		"prop.ini", ASSET_PROP, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "vehicles",
		"vehicle.ini", ASSET_VEHICLE, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "missions",
		"mission.ini", ASSET_MISSION, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "gamemodes",
		"gamemode.ini", ASSET_GAMEMODE, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "botprofiles",
		"botprofile.ini", ASSET_BOT_PROFILE, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "bot_profiles",
		"botprofile.ini", ASSET_BOT_PROFILE, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "effects",
		"effect.ini", ASSET_EFFECT, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "hud",
		"hud.ini", ASSET_HUD, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "themes",
		"theme.ini", ASSET_THEME, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "audio/sfx",
		"sound.ini", ASSET_AUDIO, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "audio/sfx",
		"sfx.ini", ASSET_AUDIO, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "audio/voice",
		"voice.ini", ASSET_AUDIO, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "audio/music",
		"music.ini", ASSET_AUDIO, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "ui",
		"ui.ini", ASSET_UI, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "ui",
		"texture.ini", ASSET_UI, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "fonts",
		"font.ini", ASSET_FONT, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "lang",
		"lang.ini", ASSET_LANG, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "animations",
		"animation.ini", ASSET_ANIMATION, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "animations/weapon",
		"animation.ini", ASSET_ANIMATION, mod_id);
	total += scanExternalDescriptorPath(mod_dir, "animations/character",
		"animation.ini", ASSET_ANIMATION, mod_id);
	total += scanTypedPdDescriptorsRecurse(mod_dir, "", mod_id);

	if (total > 0) {
		sysLogPrintf(LOG_NOTE,
			"assetcatalog_scanner: folder %s: %d external-layout/.pd* descriptors registered",
			mod_id, total);
	}
	return total;
}

#ifndef PD_SERVER
static s32 stringEndsWithNoCase(const char *s, const char *suffix)
{
	if (!s || !suffix) {
		return 0;
	}
	size_t n = strlen(s);
	size_t m = strlen(suffix);
	if (m > n) {
		return 0;
	}
	const char *tail = s + n - m;
	for (size_t i = 0; i < m; i++) {
		if (tolower((u8)tail[i]) != tolower((u8)suffix[i])) {
			return 0;
		}
	}
	return 1;
}

static const char *pathLeaf(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static void pathDirname(const char *path, char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return;
	}
	out[0] = '\0';
	if (!path) {
		return;
	}
	const char *slash = strrchr(path, '/');
	if (!slash) {
		return;
	}
	size_t len = (size_t)(slash - path);
	if (len >= outsz) {
		len = outsz - 1;
	}
	memcpy(out, path, len);
	out[len] = '\0';
}

static void pathSegment(const char *path, s32 segment_index, char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return;
	}
	out[0] = '\0';
	if (!path || segment_index < 0) {
		return;
	}

	const char *p = path;
	for (s32 i = 0; i < segment_index; i++) {
		const char *slash = strchr(p, '/');
		if (!slash) {
			return;
		}
		p = slash + 1;
	}

	const char *end = strchr(p, '/');
	size_t len = end ? (size_t)(end - p) : strlen(p);
	if (len >= outsz) {
		len = outsz - 1;
	}
	memcpy(out, p, len);
	out[len] = '\0';
}

static s32 archiveIniIsDescriptor(const char *entry_name)
{
	if (!stringEndsWithNoCase(entry_name, ".ini")) {
		return 0;
	}

	char seg0[64];
	pathSegment(entry_name, 0, seg0, sizeof(seg0));

	/* Legacy folder-mod component layout inside .pdmod. */
	if (strcmp(seg0, "_components") == 0) {
		return 1;
	}

	const char *leaf = pathLeaf(entry_name);
	return strcmp(leaf, "weapon.ini") == 0
		|| strcmp(leaf, "projectile.ini") == 0
		|| strcmp(leaf, "entity.ini") == 0
		|| strcmp(leaf, "material.ini") == 0
		|| strcmp(leaf, "character.ini") == 0
		|| strcmp(leaf, "head.ini") == 0
		|| strcmp(leaf, "body.ini") == 0
		|| strcmp(leaf, "arena.ini") == 0
		|| strcmp(leaf, "map.ini") == 0
		|| strcmp(leaf, "scenario.ini") == 0
		|| strcmp(leaf, "skin.ini") == 0
		|| strcmp(leaf, "effect.ini") == 0
		|| strcmp(leaf, "prop.ini") == 0
		|| strcmp(leaf, "vehicle.ini") == 0
		|| strcmp(leaf, "mission.ini") == 0
		|| strcmp(leaf, "gamemode.ini") == 0
		|| strcmp(leaf, "botprofile.ini") == 0
		|| strcmp(leaf, "sound.ini") == 0
		|| strcmp(leaf, "sfx.ini") == 0
		|| strcmp(leaf, "voice.ini") == 0
		|| strcmp(leaf, "music.ini") == 0
		|| strcmp(leaf, "audio.ini") == 0
		|| strcmp(leaf, "ui.ini") == 0
		|| strcmp(leaf, "texture.ini") == 0
		|| strcmp(leaf, "font.ini") == 0
		|| strcmp(leaf, "lang.ini") == 0
		|| strcmp(leaf, "animation.ini") == 0
		|| strcmp(leaf, "hud.ini") == 0
		|| strcmp(leaf, "theme.ini") == 0;
}

static s32 archiveEntryIsDescriptor(const char *entry_name)
{
	return archiveIniIsDescriptor(entry_name)
		|| typedPdContentTypeForPath(entry_name) != ASSET_NONE;
}

static asset_type_e archiveExpectedTypeForPath(const char *entry_name)
{
	char seg0[64];
	char seg1[64];
	const char *leaf = pathLeaf(entry_name);

	asset_type_e typed = typedPdContentTypeForPath(entry_name);
	if (typed != ASSET_NONE) {
		return typed;
	}

	pathSegment(entry_name, 0, seg0, sizeof(seg0));
	pathSegment(entry_name, 1, seg1, sizeof(seg1));

	if (strcmp(seg0, "_components") == 0) {
		char category[64];
		pathSegment(entry_name, 1, category, sizeof(category));
		return categoryToType(category);
	}

	if (strcmp(seg0, "weapons") == 0) return ASSET_WEAPON;
	if (strcmp(seg0, "projectiles") == 0) return ASSET_PROJECTILE;
	if (strcmp(seg0, "entities") == 0) return ASSET_ENTITY;
	if (strcmp(seg0, "materials") == 0) return ASSET_MATERIAL;
	if (strcmp(seg0, "textures") == 0) return ASSET_TEXTURE;
	if (strcmp(seg0, "skins") == 0) return ASSET_SKIN;
	if (strcmp(seg0, "animations") == 0) return ASSET_ANIMATION;
	if (strcmp(seg0, "audio") == 0) return ASSET_AUDIO;
	if (strcmp(seg0, "ui") == 0) return ASSET_UI;
	if (strcmp(seg0, "fonts") == 0) return ASSET_FONT;
	if (strcmp(seg0, "lang") == 0) return ASSET_LANG;
	if (strcmp(seg0, "scenarios") == 0) return ASSET_SCENARIO;
	if (strcmp(seg0, "props") == 0) return ASSET_PROP;
	if (strcmp(seg0, "vehicles") == 0) return ASSET_VEHICLE;
	if (strcmp(seg0, "missions") == 0) return ASSET_MISSION;
	if (strcmp(seg0, "gamemodes") == 0) return ASSET_GAMEMODE;
	if (strcmp(seg0, "botprofiles") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(seg0, "bot_profiles") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(seg0, "effects") == 0) return ASSET_EFFECT;
	if (strcmp(seg0, "hud") == 0) return ASSET_HUD;
	if (strcmp(seg0, "themes") == 0) return ASSET_THEME;

	if (strcmp(seg0, "characters") == 0) {
		if (strcmp(seg1, "heads") == 0) return ASSET_HEAD;
		if (strcmp(seg1, "bodies") == 0) return ASSET_BODY;
		return ASSET_CHARACTER;
	}

	if (strcmp(seg0, "maps") == 0) {
		if (strcmp(leaf, "arena.ini") == 0) return ASSET_ARENA;
		return ASSET_MAP;
	}

	return ASSET_NONE;
}

static s32 archivePathNeedsComponentPrefix(const char *value)
{
	if (!value || !value[0]) {
		return 0;
	}
	if (strstr(value, "::")) {
		return 0;
	}
	if (value[0] == '/' || value[0] == '\\') {
		return 0;
	}
	if (isalpha((u8)value[0]) && value[1] == ':') {
		return 0;
	}
	if (strchr(value, '/') || strchr(value, '\\')) {
		return 0;
	}
	return 1;
}

static void qualifyArchiveIniPath(ini_section_t *ini, const char *key,
                                  const char *component_dir)
{
	if (!ini || !key || !component_dir || !component_dir[0]) {
		return;
	}

	for (s32 i = 0; i < ini->count; i++) {
		if (strcmp(ini->pairs[i].key, key) != 0) {
			continue;
		}
		if (!archivePathNeedsComponentPrefix(ini->pairs[i].value)) {
			continue;
		}

		char full[sizeof(ini->pairs[i].value)];
		snprintf(full, sizeof(full), "%s/%s", component_dir, ini->pairs[i].value);
		strncpy(ini->pairs[i].value, full, sizeof(ini->pairs[i].value) - 1);
		ini->pairs[i].value[sizeof(ini->pairs[i].value) - 1] = '\0';
	}
}

static void qualifyArchiveIniPaths(ini_section_t *ini, const char *component_dir)
{
	static const char *keys[] = {
		"bodyfile",
		"headfile",
		"model_file",
		"model",
		"lo_model_file",
		"hand_model_file",
		"scene_file",
		"scene",
		"runtime_source_file",
		"geometry_file",
		"geometry",
		"blender_scene_file",
		"blender_scene",
		"visual_scene_file",
		"visual_scene",
		"visual_material_file",
		"visual_materials_file",
		"material_file",
		"material",
		"material_archive",
		"collision_file",
		"collision_source_file",
		"collision_source",
		"pads_file",
		"spawns_file",
		"volumes_file",
		"objects_file",
		"setup_fields_file",
		"setup_file",
		"rooms_file",
		"rooms",
		"props_file",
		"props",
		"objectives_file",
		"objectives",
		"navigation_file",
		"level_graph_file",
		"mission_graph_file",
		"visual_source_file",
		"music_file",
		"midi_file",
		"file_path",
		"effect_file",
		"behavior_graph",
		"graph",
		"theme_file",
		"rules_file",
		"profile_file",
		"scenario_archive",
		"ui_archive",
		"audio_archive",
		"animation_file",
		"texture_file",
		"texture_archive",
		"texture",
		"texture_manifest_file",
		"font_file",
		"glyphs_file",
		"font_archive",
		"font",
		"strings_file",
		"strings",
		"strings_tsv",
		NULL
	};

	for (s32 i = 0; keys[i]; i++) {
		qualifyArchiveIniPath(ini, keys[i], component_dir);
	}
}
#endif

/* ========================================================================
 * Public API
 * ======================================================================== */

s32 assetCatalogScanComponents(const char *modsdir)
{
	if (!modsdir || !modsdir[0]) {
		sysLogPrintf(LOG_ERROR, "assetcatalog_scanner: NULL or empty mods directory");
		return -1;
	}

	DIR *dp = opendir(modsdir);
	if (!dp) {
		sysLogPrintf(LOG_WARNING, "assetcatalog_scanner: cannot open mods directory '%s'",
			modsdir);
		return -1;
	}

	s32 total = 0;
	struct dirent *ent;
	char pathbuf[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		/* only scan mod_* directories */
		if (strncmp(ent->d_name, "mod_", 4) != 0) {
			continue;
		}

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", modsdir, ent->d_name);

		if (!isDirectory(pathbuf)) {
			continue;
		}

		s32 n = scanModDir(pathbuf, ent->d_name);
		if (n > 0) {
			sysLogPrintf(LOG_NOTE, "assetcatalog_scanner: %s: %d total components",
				ent->d_name, n);
		}
		total += n;
	}

	closedir(dp);
	sysLogPrintf(LOG_NOTE, "assetcatalog_scanner: scan complete, %d mod components registered",
		total);
	return total;
}

s32 assetCatalogScanComponentsFromArchive(const char *mod_id, mod_archive_t *archive)
{
#ifdef PD_SERVER
	(void)mod_id;
	(void)archive;
	return 0;
#else
	if (!mod_id || !mod_id[0] || !archive) {
		return 0;
	}

	s32 total = 0;
	s32 entries = modArchiveGetEntryCount(archive);

	for (s32 i = 0; i < entries; i++) {
		const char *entry_name = modArchiveGetEntryName(archive, i);
		if (!entry_name || !archiveEntryIsDescriptor(entry_name)) {
			continue;
		}

		char component_dir[FS_MAXPATH];
		if (typedPdContentTypeForPath(entry_name) != ASSET_NONE) {
			typedPdDescriptorComponentDir(entry_name, component_dir, sizeof(component_dir));
		} else {
			pathDirname(entry_name, component_dir, sizeof(component_dir));
		}
		if (!component_dir[0]) {
			continue;
		}

		u32 ini_size = 0;
		char *ini_bytes = (char *)modArchiveExtractAlloc(archive, i, &ini_size);
		if (!ini_bytes) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: could not read archive INI '%s' for '%s'",
				entry_name, mod_id);
			continue;
		}

		ini_section_t ini;
		s32 typed_archive_entry = typedPdContentTypeForPath(entry_name) != ASSET_NONE;
		s32 typed_archive_sources_qualified = 0;
		s32 parsed = iniParseBuffer(entry_name, ini_bytes, ini_size, &ini);
		if (!parsed && typed_archive_entry) {
			const char *descriptor_leaf = typedPdArchiveDescriptorLeaf(entry_name);
			if (descriptor_leaf) {
				u32 nested_size = 0;
				const char *found_descriptor = NULL;
				char *nested_ini = assetArchiveExtractDescriptorMemAlloc(
					ini_bytes, ini_size, entry_name,
					ASSET_ARCHIVE_VALIDATE_MIGRATION,
					&nested_size, &found_descriptor);
				if (nested_ini) {
					parsed = iniParseBuffer(found_descriptor ? found_descriptor : descriptor_leaf,
						nested_ini, nested_size, &ini);
					free(nested_ini);
					if (parsed) {
						qualifyTypedArchiveSourcePaths(&ini, entry_name);
						typed_archive_sources_qualified = 1;
					}
				}
			}
		}
		if (!parsed) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: invalid archive descriptor '%s' for '%s'",
				entry_name, mod_id);
			free(ini_bytes);
			continue;
		}
		free(ini_bytes);

		asset_type_e expected = archiveExpectedTypeForPath(entry_name);
		asset_type_e ini_type = sectionToType(ini.type);
		if (expected != ASSET_NONE && ini_type != expected) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: archive type mismatch in '%s': "
				"expected %d, got [%s]=%d",
				entry_name, expected, ini.type, ini_type);
		}

		if (!typed_archive_sources_qualified) {
			qualifyArchiveIniPaths(&ini, component_dir);
		}

		if (registerComponent(&ini, component_dir, mod_id)) {
			total++;
		}
	}

	if (total > 0) {
		sysLogPrintf(LOG_NOTE,
			"assetcatalog_scanner: archive %s: %d component descriptors registered",
			mod_id, total);
	}
	return total;
#endif
}

/* ========================================================================
 * D3R-8: Flat Bot Variant Scanner
 * ======================================================================== */

/**
 * Scan the flat bot_variants/ directory directly under modsdir.
 *
 * Unlike assetCatalogScanComponents(), bot variants created by the in-game
 * customizer are stored directly at:
 *   {modsdir}/bot_variants/{component_name}/bot.ini
 *
 * This function is called at startup after assetCatalogScanComponents() so
 * variants saved in a previous session are available immediately.
 * New variants saved this session are hot-registered via botVariantSave().
 *
 * @param modsdir  Path to the mods directory (e.g., "mods/")
 * @return Number of bot variants registered, or 0 if directory doesn't exist.
 */
s32 assetCatalogScanBotVariants(const char *modsdir)
{
	if (!modsdir || !modsdir[0]) {
		return 0;
	}

	char bot_variants_dir[FS_MAXPATH];
	snprintf(bot_variants_dir, sizeof(bot_variants_dir), "%s/bot_variants", modsdir);

	DIR *dp = opendir(bot_variants_dir);
	if (!dp) {
		/* Directory doesn't exist yet — created on first save, not an error */
		return 0;
	}

	s32 count = 0;
	struct dirent *ent;
	char pathbuf[FS_MAXPATH];

	while ((ent = readdir(dp)) != NULL) {
		if (ent->d_name[0] == '.') {
			continue;
		}

		snprintf(pathbuf, sizeof(pathbuf), "%s/%s", bot_variants_dir, ent->d_name);

		if (!isDirectory(pathbuf)) {
			continue;
		}

		ini_section_t ini;
		if (!findAndParseIni(pathbuf, &ini)) {
			sysLogPrintf(LOG_WARNING,
				"assetcatalog_scanner: no valid .ini in bot_variants/%s",
				ent->d_name);
			continue;
		}

		/* Use "custom" as the mod_id for user-created variants */
		if (registerComponent(&ini, pathbuf, "custom")) {
			count++;
		}
	}

	closedir(dp);

	if (count > 0) {
		sysLogPrintf(LOG_NOTE,
			"assetcatalog_scanner: bot_variants: %d variant(s) registered", count);
	}
	return count;
}
