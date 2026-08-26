/**
 * modasset_material_order.h -- ordered OBJ/MTL material identity planning.
 *
 * OBJ only names materials when geometry uses them.  MTL, by contrast, is the
 * authoritative ordered declaration table consumed by indexed render metadata.
 * This helper preserves the complete declaration domain (including unused
 * slots) and computes the old OBJ-index -> declared-index remap.
 */

#ifndef _IN_MODASSET_MATERIAL_ORDER_H
#define _IN_MODASSET_MATERIAL_ORDER_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODASSET_MATERIAL_NAME_CAP 64

typedef struct modasset_material_order {
	char (*names)[MODASSET_MATERIAL_NAME_CAP];
	s32 count;
	s32 *obj_to_declared;
} modasset_material_order_t;

/**
 * Parse ordered `newmtl` declarations and map existing OBJ material indexes.
 *
 * Every OBJ material referenced by a face must have exactly one declaration.
 * Unreferenced OBJ-only entries (notably the parser's synthetic `pd_default`)
 * may be omitted.  Declared-but-unused MTL slots are retained in `out->names`.
 * Returns 1 on success and 0 with a stable diagnostic in `error` on failure.
 */
s32 modAssetMaterialOrderBuild(const char *mtl_text,
	const char *const *obj_material_names,
	const u8 *obj_material_used,
	s32 obj_material_count,
	modasset_material_order_t *out,
	char *error,
	size_t error_cap);

void modAssetMaterialOrderFree(modasset_material_order_t *order);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODASSET_MATERIAL_ORDER_H */
