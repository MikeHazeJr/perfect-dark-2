/**
 * pdgui_theme_loader.cpp -- JSON-based theme definition loading (P3)
 *
 * Closes the remaining Visual Theme Layer gaps:
 *   1. JSON theme definition format (palette, textures, scanline, glow)
 *   2. Theme as catalog asset (base:theme_*, mod:theme_*)
 *   3. Theme hot-reload via pdguiThemeLoadFromCatalog()
 *   4. pd.ini persistence for active theme selection
 *
 * The 7 built-in palettes from pdgui_style.cpp are registered as catalog
 * assets at init time. External theme.json files follow the same schema.
 *
 * IMPORTANT: Do NOT include types.h -- it #defines bool as s32, breaking C++.
 * Use extern "C" forward declarations for game symbols instead.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>   /* 2026-04-11: mods/ directory scan for custom theme.json */
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "pdgui_theme_loader.h"
#include "pdgui_theme.h"
#include "pdgui_style.h"
#include "pdgui_nineslice.h"
#include "pdgui_effects.h"
#include "pdgui_fontmgr.h"
#include "pdgui_font_mod.h"   /* S305 P4: theme → font-mod bundling */
#include "assetcatalog.h"
#include "config.h"
#include "system.h"
#include "fs.h"
#include "modmgr.h"           /* B-238 follow-up: MODMGR_RESERVED_NAMES_LIST */
#include "modarchive.h"       /* B-238 follow-up: read theme.json from .pdmod archives */

/* =========================================================================
 * Constants
 * ========================================================================= */

#define THEME_MAX_REGISTERED  64
#define THEME_NAME_LEN        128
#define THEME_AUTHOR_LEN      64
#define THEME_VERSION_LEN     32
#define THEME_FILEPATH_LEN    256
#define THEME_SOUNDPACK_LEN   64
#define THEME_TEX_SLOTS       16
#define THEME_TEX_SLOTNAME    64
#define THEME_TEX_PATH_LEN    256
#define THEME_CATALOG_ID_LEN  64
#define THEME_CFG_KEY         "Theme.ActiveTheme"
#define THEME_DEFAULT_ID      "base:theme_blue"

/* =========================================================================
 * Theme definition structure
 * ========================================================================= */

struct theme_tex_override {
    char slot_name[THEME_TEX_SLOTNAME];   /* e.g., "ui_bg_haze" */
    char file_path[THEME_TEX_PATH_LEN];   /* relative to theme dir */
};

struct theme_def {
    char name[THEME_NAME_LEN];
    char author[THEME_AUTHOR_LEN];
    char version[THEME_VERSION_LEN];

    /* Palette: 15 legacy fields + 5 S306 extensions (toolbar tint,
     * text_positive, text_warning, button_hover, button_active) + 4 S309
     * extensions (title_glow, tint_success, tint_danger, tint_info).
     * The extensions are zero unless theme.json opts in — pdguiApplyPdStyle
     * falls back to derived defaults when zero. */
    u32  palette[24];
    s32  has_palette;

    /* Texture overrides */
    struct theme_tex_override textures[THEME_TEX_SLOTS];
    s32  num_textures;

    /* Scanline settings */
    s32  scanline_enabled;
    f32  scanline_alpha;
    s32  scanline_interval;
    s32  has_scanline;

    /* Text glow settings */
    s32  glow_enabled;
    f32  glow_intensity;
    u32  glow_color;
    s32  has_glow;

    /* Sound pack */
    char sound_pack[THEME_SOUNDPACK_LEN];

    /* S305 P4: bundled component references. When a theme is loaded, any
     * non-empty id here is forwarded to the matching subsystem so one
     * theme activation can swap the chrome + font + palette together. */
    char bundle_chrome_id[THEME_CATALOG_ID_LEN];  /* pdguiThemeSetUiChromeStyleId */
    char bundle_font_id[THEME_CATALOG_ID_LEN];    /* pdguiFontModSetActiveId */

    /* P4: 9-slice definitions for UI textures */
    struct {
        char catalog_id[THEME_TEX_SLOTNAME];
        s32  left, right, top, bottom;
        s32  edge_tile;    /* 0=stretch, 1=tile */
        s32  center_tile;  /* 0=stretch, 1=tile */
    } nineslices[THEME_TEX_SLOTS];
    s32 num_nineslices;

    /* P4: Caustic effect */
    struct {
        char element_id[THEME_TEX_SLOTNAME];
        char texture_id[THEME_TEX_SLOTNAME];
        s32  frame_count;
        f32  speed;
        f32  opacity;
        s32  blend_mode;   /* 0=multiply, 1=additive, 2=screen */
        f32  scale;
    } caustics[8];
    s32 num_caustics;

    /* P4: Border effects */
    struct {
        char element_id[THEME_TEX_SLOTNAME];
        char mask_texture_id[THEME_TEX_SLOTNAME];
        f32  opacity;
        s32  blend_mode;
        u32  tint_color;
        f32  scroll_x, scroll_y;
    } border_effects[8];
    s32 num_border_effects;

    /* P4: Font override */
    char font_name[THEME_NAME_LEN];
    char font_path[THEME_FILEPATH_LEN];
    f32  font_size;
    s32  has_font;

    /* P4: Font shadow */
    f32 shadow_offset_x, shadow_offset_y;
    u32 shadow_color;
    s32 has_shadow;

    /* P4: Font glow (distinct from text glow — applies to font rendering) */
    f32 font_glow_radius;
    f32 font_glow_intensity;
    u32 font_glow_color;
    s32 font_glow_passes;
    s32 has_font_glow;
};

/* =========================================================================
 * Registry: tracks registered themes
 * ========================================================================= */

struct theme_entry {
    char catalog_id[THEME_CATALOG_ID_LEN];
    char name[THEME_NAME_LEN];
    char filepath[THEME_FILEPATH_LEN]; /* empty for built-in; for archive themes
                                          this is "<archive>::theme.json" purely
                                          for diagnostics — load goes via embed_data */
    s32  palette_index;                /* 0-6 for built-in, -1 for JSON */
    s32  enabled;                      /* 1=available in selector, 0=disabled */
    s32  first_sight;                  /* transient: 1 if just discovered this session */
    char cfg_enabled_key[96];          /* pd.ini key for enabled persistence */
    /* B-238 follow-up: bytes for archive-sourced themes. NULL for folder /
     * built-in themes, where filepath drives fsFileLoad on apply. Owned by
     * the entry; freed on registry reset. */
    char *embed_data;
    u32   embed_size;
};

static struct theme_entry s_Themes[THEME_MAX_REGISTERED];
static s32 s_ThemeCount = 0;
static char s_ActiveThemeId[THEME_CATALOG_ID_LEN] = THEME_DEFAULT_ID;
static char s_CfgThemeId[THEME_CATALOG_ID_LEN] = THEME_DEFAULT_ID;
static s32 s_LoaderInitDone = 0;

/* Comma-separated list of mod theme slugs seen in prior sessions.
 * Used to detect first-sight themes for default-enabled auto-apply. */
static char s_SeenModThemes[2048] = "";
#define THEME_SEEN_KEY "Theme.SeenMods"

/* =========================================================================
 * Minimal JSON tokenizer (mirrors modmgr.c approach, C++ compatible)
 * ========================================================================= */

enum JTok {
    JT_NONE = 0, JT_LBRACE, JT_RBRACE, JT_LBRACKET, JT_RBRACKET,
    JT_COLON, JT_COMMA, JT_STRING, JT_NUMBER, JT_TRUE, JT_FALSE,
    JT_NULL_TOK, JT_EOF, JT_ERROR
};

struct JToken {
    const char *start;
    int         len;
    JTok        type;
};

struct JParser {
    const char *pos;
};

static void jskip_ws(JParser *j) {
    while (*j->pos && (*j->pos == ' ' || *j->pos == '\t' ||
           *j->pos == '\n' || *j->pos == '\r'))
        j->pos++;
}

static JToken jnext(JParser *j) {
    JToken tok = { nullptr, 0, JT_NONE };
    jskip_ws(j);
    if (!*j->pos) { tok.type = JT_EOF; return tok; }

    tok.start = j->pos;
    char c = *j->pos;

    switch (c) {
    case '{': tok.type = JT_LBRACE;   tok.len = 1; j->pos++; break;
    case '}': tok.type = JT_RBRACE;   tok.len = 1; j->pos++; break;
    case '[': tok.type = JT_LBRACKET; tok.len = 1; j->pos++; break;
    case ']': tok.type = JT_RBRACKET; tok.len = 1; j->pos++; break;
    case ':': tok.type = JT_COLON;    tok.len = 1; j->pos++; break;
    case ',': tok.type = JT_COMMA;    tok.len = 1; j->pos++; break;
    case '"': {
        j->pos++;
        tok.start = j->pos;
        while (*j->pos && *j->pos != '"') {
            if (*j->pos == '\\') j->pos++;
            if (*j->pos) j->pos++;
        }
        tok.len = (int)(j->pos - tok.start);
        tok.type = JT_STRING;
        if (*j->pos == '"') j->pos++;
        break;
    }
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            if (c == '0' && (j->pos[1] == 'x' || j->pos[1] == 'X')) {
                j->pos += 2;
                while (isxdigit((unsigned char)*j->pos)) j->pos++;
            } else {
                if (c == '-') j->pos++;
                while (*j->pos >= '0' && *j->pos <= '9') j->pos++;
                if (*j->pos == '.') {
                    j->pos++;
                    while (*j->pos >= '0' && *j->pos <= '9') j->pos++;
                }
            }
            tok.len = (int)(j->pos - tok.start);
            tok.type = JT_NUMBER;
        } else if (strncmp(j->pos, "true", 4) == 0) {
            tok.type = JT_TRUE; tok.len = 4; j->pos += 4;
        } else if (strncmp(j->pos, "false", 5) == 0) {
            tok.type = JT_FALSE; tok.len = 5; j->pos += 5;
        } else if (strncmp(j->pos, "null", 4) == 0) {
            tok.type = JT_NULL_TOK; tok.len = 4; j->pos += 4;
        } else {
            tok.type = JT_ERROR; j->pos++;
        }
        break;
    }
    return tok;
}

static void jstr(const JToken *tok, char *dst, int maxlen) {
    if (tok->type != JT_STRING || !tok->start) { dst[0] = '\0'; return; }
    int n = tok->len < (maxlen - 1) ? tok->len : (maxlen - 1);
    memcpy(dst, tok->start, n);
    dst[n] = '\0';
}

static float jfloat(const JToken *tok) {
    if (tok->type != JT_NUMBER || !tok->start) return 0.0f;
    return (float)strtod(tok->start, nullptr);
}

static void jskip_value(JParser *j) {
    JToken tok = jnext(j);
    if (tok.type == JT_LBRACE) {
        int depth = 1;
        while (depth > 0) {
            tok = jnext(j);
            if (tok.type == JT_LBRACE) depth++;
            else if (tok.type == JT_RBRACE) depth--;
            else if (tok.type == JT_EOF || tok.type == JT_ERROR) return;
        }
    } else if (tok.type == JT_LBRACKET) {
        int depth = 1;
        while (depth > 0) {
            tok = jnext(j);
            if (tok.type == JT_LBRACKET) depth++;
            else if (tok.type == JT_RBRACKET) depth--;
            else if (tok.type == JT_EOF || tok.type == JT_ERROR) return;
        }
    }
    /* primitives already consumed by jnext() */
}

/* =========================================================================
 * Hex color parsing: "0060bf7f" → 0x0060bf7f
 * ========================================================================= */

static u32 parse_hex_color(const char *hex, int len)
{
    u32 val = 0;
    for (int i = 0; i < len && i < 8; i++) {
        char c = hex[i];
        u32 nibble;
        if (c >= '0' && c <= '9')      nibble = (u32)(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nibble = (u32)(c - 'A' + 10);
        else break;
        val = (val << 4) | nibble;
    }
    return val;
}

/* =========================================================================
 * Palette field names (order matches struct pdgui_palette in pdgui_style.cpp)
 * ========================================================================= */

/* S306: 15 legacy fields + 5 extensions (S306) + 4 more (S309).
 * The extension keys also accept friendlier camelCase aliases so modders
 * can use "toolbarTint" instead of the underscored legacy style
 * (see palette_field_index_alias). */
static const char *k_PaletteFieldNames[24] = {
    "dialog_border1",
    "dialog_titlebg",
    "dialog_border2",
    "dialog_titlefg",
    "dialog_bodybg",
    "unused14",
    "item_unfocused",
    "item_disabled",
    "item_focused_inner",
    "checkbox_checked",
    "item_focused_outer",
    "listgroup_headerbg",
    "listgroup_headerfg",
    "unused34",
    "unused38",
    /* S306 extensions */
    "toolbar_tint",
    "text_positive",
    "text_warning",
    "button_hover",
    "button_active",
    /* S309 extensions */
    "title_glow",
    "tint_success",
    "tint_danger",
    "tint_info",
};

static int palette_field_index(const char *name) {
    for (int i = 0; i < 24; i++) {
        if (strcmp(name, k_PaletteFieldNames[i]) == 0)
            return i;
    }
    /* S306 camelCase aliases for the extension fields */
    if (strcmp(name, "toolbarTint") == 0)  return 15;
    if (strcmp(name, "textPositive") == 0) return 16;
    if (strcmp(name, "textWarning") == 0)  return 17;
    if (strcmp(name, "buttonHover") == 0)  return 18;
    if (strcmp(name, "buttonActive") == 0) return 19;
    /* S309 camelCase aliases */
    if (strcmp(name, "titleGlow") == 0)    return 20;
    if (strcmp(name, "tintSuccess") == 0)  return 21;
    if (strcmp(name, "tintDanger") == 0)   return 22;
    if (strcmp(name, "tintInfo") == 0)     return 23;
    /* Backwards-compat: "checkmark" / "checkMark" as alias for
     * checkbox_checked so users editing by hand don't need to know the
     * legacy name. */
    if (strcmp(name, "checkmark") == 0 ||
        strcmp(name, "checkMark") == 0) return 9;
    return -1;
}

/* =========================================================================
 * JSON theme parser
 * ========================================================================= */

static s32 parse_theme_json(const char *src, struct theme_def *def)
{
    memset(def, 0, sizeof(*def));

    JParser jp = { src };
    JToken tok = jnext(&jp);
    if (tok.type != JT_LBRACE) return 0;

    char key[128];
    while (true) {
        tok = jnext(&jp);
        if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
        if (tok.type == JT_COMMA) continue;
        if (tok.type != JT_STRING) { jskip_value(&jp); continue; }

        jstr(&tok, key, sizeof(key));

        tok = jnext(&jp); /* colon */
        if (tok.type != JT_COLON) break;

        if (strcmp(key, "name") == 0) {
            tok = jnext(&jp);
            jstr(&tok, def->name, sizeof(def->name));
        } else if (strcmp(key, "author") == 0) {
            tok = jnext(&jp);
            jstr(&tok, def->author, sizeof(def->author));
        } else if (strcmp(key, "version") == 0) {
            tok = jnext(&jp);
            jstr(&tok, def->version, sizeof(def->version));
        } else if (strcmp(key, "soundPack") == 0) {
            tok = jnext(&jp);
            jstr(&tok, def->sound_pack, sizeof(def->sound_pack));
        } else if (strcmp(key, "menuStyle") == 0 || strcmp(key, "chromeStyle") == 0) {
            /* S305 P4: theme bundle — "menuStyle" (user-facing name) or
             * the legacy "chromeStyle" alias references a registered
             * chrome style id (e.g. "user.MyChrome.ui-chrome") that the
             * theme activation should apply. */
            tok = jnext(&jp);
            jstr(&tok, def->bundle_chrome_id, sizeof(def->bundle_chrome_id));
        } else if (strcmp(key, "font") == 0) {
            /* S305 P4: theme bundle — font mod id to activate alongside
             * the palette (e.g. "user.MyFont.font"). */
            tok = jnext(&jp);
            jstr(&tok, def->bundle_font_id, sizeof(def->bundle_font_id));
        } else if (strcmp(key, "palette") == 0) {
            /* Parse palette object: { "field_name": "hex", ... } */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_palette = 1;
            char field[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;

                jstr(&tok, field, sizeof(field));
                tok = jnext(&jp); /* colon */
                if (tok.type != JT_COLON) break;
                tok = jnext(&jp); /* value */

                int idx = palette_field_index(field);
                if (idx >= 0 && tok.type == JT_STRING) {
                    def->palette[idx] = parse_hex_color(tok.start, tok.len);
                }
            }
        } else if (strcmp(key, "textures") == 0) {
            /* Parse textures object: { "slot": "path", ... } */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;

                if (def->num_textures < THEME_TEX_SLOTS) {
                    struct theme_tex_override *t = &def->textures[def->num_textures];
                    jstr(&tok, t->slot_name, sizeof(t->slot_name));
                    tok = jnext(&jp); /* colon */
                    if (tok.type != JT_COLON) break;
                    tok = jnext(&jp);
                    jstr(&tok, t->file_path, sizeof(t->file_path));
                    def->num_textures++;
                } else {
                    jnext(&jp); /* colon */
                    jskip_value(&jp);
                }
            }
        } else if (strcmp(key, "scanline") == 0) {
            /* Parse scanline object */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_scanline = 1;
            def->scanline_enabled = 1;
            def->scanline_alpha = 0.8f;
            def->scanline_interval = 2;

            char skey[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;

                jstr(&tok, skey, sizeof(skey));
                tok = jnext(&jp); /* colon */
                if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(skey, "enabled") == 0) {
                    def->scanline_enabled = (tok.type == JT_TRUE) ? 1 : 0;
                } else if (strcmp(skey, "alpha") == 0) {
                    def->scanline_alpha = jfloat(&tok);
                } else if (strcmp(skey, "interval") == 0) {
                    def->scanline_interval = (s32)jfloat(&tok);
                }
            }
        } else if (strcmp(key, "textGlow") == 0) {
            /* Parse textGlow object */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_glow = 1;
            def->glow_enabled = 1;
            def->glow_intensity = 0.6f;
            def->glow_color = 0x0080ffffu;

            char gkey[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;

                jstr(&tok, gkey, sizeof(gkey));
                tok = jnext(&jp); /* colon */
                if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(gkey, "enabled") == 0) {
                    def->glow_enabled = (tok.type == JT_TRUE) ? 1 : 0;
                } else if (strcmp(gkey, "intensity") == 0) {
                    def->glow_intensity = jfloat(&tok);
                } else if (strcmp(gkey, "color") == 0 && tok.type == JT_STRING) {
                    def->glow_color = parse_hex_color(tok.start, tok.len);
                }
            }
        } else if (strcmp(key, "nineslices") == 0) {
            /* Parse nineslices array: [{ "id": "...", "left": N, ... }, ...] */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACKET) { jskip_value(&jp); continue; }

            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACKET || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_LBRACE) break;

                if (def->num_nineslices >= THEME_TEX_SLOTS) {
                    /* Skip this object */
                    int depth = 1;
                    while (depth > 0) {
                        tok = jnext(&jp);
                        if (tok.type == JT_LBRACE) depth++;
                        else if (tok.type == JT_RBRACE) depth--;
                        else if (tok.type == JT_EOF) break;
                    }
                    continue;
                }

                auto *ns = &def->nineslices[def->num_nineslices];
                memset(ns, 0, sizeof(*ns));

                char nkey[64];
                while (true) {
                    tok = jnext(&jp);
                    if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                    if (tok.type == JT_COMMA) continue;
                    if (tok.type != JT_STRING) break;
                    jstr(&tok, nkey, sizeof(nkey));
                    tok = jnext(&jp); /* colon */
                    if (tok.type != JT_COLON) break;
                    tok = jnext(&jp);

                    if (strcmp(nkey, "id") == 0)
                        jstr(&tok, ns->catalog_id, sizeof(ns->catalog_id));
                    else if (strcmp(nkey, "left") == 0)   ns->left   = (s32)jfloat(&tok);
                    else if (strcmp(nkey, "right") == 0)  ns->right  = (s32)jfloat(&tok);
                    else if (strcmp(nkey, "top") == 0)    ns->top    = (s32)jfloat(&tok);
                    else if (strcmp(nkey, "bottom") == 0) ns->bottom = (s32)jfloat(&tok);
                    else if (strcmp(nkey, "edgeMode") == 0) {
                        char m[32]; jstr(&tok, m, sizeof(m));
                        ns->edge_tile = (strcmp(m, "tile") == 0) ? 1 : 0;
                    }
                    else if (strcmp(nkey, "centerMode") == 0) {
                        char m[32]; jstr(&tok, m, sizeof(m));
                        ns->center_tile = (strcmp(m, "tile") == 0) ? 1 : 0;
                    }
                }
                def->num_nineslices++;
            }
        } else if (strcmp(key, "caustics") == 0) {
            /* Parse caustics array */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACKET) { jskip_value(&jp); continue; }

            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACKET || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_LBRACE) break;

                if (def->num_caustics >= 8) {
                    int depth = 1;
                    while (depth > 0) { tok = jnext(&jp); if (tok.type == JT_LBRACE) depth++; else if (tok.type == JT_RBRACE) depth--; else if (tok.type == JT_EOF) break; }
                    continue;
                }

                auto *cd = &def->caustics[def->num_caustics];
                memset(cd, 0, sizeof(*cd));
                cd->opacity = 0.5f;
                cd->speed = 4.0f;
                cd->frame_count = 1;
                cd->scale = 1.0f;

                char ckey[64];
                while (true) {
                    tok = jnext(&jp);
                    if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                    if (tok.type == JT_COMMA) continue;
                    if (tok.type != JT_STRING) break;
                    jstr(&tok, ckey, sizeof(ckey));
                    tok = jnext(&jp); if (tok.type != JT_COLON) break;
                    tok = jnext(&jp);

                    if (strcmp(ckey, "elementId") == 0) jstr(&tok, cd->element_id, sizeof(cd->element_id));
                    else if (strcmp(ckey, "textureId") == 0) jstr(&tok, cd->texture_id, sizeof(cd->texture_id));
                    else if (strcmp(ckey, "frameCount") == 0) cd->frame_count = (s32)jfloat(&tok);
                    else if (strcmp(ckey, "speed") == 0)   cd->speed = jfloat(&tok);
                    else if (strcmp(ckey, "opacity") == 0) cd->opacity = jfloat(&tok);
                    else if (strcmp(ckey, "scale") == 0)   cd->scale = jfloat(&tok);
                    else if (strcmp(ckey, "blendMode") == 0) {
                        char m[32]; jstr(&tok, m, sizeof(m));
                        if (strcmp(m, "additive") == 0) cd->blend_mode = 1;
                        else if (strcmp(m, "screen") == 0) cd->blend_mode = 2;
                        else cd->blend_mode = 0;
                    }
                }
                def->num_caustics++;
            }
        } else if (strcmp(key, "borderEffects") == 0) {
            /* Parse borderEffects array */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACKET) { jskip_value(&jp); continue; }

            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACKET || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_LBRACE) break;

                if (def->num_border_effects >= 8) {
                    int depth = 1;
                    while (depth > 0) { tok = jnext(&jp); if (tok.type == JT_LBRACE) depth++; else if (tok.type == JT_RBRACE) depth--; else if (tok.type == JT_EOF) break; }
                    continue;
                }

                auto *be = &def->border_effects[def->num_border_effects];
                memset(be, 0, sizeof(*be));
                be->opacity = 0.5f;
                be->tint_color = 0xffffffff;

                char bkey[64];
                while (true) {
                    tok = jnext(&jp);
                    if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                    if (tok.type == JT_COMMA) continue;
                    if (tok.type != JT_STRING) break;
                    jstr(&tok, bkey, sizeof(bkey));
                    tok = jnext(&jp); if (tok.type != JT_COLON) break;
                    tok = jnext(&jp);

                    if (strcmp(bkey, "elementId") == 0) jstr(&tok, be->element_id, sizeof(be->element_id));
                    else if (strcmp(bkey, "maskTextureId") == 0) jstr(&tok, be->mask_texture_id, sizeof(be->mask_texture_id));
                    else if (strcmp(bkey, "opacity") == 0) be->opacity = jfloat(&tok);
                    else if (strcmp(bkey, "tintColor") == 0 && tok.type == JT_STRING) be->tint_color = parse_hex_color(tok.start, tok.len);
                    else if (strcmp(bkey, "scrollX") == 0)  be->scroll_x = jfloat(&tok);
                    else if (strcmp(bkey, "scrollY") == 0)  be->scroll_y = jfloat(&tok);
                    else if (strcmp(bkey, "blendMode") == 0) {
                        char m[32]; jstr(&tok, m, sizeof(m));
                        if (strcmp(m, "additive") == 0) be->blend_mode = 1;
                        else if (strcmp(m, "screen") == 0) be->blend_mode = 2;
                        else be->blend_mode = 0;
                    }
                }
                def->num_border_effects++;
            }
        } else if (strcmp(key, "font") == 0) {
            /* Parse font object */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_font = 1;
            def->font_size = 24.0f;

            char fkey[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;
                jstr(&tok, fkey, sizeof(fkey));
                tok = jnext(&jp); if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(fkey, "name") == 0) jstr(&tok, def->font_name, sizeof(def->font_name));
                else if (strcmp(fkey, "path") == 0) jstr(&tok, def->font_path, sizeof(def->font_path));
                else if (strcmp(fkey, "size") == 0) def->font_size = jfloat(&tok);
            }
        } else if (strcmp(key, "fontShadow") == 0) {
            /* Parse fontShadow object */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_shadow = 1;
            def->shadow_offset_x = 1.0f;
            def->shadow_offset_y = 1.0f;
            def->shadow_color = 0x000000A0u;

            char skey2[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;
                jstr(&tok, skey2, sizeof(skey2));
                tok = jnext(&jp); if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(skey2, "offsetX") == 0) def->shadow_offset_x = jfloat(&tok);
                else if (strcmp(skey2, "offsetY") == 0) def->shadow_offset_y = jfloat(&tok);
                else if (strcmp(skey2, "color") == 0 && tok.type == JT_STRING) def->shadow_color = parse_hex_color(tok.start, tok.len);
            }
        } else if (strcmp(key, "fontGlow") == 0) {
            /* Parse fontGlow object */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_font_glow = 1;
            def->font_glow_radius = 3.0f;
            def->font_glow_intensity = 0.6f;
            def->font_glow_color = 0x0080ffffu;
            def->font_glow_passes = 2;

            char gk[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;
                jstr(&tok, gk, sizeof(gk));
                tok = jnext(&jp); if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(gk, "radius") == 0) def->font_glow_radius = jfloat(&tok);
                else if (strcmp(gk, "intensity") == 0) def->font_glow_intensity = jfloat(&tok);
                else if (strcmp(gk, "color") == 0 && tok.type == JT_STRING) def->font_glow_color = parse_hex_color(tok.start, tok.len);
                else if (strcmp(gk, "passes") == 0) def->font_glow_passes = (s32)jfloat(&tok);
            }
        } else {
            jskip_value(&jp);
        }
    }

    return 1;
}

/* =========================================================================
 * Apply a parsed theme definition to the active rendering state
 * ========================================================================= */

/* Forward declarations for pdgui_style.cpp internals we need to reach.
 * The palette is a flat u32[15] that we can set via pdguiSetPaletteCustom(). */

/**
 * We expose a new function from pdgui_style.cpp for setting a custom palette
 * from raw u32[15] values. This avoids duplicating the palette struct.
 */
extern "C" void pdguiSetPaletteCustom(const u32 *colors15);
extern "C" void pdguiSetPaletteExtensions(u32 toolbarTint, u32 textPositive,
                                          u32 textWarning, u32 buttonHover,
                                          u32 buttonActive);
extern "C" void pdguiSetPaletteExtensions2(u32 titleGlow, u32 tintSuccess,
                                           u32 tintDanger, u32 tintInfo);

static void apply_theme_def(const struct theme_def *def)
{
    /* Apply palette if present */
    if (def->has_palette) {
        pdguiSetPaletteCustom(def->palette);
        /* S306: push the extension tail (indices 15-19). Fields left at 0
         * in theme.json tell the style apply to derive defaults. */
        pdguiSetPaletteExtensions(def->palette[15],
                                  def->palette[16],
                                  def->palette[17],
                                  def->palette[18],
                                  def->palette[19]);
        /* S309: push the extension-2 tail (indices 20-23). */
        pdguiSetPaletteExtensions2(def->palette[20],
                                   def->palette[21],
                                   def->palette[22],
                                   def->palette[23]);
    }

    /* Apply scanline settings */
    if (def->has_scanline) {
        pdguiThemeSetScanlineEnabled(def->scanline_enabled);
        pdguiThemeSetScanlineAlpha(def->scanline_alpha);
    }

    /* Apply text glow settings */
    if (def->has_glow) {
        if (def->glow_enabled) {
            pdguiThemeSetTextGlow((f32)def->glow_intensity, def->glow_color);
        } else {
            pdguiThemeSetTextGlow(0.0f, 0);
        }
    }

    /* Apply sound pack */
    if (def->sound_pack[0]) {
        if (strcmp(def->sound_pack, "default") == 0) {
            pdguiThemeSetSoundPack(0);
        }
        /* Future: named sound pack lookup */
    }

    /* P4: Apply 9-slice definitions */
    for (s32 i = 0; i < def->num_nineslices; i++) {
        nineslice_def_t ns;
        memset(&ns, 0, sizeof(ns));
        ns.left   = def->nineslices[i].left;
        ns.right  = def->nineslices[i].right;
        ns.top    = def->nineslices[i].top;
        ns.bottom = def->nineslices[i].bottom;
        ns.edge_mode   = def->nineslices[i].edge_tile ? NINESLICE_TILE : NINESLICE_STRETCH;
        ns.center_mode = def->nineslices[i].center_tile ? NINESLICE_TILE : NINESLICE_STRETCH;
        /* pdguiNinesliceRegister back-fills src/dst/per-edge from legacy short form. */
        pdguiNinesliceRegister(def->nineslices[i].catalog_id, &ns);
    }

    /* P4: Apply caustic effects */
    for (s32 i = 0; i < def->num_caustics; i++) {
        caustic_def_t cd;
        memset(&cd, 0, sizeof(cd));
        snprintf(cd.texture_id, sizeof(cd.texture_id), "%s", def->caustics[i].texture_id);
        cd.frame_count = def->caustics[i].frame_count;
        cd.speed       = def->caustics[i].speed;
        cd.opacity     = def->caustics[i].opacity;
        cd.blend_mode  = def->caustics[i].blend_mode;
        cd.scale       = def->caustics[i].scale;
        pdguiEffectsSetCaustic(def->caustics[i].element_id, &cd);
    }

    /* P4: Apply border effects */
    for (s32 i = 0; i < def->num_border_effects; i++) {
        border_fx_def_t bd;
        memset(&bd, 0, sizeof(bd));
        snprintf(bd.mask_texture_id, sizeof(bd.mask_texture_id), "%s",
                 def->border_effects[i].mask_texture_id);
        bd.opacity       = def->border_effects[i].opacity;
        bd.blend_mode    = def->border_effects[i].blend_mode;
        bd.tint_color    = def->border_effects[i].tint_color;
        bd.scroll_speed_x = def->border_effects[i].scroll_x;
        bd.scroll_speed_y = def->border_effects[i].scroll_y;
        pdguiEffectsSetBorderFx(def->border_effects[i].element_id, &bd);
    }

    /* S305 P4: Apply bundled components.  A theme mod may name a chrome
     * style + font mod to activate alongside its palette — one theme
     * activation swaps the whole visual identity.  Fails silently if the
     * referenced id isn't registered (user hasn't installed that mod yet). */
    if (def->bundle_chrome_id[0]) {
        pdguiThemeSetUiChromeStyleId(def->bundle_chrome_id);
        pdguiThemeSetUiChromeEnabled(1);
        pdguiSetPanelNineSlice(def->bundle_chrome_id);
        pdguiChromeSetEnabled(1);
        sysLogPrintf(LOG_NOTE,
            "PDGUI theme bundle: menuStyle='%s' applied", def->bundle_chrome_id);
    }
    if (def->bundle_font_id[0]) {
        pdguiFontModSetActiveId(def->bundle_font_id);
        sysLogPrintf(LOG_NOTE,
            "PDGUI theme bundle: font='%s' applied (restart to take effect)",
            def->bundle_font_id);
    }

    /* P4: Apply font override */
    if (def->has_font && def->font_path[0]) {
        s32 slot = pdguiFontMgrLoadFont(def->font_name, def->font_path, def->font_size);
        if (slot > 0) {
            pdguiFontMgrSetActive(slot);
        }
    }

    /* P4: Apply font shadow */
    if (def->has_shadow) {
        font_shadow_def_t sh;
        sh.offset_x = def->shadow_offset_x;
        sh.offset_y = def->shadow_offset_y;
        sh.color    = def->shadow_color;
        pdguiFontMgrSetShadow(&sh);
    }

    /* P4: Apply font glow */
    if (def->has_font_glow) {
        font_glow_def_t fg;
        fg.radius    = def->font_glow_radius;
        fg.intensity = def->font_glow_intensity;
        fg.color     = def->font_glow_color;
        fg.passes    = def->font_glow_passes;
        pdguiFontMgrSetGlow(&fg);
    }
}

/* =========================================================================
 * Built-in palette data (mirrors pdgui_style.cpp; avoids types.h include)
 *
 * These must stay in sync with s_Palette* in pdgui_style.cpp.
 * ========================================================================= */

static const u32 k_BuiltinPalettes[7][15] = {
    /* 0: Grey */
    { 0x20202000, 0x20202000, 0x20202000, 0x4f4f4f00, 0x00000000,
      0x00000000, 0x4f4f4f00, 0x4f4f4f00, 0x4f4f4f00, 0x4f4f4f00,
      0x00000000, 0x00000000, 0x4f4f4f00, 0x00000000, 0x00000000 },
    /* 1: Blue */
    { 0x0060bf7fu, 0x0000507fu, 0x00f0ff7fu, 0xffffffffu, 0x00002f9fu,
      0x00006f7fu, 0x00ffffffu, 0x007f7fffu, 0xffffffffu, 0x8fffffffu,
      0x000044ffu, 0x000030ffu, 0x7f7fffffu, 0xffffffffu, 0x6644ff7fu },
    /* 2: Red */
    { 0xbf00007fu, 0x5000007fu, 0xff00007fu, 0xffff00ffu, 0x2f00009fu,
      0x6f00007fu, 0xff9070ffu, 0x7f0000ffu, 0xffff00ffu, 0xffa090ffu,
      0x440000ffu, 0x003000ffu, 0xffff00ffu, 0xffffffffu, 0xff44447fu },
    /* 3: Green */
    { 0x00bf007fu, 0x0050007fu, 0x00ff007fu, 0xffff00ffu, 0x002f009fu,
      0x00ff0028u, 0x55ff55ffu, 0x006f00afu, 0xffffffffu, 0x00000000u,
      0x004400ffu, 0x003000ffu, 0xffff00ffu, 0xffffffffu, 0x44ff447fu },
    /* 4: White */
    { 0xffffffffu, 0xffffff7fu, 0xffffffffu, 0xffffffffu, 0xffffff9fu,
      0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
      0x00000000u, 0xffffff5fu, 0xffffffffu, 0xffffff7fu, 0xffffffffu },
    /* 5: Silver */
    { 0xaaaaaaffu, 0xaaaaaa7fu, 0xaaaaaaffu, 0xffffffffu, 0xffffff9fu,
      0xffffffffu, 0xffffffffu, 0xffffffffu, 0xff8888ffu, 0xffffffffu,
      0x00000000u, 0xffffff5fu, 0xffffffffu, 0xffffff7fu, 0xffffffffu },
    /* 6: Black & Gold */
    { 0xbf8f207fu, 0x1408007fu, 0xffc8407fu, 0xffd060ffu, 0x0a06009fu,
      0x3f2a107fu, 0xdda830ffu, 0x6f5020ffu, 0xffffffffu, 0xffd060ffu,
      0x2a1800ffu, 0x1a0e00ffu, 0xdda830ffu, 0xffffffffu, 0xbf8f207fu },
};

static const char *k_BuiltinNames[7] = {
    "PD Grey", "PD Blue", "PD Red", "PD Green",
    "PD White", "PD Silver", "PD Black & Gold"
};

static const char *k_BuiltinIds[7] = {
    "base:theme_grey",  "base:theme_blue",  "base:theme_red",
    "base:theme_green", "base:theme_white", "base:theme_silver",
    "base:theme_blackgold"
};

/* Default glow colors per palette (derived from dialog_border2) */
static const u32 k_BuiltinGlowColors[7] = {
    0x20202080u, /* grey */
    0x00f0ff80u, /* blue: cyan */
    0xff000080u, /* red */
    0x00ff0080u, /* green */
    0xffffff80u, /* white */
    0xaaaaaa80u, /* silver */
    0xffc84080u, /* gold */
};

/* =========================================================================
 * Seen-mods helpers (first-sight detection for default-enabled policy)
 * ========================================================================= */

static bool theme_slug_is_seen(const char *slug)
{
    if (!slug || !slug[0] || !s_SeenModThemes[0]) return false;
    size_t slen = strlen(slug);
    const char *p = s_SeenModThemes;
    while (*p) {
        if (strncmp(p, slug, slen) == 0 && (p[slen] == ',' || p[slen] == '\0'))
            return true;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
    return false;
}

static void theme_mark_slug_seen(const char *slug)
{
    if (!slug || !slug[0]) return;
    if (theme_slug_is_seen(slug)) return;
    size_t cur = strlen(s_SeenModThemes);
    size_t slen = strlen(slug);
    if (cur > 0 && cur + 1 + slen < sizeof(s_SeenModThemes)) {
        s_SeenModThemes[cur] = ',';
        memcpy(s_SeenModThemes + cur + 1, slug, slen + 1);
    } else if (cur == 0 && slen < sizeof(s_SeenModThemes)) {
        memcpy(s_SeenModThemes, slug, slen + 1);
    }
}

/* =========================================================================
 * Registry helpers
 * ========================================================================= */

static struct theme_entry *find_entry(const char *catalog_id)
{
    for (s32 i = 0; i < s_ThemeCount; i++) {
        if (strcmp(s_Themes[i].catalog_id, catalog_id) == 0)
            return &s_Themes[i];
    }
    return nullptr;
}

static struct theme_entry *add_entry(const char *catalog_id, const char *name,
                                     const char *filepath, s32 palette_index)
{
    if (s_ThemeCount >= THEME_MAX_REGISTERED) return nullptr;
    struct theme_entry *e = &s_Themes[s_ThemeCount++];
    snprintf(e->catalog_id, sizeof(e->catalog_id), "%s", catalog_id);
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->filepath, sizeof(e->filepath), "%s", filepath ? filepath : "");
    e->palette_index = palette_index;
    e->enabled = 1;       /* default-enabled policy */
    e->first_sight = 0;
    e->cfg_enabled_key[0] = '\0';
    e->embed_data = nullptr;
    e->embed_size = 0;
    return e;
}

/* =========================================================================
 * 2026-04-11: Mod directory scan for custom theme.json files
 *
 * The theme editor's "Save as Mod" path writes to `mods/<slug>/theme.json`
 * + `mods/<slug>/mod.json`.  Before this scan, those files were written
 * to disk but never registered in the theme registry, so they never
 * appeared in either the theme editor's Load Theme dropdown or the
 * Settings → Debug UI Theme selector.  This scanner walks `mods/` at
 * init and registers every `theme.json` it finds under the catalog ID
 * `mod:<slug>` (matching the legacy mod naming convention so it sorts
 * cleanly next to the `base:*` builtins).
 *
 * Parser is intentionally minimal — we only need the display name.  The
 * full `parse_theme_json()` path runs later when the user actually loads
 * the theme via pdguiThemeLoadFromCatalog().
 * ========================================================================= */

/** Extract the "name" field from a theme.json payload.  Returns a pointer
 *  into scratch if found, or a fallback if the JSON is malformed. */
/* Extract the first quoted value after a JSON key.  Returns true if found. */
static bool extract_json_string(const char *json, const char *key,
                                 char *out, size_t outlen)
{
    const char *p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;
    const char *q = p;
    while (*q && *q != '"' && (size_t)(q - p) < outlen - 1) q++;
    size_t n = (size_t)(q - p);
    if (n == 0) return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static void extract_theme_name(const char *json, char *out, size_t outlen,
                               const char *fallback)
{
    out[0] = '\0';

    if (json) {
        /* Prefer "display_name" (mod.json convention), fall back to "name" */
        if (extract_json_string(json, "\"display_name\"", out, outlen)) {
            return;
        }
        if (extract_json_string(json, "\"name\"", out, outlen)) {
            return;
        }
    }

    /* Fallback: use the directory slug, with the first letter capitalised */
    snprintf(out, outlen, "%s", fallback ? fallback : "Custom Theme");
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
    /* Replace dashes with spaces for display */
    for (char *c = out; *c; c++) {
        if (*c == '-' || *c == '_') *c = ' ';
    }
}

/** Register a single mod directory as a theme.
 *  Checks for `mods/<slug>/theme.json` first, then falls back to
 *  `mods/<slug>/mod.json` (which may contain a "theme" section). */
static void register_mod_theme_dir(const char *mods_dir, const char *slug)
{
    char theme_path[THEME_FILEPATH_LEN];
    snprintf(theme_path, sizeof(theme_path), "%s/%s/theme.json", mods_dir, slug);

    /* Check for theme.json first, then fall back to mod.json with "theme" key */
    struct stat st;
    bool found_theme_json = (stat(theme_path, &st) == 0 && S_ISREG(st.st_mode));
    if (!found_theme_json) {
        snprintf(theme_path, sizeof(theme_path), "%s/%s/mod.json", mods_dir, slug);
        if (stat(theme_path, &st) != 0 || !S_ISREG(st.st_mode)) return;
        /* Quick check: does mod.json contain a "theme" key? */
        u32 sz = 0;
        char *raw = (char *)fsFileLoad(theme_path, &sz);
        if (!raw) return;
        bool has_theme = (sz > 0 && strstr(raw, "\"theme\"") != NULL);
        free(raw);
        if (!has_theme) return;
    }

    /* Skip if already registered (e.g. re-init) */
    char catalog_id[THEME_CATALOG_ID_LEN];
    snprintf(catalog_id, sizeof(catalog_id), "mod:%s", slug);
    if (find_entry(catalog_id)) {
        sysLogPrintf(LOG_NOTE,
            "PDGUI theme loader: '%s' already registered, skipping", catalog_id);
        return;
    }

    /* Read the file just to extract the display name.  Malformed JSON is
     * fine here — extract_theme_name has a fallback to the slug. */
    u32 fileSize = 0;
    char *raw = (char *)fsFileLoad(theme_path, &fileSize);
    char display_name[THEME_NAME_LEN] = {0};

    if (raw && fileSize > 0) {
        char *json = (char *)malloc(fileSize + 1);
        if (json) {
            memcpy(json, raw, fileSize);
            json[fileSize] = '\0';
            extract_theme_name(json, display_name, sizeof(display_name), slug);
            free(json);
        }
        free(raw);
    } else {
        extract_theme_name(nullptr, display_name, sizeof(display_name), slug);
    }

    /* palette_index = -1 means "JSON-based, not a built-in palette".
     * pdguiThemeLoadFromCatalog() checks pdguiThemeIdToPaletteIndex first,
     * falls through to find_entry + filepath load for non-builtin IDs. */
    struct theme_entry *e = add_entry(catalog_id, display_name, theme_path, -1);
    if (e) {
        /* Default-enabled policy: persist enabled flag in pd.ini.
         * Key format: Theme.Enable.<slug>  (slug is the mod directory name).
         * First registration defaults to enabled=1; subsequent sessions
         * respect the stored value (user may have toggled off). */
        snprintf(e->cfg_enabled_key, sizeof(e->cfg_enabled_key),
                 "Theme.Enable.%s", slug);
        e->enabled = 1;
        configRegisterInt(e->cfg_enabled_key, &e->enabled, 0, 1);

        /* First-sight detection: if this slug wasn't in the seen list
         * from a prior session, it's brand new — auto-apply later. */
        e->first_sight = !theme_slug_is_seen(slug);
        if (e->first_sight) {
            theme_mark_slug_seen(slug);
        }

        sysLogPrintf(LOG_NOTE,
            "PDGUI theme loader: registered mod theme '%s' (\"%s\") from %s [enabled=%d, first_sight=%d]",
            catalog_id, display_name, theme_path, e->enabled, e->first_sight);
    }
}

/* Return true if `dirpath` contains theme.json, or a mod.json whose body
 * mentions a "theme" key. Used to decide whether a subdir is a leaf mod
 * (don't descend further) or a category folder (descend one level). */
static bool dir_has_theme_or_mod(const char *dirpath)
{
    char p[THEME_FILEPATH_LEN];
    struct stat st;
    snprintf(p, sizeof(p), "%s/theme.json", dirpath);
    if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) return true;
    snprintf(p, sizeof(p), "%s/mod.json", dirpath);
    if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) return true;
    return false;
}

/* Returns 1 if `name` matches a top-level reserved trust-gate folder
 * (per modmgr.h). Reserved folders are read-only-for-browsing and must
 * never be walked for theme content -- they may hold untrusted content.
 * Only applies at the root depth; recursed subdirs may legitimately be
 * named e.g. mods/UI Chrome/shared/. */
static bool is_reserved_top_level(const char *name)
{
    static const char *const reserved[MODMGR_RESERVED_NAMES_COUNT] = MODMGR_RESERVED_NAMES_LIST;
    if (!name) return false;
    for (s32 i = 0; i < MODMGR_RESERVED_NAMES_COUNT; i++) {
        if (strcmp(name, reserved[i]) == 0) return true;
    }
    return false;
}

/* Returns 1 if `name` ends in ".legacy_backup". The M-4.1 migrator renames
 * folder mods to <name>.legacy_backup after packaging; the renamed folders
 * must NOT be re-discovered as live mods. */
static bool has_legacy_backup_suffix(const char *name)
{
    if (!name) return false;
    static const char k_suffix[] = ".legacy_backup";
    size_t n = strlen(name);
    size_t s = sizeof(k_suffix) - 1;
    return n > s && strcmp(name + n - s, k_suffix) == 0;
}

/* Returns 1 if `name` ends in `.pdmod` (case-insensitive). Used to
 * enumerate archive mods alongside folder mods during the theme scan. */
static bool has_pdmod_extension(const char *name)
{
    if (!name) return false;
    size_t n = strlen(name);
    if (n < 7) return false;
    const char *t = name + n - 6;
    return (t[0] == '.') &&
           (t[1] == 'p' || t[1] == 'P') &&
           (t[2] == 'd' || t[2] == 'D') &&
           (t[3] == 'm' || t[3] == 'M') &&
           (t[4] == 'o' || t[4] == 'O') &&
           (t[5] == 'd' || t[5] == 'D');
}

/* Compute the slug for an archive mod from its on-disk filename: strip
 * the trailing ".pdmod". Truncates if the resulting slug exceeds out_cap. */
static void archive_filename_to_slug(const char *filename, char *out, size_t out_cap)
{
    if (!filename || !out || out_cap == 0) {
        if (out && out_cap > 0) out[0] = '\0';
        return;
    }
    size_t n = strlen(filename);
    if (n >= 6) n -= 6;  /* strip ".pdmod" */
    if (n >= out_cap) n = out_cap - 1;
    memcpy(out, filename, n);
    out[n] = '\0';
}

/* Register a single .pdmod archive as a theme. Opens the archive, looks
 * for theme.json (or mod.json with a "theme" key), and stashes the bytes
 * in the theme entry's embed_data so the apply path bypasses fsFileLoad. */
static void register_mod_theme_archive(const char *archive_path)
{
    if (!archive_path || !archive_path[0]) return;

    /* Derive slug from archive leaf name (strip .pdmod). */
    const char *leaf = archive_path;
    for (const char *q = archive_path; *q; q++) {
        if (*q == '/' || *q == '\\') leaf = q + 1;
    }
    char slug[THEME_CATALOG_ID_LEN];
    archive_filename_to_slug(leaf, slug, sizeof(slug));
    if (!slug[0]) return;

    /* Skip if already registered (rescan idempotency). */
    char catalog_id[THEME_CATALOG_ID_LEN];
    snprintf(catalog_id, sizeof(catalog_id), "mod:%s", slug);
    if (find_entry(catalog_id)) {
        sysLogPrintf(LOG_NOTE,
            "PDGUI theme loader: '%s' already registered, skipping (archive)", catalog_id);
        return;
    }

    mod_archive_t *arc = modArchiveOpen(archive_path);
    if (!arc) return;

    /* Look for theme.json first (canonical theme mod). */
    char *bytes = nullptr;
    u32   size  = 0;
    s32   idx   = modArchiveFindEntry(arc, "theme.json");
    if (idx >= 0) {
        bytes = (char *)modArchiveExtractAlloc(arc, idx, &size);
    } else {
        /* Fall back to mod.json with a "theme" key (legacy themes that put
         * the colour fields directly in their manifest). */
        u32 mfstSize = 0;
        char *mfst = modArchiveReadManifest(arc, &mfstSize);
        if (mfst) {
            if (mfstSize > 0 && strstr(mfst, "\"theme\"") != nullptr) {
                bytes = mfst;
                size = mfstSize;
                mfst = nullptr;  /* ownership transferred */
            }
            free(mfst);
        }
    }
    modArchiveClose(arc);

    if (!bytes || size == 0) {
        free(bytes);
        return;
    }

    /* Extract a display name. The bytes buffer needs NUL termination for
     * the strstr-based extractor; modArchive*Alloc adds a trailing NUL but
     * be defensive in case the contract drifts. */
    char *json = (char *)malloc(size + 1);
    if (!json) {
        free(bytes);
        return;
    }
    memcpy(json, bytes, size);
    json[size] = '\0';
    char display_name[THEME_NAME_LEN] = {0};
    extract_theme_name(json, display_name, sizeof(display_name), slug);
    free(json);

    /* Register the theme entry. filepath is purely informational for archive
     * themes -- the apply path uses embed_data when present. */
    char info_path[THEME_FILEPATH_LEN];
    snprintf(info_path, sizeof(info_path), "%s::theme.json", archive_path);
    struct theme_entry *e = add_entry(catalog_id, display_name, info_path, -1);
    if (!e) {
        free(bytes);
        return;
    }
    e->embed_data = bytes;
    e->embed_size = size;

    snprintf(e->cfg_enabled_key, sizeof(e->cfg_enabled_key),
             "Theme.Enable.%s", slug);
    e->enabled = 1;
    configRegisterInt(e->cfg_enabled_key, &e->enabled, 0, 1);

    e->first_sight = !theme_slug_is_seen(slug);
    if (e->first_sight) {
        theme_mark_slug_seen(slug);
    }

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme loader: registered mod theme '%s' (\"%s\") from %s [enabled=%d, first_sight=%d, archive]",
        catalog_id, display_name, archive_path, e->enabled, e->first_sight);
}

/* Walk a mods-tree root, registering every theme mod directly under it,
 * and for any manifest-less subdirectory, recurse ONE more level (category
 * folder support — e.g., mods/UI Chrome/<slug>/). Tracks walked count via
 * the passed-in pointer. */
static void scan_themes_in_root(const char *root, int allow_recurse, int *walked)
{
    if (!root || !root[0]) return;
    DIR *d = opendir(root);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        const char *name = ent->d_name;
        if (!name || name[0] == '.') continue;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;

        char subdir[THEME_FILEPATH_LEN];
        snprintf(subdir, sizeof(subdir), "%s/%s", root, name);
        struct stat st;
        if (stat(subdir, &st) != 0) continue;

        /* Archive mod (.pdmod / .zip with mod.json). Only at the root level;
         * archives under category folders also get picked up via recurse. */
        if (S_ISREG(st.st_mode)) {
            if (has_pdmod_extension(name)) {
                register_mod_theme_archive(subdir);
                if (walked) (*walked)++;
            }
            continue;
        }
        if (!S_ISDIR(st.st_mode)) continue;

        /* B-238 follow-up trust-gate alignment: at the root level, skip
         * reserved trust-gate folders (shared/, inbox/, untrusted/) and
         * any .legacy_backup migration residue. allow_recurse is set on
         * the top-level call only; nested category folders are still
         * walked normally. */
        if (allow_recurse) {
            if (is_reserved_top_level(name)) continue;
            if (has_legacy_backup_suffix(name)) continue;
        }

        if (dir_has_theme_or_mod(subdir)) {
            register_mod_theme_dir(root, name);
            if (walked) (*walked)++;
        } else if (allow_recurse) {
            /* Treat as category folder. Depth-1 only. */
            scan_themes_in_root(subdir, 0, walked);
        }
    }
    closedir(d);
}

/** Scan the `mods/` directory and register every `<slug>/theme.json` found. */
static void scan_mods_for_themes(void)
{
    /* Match modmgr search order so theme discovery follows the same roots:
     * $E/../mods, ./mods, $E/mods, then base-dir mods fallback. */
    char candidateBufs[4][THEME_FILEPATH_LEN];
    fsFullPath("$E/../mods", candidateBufs[0], sizeof(candidateBufs[0]));
    snprintf(candidateBufs[1], sizeof(candidateBufs[1]), "%s", "mods");
    fsFullPath("$E/mods", candidateBufs[2], sizeof(candidateBufs[2]));
    fsFullPath("mods",    candidateBufs[3], sizeof(candidateBufs[3]));

    int walked = 0;
    for (int ci = 0; ci < 4; ci++) {
        if (!candidateBufs[ci][0]) continue;

        /* Avoid re-scanning duplicate resolved paths. */
        bool dup = false;
        for (int pj = 0; pj < ci; pj++) {
            if (candidateBufs[pj][0] && strcmp(candidateBufs[pj], candidateBufs[ci]) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        scan_themes_in_root(candidateBufs[ci], 1, &walked);
    }

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme loader: scanned mods/ — %d directories walked, %d total themes registered",
        walked, s_ThemeCount);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

void pdguiThemeLoaderInit(void)
{
    if (s_LoaderInitDone) return;
    s_LoaderInitDone = 1;

    /* Register pd.ini config vars for active theme + seen-mods tracking */
    configRegisterString(THEME_CFG_KEY, s_CfgThemeId, sizeof(s_CfgThemeId));
    configRegisterString(THEME_SEEN_KEY, s_SeenModThemes, sizeof(s_SeenModThemes));

    /* If config had a saved theme, use it */
    if (s_CfgThemeId[0]) {
        snprintf(s_ActiveThemeId, sizeof(s_ActiveThemeId), "%s", s_CfgThemeId);
    }

    /* Register all 7 built-in palettes as catalog assets */
    for (int i = 0; i < 7; i++) {
        asset_entry_t *ae = assetCatalogRegister(k_BuiltinIds[i], ASSET_UI);
        if (ae) {
            snprintf(ae->category, CATALOG_CATEGORY_LEN, "base");
            ae->bundled    = 1;
            ae->enabled    = 1;
            ae->load_state = ASSET_STATE_LOADED;
            ae->ref_count  = ASSET_REF_BUNDLED;
        }
        add_entry(k_BuiltinIds[i], k_BuiltinNames[i], nullptr, i);
    }

    /* 2026-04-11: scan mods/ for custom theme.json files (Save-as-Mod
     * output from the theme editor, plus any user-authored mod themes). */
    scan_mods_for_themes();

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme loader: init — %d themes registered (built-in + mods), active='%s'",
        s_ThemeCount, s_ActiveThemeId);

    /* Default-enabled policy: auto-apply the first newly-detected mod theme.
     * "First-sight" means this slug was not in Theme.SeenMods from a prior
     * session.  Only one theme auto-applies per init (the first found). */
    for (s32 i = 0; i < s_ThemeCount; i++) {
        if (s_Themes[i].palette_index < 0 && s_Themes[i].first_sight
            && s_Themes[i].enabled) {
            sysLogPrintf(LOG_NOTE,
                "PDGUI theme loader: auto-applying first-sight theme '%s'",
                s_Themes[i].catalog_id);
            pdguiThemeLoadFromCatalog(s_Themes[i].catalog_id);
            configSave("pd.ini");
            break;
        }
    }

    /* S305: apply the configured theme (if no first-sight override already
     * applied one).  Previously only built-in palette themes were applied on
     * startup, because the fallback only called pdguiThemeSetPalette directly;
     * mod themes (file-based theme.json under mods/) resolved via
     * pdguiThemeLoadFromCatalog were silently skipped on every restart,
     * reverting the user to the default built-in palette.  Route through
     * pdguiThemeLoadFromCatalog so BOTH builtin and mod themes apply. */
    if (s_ActiveThemeId[0]) {
        s32 palIdx = pdguiThemeIdToPaletteIndex(s_ActiveThemeId);
        if (palIdx >= 0) {
            pdguiThemeSetPalette(palIdx);
        } else {
            /* Mod theme — load from the registry (which scan_mods_for_themes
             * has already populated) so theme_def JSON gets applied. */
            s32 ok = pdguiThemeLoadFromCatalog(s_ActiveThemeId);
            if (!ok) {
                sysLogPrintf(LOG_WARNING,
                    "PDGUI theme loader: saved theme '%s' not resolvable on startup — "
                    "falling back to default palette",
                    s_ActiveThemeId);
            }
        }
    }
}

void pdguiThemeLoaderShutdown(void)
{
    s_ThemeCount = 0;
    s_LoaderInitDone = 0;
}

/** Issue 2/8: Rescan mods/ for new theme.json files after modmgrApplyChanges().
 *  Bypasses the s_LoaderInitDone gate — safe to call after init is complete. */
void pdguiThemeRescanMods(void)
{
    scan_mods_for_themes();
}

s32 pdguiThemeLoadFromCatalog(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) return 0;

    /* Check if it's a built-in palette */
    s32 palIdx = pdguiThemeIdToPaletteIndex(catalog_id);
    if (palIdx >= 0) {
        pdguiThemeSetPalette(palIdx);

        /* Apply built-in glow color */
        pdguiThemeSetTextGlow(0.6f, k_BuiltinGlowColors[palIdx]);

        /* Persist */
        snprintf(s_ActiveThemeId, sizeof(s_ActiveThemeId), "%s", catalog_id);
        snprintf(s_CfgThemeId, sizeof(s_CfgThemeId), "%s", catalog_id);

        sysLogPrintf(LOG_NOTE,
            "PDGUI theme loader: applied built-in '%s' (palette %d)",
            catalog_id, palIdx);
        return 1;
    }

    /* Look up in registry for file-based theme */
    struct theme_entry *entry = find_entry(catalog_id);
    if (!entry || !entry->filepath[0]) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI theme loader: '%s' not found in registry", catalog_id);
        return 0;
    }

    /* B-238 follow-up: archive-sourced themes hold their JSON bytes in
     * embed_data. Apply directly without going through fsFileLoad. */
    char *json = nullptr;
    u32 fileSize = 0;
    if (entry->embed_data && entry->embed_size > 0) {
        fileSize = entry->embed_size;
        json = (char *)malloc(fileSize + 1);
        if (!json) return 0;
        memcpy(json, entry->embed_data, fileSize);
        json[fileSize] = '\0';
    } else {
        char *data = (char *)fsFileLoad(entry->filepath, &fileSize);
        if (!data) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI theme loader: failed to load '%s'", entry->filepath);
            return 0;
        }
        json = (char *)malloc(fileSize + 1);
        if (!json) { free(data); return 0; }
        memcpy(json, data, fileSize);
        json[fileSize] = '\0';
        free(data);
    }

    struct theme_def def;
    s32 ok = parse_theme_json(json, &def);
    free(json);

    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI theme loader: parse failed for '%s'", entry->filepath);
        return 0;
    }

    apply_theme_def(&def);

    /* Persist */
    snprintf(s_ActiveThemeId, sizeof(s_ActiveThemeId), "%s", catalog_id);
    snprintf(s_CfgThemeId, sizeof(s_CfgThemeId), "%s", catalog_id);

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme loader: applied '%s' from '%s'",
        catalog_id, entry->filepath);
    return 1;
}

s32 pdguiThemeLoadFromFile(const char *filepath)
{
    if (!filepath || !filepath[0]) return 0;

    u32 fileSize = 0;
    char *data = (char *)fsFileLoad(filepath, &fileSize);
    if (!data) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI theme loader: failed to load '%s'", filepath);
        return 0;
    }

    char *json = (char *)malloc(fileSize + 1);
    memcpy(json, data, fileSize);
    json[fileSize] = '\0';
    free(data);

    struct theme_def def;
    s32 ok = parse_theme_json(json, &def);
    free(json);

    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI theme loader: parse failed for '%s'", filepath);
        return 0;
    }

    apply_theme_def(&def);

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme loader: applied theme from '%s' (%s)",
        filepath, def.name[0] ? def.name : "unnamed");
    return 1;
}

s32 pdguiThemeGetCount(void)
{
    return s_ThemeCount;
}

const char *pdguiThemeGetId(s32 index)
{
    if (index < 0 || index >= s_ThemeCount) return nullptr;
    return s_Themes[index].catalog_id;
}

const char *pdguiThemeGetName(s32 index)
{
    if (index < 0 || index >= s_ThemeCount) return nullptr;
    return s_Themes[index].name;
}

/* S306: the on-disk theme.json path for a given theme. Built-in themes
 * return an empty string (they have no file on disk). */
const char *pdguiThemeGetFilePath(s32 index)
{
    if (index < 0 || index >= s_ThemeCount) return nullptr;
    return s_Themes[index].filepath;
}

const char *pdguiThemeGetActiveId(void)
{
    return s_ActiveThemeId;
}

const char *pdguiThemePaletteIndexToId(s32 index)
{
    if (index < 0 || index > 6) return nullptr;
    return k_BuiltinIds[index];
}

s32 pdguiThemeIdToPaletteIndex(const char *catalog_id)
{
    if (!catalog_id) return -1;
    for (int i = 0; i < 7; i++) {
        if (strcmp(catalog_id, k_BuiltinIds[i]) == 0)
            return i;
    }
    return -1;
}

/* -----------------------------------------------------------------------
 * Per-theme enabled flag (default-enabled policy, 2026-04-13)
 * --------------------------------------------------------------------- */

s32 pdguiThemeIsEnabled(s32 index)
{
    if (index < 0 || index >= s_ThemeCount) return 0;
    /* Built-in themes are always enabled */
    if (s_Themes[index].palette_index >= 0) return 1;
    return s_Themes[index].enabled;
}

void pdguiThemeSetEnabled(s32 index, s32 enabled)
{
    if (index < 0 || index >= s_ThemeCount) return;
    /* Built-in themes cannot be disabled */
    if (s_Themes[index].palette_index >= 0) return;
    s_Themes[index].enabled = enabled ? 1 : 0;
    /* If disabling the currently-active theme, revert to default */
    if (!enabled && strcmp(s_Themes[index].catalog_id, s_ActiveThemeId) == 0) {
        pdguiThemeLoadFromCatalog(THEME_DEFAULT_ID);
    }
    configSave("pd.ini");
}

/* 2026-04-11: register a single mod theme at runtime so the Save-as-Mod
 * path in the theme editor can make the new theme appear immediately in
 * the Load Theme dropdown and the Settings UI Theme selector, without a
 * restart.  Delegates to the same register_mod_theme_dir helper used by
 * the init-time scan_mods_for_themes. */
s32 pdguiThemeRegisterModDir(const char *slug, const char *filepath)
{
    if (!slug || !slug[0]) return 0;

    /* If the caller already knows the exact theme.json path, use it to
     * determine mods_dir; otherwise default to the relative "mods" root
     * so the behaviour matches the init-time scan. */
    char mods_dir[THEME_FILEPATH_LEN] = "mods";
    if (filepath && filepath[0]) {
        /* Extract the parent-of-parent directory from filepath
         * (.../mods/<slug>/theme.json → .../mods). */
        const char *last = strrchr(filepath, '/');
        if (last) {
            size_t n = (size_t)(last - filepath);
            /* Back up once more to strip the slug segment */
            while (n > 0 && filepath[n - 1] == '/') n--;
            const char *prev = filepath + n;
            while (prev > filepath && *(prev - 1) != '/' && *(prev - 1) != '\\') prev--;
            size_t parentLen = (size_t)(prev - filepath);
            if (parentLen > 0 && parentLen < sizeof(mods_dir)) {
                /* parentLen includes trailing slash — drop it */
                if (filepath[parentLen - 1] == '/' || filepath[parentLen - 1] == '\\') parentLen--;
                memcpy(mods_dir, filepath, parentLen);
                mods_dir[parentLen] = '\0';
                if (!mods_dir[0]) {
                    snprintf(mods_dir, sizeof(mods_dir), "mods");
                }
            }
        }
    }

    s32 before = s_ThemeCount;
    register_mod_theme_dir(mods_dir, slug);
    return (s_ThemeCount > before) ? 1 : 0;
}

} /* extern "C" */
