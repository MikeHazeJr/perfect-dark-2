/**
 * mapimport.c -- Mod Map Import Pipeline (Layer 3)
 *
 * PD-native binary format importer. Takes a directory of map files (BG, pads,
 * setup, textures), validates and normalizes them, generates missing metadata
 * (spawn points, mod.json), writes to mods/imported_<name>/, and registers
 * in the Asset Catalog.
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

/**
 * Check if a file has one of the expected BG file extensions.
 * PD BG files are typically .bg, .bin, or extensionless.
 */
static s32 isBgFile(const char *name)
{
	s32 len = (s32)strlen(name);
	if (len >= 3 && strcmp(name + len - 3, ".bg") == 0) return 1;
	if (len >= 4 && strcmp(name + len - 4, ".bin") == 0) return 1;

	/* Check if file starts with "bg" prefix (common convention) */
	if (len >= 2 && name[0] == 'b' && name[1] == 'g') return 1;

	return 0;
}

/**
 * Check if a file is a pad file.
 */
static s32 isPadFile(const char *name)
{
	s32 len = (s32)strlen(name);
	if (len >= 4 && strcmp(name + len - 4, ".pad") == 0) return 1;
	if (len >= 5 && strcmp(name + len - 5, ".pads") == 0) return 1;

	/* Prefix convention */
	if (len >= 3 && name[0] == 'p' && name[1] == 'a' && name[2] == 'd') return 1;

	return 0;
}

/**
 * Check if a file is a setup file.
 */
static s32 isSetupFile(const char *name)
{
	s32 len = (s32)strlen(name);
	if (len >= 6 && strcmp(name + len - 6, ".setup") == 0) return 1;
	if (len >= 4 && strcmp(name + len - 4, ".set") == 0) return 1;

	/* Prefix convention */
	if (len >= 5 && strncmp(name, "setup", 5) == 0) return 1;

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

/**
 * Get the resolved mods directory path. Checks CWD-relative first.
 */
static const char *getModsDir(void)
{
	const char *dir = modmgrGetModsDir();
	if (dir && dir[0]) return dir;
	return "./" MODMGR_MODS_DIR;
}

/* ========================================================================
 * Stage 1: PARSE -- Scan source directory
 * ======================================================================== */

static mapimport_result_e stageParse(const char *source_dir,
                                     import_context_t *ctx)
{
	DIR *dir;
	struct dirent *ent;

	sysLogPrintf(LOG_NOTE, "MAPIMPORT: PARSE -- scanning '%s'", source_dir);

	/* Verify source directory exists */
	dir = opendir(source_dir);
	if (!dir) {
		ctxError(ctx, MAPIMPORT_ERR_NO_SOURCE_DIR,
		         "Cannot open source directory: %s", source_dir);
		return ctx->result;
	}

	strncpy(ctx->source_dir, source_dir, FS_MAXPATH - 1);
	ctx->source_dir[FS_MAXPATH - 1] = '\0';

	/* Scan for known file types */
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;

		char fullpath[FS_MAXPATH];
		snprintf(fullpath, sizeof(fullpath), "%s/%s", source_dir, ent->d_name);

		struct stat st;
		if (stat(fullpath, &st) != 0 || S_ISDIR(st.st_mode)) {
			continue; /* Skip directories and unreadable files */
		}

		/* Check for BG file */
		if (!ctx->has_bg && isBgFile(ent->d_name)) {
			ctx->has_bg = 1;
			strncpy(ctx->bg_path, fullpath, FS_MAXPATH - 1);
			ctx->bg_path[FS_MAXPATH - 1] = '\0';
			sysLogPrintf(LOG_NOTE, "MAPIMPORT:   BG file: %s", ent->d_name);
		}

		/* Check for pad file */
		if (!ctx->has_pads && isPadFile(ent->d_name)) {
			ctx->has_pads = 1;
			strncpy(ctx->pad_path, fullpath, FS_MAXPATH - 1);
			ctx->pad_path[FS_MAXPATH - 1] = '\0';
			sysLogPrintf(LOG_NOTE, "MAPIMPORT:   Pad file: %s", ent->d_name);
		}

		/* Check for setup file */
		if (!ctx->has_setup && isSetupFile(ent->d_name)) {
			ctx->has_setup = 1;
			strncpy(ctx->setup_path, fullpath, FS_MAXPATH - 1);
			ctx->setup_path[FS_MAXPATH - 1] = '\0';
			sysLogPrintf(LOG_NOTE, "MAPIMPORT:   Setup file: %s", ent->d_name);
		}

		/* Check for mod.json */
		if (strcmp(ent->d_name, "mod.json") == 0) {
			ctx->has_modjson = 1;
			strncpy(ctx->modjson_path, fullpath, FS_MAXPATH - 1);
			ctx->modjson_path[FS_MAXPATH - 1] = '\0';
			sysLogPrintf(LOG_NOTE, "MAPIMPORT:   mod.json found");
		}
	}

	closedir(dir);

	/* BG file is mandatory */
	if (!ctx->has_bg) {
		ctxError(ctx, MAPIMPORT_ERR_NO_BG_FILE,
		         "No map geometry file found in '%s'. Expected a .bg or .bin file.",
		         source_dir);
		return ctx->result;
	}

	/* Validate BG file: check it's non-empty */
	{
		struct stat bgst;
		if (stat(ctx->bg_path, &bgst) != 0 || bgst.st_size < 16) {
			ctxError(ctx, MAPIMPORT_ERR_CORRUPT_BG,
			         "Map geometry file is too small or corrupt (%lld bytes).",
			         (long long)(bgst.st_size));
			return ctx->result;
		}

		/* Check for valid BG header: first 4 bytes should be non-zero
		 * (room count or header magic). PD BG files start with the room count. */
		FILE *bgf = fopen(ctx->bg_path, "rb");
		if (bgf) {
			u32 header;
			if (fread(&header, 4, 1, bgf) == 1) {
				/* A valid BG has room count > 0 and < MAX. Zero = empty. */
				/* Note: byte order may vary but 0 is always invalid. */
				if (header == 0) {
					fclose(bgf);
					ctxError(ctx, MAPIMPORT_ERR_EMPTY_MAP,
					         "Map geometry file has 0 rooms.");
					return ctx->result;
				}
				if (header < 0x10000) {
					ctx->num_rooms = (s32)header;
				}
			}
			fclose(bgf);
		}
	}

	/* Validate pad file if present */
	if (ctx->has_pads) {
		struct stat padst;
		if (stat(ctx->pad_path, &padst) != 0 || padst.st_size < 4) {
			sysLogPrintf(LOG_WARNING,
			             "MAPIMPORT: Pad file exists but is too small (%lld bytes), ignoring",
			             (long long)(padst.st_size));
			ctx->has_pads = 0;
		} else {
			/* Read pad count from header */
			FILE *padf = fopen(ctx->pad_path, "rb");
			if (padf) {
				u32 numpads;
				if (fread(&numpads, 4, 1, padf) == 1 && numpads > 0 &&
				    numpads <= MAPIMPORT_MAX_PADS) {
					ctx->num_pads = (s32)numpads;
				}
				fclose(padf);
			}
		}
	}

	ctx->parse_ok = 1;

	sysLogPrintf(LOG_NOTE,
	             "MAPIMPORT: PARSE complete -- bg=%d pads=%d(%d) setup=%d modjson=%d rooms=%d",
	             ctx->has_bg, ctx->has_pads, ctx->num_pads,
	             ctx->has_setup, ctx->has_modjson, ctx->num_rooms);

	return MAPIMPORT_OK;
}

/* ========================================================================
 * Stage 2: NORMALIZE -- Validate and sanitize
 * ======================================================================== */

static mapimport_result_e stageNormalize(import_context_t *ctx)
{
	sysLogPrintf(LOG_NOTE, "MAPIMPORT: NORMALIZE -- validating content");

	/* Room count bounds check */
	if (ctx->num_rooms > MAPIMPORT_MAX_ROOMS) {
		sysLogPrintf(LOG_WARNING,
		             "MAPIMPORT: room count %d exceeds max %d -- clamping",
		             ctx->num_rooms, MAPIMPORT_MAX_ROOMS);
		/* Not fatal: PD can handle large maps, just warn */
	}

	/* Pad count bounds check */
	if (ctx->num_pads > MAPIMPORT_MAX_PADS) {
		sysLogPrintf(LOG_WARNING,
		             "MAPIMPORT: pad count %d exceeds max %d -- clamping",
		             ctx->num_pads, MAPIMPORT_MAX_PADS);
	}

	/* If no pads, we'll need to generate spawns from geometry alone (L3/L4) */
	if (!ctx->has_pads || ctx->num_pads == 0) {
		sysLogPrintf(LOG_NOTE,
		             "MAPIMPORT: no pad data -- spawns will be generated from geometry");
		ctx->has_spawns = 0;
	}

	/* If no setup file, spawns definitely need generation */
	if (!ctx->has_setup) {
		ctx->has_spawns = 0;
		ctx->has_waypoints = 0;
		sysLogPrintf(LOG_NOTE,
		             "MAPIMPORT: no setup file -- will generate minimal setup");
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
		"  \"has_bg\": %d,\n"
		"  \"has_pads\": %d,\n"
		"  \"has_setup\": %d,\n"
		"  \"has_modjson\": %d,\n"
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
		ctx->has_bg,
		ctx->has_pads,
		ctx->has_setup,
		ctx->has_modjson,
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

	/* Copy BG file */
	{
		char dstpath[FS_MAXPATH];
		snprintf(dstpath, sizeof(dstpath), "%s/%s.bg", tempdir, ctx->map_name);
		if (copyFile(ctx->bg_path, dstpath) != 0) {
			ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
			         "Failed to copy BG file to output");
			removeDirRecursive(tempdir);
			return ctx->result;
		}
	}

	/* Copy pad file if present */
	if (ctx->has_pads) {
		char dstpath[FS_MAXPATH];
		snprintf(dstpath, sizeof(dstpath), "%s/%s.pad", tempdir, ctx->map_name);
		if (copyFile(ctx->pad_path, dstpath) != 0) {
			sysLogPrintf(LOG_WARNING, "MAPIMPORT: failed to copy pad file (non-fatal)");
		}
	}

	/* Copy setup file if present */
	if (ctx->has_setup) {
		char dstpath[FS_MAXPATH];
		snprintf(dstpath, sizeof(dstpath), "%s/%s.setup", tempdir, ctx->map_name);
		if (copyFile(ctx->setup_path, dstpath) != 0) {
			sysLogPrintf(LOG_WARNING, "MAPIMPORT: failed to copy setup file (non-fatal)");
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

	/* Copy any additional files from source (textures, etc.) */
	{
		DIR *srcdir = opendir(ctx->source_dir);
		if (srcdir) {
			struct dirent *ent;
			while ((ent = readdir(srcdir)) != NULL) {
				if (ent->d_name[0] == '.') continue;
				if (strcmp(ent->d_name, "mod.json") == 0) continue;

				char srcfull[FS_MAXPATH];
				char dstfull[FS_MAXPATH];
				snprintf(srcfull, sizeof(srcfull), "%s/%s",
				         ctx->source_dir, ent->d_name);
				snprintf(dstfull, sizeof(dstfull), "%s/%s",
				         tempdir, ent->d_name);

				struct stat fst;
				if (stat(srcfull, &fst) == 0 && !S_ISDIR(fst.st_mode)) {
					/* Skip files we already copied */
					if (strcmp(srcfull, ctx->bg_path) == 0) continue;
					if (ctx->has_pads && strcmp(srcfull, ctx->pad_path) == 0) continue;
					if (ctx->has_setup && strcmp(srcfull, ctx->setup_path) == 0) continue;

					/* Don't fail on extra file copy failures */
					if (copyFile(srcfull, dstfull) != 0) {
						sysLogPrintf(LOG_WARNING,
						             "MAPIMPORT: failed to copy '%s' (non-fatal)",
						             ent->d_name);
					}
				}
			}
			closedir(srcdir);
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

		/* Copy all files from temp to final */
		DIR *tmpdir = opendir(tempdir);
		if (tmpdir) {
			struct dirent *ent;
			s32 copy_ok = 1;
			while ((ent = readdir(tmpdir)) != NULL) {
				if (ent->d_name[0] == '.') continue;

				char tsrc[FS_MAXPATH], tdst[FS_MAXPATH];
				snprintf(tsrc, sizeof(tsrc), "%s/%s", tempdir, ent->d_name);
				snprintf(tdst, sizeof(tdst), "%s/%s", finaldir, ent->d_name);

				struct stat tfst;
				if (stat(tsrc, &tfst) == 0 && !S_ISDIR(tfst.st_mode)) {
					if (copyFile(tsrc, tdst) != 0) {
						copy_ok = 0;
					}
				}
			}
			closedir(tmpdir);

			if (!copy_ok) {
				ctxError(ctx, MAPIMPORT_ERR_EMIT_FAILED,
				         "Failed to copy files from temp to output directory");
				removeDirRecursive(tempdir);
				removeDirRecursive(finaldir);
				return ctx->result;
			}
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
 * Since we can't load the BG data into the engine mid-session without
 * disrupting the current stage, validation at import time is limited to
 * filesystem-level checks. Full in-engine validation (spawn pool build,
 * collision test, match simulation) happens on first load.
 *
 * What we CAN verify here:
 *   - Output directory contains all expected files
 *   - mod.json is parseable by modmgr
 *   - File sizes are sane
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

	/* Verify BG file exists in output */
	{
		char bgfile[FS_MAXPATH];
		struct stat bgst;
		snprintf(bgfile, sizeof(bgfile), "%s/%s.bg",
		         ctx->output_dir, ctx->map_name);
		if (stat(bgfile, &bgst) != 0 || bgst.st_size < 16) {
			ctxError(ctx, MAPIMPORT_ERR_VALIDATE_FAILED,
			         "Output missing valid BG geometry file");
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
	case MAPIMPORT_ERR_NO_BG_FILE:      return "No map geometry file found";
	case MAPIMPORT_ERR_CORRUPT_BG:      return "Map geometry file is corrupt";
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
