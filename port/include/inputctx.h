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
