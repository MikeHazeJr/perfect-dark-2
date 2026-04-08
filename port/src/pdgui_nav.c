/**
 * pdgui_nav.c -- D-pad wrapping utility for ImGui menus.
 *
 * M0.2 Phase C: Accept/cancel queries, device detection, and event processing
 * have been removed. Those are now handled by actionmap (actionPressed,
 * actionmapGetLastDevice). Only D-pad wrapping via callback remains.
 *
 * This is C code with extern "C" linkage for C++ callers.
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#include "pdgui_nav.h"

/* ---- Wrap callback (set by C++ backend) ---- */

static void (*s_WrapCallback)(void) = NULL;

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
