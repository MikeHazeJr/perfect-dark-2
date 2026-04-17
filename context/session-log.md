
# Session Log (Active)

> **S241–S316** (rolling window). Older sessions **S240–S157** → [_archive/session-log-archive-S240-and-older.md](_archive/session-log-archive-S240-and-older.md). Ancient **S1–S119** → [_archive/sessions/].
> Navigation hub: [INDEX.md](INDEX.md) · Back to [README.md](README.md)

## Session S319 — 2026-04-17 (Spurious STAGE_DEFECTION boot transition fix — `nostalgic-lichterman-3c1259` worktree)

**Scope**: Root-cause the double-transition `STAGE_DEFECTION (0x30) → STAGE_CITRAINING (0x26)` logged on every cold boot. The crash guard added in S317 addendum was defense-in-depth; this session eliminates the bad transition at the source.

### Root cause

`titleInitRareLogo()` (`src/game/title.c:2025`) sets `g_IsTitleDemo = true` when `!g_IsTitleDemo && IS8MB()`. On PC, `IS8MB()` is compile-time `1` (see `constants.h:92`). This means **every cold boot** activates demo mode, which then routes through `titleInitSkip` with `g_TitleNextStage = STAGE_DEFECTION`, firing `mainChangeToStage(0x30)`. The `TITLEMODE_SKIP` tick immediately detects `g_IsTitleDemo` and overrides to CI Training, firing `mainChangeToStage(0x26)` — causing the "MAIN: replacing pending stage change 0x30 -> 0x26" warning and a brief double-load of the Defection stage manifest.

On N64 the demo was a real pre-recorded playback only available with the 8MB expansion pak. On PC there is no demo recording system, so the IS8MB() branch was vestigial dead code causing the bad transition every boot.

### Fix

Removed the `if (!g_IsTitleDemo && IS8MB()) { g_IsTitleDemo = true; }` block from `titleInitRareLogo()`. `g_IsTitleDemo` is now never set to `true` during boot. `titleInitSkip` routes directly to `STAGE_CITRAINING` without the intermediate DEFECTION transition.

### Commit

| SHA | Scope |
|-----|-------|
| `(this commit)` | **fix(title): remove IS8MB demo-init — eliminates spurious STAGE_DEFECTION on every cold boot** |

### Files touched

- `src/game/title.c:2025-2027` — removed IS8MB demo-activation block; replaced with comment

### Build verify

`ninja -C Build pd` — clean. PerfectDark.exe 52,530,547 (only pre-existing warnings).

---

## Session S316 — 2026-04-17 (ROM hash cache path fix + crash investigation — `nostalgic-lichterman-3c1259` worktree)

**Scope**: Urgent crash report — Mike's second PC crashes at startup (stage 0x26 CI Training bodiesReset) after removing `.sha256` files from the game folder.

### Investigation findings

1. **`.sha256` files are NOT read at runtime.** `PerfectDark.exe.sha256` / `PerfectDarkServer.exe.sha256` are release-only artifacts uploaded to GitHub for the updater's download-integrity verification. They are never opened during normal game operation. Removing them cannot cause any crash.

2. **`catalogCacheVerifyRom` path bug (fixed).** The function was calling `sha256HashFile(romPath, ...)` with the bare filename `g_RomName = "pd.ntsc-final.z64"` instead of the full filesystem path. `sha256HashFile` calls `fopen(path, "rb")` directly without `fsFullPath`, so it fails whenever the CWD is not the data directory (i.e., always when launched from the game folder). This caused `WARNING: CATALOG: ROM hash cache: could not hash 'pd.ntsc-final.z64'` on every boot. Fix: added `fsFullPath(romPath)` call before `sha256HashFile`.

3. **Actual crash (B-163 — open).** The AV at PC+0x161258 during CI Training setup could not be fully diagnosed — the crash log upload path was inaccessible and we have no binary for `addr2line`. The double-transition warning `MAIN: replacing pending stage change 0x30 -> 0x26` at boot is suspicious. The last catalog log `CATALOG: base:model_0194 (1879) → ROM` indicates the catalog resolve succeeded; crash is downstream in the model-load path. Most likely hypothesis: pre-S312 build (`aee52a8a`) without the modeldef defensive guards introduced in S312. Current HEAD should be safe — needs playtest verification on Mike's machine after update.

### Commits

| SHA | Scope |
|-----|-------|
| `(this commit)` | **fix(catalog): use fsFullPath in catalogCacheVerifyRom ROM hash path** — eliminates spurious "could not hash" WARNING on every boot when base dir has spaces or CWD ≠ data dir. |

### Files touched

- `port/src/assetcatalog_cache.c` — `sha256HashFile(romPath, ...)` → `sha256HashFile(fsFullPath(romPath), ...)`
- `context/bugs.md` — added B-163 (startup AV in CI Training bodiesReset, needs crash log)
- `context/session-log.md`, `context/tasks-current.md`, `context/README.md`

### Build verify

`ninja -C Build pd pd-server` — pending (to run before merge).

### Deferred

- **B-163 root cause** — need crash log from Mike's PC + `addr2line -e PerfectDark.exe 0x161258` on the `aee52a8a` binary. Verify whether crash reproduces on current HEAD.
- **Double-transition 0x30 → 0x26** — investigate what triggers stage 0x30 at boot and whether it interacts with the CI Training redirect. Could be benign (fast boot redirect) or load-state corruption.

---

## Session S315 — 2026-04-17 (Palette sweep completion — `pedantic-austin-4a93bc` worktree)

**Scope**: Verify and complete the four tasks from the S311/S312/S313 marathon that were either shipped already or had remaining gaps.

### Status: all four tasks shipped

1. **In-world glyph prompts** — Verified complete from S311 pt3. `pdgui_interact_prompt.cpp` reads `g_InteractProp` each frame, renders `[KEY] Pick up / Open / Access` pills via `pdguiDrawActionPromptCentered(ACTION_USE, ...)`. `propInteractPromptLabel()` in `prop.c` classifies weapon pickup / door / terminal / generic. Wired into `pdgui_backend.cpp:607`. `g_InteractProp` set in `propobj.c` (weapon pickup line 16420, door lines 20939/20942/20964).

2. **The Grid HUD glyph integration** — Verified complete from S311 pt3 + S313. `pdgui_forge_hud.cpp` uses 4-row `pdguiDrawActionPrompt` layout covering `ACTION_FORGE_TOGGLE`, `ACTION_FORGE_ASCEND`, `ACTION_FORGE_DESCEND`, `ACTION_FORGE_BOOST`, `ACTION_FORGE_PRECISION`, `ACTION_MENU_TAB_PREV/NEXT`, `ACTION_USE`, `ACTION_CANCEL_USE`. Background panel with TitleGlow border.

3. **Blue-tint palette sweep** — S311 pt2 swept `pdgui_lobby.cpp` / `pdgui_lobby_distrib.cpp` / `pdgui_skin_editor.cpp` / `pdgui_menu_{agentselect,modmgr,moddinghub,solomission}.cpp`. S315 caught 10 additional hardcoded cyan/blue literals: `pdgui_lobby_distrib.cpp:356`, `pdgui_menu_challenges.cpp:293`, `pdgui_menu_lobby.cpp` (3 sites), `pdgui_menu_pausemenu.cpp` (2 sites), `pdgui_menu_room.cpp` (3 sites) — all → `pdguiVec4TitleGlow()`. Remaining untouched intentionally: `pdgui_backend.cpp:471` (log-viewer LOAD category, functional debug color) + `pdgui_menu_solomission.cpp` difficulty colors (Agent/SA/PA canonical PD identity) + `pdgui_menu_agentselect.cpp:502` initials text (artistic). `server_gui.cpp` explicitly skipped — pd-server doesn't link `pdgui_style.cpp`.

4. **X-button close consistency** — Verified complete from S311 pt6. `pdguiConsumeTitleClose()` wired: main menu (line 3358), CI redirect/DeadPlayer2/CinemaList (lines 4011/4116/4211), theme editor (line 649), moddinghub (line 1451), pause menu (line 725), endscreen solo + MP (lines 738/1248).

### Commit

| SHA | Scope |
|-----|-------|
| `cd7a7e01` | **feat(S315): complete blue-tint palette sweep — 10 missed literals** |

### Build verify

`ninja -C Build pd pd-server` clean. PerfectDark.exe 52,534,547 / PerfectDarkServer.exe 22,837,961. Only pre-existing warnings.

---

## Session S313 marathon batch — 2026-04-17 (Grid rename completion + per-agent audio + pd.ini audit — `great-robinson-15f409` worktree, direct to dev)

**Scope**: Continuation of the three-session marathon (S311 + S312 + S313 all merged).  S313 owns this batch of targeted follow-ups on top of the merged dev.  Worktree FF-ed to origin/dev; commits land on the same branch and merge via `--no-ff`.

### Commits on `claude/great-robinson-15f409` (on top of dev at `d96d64cb`)

| SHA | Scope |
|-----|-------|
| `8057f064` | **feat(grid): rename `FORGE:` / `FORGE.*` log prefix → `GRID:` / `GRID.*`**.  Completes the user-facing rename from the main S313 drop.  All `sysLogPrintf` prefixes flipped across `src/game/forgemode.c`, `port/src/forge/{forge_core,forge_serialize,forge_gametype,forge_ai,forge_logic,forge_undo}.c`, `port/fast3d/pdgui_menu_forge.cpp`.  Internal symbol names (`forge_*`, `FORGE_MAX_*`, `FORGE_CAT_*`, `FORGE_MOD_*`, catalog namespaces, file paths) retained for code stability.  `pdgui_forge_hud.cpp` header comment reworded to describe the module as "The Grid HUD overlay" with an explicit note on the internal/external taxonomy split. |
| `1694d3ec` | **feat(prefs): audio volumes per-agent + agent-name from `gamefileGetOverview`**.  (a) `prefs_agent.c` now reads/writes an `[Audio]` block with `MasterVolume` / `MusicVolume` / `GameplayVolume` / `UIVolume`.  Setters `audioSet{Master,Music,Gameplay,Ui}Volume` applied on load; current globals read on save.  Global pd.ini keeps the same four keys as per-machine defaults.  (b) `prefsLoadForFile` in `pdgui_menu_agentselect.cpp` switches from raw `filelistfile::name[]` byte-walk to `gamefileGetOverview` so the sidecar path matches the Agent Select display text exactly -- prior code could produce mismatched filenames because the stored name is a variable-length char-code encoding. |
| `6aaf299f` | **feat(config): drop network-tuning + protected-folders from pd.ini**.  Removed `configRegister*` for `Net.LerpTicks`, `Net.Client.{InRate,OutRate,UpdateFrames}`, `Net.Server.{Port,InRate,OutRate,UpdateFrames}`, `Update.ProtectedFolders`.  Globals kept with file-scope initializers so runtime code compiles; `-port` CLI override for the server still works.  Retained: `Net.Client.LastJoinAddr`, `Net.RecentServer.*`, `Net.Server.AllowInfoQuery`, `Net.RecentServerCount`.  New doc `context/config-pd-ini-audit.md` catalogs every `configRegister*` call in the port and documents the three-tier model (pd.ini / per-agent / compile-time) with a decision flowchart for future settings. |
| `(next)` | **Merge commit** into dev via `git merge --no-ff` from main working copy. |

### Files touched

- **Renamed log prefix** (task 1): `src/game/forgemode.c`, `port/src/forge/{forge_core,forge_serialize,forge_gametype,forge_ai,forge_logic,forge_undo}.c`, `port/fast3d/pdgui_menu_forge.cpp`, `port/fast3d/pdgui_forge_hud.cpp` (header comment).
- **Per-agent audio + agent-name fix** (tasks 2 + 3): `port/src/prefs_agent.c`, `port/include/prefs_agent.h`, `port/fast3d/pdgui_menu_agentselect.cpp`.
- **pd.ini audit** (task 4): `port/src/net/net.c` (netConfigInit), `port/src/updater.c` (updaterConfigInit), `context/config-pd-ini-audit.md` (new doc).
- **Context** (task 5): `context/session-log.md`, `context/tasks-current.md`, `context/README.md`.

### Three-tier configuration model (landed)

1. **pd.ini** = per-machine (hardware, display, audio backend, network history, ops toggles, debug).
2. **saves/prefs_\<agent\>.ini** (prefs_agent.c) = per-agent (visuals + **audio volumes (NEW)** + mod enablement).  Overlays pd.ini on Agent Select load.
3. **Compile-time constants** = tuning knobs (**network rates (NEW)**, **server port (NEW)**, **protected folders (NEW)**) where user override would only cause confusion or break compatibility.

Net global initializers and `-port` CLI override continue to work.  UPDATER_DEFAULT_PROTECTED ("mods,data,extracted,saves") remains the source of truth for updater protections -- pd.ini is always protected regardless.

### Marathon wave -- cross-session summary (S311 + S312 + S313 all on dev)

| Session | Branch | Focus | Status |
|---------|--------|-------|--------|
| **S311** | zen-poitras-f86b73 | Theme palette sweep (35+ hardcoded blue tints → `pdgui{ImU32,Vec4}TitleGlow`/`TintSuccess/Danger/Info`), `pdguiRgbaToImU32` + semantic accessors, warning.cpp MP End Game inset fix, `mainmenu.cpp` delete-confirm button scaling, CS music picker re-verified, controller-nav bridge via `pdguiDriveImGuiNav`. | **Merged to dev (`d96d64cb`)** |
| **S312** | amazing-mccarthy | Modeldef defensive guards in `modeldefFindBboxNode` / `modelFindBboxNode` (NULL rootnode / parts ∉ [1,500] / scale ≤ 0 + walker step cap 10000), `manifestEnsureLoaded` late-add WARNING for torn body/head modeldefs, new `pdgui_glyphs.{h,cpp}` -- resolve `InputAction` → short-label `[KEY] Label` pills with KBM/gamepad auto-detect, net/input wire audit. | **Merged to dev (`85421a8a`)** |
| **S313** (bulk) | great-robinson-15f409 | The Grid polish: Forge→Grid rename (strings), Bots tab (live testing), Map Variant editing (`from_base` flag + delta API), weapon pad preview, controller nav (bumper-cycle tabs), save modal w/ deps list, placement ghost, grid snap viz. | **Merged to dev (`16cb6f7e`)** |
| **S313** (marathon follow-up) | great-robinson-15f409 (same) | Log-prefix FORGE→GRID; per-agent audio volumes; agent-name handoff via `gamefileGetOverview`; pd.ini cleanup -- drop network tuning rates + server port + protected folders. | **This commit set** (`8057f064` / `1694d3ec` / `6aaf299f`) |

### Build verify

`ninja -C Build pd pd-server` clean after each commit.  Worktree build: PerfectDark.exe ~52.3 MB / PerfectDarkServer.exe ~22.8 MB.  Only pre-existing warnings (collision `sum2`, modelasm dangling-pointer, `/*` within comment noise, snd strncpy truncation) -- no new warnings introduced.

### Deferred

- Agent-name `gamefileGetOverview` fix lands the sidecar-path alignment, but existing agents with old sidecars (prefs_<old_encoded>.ini) will effectively be orphaned.  A one-shot migration pass (rename old sidecar to new name on first load of each agent) is queued.
- Gameplay-preference pd.ini keys (`Game.CenterHUD`, `Game.SkipIntro`, `Game.DisableMpDeathMusic`, etc.) could migrate per-agent next -- see `config-pd-ini-audit.md` candidate list.
- `Audio.ModPlaylist` / `ModShuffle` / `ModTrackId` are per-machine today; per-agent migration would let each Agent have its own CS music selection.

---

## Session S312 batch 2 — 2026-04-17 (Font/UI scaling + controller tab-cycle + border enforcement audit — same worktree)

Post-S311/S312/S313 merge-clean sync: audit the menu-renderer layer
for gaps the three parallel sessions may have missed, now that the
file-ownership split is over.

### What landed

1. **Font scaling audit** — single hardcoded `260.0f` Input Mapping
   search-box width and `120` / `16` literals in Updates channel
   combo + SameLine offsets were the only miss.  Migrated through
   `pdguiScale()`.  Table cell/frame/item style vars at
   `pdgui_menu_mainmenu.cpp:1828-1830` wrapped with pdguiScale so
   the Input Mapping row density scales with DPI.  All
   SetWindowFontScale callers (endscreen / countdown / mpingame /
   lobby_distrib) already compose with pdguiScaleFactor correctly
   via the `sf / bigScale / fontSize/GetFontSize()` patterns.
2. **Controller-first UX audit** — Mod Manager tab bar
   (`pdgui_menu_modmgr.cpp`) was the last tab bar still missing
   LB/RB bumper cycling.  Added the same
   `s_*PendingTab + ImGuiTabItemFlags_SetSelected` pattern that
   Room / Settings / Cheats / Modding Hub / The Grid editor use.
   UI-order-to-internal-order lookup table keeps the bumper cycling
   in visual order (Installed / By Category / By Mod) even though
   the internal encoding is (2, 0, 1).  Verified all three
   right-click context menus (Color Theme picker, Mod row,
   Room bot slot) already have `IsKeyPressed(GamepadFaceLeft)`
   X-button parity.  All SetKeyboardFocusHere calls are gated on
   `s_NeedsFocus` — no focus traps.
3. **UI scaling pass** — swept BeginChild, SetCursorPos,
   PushItemWidth, SetColumnWidth, Indent, SameLine, PushStyleVar
   for hardcoded pixel values.  All existing callers use
   `pdguiScale(...)` or a `* scale` multiplier (sourced from
   `pdguiScaleFactor()`).  No additional fixes required beyond the
   font-scaling batch.
4. **Border enforcement verification** — per-file scan across all
   23 menu renderers that call `pdguiDrawPdDialog`.  Two apparent
   misses were false-positives: `pdgui_menu_pausemenu.cpp` uses
   `pdguiThemeGetContentInset` directly to compose custom padding
   (tab-button + bottom-pinned Resume-button layout), and
   `pdgui_menu_theme_editor.cpp` renders a mini-preview
   `pdguiDrawPdDialog` inside a BeginChild swatch that intentionally
   doesn't need full content-inset handling.  The S305 + S309
   sweeps covered the remaining 20 renderers properly.

### Files touched

- `port/fast3d/pdgui_menu_mainmenu.cpp` — Input Mapping search-box
  width + table cell/frame/item style var scaling
- `port/fast3d/pdgui_menu_update.cpp` — Channel combo width +
  SameLine offsets now scaled
- `port/fast3d/pdgui_menu_modmgr.cpp` — LB/RB bumper tab cycling
  (3-tab rotation with UI-order / internal-order mapping)

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both
targets link.  Only pre-existing warnings.

### Follow-up queued

- **Wire pdgui_glyphs into contextual prompts** (from S312 batch 1)
  — pickup / door interact / forge HUD controls reminder.
- **Font-atlas rebuild on runtime font swap** — S309's Font Mod
  requires restart because atlas is built once at `pdguiInit`.
  Future work: atlas hot-reload.
- **Theme Editor mini-preview content-inset** — currently the
  miniature `pdguiDrawPdDialog` at line 391 doesn't honor user
  chrome insets; it uses a fixed headerH.  Cosmetic-only; the
  preview is meant to be a compact swatch.

---

## Session S313 — 2026-04-17 (The Grid polish + missing features — `great-robinson-15f409` worktree → merged to dev)

**Scope**: S313 is the parallel Grid-owning session in the S311/S312/S313 three-way split. Picks up on top of S310's F1-F8 bulk drop to ship the remaining design-doc items plus targeted polish. File ownership per parallel prompt: **Grid code only** (`port/src/forge/*`, `src/game/forgemode.*`, `port/fast3d/pdgui_forge_*`, `port/include/pdgui_forge.h`). No menu / theme / gameplay-logic files touched.

### Commits on `claude/great-robinson-15f409`

| SHA | Scope |
|-----|-------|
| `da73273a` | **S313 The Grid polish + features (single commit)**. Six files, 727 insertions, 72 deletions. Rename `Forge → The Grid` in user-facing strings (HUD badge, editor window title, tab button labels); internal `forge_*` / `FORGE.*` log taxonomy retained. New `forge_bot_settings_t` + `forge_bot_spawn_mode_t` + Bots tab. Map variant editing (`forge_map_variant_mode_t` + `variant_source_slug` + `from_base` flag on objects + `forgeImportBaseStageObjects` / `forgeObjectResetToBase` / `forgeObjectRemoveFromBase` / `forgeCountBaseObjects` / `forgeCountDeltaObjects` API). Save-As-Mod confirmation modal listing mod dependencies before commit. Catalog click now *begins* a ghost placement; per-frame `pdguiForgeEditorTick` (now actually called from `mainTick`) updates ghost from freefly camera; HUD reticle with valid/invalid colour + `Place Here` / `Cancel (Esc)` banner in Catalog tab. Grid-snap visualization in HUD (top-center readout + 5x5 faint crosses). Controller navigation: keyboard nav idempotent, PageUp/PageDown + GamepadL1/R1 bumper-cycle tabs with persistent tab index; gamepad nav flag deliberately left off to avoid conflicting with freefly camera stick input. Tab bar `FittingPolicyScroll`. Case-insensitive catalog search. Weapon pad Properties pane: live `Preview:` showing which weapon-source wins for this pad at runtime + Respawn Effect combo. Expanded default editor size 560→620px. |
| `16cb6f7e` | **Merge to dev** (`--no-ff`, main working copy).  Pre/post-merge line counts verified identical: 1528 / 238 / 979 / 1420 / 970 / 838 = 5973 total. |

### Files touched (summary)

- **Modified** (6): `port/fast3d/pdgui_forge_editor.cpp` (+~400 lines: Bots tab, variant UI, save modal, tab navigation, placement banner, weapon pad preview, case-insensitive search), `port/fast3d/pdgui_forge_hud.cpp` (+~60: ghost reticle, grid-snap indicator, controller hint footer, "THE GRID" rename), `port/include/forge/forge_core.h` (+~65: new enums + struct fields + API decls), `port/src/forge/forge_core.c` (+~130: bot settings impl + variant helpers + from_base defaults), `port/src/forge/forge_serialize.c` (+48: bot_testing block + variant metadata + from_base persistence), `port/src/pdmain.c` (+7: pdgui_forge.h include + `pdguiForgeEditorTick()` call in mainTick).

### Phase coverage against the design doc

- **§3.1 / §4.1 Catalog + placement**: ghost-preview now actually appears -- user clicks a catalog entry, the editor tick advances the ghost each frame from camera forward-400u, HUD draws the reticle with valid/invalid tint, and the author commits via Place Here / Enter or aborts via Cancel / Escape.  Closes the "immediate commit" usability gap from S310.
- **§6 Live testing**: new Bots tab surfaces add/remove/freeze controls + Spawn Near Me (radius-bound) + Spawn Smart (aggression slider).  Runtime engine hook is queued; today captures intent + logs `FORGE.BOT:`.
- **§10.2 base_stage**: Variant editing UI replaces the single Base Stage ID input with a Variant combo (New Empty / Edit Stage / Edit Map) + dependent fields.  Objects tagged `from_base` render with `[BASE]` badge in Properties and gain Reset-to-Base / Remove-Base-Object delta actions.
- **Controller nav** (design hint throughout): keyboard PageUp/PageDown + gamepad L1/R1 cycle tabs with persistent state; controller hints in HUD + editor footer; gamepad nav intentionally off (would fight freefly sticks).
- **§10.5 Network sharing**: Save As Mod now opens a modal listing exact mod dependencies before committing, so the author sees the bundle before it's written.

### Build verify

`ninja -C Build pd pd-server` on the worktree: clean.  `build-headless.ps1 -Target all` on main post-merge: clean.  `PerfectDark.exe` 52,217,169 / `PerfectDarkServer.exe` 22,854,403 (main, dev).  Only pre-existing warnings (collision `sum2`, modelasm dangling-pointer, `/*` within comment noise, snd strncpy truncation) -- no new warnings introduced.

### Not done in this session (deferred to follow-up polish passes)

- **Engine botmgr wire** -- `forgeBotAddRequest` / `forgeBotRemoveAll` / `forgeBotFreezeAll` today log intent + bump `pending_*` counters in `forge_bot_settings_t`.  A later session consumes those counters and calls `botmgrAllocateBot` / `botmgrRemoveAll` with the right aibotnum + chrnum + rooms args for the current stage.
- **Base-stage -> forge_object_t auto-import** -- `forgeImportBaseStageObjects` today tags existing placed objects with `from_base=1`; the actual walk of the base stage's intro-commands / pads / spawnlist to materialise as forge objects is a follow-up rung once the F3 map-load instantiator lands.
- **3D gizmo handles** -- Properties tab transforms are still numeric; drag-axis gizmos require freefly-camera raycast + in-world handle render.
- **Real world-space ghost mesh** -- the HUD shows a reticle + catalog ID text.  Drawing the actual ghost mesh in 3D requires fast3d GBI integration and is a larger change.
- **Actual grid lines in 3D** -- the HUD shows a 5x5 cross pattern + readout.  True world-space grid lines need GBI integration too.
- **Controller tab cycling via dedicated binds** -- today relies on ImGui's built-in GamepadL1/R1 mapping which requires NavEnableGamepad, which was intentionally left off to preserve freefly camera.  A follow-up can route the game's `actionmap` layer to dispatch tab-cycle requests so controller-only users get bumper cycling without the camera-stick conflict.
- **Controller nav for Place/Cancel** -- ghost placement Commit/Cancel buttons respond to mouse/keyboard; true gamepad-driven placement is tied to the above follow-up.

### Parallel session coordination

S311 (menus/theme/settings) and S312 (gameplay/input/net) were running in parallel worktrees when S313 committed.  S313 merged to dev at `16cb6f7e` on top of `8d2e7872`; S312 then merged on top at `85421a8a`; S311 was still in-flight at session end.

---

## Session S312 — 2026-04-17 (Modeldef defensive guards + glyph system + net review — `amazing-mccarthy` worktree)

**Scope**: Gameplay bug fixes, input system polish, net review.  Parallel
session on an isolated worktree; merges cleanly to `dev`.  File-ownership
split with S311 (menu renderers / theme / settings / config / UI scaling)
and S313 (The Grid forge code only) — this session touched game logic
(`src/game/`), networking (`port/src/net/`), and new input-helper module.

### What landed

1. **sp_body_108 parts=0 modeldef corruption class — defensive guards
   +  instrumentation** (root cause from S308 B-161).  `propobj.c` bbox
   walkers (`modeldefFindBboxNode`, `modelFindBboxNode`) now reject torn
   modeldefs at the top of the function: NULL rootnode, numparts out of
   [1, 500], or scale <= 0 short-circuits to NULL instead of
   descending into stale memory.  Both walkers also carry a 10000-step
   safety cap so a cyclic/dangling tree logs a WARNING and bails out
   rather than AV'ing.  `port/src/net/netmanifest.c`
   `manifestEnsureLoaded` late-add path now logs the post-load
   modeldef state for body/head assets — `MANIFEST-SP: late-add '...'
   post-load modeldef torn: parts=%d root=%p scale=%.3f` pinpoints the
   exact catalog id whose late-add produced the torn state, giving the
   next playtest log the fingerprint needed to move B-161 from defensive
   to root-cause fixed.  Guarded by `#if !defined(PD_SERVER)` so the
   server build still links (`catalogGetBodyModeldef` is client-only).

2. **Glyph system for contextual input prompts** — new
   `port/include/pdgui_glyphs.h` + `port/fast3d/pdgui_glyphs.cpp`
   module.  Maps any `InputAction` to the primary VK currently bound
   in the active IMC stack, picks the right device using
   `actionmapGetLastDevice()` (500 ms debounce), and exposes
   `pdguiGlyphGetActionLabel` for short labels ("E", "Space", "LMB",
   "A", "LB", "D-Up") plus `pdguiDrawActionPrompt(action, x, y,
   label)` / `pdguiDrawActionPromptCentered` which render a compact
   `[KEY] Label` pill on the foreground drawlist using the theme's
   title-glow accent.  Walks all six default IMCs in priority order so
   prompts reflect whichever context is on top; falls back to the
   other device if no binding for the current device.  VK ordinals
   mirrored locally (cannot include input.h directly — it pulls
   PR/os_cont.h which references OSThread and breaks C++ TUs).

3. **Net / gameplay / input review** — confirmed the current state of
   the previously-shipped fixes:
   - S306 / S304 input-context leak defensive pop is wired into
     `renderMainMenu` close (`pdguiConsumeTitleClose` channel + fallback
     Escape), and `inputCtxEndFrame` watchdog auto-recovers deep stacks
     at `MAX_STACK-1`.
   - `g_NetMatchRoomId` / `g_NetCounterOpClientId` reset paths cover
     disconnect (`net.c:1060-1066`), stage end (`net.c:925-928`), and
     server start (`net.c:630-631`); `netSendToRoom(0xFF, ...)` is a
     no-op on clients (no `room_id == 0xFF` match).
   - `roomLeave` / `roomDestroy` call `netReadyGateOnClientLeft` and
     `netReadyGateAbortForRoom` per Bug B.
   - `botSpawn` retains S301 `CHR.DIAG` breadcrumb + invisible-state
     fingerprint; no regression in the ground-clamp / room-recovery
     fallbacks.
   - ACTION_JUMP / ACTION_CROUCH / ACTION_USE correctly feed
     c1buttons/c1buttonsthisframe in `bondmove.c`; no residual CK_* or
     parallel `inputKeyJustPressed(VK_ESCAPE)` paths remain.

### Files touched

- **New**: `port/include/pdgui_glyphs.h`, `port/fast3d/pdgui_glyphs.cpp`
- **Modified**: `src/game/propobj.c` (two bbox walkers — torn-modeldef
  guards + 10000-step cap), `port/src/net/netmanifest.c`
  (manifestEnsureLoaded post-load diagnostic)

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both
targets link.  `PerfectDark.exe` 52,400,200 bytes / `PerfectDarkServer.exe`
22,840,561 bytes.  Only pre-existing warnings (propobj.c sp144/sp112
may-be-uninitialized, `/*` within comment noise across multiple files).

### Not done in this session (deferred)

- **Per-player glyph device detection** — `pdguiGlyphGetDevice` returns
  a single global device for all prompts.  Splitscreen is disabled in
  this port so the single-device answer is fine; if the mode ever
  returns we'll need a per-player signal.
- **Wire glyphs into actual gameplay HUD prompts** — the module is ready
  but no renderer consumes it yet.  Obvious first callers: pickup prompts
  (`[E] Pick up AR34`), interact prompts near doors/terminals, forge
  HUD's controls-reminder row.  Hooks belong in follow-up polish passes
  (S313 for forge, S311 for menus).
- **Root-cause fix for modeldef corruption** — S308 + S312 are both
  defensive.  Next playtest repro of the Mission 1 Obj 2 crash should
  produce a `MANIFEST-SP: ... post-load modeldef torn:` line (or a
  `MANIFEST-SP: ... post-load modeldef OK` followed by a later torn
  state, which would point at a post-late-add corruptor rather than the
  late-add itself).

---

## Session S311 follow-up — 2026-04-17 (UI polish marathon continuation — `zen-poitras-f86b73` worktree)

**Scope**: Five-task continuation of the S311 UI polish marathon. Sweeps remaining palette literals, wires the S312 glyph system into HUD prompts, migrates the last raw-ctx renderers to pool ownership, removes dead `ImGuiKey_Gamepad*` checks, and extends the S306 title-close channel across every remaining close handler.

### Six commits (`9281decf` → `c486dc72`)

1. **Palette sweep pt2** (`9281decf`) — ~95 remaining semantic color literals migrated to `pdguiVec4/ImU32 TitleGlow/TintInfo/TintSuccess/TintDanger/TextWarning/TextPositive`. Files: `pdgui_lobby.cpp`, `pdgui_lobby_distrib.cpp`, `pdgui_skin_editor.cpp`, `pdgui_menu_{agentselect,modmgr,moddinghub,solomission}.cpp`. Adds `pdguiVec4TextWarning` / `pdguiVec4TextPositive` ImVec4 companions. `server_gui.cpp` NOT migrated — pd-server doesn't link `pdgui_style.cpp`.
2. **Glyph wiring** (`5f18c65f`) — new `pdgui_interact_prompt.{h,cpp}` reads `g_InteractProp` each frame and draws "[E] Pick up" / "[E] Open" / "[E] Access" below the reticle via the S312 glyph system. Label from new `propInteractPromptLabel()` helper in `src/game/prop.c` that branches on `prop->type` + `OBJFLAG3_HTMTERMINAL` / `OBJFLAG3_INTERACTABLE`. Grid HUD controls reminder (`pdgui_forge_hud.cpp`) rewritten to a 4-row `pdguiDrawActionPrompt` layout. Menu tab hints + countdown cancel hint now fill key portion via `pdguiGlyphGetActionLabel`.
3. **Pool-ctx migration** (`94f0583f`) — renderCiSettingsRedirect, renderCiDeadPlayer2, renderCinemaList moved from raw `inputCtxPush/Pop` to `menupoolAcquireDialog` / `menupoolReleaseDialog`. New `MENU_TYPE_CINEMA` enum + 8 `REG()` calls + 7 local extern decls for dialogdefs mainmenu.cpp owns but data.h doesn't publish. Boot-time CI-redirect ctx leak class fixed at source.
4. **Dead Gamepad* checks** (`d18b87c1`) — 141 `ImGui::IsKeyPressed(ImGuiKey_Gamepad*)` read sites removed across 29 files. NavEnableGamepad is disabled; parallel keyboard checks already catch controller input via `pdguiDriveImGuiNav()`. Preserved: `io.AddKeyEvent` writes + `pdgui_glyphs.cpp` VK lookup. Net -203 lines.
5. **Title-close sweep** (`c486dc72`) — `pdguiConsumeTitleClose()` wired into Theme Editor, Modding Hub, Pause Menu, CI redirect (renderCiSettingsRedirect + renderCiDeadPlayer2 + renderCinemaList), and Endscreen (solo + MP). Fixes "first X click does nothing" class across all remaining dialogs.

### Files touched (summary)

- **New**: `port/include/pdgui_interact_prompt.h`, `port/fast3d/pdgui_interact_prompt.cpp`.
- **API additions**: `pdguiVec4TextWarning/TextPositive` in `pdgui_style.h`; `propInteractPromptLabel` in `src/game/prop.c` + `src/include/game/prop.h`; `MENU_TYPE_CINEMA` in `menupool.h` + menupool.c registry entries.
- **Renderer migrations**: `pdgui_menu_mainmenu.cpp` (CI redirect + DeadPlayer2 + CinemaList pool-ctx + title-close), `pdgui_menu_{moddinghub,theme_editor,pausemenu,endscreen}.cpp` (title-close), `pdgui_forge_hud.cpp` (glyph pill rewrite), `pdgui_countdown.cpp` (glyph label).
- **Palette fixes**: `pdgui_{lobby,lobby_distrib,skin_editor}.cpp`, `pdgui_menu_{agentselect,modmgr,moddinghub,solomission}.cpp`.
- **Gamepad cleanup**: 29 menu / tool files.

### Build verify

`PerfectDark.exe` 52,516,524 / `PerfectDarkServer.exe` 22,838,513 — clean link.

### Not done in this session (deferred)

- **`server_gui.cpp` palette migration** — blocked by pd-server not linking pdgui_style.cpp. Would cascade into theme/nineslice/effects/fontmgr deps.
- **Item-specific prompt text** — interact prompt shows "Pick up" / "Open" / "Access" but not the weapon / door name. Deeper `prop->obj` walk needed to surface "AR34" etc.
- **training.cpp Resume/OK controller activation** — removed a Gamepad-only check with no parallel Enter. Button click + action-map→Enter translation should still work but needs playtest to confirm.

---

## Session S311 — 2026-04-17 (UI polish marathon — theme palette sweep + content inset + scaled buttons — `zen-poitras-f86b73` worktree)

**Scope**: Mike's S311 punch list — hardcoded blue tints, content-inset compliance, menu close-path audit, font/element scaling, controller-first verification, CS music picker sanity check. Parallel with S312 (game logic) + S313 (forge). File ownership: menu renderers + theme system + `pdgui_style.*`. Did not touch `src/game/`, networking, or forge code.

### What shipped (single commit `55c37fc2`)

1. **Theme palette accessor expansion** (`pdgui_style.cpp`/.h):
   - New `pdguiRgbaToImU32(rgba, alpha)` helper centralises 0xRRGGBBAA→ImU32 conversion.
   - New semantic accessors `pdguiImU32TitleGlow/TintSuccess/TintDanger/TintInfo(alpha)` for C callers.
   - C++-only `pdguiVec4*` inline companions in pdgui_style.h (guarded by `__cplusplus && IMGUI_VERSION`) so TextColored / PushStyleColor sites stay terse.

2. **Hardcoded blue tint sweep** — 35+ sites migrated to the theme palette:
   - `IM_COL32(100, 200, 255, ...)` → `pdguiImU32TitleGlow(alpha)` in:
     - `pdgui_menu_warning.cpp:695` (default dialog title) and `:991` (MP File Manager title)
     - `pdgui_menu_agentselect.cpp:284` (copy-action confirm prompt)
     - `pdgui_menu_pausemenu.cpp:990` (local-player row tint, alpha=35 preserved)
     - Agentselect delete-action prompt red → `pdguiImU32TintDanger(255)`
   - `ImVec4(0.4f, 0.8f, 1.0f, 1.0f)` cyan section headers → `pdguiVec4TitleGlow()` (20 sites):
     - lobby.cpp (2), network.cpp (2), challenges.cpp (1), teamsetup.cpp (2), mpsettings.cpp (1), mainmenu.cpp (2), room.cpp (10)
   - Additional cyan variants → `pdguiVec4TitleGlow()`:
     - `ImVec4(0.5f, 0.85f, 1.0f, 1.0f)` in audiomod.cpp (Selected-track label) + moddinghub.cpp (skin + ini entry headers)
     - `ImVec4(0.6f, 0.85f, 1.0f, 1.0f)` in controldiagram.cpp (control-mode name) + mpsettings.cpp (Library header)
   - `ImVec4(0.7f, 0.8f, 1.0f, 1.0f)` endscreen SectionHeader → `pdguiVec4TitleGlow()`.
   - `ImVec4(0.6f, 0.8f, 1.0f, 1.0f)` modmgr "Base Game Asset" label → `pdguiVec4TintInfo()` (semantic info badge).

3. **Content inset fix** — `pdgui_menu_warning.cpp:789-790`: MP End Game dialog body-top switched from raw `SetCursorPosY(pdTitleH + WindowPadding.y + breathe)` arithmetic to `pdguiSetCursorBelowTitle(pdTitleH) + 8px breathe` so user chrome insets are honoured. Last remaining S309-pattern miss. endscreen/pausemenu/theme_editor use different per-dialog padding helpers that already call `pdguiThemeGetContentInset` directly — no changes needed.

4. **Element scaling fix** — `pdgui_menu_mainmenu.cpp:935/947`: delete-confirm Cancel/Delete buttons migrated from `ImVec2(120, 0)` to `ImVec2(pdguiScale(120.0f), 0)` so they scale with resolution/DPI.

5. **CS music picker verification (S309 fix)**: re-walked the import + discovery + render path. `importAudioFile` in audiomod.cpp:402-413 correctly calls `modmgrRescanDirectory()` → locate new mod by slug → `modmgrSetEnabled(i, 1)` + `modmgrSaveConfig()`. `collectModMusicTrack` in mpsettings.cpp:580 emits `SELECTTUNES: skip` diagnostic for filtered tracks. `renderSelectTunes` emits `SELECTTUNES: open — base_tracks=N mod_tracks=N` one-shot on appear. Wiring solid, no changes needed.

6. **Menu close-path audit**: all S300/S304/S306 migration debt resolved. No residual `s_*PushedCtx` raw patterns. `pausemenu` uses dedicated `g_CtxPauseMenu`. `endscreen` uses `inputCtxPush(&g_CtxImGuiMenu)` gated on `!inputCtxIsActive + freshEntry`. `solomission` manual `inputCtxPopDeferred` calls (3 sites) all guarded by `inputCtxIsActive`. `mainmenu` defensive leak-catch at `:3416` still guarded + logs `MENU_IMGUI: defensive inputCtxPopDeferred — leak class caught`. `renderCiSettingsRedirect` still uses raw push/pop (queued S306 follow-up), but isActive guards keep it idempotent. No new bugs surfaced.

7. **Controller-first audit**: `pdguiDriveImGuiNav()` in pdgui_backend.cpp:356-398 translates action-map events (controller A/B/D-pad/LB/RB) into ImGui keyboard nav keys (Enter/Escape/arrows/PageUp/Down) because `ImGuiConfigFlags_NavEnableGamepad` is disabled. Menu renderers' `ImGuiKey_GamepadFaceDown/Right/...` checks are redundant (NavEnableGamepad off → those events never fire) but harmless — the parallel `ImGuiKey_Enter/Escape` checks catch the same edges via the action-map→key translation. Input-mapping capture path uses `inputGetLastKey()` which accepts raw controller button codes (via `isVkController()`) so rebinding works with controller. No changes needed.

### Files touched

- `port/include/pdgui_style.h` — 7 new declarations (generic RGBA→ImU32, 4 semantic ImU32 accessors, + 4 C++-only ImVec4 inlines behind `__cplusplus && IMGUI_VERSION` guard).
- `port/fast3d/pdgui_style.cpp` — 5 new implementations (+35 lines).
- `port/fast3d/pdgui_menu_{agentselect,audiomod,challenges,controldiagram,endscreen,lobby,mainmenu,moddinghub,modmgr,mpsettings,network,pausemenu,room,teamsetup,warning}.cpp` — palette migrations + content-inset + button scaling.

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link clean. `PerfectDark.exe` = 52,288,651 bytes. `PerfectDarkServer.exe` = 22,838,513 bytes.

### Not done in this session (deferred)

- **Remaining non-blue literal sweep** — agentselect/agentcreate/solomission/modmgr/moddinghub still have a handful of PD-blue `IM_COL32` panel/border literals (PD-blue deco rather than semantic color) plus red/yellow/green literals that could fold into `tint_danger`/`text_warning`/`text_positive`. Lower-value cosmetic cleanup; leaving for a follow-up.
- **Dead `ImGuiKey_Gamepad*` checks** — ~130 redundant checks remain across solomission/training/mainmenu/agentselect/warning/etc. All dead code (NavEnableGamepad off) but working alongside the redundant Enter/Escape checks that catch the same actions. Removing is a ~1h churn task queued separately.
- **renderCiSettingsRedirect / renderCiDeadPlayer2 / renderCinemaList S300 pool-ctx migration** — still on raw `inputCtxPush/Pop`. Defensive leak-catch in mainmenu.cpp:3416 hides the symptom.
- **Full hardcoded-blue audit of pdgui_lobby.cpp / pdgui_lobby_distrib.cpp / server_gui.cpp / pdgui_skin_editor.cpp** — touched only menu_*.cpp files this session; those other files have matching `ImVec4(0.4f, 0.8f, 1.0f, 1.0f)` headers that could migrate.

---

## Session S310 addendum — 2026-04-17 (Mike R1-R4 refinements — same worktree)

Four design refinements from Mike rolled into The Grid feature branch:

- **R1 Seamless swap + per-player foundation**: `forgemode.c` now saves the player chr's `bodynum`/`headnum` on FREEFLY entry and restores on NORMAL exit, so the visual body swap can land at any later rung without touching the session logic.  No stage reload on toggle (already true).  New `forgeGetPlayerSessionState(playerNum)` + `forgeTogglePlayerMode(playerNum)` entry points wire per-player state for MP co-op forge (F8 stretch); F0 proxies to the global state for player 0.
- **R2/R4 Weapon pad source**: new `forge_weapon_source_t` enum + `forge_map_settings_t.weapon_source` / `allow_match_override` fields.  Default is MAP_DEFAULTS + allow_match_override=1.  Modes: MAP_DEFAULTS (each pad spawns its author-chosen weapon), MATCH_OVERRIDE (lobby weapon set overrides every pad), PREFER_MAP (specific pads keep weapon, "Any" pads use lobby).  Settings tab gained a three-option dropdown + checkbox.  Serializer round-trips both fields.
- **R3 Mod dependency collection**: new `forgeCollectDependencies` in `forge_core.c` walks all state (object catalog_id + material_id, AI body/head/weapon, weapon pad weapon_id, pickup item_id, effect asset_id, zone sounds, door key_id + sounds, atmosphere sky_id, gametype starting_weapon, every wave's enemy_catalog_id, base_stage_id) and returns the unique list of non-`base:` catalog IDs.  Serializer writes a `dependencies` array into both mod.json (for the distribution pipeline's recursive resolver) and map.json (for standalone-preview callers).  Settings tab shows a live dependency-count bullet list.

Commit: `833c3a95`.  Build verify clean: PerfectDark.exe 52,186,794 / PerfectDarkServer.exe 22,839,025.

Deferred: actual engine-level Dr. Carroll model hot-reload (currently logs the intent + swaps chr->bodynum + chr->headnum but doesn't re-skin the mesh live).

---

## Session S310 — 2026-04-17 (The Grid level editor F1-F8 bulk drop + MP lift fix — `sharp-lovelace` worktree)

**Scope**: Full F1 through F8 pass over the in-game level editor (now branded "The Grid" in user-facing UI -- internal code names stay `forge_*`). Parallel session on an isolated worktree; merges cleanly to `dev`. Also addresses the long-standing MP elevator dead-pad bug the design doc called out as an F6 blocker.

### Commits on `claude/sharp-lovelace-a08d90`

| SHA | Scope |
|-----|-------|
| `0732c806` | **F1-F5b+F7+F8 data model + editor UI**. New module `port/src/forge/forge_core.[ch]` with fixed-size pools sized to the design doc's budget caps (1024 objects, 128 zones, 64 lights, 256 logic nodes, 32 channels, 32 waves, 16 objectives, 64 prefabs, 256 undo entries). Static catalog with 100+ entries across 10 top-level categories (Geometry, Props, Weapons, Spawn Points, Pickups, Lighting, Effects, Zones, Interactables, Characters). New `pdgui_forge_editor.cpp` -- tabbed overlay (Catalog/Properties/Zones/Lighting/Logic/Game Type/Mission/Settings) drawn in FREEFLY only, so NORMAL playtest is uncluttered. Click-to-place commits an object at the camera's forward-400u position. Serializer writes `mods/Forge Maps/<slug>/{mod.json,map.json}` with a tolerant JSON reader round-tripping all metadata/settings/objects. Logic runtime fires events->conditions->actions with a cycle detector and a recursion guard (depth 128). Game Type runtime covers wave spawner + boss state + 5 role modes + 8 modifier flags. Mission tab wires objectives to logic OBJECTIVE_COMPLETE/OBJECTIVE_FAIL action nodes. F8 UI ships weather (rain/snow/sandstorm/storm) + ToD cycle enable + post-process color grades. |
| `ac7be593` | **MP elevator fix + F6 AI helpers**. Root cause of dead lifts in CI arenas: `liftActivate()` (which registers the lift prop in `g_Lifts[liftnum-1]`) was only called from the AI-script command `aiActivateLift` (opcode `0x018d` in `chraicommands.c:8498`). CI arenas have no AI scripts, so lifts there were unregistered; `liftFindByPad()` returned NULL; stepping on the pad did nothing. Fix lives in `src/game/setup.c::OBJTYPE_LIFT` -- auto-registers at setup time by reading `liftnum` from one of the lift's own pads (`PADFIELD_LIFT`). SP is unaffected since the AI-script call overwrites the same pointer (idempotent). New `port/src/forge/forge_ai.c` with count/patrol-chain/navmesh-stub helpers (navmesh auto-gen is a placeholder; forge maps reuse the base stage's navmesh and rely on S302 tiered-spawn fallback if forge geometry blocks a waypoint). |

### Phase coverage against the design doc

- **F1 Catalog + placement**: 100+ catalog entries across all design §3.1 categories; search + category filter + budget bar; click-to-place with grid snap from the editor settings; 256-op undo/redo ring with barrier grouping.
- **F2 Properties**: Per-category prop panels for weapon pad / spawn point / AI (including boss flag, name, scale, phase thresholds) / door (open dir, locked, key id) / elevator (stops, speed, wait, call button, loop) / switch (type, activation, channel out) / light (all 4 types w/ type-specific fields) / zone (all 13 types) / effect / pickup. Duplicate / Delete / Deselect actions.
- **F3 Save/load**: Writes mod.json (reusable by mod loader w/ type="forge-map") + map.json with full schema coverage (metadata, settings, skylight, atmosphere, gametype+waves, objects, logic nodes+wires+channels). Tolerant reader re-loads the full map. Mod-slug derived from map name; re-derive button.
- **F4 Zones + lighting**: 13 zone types (§6.1 full list); skylight yaw/pitch/color/intensity/shadow; atmosphere fog/ambient/exposure/bloom + color grade; placed lights point/spot/area/emissive with type-specific properties (cone angles, area dimensions, cookie textures).
- **F5 Logic**: 12 events + 9 conditions + 23 actions covering design §7.2 in full, plus F5b-specific SPAWN_WAVE / TRIGGER_BOSS_PHASE and F7-specific OBJECTIVE_COMPLETE / OBJECTIVE_FAIL. Wire create/remove, channel create/toggle/delete, per-node Fire button for manual-trigger testing, cycle detection.
- **F5b Game types**: 4 structures (single/best-of-N/wave-based/phase-based), 5 win conditions, 8 modifiers, 5 player roles (Infection, VIP, Juggernaut, Horde Defender, Gun-Game Hunter). Wave table with per-wave enemy_catalog_id / count / scale / hp_mult / speed_mult / spawn_delay / intermission / spawn_zone_uid / is_boss flag. Boss state with simulate-activation test button and damage-simulator.
- **F6 Interactables + AI**: MP lift fix lands the biggest functional gap. Interactable properties (doors/elevators/switches) fully editable. AI props cover body/head/weapon/behavior/faction/alert_radius/health_mult/respawn plus F5b boss extension. Patrol-chain walker + navmesh stub in `forge_ai.c`.
- **F7 Mission**: Is-Mission checkbox enables briefing / debrief / sequential-objective toggles; 3 objective kinds (primary/secondary/bonus); per-objective link to logic completion+failure nodes with Complete/Fail/X buttons.
- **F8 Stretch**: Weather (5 kinds), ToD cycle, post-process color grades (Neutral/Warm/Cool/Noir/Alien). Terrain remains catalog-placeable as geometry prefabs (height-paint terrain is out of scope here).

### Files touched (summary)

- **New**: `port/include/forge/forge_core.h`, `port/src/forge/forge_core.c`, `port/src/forge/forge_undo.c`, `port/src/forge/forge_logic.c`, `port/src/forge/forge_gametype.c`, `port/src/forge/forge_serialize.c`, `port/src/forge/forge_ai.c`, `port/fast3d/pdgui_forge_editor.cpp`
- **Modified**: `port/include/pdgui_forge.h` (editor entry points), `port/fast3d/pdgui_backend.cpp` (editor dispatch), `port/src/pdmain.c` (forgeCoreTick wiring), `src/game/setup.c` (lift auto-register)

### Build verify

`ninja -C Build pd pd-server` -- both targets link. PerfectDark.exe 52,176,729 / PerfectDarkServer.exe 22,839,025 (server unchanged; forge is client-only).

### Not done in this session (deferred)

- **3D gizmo handles** -- Properties tab edits transform numerically; drag-axis gizmos need freefly-camera raycast + in-world handle rendering.
- **Ghost placement reticle** -- click-in-catalog commits at camera+400u. True ghost with green/red valid-tint lives in a follow-up polish pass.
- **Runtime engine instantiation of placed objects** -- objects are serialised but not yet materialised as live engine props at match load. The F3 map-load path needs to walk the `forge_object_t` pool and call the appropriate `setupCreate*` functions. Logic OPEN_DOOR / TELEPORT / SPAWN_AI actions log-only until this lands.
- **AI navmesh auto-gen** -- `forgeAiGenerateNavmesh` is a stub; forge maps inherit the base stage's navmesh.
- **Boss HUD bar** -- data model tracks it; HUD drawing lands in `pdguiForgeHudRender` in a polish pass.
- **Co-op Forge** (F8 stretch) -- out of scope for single-session bulk drop; requires new SVC/CLC msgs for placement sync.

---

## Session S309 — 2026-04-17 (Deferred UI drop + config cleanup — optimistic-feistel worktree)

**Scope**: Mike's S309 punch list — 9 deferred UI items plus an S309 bug
report about Combat Simulator music. Worked in parallel with S310 (Grid F1)
on a separate worktree; touched menu renderers, theme system, config,
and settings.

### Items landed

1. **Content-inset sweep (P0)** — 20 `pdgui_menu_*.cpp` renderers migrated
   from `SetCursorPosY(titleH + WindowPadding.y)` to the new
   `pdguiSetCursorBelowTitle(titleH)` helper that clamps against the
   active chrome's nineslice insets + 8px breathe. New helpers live in
   `port/fast3d/pdgui_theme.cpp` + declarations in `pdgui_style.h` /
   `pdgui_theme.h`. Fixes content bleeding into nineslice borders on:
   agentcreate, agentselect, botsetup, challenges, cheats,
   controldiagram, lobby, mainmenu (Not-Available dialog), mpadvanced,
   mppause, mpsettings, mpsetup, network, playerconfig, room,
   solomission (11 dialogs), stats, teamsetup, training, warning.
2. **Color theme live preview** (`pdgui_menu_theme_editor.cpp`) —
   widened the modal to 820px × 85% and added a `renderLivePreview()`
   panel beside the color pickers. Shows a PD title strip (reads
   title_glow), sample buttons/checkbox, success/warning text labels
   (text_positive / text_warning), and three colored buttons driven by
   `pdguiGetTintSuccess/Danger/Info`. Reads happen every frame so edits
   update live.
3. **New S309 palette extensions (slots 20-23)** — added `title_glow`,
   `tint_success`, `tint_danger`, `tint_info` to `struct pdgui_palette`,
   theme.json (`titleGlow` / `tintSuccess` / `tintDanger` / `tintInfo`
   camelCase aliases accepted), `k_PaletteFieldNames`, the theme
   editor's `k_Fields` metadata (all marked `PFG_SEMANTIC` with "auto"
   reset buttons), and new `pdguiSetPaletteExtensions2` /
   `pdguiGetTitleGlow` / `pdguiGetTintSuccess/Danger/Info` accessors in
   pdgui_style. Theme loader's `apply_theme_def` forwards both
   extension tails.
4. **Title bar "hardcoded blue" fix** — `pdguiDrawTextGlow` now reads
   `pdguiGetTitleGlow()` instead of the static `s_Theme.textGlowColor`,
   so a custom theme's palette drives the title glow. Default derivation
   (border2 → glow) ensures built-in palettes keep their look without
   authoring the new slot.
5. **Font Mod creator tool** — new "Font Mod" tab in the Modding Hub
   (`pdgui_menu_moddinghub.cpp`: `fontToolReset` + `renderFontTool`)
   with file browser for `.ttf`/`.otf` + mod-name input + Save button.
   Saves to `mods/Fonts/<slug>/<slug>.<ext>` plus a `font.json` +
   `mod.json`, rescans the font mod registry and the mod manager so the
   new entry appears in the Font dropdown without restart. Interface
   tab (`renderSettingsInterface` in mainmenu.cpp) gained an "Open Font
   Mod Tool..." button that routes via `pdguiModdingHubShowTool(8)`.
6. **Resolution-aware scanlines** — `pdguiThemeDrawScanline` /
   `pdguiThemeDrawScanlineFg` call the new
   `pdguiResolveScanlineMetrics()` helper that scales line thickness +
   stride with `pdguiScaleFactor()`. At 720p = 1px/2px, at 4K = 3px/6px.
   Prevents the "almost invisible at high-DPI" look.
7. **imgui.ini disabled** — `pdguiInit` sets `io.IniFilename = NULL` so
   ImGui never writes imgui.ini. Window state is rebuilt each session
   from `pdgui_style.cpp`.
8. **Per-agent preferences (`prefs_agent.c` / `.h`)** — new sidecar at
   `saves/prefs_<agent>.ini` with `[Theme]`, `[Video]`, `[Mods]`
   sections. API: `prefsAgentInit` (called from main.c after
   pdguiInit), `prefsAgentLoad` (called from every agent-select load
   site in pdgui_menu_agentselect.cpp), `prefsAgentSave` (called at
   end of `renderSettingsInterface`; debounced by content-hash compare
   so the disk write only happens on actual value changes).
9. **Forge → "The Grid" user-facing rename** — main menu button label
   flipped to "The Grid"; all internal code (module names,
   `pdguiForge*` entry points, `ACTION_FORGE_*` enums, `FORGE.DIAG`
   log channel) retained for compatibility. S310 Grid F1 session owns
   the HUD-side rename.
10. **CS music picker fix** (`pdgui_menu_audiomod.cpp` +
    `pdgui_menu_mpsettings.cpp`) — root cause: freshly imported audio
    mods defaulted to `enabled=0` in modmgr, so on next launch
    `modmgrLoadMod` never fired for them, their `assetCatalogRegister`
    ran at import time only, and the catalog entry vanished on restart.
    Fix: `importAudioFile` now calls `modmgrRescanDirectory()` +
    `modmgrSetEnabled(slug, 1)` + `modmgrSaveConfig()` so newly
    imported music survives a relaunch. Added `SELECTTUNES:` diagnostic
    log lines in `collectModMusicTrack` + `renderSelectTunes` so future
    "my songs aren't showing up" reports surface the filter path.

### Files touched

- New: `port/src/prefs_agent.c`, `port/include/prefs_agent.h`.
- Modified (headers): `port/include/pdgui_style.h`,
  `port/include/pdgui_theme.h`.
- Modified (C++ renderers): 20 `port/fast3d/pdgui_menu_*.cpp` files
  (see item 1), `pdgui_theme.cpp`, `pdgui_style.cpp`,
  `pdgui_theme_loader.cpp`, `pdgui_backend.cpp`.
- Modified (bootstrap): `port/src/main.c` (prefsAgentInit wire).

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both
targets link. `PerfectDark.exe` = 51,916,376 bytes. `PerfectDarkServer.exe`
= 22,840,561 bytes. Only pre-existing warnings (naudio
`sp44`/`sp20`/`frac` may-be-uninit, modelasm dangling-pointer,
collision sum2 warning, `/*` within comment noise across multiple
files).

### Not done in this session (deferred)

- **Full pd.ini elimination** (item 7 from the prompt). Visuals + mod
  enablement moved to per-agent prefs; audio volumes, resolution,
  fullscreen, bindings, gameplay toggles still live in pd.ini. The
  design doc flagged those as per-machine; finishing the elimination
  needs a scope decision ("per-agent input bindings?") that's not
  obvious from the prompt. Partial landing is enough to validate the
  per-agent load/save flow.
- **Full hardcoded-blue audit**: warning.cpp, agentselect state text,
  lobby state text still use `IM_COL32(100, 200, 255, ...)` directly.
  Migrating them to `pdguiGetTitleGlow()` was out of scope — the
  complaint was about the title bar, which this session addressed.
- **Forge HUD rename**: S310 owns pdgui_forge_hud.cpp; that file still
  says "FORGE" in the mode badge. This session only touched the
  main-menu button label.

---

## Session S308 — 2026-04-17 (Door-tick bbox crash + pause menu hardening — dev direct)

**Scope**: Fatal crash during Mission 1 Obj 1 → Obj 2 transition playthrough (0xc0000005 in door bbox chain 38s into stage 0x33 / Investigation), plus pause-menu defensive audit. Worked directly on `dev` (no worktree).

### Crash trace (addr2line resolved against Downloads/PerfectDark.exe, ImageBase 0x140000000)

```
#00 modelFindBboxNode     propobj.c:1312   (node->type deref on bad node)
#01 modelFindBboxRodata   propobj.c:1349
#02 doorGetBbox           propobj.c:19426  (*dst = *bbox with NULL bbox)
#03 doorUpdateTiles       propobj.c:19522
#04 doorsCalcFrac         propobj.c:20521
#05 doorTick              propobj.c:7913
#06 objTickPlayer         propobj.c:11501
#07 propsTickPlayer       prop.c:2173
#08 lvRender              lv.c:1451
#09 mainTick              pdmain.c:674
```

Stage 0x33 ("base:investigation") transition completed cleanly — 49 new model loads + 44 unloads. Player was in room 44 at `pos=(-409,149,-4280)` walking for ~38s. Log also flagged `WARNING: body0f02ce8c: truly invalid bodymodeldef for bodynum 108 (file 0x004b) ptr=... parts=0 -- skipping` at stage-load time, evidence of the same modeldef-corruption class bleeding into a door.

### Fixes landed

1. **NULL guards in bbox traversal chain** (`src/game/propobj.c`):
   - `modelFindBboxNode()` — early-return NULL if `model == NULL` or `model->definition == NULL` (was: deref `model->definition->rootnode` unconditionally at line 1309).
   - `modeldefFindBboxNode()` — same guard for the `modeldef` variant.
   - `modelFindBboxRodata()` / `modeldefFindBboxRodata()` — also check `node->rodata` before returning `&node->rodata->bbox`.
   - `func0f0687e4()` — same guard for the DL-node walker (shared pattern with bbox walker).

2. **doorGetBbox NULL-safe + diagnostic** (`src/game/propobj.c` line 19422):
   - If `modelFindBboxRodata(door->base.model)` returns NULL, zero-init the destination bbox and log `DOOR.DIAG: doorGetBbox — no bbox for modelnum=... model=... flags=... doortype=...` at WARNING (rate-limited to 16 per run via `s_DoorBboxMissWarnCount` file-static).
   - Prevents the AV; the tile/geo math runs against an empty box rather than a dangling pointer. If this warning fires in future logs it pinpoints the exact door.

3. **Local bbox helpers NULL-safe** (`src/game/propobj.c` lines 302–444):
   - `objGetLocalXMin/XMax/YMin/YMax/ZMin/ZMax` now return `0.0f` if `bbox == NULL`.
   - `objGetRotatedLocalMin/Max` early-return `0.0f` on NULL.
   - Called from ~15 prop-tick sites across weapon/hovercar/door/misc paths.

4. **Pause menu hardening** (`port/fast3d/pdgui_menu_solomission.cpp::renderPauseMenu`):
   - `IsWindowAppearing` path now resets `s_RestartConfirm = false` and `s_RestartSelectIdx = 0` in addition to `s_PauseSelectIdx = 0`. Prior: Restart-Confirm overlay could leak between opens if the previous close happened mid-overlay.
   - Title fallback: if `langSafe(g_SoloStages[si].name3)` returns empty, show `Mission %d: Status` instead of `: Status` with leading colon.
   - Button labels `Inventory` / `Abort Mission` now resolve via `langSafe` + hard-coded English fallback, so a missing lang bank can't paint blank buttons. `Options` → `Settings` (user-facing rename to match pause menu convention).

5. **Objective-list sanitation** (`src/game/mainmenu.c::soloMenuDialogPauseStatus` MENUOP_OPEN handler):
   - Zero the entire `g_Briefing.objectivenames[]` + `objectivedifficulties[]` arrays before repopulating on every pause open (was: only wrote up to `objectiveGetCount()`, leaving tail indices potentially stale from prior mission).
   - `setupCreateProps` still does the stage-load clear; this is a defense-in-depth second clear that makes the pause handler self-contained.

### Files touched

- `src/game/propobj.c` — bbox NULL guards + `doorGetBbox` diagnostic + local-helper NULL safety
- `src/game/mainmenu.c` — objectivenames full-array zero in pause status handler
- `port/fast3d/pdgui_menu_solomission.cpp` — IsWindowAppearing state reset + title fallback + button-label lang fallbacks

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link. No new warnings. `PerfectDark.exe` ~51.52 MB (server unchanged — propobj.c is client-only in the ninja graph).

### What the fix doesn't do

The fixes are **defensive** — they prevent the AV and log a diagnostic when the bad state is reached, but don't identify the root cause of how a door's `model->definition` became invalid mid-gameplay. The `parts=0` warning on `sp_body_108` at stage load is the strongest clue: something in the 0x30 → 0x33 manifest swap is leaving a door modeldef in an inconsistent state (rootnode points into a region whose node->type is garbage). Root-cause investigation queued — next playtest log should surface `DOOR.DIAG:` lines pointing at the specific door's modelnum.

### Not fixed in this session (deferred)

- **Root cause of modeldef corruption** — the `sp_body_108` late-add bodyAllocate warning + parts=0 + door bbox NULL all point at the manifest-diff path leaving some modeldefs in a torn state. Needs targeted instrumentation at `bodyAllocateModel` + `setupLoadModeldef` for any late-add that hits a body/door.
- **Pause menu "Quit to Menu" vs. Abort** — user described a 5-button layout including "Quit to Menu"; current set is Resume/Restart Mission/Inventory/Settings/Abort Mission. Abort routes through `g_MissionAbortMenuDialog` which quits to main menu, so functional parity exists but the label mismatch remains.
- **Mission-Complete crash from `body108`** — orthogonal, but the invalid-modeldef WARNING persists across multiple stages (any stage whose manifest references sp_body_108 without a valid ROM file or catalog registration). The catalog says `base:sp_body_108 (75) → ROM` loaded successfully, but the modeldef parts=0 — catalog cache may have an empty entry shadowing ROM data. Separate investigation.

---

## Session S306 — 2026-04-16 / 2026-04-17 (Interface tab + Input Mapping redesign + theme palette extensions + delete flow + X-button close fix — dev direct)

**Scope**: Mike's multi-batch request — redesign Input Mapping screen, add delete actions for themes/mods, extend theme color editor, plus three addenda: new Settings → Interface tab, theme-bundle UI, per-agent settings. Worked directly on `dev` (no worktree). Six commits landed; per-agent prefs deferred to its own session.

### Commits on dev

| SHA | Scope |
|---|---|
| `feb5431c` | **Palette extensions** — 5 new palette fields (toolbar_tint, text_positive, text_warning, button_hover, button_active) + `checkbox_checked` now live. `pdgui_palette` struct gains 5-field tail; zero = derived default. New accessors `pdguiGetToolbarTint/TextPositive/TextWarning/CheckmarkColor` exposed via pdgui_style.h. Theme.json schema gains `toolbarTint`/`textPositive`/`textWarning`/`buttonHover`/`buttonActive` (snake + camel aliases, plus `checkmark` alias for `checkbox_checked`). Theme Editor rebuilt with grouped sections (Frame / Text / Interact / Semantic / Reserved) + per-row tooltip + "(auto)" reset button on extension slots. Moddinghub tool selector now pulls its active tint from `pdguiGetToolbarTint` instead of hardcoded blue. |
| `a504f3e7` | **Interface tab + delete scaffolding**. New `Settings → Interface` tab (between Video and Audio, index 1). Moved Menu Style / Title Bar / Font dropdowns out of Video and the Color Theme selector out of Debug — each still lives in one place. Added "Open Color Editor..." + "Open Menu Style Tool..." buttons that launch the deeper tools via the new `pdguiModdingHubShowTool(int tool)` entry point. Tab order is now 0=Video 1=Interface 2=Audio 3=Controls 4=Game 5=Updates 6=Debug 7=Catalog — `ciRedirectTargetTabForDialog` updated to match. Delete support: `pdguiThemeGetFilePath(s32)` exposed; `pdguiInterfaceRequestThemeDelete/ModDelete` + `pdguiInterfaceRenderDeleteConfirm` implemented — right-click or controller X-button on a user theme opens the context menu with "Delete Theme…"; confirmation modal echoes the on-disk path, deletes theme.json / mod.json / audio.ini, then rmdirs the folder (best-effort — user-added files stop rmdir cleanly). Theme rescan fires on success. |
| `ae4f2f50` | **Input Mapping redesign (BATCH 1)**. Pulled all 51 gameplay-IMC actions into the UI (previously 23); `AXIS_*` stays excluded because they're analog-synthesised. Nine-group taxonomy (Movement / Aim / Combat / Weapons / Vehicle / Menu Nav / C-Buttons / D-Pad / System Hotkeys). Added: live search filter (case-insensitive substring match), per-column conflict detection with red-border render on any VK bound to two or more actions, per-row tooltips for non-obvious actions (Fire Mode, D-Pad Down = radial, etc.), capture-state yellow-border affordance. Removed the redundant "Save Controls" button at the bottom (every rebind already auto-saves via actionmapSaveBinds + configSave). Click-to-rebind / right-click-to-clear / Esc-to-cancel preserved. |
| `5840f28a` | **(Mike parallel — S308)**. Door-tick bbox NULL guard + pause menu hardening. Absorbed my BATCH 2 delete wiring (right-click + controller X-button → "Delete Mod…" on Installed Mods tab; delete-confirm modal rendered both from Interface tab and from inside the Modding Hub so deletes initiated in the hub don't depend on Settings being open). |
| `db509d87` | **Theme Editor bundle dropdowns**. Added "Menu Style (optional)" + "Font (optional)" dropdowns to the Save-as-Mod panel, populated from `pdguiThemeGetChromeStyle{Id,Name}` and `pdguiFontModGet{Id,Name}`. Selection writes into theme.json as `menuStyle` / `font` keys per the S305 P4 schema. Both default to "(none)" — omits the key entirely when unbundled so older loaders stay byte-identical. |
| `affc61fa` | **X-button close path + ctx-leak fix**. Direct-signal close channel (`s_TitleCloseFrame` + `pdguiConsumeTitleClose()`) replaces pure-Escape-injection from the title X button. Fixes the "first X click does nothing, second click works" symptom — ImGui's own nav was eating the injected Escape edge before the renderer's IsKeyPressed saw it. Escape AddKeyEvent fallback preserved for renderers that haven't been migrated yet. Also adds a defensive `inputCtxPopDeferred(&g_CtxImGuiMenu)` in the top-level main-menu close path — catches the ctx-leak class where `renderCiSettingsRedirect` pushes the ctx at boot and never pops, which was producing "movement locked after menu closed" because g_ImcMenu stayed priority-active. NOTE log fires when the defensive pop catches a leak. |

### Log investigation

Mike's post-S305 playtest log (`Downloads/Perfect Dark 2.0/pd-client.log`, 22:48 → 23:40) showed:

- `MENUPOOL: acquired main_menu` at 00:03.55 with `ctx=none(shared)` — same symptom Mike reported before S304 rebuild. Root cause: `INPUTCTX: imgui_menu on_push` at **00:01.30** (one second after boot, before main menu ever opens) — `renderCiSettingsRedirect` auto-pushes `g_CtxImGuiMenu` when the CI Options dialog opens at boot and never pops it (that renderer is still on the raw ctx pattern, per the S304 deferred-migration list). When the main menu opens, the pool's acquire finds `g_CtxImGuiMenu` already on the stack → attaches in "shared" mode, does not own the pop on release. All subsequent menu close paths leave the ctx stuck, g_ImcMenu stays priority-active, and movement input gets routed away from gameplay.
- `affc61fa`'s defensive pop catches this on top-level main-menu close. A proper fix is to migrate `renderCiSettingsRedirect` (+ `renderCiDeadPlayer2` + `renderCinemaList`) to the S300 pool-owned ctx pattern; queued as S304 follow-up.
- No crash, no watchdog warnings, audio underruns persist (~37/30s) — B-141 unchanged.

### Not fixed in this session (deferred)

- **Per-agent prefs.ini** — design doc `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md` already covers the storage layout + agent-switch hook + Settings write-through. Implementation estimate ~300-400 lines; dedicated future session because of careful test-matrix needs (mid-match switch, guest agent, Settings-open switch). Theme bundle plumbing that per-agent depends on already shipped in S305 + this session's Theme Editor bundle dropdowns.
- **renderCiSettingsRedirect / renderCiDeadPlayer2 / renderCinemaList migration to S300 pool ctx** — still on raw `inputCtxPush/Pop`. `affc61fa`'s defensive pop hides the symptom; the underlying leak class remains.
- **B-141 audio underruns** — persistent but not catastrophic.
- **Settings Close X button on non-main-menu dialogs** — the S306 X-click fix only wires `pdguiConsumeTitleClose()` into `renderMainMenu`'s close handler. The CI redirect, endscreen, pause menu, modding hub, and theme editor still rely on the Escape fallback. Sweep queued.

### Files touched

- `port/fast3d/pdgui_style.cpp` + `port/include/pdgui_style.h` — palette struct extension, `pdguiSetPaletteExtensions`, semantic accessors, `s_TitleCloseFrame` + `pdguiConsumeTitleClose`.
- `port/fast3d/pdgui_theme_loader.cpp` + `port/include/pdgui_theme_loader.h` — 20-field palette in theme_def, alias table, `pdguiThemeGetFilePath`, apply_theme_def forwards extensions.
- `port/fast3d/pdgui_menu_theme_editor.cpp` — grouped sections, bundle dropdowns, 20-slot working palette, skip-zero save.
- `port/fast3d/pdgui_menu_mainmenu.cpp` — **largest change this session**: Interface tab (`renderSettingsInterface` ~300 lines), moved Menu Style / Title Bar / Font / Color Theme out of Video + Debug tabs, delete helpers + confirm modal, input mapping rewrite (`s_BindableGroups` + `s_BindableActions` with all 51 entries + conflict map + search + grouped rendering), X-button close + defensive ctx pop. Also shifted s_SettingsSubTab indices for the new Interface tab (0=Video 1=Interface 2=Audio 3=Controls 4=Game 5=Updates 6=Debug 7=Catalog) and updated `ciRedirectTargetTabForDialog` + Controls-tab re-init guard to match.
- `port/fast3d/pdgui_menu_moddinghub.cpp` — `pdguiModdingHubShowTool(int)` entry for deep-links from the Interface tab; toolbar tint now uses `pdguiGetToolbarTint`.
- `port/fast3d/pdgui_menu_modmgr.cpp` — (landed via Mike's 5840f28a) right-click + controller X-button → "Delete Mod…" context menu on Installed Mods tab; delete-confirm modal rendered from within modmgr too.

### Build verify

All six commits built clean via `source devtools/build-env.sh && ninja -C Build pd pd-server`. Final `PerfectDark.exe` ~51.5 MB, `PerfectDarkServer.exe` ~22.9 MB. Only pre-existing warnings (propobj.c size-casts, xrayalphafrac may-be-uninit, ImageBase comment-in-comment).

### Playtest punch list (what Mike should exercise)

1. **Interface tab** — open Settings, confirm a new "Interface" tab sits between Video and Audio. All four dropdowns (Color Theme, Menu Style, Title Bar, Font) + Open Color Editor + Open Menu Style Tool should work from it. LB/RB bumpers cycle through 8 tabs now instead of 7.
2. **Color Theme delete** — right-click a user theme in Interface tab → "Delete Theme…" → confirm. Theme should disappear from the list without a restart. Built-in themes should refuse the delete silently.
3. **Mod delete** — right-click a mod in Modding Hub → Mod Manager tab → "Delete Mod…" → confirm. Mod disappears from the list.
4. **Controller X-button** — GamepadFaceLeft on focused theme/mod row opens the same context menu. No-op on built-in themes.
5. **Theme palette extensions** — open Theme Editor. Four groups visible (Window Frame / Text Colors / Interactive Elements / Semantic Accents) + checkbox to show Reserved. "Auto" button on Semantic slots resets them to the derived default. Save as mod → theme.json only includes extension keys that were actually touched.
6. **Theme bundle** — open Theme Editor → fill Name, pick a Menu Style (optional) + Font (optional), Save. Open the resulting `mods/<slug>/theme.json` and confirm `menuStyle` / `font` keys are present.
7. **Input Mapping redesign** — Settings → Controls → Keyboard & Mouse. Should see Movement / Aim / Combat / Weapons / Vehicle / Menu Nav / C-Buttons / D-Pad / System Hotkeys group headers. Search box filters by action name. Bind "W" to two actions deliberately — both should show red borders. Click Fire Mode — should rebind cleanly to next press (try a controller button — should play error sfx because MKB tab is showing).
8. **X-button close on Settings** — open Settings, click the X in the top-right on the first attempt. Should close to main menu immediately. If it works first-try, `affc61fa` landed. Log should show `MENU_IMGUI: main menu ESC — settings CLOSE (view 2->0) [via X]`.
9. **Movement after menu close** — open main menu, close it, WASD should move immediately. If log shows `MENU_IMGUI: defensive inputCtxPopDeferred(g_CtxImGuiMenu) — leak class caught`, that means the ctx-leak was caught and unlocked movement.

### Constraints touched

- **Interface tab + sub-tab numbering** — `s_SettingsSubTab` range is now 0..7 (was 0..6). Downstream code that compares specific index literals (Controls re-init guard at old index 2, CI redirect mapper) updated.
- **Theme palette size** — `struct pdgui_palette` gained a 5-field tail. `pdguiSetPaletteCustom` now memcpys only the legacy 15 fields to avoid buffer over-read; new `pdguiSetPaletteExtensions` writes the tail.
- **Theme editor working palette** — `s_WorkPalette[15]` expanded to `s_WorkPalette[20]`; snapshot + reset paths cover the full 20.
- **Theme.json schema** — gained optional extension keys (`toolbarTint`, `textPositive`, `textWarning`, `buttonHover`, `buttonActive`, plus `checkmark` / `checkMark` aliases for `checkbox_checked`). Older loaders skip unknown keys.

---

## Session S307 — 2026-04-17 (Forge level editor F0 Foundation — merged to dev from tender-sutherland worktree)

**Scope**: Phase F0 of the in-game Forge level editor.  Design ref:
`context/designs/forge-level-editor-2026-04-16.md` §15.  Worktree session
running in parallel with S306 on `dev` (per Mike's instruction: don't merge
until F0 is complete and clean so S306's UI work can build/test
independently).  No protocol bump (forge is local-only at F0).

### What shipped (single commit `8d412cc1` on `claude/tender-sutherland-536de7`)

- **`src/include/game/forgemode.h`** — public C-callable API.  3-state
  `forge_session_state_t` (INACTIVE / NORMAL / FREEFLY).  Forward-declares
  `struct coord` and uses `s32` for boolean returns so the header is safe
  to include from C++ TUs (project's `#define bool s32` would otherwise
  break C++ ABI).
- **`src/game/forgemode.c`** — module state, freefly camera integrator
  (yaw + pitch from look axes, world-relative WASD movement, Q/E vertical,
  3x boost / 0.25x precision modifiers, 600 u/s base speed, 89° pitch
  clamp, 60Hz dt assumed for F0).  Mode toggle hijacks the player by
  setting `bondmovemode = MOVEMODE_CUTSCENE` (so `bmoveTick` routes to
  `bcutsceneTick`, suppressing the legacy walk update) and overrides
  `prop->pos + vv_theta + vv_verta` each tick.  Session-exit watchdog
  drops to INACTIVE whenever `g_StageNum` is no longer `STAGE_IS_GAMEPLAY`.
  Lazy-init guard so `forgeTick` is safe before any explicit `forgeInit()`.
- **`port/include/pdgui_forge.h`** — two C entry points
  (`pdguiForgeStartSession`, `pdguiForgeHudRender`).
- **`port/fast3d/pdgui_menu_forge.cpp`** — `pdguiForgeStartSession()`
  rejects if a stage transition is already pending; otherwise queues
  `forgeRequestEnterSession()` and calls
  `mainChangeToStage(STAGE_CITRAINING)`.  F3 will replace the hard-wired
  CITRAINING with a base-stage browser.
- **`port/fast3d/pdgui_forge_hud.cpp`** — foreground-draw-list HUD shell:
  top-left mode badge (NORMAL green / FREEFLY cyan), centered placement
  reticle in freefly, bottom-left camera readout
  (pos / yaw / pitch / speed-scale tag), right-edge placeholder for the F1
  catalog panel, bottom-right controls reminder.  Renders nothing when
  `forgeSessionIsActive()` is false; safe to call every frame.  Avoids
  including `types.h` (forward-declares `struct coord`) per the standing
  C++ TU rule.
- **`port/include/actionmap.h`** — five new `InputAction` enums between
  `ACTION_SCORECARD` and `ACTION_COUNT`: `ACTION_FORGE_TOGGLE`,
  `ACTION_FORGE_ASCEND`, `ACTION_FORGE_DESCEND`, `ACTION_FORGE_BOOST`,
  `ACTION_FORGE_PRECISION`.  `ACTION_COUNT` 57 → 62.
- **`port/src/actionmap.cpp`** — `s_ActionNames[]` table extended with the
  five new strings; default keyboard binds for player 0 added at the end of
  `setupGameplayDefaults` (F7 toggle, E ascend, Q descend, LSHIFT boost,
  LCTRL precision).  Dual-bind to LSHIFT/LCTRL is intentional — sprint and
  crouch share the same keys but freefly reads the dedicated FORGE actions.
  Controller toggle deliberately unbound; LB+RB chord per design comes
  later.
- **`port/src/pdmain.c`** — `forgeTick()` called from `mainTick` between
  `playermgrShuffle()` and the per-player gameplay loop, so a freefly
  bondmovemode override is in place before any per-player `bmoveTick`
  dispatches.  `#include "game/forgemode.h"` added.
- **`port/fast3d/pdgui_backend.cpp`** — `pdguiForgeHudRender()` called
  after `pdguiGameOverRender`, before `pdguiRenderAllWindowShimmers` /
  scanline overlay so the editor HUD sits beneath those post-process
  effects.
- **`port/fast3d/pdgui_menu_mainmenu.cpp`** — new top-level "Forge" button
  between Stats and Quit in the `s_MenuView == 0` block; clicks dispatch
  to `pdguiForgeStartSession()` plus `PDGUI_SND_OPENDIALOG`.

### F0 architecture decisions

1. **Mode toggle hijack** — instead of building a parallel camera matrix
   path, freefly hijacks `g_Vars.currentplayer->prop->pos` plus `vv_theta`
   and `vv_verta` each tick, with `bondmovemode = MOVEMODE_CUTSCENE` to
   suppress the legacy walk overwrite.  This works because the existing
   camera setup (`camSetLookAt` flow in `player.c:5027`) reads from the
   player struct.  Later phases can refactor this if needed; for F0 it's
   the smallest possible diff.
2. **Single tap toggle** — design specifies "hold for 0.5s" but that
   needs a hold-time tracker.  F0 uses single-tap on `ACTION_FORGE_TOGGLE`
   (F7 default) — accidental triggers in normal gameplay are unlikely
   since F7 isn't bound to anything else and normal gameplay reads the
   action via `actionPressed` (rising edge) so accidental taps from
   sprint/crouch holds don't fire it.
3. **No collision in freefly** — design explicitly says Dr. Carroll
   passes through geometry.  Forge camera ignores all collision.
   Placement-time collision queries land in F1 with the catalog/reticle.
4. **CI Training as the only base stage** — F0 hard-wires
   `STAGE_CITRAINING` since that's the existing free-roam scaffold.  F3
   brings the base-stage browser per the design doc.
5. **Session lifecycle bound to stage** — entering a forge session sets
   the request flag and queues a stage change; once in gameplay the
   watchdog activates the session.  Leaving gameplay (Esc → main menu →
   Quit, etc.) auto-cleans the session and restores the player's
   bondmovemode.
6. **C++ ABI safety** — `forgemode.h` deliberately forward-declares
   `struct coord` and uses `s32` for boolean predicates because the
   project's `#define bool s32` (in `types.h`) breaks any C++ TU that
   includes `types.h` directly.  Same pattern as `pdgui_hud.cpp` and
   `pdgui_menu_mainmenu.cpp`.

### Build verify

`source devtools/build-env.sh && cmake -G Ninja ... -B Build -S .` (one-time
configure for the worktree's own `Build/`; `build-headless.ps1` redirects to
the main working copy by design and can't be used in worktrees), then
`ninja -C Build pd pd-server`.  Both targets link cleanly:
`PerfectDark.exe` 51,780,176 bytes, `PerfectDarkServer.exe` 22,840,512.
Only pre-existing warnings (`f32 near/far` typed-name macro, `/*` within
comment); no new warnings from the F0 files.  Object files
`forgemode.c.obj`, `pdgui_menu_forge.cpp.obj`, `pdgui_forge_hud.cpp.obj`
all present in `Build/CMakeFiles/pd.dir/...`.

### Process

- Worktree-only commit `8d412cc1`.  No merge to `dev` yet — Mike requested
  the worktree stay parallel with S306's UI work until F0 is verified
  in-game.  Merge plan (post-playtest): fast-forward into `dev`, post-merge
  line-count verification per CLAUDE.md §9.
- HEAD pre-edit was `b546736d` (v0.0.107 release commit).  Single F0
  commit on top: 10 files, +830 / −1 lines.

### Not done in this session (deferred to F1+)

- Object catalog browser, placement reticle, gizmo, properties panel
  (F1–F2 per the design phase plan).
- Save/load `map.json`, base-stage browser, network sync (F3).
- Drop-to-ground transition on freefly exit (polish).
- Dr. Carroll model swap (visual; F0 keeps the player chr model in CUTSCENE
  mode).
- Controller binding for the toggle (LB+RB chord per design).
- Sim pause toggle in HUD.
- Forge entry from custom/private matches (currently main-menu only; F3
  base-stage browser is the natural place to add this).

**Next**: in-game playtest of the F0 verification recipe in `tasks-current.md` (see S307 section). Worktree `claude/tender-sutherland-536de7` merged to `dev` 2026-04-17.

---

## Session S305 — 2026-04-16 (Playtest batch: content inset + settings persistence + font mods + theme bundle — dev direct)

**Scope**: Mike's post-S304 playtest surfaced eight issues in one pass; worked directly on `dev` (no worktree). All items landed + pushed to origin.

### Fixes landed

1. **P0 content-inset enforcement** (`port/fast3d/pdgui_menu_mainmenu.cpp`): main menu items, Quit button, Settings body `BeginChild`, CI Settings redirect, and Cinema list all honour the nineslice chrome content inset (S297/S298 `pdguiThemeGetContentInset`) plus an 8px breathing-room buffer. Fixes the screenshot bug where Solo Play / Online Play / Change Agent / Settings / Mods / Cheats / Stats / Quit Game bled edge-to-edge into the border chrome.

2. **Settings persistence** (`port/src/config.c`, `port/fast3d/pdgui_theme_loader.cpp`): two root-causes for "theme / title-bar / menu-style don't survive restart":
   - `configLoad` used strict `configFindEntry` so any pd.ini key whose owning subsystem called `configRegister*` AFTER `configInit` was silently dropped. `pdguiThemeInit` (inside `pdguiInit`, line 183 of main.c) runs after `configInit` (line 169), so theme/chrome/title-bar/scanline keys never loaded. Fix: pending-value stash + replay — `configLoad` uses `FindOrAdd`, stores raw values on unregistered entries, `configRegister*` replays on registration. `configSave` preserves pending entries as raw key=value so data isn't lost if registration never happens in a run.
   - `pdguiThemeLoaderInit`'s startup apply only handled built-in palette themes. Mod themes saved via Save-as-Mod (resolved via `pdguiThemeLoadFromCatalog`) were skipped on every restart, reverting the user to the default palette. Fix: route through `pdguiThemeLoadFromCatalog` for non-builtin ids.

3. **Nine-Slice → Menu Style** rename (`port/fast3d/pdgui_menu_moddinghub.cpp`, `port/fast3d/pdgui_menu_mainmenu.cpp`): all user-visible references updated. Internal API names (`pdguiNineslice*`, catalog id `base:ui_chrome_frame`, `mods/UI Chrome/` tree) stay for compatibility. Modding Hub tab label + tool array + saved mod description + header blurb + Settings → Video dropdown label all flipped.

4. **Theme Editor P1 + P2** (`port/fast3d/pdgui_menu_theme_editor.cpp`): Save button moved off the Name/Author row into the action-button row alongside Reset/Close (was partly off-screen on narrow windows + only reachable via Tab). After a successful Save, calls `pdguiThemeRescanMods()` so the new theme appears in Settings → Debug → UI Theme list immediately. Same pattern as B-155/B-156 chrome mod auto-appear.

5. **Menu Style tool preview scaling + screen cleanup (P5 + P6)** (`port/fast3d/pdgui_menu_moddinghub.cpp`): source preview now scales UP as well as down so small imported images fill the sidebar preview at the same standard size as the assembled frame preview below (removed the `if (imgScale > 1) clamp = 1`). Power-user controls (Scale X/Y, Center Cut Axis/%, Desaturate + %, Quick Presets) moved into a collapsed "Advanced" `CollapsingHeader` so Mod Name / symmetry / tile mode / trim / Border Scale / per-edge insets stay front-and-center.

6. **Font Import as Mod (P3)** — new module: `port/include/pdgui_font_mod.h` + `port/fast3d/pdgui_font_mod.cpp`. Scans `mods/Fonts/<slug>/` (or top-level `mods/<slug>/`) for `.ttf`/`.otf` files, optionally reads `font.json` for a display name, registers each under `user.<slug>.font`. New `Video.FontId` pd.ini key tracks the active choice. `pdgui_backend.cpp` calls `pdguiFontModInit()` BEFORE the ImGui font atlas is built — if the saved FontId resolves to a mod path, `AddFontFromFileTTF` loads it as `io.FontDefault`; Handel Gothic still loads as the fallback. Settings → Video gets a "Font" dropdown with "(restart required)" hint. Font mod discovery uses the same modmgr search roots as theme/chrome discovery.

7. **Theme Mod Bundling (P4) — plumbing + design doc** (`port/fast3d/pdgui_theme_loader.cpp`, `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md`): `theme_def` gains `bundle_chrome_id` + `bundle_font_id` fields. `theme.json` can now declare `"menuStyle"` (user-facing alias; legacy `"chromeStyle"` also accepted) + `"font"` to reference a registered chrome style + font mod by catalog id. `apply_theme_def` forwards the ids to `pdguiThemeSetUiChromeStyleId` / `pdguiFontModSetActiveId` so one theme activation swaps the whole visual identity. Missing: Theme Editor UI doesn't yet expose the two new fields — bundles must be hand-authored after Save-as-Mod for now.

8. **Per-Agent Settings — design doc only**: `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md` covers the architecture for theme/menu-style/font/enabled-mods following the active Agent profile. Storage via `saves/agents/<slot>/prefs.ini` sidecar, applied on agent switch via existing setters. Flagged as a dedicated future session — ~300-400 lines + careful test matrix.

### Playtest log analysis

Reviewed `/c/Users/mikeh/Downloads/Perfect Dark 2.0/pd-client.log` (22:48, 22+ minutes). Findings:

- Main menu acquired at 00:03.85 with `ctx=none(shared)` — PRE-S304 build (S304 migrated to `ctx=imgui_menu(owned)`). Mike needs to rebuild to pick up the S304 fix.
- No `MENUPOOL: released main_menu` line in the entire log — menu was closed via a path that bypassed `menuCloseDialog`. That's exactly the leak S304's three-layer defense was built for. The watchdog and underflow auto-release aren't in this build either.
- Audio underruns: ~200 every 30s, ~16 hitches. B-141 persists but not catastrophic.
- Game ran 22+ minutes, stable heartbeats, no crashes.

**Recommendation**: rebuild with the latest `dev` (S304 fix + everything S305 landed above). If darkened/layered menu persists after that, the bug is architectural rather than slot-leak.

### Constraints touched

- `configLoad` semantics (config.c): now uses FindOrAdd instead of strict find; pending-value replay on register; save preserves pending. Documented behaviour.
- Theme.json schema: gained `menuStyle` + `font` keys.

### Build verify

All commits build-verified via `source devtools/build-env.sh && ninja -C Build pd pd-server`. Final sizes: PerfectDark.exe ~51.48 MB, PerfectDarkServer.exe ~22.85 MB.

### Not fixed in this session (deferred)

- **Theme Editor UI** for bundle_chrome_id / bundle_font_id dropdowns — today bundles are hand-authored.
- **Per-agent prefs.ini** — full implementation queued (design doc ships).
- **Content inset on OTHER menus** — renderCiSettingsRedirect + Cinema covered; 10+ other ImGui renderers (cheats, mpsetup family, mppause family, room, etc.) still render without inset if they use `pdguiDrawPdDialog` directly. Separate sweep needed.
- **B-141 audio underruns** — persistent, not addressed.

### Commits (dev)

- `fc614b2e fix(menu): S305 content-inset for main menu + CI redirect + Cinema (P0)`
- `8d4b6b62 fix(config): S305 persist settings registered after configInit`
- `9b4a24fd refactor(ui): S305 rename Nine-Slice Chrome → Menu Style`
- `74655a9d fix(theme-editor): S305 dock Save + auto-refresh themes list (P1 + P2)`
- `dfb38c62 refactor(menu-style-tool): S305 scale preview to standard + collapse Advanced (P5 + P6)`
- `3daa5275 chore: auto-commit before release (dev window)` — captured the font module diff
- `aaa245bc feat(theme): S305 P4 theme bundle plumbing + per-agent design doc`
- Mike's interleaved auto-commits + v0.0.107 release commit.

---

## Session S304 — 2026-04-16 (Menu pool slot leak + consistency watchdog — focused-roentgen worktree)

**Scope**: Fix CRITICAL B-160 — menu pool slot leak where the main menu's close path left the `MENU_TYPE_MAIN_MENU` slot active, causing every subsequent `menuPushDialog` to be rejected by the pool's structural dedup (S299). User-visible symptom: darkened/stacked/dead main menu on reopen, broken input authority. Playtest log evidence: `MENUPOOL: acquired main_menu gen=1 ctx=none(shared)` at 00:07.28 was paired only with a `MENUPOOL: released main_menu` at 00:33.24 (shutdown) — between them, three `menuPopDialog called at depth 0 (underflow)` warnings and two `menuPushDialog rejected — pool slot [main_menu] already active` rejections.

### Root cause

`port/fast3d/pdgui_menu_mainmenu.cpp::renderMainMenu` top-level ESC close handler (S295 F7 era) did:

```cpp
if (inputCtxIsActive(&g_CtxImGuiMenu)) {
    inputCtxPopDeferred(&g_CtxImGuiMenu);     // pops ctx
}
// restore game state ...
menuPopDialog();                               // pops legacy stack + releases pool
```

When a force-close path (endscreen bridge, matchStart, netmsg stage handlers, or a previous iteration of the ESC handler that already fired) had already zeroed `g_Menus[].depth`, `menuPopDialog` hit its `depth == 0` underflow guard (F-3.2, S261) and early-returned WITHOUT releasing the pool slot. The ctx was already popped by the first step, but the pool slot stayed active — the leak class that surfaces as "darkened/layered menu when switching around."

The bug is systemic: any close path that pops the input ctx AND the legacy stack as two independent steps is vulnerable if the stack is popped by something else in between. The pool release was only ever wired into `menuCloseDialog`, which is only reached when `menuPopDialog` gets past the underflow guard.

### Fix (three layers — defense in depth)

1. **Underflow-path pool recovery** (`src/game/menu.c::menuPopDialog`): the underflow guard now calls `menupoolReleaseAll()` if `menupoolCountActive() > 0`, with a `MENU: underflow detected N stale pool slot(s) — releasing (stack desync)` WARNING. This closes the leak class at the exact point where it was previously silently dropping slots. Safe because normal close paths release their own slot before depth goes to 0, and force-close sites already call `menupoolReleaseAll` explicitly; the only flows that reach underflow with active slots are the buggy ones we want to recover from.

2. **Per-frame consistency watchdog** (`src/game/menu.c::menuPoolConsistencyCheck`, wired at top of `menuTick`): detects the `legacy stack empty ∧ pool has active slots` invariant violation every frame. If it fires, logs `MENU: watchdog — legacy stack empty but N pool slot(s) active; releasing leaked slot(s)` at WARNING and calls `menupoolDumpActive()` for diagnostics before `menupoolReleaseAll()`. This catches ANY leak path we haven't thought of, even future regressions — e.g. `menuPushRootDialog`'s blanket `numdialogs = 0; depth = 0;` reset (menu.c:3735-3736) silently discards any pool slots that were active before the root push, which the watchdog will now surface.

3. **Main menu renderer migrated to S300 pool-owned ctx** (`port/fast3d/pdgui_menu_mainmenu.cpp`): `IsWindowAppearing` path replaces the raw `inputCtxPush(&g_CtxImGuiMenu)` with `menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu)` — the pool attaches the ctx to the slot that `menuPushDialog` pre-acquired (S300 attach-to-active semantics). Close path drops the explicit `inputCtxPopDeferred` call; `menuPopDialog` → `menuCloseDialog` → `menupoolReleaseDialog` now cascades the ctx pop atomically, symmetric with the acquire. This brings the main menu into parity with the ten ImGui renderers migrated in S300 (cheats, mpsetup family, mppause family, mpadvanced family, playerconfig, botsetup, agentselect, room, training, mpsettings).

### Files touched

- `src/game/menu.c` — `menuPopDialog` underflow recovery + new `menuPoolConsistencyCheck()` function
- `src/include/game/menu.h` — `menuPoolConsistencyCheck` declaration
- `src/game/menutick.c` — call `menuPoolConsistencyCheck()` at top of `menuTick`
- `port/fast3d/pdgui_menu_mainmenu.cpp` — S300 pool-owned ctx migration for `renderMainMenu` (open + close path); `menupool.h` added to extern "C" includes

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link. `PerfectDark.exe` 51,563,427 bytes, `PerfectDarkServer.exe` 22,837,840 bytes. Only pre-existing warnings (types.h `f32 near/far` typed-name macro, `/*` within comments in `pdgui_theme.h` / `pdgui_bridge.c` / `updater.h`). No new warnings.

### Not fixed in this session (deferred)

- **Other mainmenu.cpp renderers still on raw ctx pattern** — `renderCiSettingsRedirect` (line 3170+), `renderCiDeadPlayer2` (line 3249+), `renderCinemaList` (line 3358+) all still use raw `inputCtxPush/Pop` around `menuPopDialog`. They are vulnerable to the same class of leak if their legacy stack is force-popped before their close handler fires. The watchdog will catch any resulting leak, but a proper S300 migration is deferred for a scope-focused future session.
- **CI Options sibling registration** — `g_CiOptionsViaPcMenuDialog` / `g_CiOptionsViaPauseMenuDialog` are siblings opened by `menuPushDialog`'s nextsibling loop but NOT registered in `menupool.c`. Registering them would collide with `g_CiControlStyleMenuDialog` (already `MENU_TYPE_CI_OPTIONS`), producing dedup rejections on legitimate opens. Left unregistered — pool passthrough is correct behavior for placeholder siblings.

**Next**: playtest B-160 per the verify column (main-menu reopen stress, force-close paths, watchdog absence on healthy flow).

---

## Session S303 — 2026-04-16 (Campaign/Co-Op/Counter-Op full game loop sweep — reverent-chatelet worktree)

**Scope**: end-to-end audit of the three gameplay modes (Campaign solo, networked Co-Op, networked Counter-Op) plus manifest pipeline / starting weapons / menu bookends. Find problems AND fix them; add logging where runtime verification is needed. Full findings in `context/scratch/game-loop-sweep-2026-04-16.md`.

### Fixes landed (7)

1. **Manifest clear generalization** (`src/game/menutick.c`) — S298 Deep Sea `manifestClear(&g_ClientManifest)` pattern now covers three additional transition sites that previously could leak a torn-down manifest into `manifestMPTransition`: MPENDSCREEN restart-level (`case MENUROOT_MPENDSCREEN` + `g_Vars.restartlevel`), MPENDSCREEN → CITRAINING exit, and COOPCONTINUE → CITRAINING exit. Each new clear site logs `GAMELOOP.MANIFEST: ... clearing manifest (N entries) before ...`. The Deep Sea block itself gained a `GAMELOOP.COOP: Deep Sea auto-advance → stageindex=... stagenum=0x... manifest=N` line.

2. **Counter-Op antiplayernum fallback logging** (`port/src/net/net.c::netServerCoopStageStart`) — the silent `antiplayernum = 1` fallback (when `g_NetCounterOpClientId` lookup misses) now emits `GAMELOOP.COUNTEROP: antiClientId unresolved (id=...) — falling back to slot %d (clients=%u)` at LOG_WARNING. Success path logs `GAMELOOP.COUNTEROP: server start anti stage=... clients=... antiClientId=... antiplayernum=... (from wire)`. Coop start similarly logs `GAMELOOP.COOP: server start coop ...`.

3. **Stale MP manifest leak WARNING** (`port/src/pdmain.c::mainChangeToStage`) — new WARNING when `g_ClientManifest.num_entries > 0` but the local session is not in any MP/coop/anti state (`g_NetMode==NONE && !iscoop && !isanti && !normmplayerisrunning`): "stale MP manifest (%d entries) leaked into SP transition to 0x%02x — routing via MPTransition". Every gameplay transition also gets a `GAMELOOP.MANIFEST: MP transition to 0x%02x (manifest=N netmode=... iscoop=... isanti=...)` or `GAMELOOP.MANIFEST: SP transition to 0x%02x ...` or `GAMELOOP.MANIFEST: menu transition to 0x%02x — clearing manifest ...` so every stage change is attributable.

4. **Co-op telefrag audit** (`src/game/playerreset.c`) — new WARNING when `(coopplayernum >= 0 || antiplayernum >= 0) && g_NumSpawnPoints < 2`: "GAMELOOP.COOP: only N spawn pad(s) declared for coop stage 0xXX — P1/P2 telefrag risk (mode=coop|anti)". Identifies the GAP-B class (mission has a single `INTROCMD_SPAWN`, the scenario-choose path uses `chrCompareTeams` which skips same-team filtering, `playerTrySelectPoolSpawn` is gated on `mplayerisrunning` which is false in coop). A full fix needs coop-aware pad scoring; this session only makes the problem observable.

5. **Starting weapon logging** (`src/game/playerreset.c::playerReset`) — every INTROCMD_WEAPON grant now logs `GAMELOOP.WEAPON: playernum=%d mission=0x%02x INTRO gave R=%d L=%d default=%d [+eyespy]`. Anti-role gets an explicit `GAMELOOP.WEAPON: ... INTRO skipped for anti role (weapons come from possession)` line so "empty-handed anti" is diagnosable. Entry of `playerReset` also logs `GAMELOOP.{CAMPAIGN,COOP,COUNTEROP,MP_COMBATSIM}: playerReset begin playernum=... role=bond|coop|anti ...`.

6. **Endscreen push bookends + anti NULL guard** (`src/game/endscreen.c`) — `endscreenPushCoop` / `endscreenPushAnti` / `endscreenPushSolo` / `endscreenPrepare` all log `GAMELOOP.{CAMPAIGN,COOP,COUNTEROP}: endscreenPush* entry playernum=... stats=... ...` at entry. `endscreenPushCoop` and `endscreenPushAnti` log their NULL-currentplayerstats / out-of-range g_MpPlayerNum aborts as WARNINGs. `endscreenPushAnti` gains a `g_Vars.bond` NULL guard before the decision branch (`endscreen.c:1926-1929` derefs `g_Vars.bond->isdead` without a guard — prevents AV in teardown races).

7. **Menu bookend + coop manifest logs** (`src/game/mainmenu.c`, `port/fast3d/pdgui_bridge.c`, `port/src/net/netmsg.c`) — `menuhandlerAcceptMission` entry logs `GAMELOOP.{CAMPAIGN,COOP,COUNTEROP}: menuhandlerAcceptMission entry stage_id='...' stagenum=0x... stageindex=... diff=... [RETRY]` (the RETRY flag is set when the resolved stagenum matches the live stage). `pdguiEndscreenStartMission` / `NextMission` / `ExitToMainMenu` each log their entry + pre/post stageindex for Next. `SVC_STAGE_END` receive logs `GAMELOOP.{COOP,COUNTEROP,CAMPAIGN}: SVC_STAGE_END received — entering endscreen teardown`. Co-op server-side `manifestBuild` at `netmsg.c:~4870` logs `GAMELOOP.{COOP,COUNTEROP}: server-side manifest built entries=... hash=... stage='...' clients=...` so host vs. server manifest divergence is measurable.

### GAMELOOP.* log taxonomy (new)

Five tags, all pass through the log-channel classifier unchanged (appear in every playtest log):

- `GAMELOOP.CAMPAIGN:` — solo start/retry/next/exit, SVC_STAGE_END (combat-sim), endscreen prepare
- `GAMELOOP.COOP:` — coop server start, endscreen push, Deep Sea auto-advance, manifest-build, telefrag audit
- `GAMELOOP.COUNTEROP:` — anti server start, endscreen push, anti-NULL guard, wire-resolution status
- `GAMELOOP.MANIFEST:` — every `mainChangeToStage` branch (SP/MP/menu), stale-leak WARNING
- `GAMELOOP.WEAPON:` — INTROCMD_WEAPON per-grant, anti-skip notice

Grep recipe: `grep -E "GAMELOOP\.(CAMPAIGN|COOP|COUNTEROP|MANIFEST|WEAPON)" pd-client.log`.

### Gaps identified, not fully fixed (logged only, queued)

- **GAP-A co-op manifest ignores host payload** — `netmsg.c:4859 manifestBuild(&g_ServerManifest, NULL, NULL)` rebuilds server-side for coop/anti (Combat-Sim path uses `manifestDeserialize` at `:4706`). New `GAMELOOP.COOP: server-side manifest built ...` log makes divergence observable. Fix needs a design call.
- **GAP-B co-op 1-pad telefrag** — now observable via log; fix needs coop-aware pad scoring in `playerChooseSpawnLocation` or a coop-specific SP-14 expansion in playerreset.
- **Anti body/head pre-load manifest gap** — possession-based resolution already relies on post-load catalog; pre-load would require a canonical anti-chr per mission (not currently modeled).
- **Menu pool not released on `menuhandlerAcceptMission` entry** — S303 logs the entry but doesn't add `menupoolReleaseAll` (entry paths already go through `menuStop`). Queued.

### Build verify

`source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link. `PerfectDark.exe` 51,564,390 bytes, `PerfectDarkServer.exe` 22,837,840 bytes. Only pre-existing warnings (`/*` in comment in `pdgui_theme.h`/`pdgui_bridge.c`, pointer cast in `mainmenu.c` 2P anti menu handler, `chraction.c` maybe-uninitialized). Two forgotten `#include "system.h"` in `endscreen.c` + `mainmenu.c` added during build-verify to land the new logs.

### Files touched

- `src/game/menutick.c` — generalized manifestClear in three exit paths + GAMELOOP logs
- `src/game/mainmenu.c` — `menuhandlerAcceptMission` entry log + `system.h` include
- `src/game/endscreen.c` — `endscreenPush*` entry logs + `g_Vars.bond` NULL guard + `system.h` include
- `src/game/playerreset.c` — `playerReset` entry log + per-weapon logs + coop telefrag audit
- `port/src/pdmain.c` — `mainChangeToStage` manifest-route logs + stale-leak WARNING
- `port/src/net/net.c` — `netServerCoopStageStart` anti fallback WARNING + coop/anti success logs
- `port/src/net/netmsg.c` — `SVC_STAGE_END` teardown log + coop `manifestBuild` log
- `port/fast3d/pdgui_bridge.c` — endscreen bridge entry logs at StartMission / NextMission / ExitToMainMenu
- `context/scratch/game-loop-sweep-2026-04-16.md` (new)

**Next**: playtest the five recipes in the findings report. Expected healthy signal: every stage transition produces a `GAMELOOP.MANIFEST:` line; every coop/counter-op start produces a matching server + client pair; no WARNING-level GAMELOOP lines on clean stock paths. Warnings identify regressions.

---

## Session S300 — 2026-04-16 (Menu pool ctx migration + SP-14 + manifest audit — peaceful-aryabhata worktree)

**Scope**: Three-task batch on one commit, merged to `dev`.

1. **ADR §6.1b — ImGui renderers `s_*PushedCtx` → pool-owned ctx.** Completes Phase 2 of the input-authority ADR by migrating the ten ImGui renderer files off the per-file `s_FooPushedCtx` booleans and onto `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` / `menupoolReleaseDialog(def)` through the pre-allocated pool shipped in S299. The pool now owns the input-context push/pop lifecycle for each slot.
2. **SP-14 follow-up — `g_NetMatchRoomId` + `g_NetCounterOpClientId` defensive reset in `netDisconnect` and `netStartServer`.** Closes the last teardown gap not covered by the S295 `netmsgSvcStageEndRead` reset: if a server-mode host disconnects mid-match or re-hosts without a clean stage-end, the stale room id and counter-op client id no longer survive into the next session.
3. **Manifest gap audit.** Confirmed `manifestBuildForMenu` + `manifestMenuTransition` are live in `netmanifest.c` and wired through `mainChangeToStage` in `pdmain.c:772`; confirmed `manifestSPRescanSetup` provides the post-load split of `manifestBuildMission`. Added per-category diagnostic logging so future regressions are diagnosable from `pd.log` alone.

### Task 1 — ImGui renderers pool migration (ADR §6.1b)

**Files touched** — ten ImGui renderers plus the pool library:

- `port/include/menupool.h` — added three new `menu_type_t` values (`MENU_TYPE_MP_SOUNDTRACK`, `MENU_TYPE_MP_TUNES`, `MENU_TYPE_MP_TEAMNAMES`) so the four mpsettings sub-dialogs can legitimately stack (Soundtrack → SelectTunes). Added a C-side accessor `menupoolDialogDef(const struct menudialog *)` because C++ renderer callbacks can't include types.h (it redefines `bool`).
- `port/src/menupool.c` — extended `menupoolAcquire` to **attach the ctx to an already-active slot** when the caller provides one and `slot->owned_ctx` is NULL. This is the key enabler for the migration: `menuPushDialog` already acquired the slot with `ctx=NULL`, so the renderer's IsWindowAppearing call attaches the ctx without mutating the dedup state. Registered `g_MpSoundtrackMenuDialog` / `g_MpSelectTunesMenuDialog` / `g_MpTeamNamesMenuDialog` against the new types. Added `g_MpSoundtrackMenuDialog` / `g_MpSelectTunesMenuDialog` / `g_MpTeamNamesMenuDialog` externs to `src/include/data.h`.
- `port/fast3d/pdgui_menu_cheats.cpp` — removed `s_CheatsHubPushedCtx` bool; Begin()=false cull → `menupoolReleaseDialog`; IsWindowAppearing → `menupoolAcquireDialog(..., &g_CtxImGuiMenu)`; Back/Esc → `menuPopDialog()` (menuCloseDialog handles pool release).
- `port/fast3d/pdgui_menu_mpsetup.cpp` — removed `s_MpSetupPushedCtx`; converted `mp_BeginStandardWindow` / `mp_CloseCurrentDialog` helpers to take `const struct menudialogdef *def`; updated all eight renderer entry points (`renderMpArena`, `renderMpScenario*`, `renderMpWeapons`, `renderMpSelectRandomWeapons`, `renderMpQuickTeamWeapons`, `renderMpLimits`, `renderMpScenarioOptions*`, `renderMpExtGameOptions`) to name the `dialog` param and pass its definition.
- `port/fast3d/pdgui_menu_mppause.cpp` — removed `s_MpPausePushedCtx`; same helper + renderer conversion (6 renderers).
- `port/fast3d/pdgui_menu_mpadvanced.cpp` — removed `s_MpAdvancedPushedCtx`; same helper + renderer conversion (9 renderers including the three `renderMpPlayerSetupHub*` wrappers and `renderMpStuff*`).
- `port/fast3d/pdgui_menu_playerconfig.cpp` — removed `s_PlayerConfigPushedCtx`; same helper + renderer conversion (5 renderers: `renderMpCharacter`, `renderMpPlayerStats`, `renderMpLoadSettings`, `renderMpLoadPreset`, `renderMpLoadPlayer`).
- `port/fast3d/pdgui_menu_botsetup.cpp` — removed `s_BotSetupPushedCtx`; same helper + renderer conversion (5 renderers: `renderMpSimulants`, `renderMpAddSimulant`, `renderMpChangeSimulant`, `renderMpEditSimulant`, `renderMpSimulantCharacter`).
- `port/fast3d/pdgui_menu_agentselect.cpp` — removed `s_AgentSelectPushedCtx`; inline pattern (no helper), load/select transitions use `menupoolReleaseDialog` before handing off to the file manager path.
- `port/fast3d/pdgui_menu_room.cpp` — removed `s_RoomPushedCtx`; Room is pure-ImGui (no dialogdef backing), so uses the type-based API `menupoolAcquire(MENU_TYPE_ROOM, NULL, &g_CtxImGuiMenu)` / `menupoolRelease(MENU_TYPE_ROOM)`. Solo mode remains in shared ctx mode (main menu owns the push) — pool records shared-mode and doesn't pop on release.
- `port/fast3d/pdgui_menu_training.cpp` — removed `s_FrWeaponPushedCtx`; converted `renderFrWeaponList` to pool API. Only the FR entry point pushes ctx; sub-dialogs (difficulty, info) run under the shared ctx.
- `port/fast3d/pdgui_menu_mpsettings.cpp` — removed per-caller bools (`s_TunesOwnsCtx`, `s_SoundtrackOwnsCtx`, `s_TeamNamesOwnsCtx`); converted `pdms_BeginStandardWindow` / `pdms_CloseCurrentDialog` helpers to take the dialogdef. Handicap (which never pushed ctx) simply calls `pdms_CloseCurrentDialog()` with no args. Each sub-dialog maps to its own pool slot so they can stack cleanly (Soundtrack→Tunes in shared-mode).

**Key acquire semantics change**: `menupoolAcquire` now attaches ctx to an existing slot when called with a non-NULL ctx and `slot->owned_ctx == NULL`. Return code `0` still signals "slot was already active" so `menuPushDialog`'s dedup rejection still works. The internals now emit a distinct log line `MENUPOOL: attached ctx to active <type>` whenever the attach path fires (typically once per renderer's IsWindowAppearing after the menuPushDialog pre-acquire).

### Task 2 — SP-14 defensive teardown

- `port/src/net/net.c::netDisconnect` — after resetting `g_NetMode` to `NETMODE_NONE`, defensively resets `g_NetMatchRoomId = 0xFF` and `g_NetCounterOpClientId = NET_NULL_CLIENT`. Logs the prior values when either is non-sentinel, so we can diagnose any case where these leak across a disconnect (should be rare given the existing SVC_STAGE_END and netServerStageEnd resets; this is belt-and-braces).
- `port/src/net/net.c::netStartServer` — same reset just before the server-mode announcement, covering the "previous server-mode session terminated without a clean netServerStageEnd, now re-hosting" case.

### Task 3 — Manifest audit + diagnostics

- Verified `manifestBuildForMenu` (netmanifest.c:605) builds from `assetCatalogIterateByType(ASSET_BODY / ASSET_HEAD, ...)` and is wired into the menu path via `manifestMenuTransition` (netmanifest.c:1615) called from `mainChangeToStage`'s `!STAGE_IS_GAMEPLAY` branch (pdmain.c:772). No code change needed to close the original gap.
- Verified the SP pre/post-load manifest split is in place: `manifestSPTransition` fires from pdmain.c:768 (pre-load, `g_StageSetup.props` still NULL), `manifestSPRescanSetup` fires from lv.c:532 (post-load, setup files parsed). The post-load rescan uses `manifestBuildMission` to re-scan with the now-populated setup data and applies any newly-discovered entries as a diff.
- **Diagnostics added**:
  - `manifestBuildForMenu` now records per-category counters (bodies added, mod bodies, disabled; heads added, mod heads, disabled) and logs a structured summary so a zero mod count with enabled `user.<slug>.*` catalog entries is immediately obvious in `pd.log`.
  - `manifestSPRescanSetup` now logs the diff shape (`newly-discovered=N kept=M unload=K`) after the post-load diff so it's easy to tell whether the rescan found cinematic / ailist spawns the pre-load scan missed (B-118 symptom was `newly-discovered=0`).

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 530/530 targets link. `PerfectDark.exe` 51,534,311 bytes; `PerfectDarkServer.exe` 22,827,178 bytes. Only pre-existing `'/*' within comment` warnings and the `f32 near/far` typed-name macro warnings; no new ones.

**Process**: single commit on the worktree branch, then worktree fast-forward merged into `dev` (line counts verified per CLAUDE.md §9). No push to origin.

**Not done in this session** (deferred):

- The shared-action leak (ADR §3.5 / menu-input-audit §6.2) still needs per-scope state arrays in the actionmap — separate architectural change not tied to pool migration.
- No unit / integration tests for the pool (ADR §6.3).
- Training dialog registration sub-dialogs (FrDifficulty, FrTrainingInfoPreGame etc.) all map to `MENU_TYPE_TRAINING`, which means menu.c's pool dedup would reject a sub-push if the parent slot is already active. This is a pre-S300 issue — if playtest reveals the training menu can't drill into its sub-dialogs, the fix is to move sub-dialogs out of MENU_TYPE_TRAINING and into their own types. Flagged but not addressed today.

**Next**: playtest S300 — open each migrated menu (Cheats, MP Setup family, MP Pause family, MP Advanced family, Player Config, Bot Setup, Agent Select, Room, Training FR, MP Settings family) and confirm ctx transitions are clean on open, on Back/Esc, and on Begin()=false cull (simulate by triggering a modal overlay). Expected logs include `MENUPOOL: attached ctx to active <type>` per IsWindowAppearing and `MENUPOOL: released <type>` per close.

---

## Session S302 — 2026-04-16 (Spawn pool robustness — tiered selection + last-resort + solo-map CombatSim coverage — quirky-mendeleev worktree)

**Scope**: harden the spawn pool so every Combat Simulator scenario (stock MP maps, solo campaign maps reused as arenas, over-subscribed maps where players > pool slots) produces a spawn without crashing / stalling / leaving the player in the void. Adds an explicit selection-tier cascade (T1 optimal → T2 cycled → T3 reused → T4 last-resort) with `SPAWN.TIER:` logging so playtest traces show the distribution.

**Changes**

- **`src/include/game/spawnpool.h`**: new `spawn_select_tier_t` enum (`SPAWN_TIER_{NONE,1_OPTIMAL,2_CYCLED,3_REUSED,4_LASTRESORT}`), `spawnPoolSelectTiered()` (same params as `spawnPoolSelect` + `out_tier`), `spawnPoolLastResort(occupied, num_occupied, out_pos, out_room, out_angle)`, `spawnPoolTierName()` for log strings.
- **`src/game/spawnpool.c`**:
  - Refactored `spawnPoolSelect()` into internal helpers `poolMinDistSq`, `poolPickFarthest` (team-sector + FFA fallthrough), `poolMarkUsedFromOccupied`.
  - New `spawnPoolSelectTiered()` runs three passes: T1 = skip `used | reserved`, T2 = clear reservations and retry skipping `used` only (logs `SPAWN.TIER: T2_CYCLED — reservations cleared...`), T3 = skip nothing (every slot eligible, picks farthest-from-occupied — logs `SPAWN.TIER: T3_REUSED — pool oversubscribed...`). Legacy `spawnPoolSelect()` is now a thin wrapper that ignores tier.
  - New `spawnPoolLastResort()` — pool non-empty? pick the farthest point from occupied regardless of validation (logs `T4_LAST_RESORT — pool-slot reuse`). Pool empty? synthesise center + radial offset staggered by `num_occupied & 7` so consecutive fallback calls don't pile up (logs `T4_LAST_RESORT — synthesised pos=...`). Always writes a non-void pos + room; falls through to pad-file scan if bgFindRoomsByPos can't resolve.
- **`src/game/playerreset.c`**:
  - `spawn_needed` now uses `MAX_PLAYERS + g_BotCount + 4` in netplay (remote slots fill AFTER playerReset on each client), `PLAYERCOUNT() + g_BotCount + 4` locally, floored at 16 so small matches still have respawn headroom. Still capped at `MAX_MPCHRS`. Large-map span bonus retained.
  - Initial MP spawn (`g_Vars.mplayerisrunning && spawnPoolIsReady() && lvframe60 == 0`) now routes through `spawnPoolSelectTiered` + `spawnPoolLastResort` instead of falling back to `scenarioChooseSpawnLocation` on -1. Logs `SPAWN.TIER: %s initial MP spawn pool[%d] L%d ...`.
- **`src/game/player.c`**:
  - `playerTrySelectPoolSpawn` gate relaxed: was `g_NumSpawnPoints >= needed`, now `g_NumSpawnPoints >= needed + needed/2` (require a 1.5× margin). Previously stock maps with ties routed through the legacy shortlist (no burst reservation, telefrag-prone). Returns `true` with last-resort position when pool yields -1, so the caller never falls through to the legacy shortlist with an empty pool.
  - Zero-pad path in `playerChooseSpawnLocation` now builds an `occupied[]` snapshot from live players + bots and calls `spawnPoolSelectTiered` (tier logged). If the pool is entirely empty, calls `spawnPoolLastResort` which synthesises a position; final room-resolution fallback kicks in when even the synthesised pos has no room (maps with zero pad data). Logs `SPAWN.TIER: T4_LAST_RESORT — synthesised room=-1, fell through to pad %d`.

**Coverage of task requirements**

1. **Limited spawn points (pool < players)** — T3 REUSED picks farthest-from-occupied even when all slots collide with live positions; reservation bitset no longer permanently locks out slots because T2 clears + retries mid-burst. Log line identifies the scenario.
2. **Solo maps in Combat Simulator** — pool build already covers these (L2 waypoints / L3 grid / L4 radial). This session adds graceful SELECT-side exhaustion: when L4 produced only a tiny pool, T3/T4 keep each spawn placement working. `playerTrySelectPoolSpawn` gate also kicks in for maps with 0 declared pads because `g_NumSpawnPoints < needed + needed/2` is trivially true.
3. **Too many players (32-bot Chicago)** — initial match-start burst: reservation bitset claims slots as each placement commits, T1 handles the burst; if 32+ spawn calls hit within the same tick and run out of T1 slots, T2 cycles (reservations clear once all slots have been dished out) then T3 reuses the farthest-from-occupied slot. Respawn waves: same cascade, but with live `occupied[]` from actual props.
4. **Tier logging** — every spawn decision now logs `SPAWN.TIER: <tier> ...` at NOTE level for T1 (optimal) and WARNING level for T2/T3/T4 (degraded). Playtest tail can grep `SPAWN.TIER:` for the distribution.
5. **Integration points** — verified every caller: `playerReset` (initial spawn), `playerStartNewLife → scenarioChooseSpawnLocation → playerChooseGeneralSpawnLocation → playerChooseSpawnLocation → playerTrySelectPoolSpawn` (player respawn), `botSpawn → scenarioChooseSpawnLocation → ...` (bot spawn + respawn + failsafe re-spawn in `bot.c:1122`). All paths funnel through `playerChooseSpawnLocation`, which now tries the tiered pool first and falls through to `spawnPoolLastResort` on pool failure before invoking the legacy shortlist.
6. **Non-MP map spawn discovery** — retained existing `playerReset` waypoint/pad fallback (lines 283–406). Pool build then handles the rest via L2/L3/L4. If pool build itself fails (no waypoints + no pads + degenerate AABB), `spawnPoolLastResort` still synthesises a radial position around wherever `spawnPoolComputeAABB` lands (falls back to origin + 200u radius).

**Design decisions**

- **Keep back-compat `spawnPoolSelect`** as a wrapper that discards the tier — minimises blast radius for any future caller that doesn't care about the tier breakdown.
- **Legacy shortlist still runs when `g_NumSpawnPoints >= needed + needed/2`** — stock 21-pad maps for 4-player FFA (needed=4, 21 ≥ 6) keep battle-tested behaviour; CombatSim against a 4-pad solo map with 8 participants goes through the pool.
- **T2 clears ALL reservations** rather than just expiring them — the reservation bitset is a burst coordination mechanism, not a long-term exclusion. Once we've hit T2 we know earlier reservations have committed, so clearing them is correct.
- **T4 synthesised position uses `(num_occupied & 7)` stagger** — 8 distinct positions around the centre cover the worst plausible simultaneous-T4 burst. Not deterministic across ticks (occupied changes) but that's fine; T4 is already an escape hatch.

**Build-verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — 751/751 targets clean. `PerfectDark.exe` 51,533,440 bytes, `PerfectDarkServer.exe` 22,824,057 bytes. Only pre-existing `/*` within comment warnings + `f32 near`/`far` macro-name warnings. No new warnings from the changed files.

**Files touched**: `src/include/game/spawnpool.h`, `src/game/spawnpool.c`, `src/game/playerreset.c`, `src/game/player.c`.

**Not done in this session** (deferred):
- No in-game playtest yet — Mike to confirm tier distribution on: 32-bot Chicago (burst at match start), solo map used in CombatSim (pool should show high L4 layer), over-subscribed tiny arena (T3 should fire occasionally), regular stock maps (T1 every spawn).
- `playerChooseSpawnLocation` legacy shortlist fallback (4 passes, sllen==0 cycling) unchanged. Could be retired once playtest confirms the pool path covers every scenario, but keeping it as a safety net for now.
- No unit tests — the input/match layer has no automated coverage.

**Next**: playtest across the scenarios above, then audit `pd.log` for `SPAWN.TIER:` tier distribution. Healthy signal is mostly T1 with occasional T2/T3 on oversubscribed maps and zero T4 on stock maps.

---

## Session S301 — 2026-04-16 (Comprehensive instrumentation for Bug C / Bug D / Chicago crash / Airbase start / B-141 — confident-chaum worktree)

**Scope**: "Do everything you can, and add logging we can use to determine how to do the rest" — no fixes are straightforward enough to land outright given the playtest-only repros; this session is purely a diagnostics drop so the next Mike playtest produces logs that tell us exactly what the remaining open bugs are doing. Covers the five tracks queued in `bugs.md` (Bug C, Bug D, Chicago silent crash, Airbase 0xc0000005, B-141 audio skips).

**New files**:

- `port/include/crashbreadcrumb.h` — public API for a crash-surviving breadcrumb ring. 256 slots × 112 bytes per slot, all-static storage; push sites are one-line `crashBreadcrumbPush(fmt, ...)` calls, dump is one-line `crashBreadcrumbDump(f, entries)`. Safe to call from any thread; index update uses `__atomic_fetch_add` for racy-but-benign writer coordination. A second entry-point `crashBreadcrumbLogRecent(count)` routes via `sysLogPrintf` for a developer "tail" without crashing.
- `port/src/crashbreadcrumb.c` — implementation. `vsnprintf` into the claimed slot, timestamp via `sysGetMicroseconds`. Dump walks oldest-first and gates by sequence, so a slot overwritten between claim and publish is skipped rather than producing a half-written string.

**Wiring — crash path**:

1. `port/src/crash.c` — `crashInit()` calls `crashBreadcrumbInit()` before any handler is installed. VEH (`crashVectoredHandler`), full UEF (`crashHandler`), Windows SIGABRT (`crashSigabrtHandler`), and the Linux `sigaction` handler each append the breadcrumb ring to their log output via `crashBreadcrumbDump(FILE*, N)` — 64 entries from the VEH/SIGABRT paths (minimal stack budget), 128 from the UEF path. All dumps go to the same log file as the exception header, so a single post-crash log contains the PC/stack trace AND the execution trail leading to it.

**Wiring — push sites** (every push is behind a function that's already called once per frame at most, so ring pressure is bounded):

- `src/lib/main.c::mainTick` — frame heartbeat: `HEARTBEAT frame=… stage=… chrs=… last_chr=… pending=…`. One push per game tick.
- `src/lib/main.c::mainChangeToStage` — `STAGE.CHANGE current=… pending=… -> new=…`. Captures mid-transition crashes.
- `src/game/lv.c::lvTick` — `LVTICK frame=… stage=… update240=… paused=…`. Distinguishes outer-loop crashes from inside-stage crashes.
- `src/game/chraction.c::chraTickBg` — `CHRTICKBG frame=… bg=… slots=…`.
- `src/game/chr.c` (inside the chraTick dispatcher at line ~2495 where `g_ChrLastTickedIndex` is set) — `CHR.TICK slot=… chrnum=… action=… race=… model=…`. At 32 bots × 60 Hz that's ~1920 pushes/s, fills the ring in ~130 ms — exactly the horizon we want for "what was the last chr alive before the crash".
- `src/game/bondwalk.c::bwalkTick` — `BWALK.TICK player=… pos=(…) room=… floorroom=…`. Separates player-collision crashes from AI.
- `src/game/bot.c::botSpawn` — `BOT.SPAWN chrnum=… respawn=… model=… aibot=…`.
- `src/game/botmgr.c::botmgrAllocateBot` — `BOT.ALLOC chrnum=… slot=… body=… head=…`.
- `port/src/net/matchsetup.c::matchStart` — entry + pre-`mpStartMatch` + post.
- `port/src/net/netmsg.c::netmsgSvcStageStartWrite` — `SVC_STAGE_START.write stage=… mode=… tick=…`.
- `port/src/net/netmsg.c::netmsgSvcStageStartRead` — `SVC_STAGE_START.read srccl=… state=…`.
- `port/src/net/net.c::netServerStageStart` — entry + post-send.

**Wiring — standard log diag lines** (tagged for grep):

- `ENDSCREEN.DIAG:` — `renderMpEndscreen` fresh-entry + geometry + body-child + rankings-count + awards-path + actions-section. Capped at 80 prints per match via `ENDSCREEN_DIAG_MAX_PRINTS` and `s_MpEndscreenDiagPrintCount` so an oscillating endscreen can't flood `pd.log`. Still surfaces the six Bug C hypotheses (empty rankings, clamped contentH, Begin=false, padding math, chrome inset collision, body-child cull) at the first frame of render so one repro pass is enough.
- `CHR.DIAG:` — `botmgrAllocateBot`, `bodyAllocateModel`, `botSpawn` before/after. Logs catalog IDs alongside the integer ids; flags `model=NULL` + final `invisible=true` state at the end of spawn so Bug D (invisible bots on Chicago) leaves a fingerprint.
- `MATCHSTART.DIAG:` — `matchStart` entry + `pre-mpStartMatch` + `post-mpStartMatch`; `netServerStageStart` entry + send-outcome; `netmsgSvcStageStart{Write,Read}` send/receive markers. Airbase 0xc0000005 / "manifest OK but no SVC_STAGE_START" repro will show which step returned early.
- `AUDIO.DIAG:` — `audioInit` logs requested vs granted SDL spec (sample-rate mismatch = B-82 class warning). `audioEndFrame` tracks `nullProducer` (no PCM queued this frame), `mixBufOverflow` (s_MixBuf capacity exceeded), min/max/mean scheduler gap (ms), min/max queue depth (samples) — folded into the existing 30-second `AUDIO[B-141]:` summary.
- `CRASH.DIAG:` — the header under which the VEH/UEF/SIGABRT handlers dump the breadcrumb ring alongside the exception context. Also used by `crashBreadcrumbLogRecent()` for developer-requested tails.

**CMake**: `port/src/crashbreadcrumb.c` added to the pd-server `SRC_SERVER` list next to `crash.c` so both binaries link. The GLOB_RECURSE in pd picks it up automatically; the server target's explicit list had to be edited.

**Design decisions**:

- **Breadcrumb ring sits in static BSS, not heap.** A heap allocation would not survive stack-overflow crashes (the VEH runs on a fresh reserved stack page but can't trust the heap allocator state). 256 × 112 = ~28 KB static — trivial.
- **No log-channel for the new DIAG tags.** The channel filter (`sysLogClassifyMessage`) maps prefixes like `AUDIO:` but my new tags (`AUDIO.DIAG:`, `ENDSCREEN.DIAG:`, etc.) intentionally don't match any channel. Result: they fall through to "untagged, always passes" — they'll appear in every playtest log regardless of `Debug.LogChannelMask`. This matches the brief ("DOES appear with standard log settings during a playtest"). Per-site rate-limiting (ENDSCREEN caps at 80; CHR.DIAG is one-shot per spawn; MATCHSTART is one-shot per match; AUDIO.DIAG is 30-second batched) handles the "doesn't spam normal play" requirement.
- **DIAG counters vs new-bug fixes.** None of the five tracks had an obviously-straightforward root cause reachable without playtest evidence; the instrumentation is the work this session, and the next repro pass will tell us what, if anything, to fix outright. The one structural change I could justify — extending VEH dump to include breadcrumbs — is itself a diagnostic improvement and is included.
- **Bug D side-effect fixes**. While wiring `CHR.DIAG` I noticed `bodyAllocateModel` had no NULL-return log; added a LOG_WARNING when the body model fails to allocate and when the head catalog entry is missing. These were actual silent failures previously, so they stay regardless of the diagnostic pass.

**Build verify**: `source devtools/build-env.sh && ninja -C Build pd pd-server` — both targets link. `PerfectDark.exe` 51,541,351 bytes (up ~38 KB from baseline 51,502,778), `PerfectDarkServer.exe` 22,834,207 bytes. No new warnings; only the pre-existing `'/*' within comment` warnings in `updater.c` / `updater.h` / `savemigrate.c` / `server_gui.cpp` / `pdgui_theme_loader.h`, the `f32 near/far` member-name warnings in `types.h`, and the `gettime_offset` unused-fn warning in `enet.c`.

**Not done in this session**:

- No playtest verification — that's the point; the next Mike playtest produces the logs.
- No per-chr visibility state-change tracking (ADR §6 — invisible-bot hysteresis). The `CHR.DIAG` at spawn is a snapshot; mid-match visibility toggles (e.g. room swap → rooms[0]=-1) would need a dedicated `propSetInvisible` hook that isn't justified without at least one repro showing spawn-time state as OK.
- No audio voice-count or source-creation logging. The port's audio path is almost entirely RSP-driven; individual voice state lives inside `ultra/audio` and the PD engine's mixer, which this port doesn't extend. Would need an engine-side hook; out of scope for a logging-only session.
- The `LOG_CH_*` classifier could be extended with a `LOG_CH_DIAG` channel keyed on the `.DIAG:` suffix so operators can silence all DIAG messages with one mask bit — deliberately deferred to keep this session's footprint small.

**Next**: Mike replays the Chicago, Airbase, MP endscreen, and audio-skip repros; tail `pd.log` / `pd-client.log` / `pd-server.log` for the new tags. Expected signals (paraphrased from bugs.md):

- **Bug C** — the endscreen render state is now fully captured. If body is still invisible, check `ENDSCREEN.DIAG: rankings built count=0` (empty-body hypothesis), `ENDSCREEN.DIAG: body child opened contentW=… contentH=…` vs actual screen size (clamped-height hypothesis), or `ENDSCREEN.DIAG: ENTRY (fresh)` without a matching `contentAvail` line (Begin=false hypothesis).
- **Bug D** — `CHR.DIAG: WARNING chrnum=N is INVISIBLE after botSpawn` with the reason code (model NULL / CHRCFLAG_HIDDEN / VOID room) pinpoints which path failed.
- **Chicago silent crash** — on the next crash, the last ~1 second of `CHR.TICK`, `BWALK.TICK`, `HEARTBEAT`, and `LVTICK` breadcrumbs will be in `pd.crash.log` / `pd-client.log` under `CRASH.DIAG: breadcrumb ring dump follows:`. Read oldest-to-newest — the final entry is the one active at death.
- **Airbase no-SVC_STAGE_START** — grep `MATCHSTART.DIAG:` in both `pd-client.log` and `pd-server.log`. A server that logs `netServerStageStart stage=… clients=…` but no client logs `SVC_STAGE_START read` indicates the packet never reached the client (network layer). Server-side absence of `netServerStageStart` means the ready gate didn't fire — check for `BAILED reason=…` lines.
- **B-141 audio** — existing 30-second summary now includes min/max/mean scheduler gap, min/max queue depth, and null-producer/mix-overflow counters alongside drops/underruns/hitches. First summary after a skip lands should tell us whether the bug is hitches (OS jitter), underruns (RSP stalled), drops (main loop racing audio), nullProducer (sndTick/musicTick stopped firing), or mixBufOverflow (mod music overran the 8192-sample mix buffer).

---

## Session S299 — 2026-04-16 (Input Authority Phase 2 — menu pool single-instance discipline — trusting-banach worktree)

**Scope**: Implement Phase 2 of the input-authority / menu-pool ADR (`context/designs/input-authority-and-menu-pool-2026-04-13.md` §6). Phase 1 (S250) added `gameplayInputSuppressed()`, dispatch-site gates, and flushes on menu push + focus events. Phase 2 adds a **pre-allocated menu pool keyed by type** so that duplicate-push becomes structurally impossible and the `nextsibling` auto-open chain respects pool occupancy.

**New files**:

- `port/include/menupool.h` — public API for the pool layer. Declares `menu_type_t` enum covering ~29 menu identities (legacy-dialog menus + pure-ImGui pages), plus `menupoolAcquire` / `menupoolRelease` / `menupoolAcquireDialog` / `menupoolReleaseDialog` / `menupoolIsActive` / `menupoolIsDialogActive` / `menupoolReleaseAll` / `menupoolDumpActive` / `menupoolTypeName` / `menupoolCountActive`.  Pool slot has three fields: `active`, `owned_ctx` (optional — pool pushes + pops if present), `generation` (bump on each acquire, for diagnostics).  Acquire is atomic (0 on deny, 1 on fresh, -1 on invalid type); release is idempotent (always safe to call on inactive slot).
- `port/src/menupool.c` — implementation.  Flat `s_Pool[MENU_TYPE_COUNT]` slot array + `s_Registry[]` (capacity 96) dialogdef→type table.  `menupoolInit()` registers ~56 dialogdef pointers from `src/include/data.h` against their menu types (main menu PC + Pause variants → `MENU_TYPE_MAIN_MENU`; MP setup arena/scenario/weapons/limits/etc. → `MENU_TYPE_MP_SETUP`; training FR/DT/HT/Bio/Hangar → `MENU_TYPE_TRAINING`; etc.).  Unregistered dialogdefs pass through with no pool dedup (legacy-safe default).

**Wiring**:

1. **`port/src/inputctx.c`** — `inputCtxInit()` calls `menupoolInit()` + `menupoolReleaseAll()` so the pool is live before any menu can open and resets cleanly across the stage-transition nuclear reset. `inputCtxShutdown()` calls `menupoolReleaseAll()` BEFORE walking the stack — prevents pool slots from holding dangling `owned_ctx` pointers after the contexts are physically popped.
2. **`src/game/menu.c`** — `menuPushDialog()` calls `menupoolAcquireDialog(def, NULL)` after the existing F-3.1 pointer-equality scan; if the pool returns 0 (type already active), the push is denied. The `nextsibling` auto-open loop now checks `menupoolIsDialogActive(sibling)` and skips any sibling whose type is already active elsewhere, then calls `menupoolAcquireDialog(sibling, NULL)` for siblings that proceed. `menuCloseDialog()` iterates the layer's siblings and calls `menupoolReleaseDialog(def)` for each BEFORE decrementing `numdialogs` (otherwise the dialogdef pointers needed for the lookup would be gone).
3. **`port/fast3d/pdgui_bridge.c`** — `pdguiEndscreenStartMission` / `pdguiEndscreenNextMission` / `pdguiEndscreenExitToMainMenu` call `menupoolReleaseAll()` before `inputCtxPopDeferred(&g_CtxImGuiMenu)`. Pool handles identity cleanup; inputctx handles the shared menu context.
4. **`port/src/net/matchsetup.c`** — `matchStart()` and `matchStartFromChallenge()` add `menupoolReleaseAll()` alongside existing `inputCtxPopDeferred`.
5. **`port/src/net/netmsg.c`** — both stage-change handlers (co-op/anti at line 1190; combat at line 1345) add `menupoolReleaseAll()` inside their `!defined(PD_SERVER)` guards.
6. **`src/lib/main.c`** — no direct change needed. Stage-transition nuclear reset (`inputCtxShutdown()` + `inputCtxInit()`) now releases the pool via the inputctx.c wiring above.

**Design decisions**:

- **Pool = identity only (for now)**. The pool API supports optional ctx ownership, but this session does NOT migrate the 10 existing `s_*PushedCtx` patterns in the ImGui renderers (`pdgui_menu_{cheats,mpsetup,mppause,mpadvanced,playerconfig,botsetup,agentselect,room,training}.cpp`). Each renderer continues to own its own `inputCtxPush`/`PopDeferred` for `g_CtxImGuiMenu`, because those have been stabilized through S250 Phase 1 and S295 F4 leak guards. The pool is additive: it enforces **structural dedup at the `menuPushDialog` boundary** without disturbing the renderers' cull-handling. Future sessions can opt-in individual renderers to pool-owned ctx via the existing `ctx` parameter of `menupoolAcquire`.
- **Nextsibling chain** (`menu.c:1553-1620`): new `menupoolIsDialogActive` check at the top of the loop short-circuits sibling auto-open when that sibling's TYPE is already active elsewhere — complements the existing `menuDialogIsCurrent` guard in renderers (S296 F-3.2 belt; pool is the architectural braces).
- **Unregistered dialogs pass through**. `menupoolAcquireDialog` returns 1 (success, no dedup) for any dialogdef not in the registry table — preserves legacy behavior for dialogs whose push paths haven't been audited.
- **Registry capacity = 96**. Covers all 70 `data.h` externs + headroom for late-registered mod dialogs via `menupoolRegisterDialogdef(def, type)`.

**Guarantees delivered** (from ADR §6 acceptance criteria):

- ✅ Duplicate-push of the same menu TYPE is structurally impossible (not just runtime-rejected).
- ✅ Nextsibling auto-open chain respects pool occupancy (B-153 class prevention).
- ✅ Force-close paths (endscreen exit/next/start, MP match start, co-op stage change, MP stage change, stage-transition nuclear reset) release every pool slot cleanly.
- ✅ Pool API supports input-context ownership tied to slot lifecycle for renderers that opt in.
- ⚠️ Shared-action leak (ADR §3.5) is NOT fixed this session — it requires per-scope state arrays in the actionmap, which is a separate architectural change.

**Build verify**: direct worktree build via `cmake -G Ninja -DCMAKE_C_COMPILER=/c/msys64/mingw64/bin/cc.exe -DCMAKE_CXX_COMPILER=/c/msys64/mingw64/bin/c++.exe -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache ..` then `ninja -C Build pd pd-server`.  751/751 targets; `PerfectDark.exe` 51,502,778 bytes; `PerfectDarkServer.exe` 22,817,578 bytes. Only pre-existing `'/*' within comment` warnings and `f32 near/far` macro-name warnings; no new ones.  Note: the `build-headless.ps1` script is designed to redirect worktree invocations to the main working copy, so direct `cmake + ninja` is the canonical worktree build.

**Not done in this session**:

- ImGui renderer migration to pool-owned ctx (the 10 `s_*PushedCtx` boolean patterns). Deliberate deferral — the existing pattern is stable and touches ~20 renderers. Future sessions can migrate one renderer at a time, converting `s_FooPushedCtx` into `menupoolAcquire(MENU_TYPE_FOO, def, &g_CtxImGuiMenu)` / `menupoolRelease(MENU_TYPE_FOO)` and removing the bool.
- Per-scope state arrays for shared actions (ADR §3.5 follow-up). Still open.
- Unit / integration tests for the pool (ADR §6.3). No automation for input/menu layer exists; any regression would still surface the hard way.

**Next**: playtest verification — open/close every major menu (main menu, cheats, MP setup, MP pause, endscreen), confirm no duplicate-push warnings in `pd.log`, confirm force-close paths (retry mission, next mission, exit to main menu) log `MENUPOOL: released N slot(s) (bulk)` and the stage transition completes cleanly.

---

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
