#ifndef PD_STAGE_ARCHIVE_REPAIR_H
#define PD_STAGE_ARCHIVE_REPAIR_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot config and discovery run before the first main-thread pump. A zero
 * queue result leaves the caller responsible for synchronous repair. */
void stageArchiveRepairSetEnabled(s32 enabled);
s32 stageArchiveRepairQueue(s32 stagenum, const char *expected_id, const char *relative_path);
/* Keep the walker away from both pending and failed source paths. */
s32 stageArchiveRepairIsPendingPath(const char *relative_path);
s32 stageArchiveRepairIsPendingMissionPath(const char *relative_path);

/* 0 = ready (including stages without a job), 1 = pending, -1 = unavailable. */
s32 stageArchiveRepairStatus(s32 stagenum);
s32 stageArchiveRepairHasJobs(void);

/* Wait for the menu on first boot, then keep polling and publishing. */
void stageArchiveRepairPump(s32 menu_ready);

#ifdef __cplusplus
}
#endif

#endif
