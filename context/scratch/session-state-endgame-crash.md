# Session State Dump — End Game Crash + Related Match-Lifecycle Bugs

**Written:** 2026-04-13 (compaction-risk checkpoint, ~226 turns)
**Worktree:** `.claude/worktrees/hungry-bose`
**Branch:** `fix-end-game-crash-and-ux`
**HEAD SHA:** `6f562beb2e75bbadd877ce7cf7f65b402c389427`

Purpose: if this session is compacted mid-work, the next session can rehydrate from this file and finish cleanly. Do NOT delete until the merge lands.

---

## 1. Branch & HEAD
- Branch: `fix-end-game-crash-and-ux`
- HEAD SHA (pre-WIP-commit): `6f562beb2e75bbadd877ce7cf7f65b402c389427`
- Main branch for PRs: `dev`
- Working tree (before WIP commit): 2 files modified, clean otherwise.

## 2. Files Modified
Absolute paths (worktree root = `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\.claude\worktrees\hungry-bose\`):

- `port/src/net/net.c` — +10 lines (manifestClear fix in netDisconnect)
- `port/fast3d/pdgui_menu_warning.cpp` — +219 lines (new renderMpEndGameDialog + registration swap)

Diff stat:
```
 port/fast3d/pdgui_menu_warning.cpp | 219 ++++++++++++++++++++++++++++++++++++-
 port/src/net/net.c                 |  10 ++
 2 files changed, 227 insertions(+), 2 deletions(-)
```

## 3. Uncommitted Changes
Both files above are dirty. About to commit as WIP so compaction cannot eat them.

## 4. Root-Cause Findings

### 4a. End Game HARD CRASH (Bug A — 0xc0000005 access violation)
- **Trigger:** MP pause menu → End Game → Confirm.
- **Path:** `menuhandlerMpEndGame` (ingame.c:105-117) → `netDisconnect()` (net.c:935-1002) → `mainChangeToStage(STAGE_CITRAINING)`.
- **Root cause:** `netDisconnect()` calls `mainChangeToStage(STAGE_CITRAINING)` while `g_ClientManifest` still carries the match's entries. `mainChangeToStage` treats CITRAINING as a gameplay stage (STAGE_IS_GAMEPLAY), so with `g_ClientManifest.num_entries > 0` it takes the `manifestMPTransition()` branch and attempts to diff the torn-down MP manifest against whatever is loading for CI training → access violation.
- **Pattern match:** Same class as F-0.4 (pdguiEndscreenExitToMainMenu, pdgui_bridge.c:799) and L1-1 (netmsgSvcStageEndRead, netmsg.c:1419) — both previously received `manifestClear(&g_ClientManifest);` for the same reason. `netDisconnect` was missed.
- **Status:** FIXED in net.c (see §6).

### 4b. Airbase crash (implied — NOT YET INVESTIGATED)
- User's urgent state-dump prompt mentions "the Airbase crash" but I did not investigate a distinct Airbase crash this session. Need to check with user whether this is a separate report or part of the same End Game crash bucket.
- **Status:** UNRESOLVED, needs user clarification.

### 4c. Post-game endscreen fails to appear (Bug C)
- **Trigger:** Match ends naturally (time/score limit) OR End Game press. Endscreen does not render.
- **Log evidence** (`context/scratch/crash-2026-04-13-postgame/pdclient.log`, 3.5 MB):
  - `10:29.22` ACTION_PAUSE
  - `10:32.02` STATS: saved (2.8s nav)
  - `10:32.02` pause_menu IMC popped
  - `10:32.03` `mpPushEndscreenDialog` logged
  - `10:32.04` syncMouseMode → relative (gameplay)  ← anomaly
  - `10:36.76` shutdown
- **Anomaly:** After `mpPushEndscreenDialog`, mouse went back to gameplay-relative mode. If the endscreen render had run, `renderMpEndscreen`'s `IsWindowAppearing` branch would have pushed `g_CtxImGuiMenu` (which forces absolute/menu mouse). Grep for `GAME OVER|endscreen render|MpEndscreenInd` in pdclient.log returned empty → the hotswap renderer never executed.
- **Hypothesis (user's, primary):** `manifestClear(&g_ClientManifest)` in `netmsgSvcStageEndRead` runs BEFORE the endscreen reads the player-score/stage/chr-model data it needs. If the renderer tries to pull from a cleared manifest, it either crashes silently or bails out.
- **Hypothesis (mine, secondary):** Race — `mainEndStage` synchronously pushes the endscreen dialog, then `mainChangeToStage(STAGE_CITRAINING)` (triggered downstream by netDisconnect in the End-Game path, or by the subsequent tick in the natural-end path) tears down the dialog stack before any frame renders.
- **Status:** UNRESOLVED. Next steps in §7.

### 4d. False kill counter — 17 kills at match start (NEW, user raised during compaction-risk message)
- **Log:** `context/scratch/playtest-2026-04-13-false-kills/pdclient.log` (~700 KB).
- **Symptom:** Fresh match; player opens pause menu immediately after spawn; score shows 17 kills already, no actions taken.
- **User's candidate causes** (priority-ordered):
  1. `g_PlayerScores[]` not zeroed on match-start.
  2. `SVC_PLAYER_SCORES` replay/resend from previous match delivering stale values.
  3. `NET_RESYNC_FLAG_SCORES` (new from L5) re-sending a captured snapshot with old kill counts.
  4. Bot kill attribution to player slot 0 after misaligned index post-manifestBuild.
- **Status:** UNRESOLVED, not yet investigated.

### 4e. addr2line symbols
- Not captured this session. Crash did not produce a usable stack trace pointing at exact line — diagnosis came from code reading + log timeline + pattern-matching against the two prior teardown fixes (F-0.4, L1-1).

## 5. Unified Hypothesis — Lifecycle Ordering Bug Class

All four symptoms are plausibly one class: **match-state teardown + init ordering faults**. Specifically, `manifestClear` / score reset / stage change are being invoked at wrong points in the sequence, either before consumers have read the data they need (4c) or before subsequent consumers have overwritten stale data (4d). The End Game crash (4a) is the strongest direct evidence — fix already applied. 4c and 4d need the same "what reads this, when?" audit. 4b unclear until user clarifies.

Canonical sequence that likely needs enforcing at match end:
1. `SVC_STAGE_END` received.
2. Player-scores resync (final snapshot).
3. Endscreen dialog pushed AND data snapshot captured from manifest.
4. Only THEN: `manifestClear` + stage tear-down.

Canonical sequence at match start:
1. `CLC_LOBBY_START`.
2. Zero-init `g_PlayerScores[]` and any resync flags BEFORE `manifestBuild`.
3. `manifestBuild` populates from SVC_MATCH_MANIFEST.
4. `mainChangeToStage` into the gameplay stage.
5. Resync flags consumed (not replayed from a prior match).

## 6. Code Changes Made So Far

### 6a. `port/src/net/net.c` (FIX — Bug A root cause)
- Added `#include "net/netmanifest.h"` after `matchsetup.h` include block.
- In `netDisconnect()`, inserted `manifestClear(&g_ClientManifest);` immediately before `mainChangeToStage(STAGE_CITRAINING);`, with explanatory block comment referencing F-0.4 and L1-1 as sibling fixes.

### 6b. `port/fast3d/pdgui_menu_warning.cpp` (Bug B — controller-friendly modal)
- Added new function `renderMpEndGameDialog` (~150 lines) — custom first-class modal confirmation:
  - `pdguiPopupDarkenBehind(0.60f)` scrim, palette 2 (danger/red), 540×260 scaled window.
  - `ImGui::Begin` with `NoTitleBar | NoBackground | NoScrollbar`.
  - `PdDialog` frame, yellow title "End Match?", centered body text.
  - Cancel button (default focus) + red "End Match" button.
  - Hint row via `ImGui::TextDisabled`: `[Enter/Space/(A)] Confirm     [Esc/(B)] Cancel`.
  - Keybindings: Enter / KeypadEnter / Space / ImGuiKey_GamepadFaceDown → `doConfirm`; Escape / ImGuiKey_GamepadFaceRight → `doCancel`.
  - On Confirm: iterate dialog items, find SELECTABLE with handler, invoke `MENUOP_SET`, then `menuPopDialog()`.
  - On Cancel: `pdguiPlaySound(PDGUI_SND_KBCANCEL)`, then `menuPopDialog()`.
- Updated registration: `pdguiHotswapRegister(&g_MpEndGameMenuDialog, renderMpEndGameDialog, "MP End Game (modal confirm)");` (was `renderDangerDialog`).

### 6c. NOT YET ADDRESSED
- Bug C (postgame endscreen failure) — no code change.
- Bug D (false kills) — no code change.
- Bug 4b (Airbase) — unclear.

## 7. Pending Work — Punch List

In order. Do not merge until all items complete.

1. **[WIP commit pending]** Commit the current two-file change as WIP (`fix(wip): end game crash + modal confirm — pre-compaction snapshot`).
2. **Bug C investigation** — determine whether endscreen renderer silently bails due to cleared manifest, OR whether a race tears down the dialog before render:
   - Read hotswap render invocation path (how pdgui_hotswap invokes registered renderers — gated on legacy menuRender, or unconditional).
   - Check whether `mainChangeToStage` is called from netDisconnect BEFORE the endscreen can render its first frame.
   - Check whether `g_MainIsEndscreen = 1` (set in mainEndStage) persists through the stage change, or is reset.
   - Decision: either reorder `manifestClear` in netmsgSvcStageEndRead to run AFTER endscreen captures its snapshot, OR capture a manifest-snapshot for the endscreen to own (preferable — doesn't destabilize the post-match teardown).
3. **Bug D investigation** — read `context/scratch/playtest-2026-04-13-false-kills/pdclient.log`, grep for `SVC_PLAYER_SCORES`, `NET_RESYNC_FLAG_SCORES`, `kills=`, `manifestBuild`, `CLC_LOBBY_START`, `mainChangeToStage`. Determine which of the four candidate causes is live. Fix at match-start init path.
4. **Bug 4b clarification** — ask user whether "Airbase crash" is a distinct bug or the same End Game crash reproduced on Airbase.
5. **Propagation audit** — grep all `mainChangeToStage` callsites: pdgui_bridge.c:799 (F-0.4 ✓), pdmain.c:771, netmsg.c:1419 (L1-1 ✓), netmsg.c:4534, netmsg.c:5144, net.c (this fix ✓). Confirm no other callsite needs the manifestClear pattern.
6. **Build-verify** — both `pd` and `pd-server` targets via `source devtools/build-env.sh && ninja -C Build pd pd-server`. Per the build_verify feedback memory, this is FULL LINK, not per-file compile.
7. **Context updates (required before merge):**
   - `context/bugs.md` — entries for B-End-Game-Crash, B-End-Game-UX, B-Postgame-Endscreen, B-False-Kills.
   - `context/systemic-bugs.md` — flag "match-state lifecycle ordering" pattern.
   - `context/tasks-current.md` — mark End Game UX DONE; add remaining bugs as active.
   - `context/session-log.md` — session entry.
4. **Git discipline (Standing Order 9):**
   - Pre-merge snapshot: record HEAD SHA and `git diff --stat` on the branch and on dev.
   - `--no-ff` merge from `fix-end-game-crash-and-ux` into `dev`.
   - Post-merge verification: line counts of changed files must match branch-side; halt and report on any unexpected shrink.
5. **Final report** to user: root cause per bug, files touched, build status, bug IDs assigned in context/bugs.md.

## 8. Recovery Recipe (if compacted)

If you are reading this file in a fresh session:

1. `git log --oneline -5` in the worktree to find the WIP commit (message starts `fix(wip): end game crash`).
2. Read this file fully, then re-read the three source files listed in §2 to see the applied edits.
3. Re-read `context/constraints.md` and `context/session-log.md` for project conventions.
4. Pick up at §7 item 2 (Bug C investigation) unless user directs otherwise.
5. Do not repeat work already listed in §6 — those edits are already in the tree.

---

**End of state dump.** Writing WIP commit next.
