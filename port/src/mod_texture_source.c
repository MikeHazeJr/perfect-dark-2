#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <PR/ultratypes.h>
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "fs.h"
#include "mod.h"
#include "system.h"
#include "texture_source_runtime.h"

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

static void modTextureFatalPublicEntryFailure(u16 num,
	const asset_entry_t *entry, const char *path, const char *reason)
{
	sysFatalError("ASSET.SOURCE_ONLY: texture %d maps to public image source '%s' "
	              "for texture '%s' but %s; refusing ROM/static fallback.",
	              (s32)num, path ? path : "?",
	              entry ? entry->id : "?", reason ? reason : "load failed");
}

s32 modTextureDecodeRgba32Source(const void *bytes, u32 size,
    mod_texture_rgba32_source_t *out, char *error, size_t cap)
{
    int width = 0, height = 0;
    u8 *rgba, *pixels;
    s32 stride_pixels, data_size;
    if (error && cap) error[0] = 0;
    if (!out) return -1;
    memset(out, 0, sizeof(*out)); out->catalog_id = -1;
    if (!bytes || !size || size > 0x7fffffffu) {
        if (error && cap) snprintf(error, cap, "%s", "invalid image source byte size");
        return -1;
    }
	rgba = stbi_load_from_memory(bytes, (int)size,
		&width, &height, NULL, 4);
	if (!rgba || width <= 0 || height <= 0) {
		if (rgba) {
			stbi_image_free(rgba);
		}
		if (error && cap) snprintf(error, cap, "%s", "the image could not be decoded");
		return -1;
	}

	if (width > 255 || height > 255) {
		stbi_image_free(rgba);
		if (error && cap) snprintf(error, cap, "%s", "the image exceeds the 255x255 runtime texture header limit");
		return -1;
	}

	stride_pixels = (width + 3) & ~3;
	if (height > 0x7fffffff / (stride_pixels * 4)) {
		stbi_image_free(rgba);
		if (error && cap) snprintf(error, cap, "%s", "the image dimensions overflow runtime storage");
		return -1;
	}
	data_size = stride_pixels * height * 4;
	pixels = (u8 *)sysMemAlloc((u32)data_size);
	if (!pixels) {
		stbi_image_free(rgba);
		if (error && cap) snprintf(error, cap, "%s", "runtime texture allocation failed");
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
    return 1;
}

s32 modTextureLoadRgba32Source(u16 num, mod_texture_rgba32_source_t *out)
{
	const asset_entry_t *entry;
	const char *source_path;
	s32 source_catalog_id;
	u32 file_size = 0;
	u8 *file_data;

	if (!out) {
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->catalog_id = -1;

    source_catalog_id = textureSourceRuntimeCatalogIndex((s32)num);
    entry = assetCatalogGetByIndex(source_catalog_id);
    if (!entry) return 0;
    const asset_data_handle_t handle = catalogEffectiveHandle(entry);
    source_path = handle.provider == fileProvider() ? fileProviderPath(handle) : NULL;
    if (!modTexturePathHasImageExtension(source_path)) {
        modTextureFatalPublicEntryFailure(num, entry, source_path,
            "the selected provider is not an editable public image source");
        return -1;
    }

	file_data = (u8 *)fsFileLoad(source_path, &file_size);
	if (!file_data || file_size == 0) {
		if (file_data) {
			sysMemFree(file_data);
		}
		modTextureFatalPublicEntryFailure(num, entry, source_path,
			"the image file could not be read");
		return -1;
	}
	if (file_size > 0x7fffffffu) {
		sysMemFree(file_data);
		modTextureFatalPublicEntryFailure(num, entry, source_path,
			"the image file is too large to decode");
		return -1;
	}

	char decode_error[160];
    texture_source_properties_t properties;
    if (!textureSourceRuntimeReadSelected(entry, source_path, &properties,
            decode_error, sizeof(decode_error))) {
        sysMemFree(file_data);
        modTextureFatalPublicEntryFailure(num, entry, source_path, decode_error);
        return -1;
    }
    s32 decoded = modTextureDecodeRgba32Source(file_data, file_size, out, decode_error, sizeof(decode_error));
    sysMemFree(file_data);
    if (decoded <= 0) {
        modTextureFatalPublicEntryFailure(num, entry, source_path, decode_error);
        return -1;
    }

	out->catalog_id = source_catalog_id;
	out->path = source_path;

	sysLogPrintf(LOG_NOTE,
	             "CATALOG: tex %d -> public image source \"%s\" (entry %d, %dx%d RGBA32)",
	             (s32)num, source_path, source_catalog_id, out->width, out->height);
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
