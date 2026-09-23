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
 * Language-change reload prepares all live tracked banks before publication;
 * a malformed selected source leaves the prior language pointers intact.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "versioninfo.h"
#include "constants.h"
#include "system.h"
#include "types.h"
#include "langmanifest.h"
#include "lang_source.h"
#include "assetcatalog.h"
#include "assetprovider.h"
#include "fs.h"
#include "game/lang.h"
#include "data.h"

/* =========================================================================
 * Module state
 * ========================================================================= */

lang_manifest_t g_LangManifest;

extern uintptr_t *g_LangBanks[LANG_MANIFEST_MAX_BANKS];

static void *s_ModLangBuffers[LANG_MANIFEST_MAX_BANKS];
static char s_ModLangCatalogIds[LANG_MANIFEST_MAX_BANKS][CATALOG_ID_LEN];

static const char *langManifestRequestedLocale(void)
{
#if VERSION == VERSION_JPN_FINAL
    if (g_Jpn) return "ja";
#endif
#if VERSION >= VERSION_PAL_BETA
    if (g_LanguageId == LANGUAGE_PAL_FR) return "fr";
    if (g_LanguageId == LANGUAGE_PAL_DE) return "de";
    if (g_LanguageId == LANGUAGE_PAL_IT) return "it";
    if (g_LanguageId == LANGUAGE_PAL_ES) return "es";
    if (VERSION == VERSION_PAL_FINAL || VERSION == VERSION_PAL_BETA) return "en-GB";
#endif
    return "en";
}

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

static s32 langManifestPrepareExternalJson(const asset_entry_t *entry,
    lang_source_bank_t *candidate)
{
    const char *path;
    const char *error = NULL;
    s32 bank;
    s32 expected_count;
    u32 raw_size = 0;
    char *raw;
    lang_source_encoding_t encoding;

    if (!candidate) return 0;
    memset(candidate, 0, sizeof(*candidate));
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
            candidate, &error)) {
        sysLogPrintf(LOG_WARNING, "LANG-MANIFEST: rejected JSON bank '%s' (%s): %s",
                     entry->id, path, error ? error : "invalid source");
        free(raw);
        return 0;
    }
    free(raw);

    return 1;
}

static void langManifestPublishExternalJson(const asset_entry_t *entry,
    lang_source_bank_t *candidate)
{
    s32 bank = entry->ext.lang.bank_id;
    const char *path = langEntryFilePath(entry);
    free(s_ModLangBuffers[bank]);
    g_LangBanks[bank] = candidate->data;
    s_ModLangBuffers[bank] = candidate->data;
    candidate->data = NULL;
    strncpy(s_ModLangCatalogIds[bank], entry->id, CATALOG_ID_LEN - 1);
    s_ModLangCatalogIds[bank][CATALOG_ID_LEN - 1] = '\0';
    sysLogPrintf(LOG_NOTE, "LANG-MANIFEST: loaded JSON bank %d (%s) strings=%u from %s",
                 bank, entry->id, candidate->string_count, path);
}

static s32 langManifestLoadExternalJson(const asset_entry_t *entry)
{
    lang_source_bank_t candidate;
    if (!langManifestPrepareExternalJson(entry, &candidate)) return 0;
    langManifestPublishExternalJson(entry, &candidate);
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

        score = langSourceLocaleRank(entry->ext.lang.locale,
            langManifestRequestedLocale());
        if (!score) continue;
        score = score * 2 + (entry->bundled ? 0 : 1);
        if (score > best_score || (score == best_score &&
                best && strcmp(entry->id, best->id) < 0)) {
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

    if (!langManifestLoadExternalJson(entry)) return 0;
    langManifestRecordBank(bank);
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

s32 langManifestValidateId(const char *lang_id)
{
    const asset_entry_t *entry = lang_id ? assetCatalogResolve(lang_id) : NULL;
    lang_source_bank_t candidate;
    if (!entry || !langManifestPrepareExternalJson(entry, &candidate)) return 0;
    free(candidate.data);
    return 1;
}

s32 langManifestEnsureId(const char *lang_id)
{
    const asset_entry_t *entry;
    const asset_entry_t *selected;
    s32 bank;

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

    selected = langManifestFindBestEntryForBank(bank);
    if (!selected) return 0;
    if (!langIsBankLoaded(bank) || s_ModLangBuffers[bank] == NULL ||
            strcmp(s_ModLangCatalogIds[bank], selected->id) != 0) {
        if (!langManifestLoadBankFromCatalog(bank)) {
            sysLogPrintf(LOG_WARNING,
                         "LANG-MANIFEST: failed to load catalog bank %d (%s)",
                         bank, lang_id);
            return 0;
        }
    }

    langManifestRecordBank(bank);
    return 1;
}

s32 langManifestReload(void)
{
    lang_source_bank_t pending[LANG_MANIFEST_MAX_BANKS] = {{0}};
    const asset_entry_t *selected[LANG_MANIFEST_MAX_BANKS] = {0};
    s32 failed_bank = -1;

    /* Prepare every live bank before publishing any replacement. The old
     * pointers and IDs survive a malformed second (or later) bank. */
    for (s32 i = 0; i < g_LangManifest.count; i++) {
        s32 bank = g_LangManifest.bank_ids[i];
        if (bank <= 0 || bank >= LANG_MANIFEST_MAX_BANKS ||
                !g_LangBanks[bank] || selected[bank]) continue;
        selected[bank] = langManifestFindBestEntryForBank(bank);
        if (!selected[bank] ||
                !langManifestPrepareExternalJson(selected[bank], &pending[bank])) {
            failed_bank = bank;
            break;
        }
    }
    if (failed_bank >= 0) {
        for (s32 bank = 1; bank < LANG_MANIFEST_MAX_BANKS; bank++)
            free(pending[bank].data);
        sysLogPrintf(LOG_WARNING,
            "LANG-MANIFEST: locale reload rejected at bank %d; old banks retained",
            failed_bank);
        return 0;
    }
    for (s32 bank = 1; bank < LANG_MANIFEST_MAX_BANKS; bank++) {
        if (selected[bank]) langManifestPublishExternalJson(selected[bank], &pending[bank]);
    }
    return 1;
}

s32 langManifestGetCount(void)
{
    return g_LangManifest.count;
}
