/**
 * botvariant.h -- Bot Variant creation and persistence (D3R-8)
 *
 * Saves custom bot configurations as component .ini files under
 * {modsdir}/bot_variants/{slug}/ and registers them in the Asset Catalog
 * for immediate use without a full catalog rescan.
 *
 * The slug is derived from the display name: lowercased, spaces → underscores,
 * non-alphanumeric characters stripped.
 *
 * Usage:
 *   botVariantSave("Aggressive Sniper", "NormalSim",
 *                  0.9f, 0.3f, 0.85f, "custom", "High accuracy", "Player");
 *   // → writes mods/bot_variants/aggressive_sniper/bot.ini
 *   // → registers ASSET_BOT_VARIANT "aggressive_sniper" in catalog
 */

#ifndef _IN_BOTVARIANT_H
#define _IN_BOTVARIANT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Save a bot variant to disk and register it in the Asset Catalog.
 *
 * Creates {modsdir}/bot_variants/{slug}/bot.ini.
 * If a variant with the same slug already exists, it is overwritten
 * (last-write-wins, matching catalog semantics).
 *
 * @param name          Display name (e.g., "Aggressive Sniper")
 * @param base_type     AI behavior template ("NormalSim", "DarkSim", etc.)
 * @param accuracy      Accuracy multiplier [0.0, 1.0]
 * @param reaction_time Reaction time multiplier [0.0, 1.0] (lower = faster)
 * @param aggression    Aggression multiplier [0.0, 1.0]
 * @param category      Category label for Mod Manager (e.g., "custom")
 * @param description   Optional description text (NULL or "" = omitted)
 * @param author        Optional author name (NULL or "" = "Player")
 * @return 1 on success, 0 on failure
 */
s32 botVariantSave(const char *name,
                   const char *base_type,
                   f32 accuracy,
                   f32 reaction_time,
                   f32 aggression,
                   const char *category,
                   const char *description,
                   const char *author);

#ifdef __cplusplus
}
#endif

#endif /* _IN_BOTVARIANT_H */
