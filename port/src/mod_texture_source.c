#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "asset_source_debug.h"
#include "fs.h"
#include "mod.h"
#include "system.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../external/stb_image.h"

static s32 modTexturePathHasImageExtension(const char *path)
{
	const char *leaf;
	const char *dot;

	if (!path || !path[0]) {
		return 0;
	}

	leaf = strstr(path, "::");
	if (leaf) {
		leaf += 2;
	} else {
		const char *slash = strrchr(path, '/');
		const char *backslash = strrchr(path, '\\');
		leaf = slash;
		if (backslash && (!leaf || backslash > leaf)) {
			leaf = backslash;
		}
		leaf = leaf ? leaf + 1 : path;
	}

	dot = strrchr(leaf, '.');
	if (!dot) {
		return 0;
	}

	return strcasecmp(dot, ".png") == 0
		|| strcasecmp(dot, ".tga") == 0
		|| strcasecmp(dot, ".jpg") == 0
		|| strcasecmp(dot, ".jpeg") == 0
		|| strcasecmp(dot, ".bmp") == 0;
}

static void modTextureFatalPublicSourceFailure(
	u16 num, const CatalogResolveResult *r, const char *reason)
{
	const asset_entry_t *entry = r ? assetCatalogGetByIndex(r->catalog_id) : NULL;
	sysFatalError("ASSET.SOURCE_ONLY: texture %d maps to public image source '%s' "
	              "for texture '%s' but %s; refusing ROM/static fallback.",
	              (s32)num, r && r->path ? r->path : "?",
	              entry ? entry->id : "?", reason ? reason : "load failed");
}

s32 modTextureLoadRgba32Source(u16 num, mod_texture_rgba32_source_t *out)
{
	CatalogResolveResult r;
	const asset_entry_t *entry;
	u32 file_size = 0;
	u8 *file_data;
	int width = 0;
	int height = 0;
	u8 *rgba;
	s32 stride_pixels;
	s32 data_size;
	u8 *pixels;

	if (!out) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->catalog_id = -1;

	r = catalogResolveTexture((s32)num);
	if (r.source_only_blocked) {
		const asset_entry_t *blocked = assetCatalogGetByIndex(r.catalog_id);
		sysFatalError("ASSET.SOURCE_ONLY: texture %d maps to '%s' but has "
		              "no public FileProvider source; refusing ROM/static fallback.",
		              (s32)num, blocked ? blocked->id : "?");
		return -1;
	}

	if (!r.is_mod_override || !r.path) {
		return 0;
	}

	entry = assetCatalogGetByIndex(r.catalog_id);
	{
		extern s32 g_NotLoadMod;
		if (g_NotLoadMod && (!entry || !entry->bundled)) {
			return 0;
		}
	}

	if (!modTexturePathHasImageExtension(r.path)) {
		if (assetSourceDebugIsEnabledFor(ASSET_TEXTURE)) {
			modTextureFatalPublicSourceFailure(num, &r,
				"the selected public source is not an editable image source");
			return -1;
		}
		return 0;
	}

	file_data = (u8 *)fsFileLoad(r.path, &file_size);
	if (!file_data || file_size == 0) {
		if (file_data) {
			sysMemFree(file_data);
		}
		modTextureFatalPublicSourceFailure(num, &r, "the image file could not be read");
		return -1;
	}
	if (file_size > 0x7fffffffu) {
		sysMemFree(file_data);
		modTextureFatalPublicSourceFailure(num, &r, "the image file is too large to decode");
		return -1;
	}

	rgba = stbi_load_from_memory(file_data, (int)file_size,
		&width, &height, NULL, 4);
	sysMemFree(file_data);
	if (!rgba || width <= 0 || height <= 0) {
		if (rgba) {
			stbi_image_free(rgba);
		}
		modTextureFatalPublicSourceFailure(num, &r, "the image could not be decoded");
		return -1;
	}

	if (width > 255 || height > 255) {
		stbi_image_free(rgba);
		modTextureFatalPublicSourceFailure(num, &r, "the image exceeds the 255x255 runtime texture header limit");
		return -1;
	}

	stride_pixels = (width + 3) & ~3;
	if (height > 0x7fffffff / (stride_pixels * 4)) {
		stbi_image_free(rgba);
		modTextureFatalPublicSourceFailure(num, &r, "the image dimensions overflow runtime storage");
		return -1;
	}
	data_size = stride_pixels * height * 4;
	pixels = (u8 *)sysMemAlloc((u32)data_size);
	if (!pixels) {
		stbi_image_free(rgba);
		modTextureFatalPublicSourceFailure(num, &r, "runtime texture allocation failed");
		return -1;
	}
	memset(pixels, 0, (size_t)data_size);

	for (s32 y = 0; y < height; y++) {
		u32 *dst = (u32 *)(pixels + (size_t)y * (size_t)stride_pixels * 4u);
		const u8 *src = rgba + (size_t)y * (size_t)width * 4u;
		for (s32 x = 0; x < width; x++) {
			const u8 r8 = src[x * 4 + 0];
			const u8 g8 = src[x * 4 + 1];
			const u8 b8 = src[x * 4 + 2];
			const u8 a8 = src[x * 4 + 3];
			dst[x] = ((u32)r8 << 24) | ((u32)g8 << 16) | ((u32)b8 << 8) | (u32)a8;
		}
	}

	stbi_image_free(rgba);

	out->pixels = pixels;
	out->width = width;
	out->height = height;
	out->stride_pixels = stride_pixels;
	out->data_size = data_size;
	out->catalog_id = r.catalog_id;
	out->path = r.path;

	sysLogPrintf(LOG_NOTE,
	             "CATALOG: tex %d -> public image source \"%s\" (entry %d, %dx%d RGBA32)",
	             (s32)num, r.path, r.catalog_id, width, height);
	return 1;
}

void modTextureFreeRgba32Source(mod_texture_rgba32_source_t *source)
{
	if (!source) {
		return;
	}
	if (source->pixels) {
		sysMemFree(source->pixels);
	}
	memset(source, 0, sizeof(*source));
	source->catalog_id = -1;
}
