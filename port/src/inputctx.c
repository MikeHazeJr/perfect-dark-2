/**
 * inputctx.c -- Priority-based input context stack implementation.
 *
 * Pushdown automaton for input routing. Contexts are pushed/popped on a
 * stack. Events dispatch top-to-bottom; first context whose can_consume()
 * returns true gets the event. Contexts marked for removal are cleaned up
 * at frame end (never mid-frame).
 *
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#include <string.h>
#include <PR/ultratypes.h>
#include <PR/os_thread.h>
#include <PR/os_cont.h>
#include <SDL.h>
#include "input.h"
#include "inputctx.h"
#include "system.h"

/* ---- Stack storage ---- */

static InputContext *s_Stack[INPUTCTX_MAX_STACK];
static s32 s_Depth = 0;

/* ---- Pause state for g_CtxPauseMenu ---- */

static s32 s_GamePaused = 0;

/* ---- Stack API ---- */

void inputCtxInit(void)
{
    memset(s_Stack, 0, sizeof(s_Stack));
    s_Depth = 0;
    s_GamePaused = 0;
    sysLogPrintf(LOG_NOTE, "INPUTCTX: initialized");
}

void inputCtxShutdown(void)
{
    for (s32 i = s_Depth - 1; i >= 0; i--) {
        /* M-L1: Guard all dereferences — s_Stack[i] could be NULL. */
        if (s_Stack[i]) {
            if (s_Stack[i]->on_pop) {
                s_Stack[i]->on_pop(s_Stack[i]);
            }
            s_Stack[i]->active = 0;
            s_Stack[i]->marked_for_removal = 0;
        }
    }
    s_Depth = 0;
    s_GamePaused = 0;
    sysLogPrintf(LOG_NOTE, "INPUTCTX: shutdown");
}

void inputCtxPush(InputContext *ctx)
{
    if (!ctx) {
        sysLogPrintf(LOG_WARNING, "INPUTCTX: push called with NULL context");
        return;
    }

    if (s_Depth >= INPUTCTX_MAX_STACK) {
        sysLogPrintf(LOG_ERROR, "INPUTCTX: stack overflow (max %d), cannot push '%s'",
                     INPUTCTX_MAX_STACK, ctx->name ? ctx->name : "?");
        return;
    }

    /* Prevent double-push */
    for (s32 i = 0; i < s_Depth; i++) {
        if (s_Stack[i] == ctx) {
            sysLogPrintf(LOG_WARNING, "INPUTCTX: '%s' already on stack at depth %d, ignoring push",
                         ctx->name ? ctx->name : "?", i);
            return;
        }
    }

    ctx->active = 1;
    ctx->marked_for_removal = 0;
    ctx->push_tick = SDL_GetTicks();

    s_Stack[s_Depth] = ctx;
    s_Depth++;

    if (ctx->on_push) {
        ctx->on_push(ctx);
    }

    sysLogPrintf(LOG_NOTE, "INPUTCTX: pushed '%s' (depth now %d)",
                 ctx->name ? ctx->name : "?", s_Depth);
}

void inputCtxPopDeferred(InputContext *ctx)
{
    if (!ctx) {
        return;
    }

    for (s32 i = 0; i < s_Depth; i++) {
        if (s_Stack[i] == ctx) {
            ctx->marked_for_removal = 1;
            sysLogPrintf(LOG_NOTE, "INPUTCTX: '%s' marked for deferred removal",
                         ctx->name ? ctx->name : "?");
            return;
        }
    }

    sysLogPrintf(LOG_WARNING, "INPUTCTX: popDeferred called for '%s' which is not on stack",
                 ctx->name ? ctx->name : "?");
}

void inputCtxPopImmediate(void)
{
    if (s_Depth <= 0) {
        sysLogPrintf(LOG_WARNING, "INPUTCTX: popImmediate called on empty stack");
        return;
    }

    s_Depth--;
    InputContext *ctx = s_Stack[s_Depth];
    s_Stack[s_Depth] = NULL;

    if (ctx) {
        if (ctx->on_pop) {
            ctx->on_pop(ctx);
        }
        ctx->active = 0;
        ctx->marked_for_removal = 0;

        sysLogPrintf(LOG_NOTE, "INPUTCTX: immediately popped '%s' (depth now %d)",
                     ctx->name ? ctx->name : "?", s_Depth);
    }
}

s32 inputCtxDispatch(const SDL_Event *ev)
{
    if (!ev) {
        return 0;
    }

    /* Walk stack from top to bottom */
    for (s32 i = s_Depth - 1; i >= 0; i--) {
        InputContext *ctx = s_Stack[i];
        if (!ctx || ctx->marked_for_removal) {
            continue;
        }

        if (ctx->can_consume && ctx->can_consume(ctx, ev)) {
            if (ctx->on_event) {
                return ctx->on_event(ctx, ev);
            }
            return 1;
        }
    }

    return 0;
}

void inputCtxPollFrame(void)
{
    /* Call on_poll() on the topmost active (non-marked) context only */
    for (s32 i = s_Depth - 1; i >= 0; i--) {
        InputContext *ctx = s_Stack[i];
        if (!ctx || ctx->marked_for_removal) {
            continue;
        }

        if (ctx->on_poll) {
            ctx->on_poll(ctx);
        }
        return; /* Only the topmost active context polls */
    }
}

void inputCtxEndFrame(void)
{
    /* Remove all contexts marked for removal, compacting the array */
    s32 write = 0;
    for (s32 read = 0; read < s_Depth; read++) {
        InputContext *ctx = s_Stack[read];
        if (ctx && ctx->marked_for_removal) {
            if (ctx->on_pop) {
                ctx->on_pop(ctx);
            }
            ctx->active = 0;
            ctx->marked_for_removal = 0;
            sysLogPrintf(LOG_NOTE, "INPUTCTX: deferred pop of '%s'",
                         ctx->name ? ctx->name : "?");
        } else {
            s_Stack[write] = ctx;
            write++;
        }
    }

    /* Clear trailing slots */
    for (s32 i = write; i < s_Depth; i++) {
        s_Stack[i] = NULL;
    }

    s_Depth = write;

    /* Ensure SDL mouse mode matches the current top context.
     * This catches any case where something outside the context system
     * changed SDL state (e.g., deferred mouse lock, hotswap transitions). */
    inputCtxSyncMouseMode();
}

/* ---- Query API ---- */

InputContext *inputCtxGetTop(void)
{
    for (s32 i = s_Depth - 1; i >= 0; i--) {
        if (s_Stack[i] && !s_Stack[i]->marked_for_removal) {
            return s_Stack[i];
        }
    }
    return NULL;
}

s32 inputCtxIsActive(InputContext *ctx)
{
    if (!ctx) {
        return 0;
    }

    for (s32 i = 0; i < s_Depth; i++) {
        if (s_Stack[i] == ctx && !ctx->marked_for_removal) {
            return 1;
        }
    }
    return 0;
}

s32 inputCtxGetDepth(void)
{
    return s_Depth;
}

const char *inputCtxGetTopName(void)
{
    InputContext *top = inputCtxGetTop();
    if (top && top->name) {
        return top->name;
    }
    return "none";
}

/* ---- Key suppression ---- */

s32 inputCtxShouldSuppressKey(const SDL_Event *ev)
{
    if (!ev) {
        return 0;
    }

    /* Only suppress KEY_DOWN (not KEY_UP, mouse, etc.) */
    if (ev->type != SDL_KEYDOWN) {
        return 0;
    }

    InputContext *top = inputCtxGetTop();
    if (!top || top == &g_CtxGameplay) {
        return 0;
    }

    u32 now = SDL_GetTicks();
    u32 elapsed = now - top->push_tick;
    if (elapsed < INPUTCTX_PUSH_GRACE_MS) {
        return 1;
    }

    return 0;
}

/* ---- Mouse mode sync ---- */

void inputCtxSyncMouseMode(void)
{
    InputContext *top = inputCtxGetTop();
    if (!top) {
        return;
    }

    if (top == &g_CtxGameplay) {
        /* Gameplay: relative mouse, cursor hidden */
        if (!SDL_GetRelativeMouseMode()) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
            SDL_ShowCursor(SDL_DISABLE);
            sysLogPrintf(LOG_NOTE, "INPUTCTX: syncMouseMode -- restored relative mode for gameplay");
        }
    } else {
        /* Any menu/overlay context: absolute mouse, cursor visible */
        if (SDL_GetRelativeMouseMode()) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            sysLogPrintf(LOG_NOTE, "INPUTCTX: syncMouseMode -- restored absolute mode for '%s'",
                         top->name ? top->name : "?");
        }
    }
}

/* ======================================================================
 * Built-in context callbacks
 * ====================================================================== */

/* ---- g_CtxGameplay ---- */

static void gameplayOnPush(InputContext *self)
{
    (void)self;
    inputLockMouse(1);
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_ShowCursor(SDL_DISABLE);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: gameplay on_push -- mouse captured");
}

static void gameplayOnPop(InputContext *self)
{
    (void)self;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: gameplay on_pop -- mouse released");
}

static s32 gameplayCanConsume(InputContext *self, const SDL_Event *ev)
{
    (void)self;
    (void)ev;
    /* Gameplay eats all input -- it's the bottom of the stack */
    return 1;
}

static s32 gameplayOnEvent(InputContext *self, const SDL_Event *ev)
{
    (void)self;
    (void)ev;
    /* Return 0 -- let the existing game input pipeline (inputEventFilter in input.c) handle it.
     * The event is still "consumed" from the context stack's perspective because
     * can_consume returned 1, preventing lower contexts from seeing it. */
    return 0;
}

InputContext g_CtxGameplay = {
    .name        = "gameplay",
    .can_consume = gameplayCanConsume,
    .on_event    = gameplayOnEvent,
    .on_poll     = NULL,
    .on_push     = gameplayOnPush,
    .on_pop      = gameplayOnPop,
    .active      = 0,
    .marked_for_removal = 0,
};

/* ---- g_CtxImGuiMenu ---- */

static void imguiMenuOnPush(InputContext *self)
{
    (void)self;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: imgui_menu on_push -- mouse absolute, cursor visible");
}

static void imguiMenuOnPop(InputContext *self)
{
    (void)self;
    /* The context below (gameplay or another menu) handles its own mouse state via on_push */
    sysLogPrintf(LOG_NOTE, "INPUTCTX: imgui_menu on_pop");
}

static s32 imguiMenuCanConsume(InputContext *self, const SDL_Event *ev)
{
    (void)self;

    switch (ev->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTINPUT:
    case SDL_TEXTEDITING:
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERAXISMOTION:
    case SDL_JOYAXISMOTION:
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        return 1;
    default:
        return 0;
    }
}

static s32 imguiMenuOnEvent(InputContext *self, const SDL_Event *ev)
{
    (void)self;
    (void)ev;
    /* Return 1 (consumed). The actual ImGui forwarding happens in the
     * integration layer (pdgui_backend.cpp, Phase 1 Session 2) which
     * is C++ and can call ImGui_ImplSDL2_ProcessEvent() directly. */
    return 1;
}

InputContext g_CtxImGuiMenu = {
    .name        = "imgui_menu",
    .can_consume = imguiMenuCanConsume,
    .on_event    = imguiMenuOnEvent,
    .on_poll     = NULL,
    .on_push     = imguiMenuOnPush,
    .on_pop      = imguiMenuOnPop,
    .active      = 0,
    .marked_for_removal = 0,
};

/* ---- g_CtxPauseMenu ---- */

static void pauseMenuOnPush(InputContext *self)
{
    (void)self;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    s_GamePaused = 1;
    sysLogPrintf(LOG_NOTE, "INPUTCTX: pause_menu on_push -- game paused, cursor visible");
}

static void pauseMenuOnPop(InputContext *self)
{
    (void)self;
    s_GamePaused = 0;
    sysLogPrintf(LOG_NOTE, "INPUTCTX: pause_menu on_pop -- game unpaused");
}

static s32 pauseMenuCanConsume(InputContext *self, const SDL_Event *ev)
{
    /* Same as ImGui menu -- consume all keyboard/mouse/gamepad */
    return imguiMenuCanConsume(self, ev);
}

static s32 pauseMenuOnEvent(InputContext *self, const SDL_Event *ev)
{
    (void)self;
    (void)ev;
    /* Consumed. Actual ImGui forwarding in integration layer. */
    return 1;
}

InputContext g_CtxPauseMenu = {
    .name        = "pause_menu",
    .can_consume = pauseMenuCanConsume,
    .on_event    = pauseMenuOnEvent,
    .on_poll     = NULL,
    .on_push     = pauseMenuOnPush,
    .on_pop      = pauseMenuOnPop,
    .active      = 0,
    .marked_for_removal = 0,
};

/* ---- g_CtxDebugOverlay ---- */

static void debugOverlayOnPush(InputContext *self)
{
    (void)self;
    SDL_ShowCursor(SDL_ENABLE);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: debug_overlay on_push -- cursor visible");
}

static void debugOverlayOnPop(InputContext *self)
{
    (void)self;
    sysLogPrintf(LOG_NOTE, "INPUTCTX: debug_overlay on_pop");
}

static s32 debugOverlayCanConsume(InputContext *self, const SDL_Event *ev)
{
    (void)self;

    /* Debug overlay only consumes keyboard and mouse events */
    switch (ev->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTINPUT:
    case SDL_TEXTEDITING:
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
        return 1;
    default:
        return 0;
    }
}

static s32 debugOverlayOnEvent(InputContext *self, const SDL_Event *ev)
{
    (void)self;
    (void)ev;
    /* Consumed. Actual ImGui forwarding in integration layer. */
    return 1;
}

InputContext g_CtxDebugOverlay = {
    .name        = "debug_overlay",
    .can_consume = debugOverlayCanConsume,
    .on_event    = debugOverlayOnEvent,
    .on_poll     = NULL,
    .on_push     = debugOverlayOnPush,
    .on_pop      = debugOverlayOnPop,
    .active      = 0,
    .marked_for_removal = 0,
};
