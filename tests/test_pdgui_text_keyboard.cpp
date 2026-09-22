#include "catch.hpp"
#include "pdgui_text_keyboard.h"
#include "pdgui_nav_input.h"
#include "pdgui_text_keyboard_gate.h"
#include "../port/fast3d/imgui/imgui.h"
#include "../port/fast3d/imgui/imgui_internal.h"

#include <cstring>
#include <cstdio>
#include <set>
#include <string>
#include <fstream>
#include <iterator>

namespace {
struct KeyboardContext {
    std::string text;
    bool focus = true, active = false, validated = false;
    int flags = ImGuiInputTextFlags_EnterReturnsTrue;
    int edits = 0, resizes = 0;
    bool dynamicBuffer = false, multiline = false, disabled = false;
    const char *queuedPopup = nullptr;
    PdguiTextKeyboardReleaseGate releaseGate;
    ImGuiID fieldId = 0, windowId = 0;
    PdguiTextKeyboardInput input = {1, 1, 1, 0, 0, 0, 0, 0, 0, 1.0f / 60.0f};

    KeyboardContext() {
        ImGui::CreateContext();
        pdguiTextKeyboardReset();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1000, 750);
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~KeyboardContext() { pdguiTextKeyboardReset(); ImGui::DestroyContext(); }
    static int callback(ImGuiInputTextCallbackData *data) {
        auto &context = *static_cast<KeyboardContext *>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter && data->EventChar == 'x') return 1;
        if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit) ++context.edits;
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            ++context.resizes;
            context.text.resize(data->BufTextLen);
            data->Buf = &context.text[0];
        }
        return 0;
    }
    void frame(bool drawField = true, const char *field = "Name", bool modal = false,
        const char *window = "Text fields") {
        pdguiNavCaptureOwners();
        const bool owned = pdguiTextKeyboardBeginFrame(&input) != 0;
        std::array<bool, ACTION_COUNT> held = {};
        held[ACTION_MENU_ACCEPT] = input.accept;
        held[ACTION_MENU_CANCEL] = input.cancel;
        held[ACTION_MENU_UP] = input.up;
        held[ACTION_MENU_DOWN] = input.down;
        held[ACTION_MENU_LEFT] = input.left;
        held[ACTION_MENU_RIGHT] = input.right;
        releaseGate.update(owned, held, false);
        pdguiSubmitNavInput({true,
            releaseGate.allows(ACTION_MENU_ACCEPT) && input.accept,
            releaseGate.allows(ACTION_MENU_CANCEL) && input.cancel,
            releaseGate.allows(ACTION_MENU_UP) && input.up,
            releaseGate.allows(ACTION_MENU_DOWN) && input.down,
            releaseGate.allows(ACTION_MENU_LEFT) && input.left,
            releaseGate.allows(ACTION_MENU_RIGHT) && input.right});
        ImGui::NewFrame();
        pdguiNavFinishOwners();
        ImGui::SetNextWindowPos(ImVec2(20, 20));
        ImGui::SetNextWindowSize(ImVec2(800, 640));
        ImGui::Begin(window, nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (queuedPopup) ImGui::OpenPopup(queuedPopup);
        if (modal) ImGui::OpenPopup("New modal");
        if (ImGui::BeginPopupModal("New modal")) {
            ImGui::Button("Modal owner");
            ImGui::EndPopup();
        }
        if (drawField) {
            ImGui::BeginDisabled(disabled);
            if (focus) { ImGui::SetKeyboardFocusHere(); focus = false; }
            windowId = ImGui::GetCurrentWindow()->ID;
            fieldId = ImGui::GetID(field);
            if (dynamicBuffer) {
                validated = ImGui::InputText(field, &text[0], text.capacity() + 1,
                    flags | ImGuiInputTextFlags_CallbackResize, callback, this);
            } else {
                /* This address belongs to this frame only: the OSK may retain
                 * field IDs, never this caller-owned stack buffer. */
                char transient[256];
                std::snprintf(transient, sizeof(transient), "%s", text.c_str());
                validated = multiline
                    ? ImGui::InputTextMultiline(field, transient, sizeof(transient), ImVec2(500, 300), flags, callback, this)
                    : ImGui::InputText(field, transient, sizeof(transient), flags, callback, this);
                text = transient;
            }
            active = ImGui::IsItemActive();
            ImGui::EndDisabled();
        }
        ImGui::End();
        const auto activeBefore = ImGui::GetCurrentContext()->ActiveId;
        auto *navBefore = ImGui::GetCurrentContext()->NavWindow;
        pdguiTextKeyboardRender();
        REQUIRE(ImGui::GetCurrentContext()->ActiveId == activeBefore);
        REQUIRE(ImGui::GetCurrentContext()->NavWindow == navBefore);
        ImGui::Render();
    }
    void open() {
        for (int i = 0; i < 4; ++i) frame();
        REQUIRE(active);
        REQUIRE(pdguiTextKeyboardKeyCount() == 55);
    }
    int find(unsigned character, int key = PDGUI_TEXT_KEY_NONE) {
        for (int index = 0; index < pdguiTextKeyboardKeyCount(); ++index) {
            PdguiTextKeyboardKeyInfo info;
            REQUIRE(pdguiTextKeyboardGetKey(index, &info));
            if (info.character == character && info.key == key) return index;
        }
        FAIL("requested keyboard key is absent");
        return -1;
    }
    void click(int index, bool outside = false) {
        PdguiTextKeyboardKeyInfo key;
        REQUIRE(pdguiTextKeyboardGetKey(index, &key));
        const float x = (key.left + key.right) / 2, y = (key.top + key.bottom) / 2;
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_PRESS, x, y, 0));
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_RELEASE,
            outside ? -100.0f : x, outside ? -100.0f : y, 0));
    }
    void character(unsigned value) { click(find(value)); frame(); }
    void command(int key) { click(find(0, key)); frame(); }
};
}

TEST_CASE("controller keyboard opening hold cannot type until neutral and a new Accept",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.input.accept = 1;
    for (int i = 0; i < 5; ++i) context.frame();
    REQUIRE(context.text.empty());
    REQUIRE(pdguiTextKeyboardKeyCount() == 55);
    context.input.accept = 0;
    context.frame();
    context.input.accept = 1;
    context.frame();
    REQUIRE(context.text == "1");
    context.input.accept = 0;
    context.frame();
    context.input.right = 1;
    context.frame();
    context.input.right = 0;
    context.frame();
    context.input.accept = 1;
    context.frame();
    REQUIRE(context.text == "12");
}

TEST_CASE("keyboard grid covers printable ASCII with native filtering and callbacks",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.flags |= ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit;
    context.open();
    std::set<unsigned> printable;
    for (int shift = 0; shift < 2; ++shift) {
        for (int i = 0; i < pdguiTextKeyboardKeyCount(); ++i) {
            PdguiTextKeyboardKeyInfo key;
            REQUIRE(pdguiTextKeyboardGetKey(i, &key));
            if (key.character) printable.insert(key.character);
            REQUIRE(key.left >= 0);
            REQUIRE(key.right <= ImGui::GetIO().DisplaySize.x);
            REQUIRE(key.top >= 0);
            REQUIRE(key.bottom <= ImGui::GetIO().DisplaySize.y);
            const auto &caret = ImGui::GetCurrentContext()->PlatformImeData;
            REQUIRE((key.bottom <= caret.InputPos.y ||
                key.top >= caret.InputPos.y + caret.InputLineHeight));
        }
        context.command(PDGUI_TEXT_KEY_SHIFT);
    }
    REQUIRE(printable.size() == 95);
    for (unsigned c = 32; c <= 126; ++c) REQUIRE(printable.count(c) == 1);
    context.character('x');
    REQUIRE(context.text.empty());
    context.character('a');
    REQUIRE(context.text == "a");
    REQUIRE(context.edits > 0);
    context.command(PDGUI_TEXT_KEY_SHIFT);
    context.character('A');
    context.character('!');
    REQUIRE(context.text == "aA!");
}

TEST_CASE("native caret backspace undo commit and physical typing coexist with the keyboard",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.open();
    context.character('a');
    context.character('b');
    context.command(PDGUI_TEXT_KEY_LEFT);
    context.character('c');
    REQUIRE(context.text == "acb");
    context.command(PDGUI_TEXT_KEY_BACKSPACE);
    REQUIRE(context.text == "ab");
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Z, true);
    context.frame();
    REQUIRE(context.text == "acb");
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Z, false);
    context.frame();
    context.input.controller_preferred = 0;
    ImGui::GetIO().AddInputCharactersUTF8("d");
    context.frame();
    REQUIRE(context.text == "acdb");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_LeftArrow, true);
    context.frame();
    context.command(PDGUI_TEXT_KEY_RIGHT);
    REQUIRE(ImGui::IsKeyDown(ImGuiKey_LeftArrow));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_LeftArrow, false);
    context.frame();
    context.command(PDGUI_TEXT_KEY_ENTER);
    REQUIRE(context.validated);
    REQUIRE_FALSE(context.active);
    REQUIRE(pdguiTextKeyboardKeyCount() == 0);
}

TEST_CASE("keyboard cancellation preserves native revert and password semantics",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.text = "secret";
    context.flags |= ImGuiInputTextFlags_Password;
    context.open();
    context.character('a');
    REQUIRE(context.text != "secret");
    REQUIRE((ImGui::GetCurrentContext()->InputTextState.Flags & ImGuiInputTextFlags_Password) != 0);
    for (int i = 0; i < pdguiTextKeyboardKeyCount(); ++i) {
        PdguiTextKeyboardKeyInfo key;
        pdguiTextKeyboardGetKey(i, &key);
        REQUIRE(std::string(key.label) != "secret");
    }
    context.input.cancel = 1;
    context.frame();
    REQUIRE(context.text == "secret");
    REQUIRE_FALSE(context.active);
    REQUIRE(pdguiTextKeyboardKeyCount() == 0);
}

TEST_CASE("keyboard owns pointer releases and wheel without typing after an outside release",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.open();
    context.click(context.find('a'), true);
    context.frame();
    REQUIRE(context.text.empty());
    PdguiTextKeyboardKeyInfo key;
    REQUIRE(pdguiTextKeyboardGetKey(0, &key));
    REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_WHEEL, key.left + 1, key.top + 1, 0));
    REQUIRE_FALSE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_WHEEL, -100, -100, 0));
}

TEST_CASE("queued edits cannot reach removed replacement readonly unfocused or modal-owned fields",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.open();
    context.click(context.find('a'));
    SECTION("missing field") {
        context.frame(false);
        REQUIRE(context.text.empty());
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
        context.focus = true;
        context.frame();
        REQUIRE(context.text.empty());
    }
    SECTION("new field ID") {
        context.focus = true;
        context.frame(true, "Replacement");
        REQUIRE(context.text.empty());
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
    }
    SECTION("new owning window") {
        context.focus = true;
        context.frame(true, "Name", false, "Replacement window");
        REQUIRE(context.text.empty());
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
    }
    SECTION("new ImGui context") {
        struct ReplacementContext {
            ImGuiContext *previous = ImGui::GetCurrentContext();
            ImGuiContext *replacement = ImGui::CreateContext();
            ReplacementContext() { ImGui::SetCurrentContext(replacement); }
            ~ReplacementContext() {
                ImGui::DestroyContext(replacement);
                ImGui::SetCurrentContext(previous);
            }
        };
        {
            // CreateContext preserves an existing current context. Explicitly
            // select the replacement and restore the old owner on every exit.
            ReplacementContext replacement;
            REQUIRE(ImGui::GetCurrentContext() == replacement.replacement);
            REQUIRE_FALSE(pdguiTextKeyboardBeginFrame(&context.input));
            REQUIRE(pdguiTextKeyboardKeyCount() == 0);
        }
        context.frame();
        REQUIRE(context.text.empty());
    }
    SECTION("readonly transition") {
        context.flags |= ImGuiInputTextFlags_ReadOnly;
        context.frame();
        REQUIRE(context.text.empty());
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
    }
    SECTION("focus loss") {
        context.input.focus_allowed = 0;
        ImGui::GetIO().AddFocusEvent(false);
        context.frame();
        REQUIRE(context.text.empty());
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
    }
    SECTION("new modal owner") {
        context.frame(true, "Name", true);
        REQUIRE(context.text.empty());
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
    }
}

TEST_CASE("Done uses native validation for multiline and keep-active fields",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    SECTION("multiline Done commits rather than adding a newline") {
        context.multiline = true;
        context.open();
        context.character('a');
        context.command(PDGUI_TEXT_KEY_ENTER);
        REQUIRE(context.text == "a");
        REQUIRE(context.validated);
        REQUIRE_FALSE(context.active);
    }
    SECTION("keep-active configuration remains native while keyboard closes") {
        ImGui::GetIO().ConfigInputTextEnterKeepActive = true;
        context.open();
        context.character('a');
        context.command(PDGUI_TEXT_KEY_ENTER);
        REQUIRE(context.validated);
        REQUIRE(context.active);
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
        context.frame();
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
    }
}

TEST_CASE("controller text reaches native decimal filtering and resize callbacks",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    SECTION("decimal filtering") {
        context.flags |= ImGuiInputTextFlags_CharsDecimal;
        context.open();
        context.character('a');
        REQUIRE(context.text.empty());
        context.character('2');
        REQUIRE(context.text == "2");
    }
    SECTION("native callback resizes the current caller buffer") {
        context.dynamicBuffer = true;
        context.text.reserve(16);
        context.open();
        const auto initial = context.text.capacity();
        for (size_t i = 0; i < initial + 2; ++i) context.character('a');
        REQUIRE(context.text == std::string(initial + 2, 'a'));
        REQUIRE(context.resizes > 0);
    }
}

TEST_CASE("OSK rejects a queued edit when an off-pointer active native field becomes disabled",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    ImGui::GetIO().AddMousePosEvent(-100, -100);
    context.open();
    context.click(context.find('a'));
    context.disabled = true;
    context.frame();
    REQUIRE(context.text.empty());
    REQUIRE(pdguiTextKeyboardKeyCount() == 0);
}

TEST_CASE("OSK rejects same-depth popup replacement before native field delivery",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.open();
    // OpenPopup may be queued before the later BeginPopup render. Keep the
    // old native field submitted to expose depth-only owner comparisons.
    context.queuedPopup = "First queued popup";
    for (int i = 0; i < 3; ++i) context.frame();
    REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);
    REQUIRE(pdguiTextKeyboardKeyCount() == 55);
    context.click(context.find('a'));
    const auto oldId = ImGui::GetCurrentContext()->OpenPopupStack.back().PopupId;
    context.queuedPopup = "Replacement queued popup";
    context.frame();
    REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);
    REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.back().PopupId != oldId);
    REQUIRE(context.text.empty());
    REQUIRE(pdguiTextKeyboardKeyCount() == 0);
}

TEST_CASE("OSK pointer ownership follows each down through geometry changes and teardown",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.open();
    PdguiTextKeyboardKeyInfo key;
    REQUIRE(pdguiTextKeyboardGetKey(context.find('a'), &key));
    const float x = (key.left + key.right) / 2, y = (key.top + key.bottom) / 2;
    SECTION("native down outside keeps its inside release and motion") {
        REQUIRE_FALSE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_PRESS, -100, -100, 0));
        REQUIRE_FALSE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_MOVE, x, y, 0));
        REQUIRE_FALSE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_RELEASE, x, y, 0));
        context.frame();
        REQUIRE(context.text.empty());
    }
    SECTION("two consumed buttons retain both outside releases after close") {
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_PRESS, x, y, 0));
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_PRESS, x, y, 2));
        context.frame(false);
        REQUIRE(pdguiTextKeyboardKeyCount() == 0);
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_RELEASE, -100, -100, 2));
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_RELEASE, -100, -100, 0));
    }
    SECTION("old press cannot type after a new field opens underneath") {
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_PRESS, x, y, 0));
        context.frame(false);
        context.focus = true;
        for (int i = 0; i < 4; ++i) context.frame(true, "Replacement");
        REQUIRE(pdguiTextKeyboardKeyCount() == 55);
        REQUIRE(pdguiTextKeyboardGetKey(context.find('a'), &key));
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_RELEASE,
            (key.left + key.right) / 2, (key.top + key.bottom) / 2, 0));
        context.frame(true, "Replacement");
        REQUIRE(context.text.empty());
    }
    SECTION("lost release retires pointer ownership on physical neutrality") {
        REQUIRE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_PRESS, x, y, 0));
        context.input.focus_allowed = 0;
        context.frame();
        pdguiTextKeyboardReconcilePointerButtons(0);
        REQUIRE_FALSE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_WHEEL, -100, -100, 0));
        REQUIRE_FALSE(pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_MOVE, -100, -100, 0));
    }
}

TEST_CASE("OSK dismissal does not manufacture held native Activate or Back",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.open();
    SECTION("Done while Accept stays held") {
        context.input.accept = 1;
        context.command(PDGUI_TEXT_KEY_ENTER);
        REQUIRE_FALSE(context.active);
        context.frame();
        REQUIRE_FALSE(context.active);
        REQUIRE_FALSE(ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));
        context.input.accept = 0;
        context.frame();
        REQUIRE(context.releaseGate.allows(ACTION_MENU_ACCEPT));
    }
    SECTION("Cancel stays held after native revert") {
        context.input.cancel = 1;
        context.frame();
        REQUIRE_FALSE(context.active);
        context.frame();
        REQUIRE_FALSE(ImGui::IsKeyDown(ImGuiKey_GamepadFaceRight));
        REQUIRE_FALSE(context.releaseGate.allows(ACTION_MENU_CANCEL));
        context.input.cancel = 0;
        context.frame();
        REQUIRE(context.releaseGate.allows(ACTION_MENU_CANCEL));
    }
}

TEST_CASE("OSK handoff gate releases independent actions and right-stick scroll only at neutral",
    "[input][menus][text-keyboard]")
{
    PdguiTextKeyboardReleaseGate gate;
    std::array<bool, ACTION_COUNT> held = {};
    held[ACTION_MENU_ACCEPT] = held[ACTION_MENU_CANCEL] = true;
    held[ACTION_MENU_TAB_NEXT] = held[ACTION_MENU_SECONDARY] = held[ACTION_MENU_DELETE] = true;
    gate.update(true, held, true);
    gate.update(false, held, true);
    for (auto action : {ACTION_MENU_ACCEPT, ACTION_MENU_CANCEL, ACTION_MENU_TAB_NEXT, ACTION_MENU_SECONDARY, ACTION_MENU_DELETE})
        REQUIRE_FALSE(gate.allows(action));
    REQUIRE(gate.blocksScroll());
    REQUIRE(gate.allows(ACTION_MENU_DOWN));
    held[ACTION_MENU_ACCEPT] = false;
    gate.update(false, held, true);
    REQUIRE(gate.allows(ACTION_MENU_ACCEPT));
    REQUIRE_FALSE(gate.allows(ACTION_MENU_CANCEL));
    held[ACTION_MENU_ACCEPT] = true;
    gate.update(false, held, false);
    REQUIRE(gate.allows(ACTION_MENU_ACCEPT));
    REQUIRE_FALSE(gate.blocksScroll());
}

TEST_CASE("Multiline OSK New line inserts through native edits and Done still commits",
    "[input][menus][text-keyboard][imgui]")
{
    KeyboardContext context;
    context.multiline = true;
    context.flags |= ImGuiInputTextFlags_CallbackEdit;
    context.open();
    context.character('a');
    context.character('\n');
    context.character('b');
    REQUIRE(context.text == "a\nb");
    REQUIRE(context.edits >= 3);
    REQUIRE(context.active);
    context.command(PDGUI_TEXT_KEY_ENTER);
    REQUIRE(context.validated);
    REQUIRE_FALSE(context.active);
    REQUIRE(context.text == "a\nb");
}

TEST_CASE("OSK backend uses current binding labels and connected release/pointer ownership",
    "[input][menus][text-keyboard][static]")
{
    const auto source = [](const char *path) {
        std::ifstream file(std::string(PD_SOURCE_DIR) + '/' + path, std::ios::binary);
        REQUIRE(file.good());
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const auto backend = source("port/fast3d/pdgui_backend.cpp");
    const auto helper = source("port/fast3d/pdgui_text_keyboard.cpp");
    const auto native = source("port/fast3d/imgui/imgui_widgets.cpp");
    for (const char *action : {"ACTION_MENU_ACCEPT", "ACTION_MENU_CANCEL", "ACTION_MENU_UP", "ACTION_MENU_DOWN", "ACTION_MENU_LEFT", "ACTION_MENU_RIGHT"})
        REQUIRE(backend.find(std::string("pdguiGlyphGetActionLabel(") + action) != std::string::npos);
    REQUIRE(backend.find("pdguiTextKeyboardSetHints(&textHints);") != std::string::npos);
    REQUIRE(helper.find("Keyboard - Select / Done / Cancel") == std::string::npos);
    REQUIRE(helper.find("std::string(s.hints.accept)") != std::string::npos);
    for (const char *color : {"ImGuiCol_PopupBg", "ImGuiCol_Border", "ImGuiCol_ButtonActive", "ImGuiCol_Button", "ImGuiCol_Text", "ImGuiCol_NavCursor"})
        REQUIRE(helper.find(color) != std::string::npos);
    REQUIRE(helper.find("IM_COL32(") == std::string::npos);
    REQUIRE(native.find("!clear_active_id && !(g.CurrentItemFlags & ImGuiItemFlags_Disabled)") != std::string::npos);
    REQUIRE(backend.find("s_TextKeyboardReleaseGate.update(textKeyboardOwns") < backend.find("pdguiSubmitNavInput({navigationActive"));
    REQUIRE(backend.find("s_TextKeyboardReleaseGate.allows(action)") != std::string::npos);
    REQUIRE(backend.find("s_TextKeyboardReleaseGate.blocksScroll()") != std::string::npos);
    const auto pointer = backend.find("if (textPointerOwned) return 1;");
    REQUIRE(pointer != std::string::npos);
    const auto pointerStart = backend.find("bool textPointerOwned = false;");
    REQUIRE(pointerStart != std::string::npos);
    const auto pointerSwitch = backend.substr(pointerStart, pointer - pointerStart);
    for (const char *member : {"motion", "button", "wheel"}) {
        const std::string windowGuard = std::string("if (!textWindowId || ev->") + member
            + ".windowID != textWindowId) break;";
        REQUIRE(pointerSwitch.find(windowGuard) != std::string::npos);
    }
    const auto captureRelease = backend.find("const bool textOwnedRelease = ev->type == SDL_MOUSEBUTTONUP &&");
    REQUIRE(captureRelease != std::string::npos);
    const auto releaseCall = backend.find("pdguiTextKeyboardPointerEvent(PDGUI_TEXT_POINTER_RELEASE", captureRelease);
    REQUIRE(releaseCall != std::string::npos);
    REQUIRE(backend.substr(captureRelease, releaseCall - captureRelease).find(
        "textWindowId != 0 && ev->button.windowID == textWindowId") != std::string::npos);
    REQUIRE(pointer > backend.find("if (pdguiCaptureBlocksEvent("));
    REQUIRE(pointer < backend.find("ImGui_ImplSDL2_ProcessEvent(ev);", pointer));
}
