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
#include "system.h"

/* ========================================================================
 * Forward declarations for game symbols
 * ======================================================================== */

extern "C" {

/* Menu item types */
#define MENUITEMTYPE_LABEL       0x01
#define MENUITEMTYPE_LIST        0x02
#define MENUITEMTYPE_SELECTABLE  0x04
#define MENUITEMTYPE_SEPARATOR   0x0b
#define MENUITEMTYPE_DROPDOWN    0x0c
#define MENUITEMTYPE_END         0x1a

/* Menu operations */
#define MENUOP_SET 6

/* Dialog types */
#define MENUDIALOGTYPE_DANGER  2
#define MENUDIALOGTYPE_SUCCESS 3

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
 * Over-allocate so item handlers don't corrupt the stack. */
union handlerdata {
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
char *langGet(s32 textid);

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
        char *s = langGet((s32)def->title);
        if (s && s[0]) return s;
    }

    return fallback;
}

static const char *getItemLabel(struct menuitem *item)
{
    if (!item) return "";

    if (item->param2 != 0) {
        char *s = langGet((s32)item->param2);
        if (s && s[0]) return s;
    }

    return "";
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

    /* Switch palette */
    s32 prevPalette = pdguiGetPalette();
    pdguiSetPalette(paletteIdx);

    /* ---- Layout ---- */
    float scale = pdguiScaleFactor();
    float dialogW = pdguiScale(380.0f);
    float dialogH = pdguiScale(220.0f);
    ImVec2 dlgPos = pdguiCenterPos(dialogW, dialogH);
    float dialogX = dlgPos.x;
    float dialogY = dlgPos.y;

    float pdTitleH = pdguiScale(24.0f);
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
                          IM_COL32(8, 8, 16, 255), 0.0f);
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
    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y + 4.0f * scale);

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

                default:
                    /* Skip unhandled item types (lists, dropdowns, etc.)
                     * These are complex widgets; for now, show a placeholder. */
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

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

/* Dialogs that must use PD native rendering despite their type.
 * These use special handlers (keyboard input, custom rendering)
 * that our generic ImGui type renderers can't handle. Registered
 * with NULL renderFn so pdguiHotswapCheck forces PD native. */
/* Network dialogs with special handlers */
extern struct menudialogdef g_NetJoinAddressDialog;
extern struct menudialogdef g_NetJoiningDialog;
extern struct menudialogdef g_NetCoopHostMenuDialog;
/* File manager keyboard input dialogs */
extern struct menudialogdef g_FilemgrRenameMenuDialog;
extern struct menudialogdef g_FilemgrDuplicateNameMenuDialog;
extern struct menudialogdef g_FilemgrFileSavedMenuDialog;
/* MP keyboard/special dialogs */
extern struct menudialogdef g_MpPlayerNameMenuDialog;
extern struct menudialogdef g_MpSaveSetupNameMenuDialog;
extern struct menudialogdef g_MpEndscreenConfirmNameMenuDialog;
extern struct menudialogdef g_MpChangeTeamNameMenuDialog;
extern struct menudialogdef g_MpReadyMenuDialog;
/* mpsetups dialogs (port-added, were static, now exposed for registration) */
extern struct menudialogdef g_StatusOkDialog;
extern struct menudialogdef g_StatusErrorDialog;
extern struct menudialogdef g_DeleteSetupDialog;

void pdguiMenuWarningRegister(void)
{
    if (s_Registered) return;

    pdguiHotswapRegisterType(MENUDIALOGTYPE_DANGER,
                              renderDangerDialog,
                              "Danger Dialog");

    pdguiHotswapRegisterType(MENUDIALOGTYPE_SUCCESS,
                              renderSuccessDialog,
                              "Success Dialog");

    /* Force PD native for dialogs with special handlers (keyboard input,
     * custom status rendering, network state monitoring, etc.).
     * NULL renderFn = "always use PD native, skip type fallback". */
    pdguiHotswapRegister(&g_NetJoinAddressDialog, NULL, "Join Address (native)");
    pdguiHotswapRegister(&g_NetJoiningDialog, NULL, "Joining Game (native)");
    pdguiHotswapRegister(&g_NetCoopHostMenuDialog, NULL, "Co-op Host (native)");
    pdguiHotswapRegister(&g_FilemgrRenameMenuDialog, NULL, "Rename File (native)");
    pdguiHotswapRegister(&g_FilemgrDuplicateNameMenuDialog, NULL, "Duplicate Name (native)");
    pdguiHotswapRegister(&g_FilemgrFileSavedMenuDialog, NULL, "File Saved (native)");
    pdguiHotswapRegister(&g_MpPlayerNameMenuDialog, NULL, "Player Name (native)");
    pdguiHotswapRegister(&g_MpSaveSetupNameMenuDialog, NULL, "Save Setup Name (native)");
    pdguiHotswapRegister(&g_MpEndscreenConfirmNameMenuDialog, NULL, "Confirm Name (native)");
    pdguiHotswapRegister(&g_MpChangeTeamNameMenuDialog, NULL, "Team Name (native)");
    pdguiHotswapRegister(&g_MpReadyMenuDialog, NULL, "MP Ready (native)");
    /* mpsetups dialogs (port-added) */
    pdguiHotswapRegister(&g_StatusOkDialog, NULL, "Setup OK (native)");
    pdguiHotswapRegister(&g_StatusErrorDialog, NULL, "Setup Error (native)");
    pdguiHotswapRegister(&g_DeleteSetupDialog, NULL, "Delete Setup (native)");

    s_Registered = true;
    sysLogPrintf(LOG_NOTE, "pdgui_menu_warning: Registered DANGER + SUCCESS type fallbacks");
}

} /* extern "C" */
