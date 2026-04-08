#ifndef _IN_PDGUI_NINESLICE_H
#define _IN_PDGUI_NINESLICE_H

/**
 * pdgui_nineslice.h -- 9-slice (nine-patch) texture renderer for UI panels
 *
 * Given a texture + border insets, renders it stretched correctly at any size
 * without distorting corners. Corners stay fixed-size, edges stretch along
 * one axis, and the center fills the remaining space.
 *
 * Layout:
 *   +-------+-------------------+-------+
 *   | TL    |    Top edge       |   TR  |  <- fixed height (top inset)
 *   +-------+-------------------+-------+
 *   | Left  |    Center         | Right |  <- stretches vertically
 *   | edge  |    (stretch both) | edge  |
 *   +-------+-------------------+-------+
 *   | BL    |    Bottom edge    |   BR  |  <- fixed height (bottom inset)
 *   +-------+-------------------+-------+
 *
 * Insets define the non-stretching border widths in texture pixels.
 * All rendering uses ImGui's draw list API.
 *
 * Part of Phase P4: UI Texture Mod.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Insets definition
 * --------------------------------------------------------------------- */

typedef struct NineSliceInsets {
    f32 left;     /* left border width (texture pixels) */
    f32 right;    /* right border width */
    f32 top;      /* top border height */
    f32 bottom;   /* bottom border height */
} NineSliceInsets;

/* -----------------------------------------------------------------------
 * Core 9-slice draw
 * --------------------------------------------------------------------- */

/**
 * Draw a texture using 9-slice rendering.
 *
 * @param tex      ImTextureID (GL texture, cast from GLuint via uintptr_t)
 * @param pos_x    Screen-space X position
 * @param pos_y    Screen-space Y position
 * @param size_w   Desired width in screen pixels
 * @param size_h   Desired height in screen pixels
 * @param insets   Border insets (corner/edge sizes that don't stretch)
 * @param tex_w    Source texture width in pixels
 * @param tex_h    Source texture height in pixels
 * @param tint     Tint color as ImU32 (IM_COL32). Use 0xFFFFFFFF for no tint.
 */
void pdguiNineSliceDraw(void *tex, f32 pos_x, f32 pos_y,
                        f32 size_w, f32 size_h,
                        const NineSliceInsets *insets,
                        f32 tex_w, f32 tex_h, u32 tint);

/**
 * Draw 9-slice with default white tint (no color modification).
 */
void pdguiNineSliceDrawSimple(void *tex, f32 pos_x, f32 pos_y,
                              f32 size_w, f32 size_h,
                              const NineSliceInsets *insets,
                              f32 tex_w, f32 tex_h);

/**
 * Convenience: draw a PD-themed panel using 9-slice if a nineslice texture
 * is configured for the active theme, otherwise fall back to the solid
 * panel draw from pdgui_theme.cpp.
 *
 * @param x, y, w, h   Screen-space rectangle
 * @param catalog_id    Catalog ID of the 9-slice texture (NULL = use default)
 */
void pdguiNineSlicePanel(f32 x, f32 y, f32 w, f32 h,
                         const char *catalog_id);

/* -----------------------------------------------------------------------
 * Insets I/O
 * --------------------------------------------------------------------- */

/** Default insets for PD-style panels (8px corners). */
NineSliceInsets pdguiNineSliceDefaultInsets(void);

/** Parse insets from a JSON object string: {"left":8,"right":8,"top":8,"bottom":8} */
s32 pdguiNineSliceParseInsets(const char *json, NineSliceInsets *out);

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */

/** Register a 9-slice texture with its insets in the internal registry.
 *  Called by the theme loader when parsing nineslice config from theme.json. */
void pdguiNineSliceRegister(const char *catalog_id, const NineSliceInsets *insets,
                            f32 tex_w, f32 tex_h);

/** Look up registered insets for a catalog ID. Returns NULL if not registered. */
const NineSliceInsets *pdguiNineSliceGetInsets(const char *catalog_id);

/** Get registered texture dimensions. Returns 0 if not found. */
s32 pdguiNineSliceGetTexSize(const char *catalog_id, f32 *out_w, f32 *out_h);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_NINESLICE_H */
