/**
 * modpack_pdmod.c -- Priority M / B-238 shared `.pdmod` save helper.
 *
 * Implements port/include/modpack_pdmod.h. Builds on top of modarchive's
 * writer; layered above modmgr's manifest fields so the comment mirror
 * has access to the same names / authors / versions the loader will see
 * later.
 *
 * Single source of truth for the comment mirror: the manifest JSON
 * embedded inside the archive. The mirror is a projection of the headline
 * fields (name, creator/author, version, tags) computed at write time;
 * never written separately, never editable independently.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "fs.h"
#include "system.h"
#include "asset_archive_policy.h"
#include "assetcatalog_scanner.h"
#include "modarchive.h"
#include "modpack_pdmod.h"

static char s_pdmodLastError[512];

static void pdmodClearLastError(void)
{
	s_pdmodLastError[0] = '\0';
}

static void pdmodSetLastError(const char *fmt, ...)
{
	va_list ap;

	if (!fmt) {
		pdmodClearLastError();
		return;
	}

	va_start(ap, fmt);
	vsnprintf(s_pdmodLastError, sizeof(s_pdmodLastError), fmt, ap);
	va_end(ap);
	s_pdmodLastError[sizeof(s_pdmodLastError) - 1] = '\0';
}

const char *modpackPdmodLastError(void)
{
	return s_pdmodLastError;
}

/* ------------------------------------------------------------ JSON helper */

/* Locate the start of a quoted string value associated with the given
 * top-level key. Returns a pointer to the first character of the value
 * (inside the opening quote) and writes the length to *outLen. Returns
 * NULL when the key is missing or its value is not a string. */
static const char *findJsonStringValue(const char *json, const char *key, u32 *outLen)
{
	if (!json || !key || !outLen) return NULL;
	*outLen = 0;

	size_t klen = strlen(key);
	const char *p = json;
	while (p && *p) {
		const char *q = strchr(p, '"');
		if (!q) return NULL;
		const char *qend = strchr(q + 1, '"');
		if (!qend) return NULL;
		/* q+1 .. qend = key candidate */
		if ((size_t)(qend - q - 1) == klen && memcmp(q + 1, key, klen) == 0) {
			/* Skip whitespace + colon. */
			const char *r = qend + 1;
			while (*r && (*r == ' ' || *r == '\t' || *r == '\r' || *r == '\n')) r++;
			if (*r != ':') {
				p = qend + 1;
				continue;
			}
			r++;
			while (*r && (*r == ' ' || *r == '\t' || *r == '\r' || *r == '\n')) r++;
			if (*r != '"') return NULL;
			r++;
			const char *vEnd = r;
			while (*vEnd && *vEnd != '"') {
				if (*vEnd == '\\' && vEnd[1]) vEnd += 2;
				else vEnd++;
			}
			if (*vEnd != '"') return NULL;
			*outLen = (u32)(vEnd - r);
			return r;
		}
		p = qend + 1;
	}
	return NULL;
}

/* Build the comment-mirror JSON blob. Output into `out` (cap `cap`).
 * Returns the number of bytes written (excluding the trailing NUL). The
 * blob is bounded so it always fits inside the zip's 64 KiB comment field;
 * we cap each string at a few hundred chars. */
static u32 buildCommentMirror(const char *manifest, char *out, u32 cap)
{
	if (!manifest || !out || cap < 4) return 0;

	u32 nameLen = 0, creatorLen = 0, versionLen = 0;
	const char *namePtr = findJsonStringValue(manifest, "name", &nameLen);
	const char *creatorPtr = findJsonStringValue(manifest, "creator", &creatorLen);
	if (!creatorPtr) {
		creatorPtr = findJsonStringValue(manifest, "author", &creatorLen);
	}
	const char *versionPtr = findJsonStringValue(manifest, "version", &versionLen);

	/* Clamp each field to a sane upper bound; the mirror is for tools
	 * peeking at the file, not a full manifest replacement. */
	if (nameLen > 200)    nameLen = 200;
	if (creatorLen > 100) creatorLen = 100;
	if (versionLen > 64)  versionLen = 64;

	/* Manual JSON-encode each value so embedded quotes / backslashes do
	 * not corrupt the output. */
	#define EMIT_FIELD(label, src, srcLen)                                   \
		do {                                                                  \
			pos += (u32)snprintf(out + pos, cap - pos, "\"%s\":\"", label);   \
			if (pos >= cap) goto done;                                        \
			for (u32 _i = 0; _i < (srcLen) && pos + 2 < cap; _i++) {          \
				char _c = (src)[_i];                                          \
				if (_c == '"' || _c == '\\') out[pos++] = '\\';               \
				out[pos++] = _c;                                              \
			}                                                                  \
			if (pos < cap) out[pos++] = '"';                                  \
		} while (0)

	u32 pos = 0;
	if (cap == 0) return 0;
	out[pos++] = '{';
	bool needsComma = false;
	if (namePtr) {
		EMIT_FIELD("name", namePtr, nameLen);
		needsComma = true;
	}
	if (creatorPtr) {
		if (needsComma && pos < cap) out[pos++] = ',';
		EMIT_FIELD("creator", creatorPtr, creatorLen);
		needsComma = true;
	}
	if (versionPtr) {
		if (needsComma && pos < cap) out[pos++] = ',';
		EMIT_FIELD("version", versionPtr, versionLen);
	}
done:
	if (pos < cap) out[pos++] = '}';
	if (pos < cap) out[pos] = '\0';
	else           out[cap - 1] = '\0';
	#undef EMIT_FIELD
	return pos;
}

/* ------------------------------------------------------ Layout validation */

static int folderShouldSkip(const char *leaf, const char *fullPath, const char *destPath);

static s32 entryHasForbiddenBinPayload(const char *name)
{
	return assetArchiveEntryIsForbiddenBinPayload(name);
}

static void pathJoin(char *out, size_t cap, const char *left, const char *right)
{
	if (!out || cap == 0) {
		return;
	}
	if (!left || !left[0]) {
		snprintf(out, cap, "%s", right ? right : "");
		return;
	}
	if (!right || !right[0]) {
		snprintf(out, cap, "%s", left);
		return;
	}

	size_t len = strlen(left);
	if (left[len - 1] == '/' || left[len - 1] == '\\') {
		snprintf(out, cap, "%s%s", left, right);
	} else {
		snprintf(out, cap, "%s/%s", left, right);
	}
	out[cap - 1] = '\0';
}

static void pathDirnameRel(const char *path, char *out, size_t outsz)
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

static void normalizeSlashes(char *s)
{
	if (!s) {
		return;
	}
	for (; *s; s++) {
		if (*s == '\\') {
			*s = '/';
		}
	}
}

static s32 fileExistsRegular(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static s32 dirExistsLocal(const char *path)
{
	struct stat st;
	return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static s32 relativeSourcePathIsSafe(const char *path)
{
	if (!path || !path[0]) {
		return 0;
	}
	if (path[0] == '/' || path[0] == '\\') {
		return 0;
	}
	if (isalpha((u8)path[0]) && path[1] == ':') {
		return 0;
	}

	const char *p = path;
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

static asset_type_e typedPdContentTypeForPath(const char *path)
{
	return assetArchiveTypeForPath(path);
}

static const char *typedPdArchiveDescriptorLeaf(const char *path)
{
	return assetArchiveDescriptorForPath(path);
}

static void descriptorStemBaseRel(const char *srcFolder, const char *descriptorRel,
                                  char *out, size_t outsz)
{
	if (!out || outsz == 0) {
		return;
	}
	out[0] = '\0';
	if (!descriptorRel) {
		return;
	}

	char dir[FS_MAXPATH + 1];
	pathDirnameRel(descriptorRel, dir, sizeof(dir));

	if (typedPdContentTypeForPath(descriptorRel) == ASSET_NONE) {
		strncpy(out, dir, outsz - 1);
		out[outsz - 1] = '\0';
		return;
	}

	const char *leaf = strrchr(descriptorRel, '/');
	leaf = leaf ? leaf + 1 : descriptorRel;
	char stem[128];
	strncpy(stem, leaf, sizeof(stem) - 1);
	stem[sizeof(stem) - 1] = '\0';
	char *dot = strrchr(stem, '.');
	if (dot) {
		*dot = '\0';
	}

	char candidate[FS_MAXPATH + 1];
	if (dir[0]) {
		pathJoin(candidate, sizeof(candidate), dir, stem);
	} else {
		strncpy(candidate, stem, FS_MAXPATH);
		candidate[FS_MAXPATH] = '\0';
	}
	normalizeSlashes(candidate);

	char candidateAbs[FS_MAXPATH + 1];
	pathJoin(candidateAbs, sizeof(candidateAbs), srcFolder, candidate);
	if (dirExistsLocal(candidateAbs)) {
		strncpy(out, candidate, outsz - 1);
		out[outsz - 1] = '\0';
		return;
	}

	strncpy(out, dir, outsz - 1);
	out[outsz - 1] = '\0';
}

static const char *packerSidecarTemplate(const char *leaf)
{
	if (!leaf) {
		return NULL;
	}
	if (strcmp(leaf, "pads.ini") == 0) {
		return
			"; pads.ini - generated external map pad/spawn template\n"
			"[pads]\n"
			"; Add one pad_<n> row per spawn or navigation pad.\n"
			"default_spawn = 0,0,0\n"
			"; pad_0 = 0,0,0,0\n";
	}
	if (strcmp(leaf, "setup.ini") == 0) {
		return
			"; setup.ini - generated external map setup template\n"
			"[setup]\n"
			"; Add setup props here once the importer supports authored props.\n"
			"props = none\n";
	}
	if (strcmp(leaf, "props.ini") == 0) {
		return
			"; props.ini - legacy map authoring template; .pdscenario uses objects.tsv\n"
			"[props]\n"
			"props = none\n";
	}
	if (strcmp(leaf, "objectives.ini") == 0) {
		return
			"; objectives.ini - legacy map authoring template; .pdscenario uses objectives.tsv\n"
			"[objectives]\n"
			"objectives = none\n";
	}
	if (strcmp(leaf, "pads.tsv") == 0) {
		return
			"pad_id\troom_ref\tliftnum\tflags\tpos_x\tpos_y\tpos_z\tup_x\tup_y\tup_z\tlook_x\tlook_y\tlook_z\tbbox_xmin\tbbox_xmax\tbbox_ymin\tbbox_ymax\tbbox_zmin\tbbox_zmax\n"
			"pad_0000\troom_0000\t0\t0x00000\t0\t0\t0\t0\t1\t0\t0\t0\t1\t-16\t16\t0\t64\t-16\t16\n";
	}
	if (strcmp(leaf, "spawns.tsv") == 0) {
		return
			"spawn_id\tpad_ref\troom_ref\tteam\tprofile\tpos_x\tpos_y\tpos_z\tlook_x\tlook_y\tlook_z\n"
			"spawn_0000\tpad_0000\troom_0000\tany\tdefault\t0\t0\t0\t0\t0\t1\n";
	}
	if (strcmp(leaf, "volumes.tsv") == 0) {
		return
			"volume_id\tpad_ref\tkind\troom_ref\tshape\tmin_x\tmin_y\tmin_z\tmax_x\tmax_y\tmax_z\n"
			"volume_pad_0000\tpad_0000\tpad_bounds\troom_0000\taabb\t-16\t0\t-16\t16\t64\t16\n";
	}
	if (strcmp(leaf, "objects.tsv") == 0) {
		return
			"record_id\tkind\tpad_ref\tmodel_catalog_id\tweapon_catalog_id\tsecondary_weapon_catalog_id\tbody_catalog_id\thead_catalog_id\tailist_ref\tflags\tflags2\tflags3\n";
	}
	if (strcmp(leaf, "objectives.tsv") == 0) {
		return
			"objective_id\tkind\ttext_token\tdifficulty_mask\tgraph_node\toperand_kind\ttarget_ref\ttarget_record_ref\tpad_ref\tstate_ref\tmatch_value\tinitial_status\n";
	}
	if (strcmp(leaf, "navigation.ini") == 0) {
		return
			"[navigation]\n"
			"source = scene.glb\n"
			"collision_source = collision.obj\n"
			"generator = deterministic.surface_graph.v1\n"
			"supports_walk = true\n"
			"supports_jump = true\n"
			"supports_wall = true\n"
			"supports_ceiling = true\n";
	}
	if (strcmp(leaf, "level.graph.json") == 0) {
		return
			"{\n"
			"  \"schema\": \"pd2.level.graph.v1\",\n"
			"  \"source\": \"scene.glb\",\n"
			"  \"nodes\": [],\n"
			"  \"links\": []\n"
			"}\n";
	}
	return NULL;
}

static const char *packerTemplateForKind(const char *kind, const char *leaf)
{
	const char *templ = modiniTemplateForKind(kind);
	if (templ) {
		return templ;
	}
	return packerSidecarTemplate(leaf);
}

static s32 writeTemplateIfMissing(const char *absPath, const char *relPath,
                                  const char *kind, u32 *generated)
{
	if (fileExistsRegular(absPath)) {
		return MODPACK_PDMOD_OK;
	}

	const char *leaf = relPath ? strrchr(relPath, '/') : NULL;
	leaf = leaf ? leaf + 1 : relPath;
	const char *templ = packerTemplateForKind(kind, leaf);
	if (!templ) {
		pdmodSetLastError("No INI template is registered for %s", relPath ? relPath : "(unknown)");
		return MODPACK_PDMOD_ERR_TEMPLATE;
	}

	FILE *f = fopen(absPath, "wb");
	if (!f) {
		pdmodSetLastError("Could not create missing INI template %s", relPath ? relPath : absPath);
		return MODPACK_PDMOD_ERR_TEMPLATE;
	}
	size_t len = strlen(templ);
	if (fwrite(templ, 1, len, f) != len) {
		fclose(f);
		pdmodSetLastError("Could not write INI template %s", relPath ? relPath : absPath);
		return MODPACK_PDMOD_ERR_TEMPLATE;
	}
	fclose(f);
	if (generated) {
		(*generated)++;
	}
	return MODPACK_PDMOD_OK;
}

static s32 validateNoForbiddenBinsRecurse(const char *fsRoot, const char *relRoot,
                                           const char *destPath)
{
	char absDir[FS_MAXPATH + 1];
	if (relRoot && relRoot[0]) {
		pathJoin(absDir, sizeof(absDir), fsRoot, relRoot);
	} else {
		strncpy(absDir, fsRoot, FS_MAXPATH);
		absDir[FS_MAXPATH] = '\0';
	}

	DIR *d = opendir(absDir);
	if (!d) {
		pdmodSetLastError("Could not scan source folder %s", absDir);
		return MODPACK_PDMOD_ERR_IO;
	}

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		char childAbs[FS_MAXPATH + 1];
		pathJoin(childAbs, sizeof(childAbs), absDir, ent->d_name);
		if (folderShouldSkip(ent->d_name, childAbs, destPath)) {
			continue;
		}

		struct stat st;
		if (stat(childAbs, &st) != 0) {
			continue;
		}

		char childRel[FS_MAXPATH + 1];
		if (relRoot && relRoot[0]) {
			snprintf(childRel, sizeof(childRel), "%s/%s", relRoot, ent->d_name);
		} else {
			strncpy(childRel, ent->d_name, FS_MAXPATH);
			childRel[FS_MAXPATH] = '\0';
		}

		if (S_ISDIR(st.st_mode)) {
			s32 r = validateNoForbiddenBinsRecurse(fsRoot, childRel, destPath);
			if (r != MODPACK_PDMOD_OK) {
				closedir(d);
				return r;
			}
		} else if (S_ISREG(st.st_mode)) {
			if (assetArchivePathIsDeprecated(childRel)) {
				pdmodSetLastError("Deprecated .pdwpn files are not allowed in typed asset archives or .pdmod transport archives: %s", childRel);
				closedir(d);
				return MODPACK_PDMOD_ERR_LAYOUT;
			}
			if (entryHasForbiddenBinPayload(childRel)) {
				pdmodSetLastError("Authored .bin files are not allowed in mod content or .pdmod transport archives: %s", childRel);
				closedir(d);
				return MODPACK_PDMOD_ERR_LAYOUT;
			}
		}
	}

	closedir(d);
	return MODPACK_PDMOD_OK;
}

static s32 validateReferencedPath(const char *srcFolder, const char *descriptorRel,
                                  const char *key, const char *value, s32 required)
{
	if (!value || !value[0]) {
		if (required) {
			pdmodSetLastError("%s is missing required source setting %s",
				descriptorRel ? descriptorRel : "(descriptor)", key ? key : "(source)");
			return MODPACK_PDMOD_ERR_LAYOUT;
		}
		return MODPACK_PDMOD_OK;
	}

	if (!relativeSourcePathIsSafe(value)) {
		pdmodSetLastError("%s uses unsafe source path for %s: %s",
			descriptorRel ? descriptorRel : "(descriptor)", key ? key : "(source)", value);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}

	char descDir[FS_MAXPATH + 1];
	descriptorStemBaseRel(srcFolder, descriptorRel, descDir, sizeof(descDir));

	char sourceRel[FS_MAXPATH + 1];
	if (strchr(value, '/') || strchr(value, '\\')) {
		strncpy(sourceRel, value, FS_MAXPATH);
		sourceRel[FS_MAXPATH] = '\0';
	} else if (descDir[0]) {
		pathJoin(sourceRel, sizeof(sourceRel), descDir, value);
	} else {
		strncpy(sourceRel, value, FS_MAXPATH);
		sourceRel[FS_MAXPATH] = '\0';
	}
	normalizeSlashes(sourceRel);

	if (entryHasForbiddenBinPayload(sourceRel)) {
		pdmodSetLastError("%s points at forbidden authored .bin payload for %s: %s",
			descriptorRel ? descriptorRel : "(descriptor)", key ? key : "(source)", sourceRel);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}

	char sourceAbs[FS_MAXPATH + 1];
	pathJoin(sourceAbs, sizeof(sourceAbs), srcFolder, sourceRel);
	if (!fileExistsRegular(sourceAbs)) {
		pdmodSetLastError("%s references missing source file for %s: %s",
			descriptorRel ? descriptorRel : "(descriptor)", key ? key : "(source)", sourceRel);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}

	return MODPACK_PDMOD_OK;
}

static s32 validateArchiveReferencedPath(mod_archive_t *arc,
                                         const char *descriptorRel,
                                         const char *key,
                                         const char *value,
                                         s32 required)
{
	if (!value || !value[0]) {
		if (required) {
			pdmodSetLastError("%s is missing required internal archive setting %s",
				descriptorRel ? descriptorRel : "(descriptor)",
				key ? key : "(source)");
			return MODPACK_PDMOD_ERR_LAYOUT;
		}
		return MODPACK_PDMOD_OK;
	}

	if (!relativeSourcePathIsSafe(value) || strstr(value, "::")) {
		pdmodSetLastError("%s uses unsafe internal archive path for %s: %s",
			descriptorRel ? descriptorRel : "(descriptor)",
			key ? key : "(source)", value);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}
	if (entryHasForbiddenBinPayload(value)) {
		pdmodSetLastError("%s points at forbidden authored .bin payload for %s: %s",
			descriptorRel ? descriptorRel : "(descriptor)",
			key ? key : "(source)", value);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}
	if (modArchiveFindEntry(arc, value) < 0) {
		pdmodSetLastError("%s references missing internal archive file for %s: %s",
			descriptorRel ? descriptorRel : "(descriptor)",
			key ? key : "(source)", value);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}

	return MODPACK_PDMOD_OK;
}

static const char *iniFirstValueForKeys(const ini_section_t *ini,
                                        const char *const *keys, s32 keyCount,
                                        const char **outKey)
{
	for (s32 i = 0; i < keyCount; i++) {
		const char *value = iniGet(ini, keys[i], "");
		if (value[0]) {
			if (outKey) {
				*outKey = keys[i];
			}
			return value;
		}
	}
	if (outKey) {
		*outKey = keyCount > 0 ? keys[0] : "source";
	}
	return "";
}

static s32 validateDescriptorSources(const char *srcFolder, const char *descriptorRel,
                                     const char *const *requiredKeys, s32 requiredKeyCount,
                                     const char *const *optionalKeys, s32 optionalKeyCount)
{
	char descriptorAbs[FS_MAXPATH + 1];
	pathJoin(descriptorAbs, sizeof(descriptorAbs), srcFolder, descriptorRel);

	ini_section_t ini;
	if (!iniParse(descriptorAbs, &ini)) {
		pdmodSetLastError("Malformed INI descriptor: %s", descriptorRel);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}

	const char *key = NULL;
	const char *value = iniFirstValueForKeys(&ini, requiredKeys, requiredKeyCount, &key);
	s32 r = validateReferencedPath(srcFolder, descriptorRel, key, value, requiredKeyCount > 0);
	if (r != MODPACK_PDMOD_OK) {
		return r;
	}

	for (s32 i = 0; i < optionalKeyCount; i++) {
		if (!optionalKeys[i] || !optionalKeys[i][0]) {
			continue;
		}
		value = iniGet(&ini, optionalKeys[i], "");
		if (!value[0]) {
			continue;
		}
		r = validateReferencedPath(srcFolder, descriptorRel, optionalKeys[i], value, 0);
		if (r != MODPACK_PDMOD_OK) {
			return r;
		}
	}

	return MODPACK_PDMOD_OK;
}

static s32 validateArchiveDescriptorSources(mod_archive_t *arc,
                                            const ini_section_t *ini,
                                            const char *descriptorRel,
                                            const char *const *requiredKeys,
                                            s32 requiredKeyCount,
                                            const char *const *optionalKeys,
                                            s32 optionalKeyCount)
{
	const char *key = NULL;
	const char *value = iniFirstValueForKeys(ini, requiredKeys,
		requiredKeyCount, &key);
	s32 r = validateArchiveReferencedPath(arc, descriptorRel, key, value,
		requiredKeyCount > 0);
	if (r != MODPACK_PDMOD_OK) {
		return r;
	}

	for (s32 i = 0; i < optionalKeyCount; i++) {
		if (!optionalKeys[i] || !optionalKeys[i][0]) {
			continue;
		}
		value = iniGet(ini, optionalKeys[i], "");
		if (!value[0]) {
			continue;
		}
		r = validateArchiveReferencedPath(arc, descriptorRel,
			optionalKeys[i], value, 0);
		if (r != MODPACK_PDMOD_OK) {
			return r;
		}
	}

	return MODPACK_PDMOD_OK;
}

static s32 validateTypedPdDescriptorFile(const char *srcFolder, const char *descriptorRel)
{
	if (assetArchivePathIsDeprecated(descriptorRel)) {
		pdmodSetLastError("Deprecated .pdwpn files are not allowed in typed asset archives or .pdmod transport archives: %s", descriptorRel);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}
	if (!assetArchivePathIsTyped(descriptorRel)) {
		return MODPACK_PDMOD_OK;
	}
	asset_type_e type = typedPdContentTypeForPath(descriptorRel);

	static const char *modelKeys[] = {
		"model_file", "model", "geometry_file", "geometry", "file_path"
	};
	static const char *behaviorAssetKeys[] = {
		"behavior_graph", "graph", "model_file", "model", "file_path"
	};
	static const char *arenaKeys[] = { "geometry_file", "geometry" };
	static const char *scenarioKeys[] = {
		"scene_file", "scene", "runtime_source_file",
		"rooms_file", "rooms", "geometry_file", "geometry"
	};
	static const char *animationKeys[] = { "animation_file", "file_path" };
	static const char *audioKeys[] = { "file_path" };
	static const char *uiKeys[] = { "texture_file", "file_path", "texture" };
	static const char *fontKeys[] = { "font_file", "glyphs_file", "file_path", "font" };
	static const char *langKeys[] = {
		"strings_file", "strings", "strings_tsv", "file_path"
	};
	static const char *materialOptionalKeys[] = {
		"material_file", "texture_archive", "texture_file", "effect_archive",
		"file_path"
	};
	static const char *effectOptionalKeys[] = {
		"effect_file", "behavior_graph", "graph", "texture_archive",
		"audio_archive", "file_path"
	};
	static const char *metadataOptionalKeys[] = {
		"file_path", "model_file", "texture_file", "theme_file",
		"rules_file", "scenario_archive", "ui_archive", "font_archive",
		"audio_archive", "objectives_file", "briefing_file",
		"mission_graph_file", "graph"
	};
	static const char *mapOptionalKeys[] = {
		"pads_file", "setup_file", "collision_file", "material_file",
		"texture_file", "texture_manifest_file", "blender_scene_file",
		"visual_scene_file", "visual_material_file", "visual_materials_file"
	};
	static const char *scenarioOptionalKeys[] = {
		"collision_file", "collision_source_file", "collision_source",
		"objects_file", "setup_fields_file", "objectives_file", "tiles_file",
		"pads_file", "spawns_file", "volumes_file",
		"navigation_file", "level_graph_file",
		"material_file", "texture_file", "texture_manifest_file",
		"blender_scene_file", "visual_scene_file", "visual_material_file",
		"visual_materials_file", "visual_source_file"
	};

	char descriptorAbs[FS_MAXPATH + 1];
	pathJoin(descriptorAbs, sizeof(descriptorAbs), srcFolder, descriptorRel);

	ini_section_t archiveIni;
	char policyErr[256];
	if (assetArchiveValidateFile(descriptorAbs, ASSET_ARCHIVE_VALIDATE_RELEASE,
			policyErr, sizeof(policyErr)) != 0) {
		pdmodSetLastError("%s", policyErr[0] ? policyErr : "typed asset archive validation failed");
		return MODPACK_PDMOD_ERR_LAYOUT;
	}
	mod_archive_t *typedArchive = modArchiveOpen(descriptorAbs);
	if (!typedArchive) {
		pdmodSetLastError("%s is not a zip-openable typed asset archive", descriptorRel);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}
	const char *leaf = NULL;
	s32 idx = assetArchiveFindDescriptorEntry(typedArchive, descriptorRel,
		ASSET_ARCHIVE_VALIDATE_RELEASE, &leaf);
	if (idx < 0) {
		pdmodSetLastError("%s is missing its internal descriptor %s",
			descriptorRel, typedPdArchiveDescriptorLeaf(descriptorRel));
		modArchiveClose(typedArchive);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}

	u32 iniSize = 0;
	char *iniBytes = (char *)modArchiveExtractAlloc(typedArchive, idx, &iniSize);
	if (!iniBytes) {
		pdmodSetLastError("%s internal descriptor could not be read", descriptorRel);
		modArchiveClose(typedArchive);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}
	s32 parsed = iniParseBuffer(leaf ? leaf : descriptorRel, iniBytes, iniSize, &archiveIni);
	free(iniBytes);
	if (!parsed) {
		pdmodSetLastError("%s internal descriptor is malformed", descriptorRel);
		modArchiveClose(typedArchive);
		return MODPACK_PDMOD_ERR_LAYOUT;
	}

	ini_section_t *archiveIniPtr = typedArchive ? &archiveIni : NULL;

	switch (type) {
	case ASSET_WEAPON:
	case ASSET_PROJECTILE:
	case ASSET_ENTITY:
	case ASSET_PROP:
	case ASSET_VEHICLE:
	case ASSET_HEAD:
	case ASSET_BODY:
	case ASSET_MODEL:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel,
				type == ASSET_PROJECTILE || type == ASSET_ENTITY ? behaviorAssetKeys : modelKeys,
				type == ASSET_PROJECTILE || type == ASSET_ENTITY
					? (s32)(sizeof(behaviorAssetKeys) / sizeof(behaviorAssetKeys[0]))
					: (s32)(sizeof(modelKeys) / sizeof(modelKeys[0])),
				NULL, 0);
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			type == ASSET_PROJECTILE || type == ASSET_ENTITY ? behaviorAssetKeys : modelKeys,
			type == ASSET_PROJECTILE || type == ASSET_ENTITY
				? (s32)(sizeof(behaviorAssetKeys) / sizeof(behaviorAssetKeys[0]))
				: (s32)(sizeof(modelKeys) / sizeof(modelKeys[0])),
			NULL, 0);
	case ASSET_ARENA:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, arenaKeys,
				(s32)(sizeof(arenaKeys) / sizeof(arenaKeys[0])),
				mapOptionalKeys,
				(s32)(sizeof(mapOptionalKeys) / sizeof(mapOptionalKeys[0])));
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			arenaKeys, (s32)(sizeof(arenaKeys) / sizeof(arenaKeys[0])),
			mapOptionalKeys, (s32)(sizeof(mapOptionalKeys) / sizeof(mapOptionalKeys[0])));
	case ASSET_SCENARIO:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, scenarioKeys,
				(s32)(sizeof(scenarioKeys) / sizeof(scenarioKeys[0])),
				scenarioOptionalKeys,
				(s32)(sizeof(scenarioOptionalKeys) / sizeof(scenarioOptionalKeys[0])));
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			scenarioKeys, (s32)(sizeof(scenarioKeys) / sizeof(scenarioKeys[0])),
			scenarioOptionalKeys,
			(s32)(sizeof(scenarioOptionalKeys) / sizeof(scenarioOptionalKeys[0])));
	case ASSET_MATERIAL:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, NULL, 0,
				materialOptionalKeys,
				(s32)(sizeof(materialOptionalKeys) / sizeof(materialOptionalKeys[0])));
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			NULL, 0, materialOptionalKeys,
			(s32)(sizeof(materialOptionalKeys) / sizeof(materialOptionalKeys[0])));
	case ASSET_EFFECT:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, NULL, 0,
				effectOptionalKeys,
				(s32)(sizeof(effectOptionalKeys) / sizeof(effectOptionalKeys[0])));
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			NULL, 0, effectOptionalKeys,
			(s32)(sizeof(effectOptionalKeys) / sizeof(effectOptionalKeys[0])));
	case ASSET_ANIMATION:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, animationKeys,
				(s32)(sizeof(animationKeys) / sizeof(animationKeys[0])),
				NULL, 0);
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			animationKeys, (s32)(sizeof(animationKeys) / sizeof(animationKeys[0])),
			NULL, 0);
	case ASSET_AUDIO:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, audioKeys,
				(s32)(sizeof(audioKeys) / sizeof(audioKeys[0])),
				NULL, 0);
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			audioKeys, (s32)(sizeof(audioKeys) / sizeof(audioKeys[0])),
			NULL, 0);
	case ASSET_TEXTURE:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, uiKeys,
				(s32)(sizeof(uiKeys) / sizeof(uiKeys[0])),
				NULL, 0);
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			uiKeys, (s32)(sizeof(uiKeys) / sizeof(uiKeys[0])),
			NULL, 0);
	case ASSET_UI:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, uiKeys,
				(s32)(sizeof(uiKeys) / sizeof(uiKeys[0])),
				NULL, 0);
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			uiKeys, (s32)(sizeof(uiKeys) / sizeof(uiKeys[0])),
			NULL, 0);
	case ASSET_FONT:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, fontKeys,
				(s32)(sizeof(fontKeys) / sizeof(fontKeys[0])),
				NULL, 0);
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			fontKeys, (s32)(sizeof(fontKeys) / sizeof(fontKeys[0])),
			NULL, 0);
	case ASSET_LANG:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, langKeys,
				(s32)(sizeof(langKeys) / sizeof(langKeys[0])),
				NULL, 0);
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			langKeys, (s32)(sizeof(langKeys) / sizeof(langKeys[0])),
			NULL, 0);
	case ASSET_GAMEMODE:
	case ASSET_BOT_PROFILE:
	case ASSET_HUD:
	case ASSET_MISSION:
	case ASSET_THEME:
		if (typedArchive) {
			s32 r = validateArchiveDescriptorSources(typedArchive,
				archiveIniPtr, descriptorRel, NULL, 0,
				metadataOptionalKeys,
				(s32)(sizeof(metadataOptionalKeys) / sizeof(metadataOptionalKeys[0])));
			modArchiveClose(typedArchive);
			return r;
		}
		return validateDescriptorSources(srcFolder, descriptorRel,
			NULL, 0, metadataOptionalKeys,
			(s32)(sizeof(metadataOptionalKeys) / sizeof(metadataOptionalKeys[0])));
	default:
		if (typedArchive) {
			modArchiveClose(typedArchive);
		}
		return MODPACK_PDMOD_OK;
	}
}

static s32 validateTypedPdDescriptorsRecurse(const char *srcFolder,
                                             const char *relRoot,
                                             const char *destPath,
                                             u32 *checked)
{
	char absDir[FS_MAXPATH + 1];
	if (relRoot && relRoot[0]) {
		pathJoin(absDir, sizeof(absDir), srcFolder, relRoot);
	} else {
		strncpy(absDir, srcFolder, FS_MAXPATH);
		absDir[FS_MAXPATH] = '\0';
	}

	DIR *d = opendir(absDir);
	if (!d) {
		pdmodSetLastError("Could not scan source folder %s", absDir);
		return MODPACK_PDMOD_ERR_IO;
	}

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		char childAbs[FS_MAXPATH + 1];
		pathJoin(childAbs, sizeof(childAbs), absDir, ent->d_name);
		if (folderShouldSkip(ent->d_name, childAbs, destPath)) {
			continue;
		}

		struct stat st;
		if (stat(childAbs, &st) != 0) {
			continue;
		}

		char childRel[FS_MAXPATH + 1];
		if (relRoot && relRoot[0]) {
			snprintf(childRel, sizeof(childRel), "%s/%s", relRoot, ent->d_name);
		} else {
			strncpy(childRel, ent->d_name, FS_MAXPATH);
			childRel[FS_MAXPATH] = '\0';
		}

		if (S_ISDIR(st.st_mode)) {
			s32 r = validateTypedPdDescriptorsRecurse(srcFolder, childRel,
				destPath, checked);
			if (r != MODPACK_PDMOD_OK) {
				closedir(d);
				return r;
			}
		} else if (S_ISREG(st.st_mode)
				&& (assetArchivePathIsTyped(childRel)
					|| assetArchivePathIsDeprecated(childRel))) {
			s32 r = validateTypedPdDescriptorFile(srcFolder, childRel);
			if (r != MODPACK_PDMOD_OK) {
				closedir(d);
				return r;
			}
			if (checked) {
				(*checked)++;
			}
		}
	}

	closedir(d);
	return MODPACK_PDMOD_OK;
}

typedef struct modpack_sidecar_rule {
	const char *leaf;
	const char *template_kind;
} modpack_sidecar_rule_t;

static s32 processCanonicalFamily(const char *srcFolder, const char *familyRel,
                                  const char *descriptorLeaf, const char *templateKind,
                                  const char *const *requiredKeys, s32 requiredKeyCount,
                                  const char *const *optionalKeys, s32 optionalKeyCount,
                                  const modpack_sidecar_rule_t *sidecars,
                                  s32 sidecarCount, u32 *generated)
{
	char familyAbs[FS_MAXPATH + 1];
	pathJoin(familyAbs, sizeof(familyAbs), srcFolder, familyRel);
	if (!dirExistsLocal(familyAbs)) {
		return MODPACK_PDMOD_OK;
	}

	DIR *d = opendir(familyAbs);
	if (!d) {
		pdmodSetLastError("Could not scan canonical family folder %s", familyRel);
		return MODPACK_PDMOD_ERR_IO;
	}

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (!ent->d_name || ent->d_name[0] == '.') {
			continue;
		}

		char assetAbs[FS_MAXPATH + 1];
		pathJoin(assetAbs, sizeof(assetAbs), familyAbs, ent->d_name);
		if (!dirExistsLocal(assetAbs)) {
			continue;
		}

		char descriptorRel[FS_MAXPATH + 1];
		snprintf(descriptorRel, sizeof(descriptorRel), "%s/%s/%s",
			familyRel, ent->d_name, descriptorLeaf);
		descriptorRel[sizeof(descriptorRel) - 1] = '\0';

		char descriptorAbs[FS_MAXPATH + 1];
		pathJoin(descriptorAbs, sizeof(descriptorAbs), srcFolder, descriptorRel);

		s32 r = writeTemplateIfMissing(descriptorAbs, descriptorRel, templateKind, generated);
		if (r != MODPACK_PDMOD_OK) {
			closedir(d);
			return r;
		}

		for (s32 i = 0; i < sidecarCount; i++) {
			char sidecarRel[FS_MAXPATH + 1];
			snprintf(sidecarRel, sizeof(sidecarRel), "%s/%s/%s",
				familyRel, ent->d_name, sidecars[i].leaf);
			sidecarRel[sizeof(sidecarRel) - 1] = '\0';

			char sidecarAbs[FS_MAXPATH + 1];
			pathJoin(sidecarAbs, sizeof(sidecarAbs), srcFolder, sidecarRel);
			r = writeTemplateIfMissing(sidecarAbs, sidecarRel,
				sidecars[i].template_kind, generated);
			if (r != MODPACK_PDMOD_OK) {
				closedir(d);
				return r;
			}
		}

		r = validateDescriptorSources(srcFolder, descriptorRel,
			requiredKeys, requiredKeyCount, optionalKeys, optionalKeyCount);
		if (r != MODPACK_PDMOD_OK) {
			closedir(d);
			return r;
		}
	}

	closedir(d);
	return MODPACK_PDMOD_OK;
}

static s32 validateExternalFolderLayout(const char *srcFolder, const char *destPath)
{
	u32 generated = 0;
	u32 typedChecked = 0;

	s32 r = validateNoForbiddenBinsRecurse(srcFolder, "", destPath);
	if (r != MODPACK_PDMOD_OK) {
		return r;
	}

	r = validateTypedPdDescriptorsRecurse(srcFolder, "", destPath, &typedChecked);
	if (r != MODPACK_PDMOD_OK) {
		return r;
	}

	static const char *modelKeys[] = { "model_file", "model" };
	static const char *behaviorAssetKeys[] = { "behavior_graph", "graph", "model_file", "model", "file_path" };
	static const char *characterKeys[] = { "bodyfile", "body_file" };
	static const char *arenaKeys[] = { "geometry_file", "geometry" };
	static const char *scenarioKeys[] = {
		"scene_file", "scene", "runtime_source_file",
		"rooms_file", "rooms", "geometry_file", "geometry"
	};
	static const char *animationKeys[] = { "animation_file", "file_path" };
	static const char *audioKeys[] = { "file_path" };
	static const char *uiKeys[] = { "texture_file", "file_path", "texture" };
	static const char *fontKeys[] = { "font_file", "glyphs_file", "file_path", "font" };
	static const char *langKeys[] = { "strings_file", "strings", "strings_tsv", "file_path" };
	static const char *materialOptionalKeys[] = {
		"material_file", "texture_archive", "texture_file", "effect_archive",
		"file_path"
	};
	static const char *effectOptionalKeys[] = {
		"effect_file", "behavior_graph", "graph", "texture_archive",
		"audio_archive", "file_path"
	};
	static const char *metadataOptionalKeys[] = {
		"file_path", "model_file", "texture_file", "theme_file",
		"rules_file", "scenario_archive", "ui_archive", "font_archive",
		"audio_archive", "objectives_file", "briefing_file",
		"mission_graph_file", "graph"
	};
	static const char *mapOptionalKeys[] = {
		"pads_file", "setup_file", "collision_file", "material_file",
		"texture_file", "texture_manifest_file", "blender_scene_file",
		"visual_scene_file", "visual_material_file", "visual_materials_file"
	};
	static const char *scenarioOptionalKeys[] = {
		"collision_file", "collision_source_file", "collision_source",
		"objects_file", "setup_fields_file", "objectives_file", "tiles_file",
		"pads_file", "spawns_file", "volumes_file",
		"navigation_file", "level_graph_file",
		"material_file", "texture_file", "texture_manifest_file",
		"blender_scene_file", "visual_scene_file", "visual_material_file",
		"visual_materials_file", "visual_source_file"
	};

	static const modpack_sidecar_rule_t mapSidecars[] = {
		{ "pads.ini",  "pads.ini" },
		{ "setup.ini", "setup.ini" },
	};
	static const modpack_sidecar_rule_t scenarioSidecars[] = {
		{ "pads.tsv",       "pads.tsv" },
		{ "spawns.tsv",     "spawns.tsv" },
		{ "volumes.tsv",    "volumes.tsv" },
		{ "objects.tsv",    "objects.tsv" },
		{ "objectives.tsv", "objectives.tsv" },
		{ "navigation.ini", "navigation.ini" },
		{ "level.graph.json", "level.graph.json" },
	};

	#define RUN_FAMILY(path, leaf, kind, req, opt, opt_count, side, side_count)  \
		do {                                                                      \
			r = processCanonicalFamily(srcFolder, path, leaf, kind,                \
				req, (s32)(sizeof(req) / sizeof((req)[0])),                        \
				opt, (s32)(opt_count), side, (s32)(side_count), &generated);        \
			if (r != MODPACK_PDMOD_OK) return r;                                  \
		} while (0)
	#define RUN_FAMILY_OPT(path, leaf, kind, opt, side, side_count)              \
		do {                                                                      \
			r = processCanonicalFamily(srcFolder, path, leaf, kind,                \
				NULL, 0, opt, (s32)(sizeof(opt) / sizeof((opt)[0])),               \
				side, (s32)(side_count), &generated);                              \
			if (r != MODPACK_PDMOD_OK) return r;                                  \
		} while (0)

	RUN_FAMILY("weapons", "weapon.ini", "weapon", modelKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("projectiles", "projectile.ini", "projectile", behaviorAssetKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("entities", "entity.ini", "entity", behaviorAssetKeys, NULL, 0, NULL, 0);
	RUN_FAMILY_OPT("materials", "material.ini", "material", materialOptionalKeys, NULL, 0);
	RUN_FAMILY("textures", "texture.ini", "texture", uiKeys, NULL, 0, NULL, 0);
	RUN_FAMILY_OPT("skins", "skin.ini", "skin", metadataOptionalKeys, NULL, 0);
	RUN_FAMILY("characters", "character.ini", "character", characterKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("characters/heads", "head.ini", "head", modelKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("characters/bodies", "body.ini", "body", modelKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("maps", "arena.ini", "arena", arenaKeys,
		mapOptionalKeys, sizeof(mapOptionalKeys) / sizeof(mapOptionalKeys[0]),
		mapSidecars, sizeof(mapSidecars) / sizeof(mapSidecars[0]));
	RUN_FAMILY("scenarios", "scenario.ini", "scenario", scenarioKeys,
		scenarioOptionalKeys, sizeof(scenarioOptionalKeys) / sizeof(scenarioOptionalKeys[0]),
		scenarioSidecars, sizeof(scenarioSidecars) / sizeof(scenarioSidecars[0]));
	RUN_FAMILY("props", "prop.ini", "prop", behaviorAssetKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("vehicles", "vehicle.ini", "vehicle", behaviorAssetKeys, NULL, 0, NULL, 0);
	RUN_FAMILY_OPT("missions", "mission.ini", "mission", metadataOptionalKeys, NULL, 0);
	RUN_FAMILY_OPT("gamemodes", "gamemode.ini", "gamemode", metadataOptionalKeys, NULL, 0);
	RUN_FAMILY_OPT("botprofiles", "botprofile.ini", "botprofile", metadataOptionalKeys, NULL, 0);
	RUN_FAMILY_OPT("bot_profiles", "botprofile.ini", "botprofile", metadataOptionalKeys, NULL, 0);
	RUN_FAMILY_OPT("effects", "effect.ini", "effect", effectOptionalKeys, NULL, 0);
	RUN_FAMILY_OPT("hud", "hud.ini", "hud", metadataOptionalKeys, NULL, 0);
	RUN_FAMILY_OPT("themes", "theme.ini", "theme", metadataOptionalKeys, NULL, 0);
	RUN_FAMILY("audio/sfx", "sound.ini", "sfx", audioKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("audio/voice", "voice.ini", "voice", audioKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("audio/music", "music.ini", "music", audioKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("ui", "ui.ini", "ui", uiKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("fonts", "font.ini", "font", fontKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("lang", "lang.ini", "lang", langKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("animations/weapon", "animation.ini", "animation", animationKeys, NULL, 0, NULL, 0);
	RUN_FAMILY("animations/character", "animation.ini", "animation", animationKeys, NULL, 0, NULL, 0);

	#undef RUN_FAMILY
	#undef RUN_FAMILY_OPT

	if (generated > 0) {
		sysLogPrintf(LOG_NOTE,
			"MODPACK.PDMOD: generated %u missing INI template(s) before packing",
			generated);
	}
	if (typedChecked > 0) {
		sysLogPrintf(LOG_NOTE,
			"MODPACK.PDMOD: validated %u typed .pd* content descriptor(s) before packing",
			typedChecked);
	}

	return MODPACK_PDMOD_OK;
}

/* ----------------------------------------------------------- Public: bulk */

s32 modpackPdmodWriteSingle(const char *out_path,
                             const char *manifest_json, u32 manifest_len,
                             const modpack_entry_t *entries, s32 entry_count)
{
	pdmodClearLastError();
	if (!out_path || !out_path[0] || !manifest_json) {
		pdmodSetLastError("Missing output path or manifest");
		return MODPACK_PDMOD_ERR_OPEN;
	}
	if (entry_count > 0 && !entries) {
		pdmodSetLastError("Entry list is missing");
		return MODPACK_PDMOD_ERR_OPEN;
	}

	for (s32 i = 0; i < entry_count; i++) {
		const modpack_entry_t *e = &entries[i];
		if (!e->entry_name || !e->entry_name[0]) continue;
		if (assetArchivePathIsDeprecated(e->entry_name)) {
			pdmodSetLastError("Deprecated .pdwpn files are not allowed in typed asset archives or .pdmod transport archives: %s",
				e->entry_name);
			return MODPACK_PDMOD_ERR_LAYOUT;
		}
		if (entryHasForbiddenBinPayload(e->entry_name)) {
			pdmodSetLastError("Authored .bin files are not allowed in mod content or .pdmod transport archives: %s",
				e->entry_name);
			return MODPACK_PDMOD_ERR_LAYOUT;
		}
		if (assetArchivePathIsTyped(e->entry_name) && e->data && e->len > 0) {
			char policyErr[256];
			if (assetArchiveValidateBytes(e->data, e->len, e->entry_name,
					ASSET_ARCHIVE_VALIDATE_RELEASE,
					policyErr, sizeof(policyErr)) != 0) {
				pdmodSetLastError("%s", policyErr[0] ? policyErr : "typed asset archive validation failed");
				return MODPACK_PDMOD_ERR_LAYOUT;
			}
			char badNested[FS_MAXPATH + 1];
			if (modArchiveMemFindForbiddenBinPayload(e->data, e->len,
					badNested, sizeof(badNested))) {
				pdmodSetLastError("Authored .bin files are not allowed inside typed asset archives: %s::%s",
					e->entry_name, badNested);
				return MODPACK_PDMOD_ERR_LAYOUT;
			}
		}
	}

	mod_archive_writer_t *w = modArchiveBegin(out_path);
	if (!w) {
		pdmodSetLastError("Could not open destination archive: %s", out_path);
		return MODPACK_PDMOD_ERR_OPEN;
	}

	if (modArchiveAddFileMem(w, "mod.json", manifest_json, manifest_len) != MODARCHIVE_OK) {
		modArchiveAbort(w);
		pdmodSetLastError("Could not write mod.json into archive");
		return MODPACK_PDMOD_ERR_IO;
	}

	for (s32 i = 0; i < entry_count; i++) {
		const modpack_entry_t *e = &entries[i];
		if (!e->entry_name || !e->entry_name[0]) continue;
		/* The reader sanitises entry names; the writer does the same so
		 * we surface "this name is not safe" before producing an archive
		 * that the reader would reject. */
		s32 r = modArchiveAddFileMem(w, e->entry_name, e->data, e->len);
		if (r != MODARCHIVE_OK) {
			modArchiveAbort(w);
			pdmodSetLastError("Could not write archive entry: %s", e->entry_name);
			return MODPACK_PDMOD_ERR_IO;
		}
	}

	/* Comment mirror -- 1024 byte cap covers headline fields even with
	 * heavy escaping. */
	char mirror[1024];
	if (buildCommentMirror(manifest_json, mirror, sizeof(mirror)) > 0) {
		modArchiveSetComment(w, mirror);
	}

	if (modArchiveFinish(w) != MODARCHIVE_OK) {
		pdmodSetLastError("Could not finish archive: %s", out_path);
		return MODPACK_PDMOD_ERR_IO;
	}
	return MODPACK_PDMOD_OK;
}

/* ----------------------------------------------------------- Public: folder */

/* Skip-file rules for the folder packer:
 *   - dotfiles (.modstate, .DS_Store, .git*, ...)
 *   - thumbnail/cache junk that some tools leave behind
 *   - the destination archive itself (in case the caller is re-packing
 *     into the same directory) */
static int folderShouldSkip(const char *leaf, const char *fullPath, const char *destPath)
{
	if (!leaf || leaf[0] == '.') return 1;
	if (destPath && fullPath && strcmp(fullPath, destPath) == 0) return 1;
	if (!strcmp(leaf, "Thumbs.db") || !strcmp(leaf, "desktop.ini")) return 1;
	return 0;
}

/* Recursive walker. `relRoot` is the relative path inside the archive
 * (e.g. "" at the top level, "assets/textures" deeper in). */
static s32 packFolderRecurse(mod_archive_writer_t *w,
                              const char *fsRoot, const char *relRoot,
                              const char *destPath, u32 *bytesAccum)
{
	char absDir[FS_MAXPATH + 1];
	if (relRoot && relRoot[0]) {
		snprintf(absDir, sizeof(absDir), "%s/%s", fsRoot, relRoot);
	} else {
		strncpy(absDir, fsRoot, FS_MAXPATH);
		absDir[FS_MAXPATH] = '\0';
	}

	DIR *d = opendir(absDir);
	if (!d) return MODPACK_PDMOD_ERR_IO;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		char childAbs[FS_MAXPATH + 1];
		snprintf(childAbs, sizeof(childAbs), "%s/%s", absDir, ent->d_name);
		if (folderShouldSkip(ent->d_name, childAbs, destPath)) continue;

		struct stat st;
		if (stat(childAbs, &st) != 0) continue;

		char childRel[FS_MAXPATH + 1];
		if (relRoot && relRoot[0]) {
			snprintf(childRel, sizeof(childRel), "%s/%s", relRoot, ent->d_name);
		} else {
			strncpy(childRel, ent->d_name, FS_MAXPATH);
			childRel[FS_MAXPATH] = '\0';
		}

		if (S_ISDIR(st.st_mode)) {
			s32 r = packFolderRecurse(w, fsRoot, childRel, destPath, bytesAccum);
			if (r != MODPACK_PDMOD_OK) {
				closedir(d);
				return r;
			}
		} else if (S_ISREG(st.st_mode)) {
			if ((u64)*bytesAccum + (u64)st.st_size > 0xFFFFFFFFull) {
				closedir(d);
				pdmodSetLastError("Source folder exceeds 4 GiB while adding %s", childRel);
				return MODPACK_PDMOD_ERR_TOO_BIG;
			}
			*bytesAccum += (u32)st.st_size;
			if (modArchiveAddFileDisk(w, childRel, childAbs) != MODARCHIVE_OK) {
				closedir(d);
				pdmodSetLastError("Could not add archive entry %s", childRel);
				return MODPACK_PDMOD_ERR_IO;
			}
		}
	}

	closedir(d);
	return MODPACK_PDMOD_OK;
}

s32 modpackPdmodFromFolder(const char *src_folder, const char *out_path)
{
	pdmodClearLastError();
	if (!src_folder || !src_folder[0] || !out_path || !out_path[0]) {
		pdmodSetLastError("Missing source folder or output path");
		return MODPACK_PDMOD_ERR_OPEN;
	}

	/* Read mod.json so the comment mirror has source data. The folder
	 * packer also embeds it into the archive via the regular recursion,
	 * but we read it once up front to compute the mirror string. */
	char mfstPath[FS_MAXPATH + 1];
	snprintf(mfstPath, sizeof(mfstPath), "%s/mod.json", src_folder);
	FILE *mf = fopen(mfstPath, "rb");
	if (!mf) {
		pdmodSetLastError("mod.json not found in folder root: %s", mfstPath);
		return MODPACK_PDMOD_ERR_NO_MFST;
	}
	fseek(mf, 0, SEEK_END);
	long mfsize = ftell(mf);
	if (mfsize <= 0 || mfsize > (long)(8 * 1024 * 1024)) {
		fclose(mf);
		pdmodSetLastError("mod.json is empty or too large: %s", mfstPath);
		return MODPACK_PDMOD_ERR_BAD_MFST;
	}
	fseek(mf, 0, SEEK_SET);
	char *mfBuf = (char *)malloc(mfsize + 1);
	if (!mfBuf) {
		fclose(mf);
		pdmodSetLastError("Could not allocate memory for mod.json");
		return MODPACK_PDMOD_ERR_IO;
	}
	if (fread(mfBuf, 1, mfsize, mf) != (size_t)mfsize) {
		free(mfBuf);
		fclose(mf);
		pdmodSetLastError("Could not read mod.json: %s", mfstPath);
		return MODPACK_PDMOD_ERR_IO;
	}
	fclose(mf);
	mfBuf[mfsize] = '\0';

	s32 r = validateExternalFolderLayout(src_folder, out_path);
	if (r != MODPACK_PDMOD_OK) {
		free(mfBuf);
		return r;
	}

	mod_archive_writer_t *w = modArchiveBegin(out_path);
	if (!w) {
		free(mfBuf);
		pdmodSetLastError("Could not open destination archive: %s", out_path);
		return MODPACK_PDMOD_ERR_OPEN;
	}

	u32 bytesAccum = 0;
	r = packFolderRecurse(w, src_folder, "", out_path, &bytesAccum);
	if (r != MODPACK_PDMOD_OK) {
		modArchiveAbort(w);
		free(mfBuf);
		if (!s_pdmodLastError[0]) {
			pdmodSetLastError("Could not pack source folder: %s", src_folder);
		}
		return r;
	}

	char mirror[1024];
	if (buildCommentMirror(mfBuf, mirror, sizeof(mirror)) > 0) {
		modArchiveSetComment(w, mirror);
	}

	if (modArchiveFinish(w) != MODARCHIVE_OK) {
		free(mfBuf);
		pdmodSetLastError("Could not finish archive: %s", out_path);
		return MODPACK_PDMOD_ERR_IO;
	}
	free(mfBuf);
	return MODPACK_PDMOD_OK;
}
