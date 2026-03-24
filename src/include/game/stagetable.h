#ifndef _IN_GAME_STAGETABLE_H
#define _IN_GAME_STAGETABLE_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

/* Phase 2: Dynamic stage table */
void stageTableInit(void);
struct stagetableentry *stageGetEntry(s32 index);
s32 stageTableAppend(const struct stagetableentry *entry);

/* Existing lookup functions */
struct stagetableentry *stageGetCurrent(void);
s32 stageGetIndex(s32 stagenum);

/* Phase 3: Index domain translation — stagenum → solo stage index, or -1 */
s32 soloStageGetIndex(s32 stagenum);

#endif
