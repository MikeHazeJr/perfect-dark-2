#ifndef _IN_PDGUI_CAMPAIGN_BRIDGE_H
#define _IN_PDGUI_CAMPAIGN_BRIDGE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void pdguiEndscreenStartMission(void);
s32 pdguiEndscreenHasNextMission(void);
void pdguiEndscreenNextMission(void);
void pdguiEndscreenExitToMainMenu(void);
const char *pdguiEndscreenNextMissionLabel(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_CAMPAIGN_BRIDGE_H */
