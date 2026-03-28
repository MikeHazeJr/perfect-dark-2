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
 *   - One section per file: [map], [character], [textures], etc.
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
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
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

s32 iniParse(const char *filepath, ini_section_t *out)
{
	FILE *fp = fopen(filepath, "r");
	if (!fp) {
		return 0;
	}

	memset(out, 0, sizeof(*out));

	char line[512];
	s32 in_section = 0;

	while (fgets(line, sizeof(line), fp)) {
		char *p = trimWhitespace(line);

		/* skip blank lines and comments */
		if (*p == '\0' || *p == '#' || *p == ';') {
			continue;
		}

		/* section header: [type] */
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) {
				*end = '\0';
				strncpy(out->type, p + 1, sizeof(out->type) - 1);
				in_section = 1;
			}
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
	}

	fclose(fp);
	return (out->type[0] != '\0') ? 1 : 0;
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
	if (strcmp(dirname, "textures") == 0)    return ASSET_TEXTURES;
	if (strcmp(dirname, "sfx") == 0)         return ASSET_SFX;
	if (strcmp(dirname, "music") == 0)       return ASSET_MUSIC;
	if (strcmp(dirname, "props") == 0)       return ASSET_PROP;
	if (strcmp(dirname, "vehicles") == 0)    return ASSET_VEHICLE;
	if (strcmp(dirname, "missions") == 0)    return ASSET_MISSION;
	if (strcmp(dirname, "ui") == 0)          return ASSET_UI;
	if (strcmp(dirname, "tools") == 0)       return ASSET_TOOL;
	if (strcmp(dirname, "animations") == 0)  return ASSET_ANIMATION;
	if (strcmp(dirname, "hud") == 0)         return ASSET_HUD;
	if (strcmp(dirname, "gamemodes") == 0)   return ASSET_GAMEMODE;
	if (strcmp(dirname, "audio") == 0)       return ASSET_AUDIO;
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
	if (strcmp(section, "textures") == 0)     return ASSET_TEXTURES;
	if (strcmp(section, "sfx") == 0)          return ASSET_SFX;
	if (strcmp(section, "music") == 0)        return ASSET_MUSIC;
	if (strcmp(section, "prop") == 0)         return ASSET_PROP;
	if (strcmp(section, "vehicle") == 0)      return ASSET_VEHICLE;
	if (strcmp(section, "mission") == 0)      return ASSET_MISSION;
	if (strcmp(section, "ui") == 0)           return ASSET_UI;
	if (strcmp(section, "tool") == 0)         return ASSET_TOOL;
	if (strcmp(section, "animation") == 0)    return ASSET_ANIMATION;
	if (strcmp(section, "hud") == 0)          return ASSET_HUD;
	if (strcmp(section, "gamemode") == 0)     return ASSET_GAMEMODE;
	if (strcmp(section, "audio") == 0)        return ASSET_AUDIO;
	if (strcmp(section, "texture") == 0)      return ASSET_TEXTURE;
	return ASSET_NONE;
}

/* ========================================================================
 * Component Registration
 * ======================================================================== */

/**
 * Register a single component from a parsed INI section.
 *
 * @param ini       Parsed INI section
 * @param dirpath   Absolute path to the component directory
 * @param mod_id    Mod identifier (e.g., "mod_gex")
 * @return 1 on success, 0 on failure
 */
static s32 registerComponent(const ini_section_t *ini, const char *dirpath,
                              const char *mod_id)
{
	asset_type_e type = sectionToType(ini->type);
	if (type == ASSET_NONE) {
		sysLogPrintf(LOG_WARNING, "assetcatalog_scanner: unknown section type '%s' in %s",
			ini->type, dirpath);
		return 0;
	}

	/* Build the asset ID.
	 * Convention: {mod_category}_{folder_name}
	 * The INI "name" field is display-only; the folder name is the ID. */
	const char *category = iniGet(ini, "category", mod_id);

	/* Extract folder name from dirpath (last path component) */
	const char *folder = strrchr(dirpath, '/');
	folder = folder ? folder + 1 : dirpath;

	/* Build ID: use category + folder name, or just folder if category matches */
	char idbuf[CATALOG_ID_LEN];
	snprintf(idbuf, sizeof(idbuf), "%s", folder);

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
		e->ext.map.mode = 0; /* TODO: parse mode string */
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
		}
		break;

	case ASSET_SKIN:
		{
			const char *target = iniGet(ini, "target", "");
			strncpy(e->ext.skin.target_id, target, CATALOG_ID_LEN - 1);
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

	case ASSET_WEAPON:
		e->ext.weapon.weapon_id = iniGetInt(ini, "weapon_id", -1);
		strncpy(e->ext.weapon.name, iniGet(ini, "name", ""), sizeof(e->ext.weapon.name) - 1);
		strncpy(e->ext.weapon.model_file, iniGet(ini, "model_file", ""), sizeof(e->ext.weapon.model_file) - 1);
		e->ext.weapon.damage = iniGetFloat(ini, "damage", 0.0f);
		e->ext.weapon.fire_rate = iniGetFloat(ini, "fire_rate", 0.0f);
		e->ext.weapon.ammo_type = iniGetInt(ini, "ammo_type", 0);
		e->ext.weapon.dual_wieldable = iniGetInt(ini, "dual_wieldable", 0);
		break;

	case ASSET_PROP:
		e->ext.prop.prop_type = iniGetInt(ini, "prop_type", 0);
		strncpy(e->ext.prop.name, iniGet(ini, "name", ""), sizeof(e->ext.prop.name) - 1);
		strncpy(e->ext.prop.model_file, iniGet(ini, "model_file", ""), sizeof(e->ext.prop.model_file) - 1);
		e->ext.prop.flags = (u32)iniGetInt(ini, "flags", 0);
		e->ext.prop.health = iniGetFloat(ini, "health", 100.0f);
		break;

	case ASSET_ANIMATION:
		e->ext.anim.anim_id = iniGetInt(ini, "anim_id", -1);
		strncpy(e->ext.anim.name, iniGet(ini, "name", ""), sizeof(e->ext.anim.name) - 1);
		e->ext.anim.frame_count = iniGetInt(ini, "frame_count", 0);
		strncpy(e->ext.anim.target_body, iniGet(ini, "target_body", ""), sizeof(e->ext.anim.target_body) - 1);
		break;

	case ASSET_TEXTURE:
		e->ext.texture.texture_id = iniGetInt(ini, "texture_id", -1);
		e->ext.texture.width = iniGetInt(ini, "width", 0);
		e->ext.texture.height = iniGetInt(ini, "height", 0);
		e->ext.texture.format = iniGetInt(ini, "format", 0);
		strncpy(e->ext.texture.file_path, iniGet(ini, "file_path", ""), sizeof(e->ext.texture.file_path) - 1);
		break;

	case ASSET_GAMEMODE:
		e->ext.gamemode.mode_id = iniGetInt(ini, "mode_id", -1);
		strncpy(e->ext.gamemode.name, iniGet(ini, "name", ""), sizeof(e->ext.gamemode.name) - 1);
		strncpy(e->ext.gamemode.description, iniGet(ini, "description", ""), sizeof(e->ext.gamemode.description) - 1);
		e->ext.gamemode.min_players = iniGetInt(ini, "min_players", 2);
		e->ext.gamemode.max_players = iniGetInt(ini, "max_players", 4);
		e->ext.gamemode.team_based = iniGetInt(ini, "team_based", 0);
		break;

	case ASSET_AUDIO:
		e->ext.audio.sound_id = iniGetInt(ini, "sound_id", -1);
		strncpy(e->ext.audio.name, iniGet(ini, "name", ""), sizeof(e->ext.audio.name) - 1);
		e->ext.audio.category = iniGetInt(ini, "category", AUDIO_CAT_SFX);
		e->ext.audio.duration_ms = iniGetInt(ini, "duration_ms", 0);
		strncpy(e->ext.audio.file_path, iniGet(ini, "file_path", ""), sizeof(e->ext.audio.file_path) - 1);
		break;

	case ASSET_HUD:
		e->ext.hud.hud_id = iniGetInt(ini, "hud_id", -1);
		strncpy(e->ext.hud.name, iniGet(ini, "name", ""), sizeof(e->ext.hud.name) - 1);
		e->ext.hud.element_type = iniGetInt(ini, "element_type", HUD_ELEM_CROSSHAIR);
		strncpy(e->ext.hud.texture_file, iniGet(ini, "texture_file", ""), sizeof(e->ext.hud.texture_file) - 1);
		break;

	default:
		/* ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC, ASSET_UI, ASSET_HUD,
		 * ASSET_VEHICLE, ASSET_MISSION, ASSET_TOOL -- no extra fields needed */
		break;
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

/**
 * Scan a single category directory within a mod's _components/ folder.
 * e.g., mods/mod_gex/_components/maps/
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
 * @param mod_dir  Full path to the mod directory (e.g., mods/mod_gex)
 * @param mod_id   Mod identifier (e.g., "mod_gex")
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
