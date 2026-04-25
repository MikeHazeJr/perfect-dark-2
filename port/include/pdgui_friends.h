/**
 * pdgui_friends.h -- Phase 1 social UI surfaces.
 *
 * Three surfaces share this module per Section 8 of the design doc:
 *
 *   Status indicator -- always-on, top-right of the screen. Shows local
 *                       presence state (online / in-match / appear-offline)
 *                       plus the local connect code. Tap to expand into
 *                       the sidebar.
 *   Sidebar          -- friend rows with quick actions (invite / message);
 *                       inbox of pending invitations; recent activity
 *                       feed. Toggled with the Tab key inside ImGui or
 *                       by clicking the status indicator.
 *   Social menu      -- full-screen panel with the entire friend list,
 *                       block list, settings shortcuts, and the
 *                       add-friend modal (P1.I).
 *
 * Mike's Q16 rule (no drill-in/drill-out) is enforced layout-side: rows
 * are statically visible, D-pad navigates directly, A engages the focused
 * action, B cancels the surface only at the top.
 */

#ifndef _IN_PDGUI_FRIENDS_H
#define _IN_PDGUI_FRIENDS_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open / close the sidebar without going through an action map binding. */
void pdguiFriendsSidebarOpen(void);
void pdguiFriendsSidebarClose(void);
void pdguiFriendsSidebarToggle(void);
s32  pdguiFriendsSidebarIsOpen(void);

/* Open / close the full-screen Social menu. */
void pdguiFriendsSocialOpen(void);
void pdguiFriendsSocialClose(void);
s32  pdguiFriendsSocialIsOpen(void);

/* Per-friend 1:1 chat panel (Phase 2). */
void pdguiFriendsChatOpen(u32 friend_handle);
void pdguiFriendsChatClose(void);
s32  pdguiFriendsChatIsOpen(void);
u32  pdguiFriendsChatTargetHandle(void);

/* Render the Phase 1 social surfaces. Call from pdguiRender after the
 * standard overlays so the sidebar / social menu paint above gameplay
 * windows. */
void pdguiFriendsRender(s32 winW, s32 winH);

/* Status indicator -- thin top-right widget. Renders inside the same
 * frame; no separate Begin/End. */
void pdguiFriendsStatusIndicatorRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_FRIENDS_H */
