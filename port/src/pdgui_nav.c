/**
 * pdgui_nav.c -- Gamepad navigation helpers for ImGui menus.
 *
 * Provides:
 *   1. D-pad wrapping via callback to ImGui's NavMoveRequestTryWrapping
 *   2. Centralized accept/cancel press queries (A/B or Enter/Escape)
 *   3. Last-input-device tracking with 500ms debounce
 *
 * This is C code with extern "C" linkage for C++ callers.
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#include "pdgui_nav.h"
#include <SDL.h>

/* ---- Device detection state ---- */

static s32 s_LastDevice = PDNAV_DEVICE_KBM;
static s32 s_RawDevice  = PDNAV_DEVICE_KBM;
static u32 s_DeviceChangeTime = 0;

#define DEVICE_DEBOUNCE_MS 500

/* ---- Accept/Cancel tracking ---- */

static s32 s_AcceptPressed = 0;
static s32 s_CancelPressed = 0;

/* ---- Wrap callback (set by C++ backend) ---- */

static void (*s_WrapCallback)(void) = NULL;

/* ========================================================================
 * Event processing — call for every SDL event
 * ======================================================================== */

void pdguiNavOnEvent(const SDL_Event *ev)
{
    if (!ev) {
        return;
    }

    s32 newRaw = s_RawDevice;

    switch (ev->type) {
    /* Keyboard / mouse → KBM */
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
    case SDL_TEXTINPUT:
        newRaw = PDNAV_DEVICE_KBM;
        break;

    /* Controller → Gamepad */
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        newRaw = PDNAV_DEVICE_GAMEPAD;
        break;

    case SDL_CONTROLLERAXISMOTION:
        /* Filter out dead zone noise */
        if (ev->caxis.value > -4000 && ev->caxis.value < 4000) {
            break;
        }
        newRaw = PDNAV_DEVICE_GAMEPAD;
        break;

    case SDL_JOYAXISMOTION:
        if (ev->jaxis.value > -4000 && ev->jaxis.value < 4000) {
            break;
        }
        newRaw = PDNAV_DEVICE_GAMEPAD;
        break;

    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        newRaw = PDNAV_DEVICE_GAMEPAD;
        break;

    default:
        break;
    }

    /* Track raw device changes with debounce */
    if (newRaw != s_RawDevice) {
        s_RawDevice = newRaw;
        s_DeviceChangeTime = SDL_GetTicks();
    }

    /* Accept/cancel on button-down only */
    if (ev->type == SDL_CONTROLLERBUTTONDOWN) {
        if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            s_AcceptPressed = 1;
        }
        if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
            s_CancelPressed = 1;
        }
    }
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat) {
        if (ev->key.keysym.sym == SDLK_RETURN ||
            ev->key.keysym.sym == SDLK_SPACE) {
            s_AcceptPressed = 1;
        }
        if (ev->key.keysym.sym == SDLK_ESCAPE) {
            s_CancelPressed = 1;
        }
    }
}

/* ========================================================================
 * D-pad wrapping
 * ======================================================================== */

void pdguiNavSetWrapCallback(void (*fn)(void))
{
    s_WrapCallback = fn;
}

void pdguiNavTickWrap(void)
{
    if (s_WrapCallback) {
        s_WrapCallback();
    }
}

/* ========================================================================
 * Frame lifecycle
 * ======================================================================== */

void pdguiNavEndFrame(void)
{
    s_AcceptPressed = 0;
    s_CancelPressed = 0;
}

/* ========================================================================
 * Accept / Cancel queries (non-destructive read — cleared at end of frame)
 * ======================================================================== */

s32 pdguiNavAcceptPressed(void)
{
    return s_AcceptPressed;
}

s32 pdguiNavCancelPressed(void)
{
    return s_CancelPressed;
}

/* ========================================================================
 * Device detection queries
 * ======================================================================== */

s32 pdguiNavGetLastDevice(void)
{
    if (s_RawDevice != s_LastDevice) {
        u32 now = SDL_GetTicks();
        if (now - s_DeviceChangeTime >= DEVICE_DEBOUNCE_MS) {
            s_LastDevice = s_RawDevice;
        }
    }
    return s_LastDevice;
}

s32 pdguiNavIsGamepad(void)
{
    return pdguiNavGetLastDevice() == PDNAV_DEVICE_GAMEPAD;
}
