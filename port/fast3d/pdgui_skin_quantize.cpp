/**
 * pdgui_skin_quantize.cpp -- Median-cut color quantization + dithering.
 *
 * Reduces an RGBA32 image to a limited palette (16/32/256 colors) using
 * median-cut quantization, with optional Bayer or Floyd-Steinberg dithering.
 * Used by the skin editor's "Convert to PD Style" feature.
 *
 * Design doc: context/designs/skin-editor-design.md §3.7
 * Batch S-6: Median-cut quantizer + Bayer/Floyd-Steinberg dithering
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/ultratypes.h>

/* ========================================================================
 * Color box for median-cut
 * ======================================================================== */

struct ColorBox {
    s32 rMin, rMax, gMin, gMax, bMin, bMax;
    s32 count;
    s32 *indices;  /* indices into pixel array */
};

/* ========================================================================
 * Bayer 4x4 ordered dither matrix
 * ======================================================================== */

static const float s_Bayer4x4[4][4] = {
    {  0.0f/16.0f,  8.0f/16.0f,  2.0f/16.0f, 10.0f/16.0f },
    { 12.0f/16.0f,  4.0f/16.0f, 14.0f/16.0f,  6.0f/16.0f },
    {  3.0f/16.0f, 11.0f/16.0f,  1.0f/16.0f,  9.0f/16.0f },
    { 15.0f/16.0f,  7.0f/16.0f, 13.0f/16.0f,  5.0f/16.0f }
};

/* ========================================================================
 * Helpers
 * ======================================================================== */

static inline s32 clampInt(s32 v, s32 lo, s32 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline s32 colorDist(s32 r1, s32 g1, s32 b1, s32 r2, s32 g2, s32 b2) {
    s32 dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
    return dr*dr + dg*dg + db*db;
}

/* Find nearest palette color */
static s32 findNearest(s32 r, s32 g, s32 b,
                       const u8 *palette, s32 numColors) {
    s32 bestIdx = 0;
    s32 bestDist = 0x7FFFFFFF;
    for (s32 i = 0; i < numColors; i++) {
        s32 d = colorDist(r, g, b,
                          palette[i*3], palette[i*3+1], palette[i*3+2]);
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }
    return bestIdx;
}

/* ========================================================================
 * Median-cut quantization
 * ======================================================================== */

static void medianCut(const u8 *src, s32 totalPixels, s32 maxColors,
                      u8 *paletteOut, s32 *numColorsOut)
{
    /* Allocate index array for all non-transparent pixels */
    s32 *allIdx = (s32 *)malloc(sizeof(s32) * totalPixels);
    s32 opaqueCount = 0;

    for (s32 i = 0; i < totalPixels; i++) {
        if (src[i*4+3] > 0) {  /* skip fully transparent */
            allIdx[opaqueCount++] = i;
        }
    }

    if (opaqueCount == 0) {
        paletteOut[0] = paletteOut[1] = paletteOut[2] = 0;
        *numColorsOut = 1;
        free(allIdx);
        return;
    }

    /* Start with one box containing all opaque pixels */
    s32 maxBoxes = maxColors;
    ColorBox *boxes = (ColorBox *)malloc(sizeof(ColorBox) * maxBoxes);
    s32 numBoxes = 1;

    boxes[0].indices = allIdx;
    boxes[0].count = opaqueCount;

    /* Compute initial bounds */
    auto computeBounds = [&](ColorBox &box) {
        box.rMin = 255; box.rMax = 0;
        box.gMin = 255; box.gMax = 0;
        box.bMin = 255; box.bMax = 0;
        for (s32 i = 0; i < box.count; i++) {
            s32 px = box.indices[i];
            s32 r = src[px*4], g = src[px*4+1], b = src[px*4+2];
            if (r < box.rMin) box.rMin = r; if (r > box.rMax) box.rMax = r;
            if (g < box.gMin) box.gMin = g; if (g > box.gMax) box.gMax = g;
            if (b < box.bMin) box.bMin = b; if (b > box.bMax) box.bMax = b;
        }
    };
    computeBounds(boxes[0]);

    /* Iteratively split the largest-range box */
    while (numBoxes < maxColors) {
        /* Find box with largest range */
        s32 bestBox = -1;
        s32 bestRange = 0;
        for (s32 i = 0; i < numBoxes; i++) {
            if (boxes[i].count <= 1) continue;
            s32 rR = boxes[i].rMax - boxes[i].rMin;
            s32 gR = boxes[i].gMax - boxes[i].gMin;
            s32 bR = boxes[i].bMax - boxes[i].bMin;
            s32 maxR = rR > gR ? (rR > bR ? rR : bR) : (gR > bR ? gR : bR);
            if (maxR > bestRange) { bestRange = maxR; bestBox = i; }
        }

        if (bestBox < 0) break;  /* all boxes are single-color */

        ColorBox &box = boxes[bestBox];

        /* Choose axis with largest range */
        s32 rR = box.rMax - box.rMin;
        s32 gR = box.gMax - box.gMin;
        s32 bR = box.bMax - box.bMin;
        s32 axis = (rR >= gR && rR >= bR) ? 0 : (gR >= bR ? 1 : 2);

        /* Sort indices along chosen axis (simple insertion sort — small arrays) */
        for (s32 i = 1; i < box.count; i++) {
            s32 key = box.indices[i];
            s32 keyVal = src[key*4 + axis];
            s32 j = i - 1;
            while (j >= 0 && src[box.indices[j]*4 + axis] > keyVal) {
                box.indices[j+1] = box.indices[j];
                j--;
            }
            box.indices[j+1] = key;
        }

        /* Split at median */
        s32 mid = box.count / 2;

        /* Create new box from upper half */
        ColorBox &newBox = boxes[numBoxes];
        newBox.indices = box.indices + mid;
        newBox.count = box.count - mid;
        box.count = mid;

        computeBounds(box);
        computeBounds(newBox);
        numBoxes++;
    }

    /* Extract palette: average color of each box */
    for (s32 i = 0; i < numBoxes; i++) {
        s32 rSum = 0, gSum = 0, bSum = 0;
        for (s32 j = 0; j < boxes[i].count; j++) {
            s32 px = boxes[i].indices[j];
            rSum += src[px*4];
            gSum += src[px*4+1];
            bSum += src[px*4+2];
        }
        s32 n = boxes[i].count > 0 ? boxes[i].count : 1;
        paletteOut[i*3]   = (u8)(rSum / n);
        paletteOut[i*3+1] = (u8)(gSum / n);
        paletteOut[i*3+2] = (u8)(bSum / n);
    }

    *numColorsOut = numBoxes;

    /* boxes[0].indices points to allIdx, which owns the memory */
    free(allIdx);
    free(boxes);
}

/* ========================================================================
 * Public API
 * ======================================================================== */

extern "C" {

void skinQuantize(const u8 *src, u8 *dst, s32 w, s32 h,
                  s32 max_colors, s32 dither_mode)
{
    s32 totalPixels = w * h;
    if (totalPixels == 0) return;

    /* Build palette via median-cut */
    u8 palette[256 * 3];
    s32 numColors = 0;
    medianCut(src, totalPixels, max_colors, palette, &numColors);
    if (numColors == 0) { memcpy(dst, src, totalPixels * 4); return; }

    if (dither_mode == 0) {
        /* No dithering — simple nearest-color mapping */
        for (s32 i = 0; i < totalPixels; i++) {
            if (src[i*4+3] == 0) {
                dst[i*4] = dst[i*4+1] = dst[i*4+2] = dst[i*4+3] = 0;
                continue;
            }
            s32 idx = findNearest(src[i*4], src[i*4+1], src[i*4+2],
                                  palette, numColors);
            dst[i*4]   = palette[idx*3];
            dst[i*4+1] = palette[idx*3+1];
            dst[i*4+2] = palette[idx*3+2];
            dst[i*4+3] = src[i*4+3];
        }
    }
    else if (dither_mode == 1) {
        /* Bayer 4x4 ordered dithering */
        for (s32 y = 0; y < h; y++) {
            for (s32 x = 0; x < w; x++) {
                s32 i = y * w + x;
                if (src[i*4+3] == 0) {
                    dst[i*4] = dst[i*4+1] = dst[i*4+2] = dst[i*4+3] = 0;
                    continue;
                }

                float threshold = s_Bayer4x4[y % 4][x % 4] - 0.5f;
                float spread = 255.0f / (float)numColors;

                s32 r = clampInt((s32)(src[i*4]   + threshold * spread), 0, 255);
                s32 g = clampInt((s32)(src[i*4+1] + threshold * spread), 0, 255);
                s32 b = clampInt((s32)(src[i*4+2] + threshold * spread), 0, 255);

                s32 idx = findNearest(r, g, b, palette, numColors);
                dst[i*4]   = palette[idx*3];
                dst[i*4+1] = palette[idx*3+1];
                dst[i*4+2] = palette[idx*3+2];
                dst[i*4+3] = src[i*4+3];
            }
        }
    }
    else {
        /* Floyd-Steinberg error diffusion */
        /* Work on a float copy to accumulate error */
        float *errBuf = (float *)calloc(totalPixels * 3, sizeof(float));
        for (s32 i = 0; i < totalPixels; i++) {
            errBuf[i*3]   = (float)src[i*4];
            errBuf[i*3+1] = (float)src[i*4+1];
            errBuf[i*3+2] = (float)src[i*4+2];
        }

        for (s32 y = 0; y < h; y++) {
            for (s32 x = 0; x < w; x++) {
                s32 i = y * w + x;
                if (src[i*4+3] == 0) {
                    dst[i*4] = dst[i*4+1] = dst[i*4+2] = dst[i*4+3] = 0;
                    continue;
                }

                s32 r = clampInt((s32)errBuf[i*3],   0, 255);
                s32 g = clampInt((s32)errBuf[i*3+1], 0, 255);
                s32 b = clampInt((s32)errBuf[i*3+2], 0, 255);

                s32 idx = findNearest(r, g, b, palette, numColors);
                dst[i*4]   = palette[idx*3];
                dst[i*4+1] = palette[idx*3+1];
                dst[i*4+2] = palette[idx*3+2];
                dst[i*4+3] = src[i*4+3];

                /* Compute quantization error */
                float er = r - (float)palette[idx*3];
                float eg = g - (float)palette[idx*3+1];
                float eb = b - (float)palette[idx*3+2];

                /* Distribute error to neighbors */
                #define DIST_ERR(dx, dy, frac) do { \
                    s32 nx = x + (dx), ny = y + (dy); \
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) { \
                        s32 ni = ny * w + nx; \
                        errBuf[ni*3]   += er * (frac); \
                        errBuf[ni*3+1] += eg * (frac); \
                        errBuf[ni*3+2] += eb * (frac); \
                    } \
                } while(0)

                DIST_ERR( 1, 0, 7.0f/16.0f);
                DIST_ERR(-1, 1, 3.0f/16.0f);
                DIST_ERR( 0, 1, 5.0f/16.0f);
                DIST_ERR( 1, 1, 1.0f/16.0f);

                #undef DIST_ERR
            }
        }
        free(errBuf);
    }
}

} /* extern "C" */
