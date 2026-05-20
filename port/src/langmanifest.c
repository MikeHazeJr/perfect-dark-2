/**
 * langmanifest.c -- Phase 3: Language bank manifest system
 *
 * Tracks which LANGBANK_* entries are currently loaded and provides an API
 * for screens and stages to declare lang bank dependencies via catalog IDs.
 *
 * The existing langReset() path is unchanged: it still loads the common banks
 * (GUN, MPMENU, OPTIONS, etc.) and calls langManifestRecordBank() for each.
 * Screens with additional deps call langManifestEnsureId("base:lang_title")
 * to load and track the bank on demand.
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
 * UTF-8 TSV loader
 * ========================================================================= */

static char *trimLine(char *s)
{
    char *end;

    while (*s && isspace((u8)*s)) {
        s++;
    }

    end = s + strlen(s);
    while (end > s && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t')) {
        *--end = '\0';
    }

    return s;
}

static s32 parseStringIndex(const char *text, s32 *out)
{
    char *end = NULL;
    long n;

    if (!text || !text[0] || !out) {
        return 0;
    }

    if (strcmp(text, "index") == 0 || strcmp(text, "id") == 0 ||
            strcmp(text, "textid") == 0) {
        return 0;
    }

    n = strtol(text, &end, 0);
    if (end == text || n < 0) {
        return 0;
    }

    if (n > 0x1ff) {
        n &= 0x1ff;
    }
    if (n > 0x1ff) {
        return 0;
    }

    *out = (s32)n;
    return 1;
}

static char *copyTsvText(const char *src, size_t *out_len)
{
    size_t len;
    char *dst;
    size_t r = 0;
    size_t w = 0;

    if (!src) {
        return NULL;
    }

    len = strlen(src);
    dst = (char *)malloc(len + 1);
    if (!dst) {
        return NULL;
    }

    while (r < len) {
        if (src[r] == '\\' && r + 1 < len) {
            char esc = src[r + 1];
            if (esc == 'n') {
                dst[w++] = '\n';
                r += 2;
                continue;
            }
            if (esc == 't') {
                dst[w++] = '\t';
                r += 2;
                continue;
            }
            if (esc == '\\') {
                dst[w++] = '\\';
                r += 2;
                continue;
            }
        }
        dst[w++] = src[r++];
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

static s32 langManifestLoadExternalTsv(const asset_entry_t *entry)
{
    enum { LANG_TSV_MAX_STRINGS = 512 };
    const char *path;
    s32 bank;
    u32 raw_size = 0;
    char *raw;
    char *line;
    char *cursor;
    char *texts[LANG_TSV_MAX_STRINGS];
    size_t lens[LANG_TSV_MAX_STRINGS];
    size_t table_bytes = sizeof(uintptr_t) * LANG_TSV_MAX_STRINGS;
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
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: failed to load TSV '%s' for '%s'",
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

    while ((line = cursor) != NULL && *line) {
        char *tab;
        char *next = strpbrk(line, "\r\n");
        s32 index;
        char *key;
        char *value;

        if (next) {
            char newline = *next;
            *next++ = '\0';
            if (newline == '\r' && *next == '\n') {
                next++;
            }
            cursor = next;
        } else {
            cursor = line + strlen(line);
        }

        line = trimLine(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') {
            continue;
        }

        tab = strchr(line, '\t');
        if (!tab) {
            continue;
        }

        *tab++ = '\0';
        key = trimLine(line);
        value = tab;

        if (!parseStringIndex(key, &index)) {
            continue;
        }

        if (texts[index]) {
            free(texts[index]);
            texts[index] = NULL;
            lens[index] = 0;
        }

        texts[index] = copyTsvText(value, &lens[index]);
        if (texts[index]) {
            total_bytes += lens[index] + 1;
        }
    }

    bank_data = (uintptr_t *)calloc(1, total_bytes);
    if (!bank_data) {
        sysLogPrintf(LOG_ERROR, "LANG-MANIFEST: OOM building TSV bank '%s' (%zu bytes)",
                     entry->id, total_bytes);
        for (s32 i = 0; i < LANG_TSV_MAX_STRINGS; i++) {
            if (texts[i]) {
                free(texts[i]);
            }
        }
        free(raw);
        return 0;
    }

    out = (char *)bank_data + table_bytes;
    for (s32 i = 0; i < LANG_TSV_MAX_STRINGS; i++) {
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

    sysLogPrintf(LOG_NOTE, "LANG-MANIFEST: loaded TSV bank %d (%s) from %s",
                 bank, entry->id, path);
    return 1;
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
            if (!langManifestLoadExternalTsv(entry)) {
                return 0;
            }
        }
    } else if (!langIsBankLoaded(bank)) {
        langLoad(bank);
        sysLogPrintf(LOG_NOTE, "LANG-MANIFEST: loaded bank %d (%s)", bank, lang_id);
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
                langManifestLoadExternalTsv(entry);
            } else {
                if (s_ModLangBuffers[bank]) {
                    free(s_ModLangBuffers[bank]);
                    s_ModLangBuffers[bank] = NULL;
                    s_ModLangCatalogIds[bank][0] = '\0';
                    g_LangBanks[bank] = NULL;
                }
                langLoad(bank);
            }
        }
    }
}

s32 langManifestGetCount(void)
{
    return g_LangManifest.count;
}
