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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "versioninfo.h"
#include "system.h"
#include "types.h"
#include "langmanifest.h"
#include "lang_source.h"
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
    const char *path;
    const char *error = NULL;
    s32 bank;
    s32 expected_count;
    u32 raw_size = 0;
    char *raw;
    lang_source_bank_t candidate;
    lang_source_encoding_t encoding;

    if (!entry || entry->type != ASSET_LANG) return 0;
    bank = entry->ext.lang.bank_id;
    if (bank <= 0 || bank >= LANG_MANIFEST_MAX_BANKS) return 0;
    path = langEntryFilePath(entry);
    if (!path || !path[0]) return 0;
    if (entry->ext.lang.string_count > LANG_SOURCE_MAX_STRINGS) {
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: invalid string_count for '%s'", entry->id);
        return 0;
    }

    raw = (char *)fsFileLoad(path, &raw_size);
    if (!raw || raw_size == 0) {
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: failed to load JSON '%s' for '%s'",
                     path, entry->id);
        free(raw);
        return 0;
    }
    /* Presence is independent of value: declared zero means an empty table. */
    expected_count = entry->ext.lang.string_count_declared
        ? (s32)entry->ext.lang.string_count : -1;
    encoding = langSourceEncodingForLocale(VERSION_ROMID, entry->ext.lang.locale);
    if (!langSourceParseJson(raw, raw_size, expected_count, encoding,
            &candidate, &error)) {
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: rejected JSON bank '%s' (%s): %s",
                     entry->id, path, error ? error : "invalid source");
        free(raw);
        return 0;
    }
    free(raw);

    /* Publish only a completely validated candidate. Rejection above keeps
     * the previously loaded bank and its catalog identity untouched. */
    free(s_ModLangBuffers[bank]);
    g_LangBanks[bank] = candidate.data;
    s_ModLangBuffers[bank] = candidate.data;
    strncpy(s_ModLangCatalogIds[bank], entry->id, CATALOG_ID_LEN - 1);
    s_ModLangCatalogIds[bank][CATALOG_ID_LEN - 1] = '\0';
    sysLogPrintf(LOG_NOTE, "LANG-MANIFEST: loaded JSON bank %d (%s) strings=%u from %s",
                 bank, entry->id, candidate.string_count, path);
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

    count = assetCatalogGetPoolSize();

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
