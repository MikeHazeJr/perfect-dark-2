# Input-authority methodology

Companion to `context/designs/contextual-input-schemes.md` (Priority J) and `context/audits/input-authority-discipline-2026-04-25.md` (Priority K-a). Defines the policy that prevents the two-stack drift class.

## Principle

There is one stack. The user's mental model and the runtime model agree.

Concretely: `menuPushDialog` / `menuPopDialog` (and their pool-aware companions `menupoolAcquireDialog` / `menupoolReleaseDialog`) are the **sole** mechanism that transfers input authority between gameplay and a menu surface. No code outside that path may directly call `inputCtxPush(menu_ctx)` or `inputCtxPopDeferred(menu_ctx)` for a menu input context. Two surfaces are exempt and they are listed below; everything else routes through the menu stack.

The menupool's visual-slot ownership IS input-authority ownership. A pool slot acquired with `ctx = &g_CtxImGuiMenu` owns the responsibility for keeping that ctx live on the input-context stack until the slot is released. The runtime checks this invariant inside `menupoolAcquire` -- a LOG_WARNING tripwire fires the moment menupool thinks it owns a ctx that is not live on the stack.

## Why two stacks were a footgun

Today the codebase has three layers that **could** drift if anyone managed them independently:

1. **Legacy menu stack** (`g_Menus[MAX_MENUS]` in `src/game/menu.c`). Visual ownership of dialog defs.
2. **Menupool** (`s_Pool[MENU_TYPE_COUNT]` in `port/src/menupool.c`). Per-type dedup + ctx ownership.
3. **Input-context stack** (`s_Stack[INPUTCTX_MAX_STACK]` in `port/src/inputctx.c`). Input authority + mouse-mode + IMC activation.

Layers 1 and 2 are in lock-step today: `menuPushDialog` calls `menupoolAcquire` and `menuCloseDialog` calls `menupoolRelease`. Layer 3 is driven by layer 2 when the pool-aware acquire/release is called with a non-NULL ctx.

The drift class:

- A code path pushes a menu ctx directly (`inputCtxPush(&g_CtxImGuiMenu)`) without going through the pool. Layer 2 doesn't know it owns the pop. The ctx leaks across the next stage transition.
- Or the inverse: a force-close path calls `inputCtxPopDeferred` to scrub the menu ctx, but a pool slot still thinks the ctx is live and re-pushes it next frame.

These were the two halves of Issue 2's "stuck" symptom and Issue 3 was a closely related cursor authority race (different code, same shape).

## Rules

### R1. Pool-managed menus

Every ImGui menu renderer that needs input authority calls `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` (or the pause-menu variant `menupoolAcquire(MENU_TYPE_PAUSE_MENU, NULL, &g_CtxPauseMenu)`) on its `IsWindowAppearing` frame. The matching close path runs through `menuPopDialog` (which cascades to `menupoolReleaseDialog`).

Renderers do NOT push or pop input contexts directly. The pool owns it.

### R2. The two-stack invariant

After `menupoolAcquire(type, def, ctx)` returns successfully, `inputCtxIsActive(ctx)` MUST return true (when `ctx` is non-NULL). If it doesn't, that is two-stack drift -- the pool owns a slot but the input authority is somewhere else. The runtime emits a LOG_WARNING `MENUPOOL: drift --` line that pinpoints the type and ctx so the calling site can be fixed.

Symmetrically, after `menupoolRelease(type)` returns, the slot's previously-owned ctx is either marked-for-removal (deferred pop -- normal flow) or no longer on the stack (immediate pop). The end-of-frame compactor in `inputCtxEndFrame` handles the deferred case.

### R3. Force-close paths use bulk release only

Stage transitions, disconnects, and match-start cleanup call `menupoolReleaseAll()` and stop. The bulk release walks every active slot, pops every owned ctx, and additionally pops the unregistered-fallback ctx (K-b1, 2026-04-25). Callers do NOT pair this with a manual `inputCtxPopDeferred(&g_CtxImGuiMenu)` -- that was the legacy pattern, deleted in K-b3.

### R4. Only one cursor authority

`SDL_ShowCursor` is called from exactly one place: `inputCtxSyncMouseMode` (`port/src/inputctx.c`). It runs end-of-frame off the input-context stack top: gameplay -> hide; anything else -> show. ImGui's own SDL backend is configured with `ImGuiConfigFlags_NoMouseCursorChange` so its `ImGui_ImplSDL2_UpdateMouseCursor` short-circuits without touching `SDL_ShowCursor`. This closes Issue 3's cursor-flicker / RMB-only-nav symptom (K-c, 2026-04-25).

### R5. Three legitimate exceptions

These three code paths push or pop input contexts directly, by design. Anything else doing so is a bug.

1. **Boot-time gameplay push.** `port/src/main.c::main` and `src/lib/main.c::mainLoop` push `g_CtxGameplay` once at boot / stage transition. The watchdog in `inputCtxEndFrame` re-pushes it if the stack drained unexpectedly. `g_CtxGameplay` does not correspond to a menupool slot.

2. **F12 debug overlay.** `port/fast3d/pdgui_backend.cpp::pdguiDebugOverlayShow` / `Hide` push and pop `g_CtxDebugOverlay`. The debug overlay is a single ImGui window owned by the renderer, not a `menudialog`, so it has no menupool slot. The push/pop is contained to two functions.

3. **Forge IMC activation.** `src/game/forgemode.c` activates `g_ImcForgeSession` and `g_ImcForge` directly via `imcActivate` / `imcDeactivate`. These are IMCs (binding-table contexts), not InputContext objects -- they sit on a separate priority stack inside the action map and don't push onto the input-context stack itself.

If a future surface needs input authority, **first** ask whether it can be a menupool dialog. If yes, register a `menu_type_t` for it and use the pool. Only fall back to a direct push if the surface is genuinely outside the menu lifecycle (rare).

## How to add a new menu

1. Add a `MENU_TYPE_*` enum entry to `port/include/menupool.h`.
2. Register the dialogdef in `port/src/menupool.c`'s built-in registration table.
3. In the renderer's `IsWindowAppearing` block:
   - `menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);`
4. In the renderer's close path:
   - `menuPopDialog();` -- cascades to `menupoolReleaseDialog` and pops the ctx.
5. Build and run; watch the log. If you see `MENUPOOL: drift --` you violated R2 somewhere.

## How to add a force-close site

If your code path needs to "kill every menu and return to gameplay" (stage transition, disconnect, error recovery):

```c
menupoolReleaseAll();
```

That is the entire idiom. Do not pair it with `inputCtxPopDeferred`. The bulk release pops every owned ctx including the unregistered-fallback. The watchdog catches anything else.

## Verification

The K-d assertion (`MENUPOOL: drift --` LOG_WARNING) is permanent. It runs in every build, always. If it fires, the calling site has two-stack drift; fix the site, do not silence the warning.

Mike's playtest closes Issue 2 / Issue 3 by repro: the symptoms (stuck back-out, RMB-only nav) should not surface, and no drift warnings should appear in the log under normal play. Both are tracked in `context/bugs.md` as B-250 / B-251 with the structural-resolution status.
