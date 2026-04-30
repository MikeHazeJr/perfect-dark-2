# Regression investigation -- 2026-04-24

**Reported:** Mike, post-playtest of an earlier-today build.
**Symptoms:**

1. Weapon invisible / unusable in BOTH Campaign and Combat Sim (not Grid).
2. CS back-out stuck: no menu visible, no input authority anywhere.
3. Pause-menu cursor missing while RMB still routes correctly. Two parallel input states drifting.

**Method:** static read of today's commit range (`7ff165b0..7be9a1a1`); no runtime trace yet.

---

## Issue #3 (pause cursor) -- ROOT CAUSE LOCATED, structural

Two independent authorities call `SDL_ShowCursor` every frame and race:

- `inputCtxSyncMouseMode` (`port/src/inputctx.c:481-503`): sets cursor based on top of input-context stack. Menu on top -> cursor shown.
- `ImGui_ImplSDL2_UpdateMouseCursor` (`port/fast3d/imgui/imgui_impl_sdl2.cpp:628-652`): sets cursor based on `io.MouseDrawCursor` and ImGui's preferred cursor. If `MouseDrawCursor` is set OR ImGui requests `Cursor_None`, calls `SDL_ShowCursor(SDL_FALSE)` every frame.

When the pause menu opens, `inputCtxSyncMouseMode` shows the cursor; if any frame has `io.MouseDrawCursor` flipped (ImGui draws its own cursor) the SDL2 backend immediately hides it. RMB clicks still route through SDL events because the cursor visibility is independent of mouse-button event delivery -- exactly Mike's symptom.

This bug pre-dates today's commits but matches Mike's Issue 3 framing. Fix is whichever of (a) make `inputCtxSyncMouseMode` authoritative and gate the ImGui path on it, or (b) collapse cursor visibility into a single owner. Belongs to Priority K.

---

## Issue #2 (CS back-out stuck) -- structural drift, suspect K-class

The "menu gone but no input authority" pattern is the classic two-stack drift Mike named when scoping Priority K: `menupool` pops the visual but `inputctx` does not pop in step (or vice versa). I cannot fingerprint the exact pop-asymmetry site from a static read alone -- the CS menu chain is multi-deep and the offending pop could be in any of the room / lobby / matchsetup teardown paths.

Hypothesis worth testing in the next runtime trace: the new `g_ImcForge` (`29d05d49`) and `g_ImcForgeSession` (`472faddd`) introduced two new IMC activations that get pushed in `forgeTick` and popped in `forgeTransitionToInactive`. If a CS back-out path ever activates these IMCs (it shouldn't -- they should only fire in a Grid session), they could be left stuck active. Audit step: confirm none of the CS menu paths transitively call `imcActivate(&g_ImcForge*)`.

---

## Issue #1 (weapons invisible in Campaign + CS) -- NO single static smoking gun

The hypothesis-list from the brief refuted on static read:

- **`forgeIsFreefly()` leaking into non-FREEFLY contexts: REFUTED.** Predicate is clean (`src/game/forgemode.c:457-460`: `return s_forge.state == FORGE_SESSION_FREEFLY`). `s_forge.state` is module-static, zero-init = INACTIVE, transitions are written through three named functions only.
- **`actionIsBlockedInFreefly` overbroad: REFUTED.** Predicate fires only when `forgeIsFreefly()` is true (`port/src/actionmap.cpp:1238` etc.). In Campaign / CS that's false, so FIRE_PRIMARY / FIRE_SECONDARY / WEAPON_* read normally.
- **Bind dropouts from 472faddd: REFUTED.** Diff confirms only FORGE_* binds moved out of gameplay IMC; FIRE_PRIMARY (`actionmap.cpp:1993` JOFS_RTRIG), FIRE_SECONDARY (`:1995` JOFS_LTRIG), SPRINT (LSHIFT), CROUCH (LCTRL) all retained.
- **Chr-swap leak via NULL-currentplayer at `forgeTransitionToInactive`: REAL but unlikely to render weapon invisible.** When stage transitions to TITLE and `forgeCurrentPlayer()` returns NULL, `forgeRestorePlayerMode` is skipped (`forgemode.c:369-372`); `has_saved_body` stays true with stale `saved_bodynum`. But the chr being torn down means the dangling DRCAROLL bodynum does not propagate to the next stage -- new chrs get fresh bodynums from the spawn pipeline. Worth fixing as a follow-on (reset `s_forge.fly.has_saved_body = false` at the top of `forgeTransitionToInactive` regardless of `p`) but probably not Mike's reported regression.
- **Issue 10 rig_class: REFUTED for player chr.** The new `catalogGetBodyValidHeadIds` is consumed by Character Select / pickRandomHead paths only. Player chr spawn uses `chr->bodynum`/`chr->headnum` integer indices, not the rig_class string match.
- **B-228a Option E (dd622f2a): REFUTED for Campaign.** The SP-overlay block is gated on `g_Vars.mplayerisrunning` (`src/game/setup.c` overlay site). Campaign solo has `mplayerisrunning == false`. Could affect CS hosting an SP-in-MP class arena (CITRAINING / CHICAGO / VILLA / INFILTRATION / G5BUILDING / PELAGIC) but the symptom would manifest there, not in Campaign.

**What I cannot rule out from static read:**

- A regression in the music-sync path (`373bd6ec`) that affects audio buffer alignment in a way that cascades into model load (unlikely but unverified).
- A regression in the actionmap dispatch from the new ACTION_COUNT bump 62 -> 68 (`29d05d49`). If any consumer iterates `s_State[player][...]` with a stale ACTION_COUNT-based array bound elsewhere, off-by-six writes are possible. Audit candidate: `s_BindStr`, `s_ActionHoldMsOverride`, `s_State` -- all compile-time `[ACTION_COUNT]` so should size correctly, but worth checking that no header-bound consumer hard-codes 62.
- The pause cursor / RMB issue (#3) and the back-out stuck issue (#2) being **the same root cause as #1** -- if some menu-open path leaves gameplay input suppressed even after the menu closes, `actionPressed(0, ACTION_FIRE_PRIMARY)` returns 0, and the user perceives "weapon doesn't work" indistinguishably from "weapon model missing." Worth checking on the runtime log whether Mike sees the FP weapon model in the world or just no fire effect on click.

---

## Recommendation

1. **Wait for Mike's log.** `MUSIC.SYNC:`, `GRID:`, `INPUTCTX:`, `MATCHSTART.DIAG:` lines should fingerprint which mechanism is biting.
2. **In parallel, fix the body-swap NULL-restore asymmetry as a hardening pass** (reset `has_saved_body=false` unconditionally at top of `forgeTransitionToInactive`). Cheap, harmless, removes a real follow-on bug regardless of whether it relates to Mike's report.
3. **Issue #2 and #3 are Priority K territory.** They may have been latent before today and surfaced by today's IMC churn. The structural collapse-to-one-stack work K describes is the right fix, not a point patch.

**Word count: ~620 (over the 400 target -- happy to compress).**
