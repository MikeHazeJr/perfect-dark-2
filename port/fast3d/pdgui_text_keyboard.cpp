#include "pdgui_text_keyboard.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>
#include <string>

namespace {
constexpr int kCharacterKeys = 48;
constexpr int kKeys = 55;
struct PopupIdentity {
    ImGuiID id, parent;
    bool operator==(const PopupIdentity &other) const { return id == other.id && parent == other.parent; }
};
std::vector<PopupIdentity> popupChain(const ImGuiContext &g)
{
    std::vector<PopupIdentity> result;
    for (const auto &popup : g.OpenPopupStack) result.push_back({popup.PopupId, popup.OpenParentId});
    return result;
}
struct Field {
    ImGuiContext *context = nullptr;
    ImGuiID id = 0, window = 0;
    int frame = -1, flags = 0;
    std::vector<PopupIdentity> popups;
};
struct Pending {
    PdguiTextKeyboardEdit edit;
    Field owner;
    int frame;
};
struct State {
    Field candidate, owner;
    bool open = false, shifted = false, neutralNeeded = true;
    unsigned int pointerOwned = 0, pointerForwarded = 0;
    int pointerKey = -1;
    PdguiTextKeyboardHints hints = {};
    int selection = 0, previousMask = 0, menuFrame = -1;
    int commandFrame = -1, commandKey = PDGUI_TEXT_KEY_NONE;
    ImGuiID dismissedId = 0, dismissedWindow = 0;
    float repeatElapsed = 0.0f, repeatNext = 0.35f;
    ImRect bounds;
    bool layoutValid = false;
    PdguiTextKeyboardKeyInfo keys[kKeys] = {};
    std::deque<Pending> pending;
} s;

bool sameField(const Field &a, const Field &b)
{
    return a.context == b.context && a.id == b.id && a.window == b.window;
}
void closeKeyboard(bool dismiss = false)
{
    if (dismiss) {
        s.dismissedId = s.owner.id;
        s.dismissedWindow = s.owner.window;
    }
    s.open = false;
    s.layoutValid = false;
    s.pending.clear();
    s.neutralNeeded = true;
    s.pointerKey = -1; // a held press may never type into a replacement owner
    /* Retain an owned pointer gesture until its release; teardown must not
     * deliver that release to a newly exposed menu underneath. */
}
bool liveField(const Field &field)
{
    ImGuiContext *g = ImGui::GetCurrentContext();
    if (!g || field.context != g || !field.id || g->ActiveId != field.id ||
        g->InputTextState.ID != field.id || !g->ActiveIdWindow ||
        g->ActiveIdWindow->ID != field.window || g->IO.AppFocusLost ||
        (field.flags & ImGuiInputTextFlags_ReadOnly) ||
        popupChain(*g) != field.popups || g->NavWindowingTarget)
        return false;
    ImGuiWindow *window = ImGui::FindWindowByID(field.window);
    return window && g->NavWindow && g->NavWindow->RootWindow == window->RootWindow;
}
void queueEdit(PdguiTextKeyboardEdit edit)
{
    if (!s.open || !liveField(s.owner)) return;
    int frame = ImGui::GetFrameCount() + 1;
    if (!s.pending.empty()) frame = std::max(frame, s.pending.back().frame + 1);
    s.pending.push_back({edit, s.owner, frame});
}
void fillKeyIdentity(int index, PdguiTextKeyboardKeyInfo &key)
{
    static const char *lower[] = {"1234567890-=", "qwertyuiop[]", "asdfghjkl;'\\", "zxcvbnm,./` "};
    static const char *upper[] = {"!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"|", "ZXCVBNM<>?~ "};
    key.character = 0;
    key.key = PDGUI_TEXT_KEY_NONE;
    if (index < kCharacterKeys) {
        key.character = (unsigned char)(s.shifted ? upper : lower)[index / 12][index % 12];
        if (key.character == ' ') std::snprintf(key.label, sizeof(key.label), "Space");
        else std::snprintf(key.label, sizeof(key.label), "%c", (char)key.character);
        return;
    }
    static const char *labels[] = {"Shift", "Space", "Backspace", "Left", "Right", "Done", "Cancel"};
    static const int edits[] = {PDGUI_TEXT_KEY_SHIFT, PDGUI_TEXT_KEY_NONE, PDGUI_TEXT_KEY_BACKSPACE,
        PDGUI_TEXT_KEY_LEFT, PDGUI_TEXT_KEY_RIGHT, PDGUI_TEXT_KEY_ENTER, PDGUI_TEXT_KEY_ESCAPE};
    const int control = index - kCharacterKeys;
    std::snprintf(key.label, sizeof(key.label), "%s", labels[control]);
    key.key = edits[control];
    if (control == 1) {
        const bool multiline = (s.candidate.flags & ImGuiInputTextFlags_Multiline) != 0;
        key.character = multiline ? '\n' : ' ';
        if (multiline) std::snprintf(key.label, sizeof(key.label), "New line");
    }
}
void activate(int index)
{
    if (index < 0 || index >= kKeys) return;
    PdguiTextKeyboardKeyInfo key = {};
    fillKeyIdentity(index, key);
    if (key.key == PDGUI_TEXT_KEY_SHIFT) s.shifted = !s.shifted;
    else queueEdit({key.character, key.key});
}
int hitKey(float x, float y)
{
    if (!s.layoutValid) return -1;
    for (int i = 0; i < kKeys; ++i) {
        const auto &key = s.keys[i];
        if (x >= key.left && x < key.right && y >= key.top && y < key.bottom) return i;
    }
    return -1;
}
float columnCenter(int index)
{
    if (index < kCharacterKeys) return float(index % 12) + 0.5f;
    static const float centers[] = {1, 3, 5, 6.5f, 7.5f, 9, 11};
    return centers[index - kCharacterKeys];
}
void moveSelection(int mask)
{
    int row = s.selection < kCharacterKeys ? s.selection / 12 : 4;
    if (mask & (4 | 8)) {
        const int first = row * 12;
        const int count = row == 4 ? 7 : 12;
        const int direction = mask & 4 ? -1 : 1;
        s.selection = first + (s.selection - first + direction + count) % count;
    } else if (mask & (1 | 2)) {
        const float column = columnCenter(s.selection);
        row = (row + (mask & 1 ? 4 : 1)) % 5;
        const int first = row * 12;
        const int count = row == 4 ? 7 : 12;
        int best = first;
        for (int i = first + 1; i < first + count; ++i)
            if (std::fabs(columnCenter(i) - column) < std::fabs(columnCenter(best) - column)) best = i;
        s.selection = best;
    }
}
}

void pdguiTextKeyboardReset(void) { s = State{}; }
void pdguiTextKeyboardCancelOwner(void) { closeKeyboard(); }
void pdguiTextKeyboardSetHints(const PdguiTextKeyboardHints *hints)
{
    s.hints = hints ? *hints : PdguiTextKeyboardHints{};
    for (char *label : {s.hints.accept, s.hints.cancel, s.hints.up,
                       s.hints.down, s.hints.left, s.hints.right}) label[95] = '\0';
}
void pdguiTextKeyboardReconcilePointerButtons(unsigned int held_mask)
{
    s.pointerOwned &= held_mask;
    s.pointerForwarded &= held_mask;
    if (!(s.pointerOwned & 1u)) s.pointerKey = -1;
}

int pdguiTextKeyboardBeginFrame(const PdguiTextKeyboardInput *input)
{
    ImGuiContext *g = ImGui::GetCurrentContext();
    if (!g || !input || !input->enabled || !input->focus_allowed || g->IO.AppFocusLost) {
        closeKeyboard();
        return 0;
    }
    if (s.candidate.context != g || s.candidate.id != g->ActiveId ||
        s.candidate.id != s.dismissedId || s.candidate.window != s.dismissedWindow) {
        s.dismissedId = s.dismissedWindow = 0;
    }
    if (s.open && (!liveField(s.owner) || s.candidate.frame != g->FrameCount ||
        !sameField(s.candidate, s.owner))) closeKeyboard();
    if (!s.open && input->controller_preferred && s.candidate.frame == g->FrameCount &&
        liveField(s.candidate) && (s.candidate.id != s.dismissedId ||
            s.candidate.window != s.dismissedWindow)) {
        s.owner = s.candidate;
        s.open = true;
        s.shifted = false;
        s.selection = 0;
        s.neutralNeeded = true;
        s.previousMask = 0;
        s.repeatElapsed = 0;
        s.repeatNext = 0.35f;
    }
    if (!s.open) return 0;
    s.menuFrame = g->FrameCount + 1;
    const int direction = (input->up ? 1 : 0) | (input->down ? 2 : 0) |
        (input->left ? 4 : 0) | (input->right ? 8 : 0);
    const int mask = direction | (input->accept ? 16 : 0) | (input->cancel ? 32 : 0);
    if (s.neutralNeeded) {
        if (!mask) s.neutralNeeded = false;
        s.previousMask = mask;
        return 1;
    }
    if (input->cancel && !(s.previousMask & 32)) queueEdit({0, PDGUI_TEXT_KEY_ESCAPE});
    else if (input->accept && !(s.previousMask & 16)) activate(s.selection);
    if (direction != (s.previousMask & 15)) {
        s.repeatElapsed = 0;
        s.repeatNext = 0.35f;
        if (direction) moveSelection(direction);
    } else if (direction) {
        s.repeatElapsed += std::min(std::max(input->delta_seconds, 0.0f), 0.1f);
        if (s.repeatElapsed >= s.repeatNext) {
            moveSelection(direction);
            s.repeatNext += 0.075f;
        }
    }
    s.previousMask = mask;
    return 1;
}

int pdguiTextKeyboardOwnsMenuInput(void)
{
    ImGuiContext *g = ImGui::GetCurrentContext();
    return g && (s.open || s.menuFrame == g->FrameCount);
}

PdguiTextKeyboardEdit pdguiTextKeyboardInputText(unsigned int field_id,
    unsigned int window_id, int flags, int input_allowed)
{
    PdguiTextKeyboardEdit empty = {};
    ImGuiContext *g = ImGui::GetCurrentContext();
    if (!g || g->ActiveId != field_id || g->InputTextState.ID != field_id) return empty;
    if (!input_allowed || (flags & ImGuiInputTextFlags_ReadOnly)) {
        if (s.open && s.owner.id == field_id) closeKeyboard();
        s.candidate = Field{};
        return empty;
    }
    s.candidate = {g, field_id, window_id, g->FrameCount, flags, popupChain(*g)};
    if (!s.open) return empty;
    if (!sameField(s.candidate, s.owner) || !liveField(s.owner) ||
        g->ActiveIdIsJustActivated || s.owner.popups != s.candidate.popups) {
        closeKeyboard();
        return empty;
    }
    if (s.pending.empty() || s.pending.front().frame > g->FrameCount) return empty;
    const Pending pending = s.pending.front();
    if (pending.frame != g->FrameCount || !sameField(pending.owner, s.candidate)) {
        closeKeyboard();
        return empty;
    }
    s.pending.pop_front();
    s.commandFrame = g->FrameCount;
    s.commandKey = pending.edit.key;
    return pending.edit;
}

void pdguiTextKeyboardFinishInputText(unsigned int field_id,
    unsigned int window_id, int validated)
{
    ImGuiContext *g = ImGui::GetCurrentContext();
    if (g && s.open && s.owner.context == g && s.owner.id == field_id &&
        s.owner.window == window_id && validated && s.commandFrame == g->FrameCount &&
        s.commandKey == PDGUI_TEXT_KEY_ENTER) closeKeyboard(true);
}

void pdguiTextKeyboardRender(void)
{
    ImGuiContext *g = ImGui::GetCurrentContext();
    if (!s.open || !g) return;
    if (!liveField(s.owner) || !sameField(s.candidate, s.owner) ||
        s.candidate.frame != g->FrameCount || g->ActiveIdIsAlive != s.owner.id) {
        closeKeyboard();
        return;
    }
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 lo = viewport->WorkPos + ImVec2(8, 8);
    const ImVec2 hi = viewport->WorkPos + viewport->WorkSize - ImVec2(8, 8);
    const float width = std::min(hi.x - lo.x, 780.0f);
    const float cursorTop = std::max(lo.y, std::min(g->PlatformImeData.InputPos.y - 10, hi.y));
    const float cursorBottom = std::max(lo.y, std::min(g->PlatformImeData.InputPos.y +
        g->PlatformImeData.InputLineHeight + 10, hi.y));
    const bool below = hi.y - cursorBottom >= cursorTop - lo.y;
    const float available = below ? hi.y - cursorBottom : cursorTop - lo.y;
    const float height = std::min(available, 286.0f);
    /* Keep the native caret unobscured and never draw keys beyond the viewport.
     * If the viewport cannot hold readable controls, retain no invisible owner. */
    if (width < 360.0f || height < 116.0f) {
        closeKeyboard(true);
        return;
    }
    const float x = lo.x + (hi.x - lo.x - width) * 0.5f;
    const float y = below ? hi.y - height : lo.y;
    s.bounds = ImRect(ImVec2(x, y), ImVec2(x + width, y + height));
    s.layoutValid = true;
    const float header = std::min(28.0f, height * 0.15f);
    const float cellW = (width - 12) / 12;
    const float cellH = (height - header - 10) / 5;
    ImDrawList *draw = ImGui::GetForegroundDrawList();
    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
    background.w = std::max(background.w, 0.94f);
    draw->AddRectFilled(s.bounds.Min, s.bounds.Max, ImGui::GetColorU32(background), 6);
    draw->AddRect(s.bounds.Min, s.bounds.Max, ImGui::GetColorU32(ImGuiCol_Border), 6);
    draw->PushClipRect(s.bounds.Min, s.bounds.Max, true);
    const float fontSize = std::min(ImGui::GetFontSize(), cellH * 0.64f);
    std::string help = "Type or click a key; choose Done to finish; Esc cancels";
    if (s.hints.controller_mode) {
        help = "Select [" + std::string(s.hints.accept) + "]  Cancel [" + s.hints.cancel
            + "]  Move [" + s.hints.up + "]/ [" + s.hints.down + "]/ ["
            + s.hints.left + "]/ [" + s.hints.right + "]";
    }
    float helpSize = std::min(16.0f, header * 0.65f);
    const float helpWidth = ImGui::GetFont()->CalcTextSizeA(helpSize, FLT_MAX, 0, help.c_str()).x;
    if (helpWidth > width - 16) helpSize *= (width - 16) / helpWidth;
    draw->AddText(ImGui::GetFont(), helpSize, ImVec2(x + 8, y + 4),
        ImGui::GetColorU32(ImGuiCol_Text), help.c_str());
    static const int controlCol[] = {0, 2, 4, 6, 7, 8, 10};
    static const int controlSpan[] = {2, 2, 2, 1, 1, 2, 2};
    for (int i = 0; i < kKeys; ++i) {
        auto &key = s.keys[i];
        fillKeyIdentity(i, key);
        const int row = i < kCharacterKeys ? i / 12 : 4;
        const int col = i < kCharacterKeys ? i % 12 : controlCol[i - kCharacterKeys];
        const int span = i < kCharacterKeys ? 1 : controlSpan[i - kCharacterKeys];
        key.left = x + 6 + col * cellW + 2;
        key.right = key.left + span * cellW - 4;
        key.top = y + header + row * cellH + 2;
        key.bottom = key.top + cellH - 4;
        const ImVec2 min(key.left, key.top), max(key.right, key.bottom);
        const bool selected = i == s.selection;
        draw->AddRectFilled(min, max, ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_Button), 3);
        if (selected || (i == 48 && s.shifted)) draw->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_NavCursor), 3);
        ImVec2 text = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0, key.label);
        const float keyFontSize = text.x > max.x - min.x - 4
            ? fontSize * (max.x - min.x - 4) / text.x : fontSize;
        text = ImGui::GetFont()->CalcTextSizeA(keyFontSize, FLT_MAX, 0, key.label);
        draw->AddText(ImGui::GetFont(), keyFontSize, (min + max - text) * 0.5f,
            ImGui::GetColorU32(ImGuiCol_Text), key.label);
    }
    draw->PopClipRect();
}

int pdguiTextKeyboardPointerEvent(int kind, float x, float y, int button)
{
    const bool inside = s.open && s.layoutValid && s.bounds.Contains(ImVec2(x, y));
    if (kind == PDGUI_TEXT_POINTER_WHEEL) return inside || s.pointerOwned;
    if (kind == PDGUI_TEXT_POINTER_MOVE) {
        if (s.pointerForwarded) return 0; // preserve an external native drag
        const int key = inside ? hitKey(x, y) : -1;
        if (key >= 0) s.selection = key;
        return inside || s.pointerOwned;
    }
    if (button < 0 || button >= 32) return 0;
    const unsigned int bit = 1u << button;
    if (kind == PDGUI_TEXT_POINTER_PRESS) {
        if (s.pointerOwned & bit) return 1;
        if (s.pointerForwarded & bit) return 0;
        if (inside) {
            s.pointerOwned |= bit;
            if (button == 0) s.pointerKey = hitKey(x, y);
            return 1;
        }
        s.pointerForwarded |= bit;
        return 0;
    }
    if (kind == PDGUI_TEXT_POINTER_RELEASE) {
        const bool owned = (s.pointerOwned & bit) != 0;
        s.pointerOwned &= ~bit;
        s.pointerForwarded &= ~bit;
        if (owned && button == 0) {
            const int key = inside ? hitKey(x, y) : -1;
            if (key >= 0 && key == s.pointerKey) activate(key);
            s.pointerKey = -1;
        }
        return owned; // never swallow a release for a forwarded/unseen down
    }
    return 0;
}

int pdguiTextKeyboardKeyCount(void) { return s.open && s.layoutValid ? kKeys : 0; }
int pdguiTextKeyboardGetKey(int index, PdguiTextKeyboardKeyInfo *out)
{
    if (!out || !s.open || !s.layoutValid || index < 0 || index >= kKeys) return 0;
    *out = s.keys[index];
    return 1;
}
