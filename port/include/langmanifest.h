/**
 * langmanifest.h -- Phase 3: Language bank manifest system
 *
 * Tracks which language string banks are currently loaded and provides an
 * API for screens and stages to declare their lang bank dependencies.
 *
 * Design:
 *   - g_LangManifest records every LANGBANK_* that has been loaded since the
 *     last langManifestReset() call.
 *   - langManifestRecordBank() is called by langReset() after each langLoad()
 *     to keep tracking in sync with the existing load path.
 *   - langManifestEnsureId() lets a screen or stage declare "I need this bank"
 *     using a catalog ID like "base:lang_options".  If the bank is not yet
 *     loaded it calls langLoad() and records it.
 *   - langManifestReload() reloads all tracked banks in the current language.
 *     Used by langSetEuropean() / langSetJpnEnabled() instead of langReload()
 *     so the manifest stays authoritative.
 *
 * Backward compatibility:
 *   langReset() continues to load the common banks (GUN, MPMENU, OPTIONS,
 *   MISC, etc.) exactly as before.  Screens that don't call langManifestEnsureId
 *   continue to work unchanged.
 *
 * Mod lang banks:
 *   Mods register catalog entries of type ASSET_LANG with an allocated bank_id.
 *   Those entries can be referenced by "mod:lang_foo" and loaded on demand via
 *   langManifestEnsureId("mod:lang_foo").
 */

#ifndef _IN_LANGMANIFEST_H
#define _IN_LANGMANIFEST_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of slots in g_LangBanks[] (lang.c) */
#define LANG_MANIFEST_MAX_BANKS 69

/**
 * Compact tracking structure for currently-loaded lang banks.
 *
 * bank_ids[] holds the LANGBANK_* constant of each tracked bank.
 * count is the number of valid entries (always <= LANG_MANIFEST_MAX_BANKS).
 * The array may have gaps if banks were cleared; use count as the high-water
 * mark and check for -1 sentinels when iterating.
 */
typedef struct {
    s32 bank_ids[LANG_MANIFEST_MAX_BANKS];
    s32 count;
} lang_manifest_t;

/** Manifest of currently-loaded lang banks.  Updated by all load paths. */
extern lang_manifest_t g_LangManifest;

/**
 * Reset all tracking.
 * Call at the start of langReset() before loading any banks so the manifest
 * accurately reflects the new stage's load state.
 */
void langManifestReset(void);

/**
 * Record that a bank was loaded.
 * Call immediately after langLoad(bank) in langReset() and any other direct
 * lang-load site so the manifest stays in sync.
 * Silently ignores duplicate entries.
 */
void langManifestRecordBank(s32 bank);

/**
 * Ensure a lang bank identified by catalog ID is loaded.
 *
 * Resolves the catalog ID (e.g. "base:lang_options") to a LANGBANK_* value
 * via the asset catalog.  If the bank is not yet loaded (g_LangBanks[bank]
 * is NULL), calls langLoad() and records the bank in g_LangManifest.
 *
 * Returns 1 if the bank is now loaded.
 * Returns 0 if the catalog ID is unknown, the entry is not ASSET_LANG, or
 * the bank_id is out of range.
 *
 * Call from menu screens or stage-setup code that needs a specific bank:
 *
 *   // In a menu screen's init or first-render:
 *   langManifestEnsureId("base:lang_title");
 *
 *   // From C++:
 *   extern "C" s32 langManifestEnsureId(const char *lang_id);
 *   langManifestEnsureId("base:lang_mpmenu");
 */
s32 langManifestEnsureId(const char *lang_id);

/**
 * Reload all tracked banks in the current language.
 *
 * Iterates g_LangManifest and calls langLoad() for each recorded bank.
 * Use this in langSetEuropean() / langSetJpnEnabled() (PAL/JPN builds) in
 * place of the raw langReload() so that banks added via langManifestEnsureId
 * are also reloaded.
 *
 * On builds where langReload() is not available (NTSC without PAL_BETA),
 * this provides the same reload behaviour for the PC port.
 */
void langManifestReload(void);

/** Return the number of currently-tracked banks. */
s32 langManifestGetCount(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_LANGMANIFEST_H */
