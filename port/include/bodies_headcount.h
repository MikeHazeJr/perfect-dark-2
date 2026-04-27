#ifndef PD_BODIES_HEADCOUNT_H
#define PD_BODIES_HEADCOUNT_H

#include <PR/ultratypes.h>

/*
 * bodies_headcount.h -- INV-5 / Cohort E.2 (player-init-architectural-fixes
 * 2026-04-26): pure stage_id-to-head-count lookup for bodiesReset.
 *
 * Background. bodiesReset's per-stage override map historically used a
 * strcmp literal chain:
 *
 *   if      (strcmp(stage_id, "base:infiltration") == 0) count = 5;
 *   else if (strcmp(stage_id, "base:rescue")       == 0) count = 4;
 *   else if (strcmp(stage_id, "base:escape")       == 0) count = 5;
 *   // else: silently default to 8
 *
 * The silent default surfaces as "guards use the wrong head set" with
 * no diagnostic when stage_id is empty, mod-defined, or any future base
 * stage name. Cohort E.2 adds a WARNING to the call site. The pure
 * helper here lifts the lookup into a testable form so the override
 * map is locked by pd-tests and any future addition (e.g. a new base
 * stage with a different head budget) is a single-call-site change.
 *
 * Returns:
 *   The override head count (3..16) for known stage_ids, OR
 *   default_count for unknown stage_ids OR empty / NULL stage_id.
 *   Always returns a positive integer.
 *
 * Pure: no globals, no I/O, no allocator dependencies. Suitable for
 * pd-tests cherry-pick.
 */

#ifdef __cplusplus
extern "C" {
#endif

s32 bodiesGetActiveHeadCountForStageId(const char *stage_id, s32 default_count);

#ifdef __cplusplus
}
#endif

#endif /* PD_BODIES_HEADCOUNT_H */
