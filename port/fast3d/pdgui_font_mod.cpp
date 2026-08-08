/**
 * pdgui_font_mod.cpp -- font mod discovery + config-backed active font.
 *
 * See pdgui_font_mod.h. Auto-discovered by CMakeLists.txt GLOB.
 *
 * IMPORTANT: Do NOT include types.h — it #defines bool as s32, breaking C++.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "pdgui_font_mod.h"
#include "config.h"
#include "system.h"
#include "fs.h"
#include "modmgr.h"   /* B-238 follow-up: MODMGR_RESERVED_NAMES_LIST */
#include "assetcatalog.h"
#include "assetprovider.h"

#define FONTMOD_MAX             32
#define FONTMOD_NAME_LEN        96
#define FONTMOD_ID_LEN          96
#define FONTMOD_PATH_LEN        512
#define FONTMOD_CFG_KEY         "Video.FontId"

struct fontmod_entry {
    char id[FONTMOD_ID_LEN];       /* catalog ID, e.g. "user.MyFont.font" */
    char name[FONTMOD_NAME_LEN];   /* display name */
    char path[FONTMOD_PATH_LEN];   /* absolute path to the .ttf/.otf */
};

static struct fontmod_entry s_Fonts[FONTMOD_MAX];
static s32  s_Count    = 0;
static char s_CfgId[FONTMOD_ID_LEN] = "";  /* "" = built-in Handel Gothic */
static bool s_CfgRegistered         = false;

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static bool has_ext(const char *name, const char *ext)
{
    if (!name || !ext) return false;
    size_t nl = strlen(name), el = strlen(ext);
    if (nl < el) return false;
    const char *tail = name + (nl - el);
    for (size_t i = 0; i < el; i++) {
        char a = tail[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
    }
    return true;
}

static bool is_font_file(const char *name)
{
    return has_ext(name, ".ttf") || has_ext(name, ".otf");
}

static void slugify(const char *src, char *dst, size_t dstmax)
{
    if (!src || !dst || dstmax == 0) { if (dst && dstmax) dst[0] = '\0'; return; }
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dstmax - 1; i++) {
        char c = src[i];
        if (c == ' ') c = '-';
        else if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) continue;
        dst[j++] = c;
    }
    dst[j] = '\0';
}

/* -----------------------------------------------------------------------
 * Directory scan
 * --------------------------------------------------------------------- */

/** Look inside `dir` for the first .ttf/.otf file.  Returns 1 and writes
 *  the filename (basename only) into out_name on success. */
static int find_first_font_in_dir(const char *dir, char *out_name, size_t out_namelen)
{
    if (!dir || !out_name || out_namelen == 0) return 0;
    out_name[0] = '\0';
    DIR *d = opendir(dir);
    if (!d) return 0;

    int found = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        const char *n = ent->d_name;
        if (!n || n[0] == '.') continue;
        if (!is_font_file(n)) continue;
        snprintf(out_name, out_namelen, "%s", n);
        found = 1;
        break;
    }
    closedir(d);
    return found;
}

static const char *read_json_string_field(const char *json, const char *key,
                                          char *out, size_t outlen)
{
    if (!json || !key || !out || outlen == 0) return nullptr;
    out[0] = '\0';
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return nullptr;
    p = strchr(p + strlen(pat), ':');
    if (!p) return nullptr;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return nullptr;
    p++;
    size_t j = 0;
    while (*p && *p != '"' && j < outlen - 1) {
        if (*p == '\\' && p[1]) { out[j++] = p[1]; p += 2; continue; }
        out[j++] = *p++;
    }
    out[j] = '\0';
    return out;
}

static void register_font_entry(const char *slug, const char *abspath,
                                const char *display_name)
{
    if (s_Count >= FONTMOD_MAX) return;

    struct fontmod_entry *e = &s_Fonts[s_Count];

    /* ID — "user.<slug>.font" so it's unambiguous vs themes/chromes. */
    char slugBuf[FONTMOD_ID_LEN];
    slugify(slug, slugBuf, sizeof(slugBuf));
    if (!slugBuf[0]) snprintf(slugBuf, sizeof(slugBuf), "font%d", (int)s_Count);
    snprintf(e->id, sizeof(e->id), "user.%s.font", slugBuf);

    snprintf(e->name, sizeof(e->name), "%s",
             (display_name && display_name[0]) ? display_name : slug);

    snprintf(e->path, sizeof(e->path), "%s", abspath);

    s_Count++;
}

/** Consider a single candidate slug directory (mods/Fonts/<slug>/ or
 *  mods/<slug>/).  Registers the font if a .ttf/.otf is present. */
static void scan_candidate_dir(const char *absdir, const char *slug)
{
    if (!absdir || !slug) return;

    char filename[FONTMOD_NAME_LEN];
    if (!find_first_font_in_dir(absdir, filename, sizeof(filename))) {
        return;
    }

    char abspath[FONTMOD_PATH_LEN];
    snprintf(abspath, sizeof(abspath), "%s/%s", absdir, filename);

    /* Try font.json for a nicer display name; fall back to the slug. */
    char display[FONTMOD_NAME_LEN];
    display[0] = '\0';

    char manifest[FONTMOD_PATH_LEN];
    snprintf(manifest, sizeof(manifest), "%s/font.json", absdir);
    FILE *mf = fopen(manifest, "rb");
    if (mf) {
        fseek(mf, 0, SEEK_END);
        long sz = ftell(mf);
        fseek(mf, 0, SEEK_SET);
        if (sz > 0 && sz < 64 * 1024) {
            char *buf = (char *)malloc((size_t)sz + 1);
            if (buf) {
                size_t got = fread(buf, 1, (size_t)sz, mf);
                buf[got] = '\0';
                read_json_string_field(buf, "name", display, sizeof(display));
                free(buf);
            }
        }
        fclose(mf);
    }

    register_font_entry(slug, abspath, display);
}

/** Scan a root directory (e.g. "<base>/mods").  Recognises two layouts:
 *    mods/Fonts/<slug>/ - category folder; look for TTF/OTF inside
 *    mods/<slug>/       - top-level slug with no manifest
 */
static void scan_mods_root(const char *root)
{
    if (!root || !root[0]) return;
    DIR *d = opendir(root);
    if (!d) return;

    /* B-238 follow-up trust-gate alignment: skip reserved trust-gate folders
     * (shared/, inbox/, untrusted/) and any .legacy_backup residue from the
     * M-4.1 migration. These are root-level checks; the Fonts/ category
     * recursion below is depth-1 only and does not re-apply this filter. */
    static const char *const k_reserved[MODMGR_RESERVED_NAMES_COUNT] = MODMGR_RESERVED_NAMES_LIST;
    static const char k_legacy_suffix[] = ".legacy_backup";
    const size_t k_legacy_suffix_len = sizeof(k_legacy_suffix) - 1;

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr) {
        const char *n = ent->d_name;
        if (!n || n[0] == '.') continue;
        if (!strcmp(n, ".") || !strcmp(n, "..")) continue;

        bool reserved_hit = false;
        for (s32 ri = 0; ri < MODMGR_RESERVED_NAMES_COUNT; ri++) {
            if (strcmp(n, k_reserved[ri]) == 0) { reserved_hit = true; break; }
        }
        if (reserved_hit) continue;
        size_t nlen = strlen(n);
        if (nlen > k_legacy_suffix_len &&
            strcmp(n + nlen - k_legacy_suffix_len, k_legacy_suffix) == 0) {
            continue;
        }

        char subdir[FONTMOD_PATH_LEN];
        snprintf(subdir, sizeof(subdir), "%s/%s", root, n);
        struct stat st;
        if (stat(subdir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        /* Category folder: mods/Fonts/<slug>/ */
        if (!strcasecmp(n, "fonts") || !strcasecmp(n, "Font")) {
            DIR *d2 = opendir(subdir);
            if (!d2) continue;
            struct dirent *ent2;
            while ((ent2 = readdir(d2)) != nullptr) {
                const char *n2 = ent2->d_name;
                if (!n2 || n2[0] == '.') continue;
                char subsub[FONTMOD_PATH_LEN];
                snprintf(subsub, sizeof(subsub), "%s/%s", subdir, n2);
                struct stat st2;
                if (stat(subsub, &st2) != 0 || !S_ISDIR(st2.st_mode)) continue;
                scan_candidate_dir(subsub, n2);
                if (s_Count >= FONTMOD_MAX) break;
            }
            closedir(d2);
        } else {
            /* Top-level slug — register if it contains a font file. */
            scan_candidate_dir(subdir, n);
        }

        if (s_Count >= FONTMOD_MAX) break;
    }
    closedir(d);
}

static void rescan(void)
{
    s_Count = 0;

    /* Match modmgr search order for mods/ roots. */
    char cands[4][FONTMOD_PATH_LEN];
    fsFullPath("$E/../mods", cands[0], sizeof(cands[0]));
    snprintf(cands[1], sizeof(cands[1]), "%s", "mods");
    fsFullPath("$E/mods", cands[2], sizeof(cands[2]));
    fsFullPath("mods",    cands[3], sizeof(cands[3]));

    for (int i = 0; i < 4; i++) {
        if (!cands[i][0]) continue;
        /* De-dupe resolved paths. */
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (cands[j][0] && strcmp(cands[i], cands[j]) == 0) { dup = true; break; }
        }
        if (dup) continue;
        scan_mods_root(cands[i]);
        if (s_Count >= FONTMOD_MAX) break;
    }

    sysLogPrintf(LOG_NOTE, "PDGUI font mod: scan complete — %d fonts registered", s_Count);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

extern "C" {

void pdguiFontModInit(void)
{
    if (!s_CfgRegistered) {
        configRegisterString(FONTMOD_CFG_KEY, s_CfgId, sizeof(s_CfgId));
        s_CfgRegistered = true;
    }
    rescan();
}

void pdguiFontModRescan(void)
{
    rescan();
}

s32 pdguiFontModGetCount(void)
{
    return s_Count;
}

const char *pdguiFontModGetId(s32 index)
{
    if (index < 0 || index >= s_Count) return "";
    return s_Fonts[index].id;
}

const char *pdguiFontModGetName(s32 index)
{
    if (index < 0 || index >= s_Count) return "";
    return s_Fonts[index].name;
}

const char *pdguiFontModGetPath(s32 index)
{
    if (index < 0 || index >= s_Count) return "";
    return s_Fonts[index].path;
}

const char *pdguiFontModGetActiveId(void)
{
    return s_CfgId;
}

void pdguiFontModSetActiveId(const char *catalog_id)
{
    if (!catalog_id) { s_CfgId[0] = '\0'; return; }
    snprintf(s_CfgId, sizeof(s_CfgId), "%s", catalog_id);
}

const char *pdguiFontModGetActivePath(void)
{
    if (!s_CfgId[0]) return nullptr;
    for (s32 i = 0; i < s_Count; i++) {
        if (strcmp(s_Fonts[i].id, s_CfgId) == 0) {
            return s_Fonts[i].path;
        }
    }

    const asset_entry_t *entry = assetCatalogGetMutable(s_CfgId);
    if (entry && entry->type == ASSET_FONT && entry->enabled) {
        /* Catalog font_file is the public face member, including archive
         * chains such as theme.pdtheme::font.pdfont::font.ttf. The primary
         * provider path names the .pdfont container and is not itself a
         * loadable vector face. */
        const char *path = entry->ext.font.font_file;
        if (path && (has_ext(path, ".ttf") || has_ext(path, ".otf"))) {
            return path;
        }
    }
    return nullptr;
}

s32 pdguiFontModValidateCatalogId(const char *catalog_id,
                                  char *error, size_t error_cap)
{
    if (error && error_cap) error[0] = '\0';
    if (!catalog_id || !catalog_id[0]) return 1;
    const asset_entry_t *entry = assetCatalogGetMutable(catalog_id);
    if (!entry || entry->type != ASSET_FONT || !entry->enabled) {
        if (error && error_cap) snprintf(error, error_cap,
            "font source is missing, disabled, or wrong-type");
        return 0;
    }
    const char *path = entry->ext.font.font_file;
    if (!path || (!has_ext(path, ".ttf") && !has_ext(path, ".otf"))) {
        if (error && error_cap) snprintf(error, error_cap,
            "theme fonts require a public TTF or OTF source");
        return 0;
    }
    u32 size = 0;
    void *bytes = fsFileLoad(path, &size);
    if (!bytes || size < 12) {
        if (bytes) free(bytes);
        if (error && error_cap) snprintf(error, error_cap,
            "theme font public source is unreadable");
        return 0;
    }
    const u8 *sig = (const u8 *)bytes;
    const bool sfnt = (sig[0] == 0x00 && sig[1] == 0x01 &&
                       sig[2] == 0x00 && sig[3] == 0x00) ||
                      !memcmp(sig, "OTTO", 4) ||
                      !memcmp(sig, "true", 4) ||
                      !memcmp(sig, "typ1", 4) ||
                      !memcmp(sig, "ttcf", 4);
    free(bytes);
    if (!sfnt) {
        if (error && error_cap) snprintf(error, error_cap,
            "theme font public source is not a valid SFNT face");
        return 0;
    }
    return 1;
}

} /* extern "C" */
