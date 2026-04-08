#ifndef _IN_PDGUI_NINESLICE_H
#define _IN_PDGUI_NINESLICE_H

/**
 * pdgui_nineslice.h -- 9-slice (nine-patch) texture renderer (P4)
 *
 * Renders a texture with preserved corners and stretched/tiled edges+interior.
 * Used for scalable UI panels, buttons, and borders that maintain crisp
 * corners at any size.
 *
 *   +---------+-----------+---------+
 *   | TL      | Top       | TR      |  <- corners: fixed size
 *   +---------+-----------+---------+
 *   | Left    | Center    | Right   |  <- edges: stretch vertically
 *   +---------+-----------+---------+
 *   | BL      | Bottom    | BR      |  <- corners: fixed size
 *   +---------+-----------+---------+
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Border insets defining the 9-slice regions (in texture pixels) */
typedef struct {
    f32 left;    /* left column width */
    f32 top;     /* top row height */
    f32 right;   /* right column width */
    f32 bottom;  /* bottom row height */
} NineSliceInsets;

/* Rendering mode for edges and center */
typedef enum {
    NINESLICE_STRETCH = 0,  /* stretch to fill (default) */
    NINESLICE_TILE    = 1,  /* tile/repeat to fill */
} NineSliceMode;

/* Full 9-slice definition for a texture */
typedef struct {
    NineSliceInsets insets;     /* border sizes in texture pixels */
    NineSliceMode   edgeMode;  /* how to render edges */
    NineSliceMode   fillMode;  /* how to render center */
    f32 texW;                  /* source texture width */
    f32 texH;                  /* source texture height */
} NineSliceDef;

/** Draw a texture using 9-slice rendering.
 *  @param tex    ImTextureID (GL texture cast)
 *  @param x,y    screen position (top-left)
 *  @param w,h    screen size
 *  @param def    9-slice definition (insets, modes, texture dims)
 *  @param tint   tint color (0xRRGGBBAA), or 0xFFFFFFFF for no tint */
void pdguiNineSliceDraw(void *tex, f32 x, f32 y, f32 w, f32 h,
                        const NineSliceDef *def, u32 tint);

/** Draw a texture using 9-slice with default stretch mode.
 *  Convenience wrapper — sets stretch for edges and center. */
void pdguiNineSliceDrawSimple(void *tex, f32 x, f32 y, f32 w, f32 h,
                              f32 texW, f32 texH,
                              f32 insetL, f32 insetT, f32 insetR, f32 insetB,
                              u32 tint);

/** Create a default NineSliceDef with given insets and texture dimensions. */
NineSliceDef pdguiNineSliceMakeDef(f32 texW, f32 texH,
                                    f32 insetL, f32 insetT,
                                    f32 insetR, f32 insetB);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_NINESLICE_H */
