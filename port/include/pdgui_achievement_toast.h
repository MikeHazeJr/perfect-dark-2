/**
 * pdgui_achievement_toast.h -- Slide-in toast notifications for newly
 * unlocked achievements.  Called from the endscreen `IsWindowAppearing`
 * path after `achievementsRefresh()`; toasts render on the ImGui
 * foreground drawlist so they stay visible over menus.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */
#ifndef PDGUI_ACHIEVEMENT_TOAST_H
#define PDGUI_ACHIEVEMENT_TOAST_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Push a toast onto the queue.  Name is the achievement display name
 * ("Sharpshooter"); description is the achievement tagline
 * ("Get 100 headshots").  Either string may be NULL; both are copied so
 * the caller does not need to keep them alive.  Up to 4 toasts queue at
 * once; further pushes drop silently.
 */
void pdguiAchievementToastPush(const char *name, const char *description);

/**
 * Scan newly-unlocked achievements and push a toast for each.  Safe to
 * call every frame; the underlying `achievementGetNewlyUnlocked` clears
 * the "new" state on each call so each unlock fires exactly one toast.
 */
void pdguiAchievementToastPollUnlocks(void);

/**
 * Render queued toasts.  Called from `pdguiRender` every frame; no-op
 * when the queue is empty.
 */
void pdguiAchievementToastRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* PDGUI_ACHIEVEMENT_TOAST_H */
