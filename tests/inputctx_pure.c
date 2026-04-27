/*
 * inputctx_pure.c -- pure-C subset of the input context stack.
 *
 * Mirrors port/src/inputctx.c invariants without SDL/actionmap/menupool.
 * Tests in tests/test_input_authority.cpp drive this module to verify
 * push / pop / dedup / mark-for-removal / GetTop / EndFrame compaction
 * and the gameplayInputSuppressed() authority predicate.
 *
 * Re-sync notes: if the real inputctx.c changes its algorithm (e.g. top
 * lookup skips a different class of entries, or push rejects under a new
 * condition), update this copy and the tests it backs. @SYNC markers
 * inside the file point to the relevant real-file line ranges.
 */

#include <stddef.h>
#include "inputctx_pure.h"

static InputContextPure *s_Stack[INPUTCTX_PURE_MAX_STACK];
static s32 s_Depth = 0;
static InputContextPure *s_GameplayCtx = NULL;
static s32 s_FocusLost = 0;
static s32 s_FocusSettlePending = 0;

void inputCtxPureReset(void)
{
    /* Just clear the stack pointers - do NOT dereference them. Tests
     * legitimately use both file-static and stack-local InputContextPure
     * instances; a stack-local context that's gone out of scope from a
     * prior test would still be present in s_Stack[] until reset, and
     * dereferencing it would write to freed stack frames. The real
     * inputctx.c code dereferences during shutdown because its contexts
     * are global (g_CtxGameplay, etc., always alive), but in test
     * builds we err on the side of safety. */
    for (s32 i = 0; i < INPUTCTX_PURE_MAX_STACK; i++) {
        s_Stack[i] = NULL;
    }
    s_Depth = 0;
    s_GameplayCtx = NULL;
    s_FocusLost = 0;
    s_FocusSettlePending = 0;
}

void inputCtxPureSetGameplayCtx(InputContextPure *ctx)
{
    s_GameplayCtx = ctx;
}

s32 inputCtxPurePush(InputContextPure *ctx)
{
    /* @SYNC inputctx.c:132-205 (inputCtxPush) */
    if (!ctx) {
        return -1;
    }
    if (s_Depth >= INPUTCTX_PURE_MAX_STACK) {
        return -1;
    }

    /* Resurrect marked-for-removal entry rather than rejecting. */
    for (s32 i = 0; i < s_Depth; i++) {
        if (s_Stack[i] == ctx) {
            if (ctx->marked_for_removal) {
                ctx->marked_for_removal = 0;
                return 2;  /* resurrect */
            }
            return 0;  /* duplicate, ignore */
        }
    }

    ctx->active = 1;
    ctx->marked_for_removal = 0;
    s_Stack[s_Depth] = ctx;
    s_Depth++;
    return 1;
}

void inputCtxPurePopDeferred(InputContextPure *ctx)
{
    /* @SYNC inputctx.c:207-234 (inputCtxPopDeferred) */
    if (!ctx) return;
    for (s32 i = 0; i < s_Depth; i++) {
        if (s_Stack[i] == ctx) {
            ctx->marked_for_removal = 1;
            return;
        }
    }
}

void inputCtxPurePopImmediate(void)
{
    /* @SYNC inputctx.c:236-261 */
    if (s_Depth <= 0) return;
    s_Depth--;
    InputContextPure *ctx = s_Stack[s_Depth];
    s_Stack[s_Depth] = NULL;
    if (ctx) {
        ctx->active = 0;
        ctx->marked_for_removal = 0;
    }
}

void inputCtxPureEndFrame(void)
{
    /* @SYNC inputctx.c:339-410 (compaction step) */
    s32 write = 0;
    for (s32 read = 0; read < s_Depth; read++) {
        InputContextPure *ctx = s_Stack[read];
        if (ctx && ctx->marked_for_removal) {
            ctx->active = 0;
            ctx->marked_for_removal = 0;
        } else {
            s_Stack[write] = ctx;
            write++;
        }
    }
    for (s32 i = write; i < s_Depth; i++) {
        s_Stack[i] = NULL;
    }
    s_Depth = write;
}

s32 inputCtxPureDepth(void)
{
    return s_Depth;
}

InputContextPure *inputCtxPureGetTop(void)
{
    /* @SYNC inputctx.c:414-422 (skips marked) */
    for (s32 i = s_Depth - 1; i >= 0; i--) {
        if (s_Stack[i] && !s_Stack[i]->marked_for_removal) {
            return s_Stack[i];
        }
    }
    return NULL;
}

s32 inputCtxPureIsActive(InputContextPure *ctx)
{
    if (!ctx) return 0;
    for (s32 i = 0; i < s_Depth; i++) {
        if (s_Stack[i] == ctx && !ctx->marked_for_removal) {
            return 1;
        }
    }
    return 0;
}

const char *inputCtxPureGetTopName(void)
{
    InputContextPure *t = inputCtxPureGetTop();
    if (t && t->name) return t->name;
    return "none";
}

void inputCtxPureNotifyFocus(s32 gained)
{
    /* @SYNC inputctx.c:534-559 (inputCtxNotifyFocus) */
    if (gained) {
        if (s_FocusLost) {
            s_FocusLost = 0;
            s_FocusSettlePending = 1;
        }
    } else {
        s_FocusLost = 1;
    }
}

s32 inputCtxPureIsFocusLost(void)
{
    return s_FocusLost;
}

void inputCtxPureClearFocusSettle(void)
{
    s_FocusSettlePending = 0;
}

s32 gameplayInputSuppressedPure(void)
{
    /* @SYNC inputctx.c:647-672 (gameplayInputSuppressed) */
    InputContextPure *top = inputCtxPureGetTop();
    if (top && s_GameplayCtx && top != s_GameplayCtx) {
        return 1;
    }
    if (s_FocusLost) {
        return 1;
    }
    if (s_FocusSettlePending) {
        return 1;
    }
    return 0;
}
