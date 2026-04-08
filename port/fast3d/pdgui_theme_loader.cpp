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
#include <PR/ultratypes.h>

#include "pdgui_theme_loader.h"
#include "pdgui_theme.h"
#include "pdgui_style.h"
#include "pdgui_nineslice.h"
#include "pdgui_effects.h"
#include "pdgui_fontmgr.h"
#include "assetcatalog.h"
#include "config.h"
#include "system.h"
#include "fs.h"

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

    /* Palette: 15 u32 values in 0xRRGGBBAA format */
    u32  palette[15];
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

    /* --- P4 extensions --- */

    /* Nine-slice config */
    s32  has_nineslice;
    char nineslice_tex[THEME_TEX_PATH_LEN];   /* catalog_id of 9-slice texture */
    f32  nineslice_left, nineslice_right;
    f32  nineslice_top, nineslice_bottom;
    f32  nineslice_tex_w, nineslice_tex_h;

    /* Caustic overlay config */
    s32  has_caustic;
    char caustic_tex[THEME_TEX_PATH_LEN];     /* catalog_id of mask texture */
    f32  caustic_speed_x, caustic_speed_y;
    f32  caustic_scale;
    f32  caustic_opacity;
    u32  caustic_tint;
    s32  caustic_additive;

    /* Border effect config */
    s32  has_border_fx;
    s32  border_fx_type;      /* PdguiBorderFx enum value */
    f32  border_fx_intensity;
    f32  border_fx_speed;
    u32  border_fx_color;
    f32  border_fx_width;

    /* Font config */
    s32  has_font;
    char font_path[THEME_TEX_PATH_LEN];       /* relative TTF path */
    char font_catalog_id[THEME_CATALOG_ID_LEN];
    f32  font_size;
    f32  font_glow_radius;
    u32  font_glow_color;
    f32  font_shadow_x, font_shadow_y;
    u32  font_shadow_color;
};

/* =========================================================================
 * Registry: tracks registered themes
 * ========================================================================= */

struct theme_entry {
    char catalog_id[THEME_CATALOG_ID_LEN];
    char name[THEME_NAME_LEN];
    char filepath[THEME_FILEPATH_LEN]; /* empty for built-in */
    s32  palette_index;                /* 0-6 for built-in, -1 for JSON */
};

static struct theme_entry s_Themes[THEME_MAX_REGISTERED];
static s32 s_ThemeCount = 0;
static char s_ActiveThemeId[THEME_CATALOG_ID_LEN] = THEME_DEFAULT_ID;
static char s_CfgThemeId[THEME_CATALOG_ID_LEN] = THEME_DEFAULT_ID;
static s32 s_LoaderInitDone = 0;

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

static const char *k_PaletteFieldNames[15] = {
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
    "unused38"
};

static int palette_field_index(const char *name) {
    for (int i = 0; i < 15; i++) {
        if (strcmp(name, k_PaletteFieldNames[i]) == 0)
            return i;
    }
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
        } else if (strcmp(key, "nineSlice") == 0) {
            /* P4: Parse nineSlice config object */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_nineslice = 1;
            def->nineslice_left = def->nineslice_right = 8.0f;
            def->nineslice_top = def->nineslice_bottom = 8.0f;

            char nkey[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;

                jstr(&tok, nkey, sizeof(nkey));
                tok = jnext(&jp);
                if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(nkey, "texture") == 0 && tok.type == JT_STRING)
                    jstr(&tok, def->nineslice_tex, sizeof(def->nineslice_tex));
                else if (strcmp(nkey, "left") == 0)   def->nineslice_left = jfloat(&tok);
                else if (strcmp(nkey, "right") == 0)  def->nineslice_right = jfloat(&tok);
                else if (strcmp(nkey, "top") == 0)    def->nineslice_top = jfloat(&tok);
                else if (strcmp(nkey, "bottom") == 0) def->nineslice_bottom = jfloat(&tok);
                else if (strcmp(nkey, "texWidth") == 0)  def->nineslice_tex_w = jfloat(&tok);
                else if (strcmp(nkey, "texHeight") == 0) def->nineslice_tex_h = jfloat(&tok);
            }
        } else if (strcmp(key, "caustic") == 0) {
            /* P4: Parse caustic overlay config */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_caustic = 1;
            def->caustic_speed_x = 0.02f;
            def->caustic_speed_y = 0.015f;
            def->caustic_scale = 1.0f;
            def->caustic_opacity = 0.15f;
            def->caustic_tint = 0xffffffffu;

            char ckey[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;

                jstr(&tok, ckey, sizeof(ckey));
                tok = jnext(&jp);
                if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(ckey, "texture") == 0 && tok.type == JT_STRING)
                    jstr(&tok, def->caustic_tex, sizeof(def->caustic_tex));
                else if (strcmp(ckey, "speedX") == 0)   def->caustic_speed_x = jfloat(&tok);
                else if (strcmp(ckey, "speedY") == 0)   def->caustic_speed_y = jfloat(&tok);
                else if (strcmp(ckey, "scale") == 0)    def->caustic_scale = jfloat(&tok);
                else if (strcmp(ckey, "opacity") == 0)  def->caustic_opacity = jfloat(&tok);
                else if (strcmp(ckey, "tint") == 0 && tok.type == JT_STRING)
                    def->caustic_tint = parse_hex_color(tok.start, tok.len);
                else if (strcmp(ckey, "additive") == 0)
                    def->caustic_additive = (tok.type == JT_TRUE) ? 1 : 0;
            }
        } else if (strcmp(key, "borderFx") == 0) {
            /* P4: Parse border effect config */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_border_fx = 1;
            def->border_fx_type = 1;  /* GLOW_PULSE */
            def->border_fx_intensity = 0.5f;
            def->border_fx_speed = 1.0f;
            def->border_fx_color = 0;
            def->border_fx_width = 2.0f;

            char bkey[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;

                jstr(&tok, bkey, sizeof(bkey));
                tok = jnext(&jp);
                if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(bkey, "type") == 0 && tok.type == JT_STRING) {
                    char tstr[32]; jstr(&tok, tstr, sizeof(tstr));
                    if (strcmp(tstr, "none") == 0)       def->border_fx_type = 0;
                    else if (strcmp(tstr, "glow_pulse") == 0)  def->border_fx_type = 1;
                    else if (strcmp(tstr, "grad_sweep") == 0)  def->border_fx_type = 2;
                    else if (strcmp(tstr, "energy") == 0)      def->border_fx_type = 3;
                } else if (strcmp(bkey, "intensity") == 0) def->border_fx_intensity = jfloat(&tok);
                else if (strcmp(bkey, "speed") == 0)     def->border_fx_speed = jfloat(&tok);
                else if (strcmp(bkey, "color") == 0 && tok.type == JT_STRING)
                    def->border_fx_color = parse_hex_color(tok.start, tok.len);
                else if (strcmp(bkey, "width") == 0)     def->border_fx_width = jfloat(&tok);
            }
        } else if (strcmp(key, "font") == 0) {
            /* P4: Parse font config */
            tok = jnext(&jp);
            if (tok.type != JT_LBRACE) { jskip_value(&jp); continue; }

            def->has_font = 1;
            def->font_size = 0.0f;
            def->font_glow_radius = 3.0f;
            def->font_glow_color = 0x0080ff80u;
            def->font_shadow_x = 1.0f;
            def->font_shadow_y = 1.0f;
            def->font_shadow_color = 0x000000a0u;

            char fkey[64];
            while (true) {
                tok = jnext(&jp);
                if (tok.type == JT_RBRACE || tok.type == JT_EOF) break;
                if (tok.type == JT_COMMA) continue;
                if (tok.type != JT_STRING) break;

                jstr(&tok, fkey, sizeof(fkey));
                tok = jnext(&jp);
                if (tok.type != JT_COLON) break;
                tok = jnext(&jp);

                if (strcmp(fkey, "path") == 0 && tok.type == JT_STRING)
                    jstr(&tok, def->font_path, sizeof(def->font_path));
                else if (strcmp(fkey, "catalogId") == 0 && tok.type == JT_STRING)
                    jstr(&tok, def->font_catalog_id, sizeof(def->font_catalog_id));
                else if (strcmp(fkey, "size") == 0)          def->font_size = jfloat(&tok);
                else if (strcmp(fkey, "glowRadius") == 0)    def->font_glow_radius = jfloat(&tok);
                else if (strcmp(fkey, "glowColor") == 0 && tok.type == JT_STRING)
                    def->font_glow_color = parse_hex_color(tok.start, tok.len);
                else if (strcmp(fkey, "shadowX") == 0)       def->font_shadow_x = jfloat(&tok);
                else if (strcmp(fkey, "shadowY") == 0)       def->font_shadow_y = jfloat(&tok);
                else if (strcmp(fkey, "shadowColor") == 0 && tok.type == JT_STRING)
                    def->font_shadow_color = parse_hex_color(tok.start, tok.len);
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

static void apply_theme_def(const struct theme_def *def)
{
    /* Apply palette if present */
    if (def->has_palette) {
        pdguiSetPaletteCustom(def->palette);
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

    /* --- P4 extensions --- */

    /* Apply nine-slice config */
    if (def->has_nineslice && def->nineslice_tex[0]) {
        NineSliceInsets insets;
        insets.left   = def->nineslice_left;
        insets.right  = def->nineslice_right;
        insets.top    = def->nineslice_top;
        insets.bottom = def->nineslice_bottom;

        f32 tw = def->nineslice_tex_w > 0.0f ? def->nineslice_tex_w : 64.0f;
        f32 th = def->nineslice_tex_h > 0.0f ? def->nineslice_tex_h : 64.0f;

        pdguiNineSliceRegister(def->nineslice_tex, &insets, tw, th);

        sysLogPrintf(LOG_NOTE,
            "PDGUI theme loader: 9-slice registered '%s' insets=%.0f,%.0f,%.0f,%.0f",
            def->nineslice_tex, insets.left, insets.right, insets.top, insets.bottom);
    }

    /* Apply caustic overlay config */
    if (def->has_caustic) {
        PdguiCausticConfig cc = pdguiCausticDefaultConfig();
        cc.scroll_speed_x = def->caustic_speed_x;
        cc.scroll_speed_y = def->caustic_speed_y;
        cc.scale          = def->caustic_scale;
        cc.opacity        = def->caustic_opacity;
        cc.tint_color     = def->caustic_tint;
        cc.blend_additive = def->caustic_additive;
        pdguiEffectsSetCausticConfig(&cc);

        if (def->caustic_tex[0]) {
            pdguiEffectsSetCausticTexture(def->caustic_tex);
        }

        sysLogPrintf(LOG_NOTE,
            "PDGUI theme loader: caustic configured (speed=%.3f,%.3f opacity=%.2f)",
            cc.scroll_speed_x, cc.scroll_speed_y, cc.opacity);
    }

    /* Apply border effect config */
    if (def->has_border_fx) {
        PdguiBorderFxConfig bc = pdguiBorderFxDefaultConfig();
        bc.type      = (PdguiBorderFx)def->border_fx_type;
        bc.intensity = def->border_fx_intensity;
        bc.speed     = def->border_fx_speed;
        bc.color     = def->border_fx_color;
        bc.width     = def->border_fx_width;
        pdguiEffectsSetBorderFxConfig(&bc);

        sysLogPrintf(LOG_NOTE,
            "PDGUI theme loader: border fx type=%d intensity=%.2f speed=%.2f",
            bc.type, bc.intensity, bc.speed);
    }

    /* Apply font config */
    if (def->has_font && def->font_path[0]) {
        PdguiFontConfig fc = pdguiFontConfigDefault();
        fc.size_pt         = def->font_size;
        fc.glow_radius     = def->font_glow_radius;
        fc.glow_color      = def->font_glow_color;
        fc.shadow_offset_x = def->font_shadow_x;
        fc.shadow_offset_y = def->font_shadow_y;
        fc.shadow_color    = def->font_shadow_color;

        const char *fontId = def->font_catalog_id[0]
                           ? def->font_catalog_id
                           : "mod:theme_font";
        pdguiFontMgrLoadFont(fontId, def->font_path, &fc);

        sysLogPrintf(LOG_NOTE,
            "PDGUI theme loader: font '%s' from '%s' (%.0fpt glow=%.1f)",
            fontId, def->font_path, fc.size_pt, fc.glow_radius);
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
    return e;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

extern "C" {

void pdguiThemeLoaderInit(void)
{
    if (s_LoaderInitDone) return;
    s_LoaderInitDone = 1;

    /* Register pd.ini config var for active theme */
    configRegisterString(THEME_CFG_KEY, s_CfgThemeId, sizeof(s_CfgThemeId));

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

    /* Scan for theme.json files in mods/ directories */
    /* (Theme JSON files from mods are discovered by the mod manager;
     *  here we just handle the built-in set. Mod themes register via
     *  pdguiThemeLoadFromFile() called from mod loading.) */

    sysLogPrintf(LOG_NOTE,
        "PDGUI theme loader: init — %d built-in themes registered, active='%s'",
        s_ThemeCount, s_ActiveThemeId);

    /* Apply the configured theme */
    s32 palIdx = pdguiThemeIdToPaletteIndex(s_ActiveThemeId);
    if (palIdx >= 0) {
        pdguiThemeSetPalette(palIdx);
    }
}

void pdguiThemeLoaderShutdown(void)
{
    s_ThemeCount = 0;
    s_LoaderInitDone = 0;
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

    /* Load the JSON file */
    u32 fileSize = 0;
    char *data = (char *)fsFileLoad(entry->filepath, &fileSize);
    if (!data) {
        sysLogPrintf(LOG_WARNING,
            "PDGUI theme loader: failed to load '%s'", entry->filepath);
        return 0;
    }

    /* Null-terminate */
    char *json = (char *)malloc(fileSize + 1);
    memcpy(json, data, fileSize);
    json[fileSize] = '\0';
    free(data);

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

} /* extern "C" */
