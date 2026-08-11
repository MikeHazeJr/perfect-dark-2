#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "effect_dependencies.h"
#include "fs.h"
#include "modarchive.h"
#include "pdeffect_source.h"
#include "weapon_graph_runtime.h"
#include "system.h"

static void depError(char *error, size_t cap, const char *message)
{
	if (error && cap) snprintf(error, cap, "%s", message);
}

static s32 depAdd(effect_dependency_list_t *out,
	asset_type_e type, const char *id, char *error, size_t error_cap)
{
	size_t i;
	if (!id || !id[0]) {
		depError(error, error_cap,
			"declared effect dependency must not be empty");
		return 0;
	}
	if (!pdEffectCatalogIdValid(id)) {
		depError(error, error_cap, "effect dependency is not an exact catalog ID");
		return 0;
	}
	for (i = 0; i < out->count; i++) {
		if (!strcmp(out->items[i].catalog_id, id)) {
			if (out->items[i].type != type) {
				depError(error, error_cap, "effect dependency has conflicting types");
				return 0;
			}
			return 1;
		}
	}
	if (out->count == out->capacity) {
		size_t next = out->capacity ? out->capacity * 2 : 16;
		if (next < out->count + 1 || next > SIZE_MAX / sizeof(*out->items)) {
			depError(error, error_cap, "effect dependency table size overflow");
			return 0;
		}
		effect_dependency_t *grown = (effect_dependency_t *)realloc(out->items,
			next * sizeof(*out->items));
		if (!grown) {
			depError(error, error_cap, "out of memory growing effect dependencies");
			return 0;
		}
		out->items = grown;
		out->capacity = next;
	}
	out->items[out->count].type = type;
	memcpy(out->items[out->count].catalog_id, id, strlen(id) + 1);
	out->count++;
	return 1;
}

static s32 depParamRole(const weapon_graph_ir_node_t *node,
	const weapon_graph_ir_param_t *param, asset_type_e *type,
	char *error, size_t error_cap)
{
	static const char *recognized[] = {
		"audio_catalog_id", "material_ref", "texture_ref", "effect_ref",
		"audio", "sound", "sound_ref", "material", "material_id",
		"texture", "texture_id", "dependency"
	};
	size_t i;
	*type = ASSET_NONE;
	if (!strcmp(node->kind, "effect.explosion")
			&& !strcmp(param->key, "audio_catalog_id")) *type = ASSET_AUDIO;
	else if ((!strcmp(node->kind, "effect.tint")
			|| !strcmp(node->kind, "effect.glow")
			|| !strcmp(node->kind, "effect.shimmer")
			|| !strcmp(node->kind, "effect.darken")
			|| !strcmp(node->kind, "effect.screen")
			|| !strcmp(node->kind, "effect.particle")
			|| !strcmp(node->kind, "effect.explosion")
			|| !strcmp(node->kind, "effect.spark")
			|| !strcmp(node->kind, "effect.smoke"))
			&& !strcmp(param->key, "material_ref")) *type = ASSET_MATERIAL;
	else if (!strcmp(node->kind, "effect.screen")
			&& !strcmp(param->key, "texture_ref")) *type = ASSET_TEXTURE;
	else if (!strcmp(node->kind, "effect.particle")
			&& !strcmp(param->key, "texture_ref")) *type = ASSET_TEXTURE;
	if (*type != ASSET_NONE) {
		if (param->type != WEAPON_GRAPH_PARAM_STRING) {
			depError(error, error_cap, "typed effect dependency must be a catalog-ID string");
			return -1;
		}
		return 1;
	}
	for (i = 0; i < sizeof(recognized) / sizeof(recognized[0]); i++) {
		if (!strcmp(param->key, recognized[i])) {
			depError(error, error_cap,
				"effect dependency key is unsupported for this node kind");
			return -1;
		}
	}
	return 0;
}

s32 effectDependenciesCollectArchiveBytes(const void *bytes, u32 size,
	const char *expected, effect_dependency_list_t *out,
	char *error, size_t error_cap)
{
	pd_effect_source_info_t source;
	void *graph = NULL;
	u32 graph_size = 0;

	if (error && error_cap) error[0] = '\0';
	if (!bytes || !size || !out
			|| !pdEffectSourceParseArchiveBytes(bytes, size, expected, &source,
				error, error_cap)) return -1;
	if (!source.effect_file[0]) return 0;
	graph = modArchiveExtractMemAlloc(bytes, size, source.effect_file, &graph_size);
	if (!graph || !graph_size) {
		free(graph);
		depError(error, error_cap, "effect dependency graph member is missing");
		return -1;
	}

	if (source.format == PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY) {
		pd_effect_profile_library_t library;
		size_t i;
		memset(&library, 0, sizeof(library));
		if (!pdEffectSourceDecodeProfileLibrary((const char *)graph, graph_size,
				&source, &library, error, error_cap)) {
			free(graph);
			return -1;
		}
		if (library.kind == PD_EFFECT_PROFILE_EXPLOSION) {
			if (!depAdd(out, ASSET_EFFECT,
					"base:effect_smoke_profiles", error, error_cap)) {
				pdEffectSourceFreeProfileLibrary(&library);
				free(graph);
				return -1;
			}
			for (i = 0; i < library.count; i++) {
				const pd_effect_explosion_profile_t *row = &library.explosions[i];
				if (row->has_audio && !depAdd(out, ASSET_AUDIO,
						row->audio_catalog_id, error, error_cap)) {
					pdEffectSourceFreeProfileLibrary(&library);
					free(graph);
					return -1;
				}
			}
		}
		pdEffectSourceFreeProfileLibrary(&library);
	} else {
		weapon_graph_ir_t ir;
		s32 i;
		memset(&ir, 0, sizeof(ir));
		if (weaponGraphCompileJson(ASSET_EFFECT, (const char *)graph, graph_size,
				&ir, error, error_cap) != 0) {
			free(graph);
			return -1;
		}
		for (i = 0; i < ir.node_count; i++) {
			const weapon_graph_ir_node_t *node = &ir.nodes[i];
			s32 j;
			for (j = 0; j < node->param_count; j++) {
				const weapon_graph_ir_param_t *param = &ir.params[node->param_start + j];
				asset_type_e type;
				s32 role = depParamRole(node, param, &type, error, error_cap);
				if (role < 0 || (role > 0 && !depAdd(out, type, param->value,
							error, error_cap))) {
					weaponGraphIrFree(&ir);
					free(graph);
					return -1;
				}
			}
		}
		weaponGraphIrFree(&ir);
	}

	free(graph);
	if (out->count > INT_MAX) {
		depError(error, error_cap, "effect dependency count exceeds API range");
		return -1;
	}
	return (s32)out->count;
}

s32 effectDependenciesCollectArchiveFile(const char *path, const char *expected,
	effect_dependency_list_t *out, char *error, size_t error_cap)
{
	u32 size = 0;
	void *bytes = NULL;
	FILE *fp = path ? fopen(path, "rb") : NULL;
	s32 result;
	s32 sysmem_owned = 0;
	if (fp) {
		if (fseek(fp, 0, SEEK_END) == 0) {
			long end = ftell(fp);
			if (end > 0 && end <= 64 * 1024 * 1024 && fseek(fp, 0, SEEK_SET) == 0) {
				bytes = malloc((size_t)end);
				if (bytes && fread(bytes, 1, (size_t)end, fp) == (size_t)end)
					size = (u32)end;
				else { free(bytes); bytes = NULL; }
			}
		}
		fclose(fp);
	}
	if (!bytes) {
		bytes = path ? fsFileLoad(path, &size) : NULL;
		sysmem_owned = bytes != NULL;
	}
	if (!bytes || !size) {
		if (sysmem_owned) sysMemFree(bytes); else free(bytes);
		depError(error, error_cap, "cannot load effect archive dependencies");
		return -1;
	}
	result = effectDependenciesCollectArchiveBytes(bytes, size, expected, out,
		error, error_cap);
	if (sysmem_owned) sysMemFree(bytes); else free(bytes);
	return result;
}

void effectDependenciesFree(effect_dependency_list_t *list)
{
	if (!list) return;
	free(list->items);
	memset(list, 0, sizeof(*list));
}
