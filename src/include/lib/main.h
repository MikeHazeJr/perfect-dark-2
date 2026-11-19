#ifndef _IN_LIB_MAIN_H
#define _IN_LIB_MAIN_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

extern s32 g_MainIsBooting;

void mainInit(void);
void mainProc(void);
void mainOverrideVariable(char *name, void *value);
void mainLoop(void);
void mainTick(void);
void mainEndStage(void);
void mainEndStageWithoutResults(void);
void mainChangeToStage(s32 stagenum);
s32 mainStageChangePending(void);
void mainQueueGridRoomReturn(void);
s32 mainQueueGridStageAbort(u8 room_id);
s32 mainGridRoomReturnPending(void);
s32 mainGridRoomBackdropActive(void);
void mainStopGridWorldForShutdown(void);
void mainRetireGridForShutdown(void);
void func0000e990(void);
void func0000e9c0(void);
s32 mainGetStageNum(void);

#endif
