/**
 * inputctx.h -- Priority-based input context stack (pushdown automaton).
 *
 * Replaces the binary INPUTMODE_MENU/INPUTMODE_GAMEPLAY system with a
 * stack of input contexts. Events dispatch top-to-bottom; first context
 * whose can_consume() returns true gets the event. Contexts marked for
 * removal are cleaned up at frame end (never mid-frame).
 *
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#ifndef _IN_INPUTCTX_H
#define _IN_INPUTCTX_H

#include <PR/ultratypes.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INPUTCTX_MAX_STACK 16

typedef struct InputContext InputContext;

/**
 * Input context -- a layer in the input priority stack.
 * Higher position in stack = higher priority = processes events first.
 */
struct InputContext {
    const char *name;                                            /* Debug name: "gameplay", "pause_menu", etc. */

    /* Event handling */
    s32 (*can_consume)(InputContext *self, const SDL_Event *ev);  /* Return 1 if this context wants this event */
    s32 (*on_event)(InputContext *self, const SDL_Event *ev);     /* Handle the event. Return 1 if consumed. */

    /* Per-frame polling for continuous input (movement sticks, held keys) */
    void (*on_poll)(InputContext *self);

    /* Lifecycle callbacks */
    void (*on_push)(InputContext *self);                          /* Called when context is pushed (e.g., show cursor) */
    void (*on_pop)(InputContext *self);                           /* Called when context is popped (e.g., hide cursor) */

    /* Internal state -- managed by the stack, do not set directly */
    s32 active;
    s32 marked_for_removal;
    u32 push_tick;           /* SDL_GetTicks() at push time — for key suppression grace period */
};

/* Stack lifecycle */
void inputCtxInit(void);
void inputCtxShutdown(void);

/* Push a context onto the top of the stack. Calls ctx->on_push(). */
void inputCtxPush(InputContext *ctx);

/* Mark a context for deferred removal. Actual pop happens in inputCtxEndFrame(). */
void inputCtxPopDeferred(InputContext *ctx);

/* Immediately pop the top context. Use sparingly -- prefer PopDeferred for safety. */
void inputCtxPopImmediate(void);

/* Dispatch a single SDL event through the stack (top to bottom).
 * Returns 1 if any context consumed the event, 0 otherwise. */
s32 inputCtxDispatch(const SDL_Event *ev);

/* Per-frame polling -- calls on_poll() on the top active context only. */
void inputCtxPollFrame(void);

/* End-of-frame cleanup -- actually removes contexts marked for removal. */
void inputCtxEndFrame(void);

/* Query the stack */
InputContext *inputCtxGetTop(void);           /* Returns top active context, or NULL */
s32 inputCtxIsActive(InputContext *ctx);       /* Is this context on the stack? */
s32 inputCtxGetDepth(void);                    /* Current stack depth */
const char *inputCtxGetTopName(void);          /* Name of top context, or "none" */

/* Key suppression: returns 1 if the top context was pushed within the last
 * INPUTCTX_PUSH_GRACE_MS milliseconds and the event is a KEY_DOWN.
 * Callers should skip forwarding such events to ImGui to prevent the
 * triggering key from being seen as a new press by the pushed context. */
#define INPUTCTX_PUSH_GRACE_MS 100
s32 inputCtxShouldSuppressKey(const SDL_Event *ev);

/* Mouse mode sync: ensures SDL relative mouse mode matches the top context.
 * Called automatically from inputCtxEndFrame(). Can be called manually if
 * something outside the context system may have changed SDL mouse state. */
void inputCtxSyncMouseMode(void);

/* ---- Gameplay input authority predicate (ADR 2026-04-13) ----
 *
 * Central truth-source for "can gameplay-only actions be read right now?"
 *
 * Returns 1 (SUPPRESS) when ANY of the following is true:
 *   (a) Top context is not &g_CtxGameplay (menu, pause, debug, textinput...)
 *   (b) SDL window focus is currently lost (alt-tab, minimised, other-app)
 *   (c) Focus was regained within the last INPUTCTX_FOCUS_SETTLE_MS — this
 *       forces a "settle" window so held keys from before alt-tab are not
 *       synthesised as fresh presses by SDL's auto-repeat behaviour; the
 *       user must release and re-press to count.
 *
 * All gameplay-input consumers (actionmap read API, bondmove, etc.) must
 * early-return on suppression rather than trusting the caller. See ADR
 * context/designs/input-authority-and-menu-pool-2026-04-13.md §3. */
#define INPUTCTX_FOCUS_SETTLE_MS 50
s32 gameplayInputSuppressed(void);

/* Focus tracking -- called by the SDL backend on WINDOWEVENT_FOCUS_LOST/GAINED.
 * gained != 0 means focus just came back; 0 means focus was just lost. */
void inputCtxNotifyFocus(s32 gained);

/* Returns 1 if the SDL window currently does not have OS focus. */
s32 inputCtxIsFocusLost(void);

/* ---- Built-in contexts ---- */

/* Gameplay context: game owns all input. Mouse captured (relative mode).
 * Keyboard/mouse/controller all route to game logic.
 * This is the bottom-of-stack default. */
extern InputContext g_CtxGameplay;

/* ImGui menu context: ImGui owns input. Mouse in absolute mode, cursor visible.
 * Keyboard, mouse, and controller events routed to ImGui.
 * Pushed when any menu opens, popped when all menus close. */
extern InputContext g_CtxImGuiMenu;

/* Pause menu context: specialization of ImGui menu for in-game pause.
 * Same input routing as ImGuiMenu but with pause-specific lifecycle
 * (e.g., game tick pauses, specific Esc behavior). */
extern InputContext g_CtxPauseMenu;

/* Debug overlay context: F12 debug overlay. Highest priority.
 * Only pushed when debug overlay is active. */
extern InputContext g_CtxDebugOverlay;

#ifdef __cplusplus
}
#endif

#endif /* _IN_INPUTCTX_H */
