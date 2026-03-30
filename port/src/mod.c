#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "fs.h"
#include "utils.h"
#include "mod.h"
#include "data.h"
#include "assetcatalog_load.h"

#define MOD_TEXTURES_DIR "textures"
#define MOD_ANIMATIONS_DIR "animations"
#define MOD_SEQUENCES_DIR "sequences"

#define PARSE_INT(sec, name, v, min, max, ret) \
	p = modConfigParseIntValue(p, token, &v); \
	if (!p || v < (min) || v > (max)) { \
		sysLogPrintf(LOG_ERROR, "mod: %s: invalid " name " value: %s", sec, token); \
		return ret; \
	}

static inline char *modConfigParseIntValue(char *p, char *token, s32 *out)
{
	p = strParseToken(p, token, NULL);
	if (!token[0]) {
		return NULL;
	}
	char *endp = token;
	const s32 num = strtol(token, &endp, 0);
	if (num == 0 && (endp == token || *endp != '\0')) {
		return NULL;
	}
	*out = num;
	return p;
}

s32 modTextureLoad(u16 num, void *dst, u32 dstSize)
{
	/* PC: When g_NotLoadMod is set (title screen, CI main menu, solo stages),
	 * suppress mod texture overlay so base-game textures are used.  Without
	 * this check, mod texture packs (GEX, kakariko, etc.) replace textures
	 * globally for ALL stages, causing wrong textures on the CI background
	 * environment and in non-mod multiplayer arenas. */
	extern s32 g_NotLoadMod;
	if (g_NotLoadMod) {
		return -1;
	}

	/* C-5: catalog is primary texture router.
	 * catalogResolveTexture() returns the full routing decision: mod override
	 * (load from path), base-game ROM (catalog_id >= 0), or not cataloged. */
	{
		CatalogResolveResult r = catalogResolveTexture((s32)num);
		if (r.is_mod_override && r.path) {
			const s32 ret = fsFileLoadTo(r.path, dst, dstSize);
			if (ret > 0) {
				sysLogPrintf(LOG_NOTE, "CATALOG: tex %d → mod override \"%s\" (entry %d)",
				             (s32)num, r.path, r.catalog_id);
				return ret;
			}
			sysLogPrintf(LOG_WARNING, "MOD: texture %d catalog override failed (%s), falling back to legacy path",
			             (s32)num, r.path);
		} else if (r.catalog_id >= 0) {
			sysLogPrintf(LOG_NOTE, "CATALOG: tex %d → ROM (entry %d)", (s32)num, r.catalog_id);
		} else {
			sysLogPrintf(LOG_VERBOSE, "CATALOG: tex %d → ROM (not cataloged)", (s32)num);
		}
	}

	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_TEXTURES_DIR "/") >= 0);
	}

	if (!dirExists) {
		return -1;
	}

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_TEXTURES_DIR "/%04x.bin", num);

	const s32 ret = fsFileLoadTo(path, dst, dstSize);
	if (ret > 0) {
		sysLogPrintf(LOG_NOTE, "MOD: texture %d loaded from legacy path", (s32)num);
	}

	return ret;
}

void *modSequenceLoad(u16 num, u32 *outSize)
{
	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_SEQUENCES_DIR "/") >= 0);
	}

	if (!dirExists) {
		return NULL;
	}

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_SEQUENCES_DIR "/%04x.bin", num);
	if (fsFileSize(path) > 0) {
		void *ret = fsFileLoad(path, outSize);
		if (ret) {
			sysLogPrintf(LOG_NOTE, "mod: loaded external sequence %04x", num);
			return ret;
		}
	}

	return NULL;
}

void *modAnimationLoadData(u16 num)
{
	/* C-6: catalog is primary animation router.
	 * catalogResolveAnim() returns the full routing decision: mod override
	 * (load from path), base-game ROM (catalog_id >= 0), or not cataloged. */
	{
		CatalogResolveResult r = catalogResolveAnim((s32)num);
		if (r.is_mod_override && r.path) {
			void *data = fsFileLoad(r.path, NULL);
			if (data) {
				sysLogPrintf(LOG_NOTE, "CATALOG: anim %d → mod override \"%s\" (entry %d)",
				             (s32)num, r.path, r.catalog_id);
				return data;
			}
			sysLogPrintf(LOG_WARNING, "MOD: animation %d catalog override failed (%s), falling back to legacy path",
			             (s32)num, r.path);
		} else if (r.catalog_id >= 0) {
			sysLogPrintf(LOG_NOTE, "CATALOG: anim %d → ROM (entry %d)", (s32)num, r.catalog_id);
		} else {
			sysLogPrintf(LOG_VERBOSE, "CATALOG: anim %d → ROM (not cataloged)", (s32)num);
		}
	}

	/* Legacy path: load from animations/ directory */
	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), MOD_ANIMATIONS_DIR "/%04x.bin", num);
	void *data = fsFileLoad(path, NULL);
	if (!data) {
		sysFatalError("External animation %04x has no data file.\nEnsure that it is placed at %s or delete the descriptor.", num, path);
	}
	return data;
}

s32 modAnimationLoadDescriptor(u16 num, struct animtableentry *anim)
{
	static s32 dirExists = -1;
	if (dirExists < 0) {
		dirExists = (fsFileSize(MOD_ANIMATIONS_DIR "/") >= 0);
	}

	if (!dirExists) {
		return false;
	}

	char path[FS_MAXPATH + 1];

	// load the descriptor, if any
	snprintf(path, sizeof(path), MOD_ANIMATIONS_DIR "/%04x.txt", num);
	if (fsFileSize(path) <= 0) {
		return false;
	}

	char *desc = fsFileLoad(path, NULL);
	if (!desc) {
		return false;
	}

	// parse the descriptor
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char *p = strParseToken(desc, token, NULL);
	s32 tmp = 0;
	while (p && token[0]) {
		if (!strcmp(token, "numframes")) {
			PARSE_INT(path, "numframes", tmp, 0, 0xFFFF, false);
			anim->numframes = tmp;
		} else if (!strcmp(token, "bytesperframe")) {
			PARSE_INT(path, "bytesperframe", tmp, 0, 0xFFFF, false);
			anim->bytesperframe = tmp;
		} else if (!strcmp(token, "headerlen")) {
			PARSE_INT(path, "headerlen", tmp, 0, 0xFFFF, false);
			anim->headerlen = tmp;
		} else if (!strcmp(token, "framelen")) {
			PARSE_INT(path, "framelen", tmp, 0, 0xFF, false);
			anim->framelen = tmp;
		} else if (!strcmp(token, "flags")) {
			PARSE_INT(path, "flags", tmp, 0, 0xFF, false);
			anim->flags = tmp;
		} else {
			sysLogPrintf(LOG_ERROR, "mod: %s: invalid key: %s", path, token);
			return false;
		}
		p = strParseToken(p, token, NULL);
	}

	sysMemFree(desc);

	sysLogPrintf(LOG_NOTE, "mod: loaded external animation %04x", num);

	return true;
}
