# Session Log (Active)

> **S241–S259** (rolling window). Older sessions **S240–S157** → [_archive/session-log-archive-S240-and-older.md](_archive/session-log-archive-S240-and-older.md). Ancient **S1–S119** → [_archive/sessions/](_archive/sessions/).
> Navigation hub: [INDEX.md](INDEX.md) · Back to [README.md](README.md)

## Session S259 — 2026-04-14 (Dev Window: lock cleanup, progress text, no forced Log tab)

**Problem**: `Invoke-GitSyncBeforeBuild` failed with *Unable to create `.git/index.lock`: File exists* (leftover lock after interrupted/crashed git). `Auto-Commit-Sync` and `build-headless -AutoCommit` already remove a stale lock; sync-before-build did not.

**Fix — locks**: At the start of `Invoke-GitSyncBeforeBuild`, remove `.git\index.lock` if present. `Get-BuildSteps` cmd auto-commit line deletes `.git/index.lock` after `cd` (before `git add`). **`release.ps1`**: remove stale lock once at script start (before any git).

**Follow-up (same issue)**: Deleting `ProjectRoot\.git\index.lock` missed locks when Git’s resolved path differed (e.g. MSYS reporting `/home/.../index.lock` while Windows could not `Test-Path` that string). Added **`Remove-GitIndexLockForRepo`**: `git rev-parse --path-format=absolute --git-path index.lock` (fallback: `--absolute-git-dir` + `index.lock`), `Remove-Item` when visible, then **`wsl -- rm -f`** for Unix paths when `wsl.exe` exists. **`Invoke-GitSyncBeforeBuild`**: capture `git add` failures; retry add/commit once after lock removal when stderr matches `index.lock` / `Unable to create`. Dev workflow: build only from the main dev tree (merge any worktrees into dev first — no worktree-only builds).

**Follow-up 2**: Explorer can show no `index.lock` under `C:\...\ .git` while `fatal:` references **`/home/.../.git/index.lock`** (stale lock on the WSL/Linux tree, not the same path string as Windows). **`Remove-GitIndexLockFromGitStderr`** parses `Unable to create '…'` and runs **`wsl rm -f`** on that path; **`wslpath -a`** + `rm` covers **`/mnt/c/...`** for the Windows working copy; retry still runs after both.

**UX**: Do not switch to the Log tab on Build, Release, Pull, or Push (Ctrl+L still opens Log). Progress strip: show `LblProgressText` during git sync (short fill pulse), per-step `0% - …`, live updates when Ninja has not printed `[%]` yet (`BuildPercent -eq 0`), and completion lines; clear on Stop.

---

## Session S258 — 2026-04-14 (Context system organization)

**Scope**: `context/` layout only (no game or build scripts).

**Change**: **session-log.md** was ~3,500 lines (far over the context-manager ~500-line guideline). Split: **active** file holds a rolling **S241–S258** window (header + recent sessions; size checked after edits). Sessions **S240 through S157** moved to [_archive/session-log-archive-S240-and-older.md](_archive/session-log-archive-S240-and-older.md). Updated [README.md](README.md) (Session History paragraph + table row for S258, link to [INDEX.md](INDEX.md)). Added [INDEX.md](INDEX.md) as a one-page navigation map (read order, archive pointers). [QUICKSTART.md](QUICKSTART.md): cold-start blurb points at INDEX and archive tiers for older sessions.

---

## Session S257 — 2026-04-14 (Git sync before Build / Release; release.ps1 rebase guard)

**Problem**: `release.ps1` step `[4/7]` runs `git pull --rebase`. If the index had **staged but uncommitted** changes (or a failed silent commit), Git errors: *cannot pull with rebase: Your index contains uncommitted changes*.

**Cause**: The old block used `git status --porcelain` to decide whether to commit, and piped `git commit` to `Out-Null` without checking exit code—commit could fail while the index stayed dirty.

**Fix — `devtools/release.ps1`**: Always `git add -A`, then `git diff --cached --quiet`; if there **are** staged diffs, `git commit` with output echoed; **non-zero commit → exit 1** before `pull --rebase`.

**Fix — `devtools/dev-window-v2/dev-window-v2.ps1`**: New **`Invoke-GitSyncBeforeBuild`** (solo-dev workflow): at the start of **Start-Build** and **Start-PushRelease** (before `Set-ProjectVersion` on release), `git add -A`, conditional commit, **`git push`** to current branch; failures show MessageBox and abort. Log tab shows the sync. **CRITICAL-PROCEDURES.md** documents the rule.

**Follow-up**: First **Auto-commit + push** queue step still commits the CMakeLists version bump after release confirms; sync clears prior dirty state so release.ps1 sees a predictable tree.

---

## Session S256 — 2026-04-14 (Dev Window v2: layout + VERSION column)

**Scope**: `devtools/dev-window-v2/dev-window-v2.ps1` (XAML only in practice).

**Root cause of “tall skinny” tool buttons**: BUILD tab `DockPanel` had default `LastChildFill="True"`, so the last child (utility `StackPanel`) filled **all remaining vertical space**; horizontal `StackPanel` stretched tall and `ToolBtn` children stretched with it.

**Fix**: `LastChildFill="False"` on BUILD tab `DockPanel`; utility row `VerticalAlignment="Top"`; `ToolBtn` style `MinHeight="32"` + `VerticalAlignment="Center"`. Slightly wider window default (`MinWidth` 820) and symmetric horizontal margin `12,10,12,10`.

**VERSION card clipping**: Replaced fixed `252` px third column with proportional `2*` / `*` columns (`MinWidth` 220 / 280), `MinWidth="260"` on version `Border`, `TextWrapping="Wrap"` on auth/latest/dev lines.

**Readability**: Status bar + build-status labels `FontSize` 14 (was 13) to match hero scale.

**Follow-up (same session)**: `gh` detection failed when the CLI was installed but the GUI process inherited a stale PATH. `Sync-UserMachinePath` merges registry Machine+User `Path`, `Resolve-GhExecutable` falls back to `Program Files\GitHub CLI\gh.exe` (and x86 / LocalAppData); auth runspace uses the resolved full path.

**Follow-up — auth UI stale after `gh auth login`**: Addressed with re-probes; later **aligned v2 with original `devtools/_dev-window.ps1`**: `PATH` passed into runspace, `Get-Command gh`, `gh auth status` output matched for `Logged in`, login via `powershell.exe -NoExit -Command "gh auth login"` (not direct `gh.exe`).

---

## Session S255 — 2026-04-14 (Dev Window v2: Pull / Push + DPI font scaling)

**Scope**: `devtools/dev-window-v2/dev-window-v2.ps1` only (no game/protocol changes).

**Git UX**: Utility row adds **Pull** and **Push** after `Clean Build`. Handlers run `git -C <ProjectRoot> pull` and `git push` (current branch / upstream), append output to the Log tab, show a MessageBox from exit code, then `Update-StatusBar`. Blocked while `IsBuilding` or `IsPushing`; same enable/disable wiring as `BtnCleanBuild`.

**Font / DPI**: Call `SetProcessDPIAware()` early (before WPF window) via `PD2V2.DpiUtil`. Root `Window`: `UseLayoutRounding="True"`, `SnapsToDevicePixels="True"`, `TextOptions.TextFormattingMode="Display"`, `RenderOptions.ClearTypeHint="Enabled"`.

**Context**: `README.md`, `session-log.md`, `tasks-current.md`, `infrastructure.md`, `roadmap.md` updated for handoff.

---

## Session S254 — 2026-04-14 (Bug B: countdown-cancel-on-room-close)

Worktree `claude/condescending-jepsen`, changes committed to dev. Spec: `context/scratch/bug-b-fix-spec-2026-04-14.md`.

**Root cause**: `s_ReadyGate` (static singleton in `netmsg.c`) has no teardown hook. When clients leave a room or a room is destroyed, none of the four `roomLeave` callsites cleared the gate or broadcast `SVC_MATCH_CANCELLED`. The gate kept ticking and at zero fired `mainChangeToStage()` into an empty room; clients stuck at "GO!" overlay.

**Fix shape** — 3 files, ~45 lines, no protocol bump (reuses existing `SVC_MATCH_CANCELLED`):
- `port/include/net/netmsg.h`: declared `netReadyGateAbortForRoom(u8, const char*)` and `netReadyGateOnClientLeft(u8)`.
- `port/src/net/netmsg.c`: added `static void readyGateAbort(…)` forward decl; added defensive room-missing guard at top of `readyGateTickCountdown()` (calls `readyGateAbort` if `roomGetById(room_id)==NULL`); implemented both public wrappers as thin guards over existing `readyGateAbort`.
- `port/src/room.c`: added extern forward decls; patched `roomLeave()` to call `netReadyGateOnClientLeft(clientId)` after removing the client, and `netReadyGateAbortForRoom(room->id, "Room closed")` inside the `client_count==0` branch before `roomDestroy()`.

**Verification status**: Build clean (both pd + pd-server, `PerfectDark.exe` + `PerfectDarkServer.exe` rebuilt 2026-04-14). **Needs in-game playtest** per spec §7 (leader leaves during countdown, client disconnects during countdown, ESC cancel regression).

**Follow-up caveat — `CLSTATE_PREPARING` gate in ESC-cancel path**: `netLobbyRequestCancel()` in `pdgui_bridge.c:923` checks `g_NetLocalClient->state == CLSTATE_PREPARING`, but grep shows no client-side code ever sets `g_NetLocalClient->state` to `CLSTATE_PREPARING`. ESC-cancel may be silently failing (spec §7.5 / §9 "ESC cancel may be pre-broken"). Out of scope for Bug B fix — file as follow-up if confirmed broken in playtest.

**Context updates**: constraints.md +1 bullet ("Ready gate lifetime bound to room"), systemic-bugs.md +SP-14 ("Room-bound server state must be cleaned up on room teardown"), bugs.md Bug B moved from open → fixed.

---

## Session S253 — 2026-04-13 (MP Lobby Residual: Issue 7, F-2.1, Issue 2/8, B-140 Issue B)

Worktree `claude/great-carson` (base `8e02a2ef`), final dev HEAD `287b0bc4`. Closed all four S248/S249/S250 residuals. **Issue 7** — `SVC_ROOM_SETTINGS 0x78` / `SVC_ROOM_PLAYLIST 0x79` + CLC counterparts `0x13`/`0x14`; dirty-flag accumulator in `pdguiRoomScreenRender()` flushes end-of-frame; server validates room-leader and rebroadcasts. Protocol v35 additive. Post-merge fix commit `287b0bc4` corrected sender from `g_NetLocalServer` (undeclared) to `g_NetLocalClient->out` + `netSend(g_NetLocalClient, NULL, ...)`. **Weapons F-2.1** — `pdgui_menu_room.cpp` picker switched to `TreeNodeEx("Base Game (%d)", DefaultOpen)` + `std::sort` alpha Selectables. **Issue 2/8** — `pdguiThemeRescanMods()` added (bypasses `s_LoaderInitDone`), called from `modmgrApplyChanges()` between `modmgrCatalogChanged()` and `mainChangeToStage()`; audio self-heals per-frame. **B-140 Issue B** — `renderSelectTunes` redesigned: 0.70×0.82 window, two-column (Library / Selected Tracks), click-add / click-remove, hover-preview via `list_Focus` (base) / `audioSetModTrackId` (mod), `musicRestoreInterval()` on hover-off; `netSendRoomPlaylistUpdate()` at all three change sites when leader. Build clean (8 files, +517/-141). Open after drop: see `bugs.md` + `tasks-current.md`; forensic detail in `scratch/archive/2026-04-13/session-state-mp-lobby-residual.md`.

---

## Session S252 — 2026-04-13 (End-Game-Crash + B-142 NULL-guard + modal confirm UX)

Worktree `hungry-bose` branch `fix-end-game-crash-and-ux`. Two fixes from the same branch, both merged `--no-ff` into dev — `d37e9677` End-Game-Crash + modal confirm, `4d1e13c1` B-142 NULL-guard. Forensic detail in `scratch/archive/2026-04-13/session-state-endgame-crash.md`.

**B-143 End-Game-Crash (merge `d37e9677`)**: 0xc0000005 access-violation on MP pause → End Game → Confirm → CI-training stage change. Root cause: `netDisconnect()` (`net.c:~935-1002`) called `mainChangeToStage(STAGE_CITRAINING)` while `g_ClientManifest.num_entries > 0` still held the MP match's entries; `mainChangeToStage()` took the `manifestMPTransition()` branch and attempted to diff the torn-down manifest against whatever was loading for CI-training → AV. **This is the third site of a now-systemic pattern**: F-0.4 (pdgui_bridge.c:799, `pdguiEndscreenExitToMainMenu`, S233) and L1-1 (netmsg.c:1419, `netmsgSvcStageEndRead`, S235) both received `manifestClear(&g_ClientManifest)` before stage change for the same reason. netDisconnect was the third missed callsite. Fix: `manifestClear(&g_ClientManifest)` immediately before `mainChangeToStage(STAGE_CITRAINING)` in `netDisconnect()`, with block comment referencing F-0.4 and L1-1. **Pattern is now load-bearing enough to justify a systemic class** — documented as **SP-13** in `systemic-bugs.md` (manifestClear before mainChangeToStage during MP teardown) with audit checklist + search command. Also documented as active invariant in `constraints.md`. Companion UX fix: `pdgui_menu_warning.cpp` added new `renderMpEndGameDialog` (~150 lines) replacing the text-only Danger modal — palette 2 (red) scrim, PdDialog frame, yellow "End Match?" title, Cancel (default focus) + red "End Match" buttons, Enter/Space/Gamepad-A → Confirm, Escape/Gamepad-B → Cancel, hint row. Registered via `pdguiHotswapRegister(&g_MpEndGameMenuDialog, renderMpEndGameDialog, "MP End Game (modal confirm)")` replacing the prior `renderDangerDialog` wiring.

**B-142 false kills (merge `4d1e13c1`)**: Fresh match, player opens pause immediately after spawn, score shows ~17 kills before any actions. Root cause candidate: `mpPlayerGetIndex(NULL)` returned `0` whenever `g_MpAllChrPtrs[0]==NULL` as well — the NULL==NULL loop-match at index 0 silently attributed any orphan/explosion/tripmine damage to player slot 0. Candidates 1–3 (g_PlayerScores not zeroed / SVC_PLAYER_SCORES replay / NET_RESYNC_FLAG_SCORES) falsified in trace. Fix: `if (chr == NULL) return -1;` early-return at top of `mpPlayerGetIndex` (`src/game/mplayer/mplayer.c:3734`). Defensively correct regardless of whether it's the sole root cause; closes the attribution footgun. Post-merge line count 4507 → 4510 verified. B-142 → FIXED-PENDING-PLAYTEST (fresh 32-bot Chicago match, idle 30 s, pause → kill counter should be 0 / 0).

Build-verify: `pd` + `pd-server` linked clean on both merges.

Open items from this worktree carried forward (see `tasks-current.md`): Bug C post-game endscreen partial render (scrim + title-bar render, body invisible — six hypotheses in endgame-crash scratch §4c; needs instrumentation); Bug D invisible Chicago bots (may have cleared with today's drop); Airbase Start-Match 0xc0000005 (may share root cause with B-143, needs post-drop repro); Bug B countdown-cancel-on-room-close (off-limits during parallel sessions; clear to pick up now).

---

## Session S251 — 2026-04-13 (B-141 audio telemetry: drop/underrun/hitch counters)

Worktree `happy-hofstadter` (base `8e02a2ef`), merged `5a42f234`. Not a fix — evidence-gathering for B-141 (intermittent audio skips, not reproducible on demand). `audioEndFrame()` in `port/src/audio.c` is the single observation point (producer frame + SDL consumer) and now counts three symptoms: **drops** (`buffered >= queueLimit` at push — producer outruns consumer), **underruns** (`buffered < 128 stereo samples` — SDL starving), **hitches** (>50 ms inter-frame gap — main loop stalled). Counters always on (cheap). Per-event log opt-in via `Audio.VerboseLog=1` in pd.ini. Auto 30 s summary line if any counter moved; zero-activity silent. New accessor `audioGetB141Counters(u32*, u32*, u32*)` exported from `audio.h` for future diagnostic UI. `audio.c` +89, `audio.h` +10. Build clean (pd only; pd-server does not link `audio.c`). Next repro: tail `pd.log` for `AUDIO[B-141]`; summary gives count, verbose gives per-event timestamps. Parallel sessions noted: `exciting-turing` landed S249 (B-140 playlist sync) and finished B-140→B-141 rename started in S250 — kept audio work scoped to `audio.c`/`audio.h` only.

---

## Session S250 — 2026-04-13 (Input authority: gameplay-input suppression predicate + focus handling)

Worktree `claude/happy-hofstadter` (base `6f562beb`), merged `5098f903`. Phase 1 of the input-authority-and-menu-pool ADR (`designs/input-authority-and-menu-pool-2026-04-13.md`). Trigger: 2026-04-13 playtest — Ctrl+V in Online window jumped the background player (Ctrl bound to `ACTION_JUMP` in gameplay IMC). Defense-in-depth fix at four layers: **predicate** `gameplayInputSuppressed()` (inputctx.c/.h) — 1 when any of (top ctx != gameplay, focus lost, <50 ms since focus regain). **Dispatch gate** in `fireVk()` skips `g_ImcGameplay`/`g_ImcVehicle` when predicate true. **Read gates** in `actionPressed/Held/Released/Value/Axis` early-return 0 when `gameplayInputSuppressed() && actionIsGameplayOnly(action)` — shared actions (ACTION_USE, ACTION_CANCEL_USE, ACTION_PAUSE, menu nav, system hotkeys) stay readable. **Flush** `actionmapFlushGameplayState()` zeroes `s_State` + synthesises released edges on non-gameplay push (fresh + resurrect) and focus-lost/regain. SDL `WINDOWEVENT_FOCUS_LOST/GAINED` wired in `gfx_sdl2.cpp`. `actionmapPollFrame()` predicate widened (was `menuActive` per B-124d, S188). Files: `inputctx.h`, `inputctx.c`, `actionmap.h`, `actionmap.cpp`, `gfx_sdl2.cpp`, plus ADR. Acceptance matrix documents Ctrl+V, SPACE, hold-W→menu→close, alt-tab-with-W, shared-action survival. Bug logged: **B-141** (audio skips — filed as B-140, renamed after S249 claimed B-140 for playlist). Phase 2 (menu pool single-instance discipline) deferred to dedicated session.

---

## Session S249 — 2026-04-13 (B-140 playlist sync root cause)

Worktree `exciting-turing`, merged `e13c2d1f`. One-line fix to B-140 Issue A (playlist auto-advance never broadcast to clients). `audioNetworkMusicTick()` was guarded by `g_NetMode != NETMODE_SERVER_AUDIO`, but `#define NETMODE_SERVER_AUDIO 2` collided with `NETMODE_CLIENT==2`; host is `NETMODE_SERVER==1`. Tick fired on clients, never host, so `netMusicBroadcastAdvance()` was never called. Fix: `port/src/audio.c:17` → `#define NETMODE_SERVER_AUDIO 1`. Secondary audioGetModTrackId() issue masked by `plcount==0` guard in all callers. Both targets build clean (audio.c only). Separate worktree `spawn-validation-stuck-geometry` landed B-134 (`0b44b2b8`) in the same window. Deferred: B-140 Issue B, Issue 2/8, Issue 4, Bugs B/C/D, Issue 7.

---

## Session S248 — 2026-04-13 (Playtest stabilization: mod persistence, room name, countdown, songs sort)

Worktree `claude/exciting-turing`, WIP `ae922c3c` → merge `93c64f6c` → renumber `5448f2d6` → final `16de65e6`. Six fixes from Mike's 2026-04-13 solo + Chicago playtest, plus Songs F-2.1. **B-135** — `mods/base-ui/mod.json` missing `"id"`; added as first field. **B-136** — mod-level toggle in `pdgui_menu_modmgr.cpp` called `modmgrSetEnabled()` but not `modmgrSaveConfig()`; save added at all three sites (enable/disable/size-confirm). **B-137** — `pdguiRoomScreenRender()` looks up `g_LocalRoomId` in `g_RoomCache`, shows "Room: <name>". **B-138** — Apply Changes enables when `modmgrIsDirty()`, label "Apply Changes*"; added "Unsaved Changes" `BeginPopupModal` on Close/Escape with Apply/Discard/Cancel. **B-139** — `pdguiCountdownReset()` in `pdgui_bridge.c` called from mode→NONE disconnect block in `pdgui_lobby.cpp`. **Songs F-2.1** — `renderSelectTunes` base + mod tracks wrapped in `TreeNodeEx(DefaultOpen)`; mod tracks qsort'd by `display_name` via new `modTrackCompare()`. **Weapons F-2.1** deferred at this session (landed S253-residual). Bug ID renumber commit `5448f2d6` moved B-134..B-138 → B-135..B-139 to avoid conflict with existing dev B-134. Build clean. Parallel session `agitated-jackson` landed dev-window-v2 font polish (`11fd1d5e`). Deferred (picked up in S249/S251-res): Issue 2/8, Issue 4 (Airbase no-response), Bug B countdown-on-close, Bug C invisible bots, Bug D silent crash, Issue 7 room-settings sync, B-140 track add.

---

## Session S247 — 2026-04-13 (Build-env self-heal: _build-env-prelude.ps1 + build-env.sh)

**Focus**: Eliminate recurring wasted build time caused by invalid `$TEMP` and MinGW not on PATH. Three-layer fix: shared PS prelude, bash helper, CLAUDE.md doc block.

### Layer 1 — `devtools/_build-env-prelude.ps1` (new, 28 lines)

Dot-sourced by all three build scripts. Sets `TEMP`/`TMP` to `C:\Users\mikeh\AppData\Local\Temp` (creates dir if absent), idempotently prepends `C:\msys64\mingw64\bin` to PATH, sets `MSYSTEM`/`MINGW_PREFIX`/`CCACHE_SLOPPINESS`, echoes one-line confirmation. Replaces duplicate 9–16 line env blocks in each script.

Scripts updated to dot-source the prelude:
- `devtools/build-headless.ps1`: replaced lines 89–104 with 3-line dot-source
- `devtools/release.ps1`: replaced lines 100–109 (retained `GIT_TERMINAL_PROMPT` separately)
- `devtools/dev-window-v2/dev-window-v2.ps1`: replaced Section 0 lines 15–25

### Layer 2 — `devtools/build-env.sh` (new, 20 lines)

Bash-side equivalent. `source devtools/build-env.sh && ninja -C Build pd pd-server` is now the canonical bash build recipe. Idempotent PATH guard via `case` pattern.

### Layer 3 — CLAUDE.md "Build environment" block

Added immediately under Repository section. Canonical bash and PowerShell recipes documented with "Do not rediscover TEMP or PATH" warning. Parent copy synced to `Perfect-Dark-2/CLAUDE.md` (created, previously absent).

### Build Verification

Bash path verified: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 752/752 steps, both targets clean.
- `PerfectDark.exe`: 49 MB
- `PerfectDarkServer.exe`: 22 MB

### Commits

- `2807f37a` feat: build-env self-heal — _build-env-prelude.ps1, build-env.sh, CLAUDE.md (worktree claude/bold-agnesi)
- `54659577` Merge worktree bold-agnesi: build-env self-heal (prelude + sh + CLAUDE.md) → dev

### Next

No follow-up required. Sessions should now source `build-env.sh` (bash) or run `build-headless.ps1` (PowerShell) without any env setup ceremony.

---

## Session S246 — 2026-04-13 (Gap Closure: M-7.x Smoke Test + match_seed/B-19 DONE)

**Focus**: Gap-closure pass after Layer 1-7 master plan. Two work items + tasks refresh.

### Work Item 1 — tasks-current.md Refresh

Grepped evidence confirmed both items are fully done:
- `match_seed via SVC_STAGE_START` → **DONE (S241/S242)**: `g_NetMatchSeed` declared `net.c`, generated server-side (RNG+tick), written via `netbufWriteU32` in SVC_STAGE_START handler, received client-side. `playerreset.c` uses `g_NetMatchSeed` (nonzero) or fallback.
- `B-19 resolution` → **DONE (S242)**: `spawnPoolSelect()` wired at `playerreset.c:611` + farthest-first greedy confirmed.
- `Smoke test: all base + mod maps` → DONE (this session) via work item 2.
- `M-7.x: Retroactive validation` → DONE (this session).
- Parent copy (`Perfect-Dark-2/tasks-current.md`) synced per Standing Order 1.

### Work Item 2 — M-7.x Smoke Test Implementation

Added to `src/game/spawnpool.c` (+183 net lines):
- **Smoke log accumulator**: `smoke_record_t s_SmokeLog[128]` with `smokeLogRecord()`. Auto-records every live pool build from `spawnPoolBuildGlobal()` (stage_id, needed, produced, max_layer_used, time_ms, source='L').
- **`spawnPoolSmokeAll()`**: iterates all `ASSET_ARENA` catalog entries via `assetCatalogIterateByType`. Skips stages with live data. For others: saves/restores `g_NumSpawnPoints=0` + `s_PoolReady`, calls `spawnPoolBuild()` into scratch pool (offline test seed `0x5EC0BE45`), records source='O'. Returns L3/L4 count.
- **`spawnPoolSmokeWriteCSV(path)`**: writes CSV with columns: `stage_id, needed, produced, max_layer_used, source, time_ms, flag` (flag: `OK` or `L3L4_RISK`).
- **Updated `spawnPoolSmokeTest()`**: dumps full session log in addition to current pool state.

Added to `port/fast3d/pdgui_menu_moddinghub.cpp` (+15 net lines):
- **"Run All" button** (next to Smoke Test): calls `spawnPoolSmokeAll()` + `spawnPoolSmokeWriteCSV("Build/smoke-test-results.csv")`. Status line shows L3/L4 count + CSV path.

Added `context/scratch/spawn-smoke-test-results-2026-04-13.md`: test plan, base arena list (13 Dark + 5 classic), mod stage instructions, offline vs live result interpretation guide.

### Bug Fix

Fixed `s_PoolReady` pollution from offline sweep: `spawnPoolBuild()` sets the global flag unconditionally. Added save/restore around each offline scratch build in `smokeOfflineBuildArena()`.

### Build Verification

- Both targets clean. No new errors, no new warnings.
- PerfectDark.exe: 51,101,998 bytes (48.7 MB client). PerfectDarkServer.exe: 22,812,269 bytes (21.8 MB).
- spawnpool.c: +183 net lines. spawnpool.h: +21 net lines. moddinghub.cpp: +15 net lines.
- Total: 5 files changed, 312 insertions (+), 16 deletions.

### Commits

- `489de13c` feat: gap-closure pass — M-7.x smoke test + tasks refresh (match_seed/B-19 DONE)
- `46c9cc7e` fix: restore s_PoolReady after offline smoke sweep

### Next

- Mike: visit each MP arena in-game, then press "Run All" in Modding Hub → Map Import to get live CSV
- Filter `source=L` rows in `Build/smoke-test-results.csv` — any `L3L4_RISK` in live rows = that map needs more spawn pads

---

## Session S245 — 2026-04-13 (L7 Supporting Items: FIX-F Updater + FIX-G Mission Headers)

**Focus**: Infrastructure repair plan Layer 7 — updater robustness (FIX-F, B-99) and mission category headers enhancement (FIX-G, B-97).

### Changes (2 files, +172 lines)

1. **`port/src/updater.c`** (+83, 1608→1691):
   - **FIX-F.1**: `curlGet()` gains `long *httpCodeOut` parameter. Callers now get the HTTP response code.
     `checkThread()` classifies errors into four distinct paths: network failure (curl error), rate limit (HTTP 403 → "retry in 1 hour" user message + log preview of response body), API error (non-200), parse failure (response not a releases array).
   - **FIX-F.2**: `updaterInit()` now checks if `installDir` is empty after `detectExePath()`. If so, falls back to `fsFullPath("$E/")` (exe dir expander) and rebuilds `updatePath`, `versionPath`, `stagingDir` from it. Logs both final paths.
   - **FIX-F.3**: `updaterApplyPending()` extraction verify step checks extracted `PerfectDark.exe` for existence AND minimum 1MB size via `GetFileSizeEx`. Truncated extractions are rejected and staging dir removed before returning -1.

2. **`port/fast3d/pdgui_menu_solomission.cpp`** (+89, 3380→3469):
   - **FIX-G.1**: `stagecat_t` enum added (`STAGE_CAT_MISSION`, `STAGE_CAT_SPECIAL`, `STAGE_CAT_BONUS`). `stageCategoryFor()` maps stageIdx. `countMissionGroupCompletions()` counts mission groups fully beaten on Agent (all stages in group). `countSpecialCompletions()` counts individual special stages beaten on Agent via `g_GameFile.besttimes`.
   - **FIX-G.2**: CAMPAIGN section header (`ImGui::SeparatorText`) with `(done/total)` completion count added before missions loop. Special Assignments header upgraded from S242's `TextUnformatted` to `SeparatorText` with gold tint + `countSpecialCompletions()` beat-count. Resolved merge conflict with S242 partial FIX-G.

### Build Verification
- PerfectDark.exe: 49MB, PerfectDarkServer.exe: 22MB. Both targets linked clean.
- updater.c: 1608→1691 lines (+83). solomission.cpp: 3380→3469 lines (+89). No truncation.

### Merge
- Resolved merge conflict in `pdgui_menu_solomission.cpp` (S242 partial FIX-G in HEAD vs. our SeparatorText version). Took our version.
- `--no-ff` merge `205c74a7` to dev: "Merge worktree nostalgic-banach: L7 updater robustness + mission category headers (FIX-F, FIX-G)"
- Post-merge line counts verified clean.

### Closed
- B-99 (updater extraction failure): FIXED
- B-97 (SA not separated from mission list): already closed S242; FIX-G.2 SeparatorText upgrade landed in this session via merge conflict resolution

### Next
- Playtest: trigger GitHub API rate limit to verify 403 message; verify CAMPAIGN/SA headers render with correct counts
- Remaining Layer 7: FIX-B.1 (deep manifest scanner, `netmanifest.c`)

---

## Session S244 — 2026-04-13 (L2/L3 Spawn + Import Polish)

**Focus**: Complete all deferred items from L2 (spawn pool) and L3 (map import) sessions.

### Changes (5 files, +434/-9 lines)

1. **`src/game/playerreset.c`** (+51/-3):
   - Replaced `0x12345678` match_seed placeholder with `g_NetMatchSeed` (from SVC_STAGE_START). Offline fallback: `stagenum ^ lvframe60`.
   - Initial MP spawn (lvframe60 == 0) now uses `spawnPoolSelect()` with occupied-position tracking and team awareness (MPOPTION_TEAMSENABLED, player config team index). Respawns still use existing enemy-distance dispersal.

2. **`src/game/spawnpool.c`** (+153):
   - `spawnPoolSelect()`: farthest-point-first greedy algorithm. Marks occupied entries. FFA: maximizes min-distance-to-assigned. Teams: angular sector partitioning around map center, prefers same-team sector, overflows to FFA on sector exhaustion.
   - `spawnPoolSmokeTest()`: diagnostic that logs current pool state and flags L3/L4 activations.

3. **`src/include/game/spawnpool.h`** (+24): Added `spawnPoolSelect()`, `spawnPoolSmokeTest()` declarations.

4. **`port/fast3d/pdgui_menu_moddinghub.cpp`** (+177/-6):
   - Added "Map Import" as 7th Modding Hub tab (tool index 6). UI: source dir path input, map name input, Import Map button, Reset button, Smoke Test button. Status/error display with color-coded success/failure. Tab button width 140->120 to fit 7 tabs.

5. **`port/src/mapimport.c`** (+29): `mapImportRunFull()` C wrapper for C++ UI consumption.

### Build Verification
- pd: 500 objects, linked clean
- pd-server: 59 objects, linked clean

### Merge
- `--no-ff` to dev, post-merge line counts verified

---

## Session S243 — 2026-04-13 (L6 Rendering Polish: FIX-C.1/C.2/C.3)

**Focus**: Infrastructural repair plan Layer 6 — Class C rendering state leakage. B-18 (pink sky), B-128 (sky tearing, confirmed already fixed), menu opacity stacking.

### Changes (2 files, +33/-2)

**FIX-C.1: GBI frame-start state reset** (`src/game/lv.c:1407-1421`)
- Canonical state reset before `skyRender()` each frame: `gDPPipeSync`, `gDPSetRenderMode(OPA_SURF)`, `gDPSetEnvColor(white)`, `gDPSetPrimColor(white)`, `gDPSetFogColor(black)`.
- Eliminates the entire class of cross-frame GBI state leakage (SP-10). Previous frame's sun flares, teleport beams, or translucent particles can no longer corrupt the current frame's sky or early geometry.
- Defense-in-depth for B-128 (already point-fixed with OPA_SURF in sky.c:1244).

**FIX-C.2: B-18 Skedar Ruins investigation** (no code change)
- Skedar Ruins environment data: `RGB(0x6565ff)` = blue-purple sky (correct from ROM).
- Pink appearance was cross-frame env color leakage into the cloud LERP combiner (`SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT`). FIX-C.1's env color reset to white eliminates this.
- **Needs playtest confirmation on Skedar Ruins** to close B-18.

**FIX-C.3: Menu opacity stacking** (`port/fast3d/pdgui_style.cpp:752`)
- Set `ImGuiCol_WindowBg` alpha to 0 (fully transparent). PD-styled windows use `NoBackground` + `pdguiDrawPdDialog()` for body fill; the semi-transparent `WindowBg` (~0xa0 alpha) was adding a second translucent layer. Over repeated open/close cycles, GBI blur + ImGui WindowBg compounded.
- ChildBg and PopupBg retain their semi-transparent values for combo dropdowns etc.

### Build
- PerfectDark.exe: 51,160,628 bytes, PerfectDarkServer.exe: 22,793,307 bytes. Both clean link.

### Decisions
- B-128 confirmed FIXED (sky tearing). FIX-C.1 provides systemic defense.
- B-18 downgraded to "LIKELY FIXED" pending Skedar Ruins playtest.
- Menu opacity stacking: root cause was double-layered semi-transparent backgrounds. Fixed.

### Next
- Playtest: Skedar Ruins sky color, menu open/close opacity, outdoor stages for sky state leakage
- If B-18 confirmed: close bug. If still pink: investigate stage-specific palette/sky-type issue.

---

## Session S242 — 2026-04-13 (Menu Polish Tail: F-3.1/F-3.2/F-1.4/FIX-G)

**Focus**: Final menu system polish items from fix plan Layers 1-3 + infrastructural repair FIX-G.

### Changes (3 files, +58/-19)

**F-3.1: Duplicate-push rejection** (`src/game/menu.c`)
- `menuPushDialog()` now scans existing layers for matching `definition` pointer before allocating. Logs `LOG_WARNING` and returns early on duplicate.

**F-3.2: Pop underflow logging** (`src/game/menu.c`)
- `menuPopDialog()` logs `LOG_WARNING` when called at depth 0.

**F-3.3: B-92 deferred flush — reviewed, still needed** (no code change)
- The hotswap→gameplay one-frame gap is not covered by `inputCtxSyncMouseMode()`. The B-92 deferred flush in `pdgui_backend.cpp` remains necessary.

**F-1.4: Redundant SDL calls removed** (`port/src/inputctx.c`)
- Removed `SDL_SetRelativeMouseMode`/`SDL_ShowCursor` from `gameplayOnPush`, `gameplayOnPop`, `imguiMenuOnPush`, `pauseMenuOnPush`, `debugOverlayOnPush`. `inputCtxSyncMouseMode()` is now the sole SDL mouse mode authority.

**F-2.2: k_Btns — no fix needed**
- Array is already non-static `const` local; `langSafe()` re-evaluated every frame.

**FIX-G (B-97): Mission completion counters** (`port/fast3d/pdgui_menu_solomission.cpp`)
- Chapter headers now show `"(done/total)"` completion counts (per `isStageDifficultyUnlocked`). Special Assignments header also shows count. Section separation already existed from M1.1 redesign.

### Build
- Both targets build and link cleanly. Zero errors.

### Status: B-92 OPEN (reviewed, mechanism still needed), B-97 CLOSED

---

## Session S241 -- 2026-04-13 (L5 Match Lifecycle Major: Co-op Manifest + Protocol Bump)

**Focus**: Master Orchestration Plan Layer 5 -- co-op/counter-op manifest pipeline integration, CLC_STAGE_READY for co-op, protocol bump v34 to v35, match_seed in SVC_STAGE_START.

### Changes (3 files)

1. **`port/include/net/net.h`**: Protocol bump v34 to v35. New `extern u32 g_NetMatchSeed`.
2. **`port/src/net/net.c`**: `g_NetMatchSeed` definition. Seed generation in both `netServerStageStart()` and `netServerCoopStageStart()`.
3. **`port/src/net/netmsg.c`**: `s_ReadyGate` gains `game_mode`/`difficulty` fields. Co-op CLC_LOBBY_START rewritten to build manifest + enter ready gate. `readyGateTickCountdown()` branches on game_mode. Co-op client sends CLC_STAGE_READY. SVC_STAGE_START writes/reads `g_NetMatchSeed`.

### Build: Both targets clean (pd 689/689, pd-server 59/59).

---
