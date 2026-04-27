/*
 * port/src/bodies_headcount.c -- INV-5 / Cohort E.2: pure stage_id ->
 * head-count override lookup for bodiesReset.
 *
 * See port/include/bodies_headcount.h for the contract.
 *
 * Pure: no globals, no I/O. The bodiesReset call site logs a WARNING
 * separately when this lookup falls through to the default; this TU
 * does not log so it stays test-friendly without sysLogPrintf stubs.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include "bodies_headcount.h"

s32 bodiesGetActiveHeadCountForStageId(const char *stage_id, s32 default_count)
{
	if (stage_id == NULL || stage_id[0] == '\0') {
		return default_count;
	}
	if (strcmp(stage_id, "base:infiltration") == 0) { return 5; }
	if (strcmp(stage_id, "base:rescue")       == 0) { return 4; }
	if (strcmp(stage_id, "base:escape")       == 0) { return 5; }
	return default_count;
}
