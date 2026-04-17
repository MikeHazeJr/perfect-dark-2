# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. Completed work lives in `session-log.md`,
> `bugs.md`, `daily-logs/`, or `_archive/tasks-archive.md`.
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Done — 2026-04-17 (S326 — Asset Provider Phase 1 + 2, `jolly-booth-fb5419` worktree)

**Build verified.** Clean link 771/771, zero errors. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean.

**Phase 1 — Provider interface (zero behavior change):**
- New `port/include/assetprovider.h` defines `asset_data_handle_t` (opaque 128-bit payload + vtable pointer) and `asset_provider_t` vtable (`resolve_size` / `load` / `unload` / `describe`)
- New `port/include/assetload.h` declares `assetLoad` / `assetLoadToNew` / `assetUnload` / `assetDescribe` — the provider-aware dispatcher entry points
- New `port/src/assetprovider_rom.c` — `RomProvider` wraps the existing `romdataFileLoad` path; `opaque[0]` = filenum
- New `port/src/assetprovider_file.c` — `FileProvider` serves loose files via `fsFileLoad`; paths interned into a 32 KB pool so handles stay 128-bit regardless of path length
- New `port/src/assetload.c` — dispatcher; RomProvider fast-path delegates to `fileLoadRomToNew` (legacy body), generic path does `mempAlloc(MEMPOOL_STAGE) + provider.load`
- `src/game/file.c::fileLoadToNew` becomes a one-line wrapper: `return assetLoadToNew(romProviderHandle(filenum), method, loadtype);`. Original body moved to `fileLoadRomToNew` (declared in `src/include/game/file.h`) so the dispatcher avoids recursion. Every existing call site works unchanged.
- Server stubs added in `port/src/server_stubs.c` for the 6 provider entry points (return null handles; server never dispatches through the provider layer)

**Phase 2 — Catalog source descriptor + mod-override provider selection:**
- `asset_entry_t` gains an `asset_source_t source` field with `primary` / `override` handles + a `flags` bitmask
- New `catalogSetPrimary` / `catalogSetOverride` / `catalogClearOverride` / `catalogEffectiveHandle` API (`port/include/assetcatalog.h`, implemented in `port/src/assetcatalog.c`)
- Registration populates `source.primary` declaratively:
  - `assetcatalog_base.c` — every base body/head/sp entry binds to `romProviderHandle(source_filenum)`
  - `assetcatalog_base_extended.c` — base prop models bind to `romProviderHandle(g_ModelStates[i].fileid)` when `fileid > 0`
  - `assetcatalog_scanner.c` — mod characters with `bodyfile` bind to `fileProviderHandle(bodyfile)`
- `entryGetFilePath` in `assetcatalog_load.c` consults `source.primary` first: if it holds a FileProvider handle, the interned path wins over the type-specific `ext.*` fields. Legacy fallback retained for entries with no populated source.
- Net effect: the mod-override path that used to live inside `romdataFileLoad` now flows through declarative `asset_source_t` fields. `catalogResolveFile` still routes by reverse-index, but the file path it returns comes from the provider handle instead of a type-specific switch.

### Playtest verification

- **Base game cold boot** — launch PerfectDark.exe, boot to main menu. No new log errors. All character models, arenas, stages load.
- **Mod loading** — install any component mod that overrides a base character (e.g. any ASSET_CHARACTER with a bodyfile matching a ROM file name). Load CI Training. Verify the mod character body still loads from disk (not ROM). Log line `CATALOG: file N → mod override "..."` should still appear in `pd-client.log`.
- **Dedicated server** — launch PerfectDarkServer.exe. Boot should proceed normally; catalog registers with null provider handles (server has no ROM/disk assets). No crashes on catalog population.

### Follow-up (not in this session)

- Phase 3: migrate call sites from `fileLoadToNew(filenum, ...)` to `assetLoadToNew(handle, ...)` so the catalog becomes the sole entry to loads
- Phase 4: retire `filenum` from public catalog API once Phase 3 completes
- `FileProvider.load` inflate/preprocess path: currently the generic `assetLoadToNew` path skips rzipInflate + `romdataFilePreprocess`. Acceptable for Phase 2 because mod-override bytes still flow through the legacy `romdataFileLoad` pipeline that does its own preprocessing. Phase 3 migration will need to either inline the inflate/preprocess steps into `assetLoadToNew` or push preprocess responsibility to callers.

---

## Done — 2026-04-17 (S325 — D6 stats wire-in + ImGui subtitles, `xenodochial-mendel-93fca2` worktree)

**Build verified.** Clean link 769/769, zero errors. Two parallel tasks:

**Task 1 — D6 Persistent Stats gameplay wire-in complete**:
- MP matches: `mp.matches_won` / `mp.matches_lost` (in `mpCalculateAwards`, local players only)
- Time: `mp.time_played_seconds`, `solo.time_played_seconds`
- Solo missions: `solo.missions_completed`, `solo.mission_failures` (via `endscreenPrepare`, gated on !cheats !coop !anti !pdmode)
- Distance: `distance_units` + `mp.distance_units_sample` / `solo.distance_units_sample` (flushed per 10000 world units to avoid per-tick churn)
- Pickups: `items.picked_up` (always) + `items.keys_picked_up` / `ammo_crates` / `weapons_picked_up` / `shields_picked_up` (by object type)
- Doors: `doors.opened` (via `doorsCheckAutomatic` — player-triggered auto-open path)
- `statsSave()` called on match end and solo mission end
- D6 marked **DONE** in infrastructure.md

**Task 2 — Subtitle ImGui migration complete**:
- New `port/include/pdgui_subtitles.h` + `port/fast3d/pdgui_subtitles.cpp`
- New bridge `pdguiSubtitlesSnapshot` (pdgui_bridge.c) exposes active HUDMSGTYPE_INGAMESUBTITLE / HUDMSGTYPE_CUTSCENESUBTITLE entries as POD structs (no types.h exposure)
- Renders bottom-center panel (46 px margin), 560 px wide (scales with `pdguiScale`), semi-transparent rounded backdrop, 1-px drop shadow on text, text centered + word-wrapped
- Drawn on foreground drawlist so cutscene letterbox bars don't occlude
- `hudmsgsRender` now skips both subtitle types (ImGui is sole renderer); tick lifecycle (fade, audio-channel opacity) still runs in `hudmsgsTick`
- Hook point: `pdguiRender` in `pdgui_backend.cpp` after `pdguiHudRender`

### Playtest verification

- **Solo subtitle (in-game)** — start any SP mission that triggers an in-game subtitle (mission briefings, ambient dialogue). Text should appear at bottom-center with a dim rounded backdrop, NOT at the top.
- **Cutscene subtitle** — play through any mission with a cutscene. Subtitles render at bottom-center and are not occluded by the letterbox bars.
- **Font/theme sync** — change font or theme; subtitle panel should update immediately (uses active ImGui font + theme tint).
- **Stats persistence** — play a match, check `saves/playerstats.json` for new keys: `mp.matches_won`, `mp.time_played_seconds`, `items.picked_up`, `doors.opened`, etc.
- **Solo mission stats** — complete a solo mission, verify `solo.missions_completed` incremented; abort/die, verify `solo.mission_failures`.

---

## Done — 2026-04-17 (S324 — D-MEM M2 + M4; infrastructure.md M3 marked done)

**Build verified.** Clean 771/771, zero errors, both PerfectDark.exe and PerfectDarkServer.exe link clean. Changes:
- **M2 stack→heap** (3 files): `pak.c` `sp60[0x4000]` → malloc/free; `texdecompress.c` `texInflateZlib`+`texInflateNonZlib` scratch buffers → static; `menuitem.c` `alltext/headingtext/bodytext/wrapped[8000]` → static
- **M4 ALIGN16 no-op**: `constants.h` ALIGN16 macro changed from 16-byte-round-up to `(val)` — removes N64 DMA padding from all 119 call sites
- **M3 marked DONE** in infrastructure.md: IS4MB ternary collapse completed S317/S320/S322/S323A

---

## Done — 2026-04-17 (S323 Batch G — cross-audit gap fixes)

**Build verified.** Clean link 768/768, zero errors. 6 items fixed:
- **CRITICAL**: `audioNotifyEngineReady()` now called in `port/src/pdmain.c::mainProc()` after `sndInit()` — `g_AudioEngineReady` is now set at runtime; volume sliders are no longer permanent no-ops
- `playerResetLoResIf4Mb` empty stub deleted (body in player.c, declaration in player.h, call in vi.c behind `#if PAL`, call in playerreset.c)
- Dead `is4mb` local variable removed from hudmsg.c (decl, assignment, always-false arm of condition)
- Orphaned `#define MAX_SEQ_SIZE_4MB` removed from snd.c
- Dead `g_BgunGunMemBaseSize4Mb2P` global deleted (definition in bondgun.c, extern in data.h, extern in bondgunreset.c)
- Stale "takes effect on next restart" tooltip removed from pdgui_menu_mainmenu.cpp font panel

No playtest needed — audio volume fix is functional; rest is dead-code removal.

---

## Open — 2026-04-17 (S323 Batch D+F — font atlas rebuild + legacy sidecar migration)

### Playtest verification

- **Font live swap** — open Settings → Interface tab → Font dropdown. Change to any installed font mod. Font should update immediately in the current session (no restart required). Change back to Handel Gothic — should also update immediately.
- **Legacy sidecar migration** — if any agents were created before S313, their old sidecar (named from raw N64 bytes, typically `prefs_default.ini` or a garbled name) should auto-migrate to `prefs_<display_name>.ini` on first Agent Select load. Log should show `PREFS: migrated legacy sidecar '...' -> '...'` if migration fires.

---

## Done — 2026-04-17 (S323 Batch A — IS4MB/IS8MB/STAGE_4MBMENU final cleanup, `admiring-mccarthy-38734a` worktree)

**Build verified.** `grep -rn "IS4MB|IS8MB|fourmeg2player|STAGE_4MBMENU" src/ port/` returns zero hits. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean [770/770]. No playtest needed — pure dead-code removal with no runtime behavior change.

---

## Open — 2026-04-17 (S323 — menuPushRootDialog pool hygiene, `naughty-buck-8ede1d` worktree)

### Playtest verification

- **Cold boot into main menu** — open main menu, navigate to Settings, close.  Re-open main menu.  No "menuPushDialog rejected — pool slot already active" in log.  Menu is interactive on every open.
- **CI redirect at boot** — if CI Options dialog is pushed at boot (Settings via CI path), close it, return to main menu.  Watchdog in `menuPoolConsistencyCheck` should log nothing (previously logged stale-slot warning on the frame after menuPushRootDialog wiped the stack).
- **Pause → Main Menu transition** — pause in-game, return to main menu via "Exit to Main Menu".  Pool should be cleanly released before the root push.  No "pool slot already active" cascade on subsequent main menu opens.

---

## Open — 2026-04-17 (S323 — Audio channel routing enforcement)

### Playtest verification

- **Mod SFX override + GameplayVolume** — requires a mod with a sound override (`catalogResolveSound` returning `is_mod_override=true`).  Set GameplayVolume to 25%.  Trigger the overridden sound.  It should play at roughly 25% of full volume.  Without the fix it would play at 100% regardless.  Check `pd-client.log` for `CATALOG: sound %d → mod override` line confirming the WAV path was taken.
- **Per-agent audio isolation** — load Agent A, set MasterVolume to 50% via Settings → Audio. Quit to Agent Select (do not sign out — just press Escape from main menu to reach Agent Select). Screen should open and audio volumes should revert to pd.ini defaults (`AUDIO.DIAG` in log shows `audioNotifyEngineReady` baseline). Select Agent B (no custom prefs). Audio should stay at baseline, NOT at Agent A's 50%.
- **Agent A volumes reload on sign-in** — re-select Agent A.  MasterVolume should return to 50%.

---

## Open — 2026-04-17 (S322 — N64 legacy audit Tier 1/2)

### Playtest verification

- **Cold boot** — boot log clean (no IS4MB path taken, no audio init errors). Menu renders normally.
- **Audio** — music plays in multiplayer lobby. No audio crackling or voice-limit errors in log.
- **Multiplayer** — 2-player split-screen works. No crash from fourmeg2player removal or screensplit logic.
- **Stage loading** — load any gameplay stage; no crash from MEMP expansion pool change or model/texture count increases.
- **Menu blur** — menu blur effect renders (IS8MB guard was removed; `g_BlurBuffer` allocated unconditionally).

---

## Open — 2026-04-17 (S321 — N64 demo system stripped)

### Playtest verification

- **Cold boot** — boot log shows single `SP transition to 0x26`, no `0x30` reference anywhere. Boot path: logo sequence → CI Training, clean.
- **Mission cutscenes** — play to a cutscene completion. Mission should end normally (`func0000e990` path, not title-redirect). Music should resume after cutscene (previously guarded by `!g_IsTitleDemo`).
- **Defection mission** — playable normally via solo mission select. The STAGE_DEFECTION mission itself is unaffected.

---

## Open — 2026-04-17 (S320 — Agent Select default theme)

### Playtest verification

- **Agent Select shows base theme on open** — after playing as a custom-themed agent, return to Agent Select (Escape from main menu). The screen should render with `base:theme_blue` (default grey/blue PD palette), not the previously-active agent's custom theme.
- **Theme transition visible on sign-in** — select an agent with a custom theme. The moment the agent is signed in, the UI should visually transform to their theme.
- **Auto-load default agent** — if a default agent is configured (D key), Agent Select should immediately load that agent's theme on first open (no flickering required since it happens same frame).

---

## Open — 2026-04-17 (S319 — spurious boot transition fix)

### Playtest verification

- **Cold boot double-transition eliminated** — `titleInitRareLogo` no longer sets `g_IsTitleDemo = true`. Boot should no longer log `MAIN: replacing pending stage change 0x30 -> 0x26`. Verify the boot log shows a single `GAMELOOP.MANIFEST: SP transition to 0x26` on cold start.

---

## Open — 2026-04-17 (S318 — B-161 class bbox/modeldef NULL sweep)

### Playtest verification

- **Hoverbike dismount** — activate near a hoverbike; dismount should succeed normally. No regression.
- **Glass destruction** — shoot AI Villa Bot glass objects; shard spray should fire. If bbox is missing, object removes silently (no crash, no shards — acceptable).
- **Door interaction range** — approach and open doors in any SP/MP level normally; no input-eating regression.
- **Hoverbike/hoverprop collision** — hoverbike/hoverprop geo block built correctly; collides with walls as expected.
- **Chr bbox render** — no visual regression in character sorting / chr hitbox computation.

---

## Open — 2026-04-17 (S317 addendum — CI Training crash fix, dev direct)

### B-163: Playtest verification

**Status**: Crash guard landed. `setupCreateDoor` now returns early if modeldef NULL and uses identity scale if no bbox node. Build verified. Needs in-game test.

- **Launch CI Training** — game must reach gameplay without AV. If a door is missing, check log for `SETUP: door modelnum %d modeldef NULL` or `no bbox node` to identify which door
- **Double-transition 0x30 → 0x26** — still open: investigate what triggers stage 0x30 at boot and whether CI Training redirect is affected

---

## Open — 2026-04-17 (S316 — solo mission select UX, `epic-mirzakhani-884cc2` worktree)

### Playtest verification

- **Difficulty text in mission select** — launch solo mission list, select any mission. All three difficulty rows (Agent / Special Agent / Perfect Agent) must show their names even before the lang bank loads (fallback strings hardcoded).
- **Dark Agent row** — appears only after Skedar Ruins beaten on Perfect Agent. Selecting it and clicking Start Mission should open PD Mode settings dialog, not launch immediately.

---

## Open — 2026-04-17 (S315 — palette sweep completion, `pedantic-austin-4a93bc` worktree)

### Playtest verification

- **Theme-driven accents sweep verification** — install a custom theme with a wild `titleGlow` (e.g. pure magenta). Confirm the following now follow the theme accent instead of staying cyan:
  - **Lobby screen** — "Connected to dedicated server" banner; "In Match" / "in game" client state labels; ROOM_STATE_MATCH server browser entry
  - **Pause scoreboard** — "SCOREBOARD" header and local-player name row (cyan → theme accent)
  - **Room** — "in game" client state label, tab bar selected-tab overline, bot name in character preview panel, TabSelectedOverline
  - **Challenges** — "Completion:" section subheader
  - **Download overlay** — "Downloading: <component>" label in the distrib progress banner
- **Intentional non-theme colors remain** — verify these are still their original fixed colors, not theme-colored:
  - Solo Mission difficulty ring: Agent=green, Special Agent=blue, Perfect Agent=gold
  - Log viewer: `LOAD:` lines still blue (functional debug coloring)
  - Agent Select initials: still light blue `IM_COL32(200, 220, 255, 255)`

---

## Open — 2026-04-17 (S312 batch 2 — font/UI scaling + controller tab-cycle + border audit)

### Playtest verification

Three renderers touched; no user-visible regression expected.  Verify:

- **Input Mapping** (Settings → Controls → Keyboard & Mouse / Controller)
  on 1440p + 4K — search box, group headers, and row cells should look
  proportional; before the fix the box stayed at 260px regardless of
  display resolution.  Row height/padding at high DPI should no longer
  feel cramped (table cell/frame/item padding now scales).
- **Updates tab** (Settings → Updates) on 1440p + 4K — "Channel" combo
  width and horizontal spacing between the current-version text and the
  combo should scale with DPI.  Restart prompt's "Restart Now / Later"
  buttons should separate proportionally.
- **Mod Manager tab cycling** (Modding Hub → Mod Manager) with a
  controller connected — LB/RB should cycle Installed Mods → By Category
  → By Mod and back.  The three-tab bumper cycle should be round-robin.

### Follow-up queued from S312 batch 2

- ~~**Wire pdgui_glyphs into in-world prompts**~~ — **DONE (S323 audit confirmed)**:
  S311 wired `pdguiInteractPromptRender` → `pdguiDrawActionPromptCentered(ACTION_USE)` for all
  pickup/door/terminal/object prompts. S312 wired forge HUD via `pdguiDrawActionPrompt()`.
  Zero hardcoded `[E]`/`[A]` labels remain in runtime rendering code.
- **Font-atlas rebuild on runtime font swap** — atlas built once at
  `pdguiInit`; runtime swap needs a rebuild or early-pick to take
  effect without restart.
- **Theme Editor mini-preview content-inset** — mini dialog in the
  preview swatch doesn't use content-inset; it uses fixed headerH.
  Cosmetic-only.

---

## Open — 2026-04-17 (S313 marathon follow-up batch -- `great-robinson-15f409` worktree, merged to dev)

Three commits on top of the S311/S312/S313 three-way merge:

- `8057f064` -- **FORGE → GRID log prefix rename** across `forgemode.c` + all `port/src/forge/*.c` files + `pdgui_menu_forge.cpp`.  User-facing rename now complete: HUD badge already read "THE GRID", editor title already read "The Grid", log channel now reads "GRID:".  Internal code names (`forge_*`, `FORGE_MAX_*`, catalog namespaces, file paths) retained.
- `1694d3ec` -- **Audio volumes move to per-agent prefs**.  `prefs_agent.c` gained `[Audio]` block.  Agent Select load applies `MasterVolume` / `MusicVolume` / `GameplayVolume` / `UIVolume` via the corresponding `audioSet*Volume` setters.  Global pd.ini keys remain as per-machine defaults.  Also fixed **agent name handoff** -- `prefsLoadForFile` now uses `gamefileGetOverview` so sidecar filenames match Agent Select display text exactly.
- `6aaf299f` -- **pd.ini audit cleanup**.  Dropped `configRegister` for `Net.LerpTicks`, `Net.Client.{InRate,OutRate,UpdateFrames}`, `Net.Server.{Port,InRate,OutRate,UpdateFrames}`, `Update.ProtectedFolders` -- these are tuning knobs, not user prefs.  Globals retain their file-scope initializers so runtime code still works and `-port` CLI override still works.  New doc `context/config-pd-ini-audit.md` catalogs every remaining `configRegister*` call with a three-tier model + decision flowchart.

### Playtest verification

1. **Log prefix**.  Launch client, tail `pd-client.log`, press F7 to toggle The Grid freefly.  Every line that used to read `FORGE:` / `FORGE.LOGIC:` / `FORGE.SERIALIZE:` etc. should now read `GRID:` / `GRID.LOGIC:` / `GRID.SERIALIZE:`.  No "FORGE" strings should appear in the log for Grid operations.
2. **Per-agent audio volumes**.  Create Agent A, go to Settings → Audio, slide Master to 25%.  Create Agent B, set Master to 100%.  Quit and relaunch.  Load Agent A -- volume should be 25%.  Load Agent B -- volume should be 100%.  Open `saves/prefs_AgentA.ini` / `prefs_AgentB.ini` and confirm the `[Audio]` section reflects each setting independently.
3. **Agent-name handoff**.  Create an Agent with a long or special-char name.  Confirm `saves/prefs_<visible_name>.ini` is written with exactly the display text (not the raw encoded byte sequence).  Legacy agents from before this fix may orphan their old sidecar -- one-shot migration queued as a follow-up.
4. **pd.ini cleanup**.  Delete pd.ini.  Launch client.  Launch dedicated server with `--port 27123`.  Client connects.  Game plays normally.  Stop + restart client -- network tuning, server port, and protected folders all pick up their compile-time defaults; no warnings.
5. **Context doc**.  Open `context/config-pd-ini-audit.md` and verify the three-tier model section matches the actual configRegister surface (run `grep -rn configRegister port/ | wc -l` and spot-check).

### Deferred

- **Legacy sidecar migration** -- agents created before the `gamefileGetOverview` fix may have `prefs_<encoded>.ini` files that no longer match their new `prefs_<overview>.ini` path.  Small one-shot migration on first Agent Select load is queued.
- **Gameplay-preference pd.ini → per-agent** -- `Game.CenterHUD`, `Game.SkipIntro`, `Game.DisableMpDeathMusic`, `Game.GEMuzzleFlashes`, `Game.ScreenShakeIntensity`, `Game.MenuMouseControl` are candidates for the per-agent sidecar.  Listed in `config-pd-ini-audit.md`.
- **Audio.ModPlaylist / ModShuffle / ModTrackId → per-agent** -- each Agent could own their own Combat Simulator music selection.

---

## Open — 2026-04-17 (S311 — UI polish marathon, `zen-poitras-f86b73` worktree → merged to dev)

### Playtest verification of the S311 theme palette sweep

Commit `55c37fc2` + merge. Build: `PerfectDark.exe` 52,288,651 / `PerfectDarkServer.exe` 22,838,513 (pre-S312/S313 merge).

- **Theme-driven title glow** — install a custom theme with a deliberately wild `titleGlow` (e.g. pure magenta `#ff00ff`). Open each of these dialogs and confirm previously-cyan accents now follow the custom color:
  - **Warning dialogs** — default/info dialog and the **MP End Game modal** (pause → End Game → Confirm). Title glow should honour the custom tint via `pdguiImU32TitleGlow`.
  - **File Manager** (agentselect → Copy) — the "Copy agent" prompt follows the custom theme; the "Delete agent" prompt stays red (`pdguiImU32TintDanger`).
  - **Pause-menu rankings** — local-player row tint follows the custom color (35 alpha).
  - **Section headers** — Lobby, Network, Challenges, Team Setup, MP Settings → Select Tunes, Room (Bot Settings / Custom Traits / Save/Load Scenario / Level Editor / Properties), Main Menu (Recent Servers / D5.0a catalog). Every cyan section header follows the custom theme.
  - **Moddinghub + Audiomod + Controldiagram + Modmgr** — selected-entry headers (Audio Mod "Selected: ...", Modding Hub ini/skin entry headers, Control Diagram control-mode name, MP Settings "Library" header) follow the custom tint. Mod Manager "Base Game Asset" label follows `pdguiVec4TintInfo()`.
- **Delete-confirm button scaling** — Settings → Interface → right-click a user theme → Delete Theme. Cancel/Delete buttons scale with resolution (`pdguiScale(120.0f)`).
- **MP End Game content inset** — load a Menu Style mod with thick border corners, trigger the End Game modal. Body text clears the chrome border by the 8px breathe regardless of corner thickness.
- **CS music picker still works** — import a `.mp3` via Modding Hub → Audio Mods → Music → Import. Launch Combat Simulator → MP Settings → Soundtrack → Select Tunes. Track appears. Log carries `SELECTTUNES: open — base_tracks=N mod_tracks=M` + `AUDIOMOD: auto-enabled mod '...'`.
- **Controller nav** — with a controller connected, navigate main menu / Settings tabs / Room screen via D-pad + A + B + LB/RB. All work transparently via `pdguiDriveImGuiNav()` action-map → keyboard-nav translation.

### Follow-up queued from S311

- **Remaining non-blue literal sweep** — agentselect/agentcreate/solomission/modmgr/moddinghub still have PD-blue `IM_COL32` panel/border decorations plus red/yellow/green semantic literals that could fold into `tint_danger`/`text_warning`/`text_positive`.
- **Dead `ImGuiKey_Gamepad*` checks** — ~130 redundant checks across menu files. Harmless (Enter/Escape parallels catch the edges via action-map→keyboard translation). Removing is a ~1h churn task.
- ~~**renderCiSettingsRedirect / renderCiDeadPlayer2 / renderCinemaList S300 pool-ctx migration**~~ — **DONE (S311+/S323)**. All three use `menupoolAcquireDialog` + `pdguiConsumeTitleClose`. The defensive `inputCtxPopDeferred` at mainmenu.cpp:3414 is retained as an idempotent belt-and-suspenders guard.
- ~~**pdgui_lobby.cpp / pdgui_lobby_distrib.cpp / server_gui.cpp / pdgui_skin_editor.cpp blue-tint sweep**~~ — **DONE (S311 pt2 + S315)**. `server_gui.cpp` intentionally skipped (pd-server doesn't link pdgui_style). All semantic cyan/blue accents across all non-server UI files now use `pdguiVec4TitleGlow()`. Remaining hardcoded blues are intentional: log-viewer LOAD category (debug), Solo Mission difficulty colors (PD identity), agentselect initials (artistic).

---

## Open — 2026-04-17 (S312 — Modeldef guards + glyph system + net review, `amazing-mccarthy` worktree)

### Playtest verification of the S312 drop

Build: `PerfectDark.exe` 52,400,200 / `PerfectDarkServer.exe` 22,840,561.

- **Modeldef defensive guards (B-161 reinforcement)** — Replay the B-161
  repro: Defection (0x30) → Next Mission → Investigation (0x33), walk 1-2
  minutes.  Tail `pd-client.log` for either:
  - `DOOR.DIAG: doorGetBbox — no bbox for modelnum=...` (S308 guard) or
  - `MANIFEST-SP: late-add '...' post-load modeldef torn: parts=%d root=%p
    scale=%.3f` (new S312 diagnostic — identifies the exact catalog id
    whose late-add produced the torn state), or
  - `modelFindBboxNode: walker exceeded 10000 steps` / `modeldefFindBboxNode:
    walker exceeded 10000 steps` (new S312 guard — fires on a cyclic
    or dangling rootnode tree).
  No crash should occur.  The diagnostic fingerprints are the handoff for
  root-cause investigation.
- **Glyph system smoke** — no UI surface consumes it yet; the module
  compiles and exposes the API:
  - `pdguiGlyphGetDevice()` returns KBM or GAMEPAD based on the
    actionmap's 500 ms debounce.
  - `pdguiGlyphGetPrimaryVk(ACTION_USE)` returns the primary VK for the
    current device (E on KBM default, A on gamepad default).
  - `pdguiGlyphGetActionLabel(ACTION_USE, buf, sizeof buf)` writes "E"
    or "A" depending on device.
  - `pdguiDrawActionPrompt(ACTION_USE, x, y, "Use")` draws a pill.
  Pull the header via `#include "pdgui_glyphs.h"` (port/include on path);
  any C or C++ TU can call it.

### Follow-up queued from S312

- **Wire glyphs into in-world prompts** — obvious callers: pickup
  prompts (`[E] Pick up AR34` / `[A] Pick up AR34`), door prompts,
  terminal interact prompts, forge HUD controls reminder.  Currently
  the HUD uses hard-coded labels.
- **Root-cause fix for modeldef corruption class** — the S312 late-add
  diagnostic should produce enough log evidence in the next repro to
  pinpoint either the catalog id whose load is torn or a post-late-add
  corruptor.  Landing an actual fix (e.g. reloading the modeldef when
  numparts=0 is detected) is the next step.
- **Per-player glyph device** — if splitscreen is ever re-enabled,
  `pdguiGlyphGetDevice` needs a per-player signal.  Not a concern
  today (single local player only per constraints.md).

---

## Open — 2026-04-17 (S310 — The Grid editor F1-F8 bulk drop, `sharp-lovelace` worktree)

### Playtest verification of The Grid editor overlay

Build: PerfectDark.exe 52,176,729. Two commits in the `sharp-lovelace-a08d90`
worktree (`0732c806` and `ac7be593`). Files touched:
`port/include/forge/forge_core.h`, `port/src/forge/forge_{core,undo,logic,gametype,serialize,ai}.c`,
`port/fast3d/pdgui_forge_editor.cpp`, plus small edits to
`port/fast3d/pdgui_backend.cpp`, `port/src/pdmain.c`,
`port/include/pdgui_forge.h`, `src/game/setup.c`.

**Primary repro -- editor overlay in FREEFLY**:

1. Launch client; click "Forge" on main menu. CI Training loads in NORMAL play mode.
2. Press **F7** to toggle into FREEFLY. Expect a large "The Grid -- Editor"
   window to appear on the right side of the screen with 8 tabs:
   Catalog, Properties, Zones, Lighting, Logic, Game Type, Mission, Settings.
3. **Catalog tab**: search box, category filter dropdown, budget bar
   (green/yellow/red by soft/hard cap), collapsible category headers
   (Geometry, Props, Weapons, Spawn Points, Pickups, Lighting, Effects,
   Zones, Interactables, Characters). Clicking any button places an
   object ~400 units forward from the freefly camera.
4. **Properties tab**: select an object (click one in the catalog or via
   the selection UI), see its transform + type-specific props. Numeric
   transform editing and duplicate/delete buttons work.
5. **Zones tab**: quick-create Trigger/Kill/Teleporter; table lists all
   placed zones. Select/X buttons per row.
6. **Lighting tab**: skylight direction/color/intensity, atmosphere
   (fog/ambient/exposure/bloom), weather (rain/snow/sandstorm/storm),
   ToD cycle enable, sky catalog ID input.
7. **Logic tab**: add event/condition/action nodes (Op dropdown filters
   by kind), table of nodes with Fire button (triggers execution for
   events), wire create by src/dst UID, channel management (add,
   toggle, delete).
8. **Game Type tab**: name/description, structure (single/BoN/wave/phase),
   win condition, scoring rules, starting weapon, health mult, role mode
   (None/Infection/VIP/Juggernaut/HordeDefender/GunGameHunter), 8
   modifier flags, HUD toggles, wave spawner table with per-wave
   enemy/scale/hp/spawn-zone, boss state with simulate button.
9. **Mission tab**: Is-Mission checkbox, briefing/debrief text,
   sequential toggle, Add Primary/Secondary/Bonus objective buttons,
   table with desc + link-node UIDs + Complete/Fail/X buttons.
10. **Settings tab**: map metadata, players, default rules, bounds,
    grid/snap prefs, editor toggles, budget summary, Save-as-Mod +
    Load-from-Mod + Reset Map.

**Persistence**: Settings → type a name → click Save As Mod. Expect
`mods/Forge Maps/<slug>/mod.json` and `map.json` written. Reload via
Load From Mod restores the map metadata + settings.

**Primary repro -- MP elevator fix (`setup.c`)**:
Start a Combat Simulator match on any arena with a lift (Area 51,
Complex, or any CI Training variant with an elevator). Walk onto the
lift pad. Expect the lift to move between its stops. Prior behavior:
lift was completely dead in MP because it was never registered in
`g_Lifts[]`. Solo missions are unaffected (AI script `aiActivateLift`
still re-registers idempotently).

### S310 R1-R4 addendum (Mike refinements landed commit `833c3a95`)

- **R1 seamless swap**: F7 toggle confirmed non-stage-reloading.
  Verify via log stream -- should see `FORGE: freefly body-swap`
  / `FORGE: normal body-restore` pairs on each toggle.
- **R2/R4 weapon source**: Settings tab > "Weapon Source" dropdown.
  Pick MAP_DEFAULTS, save, reload, confirm round-trip.  When match
  setup UI lands the hook point is `s->weapon_source` +
  `s->allow_match_override`.
- **R3 dependencies**: Settings tab > live "Mod Dependencies" section
  shows bullet list; save a map that references a modded weapon,
  then grep `mods/Forge Maps/<slug>/mod.json` for `"dependencies":`
  array.  Distribution pipeline recursion lands with the mod-transfer
  integration pass.

### Follow-up queued from S310

- **Engine-level Dr. Carroll visual swap**: chr->bodynum + headnum
  are now assigned correctly on entry/exit (so saves + MP sync carry
  the right value), but the live mesh re-skin requires a
  `bodyAllocateModel` call on the new pair + a model-tree rebuild.
  Comes in a follow-up polish pass.
- **3D gizmo handles** -- the Properties tab edits transform
  numerically; the design doc §4.2 calls for drag-axis gizmos. Needs
  freefly-camera raycast + in-world axis rendering. Data model supports
  this already.
- **Placement reticle** -- clicking in the Catalog places the object
  ~400u forward from the camera. Full ghost reticle with
  valid/invalid tint needs a forge-owned input tick + click capture.
- **Runtime engine wire-in (partial — S314)** -- SPAWN_POINT objects inject
  into the live spawn pool via `spawnPoolAppendForgePoints`; AI objects spawn
  live bots via `botmgrAllocateBot`. Remaining deferred: placed props/weapons/
  geometry/doors/zones still log-only (need `propAllocate` + model wire path).
  `FORGE_OP_TELEPORT_PLAYER` + `SPAWN_AI` + `ENABLE/DISABLE_OBJECT` are now
  live-engine calls. `FORGE_OP_OPEN_DOOR` still only clears the data-model
  locked flag; `doorActivate()` needs the prop handle wired first.
- **Prop/weapon pad visual spawning** -- bots + spawn points are live (S314);
  floor decor / pickups / geometry still deferred. Need `propAllocate` path
  separate from `setupCreateObject` (which requires full intro-command context).
- **Door prop instantiation** -- `FORGE_CAT_INTERACTABLE` objects are logged
  but not spawned as live engine door props. Needs `propAllocate` + door model
  allocation + `doorActivate` wiring before `OPEN_DOOR` logic action is fully live.
- **AI navmesh generation** -- `forge_ai.c::forgeAiGenerateNavmesh` is a
  stub that logs counts. Full navmesh auto-gen from placed geometry is
  a post-F8 polish pass.
- **Boss health bar HUD** -- data model tracks boss state; the HUD
  drawing for the full-width bar belongs in `pdguiForgeHudRender`
  when `forgeBossState()->active`. Currently only shown in the Game
  Type tab's editor UI.
- **Terrain brushes** (F8 stretch) -- catalog entries for floors /
  ramps / platforms exist. True height-paint terrain is out of scope
  here and would require a new mesh-edit pipeline.
- **Co-op Forge** (F8 stretch) -- multiple Dr. Carroll editors syncing
  edits over the wire. Design doc §13.2 sketches the lock-per-object
  protocol; requires new SVC/CLC msgs.

---

## Open — 2026-04-17 (S309 — optimistic-feistel worktree → merged to dev)

### Playtest verification of the S309 deferred UI drop + config cleanup

Full write-up: `session-log.md` S309. Build: `PerfectDark.exe` 51,916,376 /
`PerfectDarkServer.exe` 22,840,561 bytes.

- **Content-inset sweep** — open each of the 20 migrated menus (agentcreate,
  agentselect, botsetup, challenges, cheats (3 dialogs), controldiagram,
  lobby, mpadvanced, mppause, mpsettings (2), mpsetup, network,
  playerconfig, room, solomission (11 dialogs), stats, teamsetup, training,
  warning (3)) with a Menu Style mod that has thick borders. Content
  (buttons, lists, labels) should clear the nineslice artwork by at least
  8px of breathing room on each side.
- **Theme Editor live preview** — open Theme Editor, observe the new
  preview column to the right of the color pickers. Edit Main Accent,
  Title Text, Title Glow (new S309 slot) — preview updates live. Edit
  Success / Danger / Info tints — three colored buttons reflect the new
  values.
- **Title bar blue tint customizable** — with a custom theme active, edit
  Title Glow (S309 slot). Click "auto" to return to derived-from-border2
  default; confirm glow matches border2 color.
- **Font Mod tool** — Modding Hub → Font Mod tab. Browse to a `.ttf`,
  enter a mod name, click Save. Verify `mods/Fonts/<slug>/<slug>.ttf` +
  `font.json` + `mod.json` exist. Settings → Interface → Font dropdown
  should list the new font without restart. Activating requires restart
  (ImGui atlas is session-once).
- **Scanline scaling** — at 720p, 1080p, 1440p, 4K, the scanline pattern
  should look visually consistent (1px/2px at 720p → 3px/6px at 4K).
  Check Settings → Video → Scanlines toggle; alpha slider works across
  all resolutions.
- **No imgui.ini** — launch game, quit. No `imgui.ini` should appear in
  the run directory.
- **Per-agent prefs** — create two agents (A, B). Sign in as A, pick a
  custom theme + menu style + title bar style. Quit, relaunch, sign in
  as B, pick different theme. Switch back to A — the theme should
  immediately revert to A's choice. Check `saves/prefs_<name>.ini` for
  both agents.
- **"The Grid" rename** — main menu should show "The Grid" button
  (where the "Forge" button previously was).
- **CS music picker fix** — import a `.mp3` or `.wav` via Audio Mods
  tab, select "Music" category, click Import. Verify status line
  confirms "Imported 'X' as <slug>:audio" + `AUDIOMOD: auto-enabled
  mod 'X'` in pd-client.log. Restart client, open Combat Simulator →
  MP Settings → Soundtrack → Select Tunes. Track should appear under
  "Mod Tracks (N)" in the left panel. Click → moves to right "Selected"
  panel. `SELECTTUNES: open — base_tracks=N mod_tracks=N` log line
  helps triage any future regressions.

### Follow-up queued from S309

- **Full pd.ini elimination** — only visuals + mod enablement moved to
  per-agent. Audio volumes, resolution, fullscreen, gameplay bindings
  still live in pd.ini. Needs a scope decision on per-agent input
  bindings.
- **Remaining hardcoded blue tints** — warning.cpp title text,
  agentselect state text, lobby state text, countdown overlay still
  use `IM_COL32(100, 200, 255, ...)` directly. Migrate them to
  `pdguiGetTitleGlow()`.
- **Forge HUD rename** — S310's pdgui_forge_hud.cpp still says "FORGE"
  in the mode badge. S310 owns that file.
- **Agent-name handoff** — `prefsAgentLoad` currently builds the key
  from `filelistfile::name[]` bytes, which may be encoded. Good enough
  for the current save format but replace with `gamefileGetOverview`
  so the filename matches what the user sees in Agent Select.

---

## Open — 2026-04-17 (S308 — dev direct)

### Playtest verification of B-161 (door-tick crash) + B-162 (pause menu hardening)

Commits on `dev`. All items build-verified; needs in-game playtest.

- **B-161 defensive crash guard** — Replay the exact repro from the bug report: Start Mission 1 Objective 1 (`base:defection` stage 0x30), complete it, click "Next Mission" on endscreen, start into Mission 1 Objective 2 (`base:investigation` stage 0x33), run around for 1-2 minutes. Expect: no crash. If crash moved elsewhere, look in `pd-client.log` for the new `DOOR.DIAG: doorGetBbox — no bbox for modelnum=... model=... flags=... doortype=...` WARNING — that line names the exact door's modelnum that has a NULL/torn bbox, which is the handoff for root-cause investigation.
- **B-162 pause menu hardening** — In Mission 1 Obj 2 (or any solo mission), open pause menu ≥3 times per mission. Confirm: (a) all 5 buttons always have visible labels (Resume / Restart Mission / Inventory / Settings / Abort Mission), never blank; (b) title reads "Investigation: Status" (or similar with the stage name) — never just ": Status"; (c) open Restart Confirm overlay, Escape to cancel, close pause, reopen pause — no Restart Confirm overlay bleeds in. Complete mission, advance to next mission, open pause — objectives list must be for the CURRENT mission (never shows leftover objective entries from the prior mission).

### Root-cause investigation queue (B-161 follow-on)

If the defensive fix holds but the `DOOR.DIAG:` WARNING fires repeatedly, the root cause is a modeldef being torn mid-gameplay. Playtest log from 2026-04-16 23:50 showed parallel `WARNING: body0f02ce8c: truly invalid bodymodeldef for bodynum 108 (file 0x004b) ... parts=0 -- skipping` at stage 0x33 load time — catalog says `base:sp_body_108` loaded successfully but modeldef has 0 parts. Two possible mechanisms:
1. Late-add manifest path (`MANIFEST-SP: late-add 'base:sp_body_108' type=0 (missed by pre-scan)`) loading the ROM bytes but not binding rodata correctly.
2. Catalog cache has a stub entry that shadows the ROM-load result.

Targeted instrumentation needed in `bodyAllocateModel` / `setupLoadModeldef` / `catalogResolveXXX` for late-add codepath to confirm.

---

## Open — 2026-04-17 (S307 — Forge F0 merged to dev)

### Playtest verification of Forge F0 Foundation

New modules merged from `claude/tender-sutherland-536de7` commit `8d412cc1`.
Full design ref: `context/designs/forge-level-editor-2026-04-16.md` §15 Phase F0.
Files: `src/include/game/forgemode.h`, `src/game/forgemode.c`,
`port/include/pdgui_forge.h`, `port/fast3d/pdgui_menu_forge.cpp`,
`port/fast3d/pdgui_forge_hud.cpp`; modifications to actionmap (5 new
`ACTION_FORGE_*`), `pdmain.c` (forgeTick wiring), backend.cpp (HUD dispatch),
`pdgui_menu_mainmenu.cpp` (top-level "Forge" button).

Build sizes: PerfectDark.exe 51,780,176 / PerfectDarkServer.exe 22,840,512.

**Primary repro -- Forge entry + mode toggle**:
1. Launch client. Open main menu (F12 from CI, or normal title-screen flow).
2. Click new "Forge" button (between Stats and Quit). Expect log
   `FORGE: launching session (base stage = CITRAINING 0x26)` + sound effect.
3. CI Training stage loads. Once player is wired, expect log
   `FORGE: -> NORMAL (session start (request))` + `FORGE: session active stage=0x26`.
4. Top-left HUD badge should show `FORGE -- NORMAL` in green.
5. Press **F7**. Expect `FORGE: -> FREEFLY (toggle press) pos=(...) yaw=...`,
   badge flips to cyan `FORGE -- FREEFLY`, centered crosshair appears,
   bottom-left readout shows pos / yaw+pitch / `spd NORMAL`, right edge shows
   placeholder "Object Catalog (F1: not yet implemented)" panel, bottom-right
   shows controls reminder.
6. **WASD** moves freefly camera relative to look direction. **Q**/**E**
   descend/ascend. **Mouse** looks (right-stick on controller). **LSHIFT**
   `spd BOOST`. **LCTRL** `spd PRECISION`. Pos readout updates live.
7. Camera should pass through walls/floors (no collision).
8. Press **F7** again. Expect `FORGE: -> NORMAL (toggle press)`. Player chr
   resumes at the freefly camera position; first-person controls work again.
9. Press **Esc** to open main menu. Forge session stays NORMAL. Click Quit.
   Stage transitions to TITLE. Expect `FORGE: -> INACTIVE (stage left gameplay)`.

**Health-signal log lines** (tail with `grep '^.*FORGE:' pd-client.log`):
- `FORGE: init` -- one-time at startup
- `FORGE: enter requested ...` -- on Forge button click
- `FORGE: -> NORMAL` -- on session entry or freefly exit
- `FORGE: -> FREEFLY` -- on freefly entry (with pos+yaw)
- `FORGE: -> INACTIVE` -- on stage leaving gameplay

**Absence is the failure signal**:
- No `-> FREEFLY` after F7 = action binding never registered or actionPressed
  not firing. Check `actionmapSaveBinds` output / `pd.ini` for ForgeToggle.
- No `pos` change in FREEFLY readout = `forgeUpdateFreefly` not running, or
  player chr's prop pointer was NULL.
- No badge after Forge button click = `forgeRequestEnterSession` was queued
  but stage transition didn't complete (g_MainChangeToStageNum stuck), or
  forgeTick is being called from wrong place.

### Follow-up queued from S307 (out of F0 scope)

- **F1 Object Catalog & Placement** -- left-side catalog tree, placement
  reticle with green/red validity tint, undo/redo, object budget tracking.
  See design doc §3 + §4 + §15 Phase F1.
- **Drop-to-ground on freefly exit** -- design says exiting freefly should
  fly Dr. Carroll to nearest valid ground. F0 just leaves the player at the
  freefly position (which can be mid-air; player falls naturally). Acceptable
  for F0 but worth a polish pass before F1 lands.
- **Dr. Carroll model swap** -- design says "the player's body disappears and
  is replaced by the Dr. Carroll model". F0 uses the existing player chr
  model with bondmovemode = CUTSCENE. Need a model swap or hide-mesh + spawn
  Carroll prop in F1 or F2.
- **Controller toggle binding** -- F0 ships keyboard-only (F7). Design says
  hold LB+RB. Add a chord binding once the action map supports chords, or
  bind to a single button in the controller IMC.
- **Pause simulation toggle** -- design says editor mode "pauses (or
  optionally continues) the simulation". F0 leaves sim running; need a
  forge-controlled `lvSetPaused()` toggle in F1 or F2 with a HUD checkbox.
- **Multi-stage base** -- F0 hard-wires CITRAINING. F3 brings the stage
  browser (Paradox template, blank canvas, MP map list).
- **Custom-match integration** -- F0 only enters from main menu. The user
  prompt notes Forge "must work in CI free-roam AND in custom/private
  matches". Latter requires hooking into match setup / room flow; deferred
  to F3 when the base-stage browser exists.

---

## Open — 2026-04-17 (S306 — dev direct)

### Playtest verification of the S306 multi-batch drop

Commits on `dev`: `feb5431c` palette extensions, `a504f3e7` Interface tab + delete scaffolding, `ae4f2f50` input mapping redesign, `5840f28a` (Mike parallel, absorbs BATCH 2 mod delete wiring), `db509d87` Theme Editor bundle dropdowns, `affc61fa` X-button close fix + defensive ctx pop.

1. **Interface tab** — open Settings. New "Interface" tab between Video and Audio. Dropdowns for Color Theme / Menu Style / Title Bar / Font all live here. "Open Color Editor..." and "Open Menu Style Tool..." buttons launch the deeper tools. LB/RB bumpers cycle through 8 tabs.
2. **Color Theme delete** — right-click a user theme (mod theme, not Grey/Blue/Red/Green/White/Silver/Black & Gold) → "Delete Theme…" → confirm. Theme disappears immediately. Built-ins silently refuse. GamepadFaceLeft (controller X) on focused row also opens the context menu.
3. **Mod delete** — open Modding Hub → Installed Mods tab. Right-click (or X-button) a mod → "Delete Mod…" → confirm. Mod disappears.
4. **Theme palette extensions** — open Theme Editor (Interface tab → Open Color Editor). Four groups visible: Window Frame / Text Colors / Interactive Elements / Semantic Accents (+ Reserved behind checkbox). "(auto)" button on Semantic slots zeros to derived default. Save as mod → examine theme.json in `mods/<slug>/` and confirm only the extension keys you actually touched are present.
5. **Theme bundle authoring** — Theme Editor → fill Name → pick a Menu Style (optional) + Font (optional) → Save. Confirm `menuStyle` / `font` keys appear in the saved theme.json.
6. **Input Mapping redesign** — Settings → Controls → Keyboard & Mouse. Group headers visible (Movement / Aim / Combat / Weapons / Vehicle / Menu Nav / C-Buttons / D-Pad / System Hotkeys). Search box filters by action name. Deliberately bind W to two actions — both show red borders with "Conflict: 'W' is bound to more than one action" tooltip. Non-obvious actions show explanatory tooltips on hover (Fire Mode, D-Pad Down, C-Up, etc.). "Save Controls" button at the bottom is gone (auto-saves).
7. **X-button close on Settings** — open Settings, click the X on the first attempt. Should close on first click. Log: `MENU_IMGUI: main menu ESC — settings CLOSE (view 2->0) [via X]` (the `[via X]` suffix is diagnostic — if you see it, the direct-signal channel fired).
8. **Movement after menu close** — open main menu, close. WASD should move immediately, no restart needed. If log shows `MENU_IMGUI: defensive inputCtxPopDeferred(g_CtxImGuiMenu) — leak class caught on top-level close`, that means the boot-time CI-redirect ctx leak was caught and unlocked movement.
9. **Controller X-button on themes + mods** — focus a user theme or mod row with the stick, press X (GamepadFaceLeft = Xbox X / PS Square). Context menu should open.

### Follow-up queued from S306

- **Per-agent prefs.ini** — full implementation per `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md`. ~300-400 LOC + careful test matrix (mid-match agent switch, guest agent, Settings-open switch). Dedicated future session.
- **`renderCiSettingsRedirect` / `renderCiDeadPlayer2` / `renderCinemaList` migration to S300 pool-owned ctx** — `affc61fa`'s defensive pop hides the symptom but the underlying raw inputCtxPush/Pop pattern remains. S304 deferred list.
- **X-button close polling in other renderers** — `pdguiConsumeTitleClose()` is only wired into `renderMainMenu`'s close handler. Sweep needed for Theme Editor / Modding Hub / Pause Menu / CI redirect / Endscreen — they still rely on the Escape fallback.
- **Tooltip sweep for input mapping** — 51 actions have basic display names; ~12 have explanatory tooltips. Bind tooltips for the remaining actions would raise the polish level.
- **Per-action save — keyboard / controller device filter** — currently search matches on display name only. Could extend to accept "key:W" or "ctrl:A" syntax to find all actions bound to a given device/key.

---

## Open — 2026-04-16 (S305 — dev direct)

### Playtest verification of the S305 batch

Commits on `dev`. All items build-verified; needs in-game playtest.

- **P0 content-inset** — launch game, open main menu on a narrow window + on 1080p. Confirm Solo Play / Online Play / Change Agent / Settings / Mods / Cheats / Stats / Quit Game all clear the chrome border with visible breathing space. Repeat for Settings body (tabs + content), CI Settings redirect, Cinema list. Absence of edge-to-edge bleed = pass.
- **Settings persistence** — with a mod theme active (Save-as-Mod output, e.g. `user.pokemon.theme`), pick a Menu Style, pick a Title Bar style, quit client, relaunch. Expect the same theme + menu style + title bar on restart. Watch for `PDGUI theme loader: saved theme '...' not resolvable on startup` WARNING (indicates the mod theme was saved but the registry lost it between runs).
- **Menu Style rename** — confirm the Modding Hub tab reads "Menu Style" (not "Nine-Slice Chrome"), the Settings → Video dropdown reads "Menu Style", and the tool header blurb starts with "Menu Style --".
- **Theme Editor dock + refresh** — open Theme Editor, enter Name + Author, click Save on the action row. Expect status "Saved to mods/" green text, and the saved theme to appear under "Custom (from mods/)" in Settings → Debug → UI Theme WITHOUT restart.
- **Menu Style tool preview + Advanced header** — import a small PNG (e.g. 64x64). Source Preview should now fill the sidebar width (not show tiny). Expand Advanced header to confirm Scale X/Y, Center Cut, Desaturate, Quick Presets are all still wired.
- **Font import** — drop a `.ttf` into `mods/Fonts/MyFont/MyFont.ttf`, restart, confirm Settings → Video → Font dropdown lists "MyFont". Pick it, save, confirm `Video.FontId=user.MyFont.font` in `pd.ini`. Restart, confirm menus render with the new font. Remove the mod → confirm graceful fallback to Handel Gothic.
- **Theme bundle** — hand-edit a `mods/<theme_slug>/theme.json` to include `"menuStyle": "user.<chrome_slug>.ui-chrome"` + `"font": "user.<font_slug>.font"`. Activate the theme; chrome should swap live, font message should say restart-needed.

### Follow-up queued from S305

- **Theme Editor UI for bundle fields** — add Menu Style + Font dropdowns to Save-as-Mod panel so users don't have to hand-edit `theme.json`.
- **Per-agent prefs.ini** — full implementation per `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md`. Dedicated future session.
- **Content-inset sweep for remaining menus** — cheats, mpsetup family, mppause family, room, training, mpsettings, playerconfig, botsetup, agentselect, agentcreate, stats, network, lobby, warning, teamsetup, solomission, controldiagram, challenges, matchsetup — any ImGui renderer that calls `pdguiDrawPdDialog` directly. Audit for `SetCursorPos` + `buttonW = -1.0f` antipattern.
- **B-141 audio underruns** — still ~200/30s in the latest log. Not caused by S305 work but remains open.
- **Old renderers still on raw `inputCtxPush/Pop`** (from S304) — `renderCiDeadPlayer2`, `renderCinemaList` still leak-prone. Watchdog covers them; proper S300 migration pending.

---

## Open — 2026-04-16 (S304 — focused-roentgen)

### Playtest verification of B-160 (menu pool slot leak + darkened/dead main menu)

Build: `PerfectDark.exe` 51,563,427 / `PerfectDarkServer.exe` 22,837,840 bytes. Files touched: `src/game/menu.c`, `src/game/menutick.c`, `src/include/game/menu.h`, `port/fast3d/pdgui_menu_mainmenu.cpp`. Full write-up: `session-log.md` S304.

**Primary repro — main menu reopen cycle (the user-reported bug)**:

1. Launch client. In CI free-roam, open the main menu with whatever key / controller binding you normally use.
2. Confirm log line: `MENUPOOL: acquired main_menu gen=1 def=<ptr> ctx=imgui_menu(owned)` (NOT `ctx=none(shared)` — after the migration the main menu now OWNS the ctx).
3. Press Esc (or controller B) to close the menu. Confirm log line: `MENUPOOL: released main_menu gen=1 ctx=imgui_menu` (immediately, NOT at shutdown).
4. Reopen the main menu. Confirm `MENUPOOL: acquired main_menu gen=2 ...` with a fresh generation number.
5. Repeat 3-5× fast-open/fast-close cycles. Each cycle should be a clean `acquired` / `released` pair, generations should increment monotonically (2, 3, 4, 5).
6. ABSENCE of these log lines is the health signal:
    - `MENU: menuPopDialog called at depth 0 (underflow)` — underflow was the leak trigger; should not appear on normal close
    - `MENU: menuPushDialog rejected — pool slot [main_menu] already active` — this was the user-visible symptom (menu stays darkened / can't reopen)
    - `MENU: watchdog — legacy stack empty but N pool slot(s) active` — the per-frame consistency check; any occurrence means a close path we haven't yet wired is leaking slots (auto-recovered, but identifies the path for follow-up)

**Stress test — force-close paths (the underflow fallback)**:

- Open main menu → wait for CI free-roam timer to tick into a scripted transition (if any). Confirm no leak warning.
- Open main menu → trigger a "Quit to Title" or "Quit to Main Menu" path if available. Confirm clean release in log.
- Open main menu → click "Combat Simulator" → click "Start Match" (triggers `matchStart` → `menuStop` → stage transition). Expect a `MENUPOOL: released N slot(s) (bulk)` from the force-close site; NOT the watchdog warning.

**Stress test — rapid reopen**:

- Open / close / open / close the main menu as fast as possible (mash Esc + Menu-open key). No stuck state, no double-render, no darkened backdrop, no rejected pool warnings.

**Switching around — the original user scenario**:

- Open main menu → click Settings tab → back to main → click Modding tab → back to main → click Online Play → back to main → Esc to close → reopen. Cycle through each sub-view at least twice. Same health signal as primary repro.

### Watchdog diagnostic interpretation

If the watchdog DOES fire (`MENU: watchdog — legacy stack empty but N pool slot(s) active`), the immediately-following `MENUPOOL dump:` lines identify the leaked slot(s). Common expected scenarios:

- `MENUPOOL dump: [main_menu] gen=N def=<ptr> ctx=<ctx>` — main menu close path bypassed `menuPopDialog` somehow. Trace the sequence of log lines in the ~10 frames before the watchdog fired.
- `MENUPOOL dump: [ci_options] gen=N ...` — a CI Options sub-dialog close bypassed pool release (these are still on the raw ctx pattern, see "Not fixed in this session" in S304).
- `MENUPOOL dump: [cheats]` or other migrated-renderer slot — S300 pattern regression. Should not happen after S300.

### Follow-up queued from S304

- **Migrate remaining mainmenu.cpp renderers to S300 pattern** — `renderCiSettingsRedirect` (line 3170+), `renderCiDeadPlayer2` (line 3249+), `renderCinemaList` (line 3358+) still use raw `inputCtxPush/Pop` around `menuPopDialog`. Same leak class as the main menu was; the S304 watchdog will catch any resulting leak, but proper migration removes the raw pattern.
- **Audit `menuPushRootDialog` pool hygiene** — `src/game/menu.c:3733-3736` zeroes `g_Menus[].numdialogs = 0; depth = 0;` without consulting the pool. Any active pool slots survive the root push. The S304 watchdog auto-recovers, but a principled fix is `menupoolReleaseAll()` at the top of `menuPushRootDialog` (or, better, treat root push as a stage-transition-class reset and route it through the existing reset helpers).
- **Underflow root-cause tracing** — the S304 fix treats underflow as a leak-recovery point, but doesn't identify WHICH caller triggered the underflow. A short-lived diagnostic patch could add a `sysLogCallerHint` line (file + function name of caller) to help pinpoint the origin in a future playtest.

---

## Open — 2026-04-16 (S303 — reverent-chatelet)

### Playtest verification of S303 game-loop sweep

Commit on `claude/reverent-chatelet-e076df`. Build: `PerfectDark.exe` 51,564,390 / `PerfectDarkServer.exe` 22,837,840 bytes. Full findings: `context/scratch/game-loop-sweep-2026-04-16.md`.

Tail the following across every playtest:

```bash
grep -E "GAMELOOP\.(CAMPAIGN|COOP|COUNTEROP|MANIFEST|WEAPON)" pd-client.log
grep -E "GAMELOOP\.(COOP|COUNTEROP|MANIFEST)" pd-server.log
```

- **Solo campaign first mission** — expect entry log `GAMELOOP.CAMPAIGN: menuhandlerAcceptMission entry stage_id=...`, SP transition log, per-weapon grants, endscreen prepare + NEXT_MISSION or EXIT_TO_MAIN_MENU bridge log.
- **Deep Sea co-op auto-advance** — expect `GAMELOOP.COOP: Deep Sea auto-advance → ...` followed by `GAMELOOP.MANIFEST: MP transition to 0x...` for the next stage.
- **Non-Deep-Sea co-op mission complete** — expect `GAMELOOP.COOP: MPENDSCREEN → CITRAINING lobby return` + `GAMELOOP.MANIFEST: MPENDSCREEN exit clearing manifest (N entries) before CITRAINING`. If the new `manifestClear` didn't fire (line missing), SP-13-class regression.
- **Counter-Op 2-client match** — server log: `GAMELOOP.COUNTEROP: server start anti stage=0x... antiClientId=N antiplayernum=N (from wire)`. Any `WARNING antiClientId unresolved` = bug — v36 wire didn't carry the value or server state is stale. Both clients log `GAMELOOP.COUNTEROP: endscreenPushAnti entry role=bond|anti`; anti client's weapon log is `GAMELOOP.WEAPON: ... INTRO skipped for anti role`.
- **Co-op 1-pad telefrag audit** — any coop mission should log `GAMELOOP.COOP: only N spawn pad(s) declared ...` as a WARNING if `g_NumSpawnPoints < 2`. Each warning identifies a problem stage.
- **Stale MP manifest leak detection** — if `GAMELOOP.MANIFEST: stale MP manifest (N entries) leaked into SP transition to 0x%02x — routing via MPTransition` ever fires, that identifies a previously-silent teardown gap.

### Follow-up queued from S303

- **GAP-A co-op manifest ignores host payload** — server-side `manifestBuild(&g_ServerManifest, NULL, NULL)` at `netmsg.c:4870` ignores the CLC_LOBBY_START manifest payload on the co-op/anti path (Combat-Sim deserializes via `manifestDeserialize` at `:4706`). Now logged via `GAMELOOP.COOP: server-side manifest built ...`. Design call needed: adopt host manifest + diff-merge against server state, or stay server-authoritative and document the behaviour.
- **GAP-B co-op 1-pad telefrag (proper fix)** — coop-aware pad scoring in `playerChooseSpawnLocation` or a co-op-specific SP-14 expansion in `playerreset.c` that triggers on `g_NumSpawnPoints < LOCALPLAYERCOUNT()`. The S303 audit log makes the problem observable but doesn't prevent the telefrag.
- **Anti body/head pre-load manifest gap** — possession-based resolution already relies on post-load catalog lookup. A proper fix would require modelling a canonical anti-chr per mission in mission config, or upgrading `manifestSPRescanSetup` to scan `g_StageSetup.intro` for possession-bait chrs.
- **`menuhandlerAcceptMission` menu pool release** — now logged but does not call `menupoolReleaseAll`. Low priority — `menuStop()` already walks stack — but could add defensively for parity with endscreen bridge paths.

---

## Open — 2026-04-16 (S300 — peaceful-aryabhata)

### Playtest verification of the S300 batch

Single commit on `dev` (fast-forwarded from `claude/peaceful-aryabhata-7abdc6`). Build: `PerfectDark.exe` 51,534,311 / `PerfectDarkServer.exe` 22,827,178 bytes.

- **ADR §6.1b pool-owned ctx migration** — For each migrated renderer (Cheats, MP Setup family, MP Pause family, MP Advanced family, Player Config, Bot Setup, Agent Select, Room, Training FR, MP Settings family) confirm:
  - Opening the menu emits `MENUPOOL: attached ctx to active <type>` (IsWindowAppearing attach) or `MENUPOOL: acquired <type>` (if `menuPushDialog` hadn't pre-acquired; rare but possible for pure-ImGui menus like Room).
  - Closing via Back/Esc emits `MENUPOOL: released <type>` from `menuCloseDialog`.
  - A Begin()=false cull (rapid open/close race) also releases cleanly — no trapped player input.
  - Stacking Soundtrack → SelectTunes opens both without dedup-rejection warnings; backing out of Tunes keeps Soundtrack's ctx alive (shared mode).
  - Room in solo mode keeps the main menu's ctx alive on close (shared mode); Room in network mode pops its own ctx.
- **SP-14 defensive reset** — host a listen server, start a match, disconnect mid-match (hit the X / quit). Re-host. Tail `pd.log` across the disconnect for the `disconnect-time reset` line if a match was active. After re-host, `g_NetMatchRoomId` starts as `0xFF` (any subsequent ready-gate fire should set it fresh).
- **Manifest diagnostics** — on title-screen load, expect a log line like `MANIFEST-MENU: built N entries — bodies=X(mod=Y,disabled=Z) heads=...`. If Skin Editor mod characters are enabled in Modding Hub and this shows `mod=0`, that's the old gap recurring. Play a stage with cinematic intro spawns (Deep Sea, Crash Site) and tail for `MANIFEST-SP: rescan diff — newly-discovered=N`; `N > 0` confirms the post-load scan is finding cinematic spawn assets.

### Follow-up queued from S300

- Training sub-dialog stacking: `g_FrDifficultyMenuDialog`, `g_FrTrainingInfoPreGameMenuDialog` etc. all map to `MENU_TYPE_TRAINING` in the pool registry. If playtest shows drilling from FR Weapon List → Difficulty → Info is rejected by pool dedup (warning `MENU: menuPushDialog rejected — pool slot [training] already active`), split each sub-dialog into its own type.
- Shared-action leak (ADR §6.2) still open — per-scope state arrays in the actionmap.
- Unit / integration tests for the pool (ADR §6.3).

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

## Open — 2026-04-16 (S301 — confident-chaum) — comprehensive instrumentation drop

**What shipped**: `port/include/crashbreadcrumb.h` + `port/src/crashbreadcrumb.c` (256-slot static ring, dumped by VEH / UEF / SIGABRT / Linux `sigaction` handlers); push sites in `src/lib/main.c` (mainTick heartbeat + mainChangeToStage), `src/game/lv.c` (lvTick), `src/game/chr.c` (CHR.TICK around chraTick dispatcher), `src/game/chraction.c` (chraTickBg), `src/game/bondwalk.c` (bwalkTick), `src/game/bot.c` (botSpawn), `src/game/botmgr.c` (botmgrAllocateBot), `port/src/net/matchsetup.c` (matchStart), `port/src/net/netmsg.c` (SVC_STAGE_START send/receive), `port/src/net/net.c` (netServerStageStart); standard log-channel DIAG lines with prefixes `ENDSCREEN.DIAG:` / `CHR.DIAG:` / `MATCHSTART.DIAG:` / `AUDIO.DIAG:` / `CRASH.DIAG:` covering Bug C / Bug D / Chicago / Airbase / B-141 respectively.

### Playtest: tail the log for the new DIAG tags

On the next repro pass, before reporting, run:

```bash
grep -E "ENDSCREEN\.DIAG|CHR\.DIAG|MATCHSTART\.DIAG|AUDIO\.DIAG|CRASH\.DIAG|HEARTBEAT|LVTICK|BWALK\.TICK|CHR\.TICK|BOT\.SPAWN|STAGE\.CHANGE" pd-client.log
```

and for the server side:

```bash
grep -E "MATCHSTART\.DIAG|CHR\.DIAG|CRASH\.DIAG" pd-server.log
```

### What each tag tells you

- **Bug C — MP endscreen body invisible** (`ENDSCREEN.DIAG:`). On fresh entry you get one `ENTRY (fresh)` line with `g_MpPlayerNum / g_NetMode / challengeResult / titleOverride`. Once Begin succeeds you get a geometry dump with `sf / menu / pad / win / contentAvail / scroll`. On the first frame you also get `body child opened contentW=… contentH=… innerCRA=…x…` and `rankings built count=N teamsMode=…`. If rankings count is 0, that explains the invisible body. If `contentH < minH` you see the pre-existing `ENDSCREEN: contentH clamped` warning. Capped at 80 prints per match.
- **Bug D — invisible networked bots** (`CHR.DIAG:`). Bot allocation logs `botAlloc chrnum=… body=… body_id='…'`. If `bodyAllocateModel` returns NULL you see `WARNING bodyAllocateModel returned NULL`. If `chrAllocate` returns NULL you see `WARNING chrAllocate returned NULL`. At the end of `botSpawn`, final state is `botSpawn DONE chrnum=… model=… chrflags=…x… hidden=…x… invisible=0|1`. An `invisible=1` line names the exact reason (`model=NULL` / `HIDDEN` / `VOID(-1)`).
- **Chicago silent crash ~9s** (`CRASH.DIAG:`). On the next silent death, open `pd-client.log` or `pd.crash.log` and find the `FATAL:` line. Directly below it is `CRASH.DIAG: breadcrumb ring dump follows:` followed by up to 128 entries in time order. Last entry = last subsystem alive. Expect sequences like `HEARTBEAT frame=N …` → `LVTICK frame=N …` → `CHRTICKBG …` → `CHR.TICK slot=17 chrnum=25 action=…` → (crash here). If the trail ends on `BWALK.TICK` or `STAGE.CHANGE`, the crash was NOT in chr AI.
- **Airbase 0xc0000005 / Start-Match no-response** (`MATCHSTART.DIAG:`). Server log should show `matchStart entry …` → `pre-mpStartMatch …` → `post-mpStartMatch …` → `netServerStageStart stage=… clients=N` → `SVC_STAGE_START sent …`. Each missing step is a decision point that bailed. Client log should show `SVC_STAGE_START read begin srccl=… state=…` if the message arrived. Absence of that line on client + presence on server = packet drop in transit. Absence on both = server never sent.
- **B-141 audio skips** (`AUDIO.DIAG:` / enhanced `AUDIO[B-141]:` summary). `audioInit` logs full SDL device spec — watch for `SAMPLE RATE MISMATCH` warning. Every 30 s a summary line now reports drops, underruns, hitches, nullProducer count, mixBufOverflow count, scheduler gap (max/mean ms), and queue depth (min/max samples). A skip during that window will light up exactly one bucket — that tells you whether the bug is OS jitter (hitches), RSP starvation (nullProducer), consumer stall (high max buffered), or mod mixer overflow (mixBufOverflow > 0).

### Follow-up once any track clears

- If Bug C resolves from a single ENDSCREEN.DIAG trace → promote the instrumentation's finding into a proper fix and then reduce `ENDSCREEN_DIAG_MAX_PRINTS` from 80 to 10 (keep a small safety log for regressions).
- If Chicago crash identifies a specific subsystem → add more granular breadcrumbs inside that subsystem (e.g., inside collision.c per-probe if the last entry is always `BWALK.TICK`) and ship a second diagnostic drop.
- If `AUDIO.DIAG: mixBufOverflow > 0` on the first repro → `s_MixBuf` needs to grow past 8192 samples, or `modMusicMixInto` needs to chunk. Easy fix.
- If `CHR.DIAG: WARNING … invisible=1 … VOID(-1)` on every spawn → FIX-A.2 is the area, but we now have the fingerprint for an exact reproducer.

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

- ~~Migrate the 10 ImGui renderer `s_*PushedCtx` bools to pool-owned input-context (see ADR §6.1b).~~ DONE S300 — all ten files migrated to `menupoolAcquireDialog` / `menupoolReleaseDialog` pattern; pool attaches ctx to already-active slots; three new `MENU_TYPE_*` values added for mpsettings sub-dialogs that can stack.
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
| **Manifest gap follow-ups** — AUDIT S300 | (1) `manifestBuildForMenu()` + `manifestMenuTransition()` live at netmanifest.c:605/1615 and wired from pdmain.c:772; S300 added per-category counters (bodies/heads × mod/disabled) so `MANIFEST-MENU: built …` log line makes mod-miss obvious. (2) SP pre/post-load split IS in place (`manifestSPTransition` pre-load in mainChangeToStage, `manifestSPRescanSetup` post-load in `lvInit`); S300 added `rescan diff — newly-discovered=N` log so post-load cinematic/ailist discovery is measurable. (3) Safety-net still via `manifestEnsureLoaded` at spawn time. Remaining gap: none identified; any new regression will show up in the per-category diagnostic log. |

### Audit follow-ups (S262 — closed in S300)

- ~~**Room-scope teardown hygiene (SP-14 follow-up)**~~ — DONE S300 — `netDisconnect` + `netStartServer` reset `g_NetMatchRoomId = 0xFF` and `g_NetCounterOpClientId = NET_NULL_CLIENT` defensively. Stage-end path (`netServerStageEnd` / `netmsgSvcStageEndRead`) already handled the normal teardown; S300 closes the disconnect-mid-match gap.

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
