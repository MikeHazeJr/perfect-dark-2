#ifndef PDGUI_TEXT_KEYBOARD_GATE_H
#define PDGUI_TEXT_KEYBOARD_GATE_H

#include <array>
#include "actionmap.h"

// The virtual navigation device is neutral while the OSK owns it. Remember
// each held mapped control until actual neutrality, so dismissal cannot turn
// that still-held input into a fresh press on the newly exposed native owner.
class PdguiTextKeyboardReleaseGate {
public:
    void reset() { retained_.fill(false); owner_ = scroll_ = false; }
    void update(bool owner, const std::array<bool, ACTION_COUNT> &held, bool scrollHeld)
    {
        owner_ = owner;
        for (int i = 0; i < ACTION_COUNT; ++i)
            retained_[i] = held[i] && (owner || retained_[i]);
        scroll_ = scrollHeld && (owner || scroll_);
    }
    bool allows(InputAction action) const
    {
        return !owner_ && action >= 0 && action < ACTION_COUNT && !retained_[action];
    }
    bool blocksScroll() const { return owner_ || scroll_; }
private:
    std::array<bool, ACTION_COUNT> retained_ = {};
    bool owner_ = false, scroll_ = false;
};

#endif
