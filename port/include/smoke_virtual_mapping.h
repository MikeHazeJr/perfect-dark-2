#ifndef SMOKE_VIRTUAL_MAPPING_H
#define SMOKE_VIRTUAL_MAPPING_H

#include <SDL.h>
#include <stdio.h>

/* Only the fixture-owned virtual device receives this process-local mapping.
 * SDL's automatic virtual mapping omits Touchpad on SDL 2.32.10 even when the
 * descriptor advertises that button. Keep strict identity verification. */
static inline int smokeInstallVirtualMapping(int device_index)
{
    char guid[33];
    char mapping[1024];
    int used;
    if (!SDL_JoystickIsVirtual(device_index)) return 0;
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device_index), guid, sizeof(guid));
    used = snprintf(mapping, sizeof(mapping), "%s,PD2 Smoke Virtual Controller,", guid);
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
        const char *name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)i);
        int written;
        if (!name) return 0;
        written = snprintf(mapping + used, sizeof(mapping) - used, "%s:b%d,", name, i);
        if (written < 0 || written >= (int)sizeof(mapping) - used) return 0;
        used += written;
    }
    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
        const char *name = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)i);
        int written;
        if (!name) return 0;
        written = snprintf(mapping + used, sizeof(mapping) - used, "%s:a%d,", name, i);
        if (written < 0 || written >= (int)sizeof(mapping) - used) return 0;
        used += written;
    }
    return SDL_GameControllerAddMapping(mapping) >= 0;
}

#endif
