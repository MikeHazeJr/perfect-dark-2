/**
 * pdgui_menu_warning.cpp -- Generic ImGui renderer for typed PD dialogs.
 *
 * Handles dialog types via the type-based fallback system:
 *   MENUDIALOGTYPE_DANGER  (2) — Red palette, error sound (warnings, errors, delete confirm)
 *   MENUDIALOGTYPE_SUCCESS (3) — Green palette, success sound (training complete, mission success)
 *
 * Dynamically reads each dialog's title and items, rendering them generically.
 * This covers dozens of dialogs without individual registration.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Use extern "C" forward declarations for all game symbols.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "system.h"

/* ========================================================================
 * Forward declarations for game symbols
 * ======================================================================== */

extern "C" {

/* Menu item types */
#define MENUITEMTYPE_LABEL       0x01
#define MENUITEMTYPE_LIST        0x02
#define MENUITEMTYPE_SELECTABLE  0x04
#define MENUITEMTYPE_SLIDER      0x08
#define MENUITEMTYPE_CHECKBOX    0x09
#define MENUITEMTYPE_SEPARATOR   0x0b
#define MENUITEMTYPE_DROPDOWN    0x0c
#define MENUITEMTYPE_KEYBOARD    0x0d
#define MENUITEMTYPE_MARQUEE     0x17
#define MENUITEMTYPE_END         0x1a

/* Menu item flags (from src/include/constants.h) */
#define MENUITEMFLAG_LITERAL_TEXT 0x08000000

/* Menu operations (subset actually used by the typed-dialog renderer;
 * full list in src/include/constants.h). */
#define MENUOP_GETOPTIONCOUNT     1
#define MENUOP_GETOPTIONTEXT      3
#define MENUOP_SET                6
#define MENUOP_GETSELECTEDINDEX   7
#define MENUOP_GET                8
#define MENUOP_GETSLIDER          9
#define MENUOP_GETSLIDERLABEL    10
#define MENUOP_CHECKDISABLED     12
#define MENUOP_GETTEXT           17

/* Dialog types */
#define MENUDIALOGTYPE_DEFAULT 1
#define MENUDIALOGTYPE_DANGER  2
#define MENUDIALOGTYPE_SUCCESS 3

/* Keyboard buffer size */
#define MPSETUP_MAXNAME 17

struct menuitem {
    u8 type;
    u8 param;
    u32 flags;
    intptr_t param2;
    intptr_t param3;
    union {
        uintptr_t (*handler)(s32 operation, struct menuitem *item, union handlerdata *data);
        void (*handlervoid)(s32 operation, struct menuitem *item, union handlerdata *data);
    };
};

/* handlerdata is a large union in types.h (~128+ bytes).
 * Over-allocate so item handlers don't corrupt the stack.
 * We also define the keyboard, slider, checkbox, and dropdown sub-structs
 * for confirmation dialogs and widgets (S193 sliders, S195 B4 checkbox +
 * dropdown for Cheats / Cinema / MP setup). */
struct handlerdata_keyboard {
    char *string;
};

struct handlerdata_slider {
    u32 value;
    char *label;
};

struct handlerdata_checkbox {
    u32 value;
};

struct handlerdata_dropdown {
    uintptr_t value;
    uintptr_t unk04;
};

union handlerdata {
    struct handlerdata_keyboard  keyboard;
    struct handlerdata_slider    slider;
    struct handlerdata_checkbox  checkbox;
    struct handlerdata_dropdown  dropdown;
    u8 _pad[256];
};

struct menudialogdef {
    u8 type;
    uintptr_t title;
    struct menuitem *items;
    s32 (*handler)(s32 operation, struct menudialogdef *dialogdef, union handlerdata *data);
    u32 flags;
    struct menudialogdef *nextsibling;
};

/* Language strings */
const char *langSafe(s32 textid);

/* Menu stack */
void menuPopDialog(void);

/* Video info */
s32 viGetWidth(void);
s32 viGetHeight(void);

/* Dialog flags */
#define MENUDIALOGFLAG_LITERAL_TEXT   0x2000
#define MENUDIALOGFLAG_CLOSEONSELECT 0x0001

} /* extern "C" */

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_Registered = false;

/* Per-dialog keyboard text buffer for ImGui InputText.
 * Indexed by dialog pointer (simple pool — at most one keyboard dialog active). */
static char s_KbdBuffer[MPSETUP_MAXNAME + 1] = {};
static void *s_KbdDialogDef = nullptr;   /* dialogdef that owns s_KbdBuffer */
static bool  s_KbdInitialised = false;

/* ========================================================================
 * Helpers
 * ======================================================================== */

static const char *getDialogTitle(struct menudialogdef *def, const char *fallback)
{
    if (!def) return fallback;

    if (def->flags & MENUDIALOGFLAG_LITERAL_TEXT) {
        return (const char *)(def->title);
    }

    if (def->title != 0) {
        const char *s = langSafe((s32)def->title);
        if (s && s[0]) return s;
    }

    return fallback;
}

static const char *getItemLabel(struct menuitem *item)
{
    if (!item) return "";

    /* S193 Batch 1 fix: items tagged MENUITEMFLAG_LITERAL_TEXT store a raw
     * C-string pointer in param2, not a language ID.  Before this fix,
     * langSafe() would interpret the pointer bits as a lang index and
     * crash or return garbage — visible as "missing text" on
     * g_ExitGameMenuDialog and similar legacy screens. */
    if (item->flags & MENUITEMFLAG_LITERAL_TEXT) {
        const char *lit = (const char *)item->param2;
        return lit ? lit : "";
    }

    if (item->param2 == 0) return "";

    /* S195 Batch 4 fix: match the legacy menuResolveText() contract in
     * src/game/menu.c:490.  Lang text IDs are low integers (< 0x5a00);
     * anything at or above that threshold is a pointer — either a literal
     * text pointer or a callback (char*(*)(struct menuitem *)).  The
     * CHECKBOX items in cheats.c use param2 as a callback
     * (cheatGetNameIfUnlocked) that returns the cheat name dynamically,
     * so treating them as lang IDs would render garbage. */
    if ((uintptr_t)item->param2 < 0x5a00u) {
        const char *s = langSafe((s32)item->param2);
        return (s && s[0]) ? s : "";
    }

    /* Function pointer or literal pointer.  Try as callback first — if
     * the pointer is a function, the game-side convention is
     * char *(*fn)(struct menuitem *).  If it's a literal string pointer
     * mis-classified here, the wrong branch would crash, but the game
     * convention for checkbox/list labels is that high-valued param2 is
     * always a function pointer (verified against cheats.c and
     * trainingmenus.c). */
    typedef char *(*ItemLabelFn)(struct menuitem *);
    ItemLabelFn fn = (ItemLabelFn)item->param2;
    char *s = fn(item);
    return (s && s[0]) ? s : "";
}

/* ========================================================================
 * Generic Typed Dialog Renderer
 *
 * paletteIdx:  PD palette index (2=Red/Danger, 3=Green/Success)
 * soundOnOpen: Sound to play when dialog appears
 * titleColor:  ImU32 color for the title text
 * fallbackTitle: Default title if dialog has none
 * ======================================================================== */

static s32 renderTypedDialog(struct menudialog *dialog,
                              struct menu *menu,
                              s32 winW, s32 winH,
                              s32 paletteIdx, s32 soundOnOpen,
                              ImU32 titleColor, const char *fallbackTitle)
{
    /* Get dialog definition from the live dialog struct (offset 0x00) */
    struct menudialogdef *def = *(struct menudialogdef **)((u8 *)dialog);
    if (!def) return 0;

    /* S192 popup scrim primitive — dim the full viewport behind the modal
     * so the player's focus is on the dialog, not the game scene behind it.
     * Idempotent: multiple calls per frame just draw into the same rect. */
    pdguiPopupDarkenBehind(0.55f);

    /* Switch palette */
    s32 prevPalette = pdguiGetPalette();
    pdguiSetPalette(paletteIdx);

    /* ---- Layout ---- */
    float scale = pdguiScaleFactor();
    float dialogW = pdguiScale(570.0f);
    float dialogH = pdguiScale(330.0f);
    ImVec2 dlgPos = pdguiCenterPos(dialogW, dialogH);
    float dialogX = dlgPos.x;
    float dialogY = dlgPos.y;

    float pdTitleH = pdguiScale(36.0f);
    if (pdTitleH < 18.0f) pdTitleH = 18.0f;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoScrollbar;

    /* Unique window ID based on dialog pointer to handle multiple open */
    char winId[64];
    snprintf(winId, sizeof(winId), "##typed_dialog_%p", (void *)dialog);

    if (!ImGui::Begin(winId, nullptr, wflags)) {
        ImGui::End();
        pdguiSetPalette(prevPalette);
        return 1;
    }

    /* Auto-possess + sound on appear */
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        if (soundOnOpen >= 0) {
            pdguiPlaySound(soundOnOpen);
        }
    }

    /* Opaque backdrop — typed dialogs overlay other menus, so the body
     * must not be see-through. */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          pdguiPalImU32(PDPAL_BODYBG, 255), 0.0f);
    }

    /* Draw PD-authentic dialog frame */
    const char *title = getDialogTitle(def, fallbackTitle);
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, title, 1);

    /* Title text with glow */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);

        ImVec2 titleSize = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dialogX + (dialogW - titleSize.x) * 0.5f,
                           dialogY + (pdTitleH - titleSize.y) * 0.5f),
                    titleColor, title);
    }

    /* Content below title */
    pdguiSetCursorBelowTitle(pdTitleH);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f * scale);

    /* ---- Iterate menu items ---- */
    if (def->items) {
        s32 selectableIdx = 0;
        bool hasAnySelectable = false;

        /* Count selectables */
        for (struct menuitem *it = def->items; it->type != MENUITEMTYPE_END; it++) {
            if (it->type == MENUITEMTYPE_SELECTABLE) hasAnySelectable = true;
        }

        /* Render items */
        for (struct menuitem *item = def->items; item->type != MENUITEMTYPE_END; item++) {
            switch (item->type) {
                case MENUITEMTYPE_LABEL: {
                    const char *label = getItemLabel(item);
                    if (label[0]) {
                        float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
                        ImVec2 textSize = ImGui::CalcTextSize(label, nullptr, false, availW);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                             (availW - textSize.x) * 0.5f);
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availW);
                        ImGui::TextWrapped("%s", label);
                        ImGui::PopTextWrapPos();
                    }
                    break;
                }

                case MENUITEMTYPE_SEPARATOR:
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    break;

                case MENUITEMTYPE_KEYBOARD: {
                    /* ImGui text input replacing the legacy on-screen keyboard.
                     * Initialise buffer from the handler on first appearance. */
                    if (s_KbdDialogDef != (void *)def || !s_KbdInitialised) {
                        memset(s_KbdBuffer, 0, sizeof(s_KbdBuffer));
                        if (item->handler) {
                            union handlerdata hd;
                            memset(&hd, 0, sizeof(hd));
                            hd.keyboard.string = s_KbdBuffer;
                            item->handler(MENUOP_GETTEXT, item, &hd);
                        }
                        s_KbdDialogDef = (void *)def;
                        s_KbdInitialised = true;
                    }

                    float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
                    ImGui::SetNextItemWidth(availW);
                    bool entered = ImGui::InputText("##kbd_input", s_KbdBuffer,
                                                     sizeof(s_KbdBuffer),
                                                     ImGuiInputTextFlags_EnterReturnsTrue
                                                     | ImGuiInputTextFlags_AutoSelectAll);

                    /* Auto-focus the text input on first frame */
                    if (ImGui::IsWindowAppearing()) {
                        ImGui::SetKeyboardFocusHere(-1);
                    }

                    if (entered) {
                        pdguiPlaySound(PDGUI_SND_SELECT);
                        if (item->handler) {
                            union handlerdata hd;
                            memset(&hd, 0, sizeof(hd));
                            hd.keyboard.string = s_KbdBuffer;
                            item->handler(MENUOP_SET, item, &hd);
                        }
                        s_KbdInitialised = false;
                        s_KbdDialogDef = nullptr;
                    }
                    break;
                }

                case MENUITEMTYPE_SLIDER: {
                    /* Slider with 0..255 range (per handler ABI).  Labels
                     * come from the handler's MENUOP_GETSLIDERLABEL case.
                     * Used by g_PdModeSettingsMenuDialog and peers. */
                    const char *label = getItemLabel(item);
                    if (!label[0]) label = "(slider)";

                    u32 curValue = 0;
                    char valueLabel[32] = "";
                    if (item->handler) {
                        union handlerdata hd;
                        memset(&hd, 0, sizeof(hd));
                        item->handler(MENUOP_GETSLIDER, item, &hd);
                        curValue = hd.slider.value;

                        memset(&hd, 0, sizeof(hd));
                        hd.slider.label = valueLabel;
                        item->handler(MENUOP_GETSLIDERLABEL, item, &hd);
                    }

                    ImGui::PushID((const void *)item);

                    /* Left-aligned label, right-aligned current value text. */
                    ImGui::TextUnformatted(label);
                    if (valueLabel[0]) {
                        float availW = ImGui::GetContentRegionAvail().x;
                        ImVec2 vSize = ImGui::CalcTextSize(valueLabel);
                        ImGui::SameLine(0.0f, 0.0f);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                              availW - vSize.x);
                        ImGui::TextUnformatted(valueLabel);
                    }

                    /* Slider widget.  255 is the full scale used by the
                     * PD-mode handlers (health/damage/accuracy bytes). */
                    int v = (int)curValue;
                    float sliderW = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(sliderW);
                    if (ImGui::SliderInt("##slider_val", &v, 0, 255, "",
                                          ImGuiSliderFlags_NoInput)) {
                        if (v < 0)   v = 0;
                        if (v > 255) v = 255;
                        if (item->handler) {
                            union handlerdata hd;
                            memset(&hd, 0, sizeof(hd));
                            hd.slider.value = (u32)v;
                            item->handler(MENUOP_SET, item, &hd);
                        }
                        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                    }

                    ImGui::PopID();
                    break;
                }

                case MENUITEMTYPE_SELECTABLE: {
                    const char *label = getItemLabel(item);
                    if (!label[0]) label = "OK";

                    float buttonW = 120.0f * scale;
                    float buttonH = 28.0f * scale;

                    /* Center button group on first selectable */
                    if (selectableIdx == 0) {
                        float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
                        s32 remCount = 0;
                        for (struct menuitem *rem = item; rem->type != MENUITEMTYPE_END; rem++) {
                            if (rem->type == MENUITEMTYPE_SELECTABLE) remCount++;
                        }
                        float totalBtnW = buttonW * remCount + 8.0f * scale * (remCount - 1);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                             (availW - totalBtnW) * 0.5f);
                    }

                    if (selectableIdx > 0) {
                        ImGui::SameLine(0, 8.0f * scale);
                    }

                    ImGui::PushID(selectableIdx);
                    if (ImGui::Button(label, ImVec2(buttonW, buttonH))) {
                        pdguiPlaySound(PDGUI_SND_SELECT);
                        if (item->handler) {
                            union handlerdata hd;
                            memset(&hd, 0, sizeof(hd));
                            item->handler(MENUOP_SET, item, &hd);
                        }
                    }

                    if (selectableIdx == 0) {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                    selectableIdx++;
                    break;
                }

                case MENUITEMTYPE_CHECKBOX: {
                    /* S195 Batch 4: Checkboxes are used by Cheats, MP Options,
                     * and a handful of other list-style dialogs.  Label may be
                     * a langID OR a char*(*)(menuitem*) callback (getItemLabel
                     * handles both). */
                    const char *label = getItemLabel(item);
                    if (!label[0]) label = "(unnamed)";

                    /* Read current state via MENUOP_GET.  The handler returns
                     * the value directly as the function's return code rather
                     * than via data->checkbox.value — cheatCheckboxMenuHandler
                     * follows this convention. */
                    bool checked = false;
                    bool disabled = false;
                    if (item->handler) {
                        union handlerdata hd;
                        memset(&hd, 0, sizeof(hd));
                        uintptr_t r = item->handler(MENUOP_GET, item, &hd);
                        checked = (r != 0) || (hd.checkbox.value != 0);

                        /* MENUOP_CHECKDISABLED — returns non-zero when the
                         * item should render disabled (e.g., locked cheats). */
                        memset(&hd, 0, sizeof(hd));
                        uintptr_t d = item->handler(MENUOP_CHECKDISABLED, item, &hd);
                        disabled = (d != 0);
                    }

                    ImGui::PushID((const void *)item);
                    if (disabled) ImGui::BeginDisabled();

                    bool changed = ImGui::Checkbox(label, &checked);

                    if (disabled) ImGui::EndDisabled();
                    ImGui::PopID();

                    if (changed) {
                        pdguiPlaySound(checked ? PDGUI_SND_TOGGLEON : PDGUI_SND_TOGGLEOFF);
                        if (item->handler) {
                            union handlerdata hd;
                            memset(&hd, 0, sizeof(hd));
                            hd.checkbox.value = checked ? 1u : 0u;
                            item->handler(MENUOP_SET, item, &hd);
                        }
                    }
                    break;
                }

                case MENUITEMTYPE_DROPDOWN: {
                    /* S195 Batch 4: Dropdowns are used by cheats "Buddies"
                     * picker, soundtrack selection, etc.  Enumerate options
                     * via MENUOP_GETOPTIONCOUNT + MENUOP_GETOPTIONTEXT; read
                     * current index via MENUOP_GETSELECTEDINDEX. */
                    const char *label = getItemLabel(item);
                    if (!label[0]) label = "(option)";

                    s32 optCount    = 0;
                    s32 curIdx      = 0;
                    if (item->handler) {
                        union handlerdata hd;
                        memset(&hd, 0, sizeof(hd));
                        item->handler(MENUOP_GETOPTIONCOUNT, item, &hd);
                        optCount = (s32)hd.dropdown.value;

                        memset(&hd, 0, sizeof(hd));
                        item->handler(MENUOP_GETSELECTEDINDEX, item, &hd);
                        curIdx = (s32)hd.dropdown.value;
                    }
                    if (optCount <= 0) optCount = 1;
                    if (curIdx < 0) curIdx = 0;
                    if (curIdx >= optCount) curIdx = optCount - 1;

                    /* Get current option text for the combo preview. */
                    char curText[64] = "";
                    if (item->handler) {
                        union handlerdata hd;
                        memset(&hd, 0, sizeof(hd));
                        hd.dropdown.value = (uintptr_t)curIdx;
                        uintptr_t r = item->handler(MENUOP_GETOPTIONTEXT, item, &hd);
                        const char *s = (const char *)r;
                        if (!s || !s[0]) s = (const char *)hd.dropdown.unk04;
                        if (s && s[0]) {
                            snprintf(curText, sizeof(curText), "%s", s);
                        }
                    }

                    ImGui::PushID((const void *)item);
                    ImGui::TextUnformatted(label);
                    float availW = ImGui::GetContentRegionAvail().x;
                    ImGui::SetNextItemWidth(availW);
                    if (ImGui::BeginCombo("##dropdown", curText)) {
                        for (s32 i = 0; i < optCount; i++) {
                            char itemText[64] = "";
                            if (item->handler) {
                                union handlerdata hd;
                                memset(&hd, 0, sizeof(hd));
                                hd.dropdown.value = (uintptr_t)i;
                                uintptr_t r = item->handler(MENUOP_GETOPTIONTEXT, item, &hd);
                                const char *s = (const char *)r;
                                if (!s || !s[0]) s = (const char *)hd.dropdown.unk04;
                                if (s && s[0]) {
                                    snprintf(itemText, sizeof(itemText), "%s", s);
                                } else {
                                    snprintf(itemText, sizeof(itemText), "Option %d", i);
                                }
                            } else {
                                snprintf(itemText, sizeof(itemText), "Option %d", i);
                            }

                            bool isSel = (i == curIdx);
                            if (ImGui::Selectable(itemText, isSel)) {
                                if (i != curIdx && item->handler) {
                                    union handlerdata hd;
                                    memset(&hd, 0, sizeof(hd));
                                    hd.dropdown.value = (uintptr_t)i;
                                    item->handler(MENUOP_SET, item, &hd);
                                    pdguiPlaySound(PDGUI_SND_SUBFOCUS);
                                }
                            }
                            if (isSel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                    break;
                }

                case MENUITEMTYPE_MARQUEE: {
                    /* Scrolling text banner (e.g., "Cheat Available").  ImGui
                     * has no native marquee; render the text as a dimmed
                     * centered label so the user sees it without the scroll
                     * animation.  Callback labels are the common case here. */
                    const char *label = getItemLabel(item);
                    if (label[0]) {
                        float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
                        ImVec2 ts = ImGui::CalcTextSize(label);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                              (availW - ts.x) * 0.5f);
                        ImGui::TextDisabled("%s", label);
                    }
                    break;
                }

                default:
                    /* Skip unhandled item types (lists, carousels, models).
                     * These are complex widgets; show a placeholder. */
                    {
                        const char *label = getItemLabel(item);
                        if (label[0]) {
                            ImGui::TextDisabled("[%s]", label);
                        }
                    }
                    break;
            }
        }

        /* Fallback OK if no selectables */
        if (!hasAnySelectable) {
            ImGui::Spacing();
            float buttonW = 100.0f * scale;
            float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - buttonW) * 0.5f);
            if (ImGui::Button("OK", ImVec2(buttonW, 28.0f * scale))) {
                pdguiPlaySound(PDGUI_SND_SELECT);
                menuPopDialog();
            }
            ImGui::SetItemDefaultFocus();
        }
    }

    /* B / Escape = dismiss */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        s_KbdInitialised = false;
        s_KbdDialogDef = nullptr;
        menuPopDialog();
    }

    ImGui::End();
    pdguiSetPalette(prevPalette);
    return 1;
}

/* ========================================================================
 * Type-Specific Wrappers
 * ======================================================================== */

static s32 renderDangerDialog(struct menudialog *dialog,
                               struct menu *menu,
                               s32 winW, s32 winH)
{
    return renderTypedDialog(dialog, menu, winW, winH,
                              2,                        /* Red palette */
                              PDGUI_SND_ERROR,          /* Error sound */
                              IM_COL32(255, 255, 0, 255), /* Yellow title */
                              "Warning");
}

static s32 renderSuccessDialog(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH)
{
    return renderTypedDialog(dialog, menu, winW, winH,
                              3,                          /* Green palette */
                              PDGUI_SND_SUCCESS,          /* Success chime */
                              IM_COL32(255, 255, 0, 255), /* Yellow title */
                              "Complete");
}

static s32 renderDefaultDialog(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH)
{
    return renderTypedDialog(dialog, menu, winW, winH,
                              1,                          /* Blue palette (default) */
                              -1,                         /* No special sound */
                              IM_COL32(100, 200, 255, 255), /* Light-blue title */
                              "");
}

/* ========================================================================
 * MP End Game — custom modal confirmation (B-End-Game-UX)
 *
 * Replaces the generic renderDangerDialog path for g_MpEndGameMenuDialog
 * with a first-class modal popup:
 *   - Centered "End Match?" panel with large "Are you sure?" body copy
 *   - Single Confirm / Cancel pair with visible keybinding hints
 *   - Keyboard:  Enter / Space → Confirm, Esc → Cancel
 *   - Gamepad:   A (FaceDown)  → Confirm, B (FaceRight) → Cancel
 *
 * The confirm path invokes the legacy menuhandlerMpEndGame via the dialog
 * item's handler pointer (same MENUOP_SET contract renderTypedDialog uses),
 * which on a client runs netDisconnect() — that in turn now clears
 * g_ClientManifest before mainChangeToStage() (B-End-Game-Crash fix in
 * port/src/net/net.c).
 * ======================================================================== */

static s32 renderMpEndGameDialog(struct menudialog *dialog,
                                  struct menu * /*menu*/,
                                  s32 /*winW*/, s32 /*winH*/)
{
    struct menudialogdef *def = *(struct menudialogdef **)((u8 *)dialog);
    if (!def) return 0;

    /* Full-viewport scrim — focus the player on the dialog, not the match
     * scene below.  Same primitive renderTypedDialog uses. */
    pdguiPopupDarkenBehind(0.60f);

    /* Red/warning palette (matches the legacy DANGER dialog type). */
    s32 prevPalette = pdguiGetPalette();
    pdguiSetPalette(2);

    float scale = pdguiScaleFactor();
    float dialogW = pdguiScale(540.0f);
    float dialogH = pdguiScale(260.0f);
    ImVec2 dlgPos = pdguiCenterPos(dialogW, dialogH);
    float dialogX = dlgPos.x;
    float dialogY = dlgPos.y;

    float pdTitleH = pdguiScale(36.0f);
    if (pdTitleH < 18.0f) pdTitleH = 18.0f;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoScrollbar;

    char winId[64];
    snprintf(winId, sizeof(winId), "##mp_endgame_dialog_%p", (void *)dialog);

    if (!ImGui::Begin(winId, nullptr, wflags)) {
        ImGui::End();
        pdguiSetPalette(prevPalette);
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        pdguiPlaySound(PDGUI_SND_ERROR);
    }

    /* Opaque body behind the frame — the hotswap system may render this
     * over a still-live pause menu, so the body must not be see-through. */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          pdguiPalImU32(PDPAL_BODYBG, 255), 0.0f);
    }

    /* PD-authentic frame + title text. */
    const char *title = "End Match?";
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, title, 1);
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dialogX + (dialogW - titleSize.x) * 0.5f,
                           dialogY + (pdTitleH - titleSize.y) * 0.5f),
                    IM_COL32(255, 255, 0, 255), title);
    }

    /* ---- Body ---- */
    float bodyTop = pdTitleH + ImGui::GetStyle().WindowPadding.y + 8.0f * scale;
    ImGui::SetCursorPosY(bodyTop);

    const char *bodyMsg = "Are you sure you want to end the match?";
    {
        float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
        ImVec2 ts = ImGui::CalcTextSize(bodyMsg, nullptr, false, availW);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - ts.x) * 0.5f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availW);
        ImGui::TextWrapped("%s", bodyMsg);
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* ---- Buttons ---- */
    float btnW = pdguiScale(160.0f);
    float btnH = pdguiScale(32.0f);
    float gap  = pdguiScale(16.0f);
    float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    float totalW = btnW * 2.0f + gap;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - totalW) * 0.5f);

    bool doConfirm = false;
    bool doCancel  = false;

    /* Cancel first — safer default focus (Cancel is the non-destructive
     * choice).  Keyboard/gamepad bindings below still route activations
     * directly without needing focus. */
    if (ImGui::Button("Cancel##mpendgame", ImVec2(btnW, btnH))) {
        doCancel = true;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetItemDefaultFocus();
    }

    ImGui::SameLine(0.0f, gap);

    /* Red "End Match" confirm.  Styled with the danger palette so it reads
     * as destructive even to a colour-blind player. */
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.20f, 0.20f, 1.0f));
    if (ImGui::Button("End Match##mpendgame", ImVec2(btnW, btnH))) {
        doConfirm = true;
    }
    ImGui::PopStyleColor(3);

    /* ---- Keybinding hints (always visible at the bottom) ---- */
    {
        const char *hintL = "[Enter/Space/(A)] Confirm";
        const char *hintR = "[Esc/(B)] Cancel";

        /* Reserve a row at the bottom of the dialog for hint text.  Absolute
         * Y position so it sits right above the frame's bottom edge even if
         * the body wraps. */
        float hintY = dialogH - pdguiScale(22.0f);
        if (hintY < ImGui::GetCursorPosY() + 4.0f * scale) {
            hintY = ImGui::GetCursorPosY() + 4.0f * scale;
        }
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, hintY));
        ImGui::TextDisabled("%s", hintL);

        ImVec2 rSize = ImGui::CalcTextSize(hintR);
        ImGui::SetCursorPos(ImVec2(dialogW - ImGui::GetStyle().WindowPadding.x - rSize.x,
                                   hintY));
        ImGui::TextDisabled("%s", hintR);
    }

    /* ---- Global keyboard + gamepad bindings ---- */
    if (!ImGui::IsWindowAppearing()) {
        /* ACTION_ACCEPT equivalents (per pdguiDriveImGuiNav convention) */
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
            doConfirm = true;
        }

        /* ACTION_CANCEL_USE equivalents */
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
            doCancel = true;
        }
    }

    if (doConfirm) {
        pdguiPlaySound(PDGUI_SND_SELECT);

        /* Invoke the legacy SELECTABLE that carries the real End Game
         * handler (menuhandlerMpEndGame in src/game/mplayer/ingame.c).
         * On a client this calls netDisconnect() → mainChangeToStage()
         * with a cleared manifest (B-End-Game-Crash root-cause fix).
         * On the server it calls mainEndStage() → SVC_STAGE_END. */
        if (def->items) {
            for (struct menuitem *it = def->items;
                 it->type != MENUITEMTYPE_END;
                 it++) {
                if (it->type == MENUITEMTYPE_SELECTABLE && it->handler) {
                    union handlerdata hd;
                    memset(&hd, 0, sizeof(hd));
                    it->handler(MENUOP_SET, it, &hd);
                    break;  /* exactly one confirm handler per dialog */
                }
            }
        }
        menuPopDialog();
    } else if (doCancel) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
    }

    ImGui::End();
    pdguiSetPalette(prevPalette);
    return 1;
}

/* ========================================================================
 * Noop render — suppresses a dialog without drawing anything
 * ======================================================================== */

static s32 renderNoop(struct menudialog * /*dialog*/, struct menu * /*menu*/,
                      s32 /*winW*/, s32 /*winH*/) { return 1; }

/* ========================================================================
 * Batch 1: PC-irrelevant filemgr placeholder
 *
 * The legacy filemgr dialogs (g_FilemgrDeleteMenuDialog,
 * g_FilemgrCopyMenuDialog, g_FilemgrOperationsMenuDialog,
 * g_FilemgrSelectLocationMenuDialog) were built around the N64 controller
 * pak save-file picker.  On PC the agent save/load flow is handled by
 * pdgui_menu_agentcreate / pdgui_menu_agentselect plus saveSaveAgent()
 * writing JSON to disk — the pak-file picker is never reached in the
 * normal flow.  They can still be pushed by fringe code paths, though, so
 * we register a clean "not applicable on PC" placeholder rather than let
 * them fall through to the generic DEFAULT renderer which would mis-render
 * their LIST items.
 * ======================================================================== */

static s32 renderFilemgrPcPlaceholder(struct menudialog *dialog,
                                       struct menu *menu,
                                       s32 winW, s32 winH)
{
    (void)menu; (void)winW; (void)winH;

    struct menudialogdef *def = *(struct menudialogdef **)((u8 *)dialog);
    if (!def) return 0;

    pdguiPopupDarkenBehind(0.55f);

    s32 prevPalette = pdguiGetPalette();
    pdguiSetPalette(1); /* default blue */

    float dialogW = pdguiScale(600.0f);
    float dialogH = pdguiScale(240.0f);
    ImVec2 dlgPos = pdguiCenterPos(dialogW, dialogH);
    ImGui::SetNextWindowPos(dlgPos);
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoScrollbar;

    char winId[64];
    snprintf(winId, sizeof(winId), "##filemgr_pc_%p", (void *)dialog);

    if (!ImGui::Begin(winId, nullptr, wflags)) {
        ImGui::End();
        pdguiSetPalette(prevPalette);
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    /* Opaque body */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(dlgPos,
                          ImVec2(dlgPos.x + dialogW, dlgPos.y + dialogH),
                          pdguiPalImU32(PDPAL_BODYBG, 255), 0.0f);
    }

    const char *title = getDialogTitle(def, "File Manager");
    pdguiDrawPdDialog(dlgPos.x, dlgPos.y, dialogW, dialogH, title, 1);

    float pdTitleH = pdguiScale(36.0f);
    if (pdTitleH < 18.0f) pdTitleH = 18.0f;
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dlgPos.x + 8.0f, dlgPos.y + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dlgPos.x + (dialogW - ts.x) * 0.5f,
                           dlgPos.y + (pdTitleH - ts.y) * 0.5f),
                    IM_COL32(100, 200, 255, 255), title);
    }

    pdguiSetCursorBelowTitle(pdTitleH);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pdguiScale(6.0f));

    /* Body — PC messaging */
    float availW = ImGui::GetContentRegionAvail().x;
    float bodyH  = pdguiBodyHeightForActionBar(ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginChild("##fm_body", ImVec2(0, bodyH), false,
                           ImGuiWindowFlags_NoScrollbar)) {
        const char *msg =
            "This is a Nintendo 64 controller pak dialog.\n\n"
            "On PC, agents are created, selected and deleted from the Main\n"
            "Menu via Agent Select.  Saved profiles live in\n"
            "  saves/agent_<name>.json\n"
            "and are managed directly by Agent Select.";
        ImVec2 ts = ImGui::CalcTextSize(msg, nullptr, false, availW);
        float yPad = (bodyH - ts.y) * 0.5f;
        if (yPad < 0.0f) yPad = 0.0f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yPad);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availW);
        ImGui::TextWrapped("%s", msg);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    /* Docked action bar: single OK button */
    if (pdguiBeginActionBar("##fm_action_bar")) {
        float barW = ImGui::GetContentRegionAvail().x;
        if (pdguiActionBarButton("OK", 1, barW)) {
            menuPopDialog();
        }
    }
    pdguiEndActionBar();

    /* B / Escape = dismiss */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
    }

    ImGui::End();
    pdguiSetPalette(prevPalette);
    return 1;
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

/* Previously-native dialogs — now all handled by ImGui type fallbacks.
 * P10 D5.7: Zero legacy menus remain. All dialogs (including keyboard
 * input and status dialogs) are rendered by ImGui type-based renderers.
 * The DEFAULT type fallback handles labels, selectables, separators, and
 * keyboard items (via ImGui::InputText). */
extern struct menudialogdef g_MpEndscreenConfirmNameMenuDialog;

void pdguiMenuWarningRegister(void)
{
    if (s_Registered) return;

    /* Type-based fallback renderers — cover ALL dialog types generically. */
    pdguiHotswapRegisterType(MENUDIALOGTYPE_DEFAULT,
                              renderDefaultDialog,
                              "Default Dialog");

    pdguiHotswapRegisterType(MENUDIALOGTYPE_DANGER,
                              renderDangerDialog,
                              "Danger Dialog");

    pdguiHotswapRegisterType(MENUDIALOGTYPE_SUCCESS,
                              renderSuccessDialog,
                              "Success Dialog");

    /* Also cover type 0 (some dialogs have type=0) */
    pdguiHotswapRegisterType(0,
                              renderDefaultDialog,
                              "Default Dialog (type 0)");

    /* B-115 fix: suppress Confirm Name — redundant on PC (auto-save handles it) */
    pdguiHotswapRegister(&g_MpEndscreenConfirmNameMenuDialog, renderNoop, "Confirm Name (suppressed)");

    /* B-128 fix: suppress N64 filemgr error dialogs.
     * All three have MENUDIALOGTYPE_DANGER + fn-ptr titles (filemgrMenuTextErrorTitle).
     * Without individual registrations they fall through to renderDangerDialog →
     * getDialogTitle → langSafe((s32)fn_ptr) → langGet OOB → AV.
     * On PC saves go through saveSaveAgent(); these dialogs should never appear. */
    extern struct menudialogdef g_PakNotOriginalMenuDialog;
    extern struct menudialogdef g_FilemgrSaveErrorMenuDialog;
    extern struct menudialogdef g_FilemgrFileLostMenuDialog;
    pdguiHotswapRegister(&g_PakNotOriginalMenuDialog,   renderNoop, "Pak Not Original (suppressed)");
    pdguiHotswapRegister(&g_FilemgrSaveErrorMenuDialog, renderNoop, "Filemgr Save Error (suppressed)");
    pdguiHotswapRegister(&g_FilemgrFileLostMenuDialog,  renderNoop, "Filemgr File Lost (suppressed)");

    /* S193 Batch 1: explicit registrations for the seven "low-hanging fruit"
     * dialogs in menu-replacement-plan.md.  These already hit the
     * DANGER/DEFAULT type fallback, but an explicit registration makes the
     * hotswap log clear about who owns each one and lets us swap in a
     * dedicated renderer without churning the fallback.
     *
     * ExitGame / MpEndGame — pure confirmations, DANGER fallback handles
     *   them (scrim + literal-text label fix from S193).
     * PdModeSettings — adds slider support (MENUITEMTYPE_SLIDER case) to
     *   the DEFAULT fallback; the three PD-mode sliders now render live.
     * Filemgr{Delete,Copy,Operations,SelectLocation} — the N64 controller
     *   pak file pickers are unreachable in the normal PC flow.  If they
     *   ever get pushed by fringe code, show a clean placeholder that
     *   points the player at Agent Select instead of letting the LIST
     *   items fall through to the "[label]" placeholder in the default
     *   fallback. */
    extern struct menudialogdef g_ExitGameMenuDialog;
    extern struct menudialogdef g_PdModeSettingsMenuDialog;
    extern struct menudialogdef g_MpEndGameMenuDialog;
    extern struct menudialogdef g_FilemgrDeleteMenuDialog;
    extern struct menudialogdef g_FilemgrCopyMenuDialog;
    extern struct menudialogdef g_FilemgrOperationsMenuDialog;
    extern struct menudialogdef g_FilemgrSelectLocationMenuDialog;

    pdguiHotswapRegister(&g_ExitGameMenuDialog,
                          renderDangerDialog,
                          "Exit Game (Batch 1)");
    pdguiHotswapRegister(&g_PdModeSettingsMenuDialog,
                          renderDefaultDialog,
                          "PD Mode Settings (Batch 1)");
    /* B-End-Game-UX (2026-04-13): First-class modal confirmation with a
     * visible keybinding hint row, keyboard Enter/Space + gamepad A for
     * Confirm, and Esc + gamepad B for Cancel.  Replaces the split-button
     * pattern the generic renderDangerDialog produced for this dialog (which
     * mouse-worked but did not accept controller Accept).  The Confirm path
     * invokes menuhandlerMpEndGame → netDisconnect() → the cleared-manifest
     * stage change (B-End-Game-Crash). */
    pdguiHotswapRegister(&g_MpEndGameMenuDialog,
                          renderMpEndGameDialog,
                          "MP End Game (modal confirm)");
    pdguiHotswapRegister(&g_FilemgrDeleteMenuDialog,
                          renderFilemgrPcPlaceholder,
                          "Filemgr Delete (PC placeholder, Batch 1)");
    pdguiHotswapRegister(&g_FilemgrCopyMenuDialog,
                          renderFilemgrPcPlaceholder,
                          "Filemgr Copy (PC placeholder, Batch 1)");
    pdguiHotswapRegister(&g_FilemgrOperationsMenuDialog,
                          renderFilemgrPcPlaceholder,
                          "Filemgr Operations (PC placeholder, Batch 1)");
    pdguiHotswapRegister(&g_FilemgrSelectLocationMenuDialog,
                          renderFilemgrPcPlaceholder,
                          "Filemgr Select Location (PC placeholder, Batch 1)");

    /* S195 Batch 4 — Cheats dialogs.
     *
     * Cheats are MENUDIALOGTYPE_DEFAULT and would fall through to the
     * DEFAULT type-based renderer anyway, but explicit registration makes
     * the hotswap log show named ownership instead of "(type fallback)".
     * Every item is one of:
     *   - MENUITEMTYPE_CHECKBOX with cheatCheckboxMenuHandler and a dynamic
     *     label callback (cheatGetNameIfUnlocked) in param2.  Our
     *     getItemLabel() calls the callback via the menuResolveText()
     *     contract (< 0x5a00 = langID, >= 0x5a00 = callback).
     *   - MENUITEMTYPE_SELECTABLE for "Turn off all Cheats",
     *     "Unlock Everything", "Done".
     *   - MENUITEMTYPE_MARQUEE for the scrolling "Cheat Available" banner;
     *     rendered as dimmed centered text (no native ImGui marquee).
     *   - MENUITEMTYPE_SEPARATOR between groups.
     *
     * The Batch 4 extension to renderTypedDialog handles every one of
     * these without per-cheat code.  The Cheats root uses SELECTABLE-
     * OPENSDIALOG to navigate into each category; legacy menu plumbing
     * (menuhandlerOpenDialog via menuPushDialog) still drives the push on
     * activation — our renderer only needs to activate the button. */
    extern struct menudialogdef g_CheatsMenuDialog;
    extern struct menudialogdef g_CheatsFunMenuDialog;
    extern struct menudialogdef g_CheatsGameplayMenuDialog;
    extern struct menudialogdef g_CheatsSoloWeaponsMenuDialog;
    extern struct menudialogdef g_CheatsClassicWeaponsMenuDialog;
    extern struct menudialogdef g_CheatsWeaponsMenuDialog;
    extern struct menudialogdef g_CheatsBuddiesMenuDialog;
    extern struct menudialogdef g_CheatsWarningMenuDialog;
    extern struct menudialogdef g_CheatsConfirmUnlockMenuDialog;

    pdguiHotswapRegister(&g_CheatsMenuDialog,
                          renderDefaultDialog,
                          "Cheats Root (Batch 4)");
    pdguiHotswapRegister(&g_CheatsFunMenuDialog,
                          renderDefaultDialog,
                          "Cheats Fun (Batch 4)");
    pdguiHotswapRegister(&g_CheatsGameplayMenuDialog,
                          renderDefaultDialog,
                          "Cheats Gameplay (Batch 4)");
    pdguiHotswapRegister(&g_CheatsSoloWeaponsMenuDialog,
                          renderDefaultDialog,
                          "Cheats Solo Weapons (Batch 4)");
    pdguiHotswapRegister(&g_CheatsClassicWeaponsMenuDialog,
                          renderDefaultDialog,
                          "Cheats Classic Weapons (Batch 4)");
    pdguiHotswapRegister(&g_CheatsWeaponsMenuDialog,
                          renderDefaultDialog,
                          "Cheats Weapons (Batch 4)");
    pdguiHotswapRegister(&g_CheatsBuddiesMenuDialog,
                          renderDefaultDialog,
                          "Cheats Buddies (Batch 4)");
    pdguiHotswapRegister(&g_CheatsWarningMenuDialog,
                          renderDangerDialog,
                          "Cheats Warning (Batch 4)");
    pdguiHotswapRegister(&g_CheatsConfirmUnlockMenuDialog,
                          renderDangerDialog,
                          "Cheats Confirm Unlock (Batch 4)");

    /* S195 Batch 8 — MP Pause & In-Game dialogs.
     *
     * These are pushed from the Multiplayer pause menu during a match.
     * All six are MENUDIALOGTYPE_DEFAULT.  Item composition:
     *
     *   MpPauseControl         — LABEL (dyn) + SEPARATOR + LABEL (dyn) +
     *                             SELECTABLE + DROPDOWN + SELECTABLE x2.
     *                             All item types covered by B4 extensions.
     *   MpPlayerOptions        — CHECKBOX + DROPDOWN + SELECTABLE.  Covered.
     *   MpPauseInventory       — LIST (weapons) + MARQUEE (description).
     *                             LIST shows as [placeholder] — DEFERRED for
     *                             a future batch that adds LIST support.
     *   MpPausePlayerStats     — PLAYERSTATS type — DEFERRED.
     *   MpPausePlayerRanking   — RANKING type — DEFERRED.
     *   MpPauseTeamRankings    — RANKING type — DEFERRED.
     *
     * All six are registered via renderDefaultDialog so the PD-authentic
     * frame + scrim + controller nav work, and the items that our
     * extended typed-dialog primitive supports (LABEL / SEPARATOR /
     * SELECTABLE / DROPDOWN / CHECKBOX / MARQUEE) render natively.  The
     * special types (LIST / PLAYERSTATS / RANKING) fall to the default
     * case which shows `[label]` — a visible DEFERRED marker the player
     * can see and the developer can track. */
    extern struct menudialogdef g_MpPauseControlMenuDialog;
    extern struct menudialogdef g_MpPauseInventoryMenuDialog;
    extern struct menudialogdef g_MpPausePlayerStatsMenuDialog;
    extern struct menudialogdef g_MpPausePlayerRankingMenuDialog;
    extern struct menudialogdef g_MpPauseTeamRankingsMenuDialog;
    extern struct menudialogdef g_MpPlayerOptionsMenuDialog;

    pdguiHotswapRegister(&g_MpPauseControlMenuDialog,
                          renderDefaultDialog,
                          "MP Pause Control (Batch 8)");
    pdguiHotswapRegister(&g_MpPlayerOptionsMenuDialog,
                          renderDefaultDialog,
                          "MP Player Options (Batch 8)");
    pdguiHotswapRegister(&g_MpPauseInventoryMenuDialog,
                          renderDefaultDialog,
                          "MP Pause Inventory (Batch 8 partial - LIST deferred)");
    pdguiHotswapRegister(&g_MpPausePlayerStatsMenuDialog,
                          renderDefaultDialog,
                          "MP Pause Player Stats (Batch 8 partial - PLAYERSTATS deferred)");
    pdguiHotswapRegister(&g_MpPausePlayerRankingMenuDialog,
                          renderDefaultDialog,
                          "MP Pause Player Ranking (Batch 8 partial - RANKING deferred)");
    pdguiHotswapRegister(&g_MpPauseTeamRankingsMenuDialog,
                          renderDefaultDialog,
                          "MP Pause Team Rankings (Batch 8 partial - RANKING deferred)");

    /* S195 Batch 11 — MP Player Config & Stats.
     *
     * g_MpCharacterMenuDialog: LIST of characters (bodies/heads).  DEFERRED
     *   behind LIST primitive.  Character selection already exists in the
     *   modern room.cpp flow via pdgui_menu_agentcreate; the legacy screen
     *   is only reached via the legacy MP setup tree.
     * g_MpPlayerStatsMenuDialog: PLAYERSTATS type — same DEFERRED as pause.
     * g_MpLoadSettings / LoadPreset / LoadPlayer: LIST of saved items —
     *   DEFERRED behind LIST primitive.
     *
     * Registering with renderDefaultDialog gives the PD frame + scrim and
     * a visible placeholder for the complex items, marking the work item
     * without hiding the dialog. */
    extern struct menudialogdef g_MpCharacterMenuDialog;
    extern struct menudialogdef g_MpPlayerStatsMenuDialog;
    extern struct menudialogdef g_MpLoadSettingsMenuDialog;
    extern struct menudialogdef g_MpLoadPresetMenuDialog;
    extern struct menudialogdef g_MpLoadPlayerMenuDialog;

    pdguiHotswapRegister(&g_MpCharacterMenuDialog,
                          renderDefaultDialog,
                          "MP Character (Batch 11 partial - LIST deferred)");
    pdguiHotswapRegister(&g_MpPlayerStatsMenuDialog,
                          renderDefaultDialog,
                          "MP Player Stats (Batch 11 partial - PLAYERSTATS deferred)");
    pdguiHotswapRegister(&g_MpLoadSettingsMenuDialog,
                          renderDefaultDialog,
                          "MP Load Settings (Batch 11 partial - LIST deferred)");
    pdguiHotswapRegister(&g_MpLoadPresetMenuDialog,
                          renderDefaultDialog,
                          "MP Load Preset (Batch 11 partial - LIST deferred)");
    pdguiHotswapRegister(&g_MpLoadPlayerMenuDialog,
                          renderDefaultDialog,
                          "MP Load Player (Batch 11 partial - LIST deferred)");

    /* S209 Batch 12 — Music & Misc: the 4 Batch 12 dialogs
     * (g_MpSelectTunesMenuDialog, g_MpSoundtrackMenuDialog,
     * g_MpTeamNamesMenuDialog, g_MpChallengesMenuDialog) are now fully
     * implemented in pdgui_menu_mpsettings.cpp (tunes/soundtrack/teamnames)
     * and pdgui_menu_challenges.cpp (challenges root).  The previous
     * renderDefaultDialog placeholders here were removed since the real
     * renderers register later in pdguiMenusRegisterAll() and would
     * silently override them anyway — keeping the placeholders just
     * spammed stale "Batch 12 partial" strings into the init log. */

    s_Registered = true;
    sysLogPrintf(LOG_NOTE, "pdgui_menu_warning: Registered DEFAULT + DANGER + SUCCESS fallbacks + S193 Batch 1 + S195 Batches 4/8/11 explicit dialogs");
}

} /* extern "C" */
