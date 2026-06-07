/**
 * langmanifest.c -- Phase 3: Language bank manifest system
 *
 * Tracks which LANGBANK_* entries are currently loaded and provides an API
 * for screens and stages to declare lang bank dependencies via catalog IDs.
 *
 * The base runtime path now loads the common banks (GUN, MPMENU, OPTIONS,
 * etc.) through .pdlang FileProvider source when present. Screens with
 * additional deps call langManifestEnsureId("base:lang_title") to load and
 * track the bank on demand.
 *
 * Language-change reload:
 *   langManifestReload() iterates g_LangManifest.bank_ids[] and calls
 *   langLoad() for each tracked bank.  Used by langSetEuropean() /
 *   langSetJpnEnabled() in lang.c so that manifest-declared banks are
 *   reloaded when the player switches language.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "system.h"
#include "types.h"
#include "langmanifest.h"
#include "assetcatalog.h"
#include "assetprovider.h"
#include "fs.h"
#include "game/lang.h"

/* =========================================================================
 * Module state
 * ========================================================================= */

lang_manifest_t g_LangManifest;

extern uintptr_t *g_LangBanks[LANG_MANIFEST_MAX_BANKS];

static void *s_ModLangBuffers[LANG_MANIFEST_MAX_BANKS];
static char s_ModLangCatalogIds[LANG_MANIFEST_MAX_BANKS][CATALOG_ID_LEN];

/* =========================================================================
 * Editable JSON source loader
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

static void jsonAppendUtf8(char *dst, size_t *w, u32 codepoint)
{
    if (codepoint == 0) {
        dst[(*w)++] = '?';
    } else if (codepoint <= 0xff) {
        dst[(*w)++] = (char)codepoint;
    } else if (codepoint <= 0x7ff) {
        dst[(*w)++] = (char)(0xc0 | ((codepoint >> 6) & 0x1f));
        dst[(*w)++] = (char)(0x80 | (codepoint & 0x3f));
    } else {
        dst[(*w)++] = (char)(0xe0 | ((codepoint >> 12) & 0x0f));
        dst[(*w)++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        dst[(*w)++] = (char)(0x80 | (codepoint & 0x3f));
    }
}

static char *jsonDecodeString(const char *value, size_t *out_len)
{
    const char *end;
    const char *p;
    char *dst;
    size_t cap;
    size_t w = 0;

    if (!value || *value != '"') {
        return NULL;
    }

    end = jsonStringEnd(value);
    if (!end) {
        return NULL;
    }

    p = value + 1;
    cap = (size_t)(end - p) + 1;
    dst = (char *)malloc(cap);
    if (!dst) {
        return NULL;
    }

    while (p < end - 1) {
        if (*p == '\\' && p + 1 < end - 1) {
            char esc = p[1];
            if (esc == 'n') {
                dst[w++] = '\n';
                p += 2;
                continue;
            }
            if (esc == 't') {
                dst[w++] = '\t';
                p += 2;
                continue;
            }
            if (esc == 'r') {
                dst[w++] = '\r';
                p += 2;
                continue;
            }
            if (esc == 'b') {
                dst[w++] = '\b';
                p += 2;
                continue;
            }
            if (esc == 'f') {
                dst[w++] = '\f';
                p += 2;
                continue;
            }
            if (esc == '"' || esc == '\\' || esc == '/') {
                dst[w++] = esc;
                p += 2;
                continue;
            }
            if (esc == 'u' && p + 5 < end - 1) {
                s32 h0 = jsonHexNibble(p[2]);
                s32 h1 = jsonHexNibble(p[3]);
                s32 h2 = jsonHexNibble(p[4]);
                s32 h3 = jsonHexNibble(p[5]);
                if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                    u32 cp = (u32)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                    jsonAppendUtf8(dst, &w, cp);
                    p += 6;
                    continue;
                }
            }
        }

        dst[w++] = *p++;
    }

    dst[w] = '\0';
    if (out_len) {
        *out_len = w;
    }
    return dst;
}

static const char *langEntryFilePath(const asset_entry_t *entry)
{
    if (!entry) {
        return NULL;
    }

    if (entry->source.primary.provider == fileProvider()) {
        const char *path = fileProviderPath(entry->source.primary);
        if (path && path[0]) {
            return path;
        }
    }

    if (entry->ext.lang.strings_file[0]) {
        return entry->ext.lang.strings_file;
    }

    return NULL;
}

static s32 langManifestLoadExternalJson(const asset_entry_t *entry)
{
    enum { LANG_JSON_MAX_STRINGS = 512 };
    const char *path;
    s32 bank;
    u32 raw_size = 0;
    char *raw;
    const char *root_end;
    const char *strings_value;
    const char *strings_end;
    const char *cursor;
    char *texts[LANG_JSON_MAX_STRINGS];
    size_t lens[LANG_JSON_MAX_STRINGS];
    size_t table_bytes = sizeof(uintptr_t) * LANG_JSON_MAX_STRINGS;
    size_t total_bytes = table_bytes;
    uintptr_t *bank_data;
    char *out;

    if (!entry || entry->type != ASSET_LANG) {
        return 0;
    }

    bank = entry->ext.lang.bank_id;
    if (bank <= 0 || bank >= LANG_MANIFEST_MAX_BANKS) {
        return 0;
    }

    path = langEntryFilePath(entry);
    if (!path || !path[0]) {
        return 0;
    }

    memset(texts, 0, sizeof(texts));
    memset(lens, 0, sizeof(lens));

    raw = (char *)fsFileLoad(path, &raw_size);
    if (!raw || raw_size == 0) {
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: failed to load JSON '%s' for '%s'",
                     path, entry->id);
        if (raw) {
            free(raw);
        }
        return 0;
    }
    raw[raw_size] = '\0';

    cursor = raw;
    if ((u8)cursor[0] == 0xef && (u8)cursor[1] == 0xbb && (u8)cursor[2] == 0xbf) {
        cursor += 3;
    }

    cursor = jsonSkipWs(cursor);
    root_end = jsonFindMatching(cursor);
    if (!root_end) {
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: malformed JSON bank '%s' (%s)",
                     entry->id, path);
        free(raw);
        return 0;
    }

    strings_value = jsonFindObjectKey(cursor, root_end, "strings");
    if (!strings_value || *strings_value != '[') {
        sysLogPrintf(LOG_WARNING,
                     "LANG-MANIFEST: JSON bank '%s' missing strings array (%s)",
                     entry->id, path);
        free(raw);
        return 0;
    }

    strings_end = jsonFindMatching(strings_value);
    if (!strings_end) {
        sysLogPrintf(LOG_WARNING,
                     "LANG-MANIFEST: malformed strings array in '%s' (%s)",
                     entry->id, path);
        free(raw);
        return 0;
    }

    cursor = strings_value + 1;
    while (cursor < strings_end) {
        const char *obj;
        const char *obj_end;
        const char *index_value;
        const char *text_value;
        char *endptr = NULL;
        long parsed_index;
        s32 index;

        cursor = jsonSkipWs(cursor);
        if (!cursor || cursor >= strings_end || *cursor == ']') {
            break;
        }
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor != '{') {
            cursor++;
            continue;
        }

        obj = cursor;
        obj_end = jsonFindMatching(obj);
        if (!obj_end || obj_end > strings_end) {
            break;
        }

        index_value = jsonFindObjectKey(obj, obj_end, "index");
        text_value = jsonFindObjectKey(obj, obj_end, "text");
        if (!index_value || !text_value || *text_value != '"') {
            cursor = obj_end;
            continue;
        }

        parsed_index = strtol(index_value, &endptr, 0);
        if (endptr == index_value || parsed_index < 0 ||
                parsed_index >= LANG_JSON_MAX_STRINGS) {
            cursor = obj_end;
            continue;
        }
        index = (s32)parsed_index;

        if (texts[index]) {
            free(texts[index]);
            texts[index] = NULL;
            lens[index] = 0;
        }

        texts[index] = jsonDecodeString(text_value, &lens[index]);
        if (texts[index]) {
            total_bytes += lens[index] + 1;
        }

        cursor = obj_end;
    }

    bank_data = (uintptr_t *)calloc(1, total_bytes);
    if (!bank_data) {
        sysLogPrintf(LOG_ERROR, "LANG-MANIFEST: OOM building JSON bank '%s' (%zu bytes)",
                     entry->id, total_bytes);
        for (s32 i = 0; i < LANG_JSON_MAX_STRINGS; i++) {
            if (texts[i]) {
                free(texts[i]);
            }
        }
        free(raw);
        return 0;
    }

    out = (char *)bank_data + table_bytes;
    for (s32 i = 0; i < LANG_JSON_MAX_STRINGS; i++) {
        if (!texts[i]) {
            continue;
        }
        bank_data[i] = (uintptr_t)(out - (char *)bank_data);
        memcpy(out, texts[i], lens[i] + 1);
        out += lens[i] + 1;
        free(texts[i]);
    }

    if (s_ModLangBuffers[bank]) {
        free(s_ModLangBuffers[bank]);
        s_ModLangBuffers[bank] = NULL;
    }

    g_LangBanks[bank] = bank_data;
    s_ModLangBuffers[bank] = bank_data;
    strncpy(s_ModLangCatalogIds[bank], entry->id, CATALOG_ID_LEN - 1);
    s_ModLangCatalogIds[bank][CATALOG_ID_LEN - 1] = '\0';

    free(raw);

    sysLogPrintf(LOG_NOTE, "LANG-MANIFEST: loaded JSON bank %d (%s) from %s",
                 bank, entry->id, path);
    return 1;
}

static const asset_entry_t *langManifestFindBestEntryForBank(s32 bank)
{
    const asset_entry_t *best = NULL;
    s32 best_score = -1;
    s32 count;

    if (bank <= 0 || bank >= LANG_MANIFEST_MAX_BANKS) {
        return NULL;
    }

    count = assetCatalogGetCount();

    for (s32 i = 0; i < count; i++) {
        const asset_entry_t *entry = assetCatalogGetByIndex(i);
        const char *path;
        s32 score;

        if (!entry || !entry->occupied || entry->type != ASSET_LANG) {
            continue;
        }
        if (!entry->enabled || entry->ext.lang.bank_id != bank) {
            continue;
        }

        path = langEntryFilePath(entry);
        if (!path || !path[0]) {
            continue;
        }

        score = entry->bundled ? 1 : 2;
        if (score >= best_score) {
            best = entry;
            best_score = score;
        }
    }

    return best;
}

s32 langManifestLoadBankFromCatalog(s32 bank)
{
    const asset_entry_t *entry = langManifestFindBestEntryForBank(bank);

    if (!entry) {
        sysLogPrintf(LOG_WARNING,
                     "LANG-MANIFEST: no enabled FileProvider .pdlang source for bank %d",
                     bank);
        return 0;
    }

    return langManifestLoadExternalJson(entry);
}

/* =========================================================================
 * API
 * ========================================================================= */

void langManifestReset(void)
{
    s32 i;
    for (i = 0; i < LANG_MANIFEST_MAX_BANKS; i++) {
        if (s_ModLangBuffers[i]) {
            free(s_ModLangBuffers[i]);
            s_ModLangBuffers[i] = NULL;
        }
        s_ModLangCatalogIds[i][0] = '\0';
        g_LangManifest.bank_ids[i] = -1;
    }
    g_LangManifest.count = 0;
}

void langManifestRecordBank(s32 bank)
{
    s32 i;

    if (bank <= 0 || bank >= LANG_MANIFEST_MAX_BANKS) {
        return;
    }

    /* Dedup: already tracked? */
    for (i = 0; i < g_LangManifest.count; i++) {
        if (g_LangManifest.bank_ids[i] == bank) {
            return;
        }
    }

    if (g_LangManifest.count < LANG_MANIFEST_MAX_BANKS) {
        g_LangManifest.bank_ids[g_LangManifest.count] = bank;
        g_LangManifest.count++;
    }
}

s32 langManifestEnsureId(const char *lang_id)
{
    const asset_entry_t *entry;
    s32 bank;
    const char *path;

    if (!lang_id || !lang_id[0]) {
        return 0;
    }

    entry = assetCatalogResolve(lang_id);
    if (!entry || entry->type != ASSET_LANG) {
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: unknown lang id '%s'", lang_id);
        return 0;
    }

    bank = entry->ext.lang.bank_id;
    if (bank <= 0 || bank >= LANG_MANIFEST_MAX_BANKS) {
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: invalid bank_id %d for '%s'", bank, lang_id);
        return 0;
    }

    path = langEntryFilePath(entry);
    if (path && path[0]) {
        if (!langIsBankLoaded(bank)
                || s_ModLangBuffers[bank] == NULL
                || strcmp(s_ModLangCatalogIds[bank], entry->id) != 0) {
            if (!langManifestLoadExternalJson(entry)) {
                return 0;
            }
        }
    } else if (!langIsBankLoaded(bank)) {
        if (!langManifestLoadBankFromCatalog(bank)) {
            sysLogPrintf(LOG_WARNING,
                         "LANG-MANIFEST: failed to load catalog bank %d (%s)",
                         bank, lang_id);
            return 0;
        }
        sysLogPrintf(LOG_NOTE, "LANG-MANIFEST: loaded catalog bank %d (%s)",
                     bank, lang_id);
    }

    langManifestRecordBank(bank);
    return 1;
}

void langManifestReload(void)
{
    s32 i;
    s32 bank;

    for (i = 0; i < g_LangManifest.count; i++) {
        bank = g_LangManifest.bank_ids[i];
        if (bank > 0 && bank < LANG_MANIFEST_MAX_BANKS) {
            const asset_entry_t *entry = NULL;
            if (s_ModLangCatalogIds[bank][0]) {
                entry = assetCatalogResolve(s_ModLangCatalogIds[bank]);
            }
            if (entry && entry->type == ASSET_LANG && langEntryFilePath(entry)) {
                langManifestLoadExternalJson(entry);
            } else {
                if (s_ModLangBuffers[bank]) {
                    free(s_ModLangBuffers[bank]);
                    s_ModLangBuffers[bank] = NULL;
                    s_ModLangCatalogIds[bank][0] = '\0';
                    g_LangBanks[bank] = NULL;
                }
                if (!langManifestLoadBankFromCatalog(bank)) {
                    sysLogPrintf(LOG_WARNING,
                                 "LANG-MANIFEST: reload failed for catalog bank %d",
                                 bank);
                }
            }
        }
    }
}

s32 langManifestGetCount(void)
{
    return g_LangManifest.count;
}
