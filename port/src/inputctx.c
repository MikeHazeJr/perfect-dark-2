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
#include "actionmap.h"
#include "menupool.h"
#include "system.h"

/* ---- Stack storage ---- */

static InputContext *s_Stack[INPUTCTX_MAX_STACK];
static s32 s_Depth = 0;

/* ---- Pause state for g_CtxPauseMenu ---- */

static s32 s_GamePaused = 0;

/* ---- Window focus tracking (ADR 2026-04-13) ----
 *
 * s_WindowFocusLost is set by the SDL backend on WINDOWEVENT_FOCUS_LOST and
 * cleared on WINDOWEVENT_FOCUS_GAINED.  s_FocusRegainTick captures the ms
 * timestamp of the regain so we can impose an INPUTCTX_FOCUS_SETTLE_MS
 * quiet window during which gameplay-input reads return suppressed.
 *
 * Why: SDL repeats held keys on focus regain as if they were fresh presses.
 * Without a settle window, alt-tab → click-back would replay W as "just
 * pressed" and move the player. Users must release + re-press to count. */
static s32 s_WindowFocusLost = 0;
static u32 s_FocusRegainTick = 0;

/* ---- Issue 11 (2026-04-24): push / pop history ring ----
 *
 * Small circular buffer recording the last N ctx transitions with
 * timestamps so the F9 debug overlay can show them.  Helps triage
 * input-routing bugs like Mike's Issue 3 ("pause menu input doesn't
 * work") where a ctx push might fire and immediately get popped
 * (or another ctx pushes on top) -- invisible from the live stack
 * snapshot alone.
 *
 * Oldest-first read order: callers pass a destination array of
 * at most INPUTCTX_HISTORY_SIZE slots; the copy helper writes the
 * N most-recent events in chronological order and returns N.
 * Names are pointers into InputContext.name strings which are
 * static-lifetime in this codebase, so the ring holds stable
 * pointers -- no snapshot copy needed. */
#define INPUTCTX_HISTORY_SIZE 16

typedef struct {
    u32         timestamp_ms;
    const char *name;
    s32         depth_after;
    char        event; /* 'P' push, 'M' marked-for-removal, 'R' real-pop, 'X' resurrect */
} InputCtxHistoryEntry;

static InputCtxHistoryEntry s_History[INPUTCTX_HISTORY_SIZE];
static s32                  s_HistoryHead  = 0;   /* next write slot */
static s32                  s_HistoryCount = 0;   /* valid entries, capped at SIZE */

static void s_historyRecord(char event, const InputContext *ctx, s32 depth_after)
{
    InputCtxHistoryEntry *e = &s_History[s_HistoryHead];
    e->timestamp_ms = SDL_GetTicks();
    e->name         = (ctx && ctx->name) ? ctx->name : "?";
    e->depth_after  = depth_after;
    e->event        = event;
    s_HistoryHead = (s_HistoryHead + 1) % INPUTCTX_HISTORY_SIZE;
    if (s_HistoryCount < INPUTCTX_HISTORY_SIZE) {
        s_HistoryCount++;
    }
}

/* ---- Stack API ---- */

void inputCtxInit(void)
{
    memset(s_Stack, 0, sizeof(s_Stack));
    s_Depth = 0;
    s_GamePaused = 0;
    s_WindowFocusLost = 0;
    s_FocusRegainTick = 0;
    /* Phase 2: bring up the menu pool so menuPushDialog / ImGui renderers
     * can immediately consult it. menupoolInit is idempotent, so repeated
     * inputCtxInit calls (stage-transition nuclear reset) are fine. */
    menupoolInit();
    /* Stage-transition reset path also gets here after an inputCtxShutdown.
     * Ensure no pool slots survive that reset — inputCtxShutdown popped
     * every context, so any slot that was holding a context pop is stale. */
    menupoolReleaseAll();
    sysLogPrintf(LOG_NOTE, "INPUTCTX: initialized");
}

void inputCtxShutdown(void)
{
    /* Phase 2: release every pool slot FIRST. If a pool slot owns a
     * context pop and we tear the stack down without consulting the
     * pool, the slot's "owned_ctx" pointer dangles — the next
     * menupoolRelease for that type would call inputCtxPopDeferred on
     * a context that's already been physically popped.  Releasing
     * first makes the slot→ctx relationship observe the nuclear reset
     * cleanly. */
    menupoolReleaseAll();

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

    /* Prevent double-push.
     *
     * S197a: a previously-popped (marked-for-removal) entry is still physically
     * on the stack until the next inputCtxEndFrame() compacts it out.  The old
     * check did not distinguish between that and a truly live duplicate, so
     * an in-frame pop-then-push sequence was refused as "already on stack".
     * Effect: one of the menu->menu transitions observed in the S197a log
     * (<10 ms push/pop/push on imgui_menu) left the stack in a semi-stale
     * state that surfaced as a main-menu instance flicker.
     *
     * Fix: treat a marked-for-removal entry as "resurrect" — un-mark it and
     * re-sync mouse mode (which popDeferred already switched to the next
     * context underneath).  The original on_push side effects are still in
     * place, so we deliberately do NOT call on_push again. */
    for (s32 i = 0; i < s_Depth; i++) {
        if (s_Stack[i] == ctx) {
            if (ctx->marked_for_removal) {
                ctx->marked_for_removal = 0;
                ctx->push_tick = SDL_GetTicks();
                inputCtxSyncMouseMode();
                /* Resurrecting a non-gameplay context: flush gameplay state
                 * again — we may have been in gameplay briefly between the
                 * deferred pop and the resurrect push. See ADR §3. */
                if (ctx != &g_CtxGameplay) {
                    actionmapFlushGameplayState();
                }
                sysLogPrintf(LOG_NOTE,
                    "INPUTCTX: push on marked '%s' at depth %d — un-marked for removal (resurrect)",
                    ctx->name ? ctx->name : "?", i);
                s_historyRecord('X', ctx, s_Depth);
                return;
            }
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

    /* Non-gameplay push: synthesise key-up for every held gameplay action.
     * Without this, W held at menu-open would remain "held" in s_State and
     * bondmove would see the player walking once the menu closed. See ADR
     * context/designs/input-authority-and-menu-pool-2026-04-13.md §3. */
    if (ctx != &g_CtxGameplay) {
        actionmapFlushGameplayState();
    }

    sysLogPrintf(LOG_NOTE, "INPUTCTX: pushed '%s' (depth now %d)",
                 ctx->name ? ctx->name : "?", s_Depth);
    s_historyRecord('P', ctx, s_Depth);
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
            s_historyRecord('M', ctx, s_Depth);

            /* Immediately sync mouse mode so the cursor/capture state reflects
             * the new effective top context THIS frame, not next frame.
             * inputCtxGetTop() already skips marked-for-removal contexts, so
             * the sync sees the correct top (e.g. g_CtxGameplay underneath).
             * Without this, there's a 1-frame gap where the mouse stays in
             * absolute mode after a menu closes, causing visible cursor flash
             * and one frame of lost mouse input. */
            inputCtxSyncMouseMode();
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
        s_historyRecord('R', ctx, s_Depth);
    }

    /* Sync mouse mode to reflect the new top context immediately. */
    inputCtxSyncMouseMode();
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

/* S295 F3: Watchdog state for end-of-frame sanity checks. */
#define INPUTCTX_WATCHDOG_DEEP_THRESHOLD 5      /* depths >= this get warned */
#define INPUTCTX_WATCHDOG_FORCE_RESET    (INPUTCTX_MAX_STACK - 1) /* near-overflow = pathology */
#define INPUTCTX_WATCHDOG_LOG_COOLDOWN_MS 1000

static u32 s_WatchdogLastLogMs = 0;

static s32 inputctxWatchdogShouldLog(void)
{
    u32 now = SDL_GetTicks();
    if (now - s_WatchdogLastLogMs >= INPUTCTX_WATCHDOG_LOG_COOLDOWN_MS) {
        s_WatchdogLastLogMs = now;
        return 1;
    }
    return 0;
}

/* Dump the current stack names at WARNING level. Caller controls rate-limit. */
static void inputctxWatchdogDumpStack(const char *why)
{
    sysLogPrintf(LOG_WARNING,
                 "INPUTCTX watchdog: %s depth=%d bottom='%s' top='%s'",
                 why,
                 (int)s_Depth,
                 (s_Depth > 0 && s_Stack[0]) ? (s_Stack[0]->name ? s_Stack[0]->name : "?") : "(empty)",
                 inputCtxGetTopName());
    for (s32 i = 0; i < s_Depth; i++) {
        if (s_Stack[i]) {
            sysLogPrintf(LOG_WARNING, "  [%d] %s%s",
                         (int)i,
                         s_Stack[i]->name ? s_Stack[i]->name : "?",
                         s_Stack[i]->marked_for_removal ? " (marked)" : "");
        }
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
            s_historyRecord('R', ctx, write);
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

    /* ---- S295 F3: End-of-frame watchdog ----
     *
     * Catches leaked push/pop imbalances. Two severity levels:
     *   - WARN: depth unexpectedly deep, or bottom of stack is not gameplay
     *     (rate-limited to 1 log/second to avoid spam).
     *   - FORCE-RESET: depth near MAX_STACK (true pathology: a renderer is
     *     pushing every frame without popping). Blow away the stack and
     *     re-seed with gameplay to prevent overflow + dead-input.
     *
     * Rationale: context/scratch/menu-system-investigation-2026-04-16.md §6.1
     * documented direction-A desync (no menu visible, player frozen). The
     * root cause is a leaked push — this watchdog surfaces it in logs and
     * provides a recovery path instead of silent stuck state. */
    if (s_Depth >= INPUTCTX_WATCHDOG_FORCE_RESET) {
        inputctxWatchdogDumpStack("DEPTH NEAR OVERFLOW — forcing reset to gameplay");
        for (s32 i = s_Depth - 1; i >= 0; i--) {
            if (s_Stack[i]) {
                if (s_Stack[i]->on_pop) {
                    s_Stack[i]->on_pop(s_Stack[i]);
                }
                s_Stack[i]->active = 0;
                s_Stack[i]->marked_for_removal = 0;
                s_Stack[i] = NULL;
            }
        }
        s_Depth = 0;
        /* Re-seed: gameplay must always be on the bottom. */
        inputCtxPush(&g_CtxGameplay);
        s_WatchdogLastLogMs = SDL_GetTicks();
    } else if (s_Depth >= INPUTCTX_WATCHDOG_DEEP_THRESHOLD) {
        if (inputctxWatchdogShouldLog()) {
            inputctxWatchdogDumpStack("DEEP STACK (possible leak)");
        }
    } else if (s_Depth > 0 && s_Stack[0] != &g_CtxGameplay) {
        if (inputctxWatchdogShouldLog()) {
            inputctxWatchdogDumpStack("BOTTOM != gameplay (invariant violated)");
        }
    }

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

/* B-259 (2026-04-25): single authoritative cursor-visibility writer.
 * See inputctx.h for the design. Reconciles caller intent with the
 * input-context stack: menu contexts force-show; gameplay honours
 * caller preference. Direct SDL_ShowCursor calls outside this helper
 * (and the inputCtxSyncMouseMode body below) are forbidden -- they
 * would race the timer logic in input.c and the gfx/hotswap callers. */
void inputCtxApplyCursorVisibility(s32 want_show)
{
    InputContext *top = inputCtxGetTop();
    s32 effective = want_show;
    if (top && top != &g_CtxGameplay) {
        /* Menu/overlay/textinput context owns visibility. Force-show
         * regardless of what the caller asked for. */
        effective = 1;
    }
    SDL_ShowCursor(effective ? SDL_ENABLE : SDL_DISABLE);
}

void inputCtxSyncMouseMode(void)
{
    InputContext *top = inputCtxGetTop();
    if (!top) {
        return;
    }

    if (top == &g_CtxGameplay) {
        /* Gameplay: relative mouse, cursor hidden. */
        if (!SDL_GetRelativeMouseMode()) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
            sysLogPrintf(LOG_NOTE, "INPUTCTX: syncMouseMode -- restored relative mode for gameplay");
        }
        /* B-259: re-assert cursor-hide unconditionally via the authority
         * helper. Even if relative mode was already SDL_TRUE, an external
         * caller (input.c MLOCK_AUTO timer, gfx-API, hotswap close) could
         * have toggled cursor visibility since the last sync. Going
         * through the helper makes the next SDL state match ctx intent. */
        inputCtxApplyCursorVisibility(0);
    } else {
        /* Any menu/overlay context: absolute mouse, cursor visible. */
        if (SDL_GetRelativeMouseMode()) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            sysLogPrintf(LOG_NOTE, "INPUTCTX: syncMouseMode -- restored absolute mode for '%s'",
                         top->name ? top->name : "?");
        }
        /* B-259: re-assert cursor-show unconditionally. Same rationale
         * as the gameplay branch above -- external callers can drift the
         * SDL flag away from ctx intent between sync events. */
        inputCtxApplyCursorVisibility(1);
    }
}

/* ---- Focus tracking + gameplay-input authority predicate (ADR 2026-04-13) ---- */

void inputCtxNotifyFocus(s32 gained)
{
    if (gained) {
        if (s_WindowFocusLost) {
            s_WindowFocusLost = 0;
            s_FocusRegainTick = SDL_GetTicks();
            /* On focus regain, flush gameplay state so held keys from
             * before alt-tab don't stick. SDL will re-send KEYDOWN for
             * anything physically still held; the user sees a brief
             * settle window (INPUTCTX_FOCUS_SETTLE_MS) during which
             * gameplay actions are suppressed. */
            actionmapFlushGameplayState();
            sysLogPrintf(LOG_NOTE, "INPUTCTX: focus GAINED — flushed gameplay, settle %dms",
                         INPUTCTX_FOCUS_SETTLE_MS);
        }
    } else {
        if (!s_WindowFocusLost) {
            s_WindowFocusLost = 1;
            /* Focus lost: flush now. If we held W when alt-tabbing, we
             * must not continue walking forward while the window is
             * backgrounded (and SDL may never deliver the KEYUP). */
            actionmapFlushGameplayState();
            sysLogPrintf(LOG_NOTE, "INPUTCTX: focus LOST — flushed gameplay state");
        }
    }
}

s32 inputCtxIsFocusLost(void)
{
    return s_WindowFocusLost;
}

s32 inputCtxDebugCopyStack(InputCtxDebugEntry *out, s32 maxEntries)
{
    if (!out || maxEntries <= 0) {
        return 0;
    }
    s32 n = s_Depth;
    if (n > maxEntries) {
        n = maxEntries;
    }
    for (s32 i = 0; i < n; i++) {
        InputContext *ctx = s_Stack[i];
        if (ctx) {
            out[i].name = ctx->name;
            out[i].marked_for_removal = ctx->marked_for_removal;
            out[i].push_tick_ms = ctx->push_tick;
            out[i].ctx_ptr = (void *)ctx;
        } else {
            out[i].name = "(null)";
            out[i].marked_for_removal = 0;
            out[i].push_tick_ms = 0;
            out[i].ctx_ptr = NULL;
        }
    }
    return n;
}

void inputCtxDebugSnapshotAuthority(InputCtxDebugAuthority *out)
{
    if (!out) {
        return;
    }
    out->stack_depth = s_Depth;
    InputContext *top = inputCtxGetTop();
    out->effective_top_name = inputCtxGetTopName();
    out->effective_top_is_gameplay = (top == &g_CtxGameplay) ? 1 : 0;
    out->window_focus_lost = s_WindowFocusLost;
    out->focus_settle_remaining_ms = 0;
    if (s_FocusRegainTick != 0) {
        u32 now = SDL_GetTicks();
        u32 elapsed = now - s_FocusRegainTick;
        if (elapsed < INPUTCTX_FOCUS_SETTLE_MS) {
            out->focus_settle_remaining_ms = INPUTCTX_FOCUS_SETTLE_MS - elapsed;
        }
    }
    /* Mirror gameplayInputSuppressed() without clearing s_FocusRegainTick */
    out->gameplay_would_suppress = 0;
    if (top && top != &g_CtxGameplay) {
        out->gameplay_would_suppress = 1;
    } else if (s_WindowFocusLost) {
        out->gameplay_would_suppress = 1;
    } else if (out->focus_settle_remaining_ms > 0) {
        out->gameplay_would_suppress = 1;
    }
}

/* Issue 11 (2026-04-24): copy the push/pop history ring out for the F9
 * debug overlay.  Writes the last N entries oldest-first into dst;
 * returns the count.  max_entries is the caller's array capacity.  If
 * the ring holds more events than max_entries, the oldest entries are
 * dropped and the newest N are written. */
s32 inputCtxDebugCopyHistory(InputCtxDebugHistoryEntry *dst, s32 max_entries)
{
    if (!dst || max_entries <= 0) return 0;
    s32 n = s_HistoryCount;
    if (n > max_entries) n = max_entries;
    s32 start = (s_HistoryHead - s_HistoryCount + INPUTCTX_HISTORY_SIZE)
                % INPUTCTX_HISTORY_SIZE;
    if (s_HistoryCount > max_entries) {
        start = (start + (s_HistoryCount - max_entries)) % INPUTCTX_HISTORY_SIZE;
    }
    for (s32 i = 0; i < n; i++) {
        const InputCtxHistoryEntry *src =
            &s_History[(start + i) % INPUTCTX_HISTORY_SIZE];
        dst[i].timestamp_ms = src->timestamp_ms;
        dst[i].name         = src->name;
        dst[i].depth_after  = src->depth_after;
        dst[i].event        = src->event;
    }
    return n;
}

s32 gameplayInputSuppressed(void)
{
    /* (a) non-gameplay context on top */
    InputContext *top = inputCtxGetTop();
    if (top && top != &g_CtxGameplay) {
        return 1;
    }

    /* (b) window focus currently lost */
    if (s_WindowFocusLost) {
        return 1;
    }

    /* (c) focus regained but still inside settle window */
    if (s_FocusRegainTick != 0) {
        u32 now = SDL_GetTicks();
        u32 elapsed = now - s_FocusRegainTick;
        if (elapsed < INPUTCTX_FOCUS_SETTLE_MS) {
            return 1;
        }
        /* Settle expired — clear so we don't keep doing subtraction. */
        s_FocusRegainTick = 0;
    }

    return 0;
}

/* ======================================================================
 * Built-in context callbacks
 * ====================================================================== */

/* ---- g_CtxGameplay ---- */

static void gameplayOnPush(InputContext *self)
{
    (void)self;
    inputLockMouse(1);
    /* F-1.4: SDL mouse mode handled solely by inputCtxSyncMouseMode(). */
    sysLogPrintf(LOG_NOTE, "INPUTCTX: gameplay on_push -- mouse captured");
}

static void gameplayOnPop(InputContext *self)
{
    (void)self;
    /* F-1.4: SDL mouse mode handled solely by inputCtxSyncMouseMode(). */
    sysLogPrintf(LOG_NOTE, "INPUTCTX: gameplay on_pop");
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
    /* F-1.4: SDL mouse mode handled solely by inputCtxSyncMouseMode(). */
    imcActivate(&g_ImcMenu);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: imgui_menu on_push -- g_ImcMenu activated");
}

static void imguiMenuOnPop(InputContext *self)
{
    (void)self;
    imcDeactivate(&g_ImcMenu);
    /* B-195: clear ImGui's nav / focus / active-id state so
     * WantCaptureKeyboard drops back to false. Without this, NavWindow and
     * ActiveId references from the just-closed menu keep ImGui claiming
     * keyboard input, and pdguiProcessEvent's WantCaptureKeyboard gate in
     * pdgui_backend.cpp:851 eats WASD / gameplay keys. Escape and Enter
     * are explicitly whitelisted through (line 856), which is why pause /
     * menu toggle still works while movement is dead. */
    extern void pdguiClearImGuiFocusAndNav(void);
    pdguiClearImGuiFocusAndNav();
    sysLogPrintf(LOG_NOTE, "INPUTCTX: imgui_menu on_pop -- g_ImcMenu deactivated");
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
    /* F-1.4: SDL mouse mode handled solely by inputCtxSyncMouseMode(). */
    s_GamePaused = 1;
    imcActivate(&g_ImcMenu);
    imcActivate(&g_ImcPauseMenu);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: pause_menu on_push -- game paused, menu+pause IMCs activated");
}

static void pauseMenuOnPop(InputContext *self)
{
    (void)self;
    s_GamePaused = 0;
    imcDeactivate(&g_ImcPauseMenu);
    imcDeactivate(&g_ImcMenu);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: pause_menu on_pop -- game unpaused, menu+pause IMCs deactivated");
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
    /* F-1.4: SDL mouse mode handled solely by inputCtxSyncMouseMode(). */
    imcActivate(&g_ImcMenu);
    imcActivate(&g_ImcDebugOverlay);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: debug_overlay on_push -- menu+debug IMCs activated");
}

static void debugOverlayOnPop(InputContext *self)
{
    (void)self;
    imcDeactivate(&g_ImcDebugOverlay);
    imcDeactivate(&g_ImcMenu);
    sysLogPrintf(LOG_NOTE, "INPUTCTX: debug_overlay on_pop -- menu+debug IMCs deactivated");
}

static s32 debugOverlayCanConsume(InputContext *self, const SDL_Event *ev)
{
    (void)self;

    /* Debug overlay consumes ALL input types (keyboard, mouse, AND controller)
     * to prevent game actions from firing while the overlay is open. */
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
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
    case SDL_JOYAXISMOTION:
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
