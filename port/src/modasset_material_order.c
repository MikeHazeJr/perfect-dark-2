/**
 * modasset_material_order.c -- pure ordered OBJ/MTL material identity planner.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "modasset_material_order.h"

static void materialOrderSetError(char *error, size_t error_cap,
	const char *reason)
{
	if (!error || error_cap == 0) {
		return;
	}

	snprintf(error, error_cap, "%s", reason ? reason : "material_order_invalid");
	error[error_cap - 1] = '\0';
}

static const char *materialOrderSkipHorizontal(const char *p,
	const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t')) {
		p++;
	}
	return p;
}

static s32 materialOrderFindName(
	const char (*names)[MODASSET_MATERIAL_NAME_CAP], s32 count,
	const char *name)
{
	for (s32 i = 0; i < count; i++) {
		if (strcmp(names[i], name) == 0) {
			return i;
		}
	}
	return -1;
}

static s32 materialOrderAppendName(modasset_material_order_t *out,
	const char *name, char *error, size_t error_cap)
{
	char (*grown)[MODASSET_MATERIAL_NAME_CAP];
	size_t len = strlen(name);

	if (len == 0) {
		materialOrderSetError(error, error_cap,
			"material_declaration_missing_name");
		return 0;
	}
	if (len >= MODASSET_MATERIAL_NAME_CAP) {
		materialOrderSetError(error, error_cap,
			"material_declaration_name_too_long");
		return 0;
	}
	if (materialOrderFindName(out->names, out->count, name) >= 0) {
		materialOrderSetError(error, error_cap,
			"material_declaration_duplicate");
		return 0;
	}

	grown = realloc(out->names,
		(size_t)(out->count + 1) * sizeof(*out->names));
	if (!grown) {
		materialOrderSetError(error, error_cap,
			"material_declaration_alloc_failed");
		return 0;
	}
	out->names = grown;
	memset(out->names[out->count], 0, sizeof(out->names[out->count]));
	memcpy(out->names[out->count], name, len);
	out->count++;
	return 1;
}

void modAssetMaterialOrderFree(modasset_material_order_t *order)
{
	if (!order) {
		return;
	}
	free(order->names);
	free(order->obj_to_declared);
	memset(order, 0, sizeof(*order));
}

s32 modAssetMaterialOrderBuild(const char *mtl_text,
	const char *const *obj_material_names,
	const u8 *obj_material_used,
	s32 obj_material_count,
	modasset_material_order_t *out,
	char *error,
	size_t error_cap)
{
	const char *line;

	if (error && error_cap > 0) {
		error[0] = '\0';
	}
	if (!mtl_text || !out || obj_material_count < 0
			|| (obj_material_count > 0
				&& (!obj_material_names || !obj_material_used))) {
		materialOrderSetError(error, error_cap, "material_order_invalid_args");
		return 0;
	}

	memset(out, 0, sizeof(*out));
	line = mtl_text;

	while (*line) {
		const char *end = line;
		const char *p;

		while (*end && *end != '\n' && *end != '\r') {
			end++;
		}
		p = materialOrderSkipHorizontal(line, end);

		if ((size_t)(end - p) >= 6 && memcmp(p, "newmtl", 6) == 0
				&& (p + 6 == end || isspace((u8)p[6]))) {
			const char *name_start = materialOrderSkipHorizontal(p + 6, end);
			const char *name_end = name_start;
			char name[MODASSET_MATERIAL_NAME_CAP];
			size_t len;

			while (name_end < end && !isspace((u8)*name_end)
					&& *name_end != '#') {
				name_end++;
			}
			len = (size_t)(name_end - name_start);
			if (len == 0) {
				materialOrderSetError(error, error_cap,
					"material_declaration_missing_name");
				goto fail;
			}
			if (len >= sizeof(name)) {
				materialOrderSetError(error, error_cap,
					"material_declaration_name_too_long");
				goto fail;
			}
			memcpy(name, name_start, len);
			name[len] = '\0';
			if (!materialOrderAppendName(out, name, error, error_cap)) {
				goto fail;
			}
		}

		line = end;
		while (*line == '\n' || *line == '\r') {
			line++;
		}
	}

	if (out->count == 0) {
		materialOrderSetError(error, error_cap,
			"material_declaration_table_empty");
		goto fail;
	}

	if (obj_material_count > 0) {
		out->obj_to_declared = malloc(
			(size_t)obj_material_count * sizeof(*out->obj_to_declared));
		if (!out->obj_to_declared) {
			materialOrderSetError(error, error_cap,
				"material_remap_alloc_failed");
			goto fail;
		}
	}

	for (s32 i = 0; i < obj_material_count; i++) {
		const char *name = obj_material_names[i];
		s32 declared_index;

		if (!name || !name[0]) {
			materialOrderSetError(error, error_cap,
				"obj_material_name_missing");
			goto fail;
		}
		for (s32 j = 0; j < i; j++) {
			if (strcmp(obj_material_names[j], name) == 0) {
				materialOrderSetError(error, error_cap,
					"obj_material_name_duplicate");
				goto fail;
			}
		}

		declared_index = materialOrderFindName(out->names, out->count, name);
		out->obj_to_declared[i] = declared_index;
		if (obj_material_used[i] && declared_index < 0) {
			materialOrderSetError(error, error_cap,
				"obj_used_material_not_declared");
			goto fail;
		}
	}

	return 1;

fail:
	modAssetMaterialOrderFree(out);
	return 0;
}
