#ifndef PDGUI_NAV_INPUT_H
#define PDGUI_NAV_INPUT_H

#include <cstdint>
#include <unordered_set>
#include "actionmap.h"

/* Mapped navigation uses its own ImGui key namespace. Physical keyboard
 * events remain owned by the SDL backend, including text editing. */
struct PdguiNavInput {
    bool active;
    bool accept;
    bool cancel;
    bool up;
    bool down;
    bool left;
    bool right;
};

void pdguiSubmitNavInput(const PdguiNavInput &input);

/* Snapshot before NewFrame: ImGui may consume Cancel and remove a popup
 * before the application's parent menu gets a chance to query its actions. */
void pdguiNavCaptureOwners();
/* Finish immediately after NewFrame, before any widget can reactivate an
 * owner which native Cancel just closed. */
void pdguiNavFinishOwners();
/* Native widget ownership is action-specific; parent commands such as tab
 * cycling remain available unless a popup or an owner transition blocks them. */
bool pdguiNavActionAllowed(InputAction action = ACTION_MENU_ACCEPT);
void pdguiNavSuppressActivation();
/* Opening windows own currently held native Accept/Back keys until release;
 * use this instead of fake key releases or flushing action-map held state. */
void pdguiNavSuppressOpeningGesture();

struct PdguiMouseBackGesture {
    bool held = false;
    bool dragged = false;
    float startX = 0.0f;
    float startY = 0.0f;

    bool update(bool active, bool down, float x, float y, float threshold)
    {
        if (!active) {
            held = down;
            dragged = true; // an input-owner change cancels the gesture
            return false;
        }
        if (down && !held) {
            startX = x;
            startY = y;
            dragged = false;
        }
        if (held || down) {
            float dx = x - startX, dy = y - startY;
            dragged = dragged || dx * dx + dy * dy >= threshold * threshold;
        }
        bool clicked = held && !down && !dragged;
        held = down;
        return clicked;
    }
};

/* A captured gesture belongs to capture through its release, even when the
 * review window has already replaced the listening window. */
class PdguiCaptureLatch {
public:
    void clear() { held.clear(); }
    void removeDevice(uint32_t instance)
    {
        for (auto it = held.begin(); it != held.end();) {
            if ((*it >> 56) >= 3 && (uint32_t)(*it >> 16) == instance)
                it = held.erase(it);
            else ++it;
        }
    }
    bool ownsMenuScroll() const
    {
        for (uint64_t token : held) {
            const uint64_t family = token >> 56;
            if ((family == 5 || family == 6) && (token & 0xffff) == 3)
                return true; // physical right-stick Y, SDL and generic joystick
        }
        return false;
    }
    bool block(bool listening, uint64_t token, bool down)
    {
        const bool retained = held.count(token) != 0;
        if (listening && down) held.insert(token);
        if (!down) held.erase(token);
        return listening || retained;
    }
private:
    std::unordered_set<uint64_t> held;
};

/* Key releases must reach the action map even after a text/capture owner
 * takes focus, so actions held before the transition cannot remain latched. */
inline bool pdguiKeyboardActionAllowed(bool pressed, bool textInput,
                                      bool bindingCapture, bool leakedCapture)
{
    return !pressed || !(textInput || bindingCapture || leakedCapture);
}

#endif
