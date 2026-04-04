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

#include <string.h>
#include "platform.h"
#include "system.h"
#include "types.h"
#include "langmanifest.h"
#include "assetcatalog.h"
#include "game/lang.h"

/* =========================================================================
 * Module state
 * ========================================================================= */

lang_manifest_t g_LangManifest;

/* =========================================================================
 * API
 * ========================================================================= */

void langManifestReset(void)
{
    s32 i;
    for (i = 0; i < LANG_MANIFEST_MAX_BANKS; i++) {
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

    /* Load if not already loaded */
    if (!langIsBankLoaded(bank)) {
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
            langLoad(bank);
        }
    }
}

s32 langManifestGetCount(void)
{
    return g_LangManifest.count;
}
