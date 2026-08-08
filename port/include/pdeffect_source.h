#ifndef PD_EFFECT_SOURCE_H
#define PD_EFFECT_SOURCE_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pd_effect_sound_id_fn)(s32 sound_ref, char *out, size_t out_n);

/* Build the canonical public profile-graph source for the three native base
 * effect tables. The returned UTF-8 buffer is malloc-owned by the caller. */
s32 pdEffectSourceBuildExplosionGraph(const char *catalog_id,
	const struct explosiontype *rows, size_t row_count,
	pd_effect_sound_id_fn sound_id_fn, char **out, size_t *out_len);
s32 pdEffectSourceBuildSparkGraph(const char *catalog_id,
	const struct sparktype *rows, size_t row_count, char **out, size_t *out_len);
s32 pdEffectSourceBuildSmokeGraph(const char *catalog_id,
	const struct smoketype *rows, size_t row_count, char **out, size_t *out_len);

const char *pdEffectSourceExplosionProfileId(size_t index);
const char *pdEffectSourceSparkProfileId(size_t index);
const char *pdEffectSourceSmokeProfileId(size_t index);

#ifdef __cplusplus
}
#endif

#endif
