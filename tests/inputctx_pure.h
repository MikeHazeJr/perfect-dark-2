/*
 * inputctx_pure.h -- Pure-C subset of the input context stack used by pd-tests.
 *
 * The real inputctx.c (port/src/inputctx.c) is the source of truth for the
 * priority-based input context stack and the gameplayInputSuppressed()
 * authority predicate. It pulls in SDL2 (events, GetTicks, mouse mode),
 * actionmap, and menupool. pd-tests deliberately does not link any of those.
 *
 * This file replicates the algorithmic invariants the real stack must hold:
 *
 *   1. Push grows the stack by one. Double-push of the same context is
 *      rejected (no-op) unless the existing entry was marked for removal,
 *      in which case it resurrects (un-marks).
 *   2. PopDeferred(ctx) marks the entry for removal but leaves it on the
 *      stack until EndFrame compacts. Mid-frame queries see it as not
 *      active (via IsActive / GetTop) but the slot is still occupied.
 *   3. EndFrame removes every marked entry, compacting the stack toward
 *      index 0.
 *   4. GetTop returns the highest-index non-marked entry, or NULL on empty.
 *   5. The bottom-of-stack invariant (gameplay must always be at index 0)
 *      is the SYSTEM-LEVEL contract; this file's tests assert it via
 *      explicit pushes, not as a baked-in rule.
 *   6. gameplayInputSuppressed() returns 1 when ANY of:
 *        (a) top != gameplay
 *        (b) window focus lost
 *        (c) focus regained within settle window (modeled via a flag)
 *   7. The inputctx-to-layer bridge publishes one menu-layer ownership bit
 *      while the effective top is non-gameplay, and clears it as soon as
 *      gameplay is effective again.
 *
 * @SYNC port/src/inputctx.c (push/pop/EndFrame/GetTop) lines 88-465.
 * @SYNC port/src/inputctx.c::gameplayInputSuppressed lines 647-672.
 *
 * If the real file changes the algorithm meaningfully, re-sync this copy
 * and the test will catch behaviour drift on the next pd-tests run.
 */

#ifndef _IN_TESTS_INPUTCTX_PURE_H
#define _IN_TESTS_INPUTCTX_PURE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INPUTCTX_PURE_MAX_STACK 16

typedef struct InputContextPure {
    const char *name;
    s32 active;
    s32 marked_for_removal;
    /* Identity-preserving: tests use the address of an InputContextPure
     * instance to identify it on the stack, mirroring the real code which
     * uses the address of g_CtxGameplay / g_CtxImGuiMenu / etc. */
} InputContextPure;

/* Module-global stack state. Single-stack model matches the real code. */
void inputCtxPureReset(void);

s32 inputCtxPurePush(InputContextPure *ctx);          /* 1=fresh, 0=duplicate, -1=overflow, 2=resurrect */
void inputCtxPurePopDeferred(InputContextPure *ctx);
void inputCtxPurePopImmediate(void);
void inputCtxPureEndFrame(void);

s32 inputCtxPureDepth(void);                          /* physical depth incl. marked */
InputContextPure *inputCtxPureGetTop(void);           /* skips marked */
s32 inputCtxPureIsActive(InputContextPure *ctx);
const char *inputCtxPureGetTopName(void);

/* Test mirror for the production inputctx -> LAYER_MENU bridge. */
s32 inputCtxPureMenuLayerActive(void);
s32 inputCtxPureMenuLayerPushCount(void);
s32 inputCtxPureMenuLayerPopCount(void);

/* Gameplay context identity: tests register one canonical "gameplay" ctx
 * via this setter. gameplayInputSuppressedPure() compares top against it. */
void inputCtxPureSetGameplayCtx(InputContextPure *ctx);

/* Window focus modeling */
void inputCtxPureNotifyFocus(s32 gained);             /* 0=lost, non-zero=gained */
s32 inputCtxPureIsFocusLost(void);

/* Authority predicate, mirrors the real gameplayInputSuppressed(). */
s32 gameplayInputSuppressedPure(void);

/* Tests model the focus-regain settle window with an explicit advance,
 * so we don't need real-time clocks. After Notify(gained=1), the predicate
 * stays suppressed until inputCtxPureClearFocusSettle() is called. */
void inputCtxPureClearFocusSettle(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_TESTS_INPUTCTX_PURE_H */
