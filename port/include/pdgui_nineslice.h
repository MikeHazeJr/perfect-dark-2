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
 * Source insets (src_*) are measured in SOURCE TEXTURE pixels and drive
 * UV calculation.  Destination corners (dst_corner_*) are measured in
 * DESTINATION screen pixels and drive vertex positioning.  Splitting the
 * two lets an HD source asset (say 256px corner in 512px source) render
 * at a small destination corner size (say 16px) without scaling the
 * center region awkwardly.
 *
 * Backwards-compatible short form: authors may specify `left/right/top/
 * bottom` only.  Those fields are used for both source and destination
 * when the `has_split` flag is 0 (the parser sets has_split=1 when the
 * dst_corner_* fields are explicitly present in the manifest).
 *
 * Fill modes:
 *   - center_mode          STRETCH or TILE in both axes
 *   - top_mode / bot_mode  STRETCH or TILE along the horizontal edge
 *   - left_mode / right_mode STRETCH or TILE along the vertical edge
 *   - edge_mode            legacy shortcut applied to all four edges
 *                          when the parser doesn't see per-edge modes.
 *
 * The renderer prefers per-edge modes when `has_per_edge_mode` is set,
 * and falls back to `edge_mode` otherwise.
 * --------------------------------------------------------------------- */

typedef struct nineslice_def {
    /* Legacy short-form insets (used when has_split == 0) */
    s32 left;
    s32 right;
    s32 top;
    s32 bottom;

    /* Source-texture-pixel insets (drive UV).  Mirrors the short form
     * when has_split == 0. */
    s32 src_left;
    s32 src_right;
    s32 src_top;
    s32 src_bottom;

    /* Destination-pixel corner sizes (drive vertex layout). */
    s32 dst_left;
    s32 dst_right;
    s32 dst_top;
    s32 dst_bottom;

    /* Fill modes — NINESLICE_STRETCH or NINESLICE_TILE */
    s32 edge_mode;          /* legacy: applied to all edges when has_per_edge_mode == 0 */
    s32 top_mode;
    s32 bottom_mode;
    s32 left_mode;
    s32 right_mode;
    s32 center_mode;

    /* Schema flags */
    s32 has_split;          /* 1 if dst_corner_px was present in manifest */
    s32 has_per_edge_mode;  /* 1 if per-edge top/bottom/left/right modes were present */
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

/** Read-only capacity preflight used by transactional theme activation. */
s32 pdguiNinesliceCanRegister(const char *catalog_id);

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
