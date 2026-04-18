# PD2 PROJECT CONTEXT BRIEFING — Dispatch Handoff

**Generated:** 2026-04-14 07:05 EDT (scheduled task `pd2-context-briefing`)
**Branch:** `dev` @ `18887aaa` (39 commits ahead of `origin/dev`, unpushed)
**Build Health:** ⚠ **NOT VERIFIED THIS RUN** — Cowork sandbox does not have access to Windows filesystem (`/c/`) or MSYS2 toolchain. Last known: `pd` + `pd-server` clean on `18887aaa` per S253 session log (`287b0bc4` post-merge fix verified; 8 files +517/-141 build clean).

---

## 🔴 CRITICAL FINDING — Working Tree Truncation

**Before any code session starts, Mike must resolve the uncommitted working-tree state.** The working tree at `/sessions/stoic-festive-lamport/mnt/Perfect-Dark-2/perfect_dark-mike` shows **74 modified files with -1,281 net lines of code** (13,190 ins / 14,900 del) against `HEAD` (`18887aaa`). These are **unstaged, uncommitted** changes.

Inspected diffs show the modifications are removing code that was just landed, not adding it:

| File | Delta | What's being deleted |
|------|-------|----------------------|
| `port/src/net/netmsg.c` | -280 | The entire R-5 Room Settings + Playlist sync block (`SVC_ROOM_SETTINGS 0x78`, `SVC_ROOM_PLAYLIST 0x79`, CLC `0x13`/`0x14`) — the S253 deliverable merged as `82d0c1f3` / `287b0bc4`. |
| `port/include/inputctx.h` | -141 (of 246) | ~57% of the header — the priority-based input context stack documentation and declarations. S250 territory. |
| `port/src/inputctx.c` | -122 | S250 Phase-1 input-authority wiring (`gameplayInputSuppressed()`, `actionmapFlushGameplayState()` etc.). |
| `port/src/actionmap.cpp` | -80 | Dispatch/read-gate work from S250. |
| `port/src/audio.c` | -105 | S249 (B-140 `NETMODE_SERVER_AUDIO` fix) + S251 B-141 telemetry territory. |
| `port/fast3d/pdgui_menu_warning.cpp` | -161 | S252 `renderMpEndGameDialog` modal confirm. |
| `port/fast3d/pdgui_menu_room.cpp` | -75 | S253 Weapons F-2.1 TreeNode sort + room-settings send sites. |
| `src/game/mplayer/mplayer.c` | -5 | Around line 3734 — S252 B-142 `mpPlayerGetIndex(NULL)` early-return guard. |

**This matches exactly the scenario CLAUDE.md §9 ("Git Safety — Worktree Operations") warns about** and references the prior "file truncation incident." Per the standing order:

> any file that shrank unexpectedly = halt and report to the user before continuing.

**Likely cause:** The mounted Cowork copy at `/sessions/stoic-festive-lamport/mnt/Perfect-Dark-2/...` has drifted out of sync with the canonical repo at `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\`. The context files (README, tasks-current, session-log, bugs) all describe the post-S253 state, but the source tree reflects a pre-S248 state. Candidates (in likelihood order):

1. A worktree operation (stash pop, merge, rebase) in a prior session silently clobbered the main working copy, and the CLAUDE.md §9 snapshot check didn't catch it.
2. The Cowork mount is showing a stale snapshot of a different branch checkout while the real repo on the Windows side has the S248–S253 work intact.
3. An editor-side revert across multiple files.

**RECOMMENDED FIRST ACTION for Mike (no Dispatch session yet):**

```bash
cd C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike
git status
git stash show -p stash@{0}   # claude/busy-lalande WIP — may contain the missing work
git log --oneline --all -- port/src/net/netmsg.c | head -10
```

- If the Windows-side `git status` is clean on `dev @ 18887aaa`: this is a Cowork mount-staleness artifact only; ignore the sandbox diff and proceed. Skip step below.
- If the Windows-side shows the same -1281 line truncation: **do not commit, do not push.** Find the post-S253 blobs via `git show 287b0bc4:port/src/net/netmsg.c` / `git show 82d0c1f3:port/include/inputctx.h` etc. and restore from SHA. All the work from S248–S253 is safe *in the commit graph* (commits `93c64f6c`, `e13c2d1f`, `5098f903`, `5a42f234`, `d37e9677`, `4d1e13c1`, `82d0c1f3`, `287b0bc4`) — the truncation is only in the working tree.
- Until resolved, **no new code sessions should be dispatched.** Context-only / planning sessions are safe; any `write` to the affected files risks stacking additional damage on top of a corrupt working tree.

There are also **4 untracked directories** under `context/scratch/` (crash-2026-04-13-chicago/, crash-2026-04-13-postgame/, crash-2026-04-13/, playtest-2026-04-13-false-kills/) — likely Mike's playtest capture drops from yesterday; preserve before any `git clean`.

---

## Git State Summary

- **Branch:** `dev`, 39 commits ahead of `origin/dev`. No push has happened since the S245 updater drop (`76631a32`-era) — the entire 2026-04-13 stabilization batch S248–S253 is unpushed. Recommend pushing once working-tree state is verified clean.
- **Uncommitted changes:** 74 files modified, 4 untracked directories (see above).
- **Stashes (4):**
  - `stash@{0}: On claude/busy-lalande: b6-polish-pre-rebase-stash` — pre-rebase safety stash, branch still exists
  - `stash@{1}: WIP on dev: e1ba2344 chore: auto-commit before release v0.0.56`
  - `stash@{2}: WIP on dev: 195052ba chore: auto-commit before release v0.0.51`
  - `stash@{3}: WIP on dev: b597b2b2 Build v0.0.51 - auto-commit before build`
  — All release-auto-commit stashes; safe to drop once Mike confirms, but the `b6-polish-pre-rebase-stash` should be inspected (`git stash show -p stash@{0}`) before discarding — it may contain relevant in-flight work given the truncation situation.
- **Worktrees:** `git worktree list` shows 20+ entries, ALL tagged `prunable`. One has `00000000 (error)` (`happy-hofstadter`). Recommend `git worktree prune` once Mike confirms no unmerged work in any of them.
- **Branches:** ~40 `claude/*` branches exist locally. The merged ones (`great-carson` S253, `loving-noether` context pass, `happy-hofstadter` S250/S251, `hungry-bose` S252, `exciting-turing` S249) are safe to delete; I did not enumerate each for unmerged commits in this run — see "Unmerged Worktree Work" section.

---

## Active Workstreams

Derived from `tasks-current.md` (post-S253) and `bugs.md`. Organised by readiness.

### 1. Playtest verification of the 2026-04-13 drop (S248–S253)
**Status:** Blocked on Mike's next playtest; no AI work needed unless repros produce new bugs.
**Items:** Issue 7 room-settings sync, B-140 Issue B two-panel Select Tunes, Issue 2/8 theme rescan, B-142 false kills, B-143 End-Game-Crash + modal confirm, B-141 telemetry repro, B-134 spawn validator, S250 input authority Phase 1, dev-window-v2 polish. Each has a commit SHA in `tasks-current.md` for traceability.
**Key files:** n/a (verification only).

### 2. Bug B — countdown lingers after room close
**Task IDs:** Bug B (no B-number yet; in post-drop punch list).
**Status:** Ready to pick up. Was off-limits during parallel End-Game-Crash session. No dependencies.
**Key files:** `port/src/net/netmsg.c` (`readyGateTickCountdown()`), possibly `port/src/net/net.c`.
**Last known state:** Server countdown keeps ticking after leader closes the room and fires into the next room. Fix is in ticker logic — reset `readyGate` state on room close / leader change.
**Next action:** Trace `readyGate*` call sites; add room-close predicate to the gate tick.

### 3. Bug C — post-game endscreen partial render
**Status:** Needs instrumentation first, then playtest.
**Key files:** `renderMpEndscreen` (likely `port/fast3d/pdgui_menu_endscreen.cpp` or similar).
**Last known state:** Scrim + title bar render, body content invisible. Six hypotheses listed in `context/scratch/archive/2026-04-13/session-state-endgame-crash.md §4c`.
**Next action:** Add `sysLogPrintf` on each early-return in `renderMpEndscreen`; fresh playtest log; narrow to offending early-return.

### 4. Bug D — invisible networked bots on Chicago
**Status:** May have cleared with S253 drop. Needs post-drop repro before any AI work.
**Key files:** `chr.c`, `chraction.c`, possibly manifest path. FIX-A.2 chr generation-token area.

### 5. Airbase Start-Match 0xc0000005 no-response
**Status:** May share root cause with B-143. Needs post-drop repro.
**Key files:** `port/src/net/netmsg.c` (SVC_STAGE_START path), `netmanifest.c`.

### 6. Chicago silent crash ~9 s
**Status:** Needs VEH log + symbolify from fresh repro. Mike must capture.

### 7. Input Authority Phase 2 — menu pool single-instance discipline
**Status:** Queued — own session, substantial scope.
**ADR:** `context/designs/input-authority-and-menu-pool-2026-04-13.md §6`.
**Key files:** `src/game/menu.c`, `port/fast3d/pdgui_backend.cpp`, possibly new `port/src/menupool.c`.
**Dependencies:** Phase 1 shipped S250. No blocking deps.

### 8. B-141 audio skips — root cause
**Status:** Blocked on repro against telemetry shipped in S251 (`5a42f234`).
**Key files:** `port/src/audio.c`, `port/include/audio.h`.
**Next action:** When Mike hits a skip, tail `pd.log` for `AUDIO[B-141]`; AI narrows mechanism by dominant counter (hitch/underrun/drop).

### 9. FIX-B.1 Deep manifest scanner (cinematics + AI scripts)
**Task IDs:** FIX-B.1.
**Status:** Open. FIX-B.2 done. Not blocking v0.1.0.
**Key files:** `port/src/net/netmanifest.c`, `src/game/setup.c`.
**Detail:** Cinematics + AI scripts spawn assets not in the setup list.

### 10. Manifest gap follow-ups
**Status:** Open; lower priority than above.
**Sub-items:** (1) Title/menu stage has no manifest — Skin Editor mod chars silently missing; fix: new `manifestBuildForMenu()`. (2) SP pre-scan timing — split `manifestBuildMission()` into pre/post-load phases. (3) Cutscene cinema models never in manifest — safety net only.

### 11. Build / Release verification (tail items)
- **Static link / DLL elimination** — S224 DONE, Mike to `objdump -p` verify.
- **L0-LINK `pdguiThemeRegisterModDir` server link** — S231 DONE code-side, build verify pending.
- **L0-BUILD ccache warm-build regression** — S231 code done, warm-build timing pending (target: warm `pd` <12 s).

---

## Collision Map

Given the set of active workstreams, here are the file-level risks if two sessions run in parallel:

| Resource at risk | Conflicting workstreams | Type | Mitigation |
|------------------|------------------------|------|------------|
| **`port/src/net/netmsg.c`** | Bug B (countdown reset at room close), any SVC_*/CLC_* protocol additions, FIX-B.1 (if it bumps manifest msg), future mod system work | Edit conflict + protocol-version coupling | **Serialize.** Only one session at a time. Any protocol addition must bump `NET_PROTOCOL_VER` (currently 35) with explicit handoff if two features need protocol bumps. |
| **`port/src/net/net.c`** | Bug B (if gate state lives on `netclient`), NAT traversal follow-ups, any `netDisconnect` touchpoint | Edit conflict + shared state | Serialize. |
| **`port/src/audio.c` / `audio.h`** | B-141 root cause (when unblocked), any future audio-mod features | Edit conflict | Serialize. S249/S251 shipped clean from same file by staying scoped; pattern repeatable. |
| **`src/game/menu.c`** | Input Authority Phase 2 (menu pool), any menu-screen work (pdgui_menu_*.cpp is separate) | Edit conflict — Phase 2 is a large refactor | **Phase 2 must run alone.** No parallel menu work while it's in flight. |
| **`port/fast3d/pdgui_backend.cpp`** | Input Authority Phase 2, any ImGui nav tweaks | Edit conflict | Serialize with Phase 2. |
| **`port/fast3d/pdgui_menu_*.cpp` (screens)** | Bug C instrumentation (`pdgui_menu_endscreen.cpp`), any screen polish | Per-file: edit conflict only if same screen | Parallel-safe **across different screens**. Two sessions can edit `pdgui_menu_room.cpp` and `pdgui_menu_endscreen.cpp` simultaneously. Must NOT share a single screen file. |
| **`port/src/net/netmanifest.c` + `src/game/setup.c`** | FIX-B.1 deep scanner, Manifest gap follow-ups (all three sub-items) | Edit conflict + logical dependency | Serialize. Same author, same session ideally. |
| **`CMakeLists.txt`** | Any new .c/.h file addition (GLOB_RECURSE auto-picks, but CMake regeneration needed) | Rare conflict | Generally safe; GLOB_RECURSE autodiscovers. Flag only if two sessions both add build-configuration options. |
| **`port/src/hub.c`** (server) | B-140 residuals, any server-tick feature (match timer, STUN, NAT) | Edit conflict + shared tick state | Serialize. |
| **`context/` files (tasks-current.md, session-log.md, bugs.md, systemic-bugs.md)** | **EVERY session updates these** | Merge conflict on parallel session-end updates | **Mandate append-only where possible; each session must rebase-merge the context block on exit.** Already seen twice historically. Consider: spawn-log update before code-merge so the code-merge conflict window is smaller. |

**Verdict:** The collision surface is concentrated in three files (`netmsg.c`, `net.c`, `netmanifest.c`) and the shared context docs. Most menu-screen work and most gameplay-subsystem work (collision, spawn, mplayer) is parallel-safe.

---

## Recommended Session Plan

**Preamble (Mike, before dispatching anything):** Resolve the working-tree truncation per the Critical Finding. Until that's clean, no code sessions.

Once clean, the following wave can safely run in parallel (different files, no protocol bump):

**Wave A (parallel-safe, same time):**
1. **Bug C instrumentation** — scope: `port/fast3d/pdgui_menu_endscreen.cpp` (or equivalent). Small, self-contained. Needs fresh playtest log afterward.
2. **Bug B countdown reset** — scope: `port/src/net/netmsg.c` `readyGateTickCountdown`. Small, protocol-compatible (state-only fix).
3. **FIX-B.1 deep manifest scanner** — scope: `netmanifest.c`, `setup.c`. Independent of A1/A2.

**Wait.** Wave A's Bug B touches `netmsg.c`; only one of {Bug B, FIX-B.1 if it ever touches netmsg.c} should be in flight. If FIX-B.1 stays inside `netmanifest.c` / `setup.c`, they are parallel-safe. Flag if scope creeps.

**Wave B (serial, after A lands):**
4. **Input Authority Phase 2** — solo session. Touches `src/game/menu.c` + `pdgui_backend.cpp` + possibly new file. Do NOT run anything else against menu or input while this is open.

**Deferred / needs Mike:**
- B-141 root cause (blocked on repro).
- Bug D, Chicago ~9s crash, Airbase no-response (may have cleared post-drop; need repro first).
- Playtest verification items — Mike-only.

**Never-delegate-without-review:**
- Any protocol version bump (>v35) — cross-session coordination required.
- Any worktree operation during a session (stash/rebase/reset) — CLAUDE.md §9 requires pre-op snapshot.
- Any `context/` bulk edit — must sync canonical (`perfect_dark-mike/context/`) and parent copy.

---

## Critical Build/Project Notes — Ship with Every Session

Every Dispatch session prompt must include:

1. **Working copy discipline.** Work ONLY in the main working copy (`perfect_dark-mike/`). CLAUDE.md §9 explicitly says no new worktrees — changes land in main. Edit existing files with the `Edit` tool; never `Write` over an existing source file (truncation risk).
2. **Build environment.** Every session, before any build:
   - Bash: `source devtools/build-env.sh && ninja -C Build pd pd-server`
   - PowerShell: `.\devtools\build-headless.ps1` (self-configures env)
   Do NOT re-derive `TEMP` or `PATH` manually; `_build-env-prelude.ps1` / `build-env.sh` handle it (S247).
3. **Types discipline.** `types.h` defines `#define bool s32`. **Use `s32` in legacy game code, not `_Bool`.** For new port code (`port/`), `<stdbool.h>` + `bool` is fine (see CLAUDE.md "New code types").
4. **Protocol version.** `NET_PROTOCOL_VER = 35` (as of S253). Any additive message is safe; any reorder/removal requires a bump + coordinated handoff. Document the bump in `context/networking.md`.
5. **Build-verify before reporting done.** Both targets (`pd` AND `pd-server`). `pd-server` does NOT link `audio.c` (audio-only sessions only need `pd`); otherwise both.
6. **Merge discipline.** Every session must merge its changes into `dev` (main working copy), verify both targets build clean, and confirm the merge is clean (post-merge line-count check per CLAUDE.md §9) before reporting done. No handing off unmerged work.
7. **Context update discipline.** Per CLAUDE.md §4 — decision made / bug found / task completed → update the relevant context file **immediately**, not at session end. Canonical location: `perfect_dark-mike/context/`, then sync parent copy at `Perfect-Dark-2/`.
8. **Pre-task sanity check.** Constraint / root-cause / scope / cascade / effort. Especially: **check `context/constraints.md` "Removed Constraints"** — if task complexity assumes a constraint we've dropped, simpler approach wins.
9. **Git safety on worktree ops.** No bare `git stash`, no `git reset --hard` without explicit user instruction, record HEAD + file line-counts before any rebase/reset/merge (`devtools/git-snapshot.sh`), verify post-op.
10. **Copyright + source fidelity.** Decomp code in `src/` is Rare IP under its own licensing; port additions in `port/` are our own. Don't cross-wire.

---

## Open Questions / Blockers (Mike's desk)

1. **[TOP PRIORITY] Working-tree truncation.** Resolve before any code session. See Critical Finding.
2. **Push `dev`.** 39 commits unpushed. Recommend push once truncation resolved and working tree is clean.
3. **Playtest pass on 2026-04-13 drop.** Nine items waiting for your next session at the keyboard — see tasks-current.md "Playtest verification" block.
4. **Worktree prune.** 20+ prunable worktrees and ~40 `claude/*` branches. Would you like an AI session to enumerate-and-prune (enumerate only; you approve the delete list)?
5. **Stash @{0} inspection.** `b6-polish-pre-rebase-stash` on `claude/busy-lalande` predates the MP lobby drop — is it still relevant, or safe to drop? (Inspect before deleting.)
6. **Build verification for S224 / S231 deliverables.** `objdump -p PerfectDark.exe | grep "DLL Name"` (expect opengl32 + system only) + warm-build timing (expect <12 s). Both blocked on a clean build from a clean tree.

---

## Files Touched During This Briefing

This run was read-only against context/ and git plumbing commands. No source-tree writes, no git operations, no worktree changes. Briefing saved to:

`context/scratch/dispatch-briefing-2026-04-14.md`

(Not archived to a dated subdir; this is the current working briefing.)
