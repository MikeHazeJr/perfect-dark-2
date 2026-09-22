#ifndef PDGUI_TEXT_KEYBOARD_H
#define PDGUI_TEXT_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

enum PdguiTextKeyboardEditKey {
    PDGUI_TEXT_KEY_NONE, PDGUI_TEXT_KEY_BACKSPACE,
    PDGUI_TEXT_KEY_LEFT, PDGUI_TEXT_KEY_RIGHT,
    PDGUI_TEXT_KEY_ENTER, PDGUI_TEXT_KEY_ESCAPE, PDGUI_TEXT_KEY_SHIFT
};
enum PdguiTextKeyboardPointerKind {
    PDGUI_TEXT_POINTER_MOVE, PDGUI_TEXT_POINTER_PRESS,
    PDGUI_TEXT_POINTER_RELEASE, PDGUI_TEXT_POINTER_WHEEL
};
typedef struct PdguiTextKeyboardEdit {
    unsigned int character;
    int key;
} PdguiTextKeyboardEdit;
typedef struct PdguiTextKeyboardInput {
    int enabled, controller_preferred, focus_allowed;
    int accept, cancel, up, down, left, right;
    float delta_seconds;
} PdguiTextKeyboardInput;
typedef struct PdguiTextKeyboardKeyInfo {
    char label[16];
    unsigned int character;
    int key;
    float left, top, right, bottom;
} PdguiTextKeyboardKeyInfo;

typedef struct PdguiTextKeyboardHints {
    int controller_mode;
    char accept[96], cancel[96], up[96], down[96], left[96], right[96];
} PdguiTextKeyboardHints;
/* Copy display labels only; no callback or external string is retained. */
void pdguiTextKeyboardSetHints(const PdguiTextKeyboardHints *hints);
/* Lifecycle close discards commands and key activation, retaining owned ups. */
void pdguiTextKeyboardCancelOwner(void);
/* Retire lost pointer releases using SDL's current physical button mask.
 * Uses the same SDL button-minus-one bit convention as PointerEvent. */
void pdguiTextKeyboardReconcilePointerButtons(unsigned int held_mask);

/* BeginFrame runs before ImGui::NewFrame using mapped action state only.
 * A true result owns mapped menu navigation for the following render frame. */
int pdguiTextKeyboardBeginFrame(const PdguiTextKeyboardInput *input);
/* After real menu/text widgets, before ImGui::Render. Foreground drawing does
 * not submit focusable items or change the text field's ActiveId/NavWindow. */
void pdguiTextKeyboardRender(void);
void pdguiTextKeyboardReset(void);
int pdguiTextKeyboardOwnsMenuInput(void);

/* Invoke before forwarding pointer events to ImGui. Coordinates are ImGui
 * display coordinates; button 0 is left. Owned gestures retain their release
 * even outside the keyboard. Identity observation remains a separate step. */
int pdguiTextKeyboardPointerEvent(int kind, float x, float y, int button);

/* Native InputTextEx hook, called after its outside-click decision. Reads only
 * IDs and flags; never stores or returns a caller buffer. An exact matching
 * active field consumes at most one command in its assigned frame. */
PdguiTextKeyboardEdit pdguiTextKeyboardInputText(unsigned int field_id,
    unsigned int window_id, int flags, int input_allowed);
void pdguiTextKeyboardFinishInputText(unsigned int field_id,
    unsigned int window_id, int validated);

/* Read-only geometry supports pointer hit testing and production-path tests. */
int pdguiTextKeyboardKeyCount(void);
int pdguiTextKeyboardGetKey(int index, PdguiTextKeyboardKeyInfo *out);

#ifdef __cplusplus
}
#endif
#endif
