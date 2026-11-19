#ifndef PD_AI_CATALOG_REFS_H
#define PD_AI_CATALOG_REFS_H

/*
 * ai_catalog_refs.h -- the asset arguments of AI commands by catalog id
 * (context/designs/modding/ai-typed-operands.md, slices 3 and 3b).
 *
 * A typed AI argument names its model, weapon, body, head, animation or
 * sound by catalog id. The extractor names each value it writes
 * (aiCatalogRefName) and checks the id back to the same value
 * (aiCatalogRefResolve); the stage compiler resolves the ids of every row
 * it loads with the same function, so a row always compiles to the value
 * it was written from.
 *
 * - Weapons: the base weapon table (g_WeaponDataCatalogIds) names every
 *   native weapon number, so a base weapon resolves to its native number
 *   whatever the catalog has scanned; any other id goes through the
 *   catalog.
 * - Animations: the catalog's readable id for the slot, checked against
 *   the catalog (every slot of the base table is registered, including the
 *   slots the ROM leaves empty).
 * - Sounds: the value given here is the asset part of a sound field
 *   (aiSoundRefSplit in ai_command_schema.h): a configured sound (bit 15,
 *   the catalog's configured alias by its packed number), an MP3 voice line
 *   (AI_SOUND_REF_MP3 set: the voice file by its file number) or a sample
 *   (its sound number). The playback bits are the schema's own arguments.
 * - Models, bodies and heads: the catalog's existing lookups.
 *
 * Every id returned is a catalog entry's own id (stable for the catalog's
 * lifetime); a value the catalog cannot name returns NULL.
 */

#include <stdint.h>
#include "ai_command_schema.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The catalog id for an asset argument's value, or NULL. */
const char *aiCatalogRefName(ai_operand_kind_t kind, int64_t value);

/* The value for a catalog id: 1, or 0 when the id names no asset of the
 * kind. For a sound the value is the asset part (see above). */
int aiCatalogRefResolve(ai_operand_kind_t kind, const char *id, int64_t *out_value);

#ifdef __cplusplus
}
#endif

#endif
