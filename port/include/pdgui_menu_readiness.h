#ifndef PDGUI_MENU_READINESS_H
#define PDGUI_MENU_READINESS_H

/* Independent read-only rejection bits; zero preserves ownership admission.
 * Absence stops inspection of unavailable pointers, so later bits are unknown. */
typedef enum pdgui_menu_input_rejection {
    PDGUI_MENU_INPUT_FOCUS_LOST = 1u << 0,
    PDGUI_MENU_INPUT_NO_CONTEXT = 1u << 1,
    PDGUI_MENU_INPUT_NO_NAME = 1u << 2,
    PDGUI_MENU_INPUT_NO_WINDOW = 1u << 3,
    PDGUI_MENU_INPUT_INACTIVE = 1u << 4,
    PDGUI_MENU_INPUT_HIDDEN = 1u << 5,
    PDGUI_MENU_INPUT_COLLAPSED = 1u << 6,
    PDGUI_MENU_INPUT_SKIP_ITEMS = 1u << 7,
    PDGUI_MENU_INPUT_RENDER_STALE = 1u << 8,
    PDGUI_MENU_INPUT_POPUP = 1u << 9,
    PDGUI_MENU_INPUT_WINDOW_SWITCH = 1u << 10,
    PDGUI_MENU_INPUT_NO_NAV_WINDOW = 1u << 11,
    PDGUI_MENU_INPUT_ROOT_MISMATCH = 1u << 12
} pdgui_menu_input_rejection_t;

typedef struct pdgui_menu_input_observation {
    unsigned int rejection_mask;
} pdgui_menu_input_observation_t;

#ifdef __cplusplus
extern "C" {
#endif

pdgui_menu_input_observation_t pdguiMenuObserveInput(
    const char *window_name, int application_focused);

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
