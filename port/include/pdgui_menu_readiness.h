#ifndef PDGUI_MENU_READINESS_H
#define PDGUI_MENU_READINESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only admission: the named ImGui menu was submitted in the latest
 * completed render, owns focused navigation, and has no popup/input blocker.
 * application_focused is the caller's persistent OS input-authority fact;
 * ImGuiIO::AppFocusLost is transient and is cleared by EndFrame(). The caller
 * separately checks current dialog, pool, settling and input-context facts. */
int pdguiMenuWindowOwnsInput(const char *window_name, int application_focused);

typedef struct pdgui_menu_view_readiness {
    int view;
    int submitted_tab;
    int play_focused;
    int settings_focused;
} pdgui_menu_view_readiness_t;

/* Record observations on the current native window, after its real widgets.
 * Window-owned storage dies with the ImGui context; no widget/caller buffers
 * or callbacks are retained. A requested but unsubmitted tab is -1. */
void pdguiMenuRecordView(int view, int submitted_tab, unsigned int play_item,
    unsigned int settings_item, int blocked);
/* Only a completed current frame with visible native focus and no active
 * editor/popup can return an observation. Does not alter focus or navigation. */
int pdguiMenuViewReadiness(const char *window_name, int application_focused,
    pdgui_menu_view_readiness_t *out);

#ifdef __cplusplus
}
#endif
#endif
