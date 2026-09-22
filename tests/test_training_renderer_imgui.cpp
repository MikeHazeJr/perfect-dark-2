/* This target links the real pdgui_menu_training.cpp and C navigation wrappers.
 * Only data, presentation and graph boundaries are adapters; the renderer,
 * focus, Selectable/Button decisions and Back ordering are production code. */
#include "catch.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "pdgui_audio.h"
#include "pdgui_style.h"
#include "pdgui_nav.h"
#include "pdgui_nav_input.h"
#include "inputctx.h"
#include "menupool.h"
#include "menugraph.h"
#include <array>
#include <string>
#include <vector>

extern "C" {
void pdguiTestResetFrWeaponList(void);
s32 pdguiTestRenderFrWeaponList(s32 width, s32 height);
extern struct menudialogdef g_FrDifficultyMenuDialog;
extern struct menudialogdef g_FrTrainingInfoPreGameMenuDialog;
}

namespace {
struct Calls {
    int loads = 0;
    int slotWrites = 0;
    int difficultyWrites = 0;
    int pushes = 0;
    int pops = 0;
    int slot = -1;
    int difficulty = -1;
    menu_type_t source = MENU_TYPE_NONE;
    const menudialogdef *destination = nullptr;
    bool dispatchInWindow = false;
    std::vector<std::string> order;
} calls;
std::array<bool, ACTION_COUNT> held{}, pressed{};
constexpr int weaponCount = 3;
const char *weaponNames[weaponCount] = { "Falcon 2\n", "MagSec 4\n", "CMP150\n" };

void graphBoundary(menu_type_t source)
{
    calls.source = source;
    // NewFrame owns only the implicit fallback window after renderer End().
    calls.dispatchInWindow |= GImGui->CurrentWindowStack.Size != 1;
}

struct Input {
    bool keyboardAccept = false;
    bool keyboardBack = false;
    bool tab = false;
    bool controllerAccept = false;
    bool controllerBack = false;
    bool down = false;
};

struct RendererHarness {
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGuiContext *context = ImGui::CreateContext();
    ImGuiID rows[weaponCount]{};
    ImGuiID back = 0;

    RendererHarness()
    {
        auto &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280, 720);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.MousePos = ImVec2(-1000, -1000);
        unsigned char *pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        held.fill(false); pressed.fill(false);
        pdguiNavSetActionFilter([](InputAction action) -> s32 {
            return pdguiNavActionAllowed(action) ? 1 : 0;
        });
        pdguiNavSetFrameCancel(0);
        pdguiTestResetFrWeaponList();
        for (int i = 0; i < 4; ++i) frame();
        REQUIRE(rows[0] != 0);
        REQUIRE(GImGui->NavId == rows[0]);
        REQUIRE(GImGui->NavCursorVisible);
        calls = {};
    }
    ~RendererHarness()
    {
        pdguiNavSetActionFilter(nullptr);
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous);
    }
    void frame(Input input = {})
    {
        std::array<bool, ACTION_COUNT> next{};
        next[ACTION_MENU_ACCEPT] = input.keyboardAccept || input.controllerAccept;
        next[ACTION_MENU_CANCEL] = input.keyboardBack || input.controllerBack;
        next[ACTION_MENU_DOWN] = input.down;
        for (int a = 0; a < ACTION_COUNT; ++a) pressed[a] = next[a] && !held[a];
        held = next;
        pdguiNavCaptureOwners();
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Enter, input.keyboardAccept);
        io.AddKeyEvent(ImGuiKey_Escape, input.keyboardBack);
        io.AddKeyEvent(ImGuiKey_Tab, input.tab);
        pdguiSubmitNavInput({true, input.controllerAccept, input.controllerBack,
                            false, input.down, false, false});
        ImGui::NewFrame();
        pdguiNavFinishOwners();
        REQUIRE(pdguiTestRenderFrWeaponList(1280, 720) == 1);
        // Observe the IDs of production-submitted widgets; no replacement
        // widgets, SetNavID or manual selection/dispatch runs in this harness.
        ImGuiWindow *parent = ImGui::FindWindowByName("##fr_weapon_list");
        REQUIRE(parent != nullptr);
        back = parent->GetID("Back");
        ImGuiWindow *body = nullptr;
        for (ImGuiWindow *child : parent->DC.ChildWindows)
            if (child->ChildId == parent->GetID("##fr_wl_body")) body = child;
        REQUIRE(body != nullptr);
        for (int i = 0; i < weaponCount; ++i)
            rows[i] = ImHashStr("##fr_wl_row", 0, body->GetID(i));
        ImGui::Render();
    }
    void nextTab()
    {
        Input input; input.tab = true;
        frame(input); frame(); frame();
    }
    void nextDown()
    {
        Input input; input.down = true;
        frame(input); frame(); frame();
    }
    void accept(bool controller)
    {
        Input input;
        input.controllerAccept = controller;
        input.keyboardAccept = !controller;
        frame(input);
    }
};
} // namespace

extern "C" {
InputContext g_CtxImGuiMenu = {};
float videoGetUiScaleMult(void) { return 1.0f; }
void pdguiDrawPdDialog(float, float, float, float, const char *, s32) {}
void pdguiDrawButtonEdgeGlow(f32, f32, f32, f32, s32) {}
void pdguiDrawTextGlow(f32, f32, f32, f32) {}
void pdguiSetCursorBelowTitle(float title)
{
    ImGui::SetCursorPosY(title + ImGui::GetStyle().WindowPadding.y);
}
u32 pdguiPalImU32(s32, s32 alpha) { return IM_COL32(160, 160, 160, alpha); }
void pdguiPlaySound(int) {}

const struct menudialogdef *menupoolDialogDef(const struct menudialog *) { return nullptr; }
s32 menupoolAcquireDialog(const struct menudialogdef *, InputContext *) { return 1; }
s32 menupoolReleaseDialog(const struct menudialogdef *) { return 1; }

s32 actionPressed(s32, InputAction action) { return pressed[action] ? 1 : 0; }
s32 actionHeld(s32, InputAction action) { return held[action] ? 1 : 0; }
u32 actionHoldPressStartMs(s32, InputAction) { return 0; }

s32 pdguiTrFrNumWeaponsAvailable(void) { return weaponCount; }
s32 pdguiTrFrGetSlot(void) { return 0; }
u32 pdguiTrFrWeaponBySlot(s32 slot) { return 100u + (u32)slot; }
const char *pdguiTrFrWeaponName(u32 weapon) { return weaponNames[weapon - 100u]; }
s32 pdguiTrFrWeaponScoreTier(u32 weapon) { return weapon == 100u ? 0 : 1; }
void frLoadData(void) { ++calls.loads; calls.order.emplace_back("load"); }
void pdguiTrFrSetSlot(s32 slot)
{
    ++calls.slotWrites; calls.slot = slot; calls.order.emplace_back("slot");
}
void frSetDifficulty(s32 difficulty)
{
    ++calls.difficultyWrites; calls.difficulty = difficulty; calls.order.emplace_back("difficulty");
}
s32 menuGraphFirePushDialog(menu_type_t source, const char *edge, struct menudialogdef *destination)
{
    ++calls.pushes; calls.destination = destination; graphBoundary(source);
    calls.order.emplace_back(std::string("push:") + edge);
    return 0;
}
s32 menuGraphFirePop(menu_type_t source, const char *edge)
{
    ++calls.pops; graphBoundary(source); calls.order.emplace_back(std::string("pop:") + edge);
    return 0;
}
}

TEST_CASE("Actual Firing Range row Accept dispatches once after End", "[input][menus][imgui][training_renderer]")
{
    for (bool controller : {false, true}) {
        CAPTURE(controller);
        RendererHarness ui;
        ui.accept(controller);
        REQUIRE(calls.pushes == 1);
        REQUIRE(calls.pops == 0);
        REQUIRE(calls.loads == 1);
        REQUIRE(calls.slotWrites == 1);
        REQUIRE(calls.difficultyWrites == 1);
        REQUIRE(calls.slot == 0);
        REQUIRE(calls.difficulty == 0);
        REQUIRE(calls.destination == &g_FrTrainingInfoPreGameMenuDialog);
        REQUIRE(calls.source == MENU_TYPE_FR_WEAPON_LIST);
        REQUIRE_FALSE(calls.dispatchInWindow);
        const std::vector<std::string> expected{"load", "slot", "difficulty", "push:info"};
        REQUIRE(calls.order == expected);
        ui.frame(); ui.frame();
        REQUIRE(calls.pushes == 1);
    }
}

TEST_CASE("Actual Firing Range footer Back performs no weapon writes", "[input][menus][imgui][training_renderer]")
{
    for (bool controller : {false, true}) {
        CAPTURE(controller);
        RendererHarness ui;
        for (int i = 0; i < weaponCount; ++i) ui.nextTab();
        REQUIRE(GImGui->NavId == ui.back);
        REQUIRE(calls.pushes == 0);
        ui.accept(controller);
        REQUIRE(calls.pops == 1);
        REQUIRE(calls.pushes == 0);
        REQUIRE(calls.loads == 0);
        REQUIRE(calls.slotWrites == 0);
        REQUIRE(calls.difficultyWrites == 0);
        REQUIRE_FALSE(calls.dispatchInWindow);
    }
}

TEST_CASE("Actual Firing Range native Tab and D-pad choose the focused slot", "[input][menus][imgui][training_renderer]")
{
    for (bool controller : {false, true}) {
        CAPTURE(controller);
        RendererHarness ui;
        if (controller) ui.nextDown(); else ui.nextTab();
        REQUIRE(GImGui->NavId == ui.rows[1]);
        ui.accept(controller);
        REQUIRE(calls.slot == 1);
        REQUIRE(calls.pushes == 1);
        REQUIRE(calls.pops == 0);
        REQUIRE(calls.difficulty == 1);
        REQUIRE(calls.destination == &g_FrDifficultyMenuDialog);
        REQUIRE_FALSE(calls.dispatchInWindow);
    }
}

TEST_CASE("Actual Firing Range paired Back and Accept only pop", "[input][menus][imgui][training_renderer]")
{
    for (bool controller : {false, true}) {
        CAPTURE(controller);
        RendererHarness ui;
        Input input;
        input.controllerAccept = controller;
        input.controllerBack = controller;
        input.keyboardAccept = !controller;
        input.keyboardBack = !controller;
        ui.frame(input);
        REQUIRE(calls.pops == 1);
        REQUIRE(calls.pushes == 0);
        REQUIRE(calls.loads == 0);
        REQUIRE(calls.slotWrites == 0);
        REQUIRE(calls.difficultyWrites == 0);
        REQUIRE_FALSE(calls.dispatchInWindow);
        const std::vector<std::string> expected{"pop:back"};
        REQUIRE(calls.order == expected);
    }
}
