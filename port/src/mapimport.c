/**
 * mapimport.c -- Source Map Import Pipeline
 *
 * Stages editable map source layouts and typed .pdarena/.pdscenario content
 * units. Native BG/setup/pad dumps are rejected because public mod content must
 * remain source-openable and feed the runtime through catalog/provider loading.
 *
 * Pipeline: PARSE -> NORMALIZE -> GENERATE -> EMIT -> VALIDATE -> REGISTER
 *
 * Design ref: context/designs/mod-map-import-pipeline-2026-04-13.md
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <PR/ultratypes.h>
#include "types.h"
#include "system.h"
#include "fs.h"
#include "modmgr.h"
#include "mapimport.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_load.h"

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

static void ctxError(import_context_t *ctx, mapimport_result_e code,
                     const char *fmt, ...)
{
	va_list ap;
	ctx->result = code;
	va_start(ap, fmt);
	vsnprintf(ctx->error, MAPIMPORT_ERROR_LEN, fmt, ap);
	va_end(ap);
	sysLogPrintf(LOG_WARNING, "MAPIMPORT: %s", ctx->error);
}

/**
 * Sanitize a map name to a safe directory/ID string.
 * Lowercase, alphanumeric + underscores only.
 */
static void sanitizeName(const char *input, char *output, s32 maxlen)
{
	s32 i = 0;
	s32 o = 0;

	while (input[i] && o < maxlen - 1) {
		char c = input[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
			output[o++] = c;
		} else if (c >= 'A' && c <= 'Z') {
			output[o++] = c - 'A' + 'a';
		} else if (c == ' ' || c == '-' || c == '.') {
			output[o++] = '_';
		}
		/* Skip other characters */
		i++;
	}

	if (o == 0) {
		/* Fallback if name was entirely invalid */
		strncpy(output, "unnamed_map", maxlen - 1);
		o = (s32)strlen(output);
	}

	output[o] = '\0';
}

/* Case-insensitive suffix/equality helpers for source member names. */
static s32 strEndsWithNoCase(const char *s, const char *suffix)
{
	size_t slen;
	size_t tlen;
	size_t i;

	if (!s || !suffix) return 0;
	slen = strlen(s);
	tlen = strlen(suffix);
	if (tlen > slen) return 0;

	for (i = 0; i < tlen; i++) {
		unsigned char a = (unsigned char)s[slen - tlen + i];
		unsigned char b = (unsigned char)suffix[i];
		if (tolower(a) != tolower(b)) return 0;
	}

	return 1;
}

static s32 strEqualsNoCase(const char *a, const char *b)
{
	if (!a || !b) return 0;
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static s32 isForbiddenNativePayload(const char *name)
{
	if (strEndsWithNoCase(name, ".bin")) return 1;
	if (strEndsWithNoCase(name, ".bg")) return 1;
	if (strEndsWithNoCase(name, ".pad")) return 1;
	if (strEndsWithNoCase(name, ".pads")) return 1;
	if (strEndsWithNoCase(name, ".setup")) return 1;
	if (strEndsWithNoCase(name, ".set")) return 1;
	return 0;
}

static s32 isSourceGeometryFile(const char *name)
{
	if (strEqualsNoCase(name, "scene.glb")) return 1;
	if (strEqualsNoCase(name, "scene.gltf")) return 1;
	if (strEqualsNoCase(name, "geometry.obj")) return 1;
	if (strEqualsNoCase(name, "collision.obj")) return 1;
	if (strEndsWithNoCase(name, ".pdarena")) return 1;
	if (strEndsWithNoCase(name, ".pdscenario")) return 1;
	return 0;
}

static s32 isEditablePadSource(const char *name)
{
	if (strEqualsNoCase(name, "pads.ini")) return 1;
	if (strEqualsNoCase(name, "pads.json")) return 1;
	if (strEqualsNoCase(name, "spawns.json")) return 1;
	return 0;
}

static s32 isEditableSetupSource(const char *name)
{
	if (strEqualsNoCase(name, "setup.ini")) return 1;
	if (strEqualsNoCase(name, "objects.json")) return 1;
	if (strEqualsNoCase(name, "setup.fields.json")) return 1;
	if (strEqualsNoCase(name, "level.graph.json")) return 1;
	return 0;
}

/**
 * Copy a file from src to dst. Returns 0 on success, -1 on failure.
 */
static s32 copyFile(const char *src, const char *dst)
{
	FILE *fin = fopen(src, "rb");
	if (!fin) {
		sysLogPrintf(LOG_WARNING, "MAPIMPORT: copyFile: cannot open '%s': %s",
		             src, strerror(errno));
		return -1;
	}

	FILE *fout = fopen(dst, "wb");
	if (!fout) {
		fclose(fin);
		sysLogPrintf(LOG_WARNING, "MAPIMPORT: copyFile: cannot create '%s': %s",
		             dst, strerror(errno));
		return -1;
	}

	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fin)) > 0) {
		if (fwrite(buf, 1, n, fout) != n) {
			fclose(fin);
			fclose(fout);
			return -1;
		}
	}

	fclose(fin);
	fclose(fout);
	return 0;
}

/**
 * Create a directory, handling the case where it already exists.
 * Returns 0 on success (or already exists), -1 on failure.
 */
static s32 mkdirSafe(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
		return 0; /* Already exists */
	}
#ifdef _WIN32
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}

/**
 * Recursively remove a directory and its contents.
 * Used to clean up temp directories on failure.
 */
static void removeDirRecursive(const char *path)
{
	DIR *dir = opendir(path);
	if (!dir) return;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}

		char fullpath[FS_MAXPATH];
		snprintf(fullpath, sizeof(fullpath), "%s/%s", path, ent->d_name);

		struct stat st;
		if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
			removeDirRecursive(fullpath);
		} else {
			remove(fullpath);
		}
	}

	closedir(dir);
	rmdir(path);
}

static s32 copyDirRecursive(const char *src, const char *dst)
{
	DIR *dir = opendir(src);
	struct dirent *ent;

	if (!dir) {
		sysLogPrintf(LOG_WARNING,
		             "MAPIMPORT: copyDirRecursive: cannot open '%s': %s",
		             src, strerror(errno));
		return -1;
	}

	if (mkdirSafe(dst) != 0) {
		closedir(dir);
		sysLogPrintf(LOG_WARNING,
		             "MAPIMPORT: copyDirRecursive: cannot create '%s': %s",
		             dst, strerror(errno));
		return -1;
	}

	while ((ent = readdir(dir)) != NULL) {
		char srcfull[FS_MAXPATH];
		char dstfull[FS_MAXPATH];
		struct stat st;

		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}

		snprintf(srcfull, sizeof(srcfull), "%s/%s", src, ent->d_name);
		snprintf(dstfull, sizeof(dstfull), "%s/%s", dst, ent->d_name);

		if (stat(srcfull, &st) != 0) {
			closedir(dir);
			return -1;
		}

		if (S_ISDIR(st.st_mode)) {
			if (copyDirRecursive(srcfull, dstfull) != 0) {
				closedir(dir);
				return -1;
			}
		} else if (copyFile(srcfull, dstfull) != 0) {
			closedir(dir);
			return -1;
		}
	}

	closedir(dir);
	return 0;
}

/**
 * Get the resolved mods directory path. Checks CWD-relative first.
 */
static const char *getModsDir(void)
{
	const char *dir = modmgrGetModsDir();
	if (dir && dir[0]) return dir;
	return "./" MODMGR_MODS_DIR;
}

static const char *pathLeaf(const char *path)
{
	const char *slash;
	const char *bslash;

	if (!path) return "";
	slash = strrchr(path, '/');
	bslash = strrchr(path, '\\');
	if (bslash && (!slash || bslash > slash)) {
		slash = bslash;
	}
	return slash ? slash + 1 : path;
}

static s32 generateScenarioIni(import_context_t *ctx, const char *outdir)
{
	char path[FS_MAXPATH];
	const char *scene_leaf = pathLeaf(ctx->source_geometry_path);
	FILE *f;

	snprintf(path, sizeof(path), "%s/scenario.ini", outdir);
	f = fopen(path, "w");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "MAPIMPORT: cannot create scenario.ini: %s",
		             strerror(errno));
		return -1;
	}

	fprintf(f,
		"[scenario]\n"
		"catalog_id = imported:%s_scenario\n"
		"name = %s\n"
		"mode = mp\n"
		"\n"
		"[source]\n"
		"scene_file = %s\n"
		"runtime_source_file = %s\n"
		"collision_fallback = generated\n",
		ctx->map_name,
		ctx->map_name,
		scene_leaf,
		scene_leaf);

	fclose(f);
	return 0;
}

static s32 generateArenaIni(import_context_t *ctx, const char *outdir)
{
	char path[FS_MAXPATH];
	const char *geometry_leaf = pathLeaf(ctx->source_geometry_path);
	FILE *f;

	snprintf(path, sizeof(path), "%s/arena.ini", outdir);
	f = fopen(path, "w");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "MAPIMPORT: cannot create arena.ini: %s",
		             strerror(errno));
		return -1;
	}

	fprintf(f,
		"[arena]\n"
		"catalog_id = imported:%s\n"
		"\n"
		"[geometry]\n"
		"geometry_file = %s\n",
		ctx->map_name,
		geometry_leaf);

	fclose(f);
	return 0;
}

/* ========================================================================
 * Stage 1: PARSE -- Scan source directory
 * ======================================================================== */

static mapimport_result_e scanSourceTree(import_context_t *ctx,
                                         const char *dirpath,
                                         const char *relpath,
                                         s32 depth)
{
	DIR *dir;
	struct dirent *ent;

	if (depth > 16) {
		ctxError(ctx, MAPIMPORT_ERR_VALIDATE_FAILED,
		         "Source layout is nested too deeply: %s", dirpath);
		return ctx->result;
	}

	dir = opendir(dirpath);
	if (!dir) {
		ctxError(ctx, MAPIMPORT_ERR_NO_SOURCE_DIR,
		         "Cannot open source directory: %s", dirpath);
		return ctx->result;
	}

	while ((ent = readdir(dir)) != NULL) {
		char fullpath[FS_MAXPATH];
		char childrel[FS_MAXPATH];
		struct stat st;
		const char *leaf = ent->d_name;

		if (strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
			continue;
		}
		if (leaf[0] == '.' && strcmp(leaf, ".") != 0) {
			continue;
		}

		snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, leaf);
		if (relpath && relpath[0]) {
			snprintf(childrel, sizeof(childrel), "%s/%s", relpath, leaf);
		} else {
			snprintf(childrel, sizeof(childrel), "%s", leaf);
		}

		if (stat(fullpath, &st) != 0) {
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			if (strcmp(leaf, "maps") == 0 || strcmp(leaf, "scenarios") == 0
					|| strcmp(leaf, "arenas") == 0) {
				ctx->source_is_mod_layout = 1;
			}
			if (scanSourceTree(ctx, fullpath, childrel, depth + 1) != MAPIMPORT_OK) {
				closedir(dir);
				return ctx->result;
			}
			continue;
		}

		if (isForbiddenNativePayload(leaf)) {
			strncpy(ctx->first_forbidden_path, childrel, FS_MAXPATH - 1);
			ctx->first_forbidden_path[FS_MAXPATH - 1] = '\0';
			closedir(dir);
			ctxError(ctx, MAPIMPORT_ERR_FORBIDDEN_NATIVE_PAYLOAD,
			         "Map Import accepts editable source, not native payloads: %s",
			         ctx->first_forbidden_path);
			return ctx->result;
		}

		if (strEqualsNoCase(leaf, "mod.json")) {
			ctx->has_modjson = 1;
			ctx->source_is_mod_layout = 1;
			strncpy(ctx->modjson_path, fullpath, FS_MAXPATH - 1);
			ctx->modjson_path[FS_MAXPATH - 1] = '\0';
			sysLogPrintf(LOG_NOTE, "MAPIMPORT:   mod.json: %s", childrel);
		}

		if (strEqualsNoCase(leaf, "arena.ini")) {
			ctx->source_is_arena_layout = 1;
			sysLogPrintf(LOG_NOTE, "MAPIMPORT:   arena source: %s", childrel);
		}

		if (strEqualsNoCase(leaf, "scenario.ini")) {
			ctx->source_is_scenario_layout = 1;
			sysLogPrintf(LOG_NOTE, "MAPIMPORT:   scenario source: %s", childrel);
		}

		if (isSourceGeometryFile(leaf)) {
			ctx->has_source_geometry = 1;
			if (ctx->source_geometry_path[0] == '\0') {
				strncpy(ctx->source_geometry_path, fullpath, FS_MAXPATH - 1);
				ctx->source_geometry_path[FS_MAXPATH - 1] = '\0';
			}
			sysLogPrintf(LOG_NOTE, "MAPIMPORT:   source geometry: %s", childrel);
		}

		if (isEditablePadSource(leaf)) {
			ctx->has_pads = 1;
			if (ctx->pad_path[0] == '\0') {
				strncpy(ctx->pad_path, fullpath, FS_MAXPATH - 1);
				ctx->pad_path[FS_MAXPATH - 1] = '\0';
			}
		}

		if (isEditableSetupSource(leaf)) {
			ctx->has_setup = 1;
			if (ctx->setup_path[0] == '\0') {
				strncpy(ctx->setup_path, fullpath, FS_MAXPATH - 1);
				ctx->setup_path[FS_MAXPATH - 1] = '\0';
			}
		}
	}

	closedir(dir);
	return MAPIMPORT_OK;
}

static mapimport_result_e stageParse(const char *source_dir,
                                     import_context_t *ctx)
{
	sysLogPrintf(LOG_NOTE, "MAPIMPORT: PARSE -- scanning '%s'", source_dir);

	strncpy(ctx->source_dir, source_dir, FS_MAXPATH - 1);
	ctx->source_dir[FS_MAXPATH - 1] = '\0';

	if (scanSourceTree(ctx, source_dir, "", 0) != MAPIMPORT_OK) {
		return ctx->result;
	}

	if (!ctx->has_source_geometry) {
		ctxError(ctx, MAPIMPORT_ERR_NO_SOURCE_GEOMETRY,
		         "No editable map source found in '%s'. Expected scene.glb, scene.gltf, geometry.obj, .pdarena, or .pdscenario.",
		         source_dir);
		return ctx->result;
	}

	if (ctx->source_is_scenario_layout && !ctx->source_is_mod_layout) {
		ctx->source_is_arena_layout = 0;
	}

	ctx->parse_ok = 1;

	sysLogPrintf(LOG_NOTE,
	             "MAPIMPORT: PARSE complete -- source=%d pads=%d setup=%d modjson=%d layout=%d scenario=%d arena=%d",
	             ctx->has_source_geometry, ctx->has_pads, ctx->has_setup,
	             ctx->has_modjson, ctx->source_is_mod_layout,
	             ctx->source_is_scenario_layout, ctx->source_is_arena_layout);

	return MAPIMPORT_OK;
}

/* ========================================================================
 * Stage 2: NORMALIZE -- Validate and sanitize
 * ======================================================================== */

static mapimport_result_e stageNormalize(import_context_t *ctx)
{
	sysLogPrintf(LOG_NOTE, "MAPIMPORT: NORMALIZE -- validating content");

	/* Legacy counters may be supplied by future source parsers. */
	if (ctx->num_rooms > MAPIMPORT_MAX_ROOMS) {
		sysLogPrintf(LOG_WARNING,
		             "MAPIMPORT: room count %d exceeds max %d -- clamping",
		             ctx->num_rooms, MAPIMPORT_MAX_ROOMS);
		/* Not fatal: PD can handle large maps, just warn */
	}

	if (ctx->num_pads > MAPIMPORT_MAX_PADS) {
		sysLogPrintf(LOG_WARNING,
		             "MAPIMPORT: pad count %d exceeds max %d -- clamping",
		             ctx->num_pads, MAPIMPORT_MAX_PADS);
	}

	/* If no editable pads, runtime spawn generation must derive from source geometry. */
	if (!ctx->has_pads || ctx->num_pads == 0) {
		sysLogPrintf(LOG_NOTE,
		             "MAPIMPORT: no pad data -- spawns will be generated from geometry");
		ctx->has_spawns = 0;
	}

	/* If no editable setup source exists, runtime uses generated defaults. */
	if (!ctx->has_setup) {
		ctx->has_spawns = 0;
		ctx->has_waypoints = 0;
		sysLogPrintf(LOG_NOTE,
		             "MAPIMPORT: no setup source -- runtime will use generated defaults");
	}

	ctx->normalize_ok = 1;

	sysLogPrintf(LOG_NOTE, "MAPIMPORT: NORMALIZE complete");
	return MAPIMPORT_OK;
}

/* ========================================================================
 * Stage 3: GENERATE -- Fill missing metadata
 * ======================================================================== */

/**
 * Generate a mod.json manifest for the imported map.
 * Written to the output directory (not source).
 */
static s32 generateModJson(import_context_t *ctx, const char *outdir)
{
	char path[FS_MAXPATH];
	snprintf(path, sizeof(path), "%s/mod.json", outdir);

	FILE *f = fopen(path, "w");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "MAPIMPORT: cannot create mod.json: %s",
		             strerror(errno));
		return -1;
	}

	fprintf(f,
		"{\n"
		"  \"id\": \"imported:%s\",\n"
		"  \"name\": \"%s (Imported)\",\n"
		"  \"version\": \"1.0.0\",\n"
		"  \"author\": \"Imported\",\n"
		"  \"description\": \"Imported map from %s\",\n"
		"  \"base_fallback\": \"base:area52\",\n"
		"  \"content\": {\n"
		"    \"arenas\": [{\n"
		"      \"id\": \"imported:%s\",\n"
		"      \"name\": \"%s\",\n"
		"      \"mode\": 1\n"
		"    }]\n"
		"  }\n"
		"}\n",
		ctx->map_name,
		ctx->map_name,
		ctx->source_dir,
		ctx->map_name,
		ctx->map_name);

	fclose(f);
	ctx->generated_modjson = 1;

	sysLogPrintf(LOG_NOTE, "MAPIMPORT: generated mod.json for '%s'",
	             ctx->map_name);
	return 0;
}

/**
 * Write an import_metadata.json diagnostic file.
 */
static void writeImportMetadata(import_context_t *ctx, const char *outdir)
{
	char path[FS_MAXPATH];
	snprintf(path, sizeof(path), "%s/import_metadata.json", outdir);

	FILE *f = fopen(path, "w");
	if (!f) return;

	fprintf(f,
		"{\n"
		"  \"source_dir\": \"%s\",\n"
		"  \"map_name\": \"%s\",\n"
		"  \"has_source_geometry\": %d,\n"
		"  \"has_editable_pads\": %d,\n"
		"  \"has_editable_setup\": %d,\n"
		"  \"has_modjson\": %d,\n"
		"  \"source_is_mod_layout\": %d,\n"
		"  \"source_is_scenario_layout\": %d,\n"
		"  \"source_is_arena_layout\": %d,\n"
		"  \"num_rooms\": %d,\n"
		"  \"num_pads\": %d,\n"
		"  \"generated_spawns\": %d,\n"
		"  \"num_generated_spawns\": %d,\n"
		"  \"generated_modjson\": %d,\n"
		"  \"generated_setup\": %d,\n"
		"  \"result\": %d,\n"
		"  \"error\": \"%s\"\n"
		"}\n",
		ctx->source_dir,
		ctx->map_name,
		ctx->has_source_geometry,
		ctx->has_pads,
		ctx->has_setup,
		ctx->has_modjson,
		ctx->source_is_mod_layout,
		ctx->source_is_scenario_layout,
		ctx->source_is_arena_layout,
		ctx->num_rooms,
		ctx->num_pads,
		ctx->generated_spawns,
		ctx->num_generated_spawns,
		ctx->generated_modjson,
		ctx->generated_setup,
		(s32)ctx->result,
		ctx->error);

	fclose(f);
}

static mapimport_result_e stageGenerate(import_context_t *ctx)
{
	sysLogPrintf(LOG_NOTE, "MAPIMPORT: GENERATE -- filling missing metadata");

	/* Spawns will be generated at runtime by the L1-L4 spawn pool.
	 * Maps without INTROCMD_SPAWN entries rely on L2-L4 layers.
	 * We just note that spawns are absent so the import metadata is accurate. */
	if (!ctx->has_spawns) {
		ctx->generated_spawns = 1;
		sysLogPrintf(LOG_NOTE,
		             "MAPIMPORT: no declared spawns -- runtime L2-L4 will provide");
	}

	ctx->generate_ok = 1;

	sysLogPrintf(LOG_NOTE, "MAPIMPORT: GENERATE complete");
	return MAPIMPORT_OK;
}

/* ========================================================================
 * Stage 4: EMIT -- Atomic directory write
 * ======================================================================== */

static mapimport_result_e stageEmit(import_context_t *ctx)
{
	const char *modsdir = getModsDir();
	char tempdir[FS_MAXPATH];
	char finaldir[FS_MAXPATH];

	sysLogPrintf(LOG_NOTE, "MAPIMPORT: EMIT -- writing to mods directory");

	/* Build output paths */
	snprintf(finaldir, sizeof(finaldir), "%s/%s%s",
	         modsdir, MAPIMPORT_IMPORT_PREFIX, ctx->map_name);
	snprintf(tempdir, sizeof(tempdir), "%s/.importing_%s",
	         modsdir, ctx->map_name);

	strncpy(ctx->output_dir, finaldir, FS_MAXPATH - 1);
	ctx->output_dir[FS_MAXPATH - 1] = '\0';

	/* Clean up any stale temp directory */
	removeDirRecursive(tempdir);

	/* Create temp directory */
	if (mkdirSafe(tempdir) != 0) {
		ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
		         "Cannot create temp directory: %s (%s)",
		         tempdir, strerror(errno));
		return ctx->result;
	}

	/* Stage source content without flattening it into native engine payloads. */
	if (ctx->source_is_mod_layout) {
		if (copyDirRecursive(ctx->source_dir, tempdir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to copy source layout to output");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
	} else if (strEndsWithNoCase(ctx->source_geometry_path, ".pdarena")) {
		char arenadir[FS_MAXPATH];
		char dstpath[FS_MAXPATH];
		snprintf(arenadir, sizeof(arenadir), "%s/arenas", tempdir);
		if (mkdirSafe(arenadir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Cannot create arenas directory");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
		snprintf(dstpath, sizeof(dstpath), "%s/%s", arenadir,
		         pathLeaf(ctx->source_geometry_path));
		if (copyFile(ctx->source_geometry_path, dstpath) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to copy .pdarena source archive");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
	} else if (strEndsWithNoCase(ctx->source_geometry_path, ".pdscenario")) {
		char scenariodir[FS_MAXPATH];
		char dstpath[FS_MAXPATH];
		snprintf(scenariodir, sizeof(scenariodir), "%s/scenarios", tempdir);
		if (mkdirSafe(scenariodir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Cannot create scenarios directory");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
		snprintf(dstpath, sizeof(dstpath), "%s/%s", scenariodir,
		         pathLeaf(ctx->source_geometry_path));
		if (copyFile(ctx->source_geometry_path, dstpath) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to copy .pdscenario source archive");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
	} else if (ctx->source_is_scenario_layout
			|| strEndsWithNoCase(ctx->source_geometry_path, ".glb")
			|| strEndsWithNoCase(ctx->source_geometry_path, ".gltf")) {
		char parentdir[FS_MAXPATH];
		char scenariodir[FS_MAXPATH];
		snprintf(parentdir, sizeof(parentdir), "%s/scenarios", tempdir);
		if (mkdirSafe(parentdir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Cannot create scenarios directory");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
		snprintf(scenariodir, sizeof(scenariodir), "%s/scenarios/%s",
		         tempdir, ctx->map_name);
		if (copyDirRecursive(ctx->source_dir, scenariodir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to copy scenario source folder");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
		if (!ctx->source_is_scenario_layout
				&& generateScenarioIni(ctx, scenariodir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to generate scenario.ini");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
	} else {
		char parentdir[FS_MAXPATH];
		char mapdir[FS_MAXPATH];
		snprintf(parentdir, sizeof(parentdir), "%s/maps", tempdir);
		if (mkdirSafe(parentdir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Cannot create maps directory");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
		snprintf(mapdir, sizeof(mapdir), "%s/maps/%s", tempdir, ctx->map_name);
		if (copyDirRecursive(ctx->source_dir, mapdir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to copy arena source folder");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
		if (!ctx->source_is_arena_layout
				&& generateArenaIni(ctx, mapdir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to generate arena.ini");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
	}

	/* Copy or generate mod.json */
	if (ctx->has_modjson) {
		char dstpath[FS_MAXPATH];
		snprintf(dstpath, sizeof(dstpath), "%s/mod.json", tempdir);
		if (copyFile(ctx->modjson_path, dstpath) != 0) {
			/* Fallback: generate one */
			sysLogPrintf(LOG_WARNING,
			             "MAPIMPORT: failed to copy mod.json, generating one");
			if (generateModJson(ctx, tempdir) != 0) {
				ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
				         "Failed to generate mod.json");
				removeDirRecursive(tempdir);
				return ctx->result;
			}
		}
	} else {
		if (generateModJson(ctx, tempdir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to generate mod.json");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
	}

	/* Write import metadata */
	writeImportMetadata(ctx, tempdir);

	/* Atomic rename: remove old final dir if it exists, then rename temp */
	{
		struct stat fst;
		if (stat(finaldir, &fst) == 0 && S_ISDIR(fst.st_mode)) {
			sysLogPrintf(LOG_NOTE,
			             "MAPIMPORT: removing existing import at '%s'", finaldir);
			removeDirRecursive(finaldir);
		}
	}

	if (rename(tempdir, finaldir) != 0) {
		/* On Windows, rename across volumes fails. Fall back to copy. */
		sysLogPrintf(LOG_WARNING,
		             "MAPIMPORT: rename failed (%s), attempting copy fallback",
		             strerror(errno));

		if (mkdirSafe(finaldir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Cannot create output directory: %s", finaldir);
			removeDirRecursive(tempdir);
			return ctx->result;
		}

		if (copyDirRecursive(tempdir, finaldir) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to copy source layout from temp to output directory");
			removeDirRecursive(tempdir);
			removeDirRecursive(finaldir);
			return ctx->result;
		}

		removeDirRecursive(tempdir);
	}

	ctx->emit_ok = 1;

	sysLogPrintf(LOG_NOTE, "MAPIMPORT: EMIT complete -- output: %s", finaldir);
	return MAPIMPORT_OK;
}

/* ========================================================================
 * Stage 5: VALIDATE -- Smoke test
 *
 * Validation at import time is limited to filesystem/source-contract checks.
 * Full in-engine validation (source compile, spawn pool build, collision test,
 * match simulation) happens on first load.
 *
 * What we CAN verify here:
 *   - Output directory contains all expected files
 *   - source layout still contains editable map source
 *   - native .bg/.bin/.pad/.setup payloads are still absent
 * ======================================================================== */

static mapimport_result_e stageValidate(import_context_t *ctx)
{
	sysLogPrintf(LOG_NOTE, "MAPIMPORT: VALIDATE -- checking output integrity");

	/* Verify output directory exists */
	{
		struct stat dst;
		if (stat(ctx->output_dir, &dst) != 0 || !S_ISDIR(dst.st_mode)) {
			ctxError(ctx, MAPIMPORT_ERR_VALIDATE_FAILED,
			         "Output directory does not exist: %s", ctx->output_dir);
			return ctx->result;
		}
	}

	/* Verify mod.json exists in output */
	{
		char modjson[FS_MAXPATH];
		struct stat mjst;
		snprintf(modjson, sizeof(modjson), "%s/mod.json", ctx->output_dir);
		if (stat(modjson, &mjst) != 0 || mjst.st_size < 10) {
			ctxError(ctx, MAPIMPORT_ERR_VALIDATE_FAILED,
			         "Output missing valid mod.json");
			return ctx->result;
		}
	}

	/* Verify staged source layout still has editable map source and no native payloads. */
	{
		import_context_t staged;
		memset(&staged, 0, sizeof(staged));
		if (scanSourceTree(&staged, ctx->output_dir, "", 0) != MAPIMPORT_OK) {
			ctxError(ctx, MAPIMPORT_ERR_VALIDATE_FAILED,
			         "Output source validation failed: %s", staged.error);
			return ctx->result;
		}
		if (!staged.has_source_geometry) {
			ctxError(ctx, MAPIMPORT_ERR_VALIDATE_FAILED,
			         "Output missing editable source geometry");
			return ctx->result;
		}
	}

	ctx->validate_ok = 1;

	sysLogPrintf(LOG_NOTE, "MAPIMPORT: VALIDATE complete -- all checks passed");
	return MAPIMPORT_OK;
}

/* ========================================================================
 * Stage 6: REGISTER -- Add to mod system and catalog
 * ======================================================================== */

static mapimport_result_e stageRegister(import_context_t *ctx)
{
	sysLogPrintf(LOG_NOTE, "MAPIMPORT: REGISTER -- adding to mod system");

	/* Trigger a full mod rescan. modmgrReload() will:
	 *   1. Unload all mods
	 *   2. Clear non-bundled catalog entries
	 *   3. Re-scan mods/ directory (picks up our new imported_<name>/)
	 *   4. Re-register enabled mods
	 *   5. Rebuild catalog reverse indexes
	 *
	 * The imported map will appear in the next modmgrScanDirectory() pass
	 * because it has a mod.json in mods/imported_<name>/.
	 *
	 * Note: we don't call modmgrReload() directly here because it resets
	 * asset tables and may disrupt the current game state. Instead, we
	 * signal that a catalog change occurred. The user will see the map
	 * after returning to the title screen or when the Modding Hub triggers
	 * a manual reload.
	 */
	modmgrCatalogChanged();

	ctx->register_ok = 1;

	sysLogPrintf(LOG_NOTE,
	             "MAPIMPORT: REGISTER complete -- map '%s' staged for catalog pickup",
	             ctx->map_name);
	sysLogPrintf(LOG_NOTE,
	             "MAPIMPORT: enable the mod in Modding Hub or call modmgrReload() to activate");

	return MAPIMPORT_OK;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

const char *mapImportResultStr(mapimport_result_e result)
{
	switch (result) {
	case MAPIMPORT_OK:                  return "Success";
	case MAPIMPORT_ERR_NO_SOURCE_DIR:   return "Source directory not found";
	case MAPIMPORT_ERR_NO_SOURCE_GEOMETRY: return "No editable map source found";
	case MAPIMPORT_ERR_FORBIDDEN_NATIVE_PAYLOAD: return "Native map payloads are not accepted";
	case MAPIMPORT_ERR_EMPTY_MAP:       return "Map contains no rooms";
	case MAPIMPORT_ERR_NO_PADS:         return "No pad location data";
	case MAPIMPORT_ERR_CORRUPT_PADS:    return "Pad file is corrupt";
	case MAPIMPORT_ERR_EMIT_FAILED:     return "Failed to write output files";
	case MAPIMPORT_ERR_VALIDATE_FAILED: return "Import validation failed";
	case MAPIMPORT_ERR_REGISTER_FAILED: return "Catalog registration failed";
	case MAPIMPORT_ERR_DUPLICATE_ID:    return "A map with this ID already exists";
	default:                            return "Unknown error";
	}
}

s32 mapImportExists(const char *map_name)
{
	char sanitized[MAPIMPORT_NAME_LEN];
	char checkpath[FS_MAXPATH];
	struct stat st;

	sanitizeName(map_name, sanitized, sizeof(sanitized));

	snprintf(checkpath, sizeof(checkpath), "%s/%s%s",
	         getModsDir(), MAPIMPORT_IMPORT_PREFIX, sanitized);

	return (stat(checkpath, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
}

mapimport_result_e mapImportParse(const char *source_dir,
                                  import_context_t *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	/* Derive map name from directory name */
	{
		const char *lastslash = strrchr(source_dir, '/');
		const char *lastbslash = strrchr(source_dir, '\\');
		const char *dirname;

		if (lastbslash && (!lastslash || lastbslash > lastslash)) {
			lastslash = lastbslash;
		}

		dirname = lastslash ? lastslash + 1 : source_dir;
		sanitizeName(dirname, ctx->map_name, MAPIMPORT_NAME_LEN);
	}

	return stageParse(source_dir, ctx);
}

mapimport_result_e mapImport(const char *source_dir, const char *map_name,
                             import_context_t *ctx)
{
	mapimport_result_e result;

	memset(ctx, 0, sizeof(*ctx));

	sysLogPrintf(LOG_NOTE,
	             "MAPIMPORT: === BEGIN IMPORT === source='%s' name='%s'",
	             source_dir, map_name ? map_name : "(auto)");

	/* Set map name */
	if (map_name && map_name[0]) {
		sanitizeName(map_name, ctx->map_name, MAPIMPORT_NAME_LEN);
	} else {
		/* Derive from directory name */
		const char *lastslash = strrchr(source_dir, '/');
		const char *lastbslash = strrchr(source_dir, '\\');
		const char *dirname;

		if (lastbslash && (!lastslash || lastbslash > lastslash)) {
			lastslash = lastbslash;
		}

		dirname = lastslash ? lastslash + 1 : source_dir;
		sanitizeName(dirname, ctx->map_name, MAPIMPORT_NAME_LEN);
	}

	/* Check for duplicate */
	if (mapImportExists(ctx->map_name)) {
		sysLogPrintf(LOG_WARNING,
		             "MAPIMPORT: map '%s' already imported -- overwriting",
		             ctx->map_name);
		/* Not an error: overwrite allowed (re-import) */
	}

	/* Stage 1: PARSE */
	result = stageParse(source_dir, ctx);
	if (result != MAPIMPORT_OK) goto fail;

	/* Stage 2: NORMALIZE */
	result = stageNormalize(ctx);
	if (result != MAPIMPORT_OK) goto fail;

	/* Stage 3: GENERATE */
	result = stageGenerate(ctx);
	if (result != MAPIMPORT_OK) goto fail;

	/* Stage 4: EMIT */
	result = stageEmit(ctx);
	if (result != MAPIMPORT_OK) goto fail;

	/* Stage 5: VALIDATE */
	result = stageValidate(ctx);
	if (result != MAPIMPORT_OK) goto fail;

	/* Stage 6: REGISTER */
	result = stageRegister(ctx);
	if (result != MAPIMPORT_OK) goto fail;

	sysLogPrintf(LOG_NOTE,
	             "MAPIMPORT: === IMPORT COMPLETE === map='%s' output='%s'",
	             ctx->map_name, ctx->output_dir);

	return MAPIMPORT_OK;

fail:
	sysLogPrintf(LOG_WARNING,
	             "MAPIMPORT: === IMPORT FAILED === map='%s' error='%s'",
	             ctx->map_name, ctx->error);
	return ctx->result;
}

/* ========================================================================
 * C++ wrapper -- thin shim for pdgui_menu_moddinghub.cpp
 * Avoids including mapimport.h from C++ (types.h conflict).
 * ======================================================================== */

s32 mapImportRunFull(const char *source_dir, const char *map_name,
                     char *errbuf, s32 errbuflen,
                     s32 *out_num_rooms, s32 *out_num_pads,
                     s32 *out_generated_spawns)
{
	import_context_t ctx;
	mapimport_result_e result;

	memset(&ctx, 0, sizeof(ctx));

	result = mapImport(source_dir, map_name, &ctx);

	if (errbuf && errbuflen > 0) {
		strncpy(errbuf, ctx.error, errbuflen - 1);
		errbuf[errbuflen - 1] = '\0';
	}

	if (out_num_rooms) *out_num_rooms = ctx.num_rooms;
	if (out_num_pads) *out_num_pads = ctx.num_pads;
	if (out_generated_spawns) *out_generated_spawns = ctx.num_generated_spawns;

	return (s32)result;
}
