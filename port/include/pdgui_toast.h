/**
 * pdgui_toast.h -- Phase 2 transient notification toasts.
 *
 * Bottom-right stack of fading popups for:
 *   - "X came online" / "X went offline"        (SOCIAL category, Q8)
 *   - "X invited you to play"                   (INVITES category, Q8)
 *   - "X got <achievement>"                     (SOCIAL category, Q8)
 *   - System-level connectivity events (NAT failure, version mismatch)
 *
 * Per-friend mute via socialFriend.muted, per-category mute via
 * socialNotifMaskGet -- both checked at enqueue time. A friend on the
 * block list will never enqueue a toast (the presence layer already
 * drops their pings).
 */

#ifndef _IN_PDGUI_TOAST_H
#define _IN_PDGUI_TOAST_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOAST_CATEGORY_SOCIAL   1   /* matches SOCIAL_NOTIF_SOCIAL */
#define TOAST_CATEGORY_INVITES  2   /* matches SOCIAL_NOTIF_INVITES */
#define TOAST_CATEGORY_SYSTEM   4   /* always shown */

void pdguiToastInit(void);
void pdguiToastShutdown(void);
void pdguiToastTick(void);
void pdguiToastRender(s32 winW, s32 winH);

/**
 * Enqueue a toast. friend_handle may be 0 for non-friend events
 * (system / connectivity). category gates against the user's
 * notification mask + per-friend mute. Returns 1 on enqueue, 0 if
 * suppressed.
 */
s32 pdguiToastEnqueue(u32 friend_handle, u32 category,
                       const char *title, const char *body);

s32 pdguiToastIsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_TOAST_H */
