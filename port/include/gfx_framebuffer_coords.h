#ifndef GFX_FRAMEBUFFER_COORDS_H
#define GFX_FRAMEBUFFER_COORDS_H

/* Offscreen commands use the target's original pixel coordinates. Convert
 * the bottom edge from top-origin GBI coordinates to OpenGL's bottom origin,
 * then apply only that target's optional resolution scale. */
typedef struct {
    float x, y, width, height;
} GfxFramebufferCoords;

static inline GfxFramebufferCoords gfxFramebufferMapCoords(
        GfxFramebufferCoords area, unsigned originalWidth,
        unsigned originalHeight, unsigned appliedWidth, unsigned appliedHeight)
{
    const float scaleX = (float)appliedWidth / (float)originalWidth;
    const float scaleY = (float)appliedHeight / (float)originalHeight;
    area.x *= scaleX;
    area.y = ((float)originalHeight - area.y) * scaleY;
    area.width *= scaleX;
    area.height *= scaleY;
    return area;
}

#endif
