#ifndef PD_INPUT_DEVICE_IDENTITY_H
#define PD_INPUT_DEVICE_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum InputDeviceKind { INPUT_DEVICE_MKB = 0, INPUT_DEVICE_GAMEPAD = 1 };
enum InputGlyphFamily {
    INPUT_GLYPH_GENERIC = 0,
    INPUT_GLYPH_XBOX360,
    INPUT_GLYPH_XBOXONE,
    INPUT_GLYPH_PS3,
    INPUT_GLYPH_PS4,
    INPUT_GLYPH_PS5,
    INPUT_GLYPH_SWITCH_PRO,
    INPUT_GLYPH_JOYCON_LEFT,
    INPUT_GLYPH_JOYCON_RIGHT,
    INPUT_GLYPH_JOYCON_PAIR
};
enum InputDeviceActivityKind {
    INPUT_ACTIVITY_PRESS,
    INPUT_ACTIVITY_TEXT,
    INPUT_ACTIVITY_POINTER,
    INPUT_ACTIVITY_WHEEL,
    INPUT_ACTIVITY_AXIS,
    INPUT_ACTIVITY_RELEASE
};

/* Facts come from the input owner's existing handles; this module never
 * opens a device, dispatches an action, or reads SDL/global UI state. */
typedef struct InputDeviceIdentity {
    int kind;
    int input_class; /* Existing ActionMap input-class value, kept opaque here. */
    int family;
    int standard_controller; /* false for raw/custom/accessibility devices */
    int32_t instance_id;
    int player;
    int connected;
    uint32_t button_mask; /* Existing handle button ordinals 0..31. */
    uint32_t axis_mask; /* Existing handle axes 0..5, mapped to VK slots 22..31. */
    int nintendo_button_labels;
    int joycon_vertical_mode; /* SDL's startup-only single-Joy-Con mode */
} InputDeviceIdentity;

#define INPUT_DEVICE_IDENTITY_PLAYERS 4
typedef struct InputDeviceIdentityState {
    InputDeviceIdentity last;
    InputDeviceIdentity controller[INPUT_DEVICE_IDENTITY_PLAYERS];
} InputDeviceIdentityState;

typedef struct InputDeviceActivity {
    int kind;
    int value;
    int prior_value; /* last meaningful axis position, or zero after neutral */
} InputDeviceActivity;

InputDeviceIdentity inputDeviceKeyboardIdentity(void);
void inputDeviceIdentityReset(InputDeviceIdentityState *state);
int inputDeviceActivityMeaningful(const InputDeviceActivity *activity);
/* Returns one when the visible player-0 identity changed. Other players'
 * controller records update without changing player-0/menu prompts. */
int inputDeviceIdentityObserve(InputDeviceIdentityState *state,
    const InputDeviceIdentity *identity, const InputDeviceActivity *activity);
void inputDeviceIdentityDisconnect(InputDeviceIdentityState *state, int32_t instance_id);

/* Canonical SDL controller button slots 0..20; 22..31 are the ActionMap's
 * synthetic left/right-stick directions and triggers. Raw custom controls
 * retain their real BtnN / AxisN naming regardless of any family hint. */
int inputGlyphControllerLabel(const InputDeviceIdentity *identity, int slot,
    char *out, size_t out_size);
int inputGlyphControllerSlotPresent(const InputDeviceIdentity *identity, int slot);
/* Typed VK lookup distinguishes new raw high buttons from digital axes.
 * Old overlapping VKs retain an explicit combined-control label. */
int inputGlyphControllerVkPresent(const InputDeviceIdentity *identity, uint32_t vk);
int inputGlyphControllerVkLabel(const InputDeviceIdentity *identity, uint32_t vk,
    char *out, size_t out_size);

typedef struct InputGlyphBindingChoice {
    uint32_t vk;
    int kind;
    int alternate_device;
    int controller_absent;
} InputGlyphBindingChoice;

/* Both VKs must already have passed the production action-binding resolver.
 * A disconnected controller can never win merely because a bind exists. */
InputGlyphBindingChoice inputGlyphChooseBinding(int preferred_device,
    uint32_t keyboard_vk, uint32_t controller_vk, int controller_available);
int inputGlyphFormatBindingLabel(const InputGlyphBindingChoice *choice,
    const char *short_label, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
#endif
