/**
 * fontcatalog.c -- c3849 Wave 3: runtime compiler for public .pdfont
 * bitmap font source.
 *
 * Recompiles the editable public exports (glyphs.pgm 8-bit grayscale
 * atlas + font.metrics.json metrics/kerning) emitted by
 * port/src/romextract_pdfont.c back into the native segment layout
 * textLoadFont consumes. See fontcatalog.h for the layout contract and
 * port/src/preprocess/segfonts.c for the authoritative shape.
 *
 * Round-trip notes:
 *   - the emitter wrote pixel value = nibble * 17, so the
 *     round-to-nearest repack (v + 8) / 17 (clamped to 15) is exact for
 *     base archives and well-behaved for modder-edited PGMs;
 *   - packable glyph width is capped at 16 px by the fixed 8-byte CI4
 *     row stride. Wider DECLARED widths are advance-only metrics that
 *     exist in base data (handelgothiclg 'W' declares 17 while its
 *     pixel rows hold 16 columns, in ROM and in the export alike);
 *     they pack 16 columns. Metrics carrying real pixel data past
 *     column 16 are rejected loudly (unrepresentable in the stride);
 *   - glyph count must match the build's expectation for the face
 *     (94, or 135 for the PAL handelgothic xs/sm/md faces) because
 *     textLoadFont's post-load fixups iterate NUMCHARS() entries.
 *
 * JSON helpers are a per-file private copy of the langmanifest.c
 * mini-parser (established per-file helper convention).
 */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "platform.h"
#include "system.h"
#include "types.h"
#include "constants.h"
#include "assetcatalog.h"
#include "fontcatalog.h"
#include "fs.h"

#define FONTCAT_KERNING_DIM 13
#define FONTCAT_KERNING_COUNT (FONTCAT_KERNING_DIM * FONTCAT_KERNING_DIM)
#define FONTCAT_GLYPH_ROW_BYTES 8
#define FONTCAT_MAX_GLYPH_WIDTH 16 /* 8-byte CI4 row stride ceiling */
/* The glyph draw paths gDPLoadBlock exactly (height*8 + 16) bytes per
 * glyph (((h*8+17)>>1) 16-bit texels); the ROM data keeps >= 24 bytes
 * of slack after every glyph so that over-read is always in-bounds.
 * The compiled payload packs glyphs back-to-back, so only the LAST
 * glyph needs the headroom. The extra texels are never sampled; the
 * bytes just have to be readable. */
#define FONTCAT_TAIL_PAD 16

/* The compiled payload is consumed through the build's own struct
 * fontchar; pin the 16-byte record contract shared with the emitter. */
_Static_assert(sizeof(struct fontchar) == 16,
    "segment-shaped font payload expects 16-byte fontchar records");

/* =========================================================================
 * Mini JSON helpers (private copy of the langmanifest.c parser)
 * ========================================================================= */

static const char *jsonSkipWs(const char *p)
{
    while (p && *p && isspace((u8)*p)) {
        p++;
    }
    return p;
}

static s32 jsonHexNibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static const char *jsonStringEnd(const char *p)
{
    if (!p || *p != '"') {
        return NULL;
    }

    p++;
    while (*p) {
        if (*p == '"') {
            return p + 1;
        }
        if (*p == '\\') {
            p++;
            if (*p == 'u') {
                p++;
                for (s32 i = 0; i < 4; i++) {
                    if (jsonHexNibble(p[i]) < 0) {
                        return NULL;
                    }
                }
                p += 4;
                continue;
            }
            if (*p) {
                p++;
                continue;
            }
            return NULL;
        }
        p++;
    }

    return NULL;
}

static const char *jsonFindMatching(const char *start)
{
    char open;
    char close;
    s32 depth = 0;
    const char *p;

    if (!start || (*start != '{' && *start != '[')) {
        return NULL;
    }

    open = *start;
    close = (open == '{') ? '}' : ']';

    for (p = start; *p; p++) {
        if (*p == '"') {
            p = jsonStringEnd(p);
            if (!p) {
                return NULL;
            }
            p--;
            continue;
        }
        if (*p == open) {
            depth++;
        } else if (*p == close) {
            depth--;
            if (depth == 0) {
                return p + 1;
            }
        }
    }

    return NULL;
}

static const char *jsonValueEnd(const char *value)
{
    const char *p = jsonSkipWs(value);

    if (!p || !*p) {
        return NULL;
    }

    if (*p == '"') {
        return jsonStringEnd(p);
    }
    if (*p == '{' || *p == '[') {
        return jsonFindMatching(p);
    }

    while (*p && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    return p;
}

static const char *jsonFindObjectKey(const char *obj, const char *obj_end,
                                     const char *key)
{
    const char *p;
    size_t key_len;

    if (!obj || !obj_end || !key || *obj != '{') {
        return NULL;
    }

    key_len = strlen(key);
    p = obj + 1;

    while (p < obj_end) {
        const char *key_start;
        const char *key_end;
        const char *value;
        const char *next;

        p = jsonSkipWs(p);
        if (!p || p >= obj_end || *p == '}') {
            break;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"') {
            p++;
            continue;
        }

        key_start = p + 1;
        key_end = jsonStringEnd(p);
        if (!key_end || key_end > obj_end) {
            return NULL;
        }

        p = jsonSkipWs(key_end);
        if (!p || p >= obj_end || *p != ':') {
            return NULL;
        }
        value = jsonSkipWs(p + 1);

        if ((size_t)(key_end - key_start - 1) == key_len &&
                memcmp(key_start, key, key_len) == 0) {
            return value;
        }

        next = jsonValueEnd(value);
        if (!next || next <= value) {
            return NULL;
        }
        p = next;
        if (*p == ',') {
            p++;
        }
    }

    return NULL;
}

/* Read one required integer field from a JSON object span. Returns 1 on
 * success, 0 when the key is missing or not numeric. */
static s32 jsonReadS32(const char *obj, const char *obj_end,
                       const char *key, s32 *out)
{
    const char *value = jsonFindObjectKey(obj, obj_end, key);
    char *endptr = NULL;
    long parsed;

    if (!value) {
        return 0;
    }

    parsed = strtol(value, &endptr, 0);
    if (endptr == value) {
        return 0;
    }

    *out = (s32)parsed;
    return 1;
}

/* =========================================================================
 * PGM (P5 binary grayscale) parsing
 * ========================================================================= */

static s32 s_pgmNextInt(const u8 *data, u32 size, u32 *pos, u32 *out)
{
    u32 p = *pos;
    u32 v = 0;
    s32 digits = 0;

    for (;;) {
        while (p < size && isspace(data[p])) {
            p++;
        }
        if (p < size && data[p] == '#') {
            while (p < size && data[p] != '\n') {
                p++;
            }
            continue;
        }
        break;
    }

    while (p < size && data[p] >= '0' && data[p] <= '9') {
        v = v * 10u + (u32)(data[p] - '0');
        if (v > 0xffffffu) {
            return 0;
        }
        digits++;
        p++;
    }

    if (digits == 0) {
        return 0;
    }

    *pos = p;
    *out = v;
    return 1;
}

/* Returns a pointer to the first pixel byte, or NULL on parse failure.
 * Accepts whitespace/comment-bearing headers so modder-edited PGMs from
 * standard tools load too. Requires maxval 255 (the emitted format). */
static const u8 *s_pgmParse(const u8 *data, u32 size, u32 *out_w, u32 *out_h)
{
    u32 pos = 2;
    u32 w = 0;
    u32 h = 0;
    u32 maxval = 0;

    if (!data || size < 8 || data[0] != 'P' || data[1] != '5') {
        return NULL;
    }
    if (!s_pgmNextInt(data, size, &pos, &w) ||
            !s_pgmNextInt(data, size, &pos, &h) ||
            !s_pgmNextInt(data, size, &pos, &maxval)) {
        return NULL;
    }
    if (w == 0 || h == 0 || w > 0xffffu || h > 0xffffu || maxval != 255) {
        return NULL;
    }
    /* Exactly one whitespace byte separates maxval from pixel data. */
    if (pos >= size || !isspace(data[pos])) {
        return NULL;
    }
    pos++;
    if ((u64)pos + (u64)w * (u64)h > (u64)size) {
        return NULL;
    }

    *out_w = w;
    *out_h = h;
    return data + pos;
}

/* =========================================================================
 * Face shape expectations
 * ========================================================================= */

/* Mirror of romextract_pdfont.c s_fontNumChars and textLoadFont's
 * NUMCHARS(): the compiled payload MUST carry exactly this many glyph
 * records because the caller's post-load fixups iterate that count. */
static s32 s_fontExpectedChars(const char *face)
{
#if VERSION == VERSION_PAL_FINAL
    if (face &&
        (!strcmp(face, "handelgothicsm") ||
         !strcmp(face, "handelgothicxs") ||
         !strcmp(face, "handelgothicmd"))) {
        return 135;
    }
#endif
    (void)face;
    return 94;
}

typedef struct {
    s32 index;
    s32 baseline;
    s32 height;
    s32 width;
    s32 kerning_index;
    s32 atlas_x;
    s32 atlas_y;
    s32 seen;
} fontcat_glyph_t;

/* =========================================================================
 * API
 * ========================================================================= */

s32 fontCatalogBuildFace(const char *face, u8 **out_payload, u32 *out_len)
{
    char id[CATALOG_ID_LEN];
    const asset_entry_t *entry;
    u8 *pgm = NULL;
    u32 pgm_size = 0;
    char *met = NULL;
    u32 met_size = 0;
    const u8 *pixels = NULL;
    u32 atlas_w = 0;
    u32 atlas_h = 0;
    fontcat_glyph_t *glyphs = NULL;
    s32 kern_table[FONTCAT_KERNING_COUNT];
    s32 num_chars;
    s32 glyphs_seen = 0;
    u8 *buf = NULL;

    if (out_payload) {
        *out_payload = NULL;
    }
    if (out_len) {
        *out_len = 0;
    }
    if (!face || !face[0] || !out_payload || !out_len) {
        return 0;
    }

    snprintf(id, sizeof(id), "base:font_%s", face);

    entry = assetCatalogResolve(id);
    if (!entry || entry->type != ASSET_FONT || !entry->enabled) {
        sysLogPrintf(LOG_WARNING,
            "FONT.CATALOG: no enabled ASSET_FONT row \"%s\" for face %s",
            id, face);
        return 0;
    }
    if (!entry->ext.font.font_file[0]) {
        sysLogPrintf(LOG_WARNING,
            "FONT.CATALOG: row \"%s\" has no bound glyphs member", id);
        return 0;
    }
    if (!entry->ext.font.metrics_file[0]) {
        sysLogPrintf(LOG_WARNING,
            "FONT.CATALOG: row \"%s\" has no bound metrics member", id);
        return 0;
    }

    /* Only the bitmap-atlas shape is compilable here. The walker's
     * font.otf fallback (vector mod fonts) has no rasterizer yet. */
    {
        size_t flen = strlen(entry->ext.font.font_file);
        if (flen < 4 ||
                strcmp(entry->ext.font.font_file + flen - 4, ".pgm") != 0) {
            sysLogPrintf(LOG_WARNING,
                "FONT.CATALOG: row \"%s\" glyphs member \"%s\" is not a "
                ".pgm bitmap atlas (vector fonts unsupported)",
                id, entry->ext.font.font_file);
            return 0;
        }
    }

    pgm = (u8 *)fsFileLoad(entry->ext.font.font_file, &pgm_size);
    if (!pgm || pgm_size == 0) {
        sysLogPrintf(LOG_WARNING,
            "FONT.CATALOG: failed to load glyphs member \"%s\" for \"%s\"",
            entry->ext.font.font_file, id);
        goto fail;
    }

    met = (char *)fsFileLoad(entry->ext.font.metrics_file, &met_size);
    if (!met || met_size == 0) {
        sysLogPrintf(LOG_WARNING,
            "FONT.CATALOG: failed to load metrics member \"%s\" for \"%s\"",
            entry->ext.font.metrics_file, id);
        goto fail;
    }
    met[met_size] = '\0'; /* fsFileLoad allocates size + 1 */

    pixels = s_pgmParse(pgm, pgm_size, &atlas_w, &atlas_h);
    if (!pixels) {
        sysLogPrintf(LOG_WARNING,
            "FONT.CATALOG: \"%s\" glyphs.pgm parse failed "
            "(need binary P5, maxval 255)", id);
        goto fail;
    }

    num_chars = s_fontExpectedChars(face);
    glyphs = (fontcat_glyph_t *)calloc((size_t)num_chars, sizeof(*glyphs));
    if (!glyphs) {
        sysLogPrintf(LOG_WARNING,
            "FONT.CATALOG: OOM parsing metrics for \"%s\"", id);
        goto fail;
    }
    memset(kern_table, 0, sizeof(kern_table));

    /* ---- metrics JSON ---- */
    {
        const char *cursor = met;
        const char *root_end;
        const char *glyphs_value;
        const char *glyphs_end;
        const char *kern_value;
        const char *kern_end;

        if ((u8)cursor[0] == 0xef && (u8)cursor[1] == 0xbb &&
                (u8)cursor[2] == 0xbf) {
            cursor += 3;
        }
        cursor = jsonSkipWs(cursor);
        root_end = jsonFindMatching(cursor);
        if (!root_end) {
            sysLogPrintf(LOG_WARNING,
                "FONT.CATALOG: \"%s\" font.metrics.json is malformed", id);
            goto fail;
        }

        glyphs_value = jsonFindObjectKey(cursor, root_end, "glyphs");
        if (!glyphs_value || *glyphs_value != '[') {
            sysLogPrintf(LOG_WARNING,
                "FONT.CATALOG: \"%s\" metrics missing glyphs array", id);
            goto fail;
        }
        glyphs_end = jsonFindMatching(glyphs_value);
        if (!glyphs_end) {
            sysLogPrintf(LOG_WARNING,
                "FONT.CATALOG: \"%s\" metrics glyphs array malformed", id);
            goto fail;
        }

        for (const char *p = glyphs_value + 1; p < glyphs_end;) {
            const char *obj;
            const char *obj_end;
            fontcat_glyph_t g;
            s32 slot = -1;

            p = jsonSkipWs(p);
            if (!p || p >= glyphs_end || *p == ']') {
                break;
            }
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p != '{') {
                p++;
                continue;
            }

            obj = p;
            obj_end = jsonFindMatching(obj);
            if (!obj_end || obj_end > glyphs_end) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" metrics glyph object malformed",
                    id);
                goto fail;
            }

            memset(&g, 0, sizeof(g));
            if (!jsonReadS32(obj, obj_end, "slot", &slot) ||
                    !jsonReadS32(obj, obj_end, "index", &g.index) ||
                    !jsonReadS32(obj, obj_end, "baseline", &g.baseline) ||
                    !jsonReadS32(obj, obj_end, "height", &g.height) ||
                    !jsonReadS32(obj, obj_end, "width", &g.width) ||
                    !jsonReadS32(obj, obj_end, "kerning_index",
                                 &g.kerning_index) ||
                    !jsonReadS32(obj, obj_end, "atlas_x", &g.atlas_x) ||
                    !jsonReadS32(obj, obj_end, "atlas_y", &g.atlas_y)) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" metrics glyph entry missing a "
                    "required integer field", id);
                goto fail;
            }

            if (slot < 0 || slot >= num_chars) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" glyph slot %d out of range "
                    "(expected 0..%d)", id, slot, num_chars - 1);
                goto fail;
            }
            if (glyphs[slot].seen) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" duplicate glyph slot %d",
                    id, slot);
                goto fail;
            }
            if (g.width < 0 || g.width > 255) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" glyph slot %d width %d out of "
                    "range for the native fontchar record", id, slot, g.width);
                goto fail;
            }
            if (g.height < 0 || g.height > 255 ||
                    g.baseline < -128 || g.baseline > 127 ||
                    g.index < 0 ||
#if VERSION == VERSION_JPN_FINAL
                    g.index > 0xffff ||
                    g.kerning_index < -32768 || g.kerning_index > 32767
#else
                    g.index > 0xff
#endif
                    ) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" glyph slot %d field out of range "
                    "for the native fontchar record", id, slot);
                goto fail;
            }
            if (g.atlas_x < 0 || g.atlas_y < 0 ||
                    (u32)g.atlas_x + (u32)g.width > atlas_w ||
                    (u32)g.atlas_y + (u32)g.height > atlas_h) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" glyph slot %d rect %d,%d %dx%d "
                    "exceeds atlas %ux%u", id, slot, g.atlas_x, g.atlas_y,
                    g.width, g.height, atlas_w, atlas_h);
                goto fail;
            }

            /* Fixed 8-byte CI4 row stride = 16 packable columns. The
             * base data itself carries width-as-advance glyphs wider
             * than 16 (handelgothiclg 'W' declares 17) whose pixel rows
             * still hold only 16 columns; the emitter exports exactly
             * those 16. Reject only when a wider glyph declares REAL
             * pixel data beyond column 16 (unrepresentable in the
             * stride); otherwise the pack loop clamps to 16 columns,
             * byte-identical to the ROM segment. */
            if (g.width > FONTCAT_MAX_GLYPH_WIDTH) {
                for (s32 y = 0; y < g.height; y++) {
                    const u8 *row = pixels +
                        ((u32)g.atlas_y + (u32)y) * atlas_w + (u32)g.atlas_x;
                    for (s32 x = FONTCAT_MAX_GLYPH_WIDTH; x < g.width; x++) {
                        if (row[x] != 0) {
                            sysLogPrintf(LOG_WARNING,
                                "FONT.CATALOG: \"%s\" glyph slot %d width "
                                "%d carries pixel data past the %d px CI4 "
                                "row stride ceiling", id, slot, g.width,
                                FONTCAT_MAX_GLYPH_WIDTH);
                            goto fail;
                        }
                    }
                }
            }

            g.seen = 1;
            glyphs[slot] = g;
            glyphs_seen++;
            p = obj_end;
        }

        if (glyphs_seen != num_chars) {
            sysLogPrintf(LOG_WARNING,
                "FONT.CATALOG: \"%s\" metrics carries %d glyphs, build "
                "expects %d for face %s", id, glyphs_seen, num_chars, face);
            goto fail;
        }

        kern_value = jsonFindObjectKey(cursor, root_end, "kerning");
        if (!kern_value || *kern_value != '[') {
            sysLogPrintf(LOG_WARNING,
                "FONT.CATALOG: \"%s\" metrics missing kerning array", id);
            goto fail;
        }
        kern_end = jsonFindMatching(kern_value);
        if (!kern_end) {
            sysLogPrintf(LOG_WARNING,
                "FONT.CATALOG: \"%s\" metrics kerning array malformed", id);
            goto fail;
        }

        for (const char *p = kern_value + 1; p < kern_end;) {
            const char *obj;
            const char *obj_end;
            s32 prev = -1;
            s32 cur = -1;
            s32 adjust = 0;

            p = jsonSkipWs(p);
            if (!p || p >= kern_end || *p == ']') {
                break;
            }
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p != '{') {
                p++;
                continue;
            }

            obj = p;
            obj_end = jsonFindMatching(obj);
            if (!obj_end || obj_end > kern_end) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" kerning object malformed", id);
                goto fail;
            }

            if (!jsonReadS32(obj, obj_end, "previous", &prev) ||
                    !jsonReadS32(obj, obj_end, "current", &cur) ||
                    !jsonReadS32(obj, obj_end, "adjust", &adjust)) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" kerning entry missing a required "
                    "integer field", id);
                goto fail;
            }
            if (prev < 0 || prev >= FONTCAT_KERNING_DIM ||
                    cur < 0 || cur >= FONTCAT_KERNING_DIM) {
                sysLogPrintf(LOG_WARNING,
                    "FONT.CATALOG: \"%s\" kerning pair %d,%d out of the "
                    "13x13 table", id, prev, cur);
                goto fail;
            }

            kern_table[prev * FONTCAT_KERNING_DIM + cur] = adjust;
            p = obj_end;
        }
    }

    /* ---- segment-shaped payload ---- */
    {
        u32 chars_off = (u32)offsetof(struct font, chars);
        u32 pix_off = chars_off + (u32)num_chars * (u32)sizeof(struct fontchar);
        u32 total = pix_off;
        struct fontchar *chars;
        s32 *kern;
        u32 cursor_off;

        for (s32 i = 0; i < num_chars; i++) {
            total += (u32)glyphs[i].height * FONTCAT_GLYPH_ROW_BYTES;
        }
        total += FONTCAT_TAIL_PAD; /* LoadBlock over-read headroom */

        buf = (u8 *)calloc(1, total);
        if (!buf) {
            sysLogPrintf(LOG_WARNING,
                "FONT.CATALOG: OOM building payload for \"%s\" (%u bytes)",
                id, total);
            goto fail;
        }

        kern = (s32 *)(void *)buf;
        for (s32 i = 0; i < FONTCAT_KERNING_COUNT; i++) {
            kern[i] = kern_table[i];
        }

        chars = (struct fontchar *)(void *)(buf + chars_off);
        cursor_off = pix_off;
        for (s32 i = 0; i < num_chars; i++) {
            const fontcat_glyph_t *g = &glyphs[i];

            /* Field-by-field: index/kerningindex widths are
             * build-variant (JPN u16/s16) and pixeldata is
             * pointer-size dependent. */
            chars[i].index = g->index;
            chars[i].baseline = (s8)g->baseline;
            chars[i].height = (u8)g->height;
            chars[i].width = (u8)g->width;
            chars[i].kerningindex = g->kerning_index;
            chars[i].pixeldata = (u8 *)(uintptr_t)cursor_off;

            /* Pack width+1 columns to match the renderer, which samples
             * width+1 texels (game_1531a0.c) -- a glyph whose ink reaches
             * column `width` (overhang past the advance) needs that column.
             * Capped at 16 (8-byte CI4 row stride); wider declared widths
             * are advance-only, validated above. */
            s32 packw = g->width + 1;
            if (packw > FONTCAT_MAX_GLYPH_WIDTH) {
                packw = FONTCAT_MAX_GLYPH_WIDTH;
            }

            for (s32 y = 0; y < g->height; y++) {
                const u8 *srcrow = pixels +
                    ((u32)g->atlas_y + (u32)y) * atlas_w + (u32)g->atlas_x;
                u8 *dstrow = buf + cursor_off +
                    (u32)y * FONTCAT_GLYPH_ROW_BYTES;

                for (s32 x = 0; x < packw; x++) {
                    u32 nibble = ((u32)srcrow[x] + 8u) / 17u;
                    if (nibble > 15u) {
                        nibble = 15u;
                    }
                    if (x & 1) {
                        dstrow[x / 2] |= (u8)nibble;
                    } else {
                        dstrow[x / 2] |= (u8)(nibble << 4);
                    }
                }
            }

            cursor_off += (u32)g->height * FONTCAT_GLYPH_ROW_BYTES;
        }

        sysLogPrintf(LOG_NOTE,
            "FONT.CATALOG: compiled face %s from \"%s\" "
            "(chars=%d, atlas=%ux%u, %u bytes)",
            face, id, num_chars, atlas_w, atlas_h, total);

        free(glyphs);
        free(met);
        free(pgm);
        *out_payload = buf;
        *out_len = total;
        return 1;
    }

fail:
    free(buf);
    free(glyphs);
    if (met) {
        free(met);
    }
    if (pgm) {
        free(pgm);
    }
    return 0;
}
