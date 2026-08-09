/**
 * effect_executor.h -- production consumers for compiled public .pdeffect source.
 *
 * Profile-row resolution is deliberately activation-gated: a public row ID
 * resolves only while its canonical profile library is installed. Native base
 * tables are implementation storage, not an alternate authored source.
 */
#ifndef PD_EFFECT_EXECUTOR_H
#define PD_EFFECT_EXECUTOR_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct effect_graph_runtime;

s32 effectExecutorInstallProgram(const struct effect_graph_runtime *record,
	char *error, size_t error_cap);
void effectExecutorRemoveProgram(const char *asset_id);
void effectExecutorReset(void);

s32 effectExecutorResolveExplosionProfile(const char *row_id);
s32 effectExecutorResolveSparkProfile(const char *row_id);
s32 effectExecutorResolveSmokeProfile(const char *row_id);

#ifdef __cplusplus
}
#endif

#endif
