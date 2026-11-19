#ifndef PD_SCENARIO_PORTAL_SOURCE_H
#define PD_SCENARIO_PORTAL_SOURCE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCENARIO_SOURCE_PORTAL_MAX_VERTICES 32
#define SCENARIO_PORTAL_RULE_FLOOD_BLOCKED 0x01u
#define SCENARIO_PORTAL_RULE_OPEN_TO_LIGHT 0x02u

typedef struct scenario_source_portal_row {
	s32 room1;
	s32 room2;
	u32 flags;
	s32 vertex_count;
	f32 vertices[SCENARIO_SOURCE_PORTAL_MAX_VERTICES][3];
	u8 light_rules;
} scenario_source_portal_row_t;

/* Parse the entire native table. Empty rows are valid. The caller frees
 * *rows on success. A refusal leaves both outputs unchanged. */
s32 scenarioPortalSourceRead(const char *text, size_t size, scenario_source_portal_row_t **rows,
	s32 *row_count, char *err, size_t err_cap);

/* Validation for an exact original-stage candidate. The declared room
 * count may be zero when the stage infers it from native inputs. */
s32 scenarioPortalSourceCheckCandidate(const scenario_source_portal_row_t *rows, s32 row_count,
	s32 declared_rooms, char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
