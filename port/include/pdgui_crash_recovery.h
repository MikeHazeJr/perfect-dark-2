#ifndef _IN_PDGUI_CRASH_RECOVERY_H
#define _IN_PDGUI_CRASH_RECOVERY_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

s32 pdguiCrashRecoveryIsActive(void);
void pdguiCrashRecoveryRender(s32 width, s32 height);

#ifdef __cplusplus
}
#endif

#endif
