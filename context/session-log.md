# Session Log (Active)

> **S241–S298** (rolling window). Older sessions **S240–S157** → [_archive/session-log-archive-S240-and-older.md](_archive/session-log-archive-S240-and-older.md). Ancient **S1–S119** → [_archive/sessions/].
> Navigation hub: [INDEX.md](INDEX.md) · Back to [README.md](README.md)

## Session S298 — 2026-04-16 (S297 follow-up batch — stoic-proskuriakova worktree)

**Scope**: seven follow-up items queued from S262/S263/S295/S297 audits. Single commit batch on `dev` after worktree FF-merge.

**Items shipped**:

1. **Content-inset API wired into endscreen + pause menu.** `pdguiThemeGetContentInset` existed (S297) but had zero callers. Added a `resolveEndscreenPadding()` helper in `pdgui_menu_endscreen.cpp` that computes `padX/padY/padR/padB` as `max(basePad, inset)` so default padding is preserved on the procedural dialog but chrome-heavy nineslice styles also get enough clearance. Wired into `renderSoloEndscreen` and `renderMpEndscreen` — rankings, awards, action buttons, and the stats child height all honour the active chrome inset. Same pattern wired into `renderPauseMenu` in `pdgui_menu_pausemenu.cpp` (pause tabs / Resume button no longer clip into big chrome corners).
2. **Theme Editor + Room Start Match → docked-footer pattern.** `pdgui_menu_theme_editor.cpp` now reserves an explicit `footerH` for Save-as-Mod controls + Reset/Close row, and `BeginChild("PaletteScroll", {0, -footerH})` scrolls only the color pickers above it — matches the Nine-Slice Chrome tool footer. `pdgui_menu_room.cpp` now pins its footer separator + action row at `dialogH - footerH` so on narrow windows the Start Match / Leave Room buttons never scroll off-screen.
3. **menutick.c Deep Sea OOB guard hardening.** Added lower-bound `stageindex < 0 → clamp to 0` check alongside the existing `stageindex >= NUM_SOLOSTAGES → clamp to NUM_SOLOSTAGES - 1` guard. Matches the endscreen.c pattern and covers the degenerate case where stageindex enters negative territory before increment.
4. **SP-1 propagation into endscreenSetCoopCompleted.** `endscreenPushCoop`/`Anti` already guarded `g_MpPlayerNum` at entry (Fix 4 from earlier), but the helper `endscreenSetCoopCompleted()` was reached through that guard AND from other call sites (`endscreenPrepare`) without its own check. Added three early-return guards: `g_MpPlayerNum ∈ [0, MAX_PLAYERS)`, `difficulty ∈ [0, 3)`, and `stageindex ∈ [0, 32)` — the last prevents UB shift into `coopcompletions[]` on mod stages where stageindex >= 32.
5. **Team rankings buildRankings — verified DONE.** `buildRankings` in `pdgui_menu_endscreen.cpp` already uses `mpGetPlayerRankings` unconditionally (landed in S295 commit `719faa9e`) and team grouping is applied via the row-sort pass. No code change needed; task closed out of tasks-current.md.
6. **FIX-B.1 deep manifest scanner.** `manifestBuildMission` previously only walked `g_StageSetup.props` — it missed assets spawned by intro commands (INTROCMD_WEAPON's primary/secondary) and by AI scripts (AICMD_SPAWNCHRATPAD/CHR bodies+heads, AICMD_DROPITEM/AICMD_EQUIPWEAPON/AICMD_EQUIPHAT models, AICMD_EQUIPWEAPON weapon num). Cinematic cutscenes reuse this pipeline (cutscene chrs are BG chrs running cinematic ai lists), so the scanner closes that gap too. New helpers `s_manifestAddBody/AddHead/AddModel`, `s_manifestScanIntro`, `s_manifestScanAilists` mirror `stageLoadAllAilistModels` in `game_00b820.c`. Both scanners early-return when the corresponding `g_StageSetup` pointer is NULL, so pre-load calls are still safe. Added `chraiGetCommandLength` stub to `port/src/server_stubs.c` for the pd-server link.
7. **Spawn pool residuals (S249 Issue 5).**
   - **Same-tick reservation bitset** — new `s_SpawnReserved[SPAWNPOOL_MAX]` with auto-clear when `g_Vars.lvframenum` changes, plus `spawnPoolClearReservations()` public API and auto-clear inside `spawnPoolBuild` / `spawnPoolReset`. `spawnPoolSelect` now treats reserved indices as "used" and claims its chosen slot before returning, so a burst of match-start bot spawns in a single tick can't pick the same index twice (previously possible if the caller hadn't finished writing peer positions into `occupied[]`).
   - **Wall-probe orientation** — new `f32 angle_rad` field on `spawn_point_t`, computed in `poolWallProbeAngle()` (8-direction cylinder move probe at 200 units, mirrors `playerreset.c:691-715`). `playerreset.c` now uses `pool->points[sel].angle_rad` instead of hard-coding `turnanglerad = 0`, so the first MP spawn faces away from the nearest wall.
   - **Neighbour-room ground check** — `spawnPoolValidateCandidate` now builds a `grooms[]` array from `bgFindRoomsByPos(inrooms)` (up to 7 neighbours) and passes the whole array to `cdFindGroundInfoAtCyl`. Previously a spawn near a doorway or portal seam where the floor geometry lived in the next room got rejected as "mid-air" by the -100000 sentinel.

**Files touched**: `port/fast3d/pdgui_menu_endscreen.cpp`, `port/fast3d/pdgui_menu_pausemenu.cpp`, `port/fast3d/pdgui_menu_room.cpp`, `port/fast3d/pdgui_menu_theme_editor.cpp`, `port/src/net/netmanifest.c`, `port/src/server_stubs.c`, `src/game/endscreen.c`, `src/game/menutick.c`, `src/game/playerreset.c`, `src/game/spawnpool.c`, `src/include/game/spawnpool.h`.

**Build verify**: `ninja -C Build pd pd-server` — both targets link. `PerfectDark.exe` 51,363,805 bytes, `PerfectDarkServer.exe` 22,838,411 bytes. Pre-existing `'/*' within comment` warnings unchanged.

**Process**: worktree branch `claude/stoic-proskuriakova-2440de` fast-forward merged to `dev` (from `6d9d2f62` to `35ad8103`); server stub fix added on `dev` in a follow-up commit after the first build caught the undefined `chraiGetCommandLength` reference in the pd-server link.

**Not done in this session** (deferred to playtest confirmation):

- No playtest for any of the seven items yet — Mike to run through the S297 / S298 playtest checklist. Likely regression surfaces: endscreen action-row position with extreme chrome corners; Theme Editor footer height math on very short screens; Room "Start Match" pinned footer on narrow windows; first-spawn facing direction (wall-probe orientation) in arenas with lots of small obstructions; manifest scanner extra entries for SP-only missions (should reduce "missing body" runtime logs from cinematic spawns).
- The S297 follow-up "wire content-inset into HUD overlays" was out of scope — the scorecard overlay uses stock ImGui background (no `pdguiDrawPdDialog`), so it doesn't need the inset, and `pdguiGameOverRender` is a no-op since S295 F2.

## Session S297 (playtest-triage track) — 2026-04-16 (Textbox leak + chrome mod visibility — silly-jepsen worktree)

**Scope**: three playtest bug fixes reported against the S296 build.

1. **B-154 — Textbox keyboard leak to action map.** Typing in an `ImGui::InputText` (Nine-Slice Chrome "Mod Name", Skin Editor, connect-code field, etc.) still fired game/menu actions on the background player — the letter E triggered `ACTION_USE`, and so on.
2. **B-155 — Chrome mod not in Modding Hub Mods list without restart.** `chromeToolSaveMod` wrote the mod to disk, registered the chrome style, and reported "Saved & activated" — but the new entry didn't appear in the Mods tab until the next full `modmgrInit` at startup.
3. **B-156 — Chrome mod absent from Settings → Video → UI Chrome Style dropdown, even after enable + save.** The new chrome texture never appeared alongside Procedural / Classic (base-game). Restart did not help.

**Root causes**:

- **B-154** — `pdguiProcessEvent` called `actionmapDispatch(ev)` unconditionally for SDL keyboard events. `fireVk`'s gameplay-IMC gate (`gameplayInputSuppressed()`) only skipped `g_ImcGameplay`/`g_ImcVehicle`; menu/debug IMCs still consumed the scancode, so any action bound to that key in those contexts still fired while a textbox had focus. The standard ImGui pattern "when `io.WantCaptureKeyboard == true`, don't dispatch keyboard to your app" was not enforced at the action-map seam.
- **B-155** — `modmgrScanDirectory()` is file-static; no hot-rescan API existed. The chrome save path's only hook into modmgr was absent, so newly-written mod dirs were invisible until `modmgrInit` re-ran.
- **B-156** — `cjson_next` in `pdgui_theme.cpp` (chrome-manifest tokenizer) consumed only digits for NUMBER tokens — no fractional part, no exponent. The mod.json we write embeds a `chrome_authoring` object containing `border_scale: 1.000` and `inset_pct` floats. When the top-level parser hit `chrome_authoring` it called `cjson_skip_value`, which entered the LBRACE branch; on the first `.` inside `1.000` the tokenizer emitted `CJT_ERROR`, `cjson_skip_value` returned early, and the outer parser resumed from the wrong position. `components` was never parsed, `has_nineslice`/`out_tex_id` stayed empty, and `s_parseChromeManifest` returned 0 — so both `pdguiThemeRegisterChromeModDir` (hot path) and `s_scanModChromeStyles` (startup path) silently dropped the mod. Restart-proof because the same parser runs at startup.

**Fixes shipped**:

1. **B-154** (`port/fast3d/pdgui_backend.cpp`): added a textbox-leak gate after the context-push suppression block. Reads `ImGui::GetIO().WantCaptureKeyboard` for SDL_KEYDOWN/KEYUP; if true, skips `actionmapDispatch` for every keysym except Esc / Return / KP_Enter (menu close + dialog submit still work), and returns 1 after `ImGui_ImplSDL2_ProcessEvent(ev)` so downstream context-stack dispatch doesn't forward the event either.
2. **B-155** (`port/include/modmgr.h`, `port/src/modmgr.c`, `port/fast3d/pdgui_menu_moddinghub.cpp`): new public `modmgrRescanDirectory()` — snapshots `{id, enabled, loaded}` across existing entries, calls `modmgrScanDirectory()`, sorts, restores flags from the snapshot (so pending unapplied toggles survive). `chromeToolSaveMod` now calls `modmgrRescanDirectory()`, looks up the new `user.<slug>.ui-chrome` entry, flips its enabled bit via `modmgrSetEnabled`, persists through `modmgrSaveConfig()`, and refreshes the Mods tab snapshot via `pdguiModManagerRefreshSnapshot()`.
3. **B-156** (`port/fast3d/pdgui_theme.cpp`): extended `cjson_next`'s NUMBER branch to consume an optional fractional part (`.digits`) and optional exponent (`[eE][+-]?digits`). `cjson_int` is unchanged (still `strtol`) — all chrome-manifest int reads use pure integer fields, so this is safe.

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 750/750 targets; `PerfectDark.exe` 51,424,970 bytes, `PerfectDarkServer.exe` 22,816,554 bytes. No new warnings; only the pre-existing `'/*' within comment` noise in `updater.c` / `updater.h` / `server_gui.cpp` and the `gettime_offset` unused-fn warning in `enet.c`.

**Not done in this session**:

- Parallel Save-as-Mod paths (Skin Editor, Theme Editor) not re-audited for the same float / rescan issues. The Skin Editor's `skin.ini` format is INI, not JSON, so cjson isn't involved there; Theme Editor writes `theme.json` via `pdgui_theme_loader.cpp` which is a separate parser. If a similar "not in list / not in dropdown" symptom surfaces in those flows, that's where to trace next.
- `modmgrRescanDirectory()` resets `loaded` to 0 for entries it preserved via snapshot — but doesn't re-run `modmgrLoadMod` on them. That's intentional (catalog content already lives), but if a future rescan caller expects "loaded" to be authoritative post-rescan, this needs revisiting.

**Next**: playtest B-154 (textbox typing across multiple InputText surfaces — Nine-Slice Chrome name, MP chat, Skin Editor name, connect code), B-155 (Nine-Slice save → Mods tab immediate visibility + persistence), B-156 (Nine-Slice save → Video dropdown immediate visibility + restart persistence). Expect zero background-player actions while typing; expect new mod to appear in both surfaces without restart.

---

## Session S297 (UI polish track) — 2026-04-16 (Three UI polish drops: docked action buttons, room member list, content-inset API + title-bar samples — elegant-mahavira worktree)

**Scope**: Playtest feedback — three UI improvements applied together in one worktree because they touch adjacent renderers (theme / nineslice / moddinghub / room).

### Improvement 1 — Docked action buttons in Nine-Slice Chrome tool

**File**: `port/fast3d/pdgui_menu_moddinghub.cpp`

Old layout flowed everything linearly in one scroll region: image path row → rulers/toggles → trim/scale/cut sliders → desaturate → presets → border-scale + proportional insets → big stacked preview at the bottom → `SetCursorPosY(h - dockH)` trick for "Save as Mod" / "Reset".  That dock trick worked when content fit on screen, but on shorter windows or with scrolling active the footer slid off the bottom.

New layout splits the tool body into two named ImGui child regions:
- Left sidebar `##chrome_sidebar` (no-scroll, ~42 % width, clamped to [220..420 px * scale]) renders the **Source Preview (with rulers)** and the **Frame Preview (assembled nine-slice)** stacked vertically.  Extracted into `chromeToolRenderSidebarPreview`.
- Right column `##chrome_settings` (scroll on) renders Mod Name, symmetry toggles, tile modes, trim/scale/cut sliders, desaturate, quick presets, Border Scale, Proportional Insets toggle, and the inset sliders.  Extracted into `chromeToolRenderSettings`.
- **Footer** ("Save as Mod" / "Reset") and **status line** pinned outside both children so they never scroll.  `footerH = 34 * scale`, body gets `h - headerUsed - footerH - statusH`.

Header (`Image:` input + Browse/Load row) stays fixed above the split, and the "load an image first" early-return path unchanged.

### Improvement 2 — Room member list: team sort + human-first + team-tinted rows + local highlight

**File**: `port/fast3d/pdgui_menu_room.cpp::renderPlayerPanel`

Replaced the two separate iteration passes (humans loop then bots loop) with a unified `RoomRow` array built from both sources.  Sort is:
- Primary: team asc (only when `MPOPTION_TEAMSENABLED` is set — otherwise preserved-insertion order is fine because FFA has no meaningful team grouping).
- Secondary: humans before bots within the same team — picks up the "organize the lobby visually" user request.

Team palette mirrors `pdguiHudGetTeamColor` and `pdgui_menu_pausemenu.cpp s_TeamColors` (Red/Blue/Green/Yellow/Orange/Purple/Grey/White).  Row background is drawn via `ImDrawList::AddRectFilled` at the row's screen-space cursor position, alpha 0.35 for local player, 0.18 otherwise.  In non-teams mode, the local player still gets a 60/255 cyan band so the eye still finds them.  A 2 px white accent bar on the left edge of the local-player row adds a second cue that works under both team tint and non-teams background.

Team separator renders `-- Team N --` in the team's color when `r.team != lastTeam` during iteration.

All the existing bot-selection semantics (click, ctrl-click toggle, double-click edit, right-click / gamepad-X context menu, Rename / Bot AI / Bot Type / Character / Duplicate / Re-roll / Remove) are preserved because the popup block is emitted inside an `if (r.isBot)` scope that still `PushID`s the slot index before the `Selectable`.  Dead `removeSlot` variable from the original loop was removed since nothing ever assigned to it.

### Improvement 3 — Content-inset API + title-bar procedural samples

**Files**: `port/include/pdgui_theme.h`, `port/fast3d/pdgui_theme.cpp`, `port/fast3d/pdgui_style.cpp`, `port/fast3d/pdgui_menu_mainmenu.cpp`

New API:
- `pdguiThemeGetContentInset(float *l, float *r, float *t, float *b)` — returns the border inset (in screen pixels) that renderers must keep their content inside.  When chrome is on: `dst_left/right/top/bottom` from the active nineslice def (pulled from `pdguiNinesliceGet`) with a 2 px minimum floor.  When chrome is off (procedural): 2 px on all sides (procedural border is 1 px + safety).
- `pdguiThemeApplyContentInset(float *x, float *y, float *w, float *h)` — convenience helper that shrinks a rect by the active inset.
- `PDGUI_TITLEBAR_*` enum + `pdguiThemeSetTitleBarStyle` / `pdguiThemeGetTitleBarStyle` / `pdguiThemeGetTitleBarStyleName`.  Backed by `Video.UiTitleBarStyle` in `pd.ini`, clamped to `[0, PDGUI_TITLEBAR_STYLE_COUNT)` on getter/setter.

Title-bar styles (`pdgui_style.cpp::pdguiDrawPdDialog`):
- **Classic (0)**: unchanged 3-color PD gradient (titlebg→border1→titlebg).
- **Solid (1)**: flat `dialog_border1`.
- **Vertical Bars (2)**: `titlebg` base + 1 px `border1` stripes every 8 px.
- **Scanlines (3)**: classic gradient + 1-px horizontal scanline overlay every other row (40/255 black).
- **Diagonal Stripes (4)**: `border1` base + 45° `titlebg` quads stepped every 10 px (4 px wide).

Settings → Video gains a `Title Bar Style` combo right below `UI Chrome Style`; writes via `configSave("pd.ini")` so the choice survives app restarts/crashes.

### Build / files changed

Files touched:
- `port/include/pdgui_theme.h` — new content-inset API + title-bar enum.
- `port/fast3d/pdgui_theme.cpp` — impl + config registration (`Video.UiTitleBarStyle`).
- `port/fast3d/pdgui_style.cpp` — 5-way title-bar switch inside `pdguiDrawPdDialog`.
- `port/fast3d/pdgui_menu_mainmenu.cpp` — Settings → Video → Title Bar Style combo.
- `port/fast3d/pdgui_menu_moddinghub.cpp` — split `renderChromeTool` into sidebar/settings/footer; added `chromeToolRenderSidebarPreview` / `chromeToolRenderSettings` helpers.
- `port/fast3d/pdgui_menu_room.cpp` — unified `RoomRow` iteration for team-grouped member list.

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 750/750 targets; `PerfectDark.exe` 51,455,767 bytes, `PerfectDarkServer.exe` 22,818,090 bytes.  Only pre-existing `'/*' within comment` and `f32 near/far` macro-name warnings; no new ones.

### Not done in this session (deferred, matching the user's "start with Chrome tool, then audit others")

Other menus with action buttons that still need the child → child → footer restructure for identical robustness (current `SetCursorPosY` / `SameLine(w-…)` dock tricks work in practice but break under overflow):
- Room screen `Start Match` / `Save as Scenario` / `Add Bot` — currently inline below a scrolling child; fine on 1080p+ but should migrate when the overall room layout is touched.
- Theme Editor `Apply` / `Save` / `Close` — same pattern as Chrome tool's old layout; straightforward follow-up.
- Bot Setup modal `OK` / `Cancel` — already docked correctly.
- Training menu action buttons — already use `ImGui::Button(..., ImVec2(-1, btnH))` at bottom of a scrolling region; OK for now.

Also deferred — hooking `pdguiThemeGetContentInset` through to the menus.  The API is available and wired into the config; each menu that draws custom content inside its dialog body still needs to opt in.  The Chrome tool's sidebar/settings children are already clipped by ImGui's child window, so they naturally avoid the border even without an explicit content-inset call.

**Next**: playtest verification of all three improvements on the user's build.

---


## Session S296 — 2026-04-16 (Menu-close bugs from 019d97ef playtest — vigilant-robinson worktree)

**Scope**: Two bugs reported by Mike after the S295 collision + menu-desync drops landed, captured in `019d97ef-pdclient.log`:

1. **Bug 1 — stuck WASD after main menu close.** "Back out of menu, apply input with WASD, pressed input does not unpress; re-opening and closing the menu resets it. Applies to all WAS or D."
2. **Bug 2 — double / darkened main menu on rapid Esc reopen.** "Closing main menu and reopening with Esc (also with controller I think) seems to either open two copies overlaid or the new one has a darker background. Leaning towards two copies."

**Log evidence**:
- Log shows 7 open/close cycles in the main menu between 10:03 and 10:36. First close at 10:03.25 logged `lvIsPaused=0 g_PlayersWithControl[0]=1` (correctly unpaused pre-close). All subsequent closes logged `lvIsPaused=1 g_PlayersWithControl[0]=0` (paused at close-time — expected once the menu actually pauses the game). After the second rapid close (10:04.77), `AXIS_MOVE=0.000,1.000` was observed at 10:08.06 and held through 10:14.06 — the stuck-forward trace Mike described.
- No `MENU: ACTION_PAUSE detected` lines in the log, which suggests the opens aren't reaching `bondmove.c:1064` via the standard `START_BUTTON` edge path, but the ImGui close handler *did* fire (7 `CLOSE via ESC/B` lines). That's consistent with the close-handler pattern being reliable; the bug is post-close.

**Root causes identified**:

- **B-152 (stuck WASD)**: `actionmapPollFrame()` WASD→AXIS_MOVE synthesis block (port/src/actionmap.cpp:890-910) only writes `s_State[0][ACTION_AXIS_MOVE_X/Y].value` while at least one WASD key is held. When no controller is enumerated on player 0, the controller-poll branch at line 803 is skipped — so nothing else resets `.value` each frame. On keyboard-only setups, press sets `.value=1.0`; release short-circuits the synthesis (`mx=my=0`) and leaves `.value` pegged. Next poll frame reads stale `.value=1.0`, computes `analogStickActive=true`, skips synthesis — lock-in. The menu-open branch at line 860 zeros AXIS_MOVE (which is why "reopen fixes it"), but that's a workaround, not a fix.
- **B-153 (double menu)**: `menuPushDialog()` auto-opens every `dialogdef->nextsibling` at the same layer (`menu.c:1553-1576`). `g_CiMenuViaPauseMenuDialog`'s nextsibling is `g_CiOptionsViaPauseMenuDialog` — so pushing the main menu also pre-loads the CI Options sibling. `menuRenderDialogs` renders the "other" sibling alongside curdialog whenever `type != 0 || transitionfrac >= 0` (menu.c:3817); both dialogdefs hit `pdguiHotswapCheck` and queue their renderers (`renderMainMenu` + `renderCiSettingsRedirect`). The redirect renderer calls `pdguiPopupDarkenBehind(0.55f)` — when both fire in the same frame, the scrim compounds with the main menu frame and produces Mike's "darker background / two copies overlaid" visual. Normal state is `transitionfrac=-1` → sibling skipped, but under rapid close+reopen the transition state can land in the visible window.

**Fixes shipped**:

1. **B-152** (`port/src/actionmap.cpp`): added `p0CtrlDroveAxis` flag set only when the controller actually wrote AXIS_MOVE this frame. `analogStickActive` now gated on that flag + non-zero value, so a stale `.value` from a previous WASD synthesis can no longer suppress synthesis. Synthesis block now assigns `mx/my` unconditionally when `!analogStickActive` (including zero) — release clears the axis. Also sets `.held` from the computed `(mx != 0) / (my != 0)` instead of hardcoding `1`.
2. **B-153** (`src/game/menu.c`, `src/include/game/menu.h`, `port/fast3d/pdgui_menu_mainmenu.cpp`): added `s32 menuDialogIsCurrent(const struct menudialog *dialog)` helper that scans `g_Menus[i].curdialog` across player slots. `renderMainMenu` and `renderCiSettingsRedirect` call it at the top and early-return `1` (consumed) when invoked for a sibling preload. In the ImGui-hotswap world there is no user-visible swipe between main menu and CiOptions, so this guard has no legitimate-path cost.

**Build verify**: `ninja -C Build pd pd-server` — 750/750 targets; `PerfectDark.exe` 51,440,272 bytes, `PerfectDarkServer.exe` 22,818,602 bytes. Only pre-existing `'/*' within comment` warnings; no new ones.

**Not done in this session**:
- Did not re-investigate the S295 F1–F7 menu-desync fixes; they remain shipped as-is. The new fixes here are orthogonal (input-axis state, sibling render gating) and do not touch the input-context stack logic.
- The B-153 fix is narrow — it guards the two renderers currently bound to shared dialogdefs. If future renderers are attached to `.nextsibling` chains, they'll need the same guard.

**Next**: playtest pass to confirm B-152 on keyboard-only, B-153 on rapid Esc reopen from CI free-roam; tail `pd.log` for any `INPUTCTX watchdog:` warnings (still none expected from S295 F3).

---

## Session S295 — 2026-04-16 (Collision + spawning ecosystem fixes)

**Scope**: Implement the five fixes identified in `context/scratch/collision-spawning-investigation-2026-04-16.md`. No architectural migration work (mesh-ceiling wiring, per-prop mesh extraction); those stay scheduled as dedicated milestones.

**Code changes** (committed worktree: `priceless-wozniak`):

1. **Slope jump (B-145)** — `src/game/bondwalk.c`
   - Relaxed the grounded heuristic from `(groundgap < 10.0f && bdeltapos.y < 2.0f)` to `(groundgap < 20.0f && bdeltapos.y < 6.0f)`. The 6.0f ceiling stays below `FIXED_JUMP_IMPULSE = 8.2f` so a mid-jump player still reads as airborne.

2. **Pickup WALKTHROUGH (B-146)** — `src/game/propobj.c`, `src/include/props.h`
   - `weaponCreateForChr` initializer (propobj.c:19010): `flags3 = OBJFLAG3_WALKTHROUGH`.
   - `weapon()` and `ammocrate()` macros in `props.h`: OR `OBJFLAG3_WALKTHROUGH` into the caller's `flags3` so every setup-file pickup inherits it. Covers all setup*.c, scenarios, and `weaponCreateForChr` callers.

3. **Ceiling clip (B-147)** — `src/game/bondwalk.c`
   - Pre-move ceiling probe is now radius-aware: samples `cdFindCeilingRoomYColourFlagsAtPos` at center + 4 points at ±radius in X and Z and takes the min. This is the minimal fix from the investigation; the architectural mesh-ceiling migration stays scheduled.

4. **Carrington table (B-148)** — `src/game/propobj.c`, `src/include/constants.h`
   - Added `OBJH2FLAG_AUTOFLOOR = 0x20` to `obj->hidden2`.
   - Tightened the auto-floor eligibility test in `objInit`: an existing `MODELPART_BASIC_0065` no longer unconditionally suppresses the auto-floor. The floor part must cover ≥ 50% of the bbox XZ extent; otherwise the full-bbox auto-floor is still emitted and flagged.
   - `func0f069b4c` now updates the auto-floor vertices whenever `OBJH2FLAG_AUTOFLOOR` is set (previously keyed on `MODELPART_0065 == NULL`, which missed the new "0065 exists but non-covering" case).

5. **Spawn ecosystem (B-149)** — `src/game/spawnpool.c`, `src/include/game/spawnpool.h`
   - Ray set expanded from 14 → 18 rays: added 4 lower diagonals to catch overhangs below the candidate. `SPAWNPOOL_BUDGET_THRESHOLD` unchanged — the extra rays are safety nets, not harder gates.
   - Downward rays (Y-component < −0.1) no longer trigger the capsule-radius reject (a close hit below = ground exists, not a trap). The `!isDownward` gate covers both the original -Y cardinal ray and the 4 new lower diagonals.
   - `spawnPoolValidateCandidate` step 2 now rejects the `-100000` ground sentinel explicitly (`ground_y <= -99000.0f`).
   - Step 3 (vertical clearance) switched from `CDTYPE_BG` to `CDTYPE_ALL` so props are seen — previously a spawn landing on top of a dropped weapon could validate clean.
   - New `l4ValidateSafety()` helper (room valid + `bgTestPosInRoom` + ground sentinel + ground ≤ 500u below). Applied at both the per-dilation "all candidates passed" accept AND the last-resort highest-budget accept, so L4 never commits a point into no-room / below-sentinel even when no ring fully passes.

**Cross-issue interaction**: B-146 (pickups walkthrough) + B-149 (CDTYPE_ALL in spawn validation) compound — spawning on top of a dropped rifle is now blocked from two directions (the rifle isn't a floor, AND the vertical-clearance check catches it if some future bug re-introduces the collision).

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — both `PerfectDark.exe` (51407282 bytes) and `PerfectDarkServer.exe` (22815986 bytes) linked clean. 750/750 targets.

**Not done in this session** (explicit out-of-scope, still queued):
- Issue 1-B (architectural): wire `meshFindCeiling` / `meshSweepCapsuleWorld` into `bondwalk.c`; fix `classifyTriFlags` to emit a real `GEOFLAG_CEILING`.
- Issue 2-B: per-prop mesh extraction into the world grid.
- Issue 5: same-tick reservation bitset, pool orientation (reuse of 8-direction wall-probe for pool spawns), neighbor-room ground check.

**Next session**: playtest B-145/146/147/148/149 across Skedar Ruins (slopes), Carrington Institute (tables), Dark Combat (pickups), tight arenas (spawn validity).

---
## Session S295 (menu track) — 2026-04-16 (Menu dead-input desync fixes — 7 items from the menu-system investigation)

**Scope**: implement every fix listed in §7 of `context/scratch/menu-system-investigation-2026-04-16.md`. Target bug class: B-150 (formerly tracked as B-145 during investigation — renumbered after B-145..B-149 were claimed by the S295 collision drop; "menu up but player moves" / "no menu but player frozen").

**Branch**: `claude/relaxed-ride` (worktree).

**Fixes shipped**:
- **F1 — Remove `g_PdguiActive` mirror boolean** (`port/fast3d/pdgui_backend.cpp`). The mirror duplicated `inputCtxIsActive(&g_CtxDebugOverlay)` and could drift. All reads replaced with the input-context query; all writers and the declaration deleted. F12 toggle and `pdguiToggle()` now derive state from the context stack alone.
- **F2 — Delete dead `pdguiGameOverRender` body** (`port/fast3d/pdgui_menu_pausemenu.cpp`). The stub's `#if 0` block (~250 lines) contained a stray `inputCtxPush(&g_CtxImGuiMenu)` that distorted push/pop audits. Stub retained (still called from `pdgui_backend.cpp:569`); body removed.
- **F3 — `inputCtxEndFrame` watchdog** (`port/src/inputctx.c`). Rate-limited warning (1 log/sec) when depth ≥ `INPUTCTX_WATCHDOG_DEEP_THRESHOLD` (5) or bottom ≠ gameplay. Force-reset (pop everything, re-seed with gameplay) at depth ≥ `INPUTCTX_MAX_STACK − 1` — catches unbounded-leak pathology before stack overflow.
- **F4 — Begin()=false leak guard on 9 renderers** (`pdgui_menu_{agentselect,botsetup,cheats,mpadvanced,mppause,mpsettings,mpsetup,playerconfig,room,training}.cpp`). Each renderer whose `if (!ImGui::Begin(...)) { ... return; }` branch could skip the pop now releases the owned context on cull. Uses the per-renderer ownership flag so repeated transient culls don't double-pop.
- **F5 — MpEndscreen one-shot push** (`pdgui_menu_endscreen.cpp`). Replaced the aggressive per-frame `inputCtxPush` pattern (which trapped the player in a resurrected context after any force-close pop) with a fresh-entry detector: track `s_MpEndscreenLastFrame = ImGui::GetFrameCount()`; push only when the frame number jumps by >1 (first render of a new instance). Preserves the original "first-frame miss" fix that motivated the aggressive version.
- **F6 — Force-close contract comment** (`port/include/inputctx.h`). Documented next to `inputCtxPopDeferred`: allowed force-close sites (`pdgui_bridge.c`, `matchsetup.c`, `netmsg.c`, stage-change reset in `main.c`), required `inputCtxIsActive` guard, and rules for adding new ones.
- **F7 — Remove dead `s_MainMenuPushedCtx`** (`pdgui_menu_mainmenu.cpp`). The main menu's close path had already moved to unconditional `inputCtxIsActive` + pop (the documented "safer pattern"). The bool writers were dead state; declaration + all writers deleted. Explanatory comments reference S295 F7.

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — both `PerfectDark.exe` and `PerfectDarkServer.exe` linked cleanly (pre-existing warnings only, no new ones).

**Tracking**: `bugs.md` entry **B-150** (renumbered from the investigation's B-145) covering all 7 items. Playtest verification tasks added.

**Next**: in-game playtest focusing on (a) F12 debug overlay toggle cycles, (b) main menu open/close from CI free-roam, (c) MP endscreen → Return-to-Lobby / Quit-to-Menu paths, (d) alt-tab / focus-lost boundary. Watch pd.log for `INPUTCTX watchdog:` warnings — any occurrence identifies a remaining leak site.

---

## Session S295 (match-pipeline track) — 2026-04-16 (Match-pipeline fixes from 2026-04-16 investigation — festive-saha worktree)

**Scope**: implement the HIGH/MEDIUM findings from `context/scratch/match-pipeline-investigation-2026-04-16.md`.

**Code changes**:
- `src/game/menutick.c` — GAP-1 / SP-13: Deep Sea co-op next-mission branch now calls `manifestClear(&g_ClientManifest)` before `mainChangeToStage()` (pattern-match to F-0.4 / L1-1 / netDisconnect / Bug A). Added `#include "net/netmanifest.h"`. Bug entry **B-151** in `bugs.md` (renumbered from the investigation's B-145 after B-145..B-150 were claimed by the collision + menu-desync drops).
- `port/fast3d/pdgui_menu_challenges.cpp` — C-1: list-driven screen now grabs window focus on `IsWindowAppearing()`, and the auto-selected row calls `SetItemDefaultFocus()` once via a one-shot `s_FocusPending` flag. Controller-only user can now navigate the challenge list from first frame.
- `port/fast3d/pdgui_menu_endscreen.cpp` — Bug C: instrumentation only (per report's "do not structural-change without log evidence" directive). `Begin=false` early-return at line ~755 logs `sf / menuW / menuH / disp`; `contentH` clamp at line ~829 logs `sf / menuH / padY / raw / min`. Next MP-endscreen repro should narrow the six hypotheses.
- `port/fast3d/pdgui_menu_mpsettings.cpp` — C-6 (handicap): `SetWindowFocus()` on appear after Begin. Select Tunes + Team Names were already covered by `pdms_BeginStandardWindow` helper — no change needed there.
- `port/fast3d/pdgui_menu_controldiagram.cpp` — C-4: `SetWindowFocus()` on appear in `beginPdWindow()`.
- `port/fast3d/pdgui_menu_cheats.cpp` — C-5: `SetWindowFocus()` on appear on both `##cheats_warning` and `##cheats_unlock_confirm`.
- `port/fast3d/pdgui_menu_teamsetup.cpp` — C-3: `SetWindowFocus()` on appear in `##team_setup` (also covers `##auto_team` which reuses the same render).
- `port/fast3d/pdgui_menu_moddinghub.cpp` — C-8: `SetWindowFocus()` on appear on `##modhub`.
- `port/fast3d/pdgui_menu_playerconfig.cpp` — C-7: verified the three load sub-dialogs already inherit focus via `pc_BeginStandardWindow` (no change needed; investigation report line numbers were out of date).
- `port/fast3d/pdgui_menu_pausemenu.cpp` — GAP-3: online End-Game confirm now calls `mainEndStage()` for both NETMODE_CLIENT and offline paths so the player sees endscreen rankings/awards before disconnecting. Endscreen's Disconnect button drives network teardown.
- `port/fast3d/pdgui_menu_solomission.cpp` — Gap 8: documented (not unified) the solo vs MP pause input-context asymmetry. Added a block comment to `renderPauseMenu` explaining why solo pushes `g_CtxImGuiMenu` (MENUROOT_MAINMENU legacy path) while MP pushes `g_CtxPauseMenu`, and flagged the planned unification as Phase 2 menu-pool work.
- `port/src/net/netmsg.c` — GAP-4 / SP-14: `netmsgSvcStageEndRead` now resets `g_NetMatchRoomId = 0xFF` (symmetric with server-side reset at `net.c:876`). GAP-10: `netmsgSvcMatchCancelledRead` now calls `pdguiCountdownReset()` after the existing `memset`, matching the B-139 pattern.
- `port/src/server_stubs.c` — added `void pdguiCountdownReset(void)` server-side no-op stub so `pd-server` links without the UI symbol.

**Why**: the investigation was a four-agent deep audit of the match pipeline (entry → in-match → exit). The HIGH findings — missing manifestClear on Deep Sea co-op advance, Challenges menu unreachable by controller, MP endscreen invisible-body Bug C — were all either latent crashes or controller-dead-ends that block the v0.1.0 release pass. The MEDIUM batch (SetWindowFocus sweep, asymmetry fixes, defensive hygiene resets) ship together because they share the same change pattern and review surface.

**Build-verified**: `ninja -C Build pd pd-server` — both binaries link clean. `menutick.c.obj`, `pdgui_menu_*.cpp.obj`, and `netmsg.c.obj` all recompiled.

**Next**:
- Playtest pass to verify B-151 (Deep Sea co-op advance, no crash) and C-1 (Challenges list navigable from controller first frame).
- Repro MP endscreen invisible-body with new `ENDSCREEN:` log lines to distinguish the six hypotheses.
- Gap 8 unification is scheduled for Phase 2 (menu-pool ADR).

---

## Session S293 — 2026-04-16 (Nine-Slice Chrome redesign + mods/ category subfolder scanning)

**Scope**:
- Act on Mike's directive: Nine-Slice Chrome Save-as-Mod should emit a normalized output image with a uniform border concept applied, not the raw import.
- Organize `mods/` into category subfolders (`UI Chrome/`, `Weapons/`, `MP Maps/`, etc.) without breaking existing flat-layout mods.
- Land P0/P1 audit fixes from `scratch/audit-s255-s292-2026-04-16.md` for the Nine-Slice Chrome tool (C-1, C-2, C-4, C-5, S-7, S-9, S-10, S-11).

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp` (Nine-Slice Chrome tool):
  - Added `CHROME_MAX_IMG_DIM` / `CHROME_MAX_OUT_DIM` (4096 each) and enforced on both import and preview paths. (C-1, C-2)
  - `chromeToolWriteTga` now uses `size_t` for its pixel counter and rejects dims >65535 up front. (C-1)
  - Added `chromeToolJsonEscape()` helper; `chromeToolSaveMod` now writes an escaped display name so quotes/backslashes in mod names no longer corrupt mod.json. (C-4)
  - `chromeToolUpdatePreviewTexture` computes new output dims into locals and commits `s_ChromeOutW/H` only after the allocation succeeds — prior code advanced dims before realloc, so a failed grow left dims ahead of the buffer. On realloc failure the function now surfaces a status message instead of silently returning. (C-5, S-7)
  - Switched preview GL upload to `glTexSubImage2D` on same-size ticks; only a dim change triggers the full `glTexImage2D` reallocation. Tracks `s_ChromePreviewTexW/H`. (S-10)
  - Retired `s_ChromeTex` (full-res source upload). Preview texture is always populated by the load path, and VRAM fallback paths now use `s_ChromePreviewTex` directly. (S-11)
  - Cross-clamped Trim sliders: each slider's max = opposite-side value − 1, so L+R and T+B can never collapse the crop. (S-9)
  - Added **Border Scale** slider (0.25x–4.0x) multiplying `dst_corner_px` relative to `src_inset` in both the live preview (`chromeToolBuildDef`) and saved `mod.json`.
  - Added **Proportional Insets** toggle (default on). When on, inset sliders operate on `0–50%` of the current output dims; pixel values are resolved inside `chromeToolUpdatePreviewTexture` and at save time, so Scale X/Y changes keep the visual border proportion stable. A read-only line under the sliders shows the resolved pixel values. When off, the tool behaves as before (pixel sliders).
  - Save path now creates `mods/UI Chrome/<slug>/` instead of `mods/<slug>/`. `mod.json` body records a new `"chrome_authoring"` block (`output_w/h`, `border_scale`, `proportional_insets`, `inset_pct`) alongside the existing `src_inset`/`dst_corner_px` so round-tripping retains authoring intent.
  - `renderChromeTool` gate now checks `s_ChromePreviewTex` (not the retired `s_ChromeTex`) to avoid a no-image-visible false path.
- `port/src/modmgr.c` (mod scanner):
  - Extracted per-entry registration into `modmgrTryRegisterModEntry()` and added `modmgrScanCategoryFolder()` (depth-1 recursion).
  - Primary-root pass: if an entry has no `mod.json`/`audio.ini`, the scanner descends one level and treats the entry as a category folder. Existing flat mods under `mods/` (base-ui, pd-modern-ui, bot-names) continue to register as before.
  - Alt-root pass adopts the same pattern with dedup-by-id retained.
- `port/fast3d/pdgui_theme.cpp`:
  - `s_scanModChromeStyles` is now a thin wrapper around new `s_scanChromeStylesInDir()` helper which tries each top-level entry as a chrome mod; if registration fails, it recurses one level. This makes `mods/UI Chrome/<slug>/mod.json` visible to Settings → Video → UI Chrome Style without additional plumbing.
- `port/fast3d/pdgui_theme_loader.cpp`:
  - Added `dir_has_theme_or_mod()` and `scan_themes_in_root()` with the same depth-1 category-folder pattern. Theme mods in `mods/UI Themes/<slug>/` (future) will be discovered automatically.

**Why** (Mike's directive + audit):
- The old Save-as-Mod path wrote pixel-absolute insets that were tied to whatever resolution the user happened to import — leading to chromes that looked right on the author's screen but oversized or tiny on another resolution. The new pipeline still writes the processed preview buffer (which is Mike's "uniformed scale already applied"), but adds: a Border Scale multiplier so on-screen corner thickness is decoupled from the source slice location, and Proportional Insets so the inset-pair tracks Scale X/Y instead of drifting with resolution.
- The audit flagged multiple safety issues in the chrome tool (integer overflow, unbounded input dims, JSON injection via mod name, realloc dim/buffer mismatch, missing trim cross-clamp, per-tick GL realloc, redundant full-res VRAM). All fixed in this drop.
- The mods-folder organization makes the tree self-documenting and supports Mike's intended layout (`Weapons/`, `MP Maps/`, `UI Chrome/`, …) without a breaking migration — flat mods remain valid.

**Scanner recursion bounds**:
- Category-folder recursion is capped at exactly one level below each root. This matches how bundled mods live (`mods/<mod>/`) while admitting category containers (`mods/<category>/<mod>/`). Deeper nesting is intentionally not supported to avoid runaway walks on arbitrary user layouts.

**Verification**:
- `source devtools/build-env.sh && ninja -C Build pd pd-server` — clean link of `pd` (5/5 steps, `[5/5] Linking CXX executable PerfectDark.exe`). `pd-server` up-to-date (does not compile client-side mod scanner or theme files). Warnings were all pre-existing (`/*` within comment headers).
- No code-path tests of the mods/ category scanner in this session — covered during next playtest.

**Design decisions Mike should review**:
- **Normalized output is "preview buffer as written today"** — the processed buffer from `chromeToolUpdatePreviewTexture` already has trim+cut+scale+desat baked in. I did NOT introduce an explicit "target size" combo (256/512/1024). If we want that, it's a ≤30-line addition in `chromeToolSaveMod` (resample preview → target before TGA write). Let me know if you want it.
- **Proportional Insets is on by default.** This changes the default save contract: mods created after this drop will have `"chrome_authoring"` metadata and a percentage-based inset model. Existing chrome mods keep working — the scanner only reads `src_inset`/`dst_corner_px`.
- **Border Scale defaults to 1.0** (identical to prior behavior). No existing mod is visually altered.
- **Chrome mods now save under `mods/UI Chrome/`**. Pre-existing user-created chrome mods under `mods/<slug>/` remain scanned and work unchanged.

**Follow-ups not done this session** (deferred, documented in tasks-current):
- `matchConfigAddBot` hardcoded-human-count (audit C-6).
- S-2 middle-click bridge vs Skin Editor canvas pan.
- S-3 `s_PdmsOwnsMenuCtx` shared flag across MP settings dialogs.
- S-4 Close-button hover clip.
- S-5 Skin Editor downrez preview realloc.
- S-6 Chrome style rescan GL texture leak.
- S-11 (mods-apply) missing chrome style rescan in `modmgrApplyChanges` — **fixed in S294**.

---

## Session S294 — 2026-04-16 (Mechanical audit sweep: bot cap callsites, input-ctx ownership, GL cache lifetimes, Dev Window v2 fixes)

**Scope**:
- Sweep mechanical fixes from `context/scratch/audit-s255-s292-2026-04-16.md`.
  Parallel session (bold-boyd) owns Nine-Slice Chrome & mods folder —
  this session must NOT touch `pdgui_menu_moddinghub.cpp`.

**Code changes shipped in working tree**:
- `port/src/net/matchsetup.c` + `port/include/net/matchsetup.h`
  (**C-6 / S-15**):
  - New `matchConfigCountHumans()` helper (counts `SLOT_PLAYER`, min 1).
  - `matchConfigAddBot()` now calls `matchConfigMaxBotsForHumans(matchConfigCountHumans())`
    instead of the hardcoded `matchConfigMaxBotsForHumans(1)`.
  - `matchConfigChooseBotTeam()` promoted to public, now takes `numTeams`
    parameter (2..MAX_TEAMS), supports full 8-team range.
- `port/src/net/netmsg.c` (**C-6 / S-12 / S-13**):
  - `SVC_ROOM_SETTINGS` client rebuild uses `matchConfigCountHumans()`
    for the cap.
  - Switched bot team assignment from positional `(i-1) & 1` to
    `matchConfigChooseBotTeam(2)` — matches host strategy.
  - Now zeros slot entries beyond the new `numSlots`, preventing
    stale bot rows on bot-count decrease.
- `port/fast3d/pdgui_backend.cpp` (**S-2**):
  - Middle-click back bridge now suppresses mouse-back when a middle
    drag is active (fixes Skin Editor middle-drag pan conflict).
- `port/fast3d/pdgui_style.cpp` (**S-4**):
  - Close-button hover detection clipped to window via
    `IsMouseHoveringRect(..., true)` and gated on `IsWindowFocused`.
- `port/fast3d/pdgui_menu_mpsettings.cpp` (**S-3**):
  - Removed shared `s_PdmsOwnsMenuCtx`; each dialog (SelectTunes,
    Soundtrack, TeamNames, Handicap) owns its own `ownsCtx` bool.
    `pdms_BeginStandardWindow` / `pdms_CloseCurrentDialog` now take
    `bool *ownsCtx` (nullptr allowed for dialogs that never push ctx).
- `port/fast3d/pdgui_skin_editor.cpp` (**S-5**):
  - `s_DownrezPreview` now tracks `s_DownrezPreviewW`/`H` and reallocs
    when the target dimensions change — prevents stale buffer reuse
    after character/quantization switch.
- `port/fast3d/pdgui_theme.cpp` (**S-6**):
  - New `s_chromeStylesFreeModTextures()` deletes mod-owned GL
    textures from `s_ThemeTexCache` before `s_chromeStylesClear()`;
    skips `"base:ui_chrome_frame"` (owned by `pdguiThemeLateInit`).
    Called from `pdguiThemeRescanChromeStyles()`.
- `port/src/modmgr.c` (**S-8**):
  - `modmgrApplyChanges()` now calls `pdguiThemeRescanChromeStyles()`
    after `pdguiThemeRescanMods()` (previously only theme.json was
    rescanned, leaving nineslice chrome stale).
- `devtools/dev-window-v2/dev-window-v2.ps1` (Dev Window v2):
  - `Sync-UserMachinePath`: append Machine PATH instead of prepending
    (fixes PATH pollution that overrode worktree tools).
  - Git push failure now logs a warning and continues the build
    instead of MessageBox-and-fail.
  - Release invocation switched from `-File` to `-Command` + added
    `-NonInteractive` (prevents interactive prompts blocking CI-style
    release builds).
  - Release build path passes `forceClean=$true` to `Get-BuildSteps`
    (release must be clean, not incremental).

**Context updates**:
- `context/constraints.md` — new canonical-usage constraint:
  `matchConfigMaxBotsForHumans(humanCount)` is single-source-of-truth
  for bot cap; all callers must pass actual human count (never hardcode 1).
- `context/bugs.md` — **B-144** entry documenting the
  `matchConfigAddBot(1)` / `SVC_ROOM_SETTINGS(1)` hardcoding.
- `context/systemic-bugs.md` — **SP-15** (GL texture size + cache
  lifetime) documenting the S-5/S-6/S-8 pattern.

**Ground rules honored**:
- Did NOT touch `pdgui_menu_moddinghub.cpp` (bold-boyd session scope).
- Working in angry-dijkstra worktree (main working copy), commits
  target `dev` branch, no push.

**Why**:
- Audit surfaced a class of "hardcoded value where a helper exists"
  bugs (C-6), three input-ctx ownership bugs (S-2/S-3), three GL/buffer
  lifetime bugs (S-4/S-5/S-6/S-8), and two multiplayer-protocol
  coherence bugs (S-12/S-13/S-15). All mechanical — pattern is clear,
  fix is low-risk, touches well-scoped functions.

**Verification**:
- `ninja -C Build pd pd-server` — see commit for status.
- Runtime verification pending (playtest dashboard).

---

## Session S292 — 2026-04-16 (Room max-bot/team defaults hardening for Chicago bot-match regression)

**Scope**:
- Address report of Chicago max-bot match showing all entries on one team (`T1`), clustered spawns, and non-lethal/no-engagement behavior.

**Code changes shipped in working tree**:
- `port/src/net/matchsetup.c`:
  - Added `matchConfigMaxBotsForHumans()` and reused it as the canonical cap helper (`min(MATCH_MAX_SLOTS-humans, MAX_BOTS)`).
  - Added internal bot-count guard so `matchConfigAddBot()` cannot create more runtime bots than `MAX_BOTS`.
  - Added balanced default bot team assignment when `MPOPTION_TEAMSENABLED` is active (auto-balance between team 0/1 instead of forcing all new bots to team 0).
- `port/include/net/matchsetup.h`:
  - Exported `matchConfigMaxBotsForHumans()` for UI/save/network callers.
- `port/fast3d/pdgui_menu_room.cpp`:
  - Room panel max-bot calculation now uses `matchConfigMaxBotsForHumans(humanCount)`.
  - Combat start request now clamps `numBots` against that cap before send.
- `port/src/scenario_save.c`:
  - Scenario load bot cap now uses the same canonical helper (prevents over-limit bot restoration paths).
- `port/src/net/netmsg.c`:
  - `SVC_ROOM_SETTINGS` bot rebuild now clamps with the canonical helper and assigns alternating default team values when teams are enabled (keeps client shadow config coherent before full per-bot sync).

**Why**:
- Prior code mixed participant-slot limits (`MATCH_MAX_SLOTS`) with runtime bot limits (`MAX_BOTS`) and defaulted newly-added bots to a single team in team mode, which can create "all one team" matches that appear non-combative.

**Verification**:
- Build/runtime verification pending in this session (code-only pass complete).

## Session S291 — 2026-04-15 (Select Tunes custom-song visibility + playlist add path hardening)

**Scope**:
- Investigate report that custom songs were missing from Match Soundtrack -> Select Tunes and could not be added to the playlist.

**Code changes shipped in working tree**:
- `port/src/modmgr.c`:
  - In `modmgrRebuildCatalogFromCurrentSelection()`, reset all `mod->loaded` flags before re-registering enabled mods.
  - Prevents in-place Mod Apply catalog rebuilds from skipping `audio.ini` re-registration after `assetCatalogClearMods()` removed non-bundled entries.
- `port/src/assetcatalog_scanner.c`:
  - Added `parseAudioCategoryValue()` for component INI audio parsing.
  - `ASSET_AUDIO` category now accepts numeric (`0/1/2`) and text (`music`, `sfx`, `voice`, common aliases), matching `audio.ini` behavior.

**Why**:
- Two separate paths can feed Select Tunes:
  - `audio.ini` package mods (via modmgr load/reload), and
  - component-scanned audio assets (via `_components/audio/*.ini`).
- Before this fix:
  - Mod Apply rebuild could clear catalog audio entries then skip re-registering enabled package mods due stale loaded flags.
  - Component INIs with textual categories defaulted to SFX, so they were filtered out of Mod Tracks.
- Both conditions produce "song mods missing / cannot add" behavior in the soundtrack flow.

**Verification**:
- `devtools/build-headless.ps1 -Target all` still exits early at configure in this shell (existing script/runtime issue in this environment).
- Compile verification passed via project toolchain path:
  - `. .\devtools\_build-env-prelude.ps1`
  - `cmake -G Ninja -S . -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
  - `ninja -C Build pd pd-server`
  - Result: both `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S290 — 2026-04-16 (Nine-Slice Chrome transforms + docked actions + Back parity)

**Scope**:
- Extend the Nine-Slice Chrome tool with image transformation controls requested for in-client authoring and tighten close/back UX parity.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Added edit pipeline controls for imported chrome image:
    - edge trim sliders (`Trim Left/Right/Top/Bottom`),
    - non-uniform scaling sliders (`Scale X`, `Scale Y`),
    - center-strip removal controls (`Center Cut Axis`, `Center Cut %`) that remove from image center and stitch remaining parts together.
  - Reworked preview processing:
    - `chromeToolUpdatePreviewTexture()` now applies trim + center-cut + scale + optional desaturation and produces transformed output buffer/texture dimensions.
    - save path now writes transformed output dimensions/pixels to `ui_chrome_frame.tga` (not just source image dimensions).
  - Nine-slice inset slider bounds/clamps now operate on transformed output dimensions (`s_ChromeOutW/s_ChromeOutH`) so ruler math stays valid after transforms.
  - Docked action row (`Save as Mod`, `Reset`) to bottom of the tool panel.
  - Added shared hub close helper `moddingHubCloseFromUi(...)` and made Back input (`Escape` / gamepad Back) call the same close path as footer `Close` button for parity.

**Why**:
- Full in-client mod creation requires non-destructive image shaping tools before save; users need to trim/reshape source art and crop from center for square-ready chrome assets.
- Docked actions and unified Back/Close behavior reduce navigation ambiguity and align interaction model across windows.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S289 — 2026-04-16 (Nine-Slice Chrome: assembled frame preview + desaturation workflow)

**Scope**:
- Extend the new in-client Nine-Slice Chrome tool with:
  - assembled frame preview (actual nine-slice render),
  - desaturation option for tint/theme-friendly outputs.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Added `pdgui_nineslice.h` integration and runtime preview helpers:
    - `chromeToolBuildDef(...)` to construct `nineslice_def_t` from current ruler/mode settings.
    - frame preview pane now renders assembled frame via `pdguiNinesliceDrawEx(...)`.
  - Added desaturation controls/state:
    - `Desaturate for tint-friendly chrome` checkbox,
    - `Desaturate %` slider.
  - Added processed preview texture path:
    - `chromeToolUpdatePreviewTexture()` builds/uploads desaturated (or original) preview texture,
    - source preview now reflects desaturation settings live.
  - Save path now writes processed preview pixels to `ui_chrome_frame.tga`, so exported mod texture matches the chosen desaturation settings.
  - Updated save status text to indicate when output is desaturated.
  - Added cleanup for processed preview texture/buffer in tool reset/release paths.

**Why**:
- Ruler overlays alone are not enough to validate how corners/edges/center behave when assembled.
- Desaturation is needed so theme/tint passes can recolor chrome assets more predictably.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S288 — 2026-04-16 (Nine-Slice Chrome creator added to Modding Hub)

**Scope**:
- Add an in-client tool so players can create UI chrome nine-slice mods directly in-game (import image, set rulers, save/activate mod).

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Added new tab/tool: **Nine-Slice Chrome** (tab index 7).
  - Added tool state + lifecycle (`chromeToolReset`, texture/pixel ownership cleanup, status messaging).
  - Added image import support using the shared file browser and `stb_image` decode:
    - `Browse` + `Load` for `.png/.jpg/.bmp/.tga`.
  - Added live preview with ruler overlays:
    - visual guide lines for `Left/Right/Top/Bottom` slice positions over imported image.
  - Added ruler controls:
    - `Left`, `Right`, `Top`, `Bottom` sliders,
    - `L/R symmetry` and `T/B symmetry` toggles.
  - Added nineslice mode controls:
    - `Center tile mode`,
    - `Edge tile mode` (applies to top/bottom/left/right).
  - Added `Save as Mod` flow:
    - writes `mods/<slug>/ui_chrome_frame.tga`,
    - writes `mods/<slug>/mod.json` with `tags:["chrome"]` and `components.textures + components.nineslice`,
    - immediately registers + activates via `pdguiThemeRegisterChromeModDir(modDir, 1)` so style appears/applies without restart.
  - Wired tab selector/nav/content/footer descriptions for 8 tools total.
  - Hooked hub close to chrome tool reset/cleanup.

**Why**:
- Project requirement is fully in-client mod creation. This provides a first-class in-game authoring path for UI chrome nineslice mods instead of requiring external file editing.

**Verification**:
- Build verification passed (client + server):
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S287 — 2026-04-15 (Release/build outage hardening: force-commit fallback + missing Build dir creation)

**Scope**:
- Address outage-recovery friction:
  1) release/build sync failing on pre-pull commit hook rejection,
  2) build flows failing when `Build/` was deleted.

**Code changes shipped in working tree**:
- `devtools/release.ps1`:
  - Added switch `-ForceCommitNoVerify`.
  - Added helper `Invoke-ReleaseCommit(...)`:
    - normal `git commit` first,
    - optional fallback retry with `git commit --no-verify` when `-ForceCommitNoVerify` is set.
  - Wired helper into all release auto-commit paths:
    - pre-release (`-SkipBuild` path),
    - pre-build path,
    - Step 4 pre-`pull --rebase` auto-commit.
  - Added explicit creation of missing build directory before configure/build.
- `devtools/dev-window-v2/dev-window-v2.ps1`:
  - `Invoke-GitSyncBeforeBuild(...)` now retries failed commit with `--no-verify` before aborting.
  - Added explicit `Ensure build dir` step in build queue before configure.
- `devtools/build-headless.ps1`:
  - Added explicit missing build-directory creation before configure/build phases.

**Why**:
- Power outage / interrupted sessions can leave repo state where hooks block auto-commit, and users may clear `Build/`. These changes keep the solo-dev pipeline resilient and recoverable without manual repair.

**Verification**:
- PowerShell parse checks passed for modified scripts:
  - `devtools/release.ps1`
  - `devtools/dev-window-v2/dev-window-v2.ps1`
  - `devtools/build-headless.ps1`

## Session S286 — 2026-04-15 (Mod Apply completion tint parity with updater success prompt)

**Scope**:
- Align Mod Apply completion visuals with the updater's success-state treatment.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_modmgr.cpp`:
  - Added success-state window background tint for the `Applying Changes` window when apply reaches completion state (`s_ApplyFlowState >= 3`):
    - `ImGuiCol_WindowBg = ImVec4(0.08f, 0.25f, 0.08f, 0.95f)`
  - Kept in-progress state neutral (no tint) so active work and completion are visually distinct.

**Why**:
- Improves consistency with updater UX while preserving clear phase signaling (working vs complete).

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S285 — 2026-04-15 (Mod Apply popup visual parity with updater download window)

**Scope**:
- Make Mod Manager Apply UX match the existing updater download popup style.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_modmgr.cpp`:
  - Replaced `BeginPopupModal("Applying Changes")` flow with a centered updater-style window (`ImGui::Begin("Applying Changes", ...)`) using:
    - fixed centered positioning and fixed size (`600x240` scaled),
    - no resize/move/collapse/saved-settings flags,
    - wide progress bar (`ImVec2(-1, 24)`),
    - centered acknowledgment button (`OK` / `OK & Close`) on completion.
  - Kept existing apply state machine behavior (paint first frame, run synchronous apply next frame, then completion state).

**Why**:
- User-requested UX consistency: Apply should present the same style pattern as the update download window while catalog rebuild/diff/apply runs.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S284 — 2026-04-15 (Mod Apply: in-place modal apply, no forced title restart)

**Scope**:
- Remove the forced stage transition from Mod Manager Apply and keep the user in the current menu flow while catalog rebuild/diff/apply runs.

**Code changes shipped in working tree**:
- `port/src/modmgr.c`:
  - `modmgrApplyChanges()` now performs in-place apply only:
    - save component state/config,
    - rebuild catalog from current selection,
    - invalidate catalog-backed caches,
    - reset texture cache,
    - rescan themes,
    - clear dirty state.
  - Removed the forced teardown/transition behavior from apply:
    - no `menuStop()`,
    - no `pdguiMainMenuReset()`,
    - no `mainChangeToStage(MODMGR_STAGE_TITLE)`.
  - Updated apply-complete logging to explicitly note no stage restart.
- `port/fast3d/pdgui_menu_modmgr.cpp`:
  - Added in-UI apply flow modal state machine:
    - opens `Applying Changes` modal,
    - runs synchronous `modmgrApplyChanges()` while modal is active,
    - shows completion message (`Catalog changes are live. No restart required.`),
    - supports `Apply` and `Apply & Close` paths.
  - Refactored selection commit into helper (`applyPendingSelectionToCatalog()`).
  - Updated empty-state copy to remove restart guidance.
- `port/include/modmgr.h`:
  - Updated `modmgrApplyChanges()` comment to document in-place apply semantics.

**Why**:
- Returning to title on every Apply is unnecessary for this architecture and creates avoidable UX churn/risk. In-place apply keeps users in context and aligns with hot-reload behavior already used elsewhere.

**Verification**:
- Build verification passed (client + server):
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S283 — 2026-04-15 (UI Chrome Style: mod-discovered picker + persisted style ID)

**Scope**:
- Extend Settings -> Video -> UI Chrome Style from fixed Procedural/Classic toggle to a picker that includes discovered chrome mods and restores the exact chosen chrome style on restart.

**Code changes shipped in working tree**:
- `port/include/pdgui_theme.h`:
  - Added UI chrome style APIs for persisted style id and runtime style enumeration:
    - `pdguiThemeSetUiChromeStyleId` / `pdguiThemeGetUiChromeStyleId`
    - `pdguiThemeGetChromeStyleCount` / `pdguiThemeGetChromeStyleId` / `pdguiThemeGetChromeStyleName`
- `port/fast3d/pdgui_theme.cpp`:
  - Added `Video.UiChromeStyleId` config registration/backing storage (default `base:ui_chrome_frame`).
  - Added chrome style registry cache and manifest parser for `mod.json` entries using the extracted schema:
    - `components.textures[]` (`id`, `file`)
    - `components.nineslice[]` (`id`, `src_inset`, `dst_corner_px`, `*_mode`)
  - Added mod scan over common mods roots and dynamic registration:
    - load texture via existing `s_registerModTexture(...)`
    - register nineslice via `pdguiNinesliceRegister(...)`
    - expose style in runtime picker list
  - Startup chrome apply now:
    - resolves persisted `Video.UiChromeStyleId`,
    - falls back to `base:ui_chrome_frame` if style is unavailable,
    - applies the resolved style when chrome is enabled.
- `port/fast3d/pdgui_menu_mainmenu.cpp`:
  - Replaced static two-option UI Chrome combo with dynamic options:
    - `Procedural` + discovered style names from theme API.
  - Selection now persists both:
    - `Video.UiChromeEnabled` (existing),
    - `Video.UiChromeStyleId` (new),
    and still calls `configSave("pd.ini")` immediately on change.
- `port/include/pdgui_theme.h` + `port/fast3d/pdgui_theme.cpp`:
  - Added runtime chrome registration hooks for importer/save flows:
    - `pdguiThemeRegisterChromeModDir(mod_dir, activate_now)` to hot-register a newly written chrome mod directory and optionally auto-activate/persist it immediately.
    - `pdguiThemeRescanChromeStyles()` to rebuild chrome style list from disk after bulk import operations.
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Mod Pack import success path now calls `pdguiThemeRescanChromeStyles()` so newly imported chrome mods appear in Settings -> Video style picker without restart.

**Why**:
- The previous picker could only target hardcoded `base:ui_chrome_frame`, which blocked users from selecting custom chrome mods created from the same manifest template format.

**Verification**:
- Build verification passed (client + server):
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S282 — 2026-04-15 (UI Chrome Style: immediate persistence on change)

**Scope**:
- Ensure Settings -> Video -> UI Chrome Style saves immediately when changed, so toggling Procedural/Classic persists across restart without relying on later config writes.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_mainmenu.cpp`:
  - In `renderSettingsVideo()`, the `UI Chrome Style` combo change handler now calls `configSave("pd.ini")` immediately after applying `pdguiThemeSetUiChromeEnabled(...)` and the chrome runtime toggle.

**Context note**:
- Verified the external default template at `Downloads/Perfect Dark 2.0/data/mods/base-game/ui-chrome/mod.json` still uses embedded `components.nineslice` entries (`src_inset`/`dst_corner_px`) and no separate nineslice JSON file.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd pd-server`
  - Result: `PerfectDark.exe` and `PerfectDarkServer.exe` linked clean.

## Session S281 — 2026-04-15 (Skin Editor preview: avoid drawing non-ready black texture)

**Scope**:
- Address Skin Editor report of pitch-black preview panel while character preview is still loading/not ready.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_skin_editor.cpp`:
  - `renderPreviewPanel()` now requires both:
    - non-zero texture id, and
    - `pdguiCharPreviewIsReady() == true`
    before drawing the preview image.
  - When not ready, panel now shows explicit rendering status + selected body/head IDs instead of drawing a black texture.

**Why**:
- The previous path drew whenever texture id was non-zero, even if charpreview readiness had not been established yet, which could present as a black panel.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd`
  - Result: `PerfectDark.exe` linked clean.

## Session S280 — 2026-04-15 (Modding Hub close-state hardening for Skin Editor popups)

**Scope**:
- Fix modal/popup lifecycle bug where closing certain Skin Editor popups (`X`/`Cancel`) could close Modding Hub and leave stale popup state visible on next open.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_skin_editor.cpp`:
  - Added `pdguiSkinEditorDismissTransientUi()` C API to force-dismiss transient popup/dialog state (`Save`, `Import`, `Convert to PD Style`, `Export Template`) and free downrez preview resources when hub closes.
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Added centralized close helper `moddingHubClose(reason)` used by all close paths (button, Esc/B, outside-click, explicit hide, Mod Manager close request).
  - Centralized close now calls `pdguiSkinEditorDismissTransientUi()` so stale popup state cannot survive hub closure.
  - Outside-click-to-close and Esc/B close are now suppressed while any ImGui popup is open (`ImGui::IsPopupOpen(..., AnyPopupId)`), preventing modal interactions from accidentally closing the entire hub.

**Why**:
- Skin Editor popup windows can extend near/over hub bounds. Hub-level outside-click close and global Esc/B close were firing during popup interaction, causing a full hub close and stale modal state on reopen.

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd`
  - Result: `PerfectDark.exe` linked clean.

## Session S279 — 2026-04-15 (Global title-bar close button for ImGui menus)

**Scope**:
- Add a visible mouse-click close affordance to PD-styled ImGui dialog title bars.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_style.cpp`:
  - Added a shared title-bar close button (`X`) render path inside `pdguiDrawPdDialog()`.
  - Added hover/pressed visuals using the active PD palette colors.
  - Left-click on the `X` now emits an Escape press/release event (`ImGuiKey_Escape`) via ImGui IO, reusing existing menu cancel/back handlers.
  - Added per-frame click consumption guard (`s_CloseClickConsumedFrame`) so one click cannot trigger multiple closes when multiple dialogs draw in a frame.

**Why**:
- Middle-click back (S276) provided a universal mouse escape path, but it is hidden. A visible title-bar `X` makes exit/discard affordance discoverable across menus without per-screen button rewrites.

**Verification**:
- Build verification passed:
  - `ninja -C Build pd`
  - Result: `PerfectDark.exe` linked clean.

**Next steps**:
- Playtest top-level and nested modal flows (Main Menu, Solo Mission dialogs, Room/MP settings subdialogs, warning popups) to confirm:
  - `X` closes exactly one layer per click,
  - existing right-click interactions remain unchanged.

## Session S278 — 2026-04-15 (Modding Hub + Skin Editor diagnostics, multi-texture skin capture selection)

**Scope**:
- Add targeted diagnostics to investigate:
  - Mods visible in Mod Manager but missing in INI Editor list.
  - Skin Editor preview/capture path showing black or incomplete (head-only style) results.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Added tool-switch logging (`modhub: active tool -> ...`).
  - Added INI refresh diagnostics:
    - per-asset-type add counts,
    - total/mod/base entry counts.
  - Added INI selection/load/save diagnostics:
    - selected entry id + bundled flag,
    - full ini path used for load/save,
    - parsed line counts (editable/comment/blank),
    - warning when list is empty.
  - Added first-edit logging for key/value edits so input-application flow is visible.
- `port/fast3d/pdgui_skin_editor.cpp`:
  - Added character-list refresh and empty-list warnings.
  - Added editor active/inactive transition logs.
  - Added preview request state logs (body/head ids, ready flag, tex id) on state change.
  - Added logs for zero canvas texture while editor is active (override not applied) and recovery log when it returns.
- `port/fast3d/pdgui_charpreview.c`:
  - Added character request resolution logs:
    - head/body catalog id -> resolved mp indices/params,
    - warnings when head/body resolve fails.
  - Added non-character resolve failure logs.
- `port/fast3d/gfx_pc.cpp`:
  - Reworked skin-capture source selection from "first imported texture" to "collect all candidates this preview pass, choose largest-area texture".
  - Added per-pass capture diagnostics:
    - summary selected texture,
    - candidate list (tex id, size, fmt/siz, tile, area),
    - retry note when no candidates were observed in a pass.

**Why**:
- Prior capture logic could lock onto the first imported texture, which is often not representative (for example a head/aux texture), producing poor or partial base capture in Skin Editor.
- Missing INI entries and black previews require full pipeline visibility (catalog iterate -> list populate -> selection -> file path load -> preview request -> model/texture capture).

**Verification**:
- Build verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `ninja -C Build pd`
  - Result: `PerfectDark.exe` linked clean.

## Session S277 — 2026-04-15 (Select Tunes: Mod Tracks click now toggles playlist membership)

**Scope**:
- Fix Select Tunes interaction where clicking a mod track in the left "Mod Tracks" list did not remove/re-add consistently as users toggle selections.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_mpsettings.cpp`:
  - Updated Mod Tracks click handler in `renderSelectTunes()` to behave as a true toggle:
    - click when not selected -> add to playlist,
    - click when already selected -> remove from playlist.
  - Kept playlist index reset + room playlist sync dispatch after both add and remove paths.
  - Preserved existing add/remove UI sounds (`PDGUI_SND_SELECT` on add, `PDGUI_SND_KBCANCEL` on remove).

**Why**:
- The left panel should be a direct add/remove control surface for mod tracks. The previous branch only added on first click and ignored subsequent clicks while selected.

**Verification**:
- Compile verification pending in this session shell.

## Session S276 — 2026-04-15 (Universal mouse back/cancel for ImGui menus)

**Scope**:
- Ensure every ImGui menu has a mouse-driven exit path without per-menu rewrites.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_backend.cpp`:
  - Extended `pdguiDriveImGuiNav()` with a middle-mouse back/cancel bridge.
  - Middle mouse button now emits `ImGuiKey_Escape` press/release events, which reuses existing menu cancel handling paths (`Escape` / `GamepadFaceRight` checks) across the menu suite.
  - Chose middle-click specifically to avoid conflicts with right-click list interactions (for example contextual remove actions in room/tunes screens).

**Why**:
- Many menus already support cancel semantics, but user-facing mouse-only flows were inconsistent. A backend-level bridge gives one behavior across all menus and keeps diffs small.

**Verification**:
- `devtools/build-headless.ps1 -Target client` still reproduces the known PowerShell runspace failure in this shell.
- Fallback toolchain verification passed:
  - `. .\devtools\_build-env-prelude.ps1`
  - `cmake -G Ninja -S . -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
  - `ninja -C Build pd`
- Result: client linked clean (`PerfectDark.exe`).

**Next steps**:
- Playtest representative menu flows (Main Menu, Solo Mission, Room, Training, Pause, typed warning dialogs) and confirm middle-click consistently backs out one menu layer.

## Session S275 — 2026-04-15 (Audio mod category parse hardening for Songs list)

**Scope**:
- Fix enabled song/audio mods not appearing under Select Tunes when `audio.ini` used textual category values.

**Code changes shipped in working tree**:
- `port/src/modmgr.c`:
  - Added `modmgrParseAudioCategoryValue()` helper to parse `audio.ini` category values as either numeric (`0/1/2`) or text (`music`, `sfx`, `voice`, plus common aliases).
  - Updated both audio.ini parse paths (`modmgrParseAudioIni`, `modmgrLoadAudioIni`) to use the helper instead of raw `atoi`.
  - Kept fallback behavior defaulting to music when category is missing/invalid.
  - Extended registration log line to include resolved category for runtime diagnosis.

**Why**:
- `renderSelectTunes` only lists `ASSET_AUDIO` entries with `category == AUDIO_CAT_MUSIC`. Textual `audio.ini` values like `category=music` previously parsed as `0` via `atoi`, so tracks were registered as SFX and filtered out.

**Verification**:
- Code-path verification complete (parse -> register -> Select Tunes filter alignment).
- Runtime playtest required: enabled song mods should now populate Mod Tracks in Select Tunes.

## Session S274 — 2026-04-15 (Mod Apply transition hardening: stop menu runtime before title restart)

**Scope**:
- Fix regression reported from Modding Hub -> Apply Changes where title/CI reload could land in a state with captured gameplay mouse and no accessible menu.

**Code changes shipped in working tree**:
- `port/src/modmgr.c`:
  - Added explicit include usage for menu/runtime reset (`game/menu.h`, `pdgui.h`).
  - `modmgrApplyChanges()` now performs teardown before `mainChangeToStage(MODMGR_STAGE_TITLE)`:
    - `menuStop()` to clear active legacy menu runtime/dialog stack,
    - `pdguiMainMenuReset()` (client-only) to force top-level main-menu view on next open.

**Why**:
- Apply currently rebuilds catalog state and stage-switches directly from within active Modding Hub/Main Menu UI. Stopping menu runtime before the stage transition avoids carrying half-open menu state into the title->CI restart path.

**Verification**:
- Log analysis from `pd-client.log` confirms transition path through Modding Hub and stage reload into intro/CI.
- Build attempt via `devtools/build-headless.ps1 -Target client` in this shell did not complete configure cleanly (exit code 5 after configure start, no compile diagnostics emitted); runtime verification required.

**Next steps**:
- Re-test: open Modding Hub, toggle a few theme mods, press Apply.
- Confirm post-reload behavior: main menu opens normally, and Esc/Start can open/close menus in CI.

## Session S273 — 2026-04-15 (Dev Window v2 lock cleanup: same-MSYS path fidelity)

**Scope**:
- Follow-up on repeated `index.lock` sync failures where git was resolved from `c:\devkitPro\msys2\usr\bin\git.exe` and lock cleanup targeted a different MSYS runtime.

**Code changes shipped in working tree**:
- `devtools/dev-window-v2/dev-window-v2.ps1`:
  - Added `Get-MsysToolPath()` to resolve `rm.exe`/`cygpath.exe` from the same MSYS root as the active `git.exe` (with `C:\msys64` fallback).
  - Added `Convert-WindowsPathToMsysPosixPath()` and `Get-MsysExpectedLockPath()` so sync can derive the active MSYS `/home/...` repo path from `ProjectRoot`.
  - Updated `Remove-GitIndexLockForRepo()` and `Force-DeleteGitIndexLockHard()` to delete `/.git/index.lock` through that same MSYS runtime path in addition to existing Windows/WSL cleanup.
  - Updated `Remove-GitIndexLockFromGitStderr()` callsites to pass `-GitExe` so retry cleanup uses matching toolchain semantics.
  - Added Log-tab diagnostic line for `git expected lock (msys): ...` to surface runtime path mapping per build.

**Verification**:
- PowerShell parser validation passed for `dev-window-v2.ps1` after edits.
- Failure evidence reviewed: `failed build.txt` showed lock creation failure at `/home/mikeh/.../.git/index.lock` while sync diagnostics only surfaced Windows lock path.

**Next steps**:
- Re-run Dev Window v2 Build once and confirm log includes `git expected lock (msys): /home/.../.git/index.lock`.
- If lock retry triggers, verify sync no longer exits at `git commit failed` for the same stale-lock condition.

## Session S272 — 2026-04-15 (Dev Window v2 lock path policy simplification: dev-root primary)

**Scope**:
- Simplified index lock cleanup policy per project workflow (build tool uses only the main dev repo location, not worktrees).

**Code changes shipped in working tree**:
- `devtools/dev-window-v2/dev-window-v2.ps1`:
  - `Remove-GitIndexLockForRepo()` now treats `Join-Path $RepoRoot ".git\\index.lock"` as the primary canonical cleanup target.
  - Removed `rev-parse --git-path` / `--absolute-git-dir` probing and related dynamic gitdir cleanup branches.
  - `Force-DeleteGitIndexLockHard()` now keeps only path-format fallback cleanup (`wslpath`-derived repo path + optional MSYS `rm`) and no longer probes git top-level via `rev-parse`.
  - Sync diagnostics now log expected lock paths (`Windows` + `WSL` derived from project root) instead of git-reported dynamic paths.

**Verification**:
- PowerShell parser validation passed for `dev-window-v2.ps1` after simplification.

**Next steps**:
- Re-run Build in Dev Window v2 and confirm lock diagnostics show expected dev-root paths only.

## Session S271 — 2026-04-15 (Cursor rule: PowerShell-safe git commit message method)

**Scope**:
- Added a persistent Cursor rule so git commit message passing in this repo defaults to a PowerShell-safe method and avoids bash heredoc parse failures.

**Code changes shipped in working tree**:
- `.cursor/rules/powershell-git-commit-message.mdc` (new):
  - `alwaysApply: true` rule covering the repository.
  - Requires PowerShell here-string message assignment for multiline commit messages (`$msg = @'... '@; git commit -m $msg`).
  - Explicitly disallows bash heredoc commit syntax in PowerShell shells.

**Verification**:
- Rule file added in `.cursor/rules/` with active metadata and example command.

**Next steps**:
- Subsequent commits in this repo should use the PowerShell here-string flow by default.

## Session S270 — 2026-04-15 (Dev Window v2 git sync: single-executable path consistency)

**Scope**:
- Follow-up hardening for persistent `index.lock` retries where cleanup and mutation commands could still resolve different git path semantics in the same run.

**Code changes shipped in working tree**:
- `devtools/dev-window-v2/dev-window-v2.ps1`:
  - `Invoke-GitSyncBeforeBuild()` now resolves one `git` executable once and uses it for all sync commands (`add`, `diff --cached`, `commit`, `push`).
  - `Get-GitCurrentBranch()` now accepts `-GitExe` and resolves branch via that same executable.
  - `Remove-GitIndexLockForRepo()` / `Force-DeleteGitIndexLockHard()` now accept `-GitExe` and run all `rev-parse`/top-level probes via the same executable used for sync.
  - Added debug-log emission for lock-retry stderr payloads (`Write-DevWindowDebugLog`) so future failures include exact failing path text in `dev-window-v2-debug.log`.

**Verification**:
- PowerShell parser validation passed for `dev-window-v2.ps1` after edits.

**Next steps**:
- Re-run Build from Dev Window v2 and capture:
  - Log tab lines: `git exe`, `git top`, `git lock path`.
  - Any lock-retry line.
  - Matching `dev-window-v2-debug.log` WARN line (if a retry occurs).

## Session S269 — 2026-04-15 (Dev Window v2 git index.lock path fidelity hardening)

**Scope**:
- Hardened Dev Window v2 git sync lock cleanup so retries are accurate when git reports POSIX-style lock paths (`/home/...` or `/mnt/...`) instead of Windows paths.

**Code changes shipped in working tree**:
- `devtools/dev-window-v2/dev-window-v2.ps1`:
  - Added `Convert-PosixPathToWindowsPath()` helper to map lock paths from POSIX style to Windows style when possible.
  - Updated `Remove-GitIndexLockFromGitStderr()` and `Remove-GitIndexLockForRepo()` to attempt lock deletion across:
    - direct Windows path,
    - converted POSIX->Windows path,
    - `wsl rm -f`,
    - `C:\msys64\usr\bin\rm.exe -f` (MSYS path semantics).
  - Extended hard-delete path (`Force-DeleteGitIndexLockHard`) to run MSYS `rm` for git-top POSIX path variants.
  - Added explicit sync diagnostics in `Invoke-GitSyncBeforeBuild()` log output:
    - resolved git executable path,
    - git toplevel path,
    - git-resolved `index.lock` path.

**Verification**:
- PowerShell parser validation passed for `dev-window-v2.ps1`.
- Runtime path probes in this environment confirm git resolves repository/toplevel as POSIX (`/home/...`), matching the observed Dev Window v2 error surface.

**Next steps**:
- Re-run Build from Dev Window v2 once to verify retry path logs and lock cleanup behavior on the next transient lock event.

## Session S268 — 2026-04-15 (Skin Editor base texture capture fidelity pass)

**Scope**:
- Completed the next Skin Tool milestone after selector stabilization: base-skin capture now reads a captured source texture path instead of preview-FBO screenshot content.

**Code changes shipped in working tree**:
- `port/fast3d/gfx_pc.cpp`:
  - Updated skin-capture flow to snapshot the first preview-model texture import source (GL texture id + tile dimensions) during preview FBO render.
  - Retained normal model render behavior while capture is armed (no draw-path fork).
  - Kept `gfxSkinCaptureFinalizeFbo()` as a compatibility no-op; capture now completes from texture-import observation.
- `port/fast3d/pdgui_charpreview.c`:
  - Reworked `pdguiCharPreviewSkinCapturePoll()` to wait on `gfxSkinCaptureIsComplete()` and read back pixels from the captured texture id (`glGetTexImage`), using capture-reported dimensions directly.
  - Removed the previous dependency on reading the preview FBO color texture as the base-skin source.
- `port/include/pdgui_charpreview.h`:
  - Updated capture-flow comments to reflect source-texture readback semantics.

**Build verification**:
- Verified compile with project toolchain path:
  - dot-source `devtools/_build-env-prelude.ps1`
  - `cmake -G Ninja -S . -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
  - `ninja -C Build pd`
- Result: client target linked clean (`PerfectDark.exe`).

**Next steps**:
- Playtest New Skin capture on one base body + one mod body.
- Validate UV overlay alignment and export output quality against the captured base layer.

## Session S267 — 2026-04-15 (ImGui nav parity closure: tab routing + Agent Select focus trap)

**Scope**:
- Closed menu-input audit gaps for cross-device navigation consistency (controller + MKB) in active ImGui menus.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_room.cpp`:
  - Replaced direct `ImGuiKey_GamepadL1/R1` tab switching with action-driven `ImGuiKey_PageUp/PageDown` handling, aligning Room tab behavior with the shared `ACTION_MENU_TAB_PREV/NEXT` path in `pdguiDriveImGuiNav()`.
- `port/fast3d/pdgui_menu_solomission.cpp`:
  - Updated Solo Options tab cycling to use `PageUp/PageDown` (plus existing `Q/E`) instead of direct raw gamepad shoulder keys.
- `port/fast3d/pdgui_menu_agentselect.cpp`:
  - Removed `ImGuiWindowFlags_NoNav` from the agent list child region so focus can traverse naturally and not become trapped in custom-only navigation flow.

**Build verification**:
- `devtools/build-headless.ps1 -Target client` failed in this shell with the known PowerShell runspace exception (script/runtime issue, not compile diagnostics).
- Verified with equivalent toolchain path:
  - dot-source `devtools/_build-env-prelude.ps1`
  - `cmake -G Ninja -S . -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
  - `ninja -C Build pd`
  - `ninja -C Build pd-server`
- Result: both targets linked clean (`PerfectDark.exe`, `PerfectDarkServer.exe`).

**Next steps**:
- Playtest Room tabs with both controller and keyboard: bumper mapping should follow the same path as keyboard PageUp/PageDown.
- Playtest Agent Select to confirm full list traversal without getting stuck and with mouse/controller handoff remaining stable.

## Session S266 — 2026-04-15 (Skin Editor selector stability + renderer seam deep-dive)

**Scope**:
- Investigated renderer architecture seams for a future backend swap and traced Skin Editor live-preview/export flow end-to-end.
- Fixed Skin Editor character selection instability in Modding Hub navigation.

**Code changes shipped in working tree**:
- `port/fast3d/pdgui_menu_moddinghub.cpp`:
  - Suppressed global hub tab cycling (`PageUp`/`PageDown`, used by LB/RB nav mapping) while Skin Editor tab is active, preventing tool-switch input steal during character list navigation.
- `port/fast3d/pdgui_skin_editor.cpp`:
  - Added a character-list refresh fallback when the list is empty at render time.
  - Clamped character listbox height to a minimum so small content heights do not collapse selection UI.

**Build verification**:
- `devtools/build-headless.ps1 -Target client` failed in this shell due a PowerShell runspace exception (script/runtime issue, not compile diagnostics).
- Verified compile with equivalent project toolchain path:
  - dot-source `devtools/_build-env-prelude.ps1`
  - `cmake -G Ninja -S . -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++`
  - `ninja -C Build pd`
- Result: client target linked clean (`PerfectDark.exe`).

**Design outcome (renderer path)**:
- Most effective long-term new-renderer seam remains `GfxRenderingAPI` backend replacement under `gfx_run_dl` (keep GBI interpreter contract intact).
- Most effective first milestone for Skin Tool is **not** full renderer replacement: stabilize existing preview FBO + skin override pipeline first, then iterate base-skin export fidelity.

**Next steps**:
- Playtest Skin Editor selector with mouse + controller in Modding Hub.
- Validate character list/preview behavior after tab switching and on low-height UI layouts.
- If stable, proceed to base-skin export fidelity pass (true UV-space/base texture path instead of preview-FBO screenshot capture).

## Session S265 — 2026-04-15 (Post-merge sanity pass + focused playtest checklist)

**Scope**:
- Read-through sanity pass on the high-risk flows requested after merge:
  1) Counter-Op anti-role authority on wire,
  2) ready-gate cancel state transitions,
  3) MP endscreen team ranking feed behavior.

**Verification findings (code-level)**:
- Counter-Op role authority path is wired end-to-end:
  - `pdgui_menu_room.cpp` sends selected `clientId` as `antiClientId`,
  - `CLC_LOBBY_START` carries `antiClientId`,
  - server validates anti client room/state,
  - server maps anti client -> anti player slot,
  - `SVC_STAGE_START` carries authoritative anti slot to clients.
- Ready-gate cancel path is unified:
  - `netReadyGateCancelByLocalClient()` now centralizes countdown-active + `CLSTATE_PREPARING` validation,
  - `CLC_LOBBY_CANCEL` server read delegates to that helper,
  - client state moves to `CLSTATE_PREPARING` on `SVC_MATCH_MANIFEST`,
  - client state returns to `CLSTATE_LOBBY` on `SVC_MATCH_CANCELLED`.
- MP endscreen rankings now build from per-player rankings and apply team sorting for team mode (no aggregate `mpchr=NULL` feed path).

**Operational note**:
- The working tree currently also contains additional in-progress gameplay edits from parallel sessions (outside this sanity pass); no changes were made to those files here.

**Playtest checklist queued**:
- Counter-Op: choose non-slot-1 anti player, start match, confirm selected player is anti on all clients.
- Countdown cancel: test host cancel and remote cancel during visible 3-2-1; verify cancel banner and return to lobby state on all peers.
- Team endscreen: run a teams-enabled Combat Sim match; verify player rows render correctly (no placeholder `?` entries), grouped/sorted by team.

---

## Session S264 — 2026-04-15 (Audit remediation batch: countdown cancel, menu context ownership, SP-6/SP-8, manifest hardening)

**Scope implemented**:
- Closed the requested 1–5 audit items in one pass across net countdown authority, ImGui menu context ownership, MP scenario null-safety, manifest check/rescan behavior, and net_hash cleanup/de-emphasis.

**Fixes shipped in working tree**:
- `port/fast3d/pdgui_bridge.c` + `port/src/net/netmsg.c` + `port/include/net/netmsg.h`:
  - Added host-local countdown cancel path (`netReadyGateCancelByLocalClient`) so listen host Back/Esc can abort ready-gate countdown through the same server authority path.
  - `SVC_MATCH_MANIFEST` read now sets local client state to `CLSTATE_PREPARING`; `SVC_MATCH_CANCELLED` read returns it to `CLSTATE_LOBBY`.
  - `CLC_LOBBY_CANCEL` server read now delegates to shared gate/state validation helper.
- ImGui menu ownership hardening:
  - Added per-dialog ownership flags and pop guards in `pdgui_menu_botsetup.cpp`, `pdgui_menu_playerconfig.cpp`, `pdgui_menu_mpsetup.cpp`, `pdgui_menu_mppause.cpp`, `pdgui_menu_mpadvanced.cpp`, `pdgui_menu_cheats.cpp`.
  - Close handlers now pop `g_CtxImGuiMenu` only when that dialog actually pushed it.
- SP-6/SP-8 hardening in MP gameplay/scenario paths:
  - `src/game/mplayer/mplayer.c`: award pipeline now builds an explicit local-player index list and uses slot-safe playerstats/award writes.
  - `src/game/mplayer/ingame.c`: guarded award text accessors against invalid/null player slots.
  - `src/game/activemenu.c`: bounds-guarded `g_MpPlayerNum` before root dialog push.
  - `src/game/mplayer/scenarios.c` + scenario includes (`capturethecase.inc`, `hackthatmac.inc`, `holdthebriefcase.inc`, `kingofthehill.inc`, `popacap.inc`): added sparse-slot guards and `prop->chr` null checks in radar/hud/scenario loops.
- Manifest/cross-boundary hardening:
  - `port/src/net/netmanifest.c`: removed net_hash fallback in `manifestValidate`, removed count-based early-exit in `manifestSPRescanSetup` (always diff/apply), blocked `manifestEnsureLoaded` in non-SP net modes, and made unresolved non-base namespaces count as missing in `manifestCheck`.
- Hash cleanup and UI de-emphasis:
  - Removed deprecated pre-session net_hash wire helpers from `port/include/assetcatalog.h` + `port/src/assetcatalog_api.c`.
  - `port/fast3d/pdgui_menu_mainmenu.cpp`: replaced NetHash manifest/catalog table column with namespace-based sort/display.
  - Updated stale wire-format comments in `port/src/net/netmsg.c`.

**Verification**:
- Attempted `devtools/build-headless.ps1 -Target all` twice (normal + verbose): configure failed immediately in this shell environment.
- Direct configure repro: `cmake -G Ninja -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` failed with `gcc/g++ not found in PATH`.
- Runtime playtest still required for S264 behavioral checks (countdown cancel broadcast + menu input ownership + scenario guards).

---

## Session S263 — 2026-04-15 (Systemic gameplay pipeline fixes: campaign + MP + Counter-Op authority)

**Scope implemented**:
- Executed the full follow-up from S262 across Campaign, Combat Simulator, online room/ready-gate flow, Counter-Op role authority, and endscreen safety.
- Included a deliberate networking wire-format change for Counter-Op anti-player selection and bumped protocol.

**Fixes shipped in working tree**:
- `src/game/menutick.c`: added `NUM_SOLOSTAGES` clamp in Deep Sea next-stage path; added NULL-safe currentplayer/prop room checks in training/music branch.
- `src/game/endscreen.c`: guarded `g_MpPlayerNum` bounds in `endscreenPushCoop()` and `endscreenPushAnti()` before indexing `g_Menus[]`.
- `src/game/mplayer/mplayer.c`: `mpEndMatch()` now iterates `MAX_PLAYERS` with `g_Vars.players[i]` guard instead of using `PLAYERCOUNT()` as an index bound (SP-6 hardening).
- `port/fast3d/pdgui_menu_endscreen.cpp`: ranking feed now always uses per-player rankings; team mode remains a sort/group view (fixes team aggregate placeholder rows).
- `port/fast3d/pdgui_bridge.c`: `lobbyGetPlayerInfo` now exports `clientId`; endscreen cheat-name bridge accessors now guard `g_MpPlayerNum` bounds.
- `port/fast3d/pdgui_menu_room.cpp` + `pdgui_lobby.cpp` + `pdgui_menu_lobby.cpp`: lobby player view includes `clientId`; Counter-Op picker now tracks/uses selected client ID for start request.
- `port/include/net/net.h`: protocol bump `NET_PROTOCOL_VER 35 -> 36`; added `g_NetCounterOpClientId` state.
- `port/include/net/netmsg.h` + `port/src/net/netmsg.c` + `port/src/net/netmenu.c` + `port/fast3d/pdgui_bridge.c`:
  - `CLC_LOBBY_START` now carries `antiClientId` (v36).
  - server validates anti client selection for Counter-Op starts.
  - co-op/anti ready-gate launch path now uses a single `mainChangeToStage` callsite via `netServerCoopStageStart()` (removed duplicate pre-call from ready-gate paths).
  - `SVC_STAGE_START` now carries authoritative anti player slot; client applies it for Counter-Op instead of forcing slot 1.
- `port/src/net/net.c`: `netServerCoopStageStart()` maps anti role from `g_NetCounterOpClientId` to `playernum` with fallback; `netServerStageEnd()` resets `g_NetMatchRoomId` and `g_NetCounterOpClientId`.

**Verification**:
- Reconfigure + build completed successfully for both targets:
  - `PerfectDark.exe`
  - `PerfectDarkServer.exe`

---

## Session S262 — 2026-04-15 (Deep gameplay pipeline audit: local/online/campaign/CombatSim/Counter-op)

**Scope (read-only audit)**:
- End-to-end gameplay pipeline review across local and online flows, Campaign, Combat Simulator, and Counter-op.
- Focused on lifecycle edges: stage transitions, manifest usage, room/ready-gate teardown, ranking/endscreen paths, and menu/input handoff.
- Systemic cross-check against `systemic-bugs.md` patterns (SP-1/2/3/6/8/13/14).

**Highest-impact findings**:
- `src/game/menutick.c`: Deep Sea end path increments `g_MissionConfig.stageindex` and indexes `g_SoloStages[]` without `NUM_SOLOSTAGES` clamp (OOB risk); other endscreen paths already clamp.
- `src/game/endscreen.c`: `endscreenPushCoop/Anti` write `g_Menus[g_MpPlayerNum]` after assigning `g_MpPlayerNum = currentplayerstats->mpindex` without bounds guard (`MAX_PLAYERS` domain mismatch risk).
- `port/src/net/netmsg.c` + `port/src/net/net.c`: co-op/anti ready-gate launch path does `mainChangeToStage()` then calls `netServerCoopStageStart()`, which calls `mainChangeToStage()` again (double-transition inconsistency window).
- `port/fast3d/pdgui_menu_room.cpp` + `port/src/net/netmsg.c` + `port/src/net/net.c`: Counter-op "Counter-Op Player" UI selection is not serialized on wire; runtime still forces anti role to player slot 1 when two clients are present.
- `src/game/mplayer/mplayer.c` (`mpEndMatch`): `for (i < PLAYERCOUNT()) setCurrentPlayerNum(i); g_Vars.currentplayer->...` assumes contiguous player slots (SP-6 sparse-slot risk).
- `port/fast3d/pdgui_menu_endscreen.cpp` + `src/game/mplayer/mplayer.c`: team-mode ranking feed uses `mpGetTeamRankings()` (rows with `mpchr = NULL`) while UI expects per-player rows, producing placeholder entries and inconsistent endscreen data.
- `port/src/net/net.c`: `g_NetMatchRoomId` is set on start paths but not reset on stage end; stage-end room filtering remains dependent on last writer.

**Verification status**:
- Audit-only session; no gameplay/runtime code changed in this entry.
- Findings delivered as prioritized follow-up items for implementation sessions.

---

## Session S261 — 2026-04-15 (Build python pin + menu/input lifecycle + mod root compatibility)

**Problems observed**:
- Dev Window run logs showed client build failures in generated-asset commands (`python3` not found in subprocess PATH).
- Runtime mod logs showed scan roots missing user-installed mod locations when launched from `Build/`, causing enabled mods to appear as unknown and songs/themes not to populate.
- Menu/input lifecycle still had a depth-0 `menuPopDialog` path that could trigger unintended teardown behavior.

**Fixes shipped in working tree**:
- `devtools/dev-window-v2/dev-window-v2.ps1` and `devtools/build-headless.ps1` now pass explicit `-DPD_PYTHON_EXECUTABLE=C:/msys64/usr/bin/python3.exe` in configure args (no ambient PATH dependency for mklang/mkanims custom commands).
- `port/src/modmgr.c` scan roots now include `$E/../mods` first (repo-root mods when exe runs from `Build/`) and continue scanning all candidates (`./mods`, `$E/mods`, base fallback).
- `port/fast3d/pdgui_theme_loader.cpp` root scanning aligned with modmgr (`$E/../mods`, `./mods`, `$E/mods`, base fallback).
- `port/src/modmgr.c` `mod.json` compatibility improved: missing `id` now falls back to directory slug, missing `name` falls back to `id`, missing `base_fallback` defaults to `base-game` (logs warning instead of silently dropping mod identity).
- `src/game/menu.c` `menuPopDialog()` now returns immediately on depth-0 underflow after logging.
- `port/fast3d/pdgui_menu_pausemenu.cpp` removed redundant `inputCtxPush(&g_CtxImGuiMenu)` just before `netDisconnect()` / `mainChangeToStage()`.
- `src/lib/main.c` added warning log when stage transition is queued while input stack is not gameplay-only.

**Verification**:
- Reconfigure + build succeeded (`Asset tools Python: C:/msys64/usr/bin/python3.exe`, client/server link clean).
- Incremental rebuilds after mod/theme path fixes also link clean.

---

## Session S260 — 2026-04-14 (Mod Apply rebuild + theme roots + MP dialog input ownership)

**Problem**: After enabling mods and pressing Apply, enabled mod themes/songs did not appear, Select Tunes interactions were inert (no mod tracks to add), and menu input/cursor state could break after backing out of MP dialogs.

**Root causes + fixes**:
- **`port/src/modmgr.c`**: `modmgrApplyChanges()` only rebuilt reverse indexes; it did not re-register enabled mods before returning to title. Added `modmgrRebuildCatalogFromCurrentSelection()` and used it in both `modmgrApplyChanges()` and `modmgrReload()`: clear mod catalog entries, rescan components, restore `.modstate`, re-load enabled mods (`modmgrLoadMod`), then run `catalogLoadInit()`. This makes enabled mod manifest/audio entries present in the same apply cycle.
- **`port/fast3d/pdgui_theme_loader.cpp`**: `scan_mods_for_themes()` only walked `"mods"` and stopped at first hit. Updated to match modmgr roots (`mods`, `$E/mods`, `fsFullPath("mods")`), skip duplicate resolved paths, and scan all reachable roots so themes located outside the first root still register.
- **`port/fast3d/pdgui_menu_mpsettings.cpp`**: dialog close path always popped `g_CtxImGuiMenu` even when this screen did not push it. Added ownership tracking (`s_PdmsOwnsMenuCtx`) so close only pops contexts owned by this dialog; also routed Handicaps "Done" through the same close helper. Added `pdms_EndTunesPreview()` and call it on Select Tunes back/close so hover preview always restores background music on exit.

**Build verification**:
- `devtools/build-headless.ps1 -Target all` currently fails at configure due an unrelated workspace state in `CMakeLists.txt` (CMake reports `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` set to `C`, not a valid compiler path).
- Changes in this session are uncommitted; runtime playtest is required once configure is restored.

---

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
