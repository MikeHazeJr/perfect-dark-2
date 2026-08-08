/**
 * pdgui_nineslice.cpp -- 9-slice texture renderer for PD2 UI (P4)
 *
 * Divides a texture into 9 regions using inset values:
 *
 *   +------+------------------+------+
 *   |  TL  |     TOP EDGE     |  TR  |   <- top inset
 *   +------+------------------+------+
 *   |      |                  |      |
 *   | LEFT |     CENTER       | RIGHT|
 *   |      |                  |      |
 *   +------+------------------+------+
 *   |  BL  |    BOTTOM EDGE   |  BR  |   <- bottom inset
 *   +------+------------------+------+
 *      ^                         ^
 *    left inset              right inset
 *
 * Corners: fixed size, never stretched.
 * Edges: stretch or tile along their axis.
 * Center: stretch or tile in both axes.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <PR/ultratypes.h>

#include "imgui/imgui.h"
#include "pdgui_nineslice.h"
#include "pdgui_theme.h"
#include "assetcatalog.h"
#include "system.h"
#include "fs.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#define NS_MAX_DEFS  128
#define NS_ID_LEN     64

/* =========================================================================
 * Registry
 * ========================================================================= */

struct ns_entry {
    char            catalog_id[NS_ID_LEN];
    nineslice_def_t def;
};

static struct ns_entry s_NsDefs[NS_MAX_DEFS];
static s32 s_NsCount = 0;
static s32 s_NsInitDone = 0;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

static struct ns_entry *s_findEntry(const char *id)
{
    for (s32 i = 0; i < s_NsCount; i++) {
        if (strcmp(s_NsDefs[i].catalog_id, id) == 0)
            return &s_NsDefs[i];
    }
    return nullptr;
}

/* 0xRRGGBBAA → ImU32 (ImGui packed ABGR) */
static inline ImU32 NsCol(u32 rgba)
{
    uint8_t r = (uint8_t)((rgba >> 24) & 0xffu);
    uint8_t g = (uint8_t)((rgba >> 16) & 0xffu);
    uint8_t b = (uint8_t)((rgba >>  8) & 0xffu);
    uint8_t a = (uint8_t)((rgba >>  0) & 0xffu);
    return IM_COL32(r, g, b, a);
}

/**
 * Draw a sub-rectangle of a texture to a destination rectangle.
 * Handles both stretch (single quad) and tile (repeated quads) modes.
 */
static void s_drawRegion(ImDrawList *dl,
                         ImTextureID tex,
                         float src_x, float src_y, float src_w, float src_h,
                         float tex_w, float tex_h,
                         float dst_x, float dst_y, float dst_w, float dst_h,
                         s32 tile_mode, ImU32 tint)
{
    if (dst_w <= 0.0f || dst_h <= 0.0f || src_w <= 0.0f || src_h <= 0.0f)
        return;

    /* UV coordinates for the source region */
    float u0 = src_x / tex_w;
    float v0 = src_y / tex_h;
    float u1 = (src_x + src_w) / tex_w;
    float v1 = (src_y + src_h) / tex_h;

    if (tile_mode == NINESLICE_STRETCH) {
        dl->AddImage(tex,
                     ImVec2(dst_x, dst_y),
                     ImVec2(dst_x + dst_w, dst_y + dst_h),
                     ImVec2(u0, v0), ImVec2(u1, v1),
                     tint);
    } else {
        /* Tile mode: repeat the source region at its original pixel size */
        float tile_w = src_w;
        float tile_h = src_h;

        for (float ty = dst_y; ty < dst_y + dst_h; ty += tile_h) {
            float th = tile_h;
            float tv1_local = v1;
            if (ty + th > dst_y + dst_h) {
                float frac = (dst_y + dst_h - ty) / tile_h;
                th = dst_y + dst_h - ty;
                tv1_local = v0 + (v1 - v0) * frac;
            }

            for (float tx = dst_x; tx < dst_x + dst_w; tx += tile_w) {
                float tw = tile_w;
                float tu1_local = u1;
                if (tx + tw > dst_x + dst_w) {
                    float frac = (dst_x + dst_w - tx) / tile_w;
                    tw = dst_x + dst_w - tx;
                    tu1_local = u0 + (u1 - u0) * frac;
                }

                dl->AddImage(tex,
                             ImVec2(tx, ty),
                             ImVec2(tx + tw, ty + th),
                             ImVec2(u0, v0),
                             ImVec2(tu1_local, tv1_local),
                             tint);
            }
        }
    }
}

/* =========================================================================
 * Minimal JSON parser for nineslice definitions
 * (Reuses the same lightweight approach as pdgui_theme_loader.cpp)
 * ========================================================================= */

static void ns_skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++;
}

static int ns_match_str(const char **p, const char *str) {
    ns_skip_ws(p);
    if (**p != '"') return 0;
    (*p)++;
    int len = (int)strlen(str);
    if (strncmp(*p, str, len) != 0 || (*p)[len] != '"') return 0;
    *p += len + 1;
    return 1;
}

static int ns_read_int(const char **p) {
    ns_skip_ws(p);
    return (int)strtol(*p, (char **)p, 10);
}

static int ns_read_string(const char **p, char *dst, int maxlen) {
    ns_skip_ws(p);
    if (**p != '"') return 0;
    (*p)++;
    int i = 0;
    while (**p && **p != '"' && i < maxlen - 1) {
        dst[i++] = **p;
        (*p)++;
    }
    dst[i] = '\0';
    if (**p == '"') (*p)++;
    return 1;
}

/* Parse a "stretch" or "tile" string into NINESLICE_STRETCH / NINESLICE_TILE. */
static s32 s_parseFillMode(const char **p)
{
    char mode[32];
    ns_read_string(p, mode, sizeof(mode));
    return (strcmp(mode, "tile") == 0) ? NINESLICE_TILE : NINESLICE_STRETCH;
}

/* Skip whatever primitive/object/array value starts at *p. */
static void s_skipValue(const char **p)
{
    ns_skip_ws(p);
    if (**p == '"') {
        (*p)++;
        while (**p && **p != '"') (*p)++;
        if (**p) (*p)++;
    } else if (**p == '{') {
        int d = 1; (*p)++;
        while (d > 0 && **p) {
            if (**p == '{') d++;
            else if (**p == '}') d--;
            (*p)++;
        }
    } else if (**p == '[') {
        int d = 1; (*p)++;
        while (d > 0 && **p) {
            if (**p == '[') d++;
            else if (**p == ']') d--;
            (*p)++;
        }
    } else {
        while (**p && **p != ',' && **p != '}') (*p)++;
    }
}

/* Parse a { top, bottom, left, right } sub-object, writing into four s32
 * outputs.  Returns 1 on success.  Missing fields keep their prior value. */
static s32 s_parseInsetObject(const char **p,
                              s32 *out_top, s32 *out_bottom,
                              s32 *out_left, s32 *out_right)
{
    ns_skip_ws(p);
    if (**p != '{') return 0;
    (*p)++;

    while (**p && **p != '}') {
        ns_skip_ws(p);
        if (**p == ',' || **p == ':') { (*p)++; continue; }
        if (**p != '"') { (*p)++; continue; }

        char key[32] = {0};
        (*p)++;
        int ki = 0;
        while (**p && **p != '"' && ki < 31) key[ki++] = *(*p)++;
        key[ki] = '\0';
        if (**p == '"') (*p)++;

        ns_skip_ws(p);
        if (**p == ':') (*p)++;
        ns_skip_ws(p);

        if (strcmp(key, "top") == 0)         *out_top    = ns_read_int(p);
        else if (strcmp(key, "bottom") == 0) *out_bottom = ns_read_int(p);
        else if (strcmp(key, "left") == 0)   *out_left   = ns_read_int(p);
        else if (strcmp(key, "right") == 0)  *out_right  = ns_read_int(p);
        else s_skipValue(p);
    }

    if (**p == '}') (*p)++;
    return 1;
}

static s32 s_parseNinesliceJson(const char *src, nineslice_def_t *def)
{
    memset(def, 0, sizeof(*def));
    def->edge_mode   = NINESLICE_STRETCH;
    def->top_mode    = NINESLICE_STRETCH;
    def->bottom_mode = NINESLICE_STRETCH;
    def->left_mode   = NINESLICE_STRETCH;
    def->right_mode  = NINESLICE_STRETCH;
    def->center_mode = NINESLICE_STRETCH;

    const char *p = src;
    ns_skip_ws(&p);
    if (*p != '{') return 0;
    p++;

    while (*p && *p != '}') {
        ns_skip_ws(&p);
        if (*p == ',' || *p == ':') { p++; continue; }
        if (*p != '"') { p++; continue; }

        /* Read key */
        char key[64] = {0};
        p++; /* skip opening quote */
        int ki = 0;
        while (*p && *p != '"' && ki < 63) key[ki++] = *p++;
        key[ki] = '\0';
        if (*p == '"') p++;

        ns_skip_ws(&p);
        if (*p == ':') p++;
        ns_skip_ws(&p);

        /* --- Legacy short-form flat insets (used when no src_inset block) --- */
        if (strcmp(key, "left") == 0)        def->left   = ns_read_int(&p);
        else if (strcmp(key, "right") == 0)   def->right  = ns_read_int(&p);
        else if (strcmp(key, "top") == 0)     def->top    = ns_read_int(&p);
        else if (strcmp(key, "bottom") == 0)  def->bottom = ns_read_int(&p);

        /* --- New-form split insets --- */
        else if (strcmp(key, "src_inset") == 0) {
            s_parseInsetObject(&p,
                &def->src_top, &def->src_bottom,
                &def->src_left, &def->src_right);
        }
        else if (strcmp(key, "dst_corner_px") == 0) {
            s_parseInsetObject(&p,
                &def->dst_top, &def->dst_bottom,
                &def->dst_left, &def->dst_right);
            def->has_split = 1;
        }

        /* --- Legacy single edge_mode shortcut --- */
        else if (strcmp(key, "edgeMode") == 0 || strcmp(key, "edge_mode") == 0) {
            def->edge_mode = s_parseFillMode(&p);
        }
        else if (strcmp(key, "centerMode") == 0 || strcmp(key, "center_mode") == 0) {
            def->center_mode = s_parseFillMode(&p);
        }

        /* --- New per-edge modes --- */
        else if (strcmp(key, "top_mode") == 0) {
            def->top_mode = s_parseFillMode(&p);
            def->has_per_edge_mode = 1;
        }
        else if (strcmp(key, "bottom_mode") == 0) {
            def->bottom_mode = s_parseFillMode(&p);
            def->has_per_edge_mode = 1;
        }
        else if (strcmp(key, "left_mode") == 0) {
            def->left_mode = s_parseFillMode(&p);
            def->has_per_edge_mode = 1;
        }
        else if (strcmp(key, "right_mode") == 0) {
            def->right_mode = s_parseFillMode(&p);
            def->has_per_edge_mode = 1;
        }

        else {
            s_skipValue(&p);
        }
    }

    /* --- Back-fill derived fields ---
     *
     * If the manifest supplied only the legacy flat insets we reuse them for
     * both source and destination (preserves previous behaviour for existing
     * mods authored against the old schema).
     *
     * If per-edge modes were NOT set, all four edges inherit edge_mode. */
    if (def->src_left == 0 && def->src_right == 0 &&
        def->src_top == 0  && def->src_bottom == 0) {
        def->src_left   = def->left;
        def->src_right  = def->right;
        def->src_top    = def->top;
        def->src_bottom = def->bottom;
    }
    if (!def->has_split) {
        def->dst_left   = def->src_left;
        def->dst_right  = def->src_right;
        def->dst_top    = def->src_top;
        def->dst_bottom = def->src_bottom;
    }
    if (!def->has_per_edge_mode) {
        def->top_mode    = def->edge_mode;
        def->bottom_mode = def->edge_mode;
        def->left_mode   = def->edge_mode;
        def->right_mode  = def->edge_mode;
    }

    return 1;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

void pdguiNinesliceInit(void)
{
    if (s_NsInitDone) return;
    s_NsInitDone = 1;
    s_NsCount = 0;
    sysLogPrintf(LOG_NOTE, "PDGUI nineslice: init");
}

void pdguiNinesliceShutdown(void)
{
    s_NsCount = 0;
    s_NsInitDone = 0;
    sysLogPrintf(LOG_NOTE, "PDGUI nineslice: shutdown");
}

/* Populate derived src_* / dst_* / per-edge mode fields from legacy short-
 * form inputs.  Runs on every register call so programmatic callers that
 * set only the classic left/right/top/bottom + edge_mode + center_mode
 * fields still get a valid struct. */
static void s_backfillDef(nineslice_def_t *def)
{
    if (def->src_left == 0 && def->src_right == 0 &&
        def->src_top == 0  && def->src_bottom == 0) {
        def->src_left   = def->left;
        def->src_right  = def->right;
        def->src_top    = def->top;
        def->src_bottom = def->bottom;
    }
    if (!def->has_split) {
        def->dst_left   = def->src_left;
        def->dst_right  = def->src_right;
        def->dst_top    = def->src_top;
        def->dst_bottom = def->src_bottom;
    }
    if (!def->has_per_edge_mode) {
        def->top_mode    = def->edge_mode;
        def->bottom_mode = def->edge_mode;
        def->left_mode   = def->edge_mode;
        def->right_mode  = def->edge_mode;
    }
}

s32 pdguiNinesliceRegister(const char *catalog_id, const nineslice_def_t *def)
{
    if (!catalog_id || !def) return 0;

    /* Update existing? */
    struct ns_entry *existing = s_findEntry(catalog_id);
    if (existing) {
        existing->def = *def;
        s_backfillDef(&existing->def);
        return 1;
    }

    if (s_NsCount >= NS_MAX_DEFS) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI nineslice: registry full (%d), cannot register '%s'",
            NS_MAX_DEFS, catalog_id);
        return 0;
    }

    struct ns_entry *e = &s_NsDefs[s_NsCount++];
    snprintf(e->catalog_id, sizeof(e->catalog_id), "%s", catalog_id);
    e->def = *def;
    s_backfillDef(&e->def);

    sysLogPrintf(LOG_NOTE,
        "PDGUI nineslice: registered '%s' (src L=%d R=%d T=%d B=%d  dst L=%d R=%d T=%d B=%d  center=%s)",
        catalog_id,
        e->def.src_left, e->def.src_right, e->def.src_top, e->def.src_bottom,
        e->def.dst_left, e->def.dst_right, e->def.dst_top, e->def.dst_bottom,
        e->def.center_mode == NINESLICE_TILE ? "tile" : "stretch");

    return 1;
}

s32 pdguiNinesliceCanRegister(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) return 0;
    return s_findEntry(catalog_id) != nullptr || s_NsCount < NS_MAX_DEFS;
}

const nineslice_def_t *pdguiNinesliceGet(const char *catalog_id)
{
    if (!catalog_id) return nullptr;
    struct ns_entry *e = s_findEntry(catalog_id);
    return e ? &e->def : nullptr;
}

s32 pdguiNinesliceLoadDef(const char *json_path, const char *catalog_id)
{
    if (!json_path || !catalog_id) return 0;

    u32 fileSize = 0;
    char *data = (char *)fsFileLoad(json_path, &fileSize);
    if (!data) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI nineslice: failed to load '%s'", json_path);
        return 0;
    }

    /* Null-terminate */
    char *json = (char *)malloc(fileSize + 1);
    memcpy(json, data, fileSize);
    json[fileSize] = '\0';
    free(data);

    nineslice_def_t def;
    s32 ok = s_parseNinesliceJson(json, &def);
    free(json);

    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI nineslice: parse failed for '%s'", json_path);
        return 0;
    }

    return pdguiNinesliceRegister(catalog_id, &def);
}

void pdguiNinesliceDraw(const char *tex_id,
                        float x, float y, float w, float h,
                        u32 tint)
{
    if (!tex_id) return;

    void *tex = pdguiThemeGetTexture(tex_id);
    if (!tex) return;

    const nineslice_def_t *def = pdguiNinesliceGet(tex_id);
    if (!def) {
        /* No 9-slice def: draw as simple stretched quad */
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddImage((ImTextureID)(uintptr_t)tex,
                     ImVec2(x, y), ImVec2(x + w, y + h),
                     ImVec2(0, 0), ImVec2(1, 1),
                     NsCol(tint));
        return;
    }

    /* Get texture dimensions from catalog */
    const asset_entry_t *entry = assetCatalogResolve(tex_id);
    if (!entry || !entry->loaded_data) return;

    /* Estimate texture dimensions from data_size_bytes (RGBA32: w*h*4) */
    /* We need actual dimensions -- stored alongside the texture.
     * For ROM textures we know the sizes from the config table.
     * Use a reasonable approach: check if it's a known UI texture. */
    s32 tex_w = 64, tex_h = 64;  /* default guess */

    /* data_size_bytes = w * h * 4, and we know w/h for registered textures.
     * For now, use the def insets to infer minimum size. */
    if (entry->data_size_bytes > 0) {
        /* Try common square sizes first */
        u32 total_pixels = entry->data_size_bytes / 4;
        for (s32 sz = 256; sz >= 1; sz >>= 1) {
            if (total_pixels == (u32)(sz * sz)) {
                tex_w = tex_h = sz;
                break;
            }
        }
        /* Non-square: try common aspect ratios */
        if ((u32)(tex_w * tex_h) != total_pixels) {
            for (s32 tw = 256; tw >= 1; tw >>= 1) {
                if (total_pixels % tw == 0) {
                    s32 th = total_pixels / tw;
                    if (th <= 256) { tex_w = tw; tex_h = th; break; }
                }
            }
        }
    }

    pdguiNinesliceDrawEx(tex, tex_w, tex_h, def, x, y, w, h, tint);
}

void pdguiNinesliceDrawEx(void *tex, s32 tex_w, s32 tex_h,
                          const nineslice_def_t *def,
                          float x, float y, float w, float h,
                          u32 tint)
{
    if (!tex || !def) return;

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImTextureID tid = (ImTextureID)(uintptr_t)tex;
    ImU32 col = NsCol(tint);

    float ftw = (float)tex_w;
    float fth = (float)tex_h;

    /* --- Source insets (drive UV) ---
     * These are measured in source texture pixels. */
    float sl = (float)def->src_left;
    float sr = (float)def->src_right;
    float st = (float)def->src_top;
    float sb = (float)def->src_bottom;
    float src_mid_w = ftw - sl - sr;
    float src_mid_h = fth - st - sb;
    if (src_mid_w < 0.0f) src_mid_w = 0.0f;
    if (src_mid_h < 0.0f) src_mid_h = 0.0f;

    /* --- Destination corners (drive vertex layout) ---
     * These are measured in screen pixels.  Clamp so opposite corners
     * don't overlap on tiny draw rects. */
    float dl_px = (float)def->dst_left;
    float dr_px = (float)def->dst_right;
    float dt_px = (float)def->dst_top;
    float db_px = (float)def->dst_bottom;
    if (dl_px + dr_px > w) {
        float scale = w / (dl_px + dr_px);
        dl_px *= scale;
        dr_px *= scale;
    }
    if (dt_px + db_px > h) {
        float scale = h / (dt_px + db_px);
        dt_px *= scale;
        db_px *= scale;
    }
    float dst_mid_w = w - dl_px - dr_px;
    float dst_mid_h = h - dt_px - db_px;
    if (dst_mid_w < 0.0f) dst_mid_w = 0.0f;
    if (dst_mid_h < 0.0f) dst_mid_h = 0.0f;

    s32 top_mode    = def->top_mode;
    s32 bottom_mode = def->bottom_mode;
    s32 left_mode   = def->left_mode;
    s32 right_mode  = def->right_mode;

    /* Row 0: TL, Top, TR */
    s_drawRegion(dl, tid, 0,  0,  sl, st, ftw, fth,
                 x, y, dl_px, dt_px,
                 NINESLICE_STRETCH, col);

    s_drawRegion(dl, tid, sl, 0,  src_mid_w, st, ftw, fth,
                 x + dl_px, y, dst_mid_w, dt_px,
                 top_mode, col);

    s_drawRegion(dl, tid, ftw - sr, 0, sr, st, ftw, fth,
                 x + w - dr_px, y, dr_px, dt_px,
                 NINESLICE_STRETCH, col);

    /* Row 1: Left, Center, Right */
    s_drawRegion(dl, tid, 0, st, sl, src_mid_h, ftw, fth,
                 x, y + dt_px, dl_px, dst_mid_h,
                 left_mode, col);

    s_drawRegion(dl, tid, sl, st, src_mid_w, src_mid_h, ftw, fth,
                 x + dl_px, y + dt_px, dst_mid_w, dst_mid_h,
                 def->center_mode, col);

    s_drawRegion(dl, tid, ftw - sr, st, sr, src_mid_h, ftw, fth,
                 x + w - dr_px, y + dt_px, dr_px, dst_mid_h,
                 right_mode, col);

    /* Row 2: BL, Bottom, BR */
    s_drawRegion(dl, tid, 0, fth - sb, sl, sb, ftw, fth,
                 x, y + h - db_px, dl_px, db_px,
                 NINESLICE_STRETCH, col);

    s_drawRegion(dl, tid, sl, fth - sb, src_mid_w, sb, ftw, fth,
                 x + dl_px, y + h - db_px, dst_mid_w, db_px,
                 bottom_mode, col);

    s_drawRegion(dl, tid, ftw - sr, fth - sb, sr, sb, ftw, fth,
                 x + w - dr_px, y + h - db_px, dr_px, db_px,
                 NINESLICE_STRETCH, col);
}

} /* extern "C" */
