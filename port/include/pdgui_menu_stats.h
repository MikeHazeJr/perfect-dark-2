/**
 * pdgui_menu_stats.h -- M2.3 Stats Viewer UI public API
 */

#ifndef PDGUI_MENU_STATS_H
#define PDGUI_MENU_STATS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

void pdguiMenuStatsShow(void);
void pdguiMenuStatsHide(void);
s32  pdguiMenuStatsIsVisible(void);
void pdguiMenuStatsRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* PDGUI_MENU_STATS_H */
