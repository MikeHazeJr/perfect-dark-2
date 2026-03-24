/**
 * inputmodes.c -- Per-action input modifiers (single press, double-tap, hold).
 *
 * Inspired by Call of Duty's "Contextual Tap" system: each bindable action
 * can optionally require a double-tap or hold instead of a single press.
 * This allows actions sharing the same key to be disambiguated by input mode.
 *
 * Example: Interact = X (double-tap, 0.8s), Reload = X (single press).
 * A quick tap of X reloads; two taps within 0.8s triggers interact.
 *
 * Storage: 32 actions × (mode + timing) in pd.ini via config system.
 * Runtime state tracked per-action for double-tap detection.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include "platform.h"
#include "config.h"

/* Input mode enum: matches the ImGui combo indices in the settings UI */
#define INPUTMODE_SINGLE    0  /* Normal single press */
#define INPUTMODE_DOUBLETAP 1  /* Press twice within timing window */
#define INPUTMODE_HOLD      2  /* Hold for timing duration */

#define INPUTMODE_MAX_ACTIONS 32

/* Per-action mode and timing.
 * mode:   0=single, 1=double-tap, 2=hold
 * timing: seconds (0.25–1.5), only used when mode != 0 */
static s32 g_InputMode[INPUTMODE_MAX_ACTIONS];
static f32 g_InputTiming[INPUTMODE_MAX_ACTIONS];

/* Default timing value for new modifiers */
#define INPUTMODE_DEFAULT_TIMING 0.5f

/* ---- Config registration ---- */

static void inputModesConfigInit(void)
{
    char nameBuf[64];

    for (s32 i = 0; i < INPUTMODE_MAX_ACTIONS; i++) {
        g_InputMode[i] = INPUTMODE_SINGLE;
        g_InputTiming[i] = INPUTMODE_DEFAULT_TIMING;

        snprintf(nameBuf, sizeof(nameBuf), "InputMode.Action%d.Mode", i);
        configRegisterInt(nameBuf, &g_InputMode[i], INPUTMODE_SINGLE, INPUTMODE_HOLD);

        snprintf(nameBuf, sizeof(nameBuf), "InputMode.Action%d.Timing", i);
        configRegisterFloat(nameBuf, &g_InputTiming[i], 0.25f, 1.5f);
    }
}

PD_CONSTRUCTOR static void inputModesInit(void)
{
    inputModesConfigInit();
}

/* ---- Public API ---- */

s32 inputModeGet(u32 ck)
{
    if (ck >= INPUTMODE_MAX_ACTIONS) return INPUTMODE_SINGLE;
    return g_InputMode[ck];
}

void inputModeSet(u32 ck, s32 mode)
{
    if (ck >= INPUTMODE_MAX_ACTIONS) return;
    if (mode < INPUTMODE_SINGLE) mode = INPUTMODE_SINGLE;
    if (mode > INPUTMODE_HOLD) mode = INPUTMODE_HOLD;
    g_InputMode[ck] = mode;
}

f32 inputModeGetTiming(u32 ck)
{
    if (ck >= INPUTMODE_MAX_ACTIONS) return INPUTMODE_DEFAULT_TIMING;
    return g_InputTiming[ck];
}

void inputModeSetTiming(u32 ck, f32 seconds)
{
    if (ck >= INPUTMODE_MAX_ACTIONS) return;
    if (seconds < 0.25f) seconds = 0.25f;
    if (seconds > 1.5f) seconds = 1.5f;
    g_InputTiming[ck] = seconds;
}
