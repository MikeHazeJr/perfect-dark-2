#ifndef _IN_PDGUI_STORYBOARD_H
#define _IN_PDGUI_STORYBOARD_H

/**
 * pdgui_storyboard.h -- Menu Storyboard & Migration Preview System.
 *
 * Controller-navigable carousel that lists every PD menu dialog on the left
 * and renders a decoupled preview on the right.  Pressing X toggles between
 * the original PD renderer ("OLD") and a new ImGui rebuild ("NEW").
 * Pressing Y cycles a per-menu quality rating (Good / Fine / Incomplete / Redo)
 * that persists across sessions via JSON.
 *
 * Phase D4 of the modernization roadmap.  See context/menu-storyboard.md
 * (ADR-001) for the full architecture spec.
 *
 * Build gate: compiled unconditionally (GLOB_RECURSE picks it up) but the
 * F11 toggle is runtime-gated by --dev or a future PD_DEV_PREVIEW define.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------ Lifecycle ------ */

/* One-time init (call from pdguiInit after font + style are ready). */
void pdguiStoryboardInit(void);

/* Tear down, flush ratings to disk. */
void pdguiStoryboardShutdown(void);

/* ------ Per-frame ------ */

/* Render the storyboard.  Only call when pdguiStoryboardIsActive().
 * winW/winH are the SDL window size for layout calculations. */
void pdguiStoryboardRender(s32 winW, s32 winH);

/* ------ Input ------ */

/* Process an SDL event.  Returns 1 if consumed (suppress PD input). */
s32 pdguiStoryboardProcessEvent(void *sdlEvent);

/* ------ Toggle / Query ------ */

/* Toggle visibility (bound to F11). */
void pdguiStoryboardToggle(void);

/* Is the storyboard currently visible? */
s32 pdguiStoryboardIsActive(void);

/* ------ Quality Rating ------ */

/* Rating states stored per-menu. */
#define PD_RATING_UNRATED     0
#define PD_RATING_GOOD        1
#define PD_RATING_FINE        2
#define PD_RATING_INCOMPLETE  3
#define PD_RATING_REDO        4

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_STORYBOARD_H */
