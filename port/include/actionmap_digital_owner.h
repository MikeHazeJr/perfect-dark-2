#ifndef PD_ACTIONMAP_DIGITAL_OWNER_H
#define PD_ACTIONMAP_DIGITAL_OWNER_H

#include "actionmap.h"
#include <cstdint>
#include <unordered_map>

/* A physical edge keeps its winning action until its own release. Device
 * instance is part of the key, so another controller cannot release it. */
enum class ActionSourceKind : uint8_t {
    Keyboard = 1, MouseButton, Wheel, ControllerButton, RawButton,
    ControllerAxis, RawAxis
};

inline uint64_t actionSourceKey(ActionSourceKind kind, int32_t instance, uint16_t control)
{
    return (uint64_t(uint8_t(kind)) << 56) |
           (uint64_t(uint32_t(instance)) << 16) | uint64_t(control);
}

struct ActionDigitalOwner {
    s32 player;
    InputAction action;
    const InputMappingContext *context;
};

struct ActionDigitalRelease {
    ActionDigitalOwner owner{};
    bool valid = false;
    bool last = false;
};

class ActionDigitalOwners {
public:
    struct Press { bool accepted; bool first; };

    Press press(uint64_t source, s32 player, InputAction action,
               const InputMappingContext *context)
    {
        if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS ||
            action < 0 || action >= ACTION_COUNT || !context) return {false, false};
        auto inserted = owners_.emplace(source, ActionDigitalOwner{player, action, context});
        if (!inserted.second) return {false, false};
        s32 &count = counts_[player][action];
        return {true, ++count == 1};
    }

    ActionDigitalRelease release(uint64_t source)
    {
        auto it = owners_.find(source);
        if (it == owners_.end()) return {};
        ActionDigitalRelease result;
        result.owner = it->second;
        result.valid = true;
        s32 &count = counts_[result.owner.player][result.owner.action];
        result.last = --count == 0;
        owners_.erase(it);
        return result;
    }

    template <typename Predicate, typename OnRelease>
    void retireWhere(Predicate matches, OnRelease onRelease)
    {
        for (auto it = owners_.begin(); it != owners_.end();) {
            if (!matches(it->first, it->second)) { ++it; continue; }
            ActionDigitalRelease result;
            result.owner = it->second;
            result.valid = true;
            result.last = --counts_[result.owner.player][result.owner.action] == 0;
            it = owners_.erase(it);
            onRelease(result);
        }
    }

    void clear()
    {
        owners_.clear();
        for (auto &player : counts_) for (s32 &count : player) count = 0;
    }

    s32 count(s32 player, InputAction action) const { return counts_[player][action]; }

private:
    std::unordered_map<uint64_t, ActionDigitalOwner> owners_;
    s32 counts_[ACTIONMAP_MAX_PLAYERS][ACTION_COUNT] = {};
};

#endif
