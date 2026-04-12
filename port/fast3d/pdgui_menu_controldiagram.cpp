/**
 * pdgui_menu_controldiagram.cpp -- D5 P3 Batch 10.
 *
 * Host for the two control-scheme configuration dialogs that the training
 * batch plan calls out as "controller diagram, no 3D needed":
 *
 *   g_SoloMissionControlStyleMenuDialog  -- 9 control styles (1.1-1.4, 2.1-
 *                                            2.4, Ext) + per-mode layout info
 *                                            that replaces the legacy
 *                                            MENUITEMTYPE_CONTROLLER GBI
 *                                            texture render.
 *   g_MpControlMenuDialog                -- 10 checkbox rows + Aim Control
 *                                            dropdown (Hold / Toggle) + the
 *                                            same 9-mode Control Style
 *                                            sub-push.
 *
 * Zero function loss: every state mutation routes through pdgui_bridge.c
 * accessors (pdguiCdGet/SetControlMode, pdguiCdGet/SetPlayerOption,
 * pdguiCdGet/SetAimControl), which in turn mirror the legacy
 * menuhandlerControlStyleImpl / menuhandlerMpControlCheckbox /
 * menuhandlerMpAimControl SET paths exactly -- including the
 * OPTION_FORWARDPITCH inversion quirk where the stored bit is
 * "Reverse Pitch OFF" but displayed as "Reverse Pitch ON/OFF".
 *
 * Network wiring: the CONTROLMODE_* and OPTION_* fields are stored in
 * g_PlayerConfigsArray[].options / g_PlayerExtCfg[].extcontrols, all of
 * which are per-client local settings that never cross the wire.  See
 * context/scratch/D5-P3-batch10-2026-04-11.md for the audit.
 *
 * IMPORTANT: C++ file -- must NOT include types.h (#define bool s32 breaks
 * C++).  Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
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

/* =========================================================================
 * Forward declarations (C boundary)
 * ========================================================================= */

extern "C" {

struct menuitem;
struct menudialog;
struct menudialogdef;
struct menu;

/* Dialog defs */
extern struct menudialogdef g_SoloMissionControlStyleMenuDialog;
extern struct menudialogdef g_MpControlMenuDialog;

/* Menu stack */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);

/* Bridge accessors (pdgui_bridge.c, Batch 10) */
s32  pdguiCdGetControlMode(s32 mpindex);
void pdguiCdSetControlMode(s32 mpindex, s32 mode);
s32  pdguiCdGetPlayerOption(s32 option);
void pdguiCdSetPlayerOption(s32 option, s32 on);
s32  pdguiCdGetAimControl(void);
void pdguiCdSetAimControl(s32 mode);

} /* extern "C" */

/* =========================================================================
 * OPTION_* constants (from src/include/constants.h; Batch 4 gotcha pattern:
 * re-declared locally so C++ file doesn't pull types.h).  MUST match.
 * ========================================================================= */

#define MP_OPTION_FORWARDPITCH     0x0001
#define MP_OPTION_LOOKAHEAD        0x0002
#define MP_OPTION_SIGHTONSCREEN    0x0004
#define MP_OPTION_AUTOAIM          0x0008
#define MP_OPTION_AMMOONSCREEN     0x0020
#define MP_OPTION_SHOWGUNFUNCTION  0x0040
#define MP_OPTION_HEADROLL         0x0080
#define MP_OPTION_ALWAYSSHOWTARGET 0x0200
#define MP_OPTION_SHOWZOOMRANGE    0x0400
#define MP_OPTION_PAINTBALL        0x0800

/* CONTROLMODE_PC value; mirrors constants.h CONTROLMODE_PC */
#define CD_CTRLMODE_PC 8

/* Solo control-style `mpindex` used by the legacy handler for the Solo
 * Mission dialog (menuhandler001024dc calls menuhandlerControlStyleImpl
 * with mpindex=4 for the single-player seat). */
#define CD_SOLO_MPINDEX 4

/* =========================================================================
 * PdButton helper (same pattern as other Batch pdgui_menu_*.cpp files)
 * ========================================================================= */

extern "C" void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

static bool PdButton(const char *label, const ImVec2 &size = ImVec2(0, 0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

/* =========================================================================
 * Window-frame helper (identical shape to pdms_BeginStandardWindow)
 * ========================================================================= */

static bool beginPdWindow(const char *imguiId, const char *title)
{
    float diagW = pdguiMenuWidth();
    float diagH = pdguiMenuHeight();
    ImVec2 pos  = pdguiMenuPos();

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(diagW, diagH));

    if (!ImGui::Begin(imguiId, nullptr, wflags)) {
        return false;
    }

    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + diagW, pos.y + diagH),
                          pdguiPalImU32(PDPAL_BODYBG, 255));
    }

    pdguiDrawPdDialog(pos.x, pos.y, diagW, diagH, title, 1);

    float titleH = pdguiScale(39.0f);
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(pos.x + 8.0f, pos.y + 2.0f, diagW - 16.0f, titleH - 4.0f);
        ImVec2 ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(pos.x + (diagW - ts.x) * 0.5f,
                           pos.y + (titleH - ts.y) * 0.5f),
                    pdguiPalImU32(PDPAL_TITLEFG, 255), title);
        ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);
    }

    return true;
}

static bool backPressed(void)
{
    return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

/* =========================================================================
 * Control-style names and layout descriptions.
 *
 * These mirror the legacy 9-entry g_ControlStyleOptions table + the
 * controller texture diagram.  On the N64 the diagram was a literal
 * texture overlay showing which physical button does what.  On PC we
 * present the equivalent information as a text block so users can read
 * the mapping per mode at a glance.
 *
 * Modes 1.1-1.4 (single controller):
 *   .1 Honey  - Left stick: look, Right stick: unused, C: move
 *   .2 Solitaire - Left stick: look-y+strafe, C: look-x+move
 *   .3 Kissy - Left stick: move, C: look
 *   .4 Goodhead - Left stick: look, C: move forward/back + turn
 *
 * Modes 2.1-2.4: dual-controller variants of the above (PC-dead).
 * Mode Ext: PC mouse+keyboard routing.
 * ========================================================================= */

struct ControlModeInfo {
    const char *name;
    const char *layout;
    s32         isExt;
};

static const ControlModeInfo g_ControlModes[9] = {
    {
        "1.1 Honey",
        "Left stick  : Look\n"
        "C buttons   : Move / Strafe\n"
        "A button    : Jump / Accept\n"
        "B button    : Crouch / Cancel\n"
        "Z trigger   : Fire / Use\n"
        "R trigger   : Aim mode\n"
        "Start       : Pause menu\n",
        0
    },
    {
        "1.2 Solitaire",
        "Left stick  : Look Y + Strafe\n"
        "C buttons   : Look X + Move fwd/back\n"
        "A button    : Jump / Accept\n"
        "B button    : Crouch / Cancel\n"
        "Z trigger   : Fire / Use\n"
        "R trigger   : Aim mode\n"
        "Start       : Pause menu\n",
        0
    },
    {
        "1.3 Kissy",
        "Left stick  : Move\n"
        "C buttons   : Look\n"
        "A button    : Jump / Accept\n"
        "B button    : Crouch / Cancel\n"
        "Z trigger   : Fire / Use\n"
        "R trigger   : Aim mode\n"
        "Start       : Pause menu\n",
        0
    },
    {
        "1.4 Goodhead",
        "Left stick  : Look\n"
        "C up / down : Move forward / back\n"
        "C left/right: Turn\n"
        "A button    : Jump / Accept\n"
        "B button    : Crouch / Cancel\n"
        "Z trigger   : Fire / Use\n"
        "R trigger   : Aim mode\n",
        0
    },
    {
        "2.1 Double Honey",
        "Two-controller variant of 1.1 (dead on PC).\n"
        "Retained for configuration compatibility.",
        0
    },
    {
        "2.2 Double Solitaire",
        "Two-controller variant of 1.2 (dead on PC).\n"
        "Retained for configuration compatibility.",
        0
    },
    {
        "2.3 Double Kissy",
        "Two-controller variant of 1.3 (dead on PC).\n"
        "Retained for configuration compatibility.",
        0
    },
    {
        "2.4 Double Goodhead",
        "Two-controller variant of 1.4 (dead on PC).\n"
        "Retained for configuration compatibility.",
        0
    },
    {
        "Ext (PC)",
        "Mouse + keyboard routing.  Bindings managed through\n"
        "Settings -> Controls / Rebind UI.\n\n"
        "Left stick = WASD by default.  Look = mouse.\n"
        "Jump = Space.  Use = F.  Fire = LMB.\n\n"
        "This is the recommended mode for PC players.",
        1
    },
};

/* =========================================================================
 * Renderer: Solo Mission Control Style (g_SoloMissionControlStyleMenuDialog)
 * ========================================================================= */

static s32 s_SmcCursor = -1;

static s32 renderSoloMissionControlStyle(struct menudialog *dialog,
                                           struct menu *menu,
                                           s32 winW, s32 winH)
{
    if (!beginPdWindow("##cd_smc", "Control")) {
        ImGui::End();
        return 1;
    }

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float padY    = ImGui::GetStyle().WindowPadding.y;
    float childH  = diagH - titleH - footerH - padY * 2.0f;

    /* Resolve currently-active mode.  Legacy menuhandler001024dc uses
     * mpindex = 4 for the single-player seat. */
    s32 cur = pdguiCdGetControlMode(CD_SOLO_MPINDEX);
    if (cur == CD_CTRLMODE_PC) {
        cur = 8;  /* Ext slot in our 9-entry table */
    } else if (cur < 0 || cur > 7) {
        cur = 0;
    }

    if (s_SmcCursor < 0 || s_SmcCursor > 8) {
        s_SmcCursor = cur;
    }

    /* Keyboard / D-pad nav */
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)
        || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true)) {
        if (s_SmcCursor < 8) {
            s_SmcCursor++;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)
        || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true)) {
        if (s_SmcCursor > 0) {
            s_SmcCursor--;
            pdguiPlaySound(PDGUI_SND_SUBFOCUS);
        }
    }

    /* Two-column layout: list on the left, diagram on the right */
    float listW     = pdguiScale(260.0f);
    float diagramW  = diagW - listW - pdguiScale(80.0f);
    if (diagramW < pdguiScale(260.0f)) diagramW = pdguiScale(260.0f);

    ImGui::BeginChild("##smc_list", ImVec2(listW, childH), true);
    {
        for (s32 i = 0; i < 9; i++) {
            ImGui::PushID(i);
            bool sel = (i == s_SmcCursor);
            if (ImGui::Selectable(g_ControlModes[i].name, sel, 0,
                                  ImVec2(0, pdguiScale(24.0f)))) {
                s_SmcCursor = i;
                /* Immediate apply on click so the diagram + legacy state
                 * both update.  Ext (index 8) maps to CONTROLMODE_PC. */
                s32 mode = g_ControlModes[i].isExt ? CD_CTRLMODE_PC : i;
                pdguiCdSetControlMode(CD_SOLO_MPINDEX, mode);
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            if (ImGui::IsItemHovered()) {
                /* Hover preview only — don't commit to legacy state until
                 * the user clicks.  The legacy handler has no LISTITEMFOCUS
                 * branch for this handler, so there's nothing to delegate. */
                s_SmcCursor = i;
            }
            if (sel) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine(0, pdguiScale(20.0f));
    ImGui::BeginChild("##smc_info", ImVec2(diagramW, childH), true);
    {
        if (s_SmcCursor >= 0 && s_SmcCursor < 9) {
            const ControlModeInfo *info = &g_ControlModes[s_SmcCursor];
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%s", info->name);
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(info->layout);
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
        if (PdButton("Back", ImVec2(btnW, btnH))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Renderer: MP Control (g_MpControlMenuDialog)
 *
 * 11 checkbox rows + "Aim Control" dropdown (Hold/Toggle) + nested
 * "Control Style" push to g_MpControlMenuDialog's Control Style sub-
 * dialog (same list as Solo).  The legacy menuhandlerMpControlStyle uses
 * item->param3 to pick the option bit, and
 * menuhandlerMpControlCheckbox likewise; both are wrapped through the
 * bridge accessors so we preserve the OPTION_FORWARDPITCH inversion.
 * ========================================================================= */

struct MpCtrlRow {
    const char *label;
    u32         optionBit;
};

static const MpCtrlRow g_MpCtrlRows[] = {
    { "Reverse Pitch",    MP_OPTION_FORWARDPITCH     },
    { "Look Ahead",       MP_OPTION_LOOKAHEAD        },
    { "Head Roll",        MP_OPTION_HEADROLL         },
    { "Auto-Aim",         MP_OPTION_AUTOAIM          },
    { "Sight on Screen",  MP_OPTION_SIGHTONSCREEN    },
    { "Show Target",      MP_OPTION_ALWAYSSHOWTARGET },
    { "Zoom Range",       MP_OPTION_SHOWZOOMRANGE    },
    { "Ammo on Screen",   MP_OPTION_AMMOONSCREEN     },
    { "Gun Function",     MP_OPTION_SHOWGUNFUNCTION  },
    { "Paintball",        MP_OPTION_PAINTBALL        },
};

static s32 renderMpControl(struct menudialog *dialog,
                             struct menu *menu,
                             s32 winW, s32 winH)
{
    if (!beginPdWindow("##cd_mpctrl", "Control")) {
        ImGui::End();
        return 1;
    }

    if (backPressed()) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    float diagW   = pdguiMenuWidth();
    float diagH   = pdguiMenuHeight();
    float titleH  = pdguiScale(39.0f);
    float footerH = pdguiScale(75.0f);
    float padY    = ImGui::GetStyle().WindowPadding.y;
    float childH  = diagH - titleH - footerH - padY * 2.0f;

    ImGui::BeginChild("##mp_ctrl_body",
                      ImVec2(diagW - pdguiScale(30.0f) * 2, childH),
                      false);

    ImGui::TextDisabled("Tune per-player display and assist options.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Control Style push-row */
    {
        const s32 cur = pdguiCdGetControlMode(0);
        const char *styleLbl;
        char buf[48];
        if (cur == CD_CTRLMODE_PC) {
            styleLbl = "Ext (PC)";
        } else if (cur >= 0 && cur <= 7) {
            const char *tier = (cur < 4) ? "1" : "2";
            snprintf(buf, sizeof(buf), "%s.%d", tier, (cur % 4) + 1);
            styleLbl = buf;
        } else {
            styleLbl = "--";
        }

        if (ImGui::Selectable("Control Style", false, 0,
                              ImVec2(0, pdguiScale(24.0f)))) {
            menuPushDialog(&g_SoloMissionControlStyleMenuDialog);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        ImVec2 rmax = ImGui::GetItemRectMax();
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 ts   = ImGui::CalcTextSize(styleLbl);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(rmax.x - ts.x - pdguiScale(12.0f),
                           rmin.y + (rmax.y - rmin.y - ts.y) * 0.5f),
                    pdguiPalImU32(PDPAL_ITEM_UNFOCUSED, 220), styleLbl);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* 10 checkboxes */
    for (size_t i = 0; i < sizeof(g_MpCtrlRows) / sizeof(g_MpCtrlRows[0]); i++) {
        const MpCtrlRow *r = &g_MpCtrlRows[i];
        bool on = pdguiCdGetPlayerOption((s32)r->optionBit) != 0;
        ImGui::PushID((int)i);
        if (ImGui::Checkbox(r->label, &on)) {
            pdguiCdSetPlayerOption((s32)r->optionBit, on ? 1 : 0);
            pdguiPlaySound(PDGUI_SND_SELECT);
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* Aim Control dropdown: Hold / Toggle */
    {
        const char *items[2] = { "Hold", "Toggle" };
        s32 cur = pdguiCdGetAimControl();
        if (cur < 0 || cur > 1) cur = 0;
        ImGui::Text("Aim Control");
        ImGui::SameLine(pdguiScale(180.0f));
        ImGui::SetNextItemWidth(pdguiScale(180.0f));
        if (ImGui::BeginCombo("##aim_ctrl", items[cur])) {
            for (s32 i = 0; i < 2; i++) {
                bool sel = (i == cur);
                if (ImGui::Selectable(items[i], sel)) {
                    pdguiCdSetAimControl(i);
                    pdguiPlaySound(PDGUI_SND_SELECT);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::EndChild();

    /* Footer */
    ImGui::SetCursorPosY(diagH - footerH + pdguiScale(12.0f));
    ImGui::Separator();
    ImGui::Spacing();
    {
        float btnW = pdguiScale(210.0f);
        float btnH = pdguiScale(42.0f);
        ImGui::SetCursorPosX((diagW - btnW) * 0.5f);
        if (PdButton("Back", ImVec2(btnW, btnH))) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

extern "C" {

static bool s_Registered = false;

void pdguiMenuControlDiagramRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    pdguiHotswapRegister(&g_SoloMissionControlStyleMenuDialog,
                         renderSoloMissionControlStyle,
                         "Solo Control Style");
    pdguiHotswapRegister(&g_MpControlMenuDialog,
                         renderMpControl,
                         "MP Control Options");

    sysLogPrintf(LOG_NOTE,
        "pdgui_menu_controldiagram: registered (Batch 10, 2 dialogs)");
}

} /* extern "C" */
