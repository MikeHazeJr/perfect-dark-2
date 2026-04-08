#ifndef _IN_PDGUI_NINESLICE_H
#define _IN_PDGUI_NINESLICE_H

/**
 * pdgui_nineslice.h -- 9-slice texture renderer for PD2 UI (P4)
 *
 * Provides scalable UI panel rendering by dividing a texture into 9 regions:
 *   4 fixed-size corners, 4 stretchable/tileable edges, 1 stretchable/tileable center.
 *
 * Each texture has a nineslice definition stored as JSON alongside it.
 * The definition specifies left/right/top/bottom insets that define the slice grid.
 *
 * Edge and center fill modes:
 *   NINESLICE_STRETCH — scale the region to fill (default)
 *   NINESLICE_TILE    — repeat the region at original pixel density
 *
 * Rendering uses ImGui draw lists for seamless integration with the theme system.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase P4: UI Texture Mod.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Fill mode for edges and center
 * --------------------------------------------------------------------- */

#define NINESLICE_STRETCH  0
#define NINESLICE_TILE     1

/* -----------------------------------------------------------------------
 * 9-slice definition
 *
 * Insets are in texture pixels from each edge.
 * The texture is divided into a 3x3 grid by the insets.
 * --------------------------------------------------------------------- */

typedef struct nineslice_def {
    s32 left;          /* pixels from left edge to first vertical divider */
    s32 right;         /* pixels from right edge to second vertical divider */
    s32 top;           /* pixels from top edge to first horizontal divider */
    s32 bottom;        /* pixels from bottom edge to second horizontal divider */

    s32 edge_mode;     /* NINESLICE_STRETCH or NINESLICE_TILE for edges */
    s32 center_mode;   /* NINESLICE_STRETCH or NINESLICE_TILE for center */
} nineslice_def_t;

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/** Initialize 9-slice system. Call after pdguiThemeInit(). */
void pdguiNinesliceInit(void);

/** Shutdown: free cached definitions. */
void pdguiNinesliceShutdown(void);

/* -----------------------------------------------------------------------
 * Definition management
 * --------------------------------------------------------------------- */

/** Register a 9-slice definition for a catalog texture ID.
 *  Overwrites any previous definition for the same ID.
 *  Returns 1 on success, 0 on failure (registry full). */
s32 pdguiNinesliceRegister(const char *catalog_id, const nineslice_def_t *def);

/** Get the 9-slice definition for a catalog texture ID.
 *  Returns NULL if no definition registered. */
const nineslice_def_t *pdguiNinesliceGet(const char *catalog_id);

/** Load a 9-slice definition from a JSON file.
 *  JSON format: { "left": N, "right": N, "top": N, "bottom": N,
 *                 "edgeMode": "stretch"|"tile", "centerMode": "stretch"|"tile" }
 *  Returns 1 on success, 0 on failure. */
s32 pdguiNinesliceLoadDef(const char *json_path, const char *catalog_id);

/* -----------------------------------------------------------------------
 * Rendering
 * --------------------------------------------------------------------- */

/** Draw a texture using its registered 9-slice definition.
 *  Falls back to simple stretched quad if no definition is registered.
 *  tex_id: catalog ID of the source texture.
 *  x,y,w,h: screen-space destination rectangle.
 *  tint: ImU32 color tint (IM_COL32(255,255,255,255) for no tint).
 */
void pdguiNinesliceDraw(const char *tex_id,
                        float x, float y, float w, float h,
                        u32 tint);

/** Draw using an explicit definition (no registry lookup).
 *  tex: ImTextureID (GL texture handle cast).
 *  tex_w, tex_h: source texture dimensions in pixels.
 *  def: 9-slice definition to use.
 */
void pdguiNinesliceDrawEx(void *tex, s32 tex_w, s32 tex_h,
                          const nineslice_def_t *def,
                          float x, float y, float w, float h,
                          u32 tint);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_NINESLICE_H */
