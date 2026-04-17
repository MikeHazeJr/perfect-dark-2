# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. Completed work lives in `session-log.md`,
> `bugs.md`, `daily-logs/`, or `_archive/tasks-archive.md`.
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Open — 2026-04-16 (S302 — quirky-mendeleev)

### Playtest verification of spawn pool tiered selection

Reference: `src/game/spawnpool.c`, `src/game/playerreset.c`, `src/game/player.c`. Build: `PerfectDark.exe` 51,533,440 / `PerfectDarkServer.exe` 22,824,057 bytes.

- **Tier distribution (healthy signal)** — Stock 4-player FFA on Felicity / Warehouse / Temple:
  - `pd.log` should show `SPAWN.TIER: T1_OPTIMAL initial MP spawn ...` for the 4 human spawns and for every respawn. No T2/T3/T4 on well-resourced stock maps.
  - Grep: `grep 'SPAWN.TIER:' pd.log | awk '{print $2}' | sort | uniq -c` — expect near-100% T1_OPTIMAL.
- **Burst reservation (32-bot Chicago)** — start a max-bot Combat Sim on Chicago:
  - First tick should produce 1 local human + 32 bot placements in rapid succession. Every placement should get a distinct pool slot (no two `pool[N]` entries with the same N in the first 33 SPAWN.TIER lines).
  - If the pool was built with >33 slots, expect all T1. If fewer, expect T2_CYCLED once the reservation bitset saturates mid-burst, then resume T1 on the cleared slots.
  - Grep: `grep 'SPAWN.TIER:.*T2_CYCLED' pd.log` — count should be <= pool->count - slots (i.e. T2 only fires when burst exceeds pool capacity).
- **Over-subscribed tiny arena** — pick a mod map with a very small pool (ring test smoke log says L4 layer, small count). Start a 16-bot match. Expect periodic `SPAWN.TIER: T3_REUSED — pool oversubscribed` lines during respawn waves. Players may briefly telefrag each other — that's the designed behaviour (no void spawns, no crashes).
- **Solo map in Combat Sim (zero declared pads)** — load G5 Building / Chicago SP stage as a MP arena. Expect pool build log to show `max_layer=2` or higher (`L2 waypoints` / `L3 grid` / `L4 radial`). Spawn decisions should still log `SPAWN.TIER: T1_OPTIMAL` until the pool is oversubscribed; T2/T3 only when placements exceed pool capacity.
- **Last-resort synthesised position** — engineered repro: load a map with zero intro spawns, zero waypoints, zero pads (e.g. a broken mod map). Pool build will fall all the way to L4 radial; if L4 also fails, `spawnPoolLastResort` should log `SPAWN.TIER: T4_LAST_RESORT — synthesised pos=...` and the player should spawn near the stage AABB centre (not at (0,0,0)). No crash.
- **`spawn_needed` in netplay** — join a dedicated server match with 6 other human clients + 10 bots. On each client's `pd.log`, confirm pool build line reports `needed=%d` with %d = 18 (or higher with span bonus), not 11 (which would be PLAYERCOUNT()=1 + 10 bots).
- **No regressions on S298 reservation bitset** — same-tick burst on Chicago should still produce unique pool indices; reservation auto-clear on `g_Vars.lvframenum` change still works (T2 explicitly clears it too).

### Follow-up if tier distribution looks wrong

- **Heavy T3/T4 on stock maps**: pool count came out too small. Check `pd.log` for `SPAWNPOOL: build complete -- %d points` and compare against `needed`. If produced << needed, the validator is too strict for that map — consider relaxing `SPAWNPOOL_BUDGET_THRESHOLD` or the capsule-radius reject for that specific geometry.
- **T2_CYCLED fires every tick**: reservation bitset isn't auto-clearing — check `spawnPoolTickCheck` is seeing `g_Vars.lvframenum` advance.
- **T4 synthesised at (0,0,0)**: `spawnPoolComputeAABB` returned `valid=false`. Check whether `g_Rooms` / `g_Vars.roomcount` are populated before playerReset runs on this stage.

---

## Open — 2026-04-16 (S299 — trusting-banach)

### Playtest verification of Input Authority Phase 2 (menu pool)

Reference: `context/designs/input-authority-and-menu-pool-2026-04-13.md` §6, commit on `claude/trusting-banach-a43c0e`.

- **Structural dedup at `menuPushDialog`** — Try to force a duplicate push: from the main menu, open any dialog (e.g. Cheats) and attempt to invoke the same menu again via a second binding (keyboard + controller nearly simultaneous). Confirm `pd.log` shows either `MENU: menuPushDialog rejected duplicate def %p` (F-3.1 pointer scan) OR `MENU: menuPushDialog rejected — pool slot [...] already active` (pool layer). No double-open should be possible.
- **Nextsibling respect** — Open the main menu (CI free-roam). Pool should log `MENUPOOL: acquired main_menu ...` and `MENUPOOL: acquired ci_options ...` (the auto-opened sibling). No B-153-style double-render. Close with Esc.
- **Force-close cleanup** — Start a solo mission → End Game → Exit to Main Menu. Confirm `pd.log` shows `MENUPOOL: released N slot(s) (bulk)` alongside the existing `INPUTCTX: 'imgui_menu' marked for deferred removal`.
- **Stage-transition reset** — Transition from main menu → mission load → gameplay. Pool should be cleanly empty during and after `inputCtxShutdown` / `inputCtxInit`. Tail `pd.log` across the transition for any residual `MENUPOOL:` lines after the stage has loaded.
- **Regression sweep** — Repeat the S296/S297 playtest checklists (stuck WASD, double main menu, textbox leak, chrome mod visibility). The pool is identity-only this session and should not affect those existing fixes.

### Follow-up queued for future sessions

- Migrate the 10 ImGui renderer `s_*PushedCtx` bools to pool-owned input-context (see ADR §6.1b). Per-file migration: `pdgui_menu_{cheats,mpsetup,mppause,mpadvanced,playerconfig,botsetup,agentselect,room,training}.cpp`. Replace `s_FooPushedCtx`/manual push/pop with `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` on IsWindowAppearing + `menupoolReleaseDialog(def)` on Begin-false cull + `menuPopDialog()` on close.
- Per-scope state arrays for the shared-action leak (ADR §6.2 follow-up). Still open.
- Unit/integration tests for menu pool (ADR §6.3).

---

## Open — 2026-04-16 (S298 — stoic-proskuriakova)

### Playtest verification of the S298 follow-up batch

Commit on `dev` (fast-forwarded from `claude/stoic-proskuriakova-2440de`). Build: `PerfectDark.exe` 51,363,805 / `PerfectDarkServer.exe` 22,838,411 bytes.

- **Content inset in endscreens** — load a chrome style with large nineslice corners (e.g. 24+ px `border_scale: 2.0` on the Nine-Slice template). Play a solo mission to end; play a Combat Sim match to end. Expect: DEBRIEF / OBJECTIVES columns, rankings table, awards, and the action button row all sit inside the inner frame — no text or button bleeds into the chrome border. Pause menu tabs + Resume button same check.
- **Theme Editor docked footer** — open Main Menu → Theme Editor. On 720p and 1080p verify the Save-as-Mod inputs + Reset/Close buttons are always visible even as the color list is scrolled, and scrolling only affects the color pickers.
- **Room Start Match docked footer** — host a room on a narrow window (resize game window to ~900 px wide), fill the bot list, toggle tabs (Combat Simulator / Campaign / Counter-Op / Level Editor). Expect Start Match + Leave Room button row to always be pinned at the bottom of the dialog, never clipped or pushed offscreen.
- **Deep Sea end-path OOB** — replay Deep Sea co-op → mission complete → expect the next-mission transition to land cleanly on the Deep Sea follow-up (no AV / no -1 index crash even when the campaign list has been modified by mods).
- **SP-1 guard — `endscreenSetCoopCompleted`** — complete any co-op mission; sanity-check no crash from the `1 << stageindex` shift on a mod stage whose `stageindex >= 32`. No visible UI change, just no crash.
- **Manifest scanner FIX-B.1** — play through a mission with cinematic spawns (Deep Sea intro, Crash Site intro, any stage with SPAWNCHRATPAD-driven cutscene chrs). Tail `pd.log` for `manifest-diff:` lines; expect the set of `load` entries to include bodies/heads/models referenced by intro/ailist scans, not just the static props. Best signal: no more "CHR 0xXX missing from manifest" runtime warnings that previously showed up during Deep Sea act 2 cinematic.
- **Spawn pool residuals**:
  - **Reservation** — 32-bot Chicago, inspect log for `SPAWN: initial MP spawn via pool[N]` lines. Every N should be unique (no duplicates across the 32+ entries from the same match-start tick).
  - **Wall-probe orientation** — same match, watch the initial facing of bots on a map with lots of pocket spawns (G5 elevators, Skedar Ruins recessed spawns). Bots should face out of pockets, not into the corner.
  - **Neighbour-room ground check** — on any stage with portal seams (Felicity balconies, Temple bridges) verify no bots spawn mid-air or fall through portal boundaries on first-spawn.

### Follow-up if any of the seven recur

- If content inset clips persist, dump `pdguiThemeGetContentInset` values at render time and compare against `pdguiNinesliceGet(s_CfgUiChromeStyleId)->dst_*` corner values.
- If manifest scanner misses an asset, log the ailist index + cmd[0]..cmd[7] hex bytes for the suspect command — the scanner's dispatch may need an extra AICMD case.
- If reservation bitset exhausts the pool during normal play, drop to logging `s_SpawnReserved[]` snapshots around each select call; expected pattern is "reset each tick" — if it survives longer, `g_Vars.lvframenum` is not advancing as expected.

---

## Open — 2026-04-16 (S297 — silly-jepsen)

### Playtest verification of B-154 / B-155 / B-156

Commit on `claude/silly-jepsen-cc481a`. Build: pd 51,424,970 / pd-server 22,816,554 bytes.

- **B-154 (textbox input leak)** — Open each of these and type alphabetic keys, confirming the background player takes no action:
  - Modding Hub → Nine-Slice Chrome → "Mod Name" InputText: type `Weapon` / `Esteemed` / `Tactical` — no ACTION_USE / ACTION_LOOK / etc.
  - Modding Hub → Nine-Slice Chrome → "Image" path InputText.
  - Skin Editor → name field.
  - MP connect-code entry field.
  - MP chat InputText (if active).
  - Verify Esc still closes each menu and Return/Enter still submits (these are the only keys that bypass the new gate).
- **B-155 (chrome mod visibility in Mods list)** — Nine-Slice Chrome → load image → Save as Mod. Open Modding Hub → Mods tab. The new entry (`user.<slug>.ui-chrome`) should appear immediately, enabled (checked). Quit + relaunch; confirm the mod is still in the list and still enabled (config persisted via `modmgrSaveConfig`). Also verify pending (unapplied) enable/disable toggles on OTHER mods are preserved across the save-triggered rescan (pick a mod, flip enabled, save a chrome mod, confirm the flipped mod's state survived).
- **B-156 (chrome mod visibility in Video dropdown)** — Same save flow. Open Settings → Video → UI Chrome Style. The new chrome style name should appear between "Procedural" and any other discovered style. Select it; chrome updates live. Restart the game; confirm the style persists (`Video.UiChromeStyleId`) and is still in the dropdown.

### Follow-up if any of the three recur

- If B-154 recurs: capture `io.WantCaptureKeyboard` + `inputCtxGetTopName()` state around the leak (add temporary `sysLogPrintf` in the new gate block). If WantCaptureKeyboard reads 0 during an active InputText, upgrade the gate to also consult `io.WantTextInput`.
- If B-155 recurs: verify `modmgrRescanDirectory()` actually finds the new dir — add `sysLogPrintf` listing each candidate `modsdir` and the path the scan decided to walk. Path-mismatch between `$E/../mods` vs `./mods` is the likely suspect.
- If B-156 recurs: grep `pd.log` for `UI.CHROME: registered style` — if the expected id isn't logged, re-trace `s_parseChromeManifest` (dump `has_chrome_tag / out_tex_id / out_tex_file / has_nineslice` just before the final `return`). The S297 float-tokenizer fix doesn't cover every possible JSON parser weakness; `\u` escapes are also unhandled, for instance.
---

## Open — 2026-04-16 (S297 — elegant-mahavira)

### Playtest verification of the S297 UI polish drop

Commit on `claude/elegant-mahavira-198cc0`.

- **Docked Chrome tool footer** — Open Modding Hub → Nine-Slice Chrome, load any image ≥ 1024 × 1024, resize the game window smaller (e.g. 720 p).  The left sidebar should show both **Source Preview (with rulers)** and **Frame Preview (assembled nine-slice)** stacked vertically without scrolling; the right column scrolls through all sliders/toggles/presets; **Save as Mod** / **Reset** remain visible at the bottom at all window sizes.  Verify the two previews still live-update when sliders change.
- **Room member list (teams on)** — Start a room with `Teams` option enabled and at least two human players across two teams plus a few bots.  Verify:
  - Members are grouped by team, humans render before bots within each team.
  - Each team band shows a `-- Team N --` header in that team's color (Red/Blue/Green/Yellow/…).
  - Row background behind each name is tinted to the team color (18 % alpha for others, 35 % for the local player).
  - Local player's row also gets a 2 px white left-edge accent bar.
- **Room member list (teams off)** — Same flow without `Teams` enabled.  Verify no team separators are emitted, local-player row still has a subtle cyan background + accent bar, and bots still render after humans.
- **Title-bar styles** — Open Settings → Video, scroll to `UI Chrome Style`, change `Title Bar Style` through all five values.  Expect:
  - Classic (default) — original 3-color PD gradient.
  - Solid — flat `dialog_border1` band.
  - Vertical Bars — lighter `titlebg` base with darker 1-px stripes every 8 px.
  - Scanlines — classic gradient with 1-px horizontal scanlines.
  - Diagonal Stripes — border1 base with 45° `titlebg` bars every 10 px.
  Verify persistence: restart client, style should be restored from `pd.ini`.
- **Content-inset API (smoke)** — Toggle chrome on, then off.  No visible difference in existing menus (the API is additive; no caller wired yet).  Expect `PDGUI theme: D5.0 early init (...)` log line unchanged.

### Follow-up tasks queued from S297 — CLOSED S298

- ~~Migrate Theme Editor / Room `Start Match` action rows to the Chrome tool's child → child → footer pattern for overflow resilience.~~ DONE S298 — Theme Editor uses explicit `footerH` reservation with pinned Save/Reset/Close row; Room `pdguiRoomScreenRender` pins Start Match/Leave Room footer at `dialogH - footerH`.
- ~~Wire `pdguiThemeGetContentInset` into custom-drawn menus (endscreen, scorecard, HUD overlays) so they never clip into nineslice borders.~~ DONE S298 — wired into `renderSoloEndscreen`, `renderMpEndscreen`, and `renderPauseMenu` via `resolveEndscreenPadding`. Scorecard + HUD don't use `pdguiDrawPdDialog` so no change needed.

---

## Open — 2026-04-16 (S296 — vigilant-robinson)

### Playtest verification of B-152 (stuck WASD) and B-153 (double main menu)

Commit on `claude/vigilant-robinson-ba0ee9`. Reproduction source: `019d97ef-pdclient.log`.

- **B-152 (stuck WASD)** — keyboard-only recommended. Open main menu from CI free-roam, close with Esc, press+release W individually. Character should stop on release. Repeat with A, S, D. Do the same with a controller plugged in to verify the controller path is unaffected (axis should continue to reset each frame from the stick poll). Tail `pd.log` for `BMOVE:` lines — `AXIS_MOVE=0.000,0.000` should be visible after each release, not stuck at 1.0.
- **B-153 (double main menu)** — open main menu → press Esc immediately to close → press Esc again within < 1s to reopen. Single menu copy should render with normal backdrop. Repeat 5+ times to confirm no spurious double-render. Test controller B-button path as well (Mike flagged "also with controller I think").

### Follow-up if B-152 or B-153 recur

- If stuck-axis returns: capture `pd.log` with the BMOVE lines straddling the close → stuck window; check whether `.value` is being written from an unexpected path. Consider adding verbose diagnostic to `actionmapPollFrame`'s synthesis block under `sysLogGetVerbose()`.
- If double-menu returns: hotswap queue is the next place to instrument. Dump `s_Queue` contents (name + dialogdef pointer) each frame when it has > 1 entry. Likely candidate: a new renderer or mod attaching to a dialogdef that's in a nextsibling chain.

---

## Open — 2026-04-16 (S295)

### Playtest verification of the S295 collision + spawning drop

- **B-145 slope jump**: on any Skedar ramp / Carrington stairs / outdoor slope, spam jump while walking up — every press should fire `JUMP: APPLIED` in the log, not `JUMP: BLOCKED`.
- **B-146 pickup walkthrough**: drop a rifle / sniper / launcher in an MP arena, walk into the model — capsule should push the prop or pass through, never step up onto it. Multi-ammocrate piles should be identically non-standable.
- **B-147 ceiling clip**: jump against the edge of a slanted ceiling (Skedar temple, G5 / CI corridor bends). Head should stop at the ceiling, not poke through on the diagonal.
- **B-148 Carrington tables**: break-room tables — player should stand on the top face, not fall through the middle.
- **B-149 spawn pool**: multi-bot match in a small arena with scattered pickups. Over 5 rounds, confirm no mid-air spawns, no "stuck in wall" spawns, no spawns standing on a dropped weapon / crate. Cross-check pool dump in `pd.log` for L4 last-resort entries.

### Scheduled architectural follow-ups (deferred, not regressed by S295)

- **Issue 1-B** (mesh ceiling wiring): `classifyTriFlags` emits a real `GEOFLAG_CEILING`, `meshFindCeiling` filters on normal.y; wire into `bondwalk.c` pre-move clamp and into `capsuleSweep` for upward motion.
- **Issue 2-B** (per-prop mesh extraction): extend `meshWorldAddRoomGeo`-style top-face extraction to per-prop colmesh so desks/crates/tables get correct top faces generally.
- ~~**Issue 5 residual**~~: DONE S298 — `s_SpawnReserved[]` same-tick bitset in `spawnPoolSelect` (auto-cleared on `g_Vars.lvframenum` change + pool rebuild), `spawn_point_t.angle_rad` wall-probe stored at build time and used from `playerreset.c`, `spawnPoolValidateCandidate` passes `bgFindRoomsByPos`-collected neighbour rooms into `cdFindGroundInfoAtCyl`.

---

### Playtest verification of the 2026-04-16 menu dead-input desync fixes (B-150)

Reference: `context/scratch/menu-system-investigation-2026-04-16.md` §7, commit on `claude/relaxed-ride`. (Originally tracked as B-145 in the investigation; renumbered to B-150 after B-145..B-149 were claimed by the S295 collision drop.)

- **F1 — g_PdguiActive removed** — toggle F12 overlay on/off several times in CI free-roam; overlay should appear/disappear and player input toggle correctly each time; verify no "F12 does nothing" regression after rapid cycles.
- **F2 — dead `pdguiGameOverRender` body removed** — no behavioral change expected; just confirm no crash on MP end screen (hotswap endscreen path remains the sole renderer).
- **F3 — `inputCtxEndFrame` watchdog** — tail `pd.log` across a full session (title → solo mission → MP match → end screen → main menu → quit). Expect **zero** `INPUTCTX watchdog:` lines. Any occurrence identifies a remaining leak; capture the stack-dump context.
- **F4 — Begin()=false leak guards (9 renderers)** — stress navigating sub-dialogs quickly (Bot Setup → edit sim → back → back; MP Settings → Soundtrack → Select Tunes → back → back). On any transient window cull we should not see the player frozen without a menu visible.
- **F5 — MpEndscreen one-shot push** — MP match → pause → End Game → verify first-frame clickability of endscreen (original Bug C repro). Then Return-to-Lobby → ensure player control resumes in the lobby.
- **F6 — force-close contract** — no runtime change; review by human.
- **F7 — `s_MainMenuPushedCtx` removed** — open/close main menu from CI multiple times; then from CI exit via Quit → ensure the close path still pops the context (watch for any "menu gone but player frozen" state).

---

## Open — 2026-04-16 (S295 match-pipeline track)

### Playtest verification of S295 match-pipeline fixes (festive-saha worktree)

- **B-151 (GAP-1)** — Online co-op advance past Deep Sea. Expect clean stage transition, no `c0000005`. (Originally tracked as B-145 in the match-pipeline investigation; renumbered to B-151.)
- **C-1 (Challenges)** — Open Combat Challenges with a controller only. D-pad should move the selection from first frame; no "extra key to wake up" gap.
- **Bug C (MP endscreen)** — Next end-of-match repro: tail `pd.log` for `ENDSCREEN:` lines. If either appears, that narrows the six hypotheses (`Begin=false` = window-level issue; `contentH clamped` = layout-arithmetic issue).
- **GAP-3** — Online pause → End Game (Confirm?): expect endscreen to render with rankings/awards before Disconnect. Previously client jumped straight to main menu.
- **C-3/C-4/C-5/C-6/C-8 focus sweep** — Open Team Control, Control Diagram, Cheats → warning + unlock confirm, Player Handicaps, Modding Hub each with controller only: D-pad nav should work from first frame.
- **GAP-4** — After an online match ends, verify the client is not stuck with stale `g_NetMatchRoomId`. Leaving the room and rejoining should have clean state.
- **GAP-10** — When SVC_MATCH_CANCELLED fires, countdown overlay must clear (already covered by memset; this is defense-in-depth via `pdguiCountdownReset`).

---

## Open — 2026-04-16 (S293)

### Playtest verification of the 2026-04-16 Nine-Slice + mods folder drop

- **S293 Nine-Slice Chrome redesign** — verify:
  - Save-as-Mod writes to `mods/UI Chrome/<slug>/` and immediately activates in Settings → Video → UI Chrome Style.
  - Border Scale slider (0.25x–4.0x) changes on-screen corner thickness without rebaking pixels.
  - Proportional Insets toggle: on → sliders show `%`, resolved pixel insets print under the sliders and track Scale X/Y; off → sliders revert to absolute pixels.
  - Import a large image (e.g., 8K screenshot) — expect a clear "Image too large" status line, no crash.
  - Scale X/Y to 400% on a 2K image — expect preview to cap at 4096px, status shows OOM-style hint only if the cap is still too large.
  - Trim sliders: dragging Trim Left past `ImgW - TrimRight - 1` is blocked; no 1-pixel degenerate crop.
  - Mod name with a literal double quote (`foo"bar`) → saved `mod.json` parses cleanly on next scan (no registry drop).
- **S293 mods/ category subfolder scanning** — verify:
  - Existing flat mods (`mods/base-ui/`, `mods/pd-modern-ui/`, `mods/bot-names/`) still load normally.
  - A chrome mod saved to `mods/UI Chrome/<slug>/` appears in Mod Manager and in Settings → Video → UI Chrome Style.
  - Creating an empty `mods/Weapons/` (or similar) does not break the scanner.

### Audit follow-ups deferred from S293

Non-nine-slice items from `scratch/audit-s255-s292-2026-04-16.md`:
- **C-6** `matchConfigAddBot` hardcoded `1` human count (`matchsetup.c:438`, `netmsg.c:6125` SVC_ROOM_SETTINGS rebuild).
- **S-2** Middle-click back bridge vs Skin Editor canvas pan (`pdgui_backend.cpp:337-343` vs `pdgui_skin_editor.cpp:454`).
- **S-3** Shared `s_PdmsOwnsMenuCtx` across Handicap/SelectTunes/Soundtrack/TeamNames (input ctx leak).
- **S-4** `IsMouseHoveringRect(..., false)` on custom close button not clipped to focused window.
- **S-5** Skin Editor downrez preview buffer never realloced on character change.
- **S-6** Chrome style rescan leaks GL textures (`s_chromeStylesClear` zeros count only).
- **S-11** (mods-apply) `modmgrApplyChanges` missing `pdguiThemeRescanChromeStyles()` call.

---

## Open — 2026-04-13+

> Carried forward from the 2026-04-13 stabilization drop. Forensic detail in
> `scratch/archive/2026-04-13/`.

### Playtest verification of the 2026-04-13 drop

Mike to confirm each on next build. Bug/feature → commit on `dev`:

- **Issue 7** room-settings sync (`287b0bc4`) — non-leader sees leader's bot/player count / arena / timelimit real-time.
- **B-140 Issue B** two-panel Select Tunes (`287b0bc4`) — add/remove mod tracks, hover preview, leader's playlist syncs to room.
- **Issue 2/8** theme rescan (`287b0bc4`) — newly-enabled mod themes appear in Settings → Video without restart.
- **Mod Apply follow-up (S260/S284, `2c1a52bc`)** — verify Apply now rebuilds enabled mod manifests/audio in-place (no forced title restart), themes + mod songs populate immediately, and MP dialog close no longer steals main-menu input context.
- **Mod Apply updater-style popup parity (S285, `2c1a52bc`)** — verify Apply now uses centered updater-style progress window (`Applying Changes`) during catalog rebuild/diff/apply and completion acknowledge flow (`OK` / `OK & Close`).
- **Mod Apply success-state visual parity (S286, `2c1a52bc`)** — verify Apply completion state now uses updater-like green-tinted success window while preserving neutral in-progress styling.
- **Release/build outage hardening (S287, `d136fa4d`)** — verify release/dev-window commit sync now supports forced commit fallback (`--no-verify`) when hooks fail, and build scripts create missing `Build/` automatically before configure/build.
- **Nine-Slice Chrome in-client creator (S288, `1b530d8d`)** — verify Modding Hub now exposes `Nine-Slice Chrome` tab with image import, ruler sliders (`L/R/T/B`) with symmetry toggles, live ruler overlay preview, Save-as-Mod output (`mods/<slug>/mod.json` + `ui_chrome_frame.tga`), and immediate style activation in `Settings -> Video -> UI Chrome Style`.
- **Nine-Slice frame preview + desaturation option (S289, `1b530d8d`)** — verify Nine-Slice Chrome tab now includes assembled frame preview pane (not just source rulers) and optional desaturation slider for tint-friendly saved chrome textures.
- **Nine-Slice transform pipeline + docked actions (S290, `1b530d8d`)** — verify Nine-Slice Chrome supports trim edges, X/Y scaling, center-strip removal (`axis + %`), saves transformed output image, docks `Save as Mod`/`Reset` actions to tool footer, and Back input matches Modding Hub Close-button behavior.
- **Skin Editor character selector input-steal fix (S266, `ed1e9340`)** — verify Modding Hub -> Skin Editor character list selection is stable (mouse + controller). `PageUp/PageDown` (LB/RB tab-cycle mapping) should no longer yank the tool away while selecting characters; list should still render with short content heights.
- **Skin Editor base-capture source fix (S268, `ed1e9340`)** — verify New Skin capture seeds layer-0 from captured source body texture dimensions (not 256x256 preview-FBO screenshot content). Check UV overlay alignment and exported base skin quality on at least one base body and one mod body.
- **ImGui nav parity closure (S267, `ed1e9340`)** — verify Room and Solo Options tab cycling follow action-driven `PageUp/PageDown` mapping (controller + keyboard parity), and Agent Select list no longer traps focus (full traversal via controller and MKB).
- **Mod Apply menu-lock regression fix (S274/S284, `2c1a52bc`)** — verify Modding Hub -> Apply no longer lands in CI with captured mouse + no accessible menus; apply should remain in the current UI flow with the new in-place modal.
- **Song mods missing in Select Tunes follow-up (S291, `35a8daaf`)** — verify both paths now surface music tracks in Mod Tracks and keep add/remove working: (1) component audio INIs with textual `category` values (`music`/`sfx`/`voice`) and (2) Mod Apply in-place rebuild no longer drops enabled `audio.ini` mods from the catalog due stale `mod->loaded` flags.
- **Universal menu mouse-back bridge (S276, `2c1a52bc`)** — verify middle-click backs out of ImGui menus consistently (Main Menu submenus, Solo Mission stack, Room, Pause, Training, typed warning dialogs) without breaking right-click list interactions.
- **Global title-bar close button (S279, `2c1a52bc`)** — verify PD title-bar `X` appears across ImGui dialogs and closes one menu layer per click without affecting existing right-click item actions.
- **Select Tunes Mod Tracks click-toggle fix (S277, `2c1a52bc`)** — verify clicking a mod track in the left list toggles membership (adds to right playlist when absent, removes when already present), and leader sync still updates room peers.
- **Modding Hub diagnostics + skin capture candidate logging (S278, `2c1a52bc`)** — reproduce INI Editor missing entries and Skin Editor black/partial preview; inspect new `modhub.ini:*`, `skin_editor:*`, `pdgui_charpreview:*`, and `skin_capture.gfx:*` logs to confirm entry counts, selection/load flow, and selected capture source texture list.
- **Modding Hub popup close-state fix (S280, `2c1a52bc`)** — verify closing Skin Editor popup windows (`X`/`Cancel`) no longer closes Modding Hub via outside-click/escape side effects, and reopening Modding Hub no longer shows stale popup overlays.
- **Skin Editor preview black-panel guard (S281, `2c1a52bc`)** — verify Skin Editor preview pane no longer draws a pitch-black texture when charpreview is not ready; should show rendering status until ready texture is available.
- **UI Chrome style immediate persistence (S282, `2c1a52bc`)** — verify Settings -> Video -> UI Chrome Style now persists immediately when changed (Procedural/Classic survives restart without extra save actions).
- **UI Chrome runtime registration hook + picker persistence (S283, `2c1a52bc`)** — verify chrome style runtime APIs now support importer/save flows (`pdguiThemeRegisterChromeModDir`, `pdguiThemeRescanChromeStyles`), Mod Pack import triggers chrome style rescan immediately, and Settings -> Video persists/restores exact style via `Video.UiChromeStyleId`.
- **S263 systemic pipeline fixes** — verify: Deep Sea coop transition clamps stage index; Counter-Op selected anti player is honored online (not forced to slot 1); co-op/anti launch has no double-transition side effects; team-mode endscreen rankings show player rows (no placeholder '?' entries).
- **B-142** false kills (`4d1e13c1`) — fresh 32-bot Chicago match, idle 30 s, pause → kill counter 0/0.
- **S292 room max-bot/team-mode hardening** (`35a8daaf`) — verify Chicago with max bots from Room UI:
  - Room bot cap now respects runtime bot limit (no `MATCH_MAX_SLOTS` spillover paths).
  - Team-enabled + newly added bots no longer default to all one team (balanced default assignment).
  - Scoreboard should no longer show all `T1` by default in team mode; bots should engage and take damage.
- **B-143 End-Game-Crash** + modal confirm (`d37e9677`) — End Game → Confirm → no AV, CI training loads.
- **B-141 telemetry** (`5a42f234`) — on next audio-skip repro, tail `pd.log` for `AUDIO[B-141]`.
- **B-134 spawn validator** (`0b44b2b8`) — Chicago fire-escape area, no railing-interior spawn.
- **S250 input authority Phase 1** (`5098f903`) — Ctrl+V in Online window = no background jump; hold W → menu → close = no residual walk.
- **Dev-window-v2 polish** — S248 font/control baseline (`11fd1d5e`); S255 adds Pull/Push + DPI/text layout (verify legibility on your display scaling).
- **Bug B** countdown-cancel-on-room-close (`731831ec`, 2026-04-14) — fixed, awaiting playtest verification. Leader leaves room during countdown or client disconnects mid-countdown → 3-2-1 overlay clears, "cancelled" banner shows, no stuck UI. See session-log S254 + spec §7.
- **S264 audit remediation batch** — verify end-to-end:
  - Back/Esc during countdown now cancels from both host and client paths and shows canceller name on all clients.
  - Nested ImGui dialog close paths no longer pop parent-owned menu context (no gameplay input bleed-through while submenu is open).
  - MP scenario/radar SP-6/SP-8 guards hold under sparse player slots and NULL `prop->chr` transitions.
  - Manifest hardening: post-setup SP rescan always diff/applies; unresolved non-base IDs are treated as missing in manifest check.
- **S265 post-merge sanity playtest** — focused confirmation:
  - Counter-Op anti-role selection follows chosen player (`antiClientId`) across host/client.
  - Ready-gate cancel transitions restore lobby state on all peers after `SVC_MATCH_CANCELLED`.
  - Team endscreen rankings show player rows with team grouping/sort (no `?` placeholders).

### Still open (post-drop)

| Item | File / notes |
|------|--------------|
| **Bug C — post-game endscreen partial render** | Scrim + title-bar render; body content invisible. Six hypotheses in `scratch/archive/2026-04-13/session-state-endgame-crash.md` §4c. Needs `sysLogPrintf` instrumentation on each `renderMpEndscreen` early-return + fresh playtest log. |
| **Bug D — invisible networked bots on Chicago** | Chr generation token mismatch likely (FIX-A.2 area). May have cleared with S253; needs post-drop repro. |
| **Chicago silent crash ~9 s** | Needs VEH log + symbolify. May have cleared with today's drop. |
| **Airbase 0xc0000005 / Start-Match no-response** | Log shows manifest OK, no SVC_STAGE_START. May share root cause with Bug A (fix shipped). Needs post-drop repro. |
| **Input authority Phase 2** | Menu pool single-instance discipline. ADR: `designs/input-authority-and-menu-pool-2026-04-13.md` §6. Scope: `src/game/menu.c`, `pdgui_backend.cpp`, possibly new `port/src/menupool.c`. Dedicated session. |
| **B-141 audio skips — root cause** | Blocked on repro against telemetry. |
| **FIX-B.1 deep manifest scanner** | `netmanifest.c`, `setup.c`. Cinematics + AI scripts spawn assets not in the setup list. FIX-B.2 dependency DONE. |
| **Manifest gap follow-ups** | See `scratch/archive/2026-04-13/manifest-gap-report-2026-04-13.md`. (1) Title/menu stage has no manifest — Skin Editor mod chars silently missing; fix: `manifestBuildForMenu()`. (2) SP pre-scan timing — split `manifestBuildMission()` into pre/post-load phases (partially mitigated by `manifestSPRescanSetup`). (3) Cutscene cinema models never in manifest — safety net only. |

### Audit follow-ups (S262 — open, not yet implemented)

- **Room-scope teardown hygiene (SP-14 follow-up)** — `port/src/net/net.c`: review/reset lifecycle for `g_NetMatchRoomId` at stage end.

### Audit follow-ups (S262 — closed in S298)

- ~~**Campaign end-path OOB guard**~~ — DONE S298 (lower-bound check added to menutick.c alongside S264 upper-bound clamp).
- ~~**Endscreen menu index safety (SP-1)**~~ — DONE (`endscreenPushCoop/Anti` guards already in place; S298 extends the same guard pattern into `endscreenSetCoopCompleted`).
- ~~**Team rankings data/UI mismatch**~~ — DONE S295 (`buildRankings` now uses `mpGetPlayerRankings` + team sort; verified in S298).

---

## Build / Release

| Item | Status | Detail |
|------|--------|--------|
| **Git sync before Build/Release (dev-window-v2)** | DONE (S257) | `Invoke-GitSyncBeforeBuild` + `release.ps1` index-safe commit before `pull --rebase`. See `session-log` S257, `CRITICAL-PROCEDURES.md`. |
| **Git index.lock path fidelity (dev-window-v2)** | DONE (S269/S270/S272/S273) | Lock cleanup handles path-format mismatch while treating dev-root `.git\\index.lock` as canonical (S272). S273 adds same-runtime MSYS cleanup: resolve `rm`/`cygpath` from the active `git.exe` root and remove the derived MSYS lock path (`/home/.../.git/index.lock`) plus Windows/WSL paths. |
| **Cursor commit method (PowerShell-safe)** | DONE (S271) | Added always-apply rule `.cursor/rules/powershell-git-commit-message.mdc` so multiline commit messages use PowerShell here-strings (`$msg = @'...'@; git commit -m $msg`) instead of bash heredoc syntax. |
| **Dev-window python command robustness** | DONE (S261) | Dev-window-v2 + headless configure now pass `-DPD_PYTHON_EXECUTABLE=C:/msys64/usr/bin/python3.exe` explicitly so asset generator custom commands never depend on `python3` being on child PATH. |
| **Static link / DLL elimination** | DONE (S224) — **Mike: verify with objdump** | CMakeLists.txt: SDL2 deps completed (dinput8/dxguid/shell32/user32/uuid), DLL copy block removed. Carve-out: `opengl32.dll` only. Verify: `objdump -p Build/PerfectDark.exe \| grep "DLL Name"` should show only system DLLs. Design: `designs/static-link-dll-elimination-2026-04-13.md`. |
| **L0-LINK: pdguiThemeRegisterModDir server link** | DONE (S231) | Stub confirmed at `port/src/server_stubs.c:417`. Both-targets link verify pending Mike's build. |
| **L0-BUILD: ccache warm-build regression** | CODE DONE (S231) — **Mike: run warm-build timing verify** | `CCACHE_SLOPPINESS=pch_defines,time_macros` in all 3 build scripts. Target: warm `pd` <12 s (was 30.7 s after PCH in `955dffa2`). Run `.\devtools\build-headless.ps1 -Target client` twice; second run should be <12 s. |

---

## Input Authority & Menu Pool (ADR 2026-04-13)

ADR: `context/designs/input-authority-and-menu-pool-2026-04-13.md`

Phase 1 — gameplay-input authority predicate — ✅ DONE (S250, merge `5098f903`).

- `gameplayInputSuppressed()` single truth-source (context-stack top, window focus, 50 ms focus-regain settle).
- Dispatch-site gate in `fireVk()` + read-site gates in `actionPressed/Held/Released/Value/Axis` for gameplay-only actions.
- `actionmapFlushGameplayState()` on `inputCtxPush` (fresh + resurrect) + focus-lost/regain — held keys synthesise clean released edge.
- SDL `WINDOWEVENT_FOCUS_LOST/GAINED` wired in `gfx_sdl2.cpp`.

Phase 2 — menu pool single-instance discipline — QUEUED (own session). Pre-allocated slots keyed by type; open=populate+activate, close=deactivate+clear; duplicate-push structurally impossible. Also resolves shared-action leak (background bondmove reading `ACTION_USE` while menu owns A). Scope: `src/game/menu.c`, `port/fast3d/pdgui_backend.cpp`, possibly new `port/src/menupool.c`. ADR §6.

---

## B-141 Audio Telemetry (S251 — investigation)

B-141 = audio skips / pauses intermittent (2026-04-13 playtest, not reproducible on demand). Telemetry shipped `5a42f234`; waiting for repro to narrow mechanism.

- `audioEndFrame()` counts drops / underruns / hitches (>50 ms inter-frame); always on, low overhead.
- `Audio.VerboseLog=1` in pd.ini → per-event `AUDIO[B-141]` lines.
- Auto 30 s summary if any counter moved; zero-activity windows silent.
- Accessor: `audioGetB141Counters()` for diagnostic UI.
- **Root cause + fix blocked on repro**. Expected mechanism differs by dominant counter: hitch-dominated = main-loop stall (profile RSP or render), underrun-dominated = scheduler preemption / buffer too small, drop-dominated = producer runs ahead during slow frames.

---

## ✅ Shipped 2026-04-13 — MP Lobby / Mod Stabilization Drop

S248 → S253 batch all on `dev`. Session-by-session detail in `session-log.md`;
bug-level detail in `bugs.md`; forensic handoffs in
`scratch/archive/2026-04-13/`.

| Session | Commit | Scope |
|---------|--------|-------|
| S248 | `16de65e6` | B-135 / B-136 / B-137 / B-138 / B-139 + Songs F-2.1 |
| Dev-window polish | `11fd1d5e` | font/control size (parallel `agitated-jackson`) |
| S249 | `e13c2d1f` | B-140 Issue A playlist auto-advance |
| S249 | `0b44b2b8` | B-134 spawn validator railing trap |
| S250 | `5098f903` | Input authority Phase 1 |
| S251 | `5a42f234` | B-141 audio telemetry |
| S252 | `d37e9677`, `4d1e13c1` | B-143 End-Game-Crash + modal confirm; B-142 false kills |
| S253 | `82d0c1f3` → `287b0bc4` | Issue 7, Weapons F-2.1, Issue 2/8, B-140 Issue B |

---

## Backlog (Post v0.1.0)

| Item | Target | Detail |
|------|--------|--------|
| **D5 Phase 5 -- Lobby scene** | v0.1.0+ | Player portraits, connected player avatars, character preview |
| **B-12 Phase 3 -- Remove chrslots** | v0.2.0 | Protocol bump, participant system replaces bitmask entirely |
| **D14a -- Counter-Op mode** | v0.6.0 | NPC possession mechanic |
| **D15 -- Map Editor / Forge** | v0.5.0 | Level editor, character creator, skin system |
| **D16 -- Master Server** | v0.4.0 | Server registry, heartbeat, browser |
| **D6 -- Persistent Stats** | v1.0.0 | JSON/SQLite stats, post-game scorecard, lifetime viewer |
| **D7 -- Discord Rich Presence** | v1.0.0 | Activity API, join button |
| **D10 -- Spectator Mode** | v0.6.0 | Free-cam, follow-cam, HUD overlay |

---

## Phase-2 Feature Lines (SHIPPED)

### Audio Mod Menu — A-1 → A-7 (COMPLETE 2026-04-12)

Feature complete. Full batch line shipped 2026-04-12: catalog extension (A-1) → mod music stream (A-2) → Audio Mod Menu UI (A-3) → Soundtrack Menu extension (A-4) → pack creation (A-5) → multi-format import (A-6, MP3/OGG/WAV) → network sync (A-7). Seamless network sync added later same day. Protocol v32 → v33 → v34. Detail: `daily-logs/2026-04-12.md`.

### Skin Editor — S-1 → S-9 (COMPLETE 2026-04-12)

Feature complete. Full batch line shipped 2026-04-12: canvas + 2D editor (S-1/S-3) → live 3D preview (S-2) → save-as-mod (S-4) → image import (S-5) → PD-style downrez / quantize (S-6) → blend modes (S-7) → UV wireframe (S-8) → network sync via existing ASSET_SKIN infrastructure (S-9). Detail: `daily-logs/2026-04-12.md`.

### Mod Map Import Pipeline (L3) — COMPLETE 2026-04-13

M-5.x (`mapimport.h`/`mapimport.c` — 6-stage pipeline: PARSE / NORMALIZE / GENERATE / EMIT / VALIDATE / REGISTER) + M-6.x (Modding Hub tab + dialog + Smoke Test + `mapImportRunFull()` wrapper) + M-7.x (retroactive validation via `spawnPoolSmokeAll()` + CSV output). Detail: session-log S240 / S244 / S246.

### Spawn System (L2 Architecture) — COMPLETE 2026-04-13

L1-L4 fallback chain (`spawnpool.c/h`): raycast-budget validator + L1 declared + L2 waypoint + L3 grid + L4 radial. Deterministic from `hash(stage_id) ^ match_seed`. `g_SpawnPoints` expanded 24→40. `match_seed` in `SVC_STAGE_START` (v35). Farthest-first greedy selection in `spawnPoolSelect()`. B-134 capsule-radius threshold (SPAWNPOOL_CAPSULE_RADIUS=30 — see `constraints.md`). Detail: session-log S239 / S241 / S242 / S249.

---

## Master Orchestration Plan — Status 2026-04-13

Plan: `context/designs/master-orchestration-plan-2026-04-13.md`

L0–L7 complete. Full layer-by-layer status (L0-BUILD / L0-LINK / F-0.1/2/3/4 /
FIX-A / L1-1 / FIX-B.2 / L1-2/3/4/5 / F-1.1/2/3/4 / F-2.1/2 / F-3.1/2/3 /
FIX-G / FIX-F) — see `session-log.md` S231 – S253.

Open supporting items:

| ID | Title | Status |
|----|-------|--------|
| **FIX-B.1** | Deep manifest scanner (cinematics + AI scripts) | DONE S298 — `netmanifest.c` now scans `g_StageSetup.intro` + `g_StageSetup.ailists`. |

---

## Design Guidelines (Planned)

| System | Status |
|--------|--------|
| Menu/UI | DONE (in `designs/d5-full-menu-overhaul.md`) |
| Input | IMPLEMENTED (M0.2 action maps). Guidelines doc PLANNED. |
| Networking | PLANNED |
| Mod System | PLANNED |
| Audio | PLANNED |
| Visual/Theme | PLANNED |
| Collision/Physics | PLANNED |
| Level Editor (Forge) | PLANNED |
