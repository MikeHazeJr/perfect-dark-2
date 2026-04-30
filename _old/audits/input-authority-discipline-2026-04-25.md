# Input-authority discipline audit -- 2026-04-25

Priority K-a deliverable. Maps every site that touches input authority (input-context stack push/pop or menupool acquire/release) and classifies each as pool-managed, system-level legitimate, or drift-prone defensive. Drives the K-b collapse work and the K-d assertion design.

Branch: `claude/stoic-wing-35829b`. HEAD when audit captured: `3f5b42d4` (post-Priority-J).

## Summary scorecard

| Category | Sites | Status |
|---|---|---|
| Pool-managed acquire/release with ctx | 18+ | Canonical, keep |
| Pool-managed acquire/release without ctx (legacy fallback) | 0 | None remain in tree |
| Direct `inputCtxPush(g_CtxGameplay)` -- system bottom | 3 | Legitimate |
| Direct `inputCtxPush(g_CtxDebugOverlay)` -- F12 toggle | 2 | Legitimate (no dialog) |
| Direct `inputCtxPush(g_CtxImGuiMenu)` -- defensive force-push | 2 | Drift symptom -- collapse target |
| Direct `inputCtxPopDeferred(g_CtxImGuiMenu)` -- defensive scrub | 11 | Drift symptom -- collapse target |
| `menupoolReleaseAll()` paired with explicit ctx pop | 5 | Drift symptom -- collapse target |
| Cursor-visibility authority count | 2 | Race source -- collapse target |

Total drift-prone surface: **20 sites** plus the cursor race. Eight of those are paired (release + pop in the same block) so the de-dup count is closer to 12 unique fix points.

## Index of every input-authority site

### A. Pool-managed (canonical)

`menupoolAcquireDialog(def, ctx)` / `menupoolAcquire(type, def, ctx)` are the canonical entry points. They acquire the visual slot and push the ctx atomically; release pops the ctx atomically. When the same ctx is shared across siblings (e.g. main menu pushes `g_CtxImGuiMenu`, then theme editor opens under the same ctx), only the first opener owns the pop -- subsequent acquires attach passively (`owned_ctx = NULL`).

Pool-managed sites confirmed across:

- `port/fast3d/pdgui_menu_agentselect.cpp` (3 calls)
- `port/fast3d/pdgui_menu_botsetup.cpp` (2 calls)
- `port/fast3d/pdgui_menu_cheats.cpp` (2 calls)
- `port/fast3d/pdgui_menu_endscreen.cpp` (2 calls -- but paired with defensive force-push, see D)
- `port/fast3d/pdgui_menu_lobby.cpp` (2 calls -- `MENU_TYPE_SOCIAL_LOBBY`)
- `port/fast3d/pdgui_menu_mainmenu.cpp` (8+ calls, modding-hub / theme-editor / settings)
- `port/fast3d/pdgui_menu_mpadvanced.cpp` (2 calls)
- `port/fast3d/pdgui_menu_mppause.cpp` (2 calls)
- `port/fast3d/pdgui_menu_mpsettings.cpp` (2 calls)
- `port/fast3d/pdgui_menu_mpsetup.cpp` (2 calls)
- `port/fast3d/pdgui_menu_pausemenu.cpp` (2 calls -- `MENU_TYPE_PAUSE_MENU`, ctx = `g_CtxPauseMenu`)
- `port/fast3d/pdgui_menu_playerconfig.cpp` (2 calls)
- `port/fast3d/pdgui_menu_room.cpp` (4 calls -- `MENU_TYPE_ROOM`)
- `port/fast3d/pdgui_menu_solomission.cpp` (1 call)
- `port/fast3d/pdgui_menu_teamsetup.cpp` (2 calls)
- `port/fast3d/pdgui_menu_training.cpp` (2 calls)

These are the canonical pattern. Acquire pairs with release; pool owns the inputctx push/pop.

### B. System-level bottom-of-stack pushes (legitimate)

`g_CtxGameplay` is the always-on bottom of the inputctx stack. Three legitimate direct push sites:

| File | Line | Purpose |
|---|---|---|
| `port/src/main.c:204` | Initial push at game boot, immediately after `inputCtxInit`. |
| `src/lib/main.c:1086` | Stage transition reseed -- if the stack drained during teardown, re-push gameplay before tick resumes. |
| `port/src/inputctx.c:394` | Watchdog force-reset path: when the stack approaches `INPUTCTX_MAX_STACK`, blow it away and re-push gameplay. Logged at WARNING. |

**Verdict:** legitimate. `g_CtxGameplay` does not correspond to any menupool slot. These three sites are the only places that push the gameplay context, and they all correspond to invariant-restoration (boot, transition, watchdog) -- never gameplay state changes during normal operation.

### C. F12 debug overlay (legitimate)

`port/fast3d/pdgui_backend.cpp:1057` and `:1175` push `g_CtxDebugOverlay` on F12 toggle on; `:1059` and `:1177` pop on toggle off. Two paths because the F12 entry point is duplicated (event handler + render-side tick).

**Verdict:** legitimate -- the debug overlay is a single ImGui window owned by the renderer, not a `menudialog`, so it has no menupool slot. The push/pop is contained to two functions (`pdguiDebugOverlayShow` / `pdguiDebugOverlayHide`).

### D. Defensive force-push of `g_CtxImGuiMenu` (drift symptom)

| File | Line | Comment | Repro race |
|---|---|---|---|
| `port/fast3d/pdgui_menu_endscreen.cpp:452` | "B-RMB-Endscreen fix" | Solo endscreen first frame raced with `menupoolReleaseAll`'s deferred pop; ctx never attached. |
| `port/fast3d/pdgui_menu_endscreen.cpp:940` | "B-End-Game-Input" | MP endscreen same race. |

These force-push sites only fire when `inputCtxIsActive(&g_CtxImGuiMenu)` returns false after `menupoolAcquireDialog`. The fact that they exist means the pool acquire failed to attach in some condition. Root cause: the `marked_for_removal` resurrect path in `inputCtxPush` (inputctx.c:159-181) un-marks the ctx but the pool slot's `owned_ctx` was never set, so subsequent release does not pop it. The endscreen force-push covers the visible symptom but adds a parallel path.

**Verdict:** drift symptom. The cleanup is to make `menupoolAcquire`'s already-active branch (S300 change, menupool.c:242-253) handle the resurrect case. Today's branch already pushes when the slot is already active and the ctx isn't on the stack -- the race is that the ctx **is** on the stack (marked for removal but compactor hasn't run), so `inputCtxIsActive` returns true and the push is skipped, but next frame the deferred pop wipes it. Fix: when slot is already active and ctx is marked for removal, un-mark explicitly. Then the force-push is structurally unnecessary. Filed as K-b1.

### E. Defensive pop of `g_CtxImGuiMenu` (drift symptom)

| File | Line | Reason |
|---|---|---|
| `port/fast3d/pdgui_menu_solomission.cpp:1399` | After `menuhandlerAcceptMission(MENUOP_SET, ...)` -- transitions to gameplay, but doesn't pop the menu ctx itself. |
| `port/fast3d/pdgui_menu_solomission.cpp:2549` | Same pattern. |
| `port/fast3d/pdgui_menu_solomission.cpp:2627` | Same pattern. |
| `port/fast3d/pdgui_bridge.c:807` | `pdguiEndscreenExitToMainMenu` -- paired with `menupoolReleaseAll()` at :805. |
| `port/fast3d/pdgui_bridge.c:835` | Same shape. |
| `port/fast3d/pdgui_bridge.c:887` | Same shape. |
| `port/src/net/net.c:1280` | `netDisconnect` -- paired with `menupoolReleaseAll()` at :1278. |
| `port/src/net/matchsetup.c:862` | `matchStart` -- paired with `menupoolReleaseAll()` at :858. |
| `port/src/net/matchsetup.c:962` | Cleanup path -- paired with `menupoolReleaseAll()` at :958. |
| `port/src/net/netmsg.c:1591` | `SVC_STAGE_START` (CS) -- paired with `menupoolReleaseAll()` at :1589. |
| `port/src/net/netmsg.c:1745` | `SVC_STAGE_START` (co-op) -- paired with `menupoolReleaseAll()` at :1743. |
| `port/fast3d/pdgui_menu_mainmenu.cpp:4596` | "leak class caught on top-level close" -- top-level main menu close path. |

The 5 paired-with-`menupoolReleaseAll` sites (bridge x3, net.c, matchsetup.c x2, netmsg.c x2) follow this shape:

```c
menupoolReleaseAll();
if (inputCtxIsActive(&g_CtxImGuiMenu)) {
    inputCtxPopDeferred(&g_CtxImGuiMenu);
}
```

`menupoolReleaseAll()` walks every slot and releases each via `menupoolRelease`, which pops the ctx if `owned_ctx` was set. If a slot held `g_CtxImGuiMenu` as its owned ctx, the pop fires automatically. The defensive `inputCtxPopDeferred` only catches the case where the ctx was pushed **outside** the pool (e.g. the unregistered-dialogdef fallback in `menupoolAcquireDialog`, or the legacy paths the M-22 migration in S388 was meant to cover).

The 3 solomission sites are different: they fire after `menuhandlerAcceptMission` accepts the difficulty selection and stages the transition to gameplay. The active dialog (`g_SoloMissionAcceptMenuDialog`) is still on the menupool, so the ctx pop is preempting the natural release path. This is a code smell -- `menuhandlerAcceptMission` should call `menuPopDialog` (which cascades to `menupoolReleaseDialog`), not leave the dialog open while transitioning input authority.

The mainmenu :4596 site is the most diagnostic -- the comment explicitly names it as a "defensive" leak catch.

**Verdict:** drift symptom. K-b1 fix (resurrect un-mark) closes the underlying race for the paired sites. K-b2 task: audit `menuhandlerAcceptMission` to call `menuPopDialog` so the solomission defensive pops become unnecessary. K-b3 task: delete the bare defensive pop sites once the audit confirms the underlying paths are pool-managed.

### F. `menupoolReleaseAll()` paired sites

Five force-close paths call `menupoolReleaseAll()` to nuke every slot at a stage transition or disconnect:

| File | Line | Site |
|---|---|---|
| `port/fast3d/pdgui_bridge.c:805,833,885` | endscreen exit (3 paths) |
| `port/src/net/net.c:1278` | netDisconnect |
| `port/src/net/matchsetup.c:858,958` | matchStart, post-match cleanup |
| `port/src/net/netmsg.c:1589,1743` | SVC_STAGE_START (CS, co-op) |

Each of these is correct on its own -- the pool needs to be cleaned at hard transitions. The drift problem isn't `menupoolReleaseAll` itself; it's the trailing direct ctx pop. See E.

### G. Cursor-visibility authorities (race source)

| Authority | Function | File:Line | Trigger |
|---|---|---|---|
| **A1: inputctx mode sync** | `inputCtxSyncMouseMode` | `port/src/inputctx.c:481-503` | Called at end-of-frame (`inputCtxEndFrame`) and on every push/pop. |
| **A2: ImGui SDL backend** | `ImGui_ImplSDL2_UpdateMouseCursor` | `port/fast3d/imgui/imgui_impl_sdl2.cpp:628-652` | Called every frame from `ImGui_ImplSDL2_NewFrame`. |

Both call `SDL_ShowCursor`. They drive off different state:

- A1: top-of-stack identity. Gameplay → DISABLE; anything else → ENABLE.
- A2: `io.MouseDrawCursor || imgui_cursor == ImGuiMouseCursor_None` → DISABLE; otherwise SET_CURSOR + ENABLE.

In menu mode they happen to agree: A1 wants ENABLE; A2 also wants ENABLE (default `io.MouseDrawCursor=false`, default cursor != None).

In gameplay they disagree: A1 wants DISABLE; A2 wants ENABLE.

**Today's mitigation:** `SDL_SetRelativeMouseMode(SDL_TRUE)` in gameplay implicitly hides the cursor regardless of `SDL_ShowCursor`. So gameplay doesn't see a cursor even though A2 keeps trying to enable it. The race surfaces only at transitions (one frame of cursor flicker) and in pause-menu cases where the relative mode toggles off slightly out of phase with the cursor visibility.

**Issue 3 (pause-cursor / RMB) repro:** in-game pause menu opens, cursor expected to appear. A1 (end of last gameplay frame) just set DISABLE. A2 in the menu's first NewFrame reads `io.MouseDrawCursor=false` and sets ENABLE. inputCtxSyncMouseMode runs end-of-frame and re-sets ENABLE again. By the time the user moves the mouse, the cursor is up. But on Windows, the SDL backend reports `WantCaptureMouse=true` only after the window has been clicked once -- so until the user clicks, ImGui's mouse position is stale and the menu doesn't react to hover. Right-mouse-button click is the workaround because it bypasses the focus-on-click step.

**Verdict:** authority race. Fix: set `io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange` once at backend init so A2 short-circuits at line 631-632 (`return` early) and never calls `SDL_ShowCursor`. A1 becomes sole cursor authority. Filed as K-c.

## Plan

| Item | Action | File(s) |
|---|---|---|
| K-b1 | `menupoolAcquire` already-active branch: explicitly resurrect a marked-for-removal ctx. | `port/src/menupool.c` |
| K-b2 | `menuhandlerAcceptMission` audit: confirm whether it pops the dialog or leaves it active. | `src/game/menu.c` (or wherever the handler lives) |
| K-b3 | Delete defensive pop sites once K-b1 lands. | endscreen, bridge, net.c, matchsetup, netmsg, mainmenu defensive |
| K-c | `ImGuiConfigFlags_NoMouseCursorChange` at backend init. | `port/fast3d/pdgui_backend.cpp` |
| K-d | Post-acquire / pre-release assertion in menupool. | `port/src/menupool.c` |
| K-e | Mark Issue 2 / Issue 3 STRUCTURALLY-RESOLVED-PENDING-PLAYTEST. | `context/bugs.md` |
| K-f | Methodology doc one-pager. | `context/designs/input-authority-methodology.md` |

K-b1 is the structural fix that turns most of K-b3 into mechanical deletes. K-c is small and surgical. K-d is a fresh assertion. K-b2 is exploratory -- if `menuhandlerAcceptMission` already cascades cleanly, the solomission defensive pops are also mechanical deletes.

## Co-existence rule

The audit found zero overlap with the FP-weapon investigation paths. None of these files are on the off-limits list:

- `src/game/inv*.c`, `src/game/bondinit.c`, `src/game/bgun.c`, `src/game/wpnload.c` -- not touched.
- `port/src/forge/forge_runtime.c` -- not touched.
- `LOG.WPN.DIAG` instrumentation -- not touched.

K's scope is purely menu / input authority / cursor.
