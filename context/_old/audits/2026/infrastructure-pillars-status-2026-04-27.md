# Infrastructure Pillars: Code-First Status Report

> **Date written**: 2026-04-29 (filename retains the originally-requested 2026-04-27 stamp per Mike's scope).
> **Mode**: Evaluation only. No code changes made by this session.
> **Methodology**: Two-phase strict ordering. Phase A reads source code only (`.c`, `.cpp`, `.h`, `.hpp`, build files, inline comments). Phase B reads `context/` and compares. No em-dashes anywhere in this report.
> **Scope**: Catalog, Input, Menu/UI/UX, Modding, Connectivity, Tests, Dev Tooling, Save/Wire format, plus surfaced extras (Updater, Property Handler, Forge, Social, Voice).
> **Possibility framing**: Subjective judgments are flagged with "appears to," "suggests," or "looks like" rather than asserted as fact.

---

## How to Read This

For each pillar there are two sections.

**Phase A** is what the code itself says, with file:line citations. It deliberately ignores design docs, trackers, session logs, and memory.

**Phase B** is the comparison to `context/`. It calls out three things separately:
- where the live code state matches the context claim,
- where context claims more than the code shows,
- where the code shows more than context describes.

The objective of separating the two phases is that, in a 12-month-old codebase with many design docs, drift between intent and reality is normal. Drift in either direction is informative.

---

# PHASE A: Code-Only Review

## 1. Catalog System

### Phase A: Current implementation state

Three layers exist in code today.

**Layer A**: static `struct weapon *` pointer array `g_Weapons[]` lives at `src/game/invitems.c:5700`, with extern declaration at `src/include/game/inv.h:9`. The array has 86 entries (`WEAPON_NONE` through `WEAPON_SUICIDEPILL`). The 86 individual `invitem_*` structs (e.g. `invitem_falcon2`) are also static C globals in the same file. `g_AibotWeaponPreferences[]` lives at `src/game/botinv.c:23` with around 48 entries. None of this data has moved out of the static binary.

**Layer B**: catalog rows are registered in `port/src/assetcatalog_base_extended.c:407-442`. Table `s_BaseWeapons[]` at line 57 has 41 entries covering `MPWEAPON_*` slots `0x00..0x28`. Each entry calls `assetCatalogRegisterWeapon` and sets `e->runtime_index` to `g_MpWeapons[mpw].weaponnum`. The `ext.weapon` shadow fields (`damage`, `fire_rate`, `ammo_type`) were removed in F9 (`port/include/assetcatalog.h:288-302`). Replacement `pdbase_path[128] / pdbase_offset / pdbase_size` scaffold fields are present at `assetcatalog.h:299-301` but empty.

**Manager / accessor layer**: `port/include/catalog_mgr_weapons.h` declares 8 public functions. The header comment at line 14 says "this manager is a thin pass-through router over the legacy `g_Weapons[]` / `g_AibotWeaponPreferences[]` / `invaimsettings_default` / `invnoisesettings_silent` globals." The router is at `port/src/catalog_mgr_weapons.c:41-60`: bounds-check and return `g_Weapons[weapon_id]`. Default fallback accessors at lines 99-107 return `&invaimsettings_default` and `&invnoisesettings_silent` by const pointer. A pure validator file `port/src/catalog_mgr_weapons_pure.c` is globals-free, testable, and compiled into `pd-tests`.

**.pdbase loader**: F10 was committed as scaffold-only. `port/src/loader_pdbase.c:20-61` is the body. `loaderPdbaseScan` zeroes a result struct, logs a single "OK" line, returns. The directory walk is explicitly not implemented (comment at `loader_pdbase.c:37`: "Phase 2 (F10): scaffold only. The directory enumeration is not implemented yet"). `loaderPdbaseBuildWeaponManager` at line 52 is a no-op. Nothing decodes.

**Migration completeness for S484 weapons F1-F10**: live direct dereferences of `g_Weapons[]` in the codebase total exactly 3, all inside `catalog_mgr_weapons.c` lines 59, 67, 96. The two `src/` occurrences in `setup.c:2776` and `setup.c:2865` are inside block comments (bug narrative), not live code. The `loader_pdbase.c:10` occurrence is a comment. So the manager is the choke-point as designed.

### Phase A: What's done right (code evidence)

- The router is bounds-guarded. `catalog_mgr_weapons.c:47-58` returns NULL silently for negative weapon_id (the documented "unarmed" sentinel) and emits `CATALOG.MGR.WEAPON.MISS:` for out-of-range positive. The B-263 class (FIESTA sentinel 0xFE crashing as `g_Weapons[254]`) is structurally prevented at this surface.
- F2 through F8 call-site migrations are pinned by static text scans in `tests/test_weapon_direct_reads_audit.cpp`. Lines 44, 49, 53, 57, 63, 67-90, 93-102 each `REQUIRE` that a specific source file contains zero `g_Weapons[` substrings. This is a regression bar that catches re-introduction at compile time.
- F8 default-fallback rewiring is also pinned: `tests/test_weapon_direct_reads_audit.cpp:104-113` asserts the I.1 mutator (`currentPlayerSetWeaponPos`) is absent from `src/game/game_0b0fd0.c`.
- F9 ext.weapon shadow-field removal is pinned: lines 115-132 of the same test file `REQUIRE` the `damage` / `fire_rate` / `ammo_type` field names are absent from `port/include/assetcatalog.h` and from the scanner / netdistrib implementations.
- The pure validator file is the right architectural seam. `port/src/catalog_mgr_weapons_pure.c` has zero globals, zero I/O, zero allocator calls. `tests/test_catalog_mgr_weapons_api.cpp` covers 10 cases including bounds, EYESPY stage-to-variant mapping, mutual exclusion of flag masks, NULL-out guard.
- Count constants agree across surfaces: `CATALOG_MGR_WEAPON_COUNT` in `catalog_mgr_weapons.h:41`, `CATALOG_MGR_WEAPON_COUNT_PURE` in `catalog_mgr_weapons_pure.h:27`, and `WEAPON_SUICIDEPILL + 1` from the inv.h enum all equal 86.

### Phase A: What's wrong or incomplete (code evidence)

- **The manager owns no data.** This is the largest structural fact. `catalog_mgr_weapons.c:59` returns `g_Weapons[weapon_id]` directly. The 86 `invitem_*` structs are still static C globals in `invitems.c`. The 48+ aibotweaponpreference entries are still in `botinv.c:23`. F10 loaded nothing. F11+ data move is entirely unimplemented.
- **Loader header lies to its implementation.** `loader_pdbase.h:49-51` says "Phase 2 (F10): the directory scan is implemented but archives are not yet decoded." `loader_pdbase.c:37` explicitly says "the directory enumeration is not implemented yet." Two files in the same module disagree. A reader trusting the header would think the scanner was wired.
- **Stale count comment in the test.** `tests/test_catalog_mgr_weapons_api.cpp:15` says "CATALOG_MGR_WEAPON_COUNT pin: 89 (matches WEAPON_SUICIDEPILL + 1 in src/include/constants.h:4490)." The actual `REQUIRE` on line 56 asserts 86. The line reference `:4490` also looks wrong. The code is correct; the comment is misleading.
- **F10 test pins shape only, not behavior.** `tests/test_loader_pdbase_scan.cpp` has 3 cases. All three are static text checks against `loader_pdbase.h` and one log-string check against `loader_pdbase.c`. None call `loaderPdbaseScan` or `loaderPdbaseBuildWeaponManager` or assert return values. The tests would pass even if the implementation were stripped, so long as the header keywords remained.
- **Catalog covers 41 MPWEAPON entries vs 86 manager slots.** Slots `0x29..0x55` (`WEAPON_PSYCHOSISGUN` through `WEAPON_SUICIDEPILL`) have no catalog representation. `catalogManagerGetWeaponById("base:falcon2")` resolves through `runtime_index = g_MpWeapons[mpw].weaponnum`, but no string-keyed lookup exists for solo or mission-only weapons. This is consistent with S484 Phase 2 scope, and probably intentional, but the asymmetry should be documented or closed.
- **EYESPY mutator still mutates static storage.** `catalog_mgr_weapons.c:132-134` writes `w->name`, `w->shortname`, `w->flags` directly on the struct pointer retrieved from `g_Weapons[WEAPON_EYESPY]`. When data moves to `.pdbase` archives in F11+, this in-place mutation needs a different mechanism (the loaded record will not be a permanent static).
- **Const-cast violation.** `catalog_mgr_weapons.c:99-107` returns `const struct invaimsettings *`. The call site at `src/game/game_0b0fd0.c:120` casts away const into a non-const pointer. This appears to have been inherited from the legacy `&invaimsettings_default` usage pattern.
- **Bridging dependency through `g_MpWeapons[mpw].weaponnum`.** `assetcatalog_base_extended.c:429-431` chains three index spaces (catalog key MPWEAPON, runtime_index WEAPON, `g_Weapons[]` position). If `g_MpWeapons[mpw].weaponnum` is uninitialized for a slot, `catalogManagerGetWeaponById` would silently return `g_Weapons[0]` (the "Nothing" weapon). There is no log on the zero-runtime_index path.

### Phase A: To complete

Listed in implementation-dependency order.

**Immediate documentation-in-code cleanup.**
- Fix `tests/test_catalog_mgr_weapons_api.cpp:15` to say 86, with the correct constants.h line.
- Reconcile `loader_pdbase.h:49-51` with `loader_pdbase.c:37`. Header should not claim more than the body delivers.

**F11 data move (the actual point of Phase 2).**
- Implement the filesystem walk inside `loaderPdbaseScan` so it enumerates `*.pdbase` archives.
- Implement `loaderPdbaseBuildWeaponManager` to decode weapon records from archives and register them with the manager.
- Move the 86 `invitem_*` struct definitions from `invitems.c` to a `base/weapons.pdbase` archive.
- Populate `ext.weapon.pdbase_path / pdbase_offset / pdbase_size` on catalog entries.
- Extend `catalogManagerGetWeaponByIndex` to serve from manager-owned data instead of `g_Weapons[]`.

**F12 Layer A retirement.**
- Retire `g_AibotWeaponPreferences[]` from `botinv.c`.
- Retire `invaimsettings_default` and `invnoisesettings_silent` externs.
- Fix the const-cast at `src/game/game_0b0fd0.c:120` (callers use `const struct invaimsettings *`, or the manager returns mutable when mutation is legitimate).

**F13 test depth.**
- Replace static-only `test_loader_pdbase_scan.cpp` cases with behavioral tests that call the scanner against a fixture directory and assert non-zero counts.
- Add a round-trip test for `catalogManagerGetWeaponById("base:falcon2")` that verifies the MPWEAPON-to-WEAPON bridge.
- Add a zero-runtime_index loud-fail to the by-id accessor.

**Boundary clarity.** Either grow the catalog to all 86 WEAPON slots, or document that string-keyed lookup only resolves MP-selectable weapons.

---

## 2. Input System

### Phase A: Current implementation state

Five principal files at canonical paths.

- `port/include/actionmap.h` declares 103 named actions, 12 IMC singletons, the full query API.
- `port/src/actionmap.cpp` implements dispatch, bind management, analog polling, hold/tap timing.
- `port/include/inputctx.h` and `port/src/inputctx.c` implement a pushdown automaton for input ownership.
- `port/include/inputlayer.h` and `port/src/inputlayer.c` implement a typed layer stack (Cohort 2-4 work).

The single event entry point is `pdguiProcessEvent()` at `port/fast3d/pdgui_backend.cpp:1272`. It calls `ImGui_ImplSDL2_ProcessEvent()` (line 1398), applies a `WantCaptureKeyboard` gate (line 1409), then `actionmapDispatch()` (line 1439), then `inputCtxDispatch()` (line 1453). The SDL event loop sits at `port/fast3d/gfx_sdl2.cpp:320`.

Tests: `tests/test_input_authority.cpp`, `test_input_layer_stack.cpp`, `test_actionmap_flush.cpp`, `test_right_stick_scroll.cpp`, `test_social_toggle_imc.cpp`, plus pure-C mirrors `actionmap_pure.c`, `inputctx_pure.c`, `inputlayer_pure.c`.

### Phase A: What's done right (code evidence)

- **Action enum coverage.** `actionmap.h:54-248` defines 103 actions across movement, analog aim, N64 C-buttons, D-pad, combat, weapons, vehicle, menu nav, system, Forge, observer, voice PTT, skin editor, cutscene skip. Aliasing handled at line 252 (`ACTION_MENU_ACCEPT == ACTION_USE`).
- **Deterministic dispatch.** `actionmap.cpp:566-594` walks `s_Active[]` highest priority first, lower trigger-slot index first, then lower action id. Documented at lines 576-580 (B-221.5 comment).
- **Single suppression predicate.** `inputctx.c:716-741`'s `gameplayInputSuppressed()` is the one truth-source: top-of-stack non-gameplay, focus loss, or 50ms focus settle. `actionmap.cpp:1392-1411` (`actionLayerAllows`) gates every query API call on this predicate.
- **Flush discipline.** `actionmapFlushGameplayState()` synthesizes release edges for held gameplay actions while preserving shared actions (USE, PAUSE). Called on non-gameplay context push (`inputctx.c:263`), focus loss/gain (`inputctx.c:614, 625`), cutscene push (`inputlayer.c:117`).
- **Hold/tap as first-class.** `actionmap.h:424-440` exposes `actionHeldForMs`, `actionWasTap`, `actionConsumeHold`, `actionHoldProgress`. Implementation at `actionmap.cpp:1539-1712` is gated through `actionLayerAllows`.
- **Deferred-pop correctness.** `tests/test_input_authority.cpp:145-163` verifies `PopDeferred` marks-without-removing, `inputCtxGetTop` skips marked entries, `EndFrame` compacts at frame boundary. The S197a resurrect path (pop-then-push within a frame) is exercised at lines 178-193.
- **Generation-number layer handles.** `inputlayer.c:21-26` carries `slot + generation`; pop zeroes generation; stale-handle pop returns `-1` rather than corrupting (verified at `test_input_layer_stack.cpp:140-152`).
- **IMC lifecycle tied to ctx.** `inputctx.c:799` activates `g_ImcMenu` on push; line 806 deactivates and calls `pdguiClearImGuiFocusAndNav()`. Pause menu activates `g_ImcPauseMenu` at line 872. Debug overlay uses `g_ImcDebugOverlay` at line 917.
- **Watchdog.** `inputctx.c:448-472`: depth >= 15 triggers an emergency reset and re-seeds gameplay; depth >= 5 logs DEEP STACK warning. Self-recovering.
- **Static-analysis tests.** `test_input_layer_stack.cpp:295-404` reads source text and asserts `inputctx.c` includes `inputlayer.h`, publishes a menu-layer bridge, and that every `actionmap.cpp` query call goes through `actionLayerAllows`.

### Phase A: What's wrong or incomplete (code evidence)

- **F-key system actions bypass the actionmap.** `pdgui_backend.cpp:1288-1379` contains 10 raw `if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_Fxx)` blocks (F6, F7, F8, F9, F10, F12, Shift+F1, F2, Shift+F2, plus the RS-click hotswap toggle at line 1310). These consume events before `actionmapDispatch` is reached at line 1439. They are not rebindable, not gateable through `gameplayInputSuppressed()`, and the comment at line 1284 only documents this as a soft contract.
- **`gfx_sdl2.cpp` has 3 raw hotkeys.** Lines 335-345: Alt+Enter (fullscreen), F10 (mesh debug), backquote (console toggle). These are processed before `pdguiProcessEvent`. Backquote duplicates `ACTION_CONSOLE_TOGGLE`.
- **`input.c` retains direct hardware polling.** `input.c:1075, 1092, 1099` call `SDL_GameControllerGetAxis` and `SDL_GameControllerGetButton` for trigger and stick-direction VKs. Used by the legacy `inputKeyJustPressed` path (line 1115, marked DEPRECATED). Reachable from any joystick-VK caller, bypassing actionmap deadzone and sensitivity.
- **Layer stack IMC pointers deferred.** `inputlayer.c:210` has `.imc = NULL, /* Cohort 3: &g_ImcGameplay */` and line 237 has `.imc = NULL, /* Cohort 3: &g_ImcMenu */`. The layer stack only owns IMC lifecycle for vehicle and cutscene; gameplay and menu IMC lifecycles are still split across `inputctx.c`'s on_push/on_pop callbacks.
- **`pdgui_spectator.cpp:162` uses the wrong gate.** It checks `io.WantCaptureKeyboard` rather than `gameplayInputSuppressed()` or top-of-stack ctx. WantCaptureKeyboard is ImGui-internal state, not the input authority.
- **Right-stick scroll spec is decoupled from runtime.** `tests/test_right_stick_scroll.cpp:14-18` says explicitly: the runtime is `pdguiDriveImGuiNav`, this suite is the spec, runtime constants are not test-asserted. If `pdguiDriveImGuiNav` drifts from deadzone 0.15 / max 1200 px/s / exponent 1.7, no test catches it.
- **Observer layer asymmetry.** `inputlayer.c:157-183`: `onObserverPush` activates `g_ImcObserver` only when `source == SCENE_OBSERVER_SOURCE_SPECTATOR`. `onObserverPop` deactivates unconditionally. Likely safe (deactivate of inactive IMC is a no-op) but the asymmetry is a defect.

### Phase A: To complete

- Migrate F-key actions into the action map. F8 / F9 / F10 should become `ACTION_HOTSWAP_TOGGLE`, `ACTION_DIAG_TOGGLE`, `ACTION_MESH_DEBUG_TOGGLE` on `g_ImcDebugOverlay` or a new system IMC. The RS-click hotswap path becomes a JOY_BTN binding on the same action.
- Delete the backquote raw handler in `gfx_sdl2.cpp:343`. The actionmap binding for `ACTION_CONSOLE_TOGGLE` already covers it.
- Audit and retire `inputKeyPressed()` controller polling in `input.c:1075-1099`. After migration, narrow to mouse buttons only, matching the DEPRECATED comment.
- Wire `g_LayerGameplay.imc = &g_ImcGameplay` and `g_LayerMenu.imc = &g_ImcMenu` so the layer stack owns those lifecycles instead of inputctx callbacks.
- Replace `WantCaptureKeyboard` in `pdgui_spectator.cpp:162` with `gameplayInputSuppressed()` or top-of-ctx check.
- Add a static test that pins the right-stick scroll runtime to its spec constants.
- Fix the observer push/pop asymmetry: either activate unconditionally on push, or guard the pop.

---

## 3. Menu / UI / UX

### Phase A: Current implementation state

Thirty-one `pdgui_menu_*.cpp` files compile. One is retired (`pdgui_menu_matchsetup.cpp.retired`, not picked up by the CMake `GLOB_RECURSE`). Three architectural layers:

- **Input ownership stack** (`inputctx.h`): 16-slot stack, four contexts (`g_CtxGameplay`, `g_CtxImGuiMenu`, `g_CtxPauseMenu`, `g_CtxDebugOverlay`).
- **Menu pool** (`port/include/menupool.h`, `port/src/menupool.c`): keyed by `menu_type_t`, structural dedup, optional ctx ownership per slot.
- **Menu graph** (`port/include/menugraph.h`): named edges between menu types for validated transitions.

Layout primitives in `port/include/pdgui_layout.h` and `pdgui_widgets.h`. Theme system in `pdgui_theme.cpp` with editor at `pdgui_menu_theme_editor.cpp`. Stack-debug overlay at `pdgui_menu_stack_debug.cpp` renders the live stack, pool, legacy chain, and ctx-history without capturing input (`ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav` at lines 40-41).

Test surface: `test_menu_stack.cpp` (9 cases), `test_menu_reachability.cpp` (6 synthetic trees), `test_menu_graph.cpp` (static source guard), `test_right_stick_scroll.cpp` (math spec), `menupool_pure.c` (pure mirror of pool data structure).

### Phase A: What's done right (code evidence)

- **Menu pool resolves the three-source-of-truth problem.** `menupool.h:33-39` documents the prior state: legacy dialog stack + per-renderer `s_FooPushedCtx` booleans + input ctx stack. The pool resolves all three. Structural dedup at `menupool.c:255-277`. Per-slot ctx push/pop pair so `menupoolReleaseAll()` (`menupool.c:479-514`) atomically tears down all menus and their input contexts on stage transitions. Migration is complete: every `.cpp` once owning `s_*PushedCtx` now has the removal comment, e.g. `pdgui_menu_agentselect.cpp:202`, `pdgui_menu_cheats.cpp:201`, `pdgui_menu_mpadvanced.cpp:355`.
- **Nav abstraction is clean.** No raw `ImGui::IsKeyPressed(ImGuiKey_*)` in any `pdgui_menu_*.cpp`. `test_menu_graph.cpp:66-83` enforces this statically. Menus call `pdguiMenuAcceptPressed()`, `pdguiMenuCancelPressed()`, etc. from `pdgui_nav.h:42-64`, which delegate to actionmap. 27 of 31 menu files use these helpers.
- **Layout primitives shared.** `pdgui_layout.h` defines body/action-bar split with a diagram. `pdguiBeginActionBar / pdguiEndActionBar / pdguiActionBarButton` used in 19 of 31 files. `pdguiPopupDarkenBehind` standardizes scrim. Confirm-modal primitive at `pdgui_layout.h:196-222` canonicalizes the 5-frame force-focus + 3-frame debounce pattern.
- **Modal focus handled deliberately.** `pdgui_menu_warning.cpp:853-870` (renderMpEndGameDialog) uses `SetKeyboardFocusHere(0)` for `ENDGAME_FORCE_FOCUS_FRAMES` (5) frames + `SetItemDefaultFocus`. Same pattern in `pdgui_layout.cpp:307-315` (renderConfirmModal). Pattern was extracted from per-dialog ad-hoc code per `menupool.c:43-48` comment.
- **Shared widget helpers enforce label-left.** `pdgui_widgets.h:5-8` documents the spec. `pdguiCheckbox/Combo/SliderInt/SliderFloat/InputText` used 78 times across 12 files.
- **Theme system.** `pdgui_theme.cpp` decodes ROM textures to RGBA32, uploads as GL textures. Theme editor maintains a 24-element working palette with bundle support (palette + chrome style + font in one activation).
- **Test invariants.** `test_menu_stack.cpp` covers 9 lifecycle cases. `test_menu_reachability.cpp` specifies the flat-menu rule. `test_right_stick_scroll.cpp` formalizes the math (deadzone, curve exponent, cap, monotonicity, sign, dt, accumulator). `test_menu_graph.cpp` uses static source-text scanning to ensure no raw key polling appears in menus.

### Phase A: What's wrong or incomplete (code evidence)

- **Menu graph is partial overlay, not the layer.** `menuGraphFirePop` appears in 11 files (62 usages); `menuPushDialog / menuPopDialog` appears in 15 files (127 usages). The validated transition graph (`menugraph.h:73-110`) is aspirational rather than enforced. No test asserts that every menu navigation fires through `menuGraphFire*`.
- **`menugraph` node table not visible from header.** `menugraph.h` declares `menuGraphNode`, `menuGraphEdge`, `menuGraphNodeCount` but the table is in a `.c` file. `test_menu_graph.cpp` checks no-raw-key-polling but does not verify the graph itself is complete or consistent (every node has at least one inbound and outbound edge).
- **Three menu item types are deferred.** `pdgui_menu_warning.cpp:1247-1258` explicitly labels DEFERRED: `MENUITEMTYPE_LIST` (MP Pause Inventory, MP Character body/head, MP Load Settings/Preset/Player), `MENUITEMTYPE_PLAYERSTATS` (MP Pause Player Stats), `MENUITEMTYPE_RANKING` (MP Pause Player Ranking, MP Pause Team Rankings). Fallback at lines 622-629 outputs `[label]` placeholder text. The in-match inventory and ranking screens are placeholders.
- **Right-stick scroll spec decoupled from runtime.** Same finding as Section 2. `test_right_stick_scroll.cpp:14-17` says the runtime is `pdguiDriveImGuiNav`, the test suite is the spec, and there is no static or runtime test linking them.
- **Action bar adoption uneven.** Used in 19 of 31 files. The remaining 12 (`solomission`, `training`, `mpsettings` partially, `mpadvanced`, `challenges`, `logviewer`, `audiomod`, `stats`, `theme_editor`, `modmgr`, `endscreen`, plus `mpsetup` partially) place CTAs in the scroll body. UX inconsistency: a learned "primary button is at the bottom" assumption breaks in those screens.
- **Widget helpers used in 12 of 31 files.** Nineteen menus call `ImGui::Checkbox/Combo/SliderInt` directly with the right-side label default, violating the label-left spec.
- **Single-slot unregistered fallback.** `menupool.c:370-393` tracks one unregistered dialog at a time via `s_UnregisteredOwnedDef / s_UnregisteredOwnedCtx`. Two concurrent unregistered dialogs would silently leak the first's ctx. Comment acknowledges design choice; still a real gap if mod dialogs register late.
- **`pdgui_menu_audiomod.cpp` is absent from all primitive counts.** Did not appear in action bar, widget helper, or nav helper grep results. May indicate a stub body or fully manual layout.

### Phase A: To complete

- Implement `MENUITEMTYPE_LIST` primitive (read MENUOP_GETOPTIONCOUNT + MENUOP_GETOPTIONTEXT, render Selectable rows, invoke MENUOP_SET on selection). Unblocks five MP screens and the mid-match inventory.
- Implement `MENUITEMTYPE_PLAYERSTATS` and `MENUITEMTYPE_RANKING` renderers calling `mpGetPlayerRankings / mpGetTeamRankings`.
- Wire right-stick scroll runtime to spec via static test (read `pdguiDriveImGuiNav` source, verify the three constants).
- Migrate the remaining 12 menus to action bar primitive.
- Migrate raw `menuPushDialog / menuPopDialog` calls to `menuGraphFire*` (127 sites in 15 files), populate the graph node/edge table accordingly, add a completeness test.
- Add a static guard for `ImGui::Checkbox/Combo/SliderInt` in `pdgui_menu_*.cpp` (modeled on `requireNoRawMenuShortcutPolling`).
- Audit `pdgui_menu_audiomod.cpp` to confirm its primitive use or migrate it.

---

## 4. Modding

### Phase A: Current implementation state

The pipeline is substantially implemented, not scaffolding.

**`.pdmod` archive reader** (`port/src/modarchive.c`): zip central-directory parser using system zlib. Path-traversal sanitisation at parse time (`modarchive.c:13-17`). One `FILE*` per archive for lazy per-entry extraction. Atomic write path (`modArchiveBegin / AddFileMem / AddFileDisk / Finish`) streams to `.tmp` and renames on success.

**Manifest parsing** (`modmgr.c:122-283`): custom token-by-token JSON parser sufficient for the mod.json schema. Handles `id`, `name`, `version`, `author`, `description`, `base_fallback`, `dependencies`, `content` (with bodies/heads/arenas arrays), `template`, `tags`, `requires_restart`. Falls back to filename-slug when `id` missing. `audio.ini` fallback path for legacy audio mods.

**Asset registration**: two paths. INI components (maps, characters, skins, weapons) go through `assetCatalogScanComponents` via `assetcatalog_scanner.c:205-253`. JSON-declared content goes through `modmgrRegisterModJsonContentBuf` calling `assetCatalogRegisterBody/Head/Arena`. Both paths converge on the catalog.

**VFS** (`port/src/modvfs.c`): 256 MB LRU cache over open archive handles. `modVfsMount` at `modmgr.c:1895` registers archive contents for reading via `fsFileLoad` and `fsFileSize`. No on-disk extraction at runtime.

**Mod enumeration** (`modmgr.c:1395-1586`): handles 4 candidate roots (`$E/../mods`, `./mods`, `$E/mods`, base-dir mods) with dedup. Reserved names (`shared`, `inbox`, `untrusted`) enforced at scan-walker (`modmgr.c:1478`) and as second-line in `modmgrArchivePathIsTrusted` (`modmgr.c:1252-1262`). `.legacy_backup` skipped at both layers. Auto-migration from folder-mod to `.pdmod` runs via `modMigrateRun` (`modmgr.c:1460`).

**Network manifest** (`port/src/net/netmanifest.c`): `manifestBuildForHost` reads `g_MpSetup`, `g_NetLocalClient`, `g_MatchConfig`, `modmgrGetMod()` to build `match_manifest_t` before `CLC_LOBBY_START` (`netmanifest.h:179-195`). `manifestCheck` (`netmanifest.c:2055-2172`) iterates entries, resolves via `assetCatalogResolve`, sends `CLC_MANIFEST_STATUS(READY)` or `NEED_ASSETS`. SHA-256 compared at `netmanifest.c:2085-2110`.

**Network distribution** (`port/src/net/netdistrib.c`): `SVC_DISTRIB_BEGIN / CHUNK / END`. PDCA archive of component dir, zlib compressed, chunked. Client decompresses, extracts to session-only or permanent path, hot-registers via `assetCatalogRegister` (`netdistrib.c:1156-1232`). Phase D-to-E manifest re-check at line 1258. v46 protocol added a mandatory SHA-256 digest on BEGIN (constraint ledger).

**UI** (`pdgui_menu_modmgr.cpp` + `pdgui_menu_moddinghub.cpp`): ModMgr populates via `assetCatalogIterateByType` and `modmgrGetMod*` accessors. Modding Hub has 9 tool tabs (Mod Manager, INI Editor, Scale Tool, Mod Pack, Audio Mods, Skin Editor, Map Import, Nine-Slice Chrome, Font Mod) at `moddinghub.cpp:129`.

**Property Handler DLL** (`tools/pdmod_prophandler/`): Windows Shell property handler. Surfaces mod.json fields (name, author, version) to Explorer when a `.pdmod` is selected. Standalone, not loaded by the game. Zero `LoadLibrary / dlopen` in the game code.

### Phase A: What's done right (code evidence)

- Production `.pdmod` reader and writer with atomic-rename.
- Two-tier asset registration (INI + JSON content) converging on catalog.
- VFS LRU cache eliminates on-disk extraction during normal play.
- Mod scanning enforces reserved-name discipline at two layers.
- Network manifest carries SHA-256 per-entry; netdistrib distributes missing components with zlib chunking; v46 added mandatory archive digest at BEGIN.
- UI is wired to real data, not stubs.
- Property handler is correctly scoped: external tooling only, runs through Explorer, not in-process to the game.

### Phase A: What's wrong or incomplete (code evidence)

- **Dead legacy manifest serialiser.** `modmgrWriteManifest / modmgrReadManifest` (`modmgr.c:2456-2554`) implement a custom binary format that predates `match_manifest_t / manifestBuildForHost`. Search finds no live netplay caller. `modmgrReadManifest` content-hash compare uses CRC32 of `id:version` string at `modmgr.c:2534-2543`, weaker than the SHA-256 path. Should be removed or documented.
- **`.pdmod` cannot deliver INI-based components.** When a `.pdmod` mounts via `modVfsMount`, the JSON content path registers bodies/heads/arenas/audio. But the INI scanner (`assetcatalog_scanner.c`) uses `fopen / stat / opendir`, expecting on-disk files. A `.pdmod` containing a `maps/` subdir with `map.ini` and geometry would not appear in the catalog from the archive. Maps and characters via `.pdmod` are folder-only.
- **SHA-256 mismatch between archive and folder mods.** `modmgr.c:1331` calls `modArchiveSha256(archivePath, ...)` for archive mods (file-level digest). Folder mods compute SHA-256 over `mod.json` only at `modmgr.c:1074`. `manifestCheck` at `netmanifest.c:2090-2093` compares these as if equal, so client-folder vs server-archive (same content) always disagrees, triggering an unnecessary `NEED_ASSETS` distribution.
- **NetDistrib hot-register skips `g_ModRegistry`.** `netdistrib.c:1200` calls `assetCatalogRegister` directly. The component appears in the catalog but `modmgrGetCount() / modmgrGetMod()` iterate `g_ModRegistry` which is not updated. Mod Manager UI will not show distributed mods until next launch.
- **`manifest_pure.c` is hand-synced.** `tests/manifest_pure.c:1-31` documents the manual extraction with `@SYNC` line-number comments pointing into `netmanifest.c`. As `netmanifest.c` evolves (over 2000 lines), this drifts. The test catches container bugs but not production-only drift.
- **`net.h:13-17` references a header field not visibly verified.** Comment notes a hash field on `SVC_DISTRIB_BEGIN`. The receiver at `netdistrib.c:106` carries `expected_sha256`, but the verify call before extraction is not in the surveyed excerpts. Worth confirming end-to-end.

### Phase A: To complete

- Audit and remove or wire `modmgrWriteManifest / modmgrReadManifest` (`modmgr.c:2456-2554`).
- Extend `modmgrLoadMod` for archive mods to also drive the INI scanner over archive contents (new helper `assetCatalogScanComponentsFromArchive(mod_id, archive_handle)`).
- Unify the SHA-256 fields: both folder and archive mods digest `mod.json` bytes.
- Call `modmgrRescanDirectory` after `SVC_DISTRIB_END` (`netdistrib.c:1258`) so `g_ModRegistry` reflects distributed mods.
- Replace `manifest_pure.c` hand-sync with a compile-boundary approach: factor container/hash/diff/serialise into a TU that imports no globals, link both production and test against the same TU.
- Verify the `expected_sha256` compare runs before `extractArchive` writes to disk.

---

## 5. Connectivity / Online

### Phase A: Current implementation state

The networking subsystem is substantially real code. The 6-tier P2P state machine, protocol layer, presence pings, and voice skeleton are wired. Main structural gap: signaling exchange of peer endpoints before session start is not fully bridged from presence/invite into the p2p orchestrator.

**P2P orchestrator** (`port/src/net/p2p.c`): 64-entry pair table (`P2P_MAX_PAIRS`, p2p.c:24). `p2pPairBegin` (line 223) allocates a slot, stores hint IP/port, calls `enterTier(p, P2P_TIER_LAN)` (line 256). Escalation chain: LAN (0), DIRECT (1), STUN (2), UPNP (3), ICE (4), TURN (5). Per-tier soft timeout via `P2P_TIER_TIMEOUT_MS`. `p2pTick` (line 182) polls all six tier polls; `escalate` (line 136) advances on overage; exhaustion sets `P2P_PAIR_FAILED` (line 146). Late-success accepted (line 333). Diag API (`p2pPairDiag`) at line 370.

**Tier modules**:
- LAN (`p2p_lan.c`): broadcast UDP port 27101, 32-byte PDLAN datagrams every 3s, 64-entry cache with 15s TTL, BYE on shutdown, version-mismatch drop.
- Direct (`p2p_direct.c`): 16-byte PDDIR probe to hint IP/port, ACK-reflection, nonce-matched. Fails immediately if both hints zero.
- STUN (`p2p_stun.c`, `netstun.c`): RFC 5389. `stunDiscoverAsync(0)` kicks a worker thread. Two-probe NAT type detection (cone vs symmetric). Public Google + Cloudflare server list. Reports local reflexive on success.
- UPnP (`p2p_upnp.c`, `netupnp.c`): miniupnpc on background thread. HTTP fallback via libcurl against `api.ipify.org`. Server auto-starts UPnP and STUN at `netStartServer` (`net.c:896-902`). Skips `UPNP_DeletePortMapping` when `g_AppQuitting` (avoids 10-30s blocking).
- ICE (`p2p_ice.c`): two local candidates (default NIC via UDP-connect-trick + STUN reflexive), parallel PDICE 16-byte probes. `p2pIceAddPeerCandidate` exists (line 260) for remote candidate injection.
- TURN (`p2p_turn.c`): selects highest-kbps candidate from `s_Cands`. Player-hosted relay, not RFC 5766.

**netholepunch.c**: a separate older DIRECT to PUNCH to PUNCH_ENET waterfall using the ENet socket directly (`netholepunch.c:175-210`). Predates the tier machine, handles ENet connection phase after NAT piercing.

**Presence** (`presence.c`): no external presence service. Always-on UDP socket port 27105. 184-byte frames signed Ed25519 over first 120 bytes plus domain string `"pd-presence-v2"` (line 64-70). v2 added pubkey + signature on 2026-04-25. Ping interval 30s, online-window 60s. Friends pinged via social friend list iteration. No lobby browser, no HTTPS matchmaking.

**Voice** (`voice.c`): gated on `HAVE_OPUS`. When undefined, module is no-op shell with state, settings, PTT toggle, UI hooks but no encode/decode/wire. When defined: SDL capture device 16 kHz mono S16, per-peer `OpusDecoder`, frame Ed25519 signed over `"pd-voice-v1"`, friend allowlist + handle binding + per-friend mute. UDP port 27108.

**Connect codes** (`connectcode.c`, `test_connectcode.cpp`): 256-entry word dictionary, 4-slot or 6-slot form (with port). Byte-order convention documented at `test_connectcode.cpp:19-33`. Static test at lines 193-266 reads source files via `std::ifstream` and verifies UI surfaces use `connectCodeDecodeWithPort`, "Connect Code:" present, "Enter IP:port" labels absent.

**netbuf** (`netbuf.c`): cursor-based buffer, multi-byte writes via `PD_LE16/LE32/LE64`, error-sticky flag, length-prefixed strings, zero-length safe static empty string return.

**Server-authoritative** (`netmsg.c`): `g_NetMode == NETMODE_SERVER` is the authority gate. `test_net_lifecycle_static.cpp` verifies parse-before-commit ordering on `CLC_LOBBY_START`, `CLC_AUTH`, `CLC_BOT_MOVE`, `CLC_MOVE` weapon select, chat rate-limit, room rate-limit, desync threshold.

**Listen vs dedicated**: `g_NetDedicated` checked at `netmsg.c:721` for CLC_AUTH ROM/mod skip. Bot authority at `netmsg.c:7298`. Listen-only server tick at `netmsg.c:7242`. SVC_BOT_AUTHORITY at `netmsg.c:7704`.

### Phase A: What's done right (code evidence)

- 6-tier escalation orchestrator (late-success handling, stale-failure filtering, per-tier accounting, diagnostic API).
- LAN tier complete (BYE, TTL eviction, version filtering, block-list integration).
- STUN RFC 5389 compliant (XOR-MAPPED-ADDRESS + MAPPED-ADDRESS fallback, two-probe NAT type, real public servers).
- UPnP background thread; quit-guard skips slow teardown.
- Protocol buffer is safe (overread, overwrite, malformed strings, error-sticky).
- Static tests verify parse-before-commit ordering on every sensitive handler.
- Connect codes hide raw IPs; static tests gatekeep UI regression.
- Dedicated server skips ROM/mod check at `g_NetDedicated` boundary.
- Voice gated on optional dependency with graceful no-op.
- Presence Ed25519 signed; v2 pubkey-bound.
- TURN selects by measured kbps from `groupSessionUpdateKbps`.

### Phase A: What's wrong or incomplete (code evidence)

- **ICE peer candidate exchange not wired.** `p2pIceAddPeerCandidate` (`p2p_ice.c:260`) exists but is never called from presence or invite layer in the surveyed files. Without it, ICE tier probes only local NIC + local STUN reflexive against the hint, functionally equivalent to Tier 1 with backup.
- **STUN reflexive not transmitted to peer.** `p2p_stun.c:125-131` reports success with local reflexive as endpoint but does not coordinate with remote. Real STUN-based hole punching needs both sides to know each other's reflexive. Presence packet has `listen_ipv4 / listen_port` fields (`p2p_lan.c:12-16` documents the same shape) but the bridge from `p2pPublishMyReflexive` to presence outbound is not visible.
- **kbps measurement is a placeholder.** `groupSessionRecomputeAuthority` at `group_session.c:136` comments `/* placeholder for local kbps until measurement is wired */`. Local kbps initialized to 0; first non-zero reporter wins authority.
- **UPnP only maps the ENet port.** `p2p_upnp.c:75-76` calls `netUpnpSetup(g_NetServerPort ? ... : NET_DEFAULT_PORT)`. Direct probe (27102), ICE (27103), TURN (27104) sockets remain unmapped. Tier 3 UPnP success is partial.
- **TURN has no public fallback.** `p2p_turn.c:211-213` reports "no relay available" when `s_Cands` is empty. Brand-new server with no players in group session fails at tier 5.
- **netholepunch parallel to tier machine.** Two systems overlap. The handoff from `p2pPairGetEndpoint` to `enet_host_connect` is not visible in surveyed files.
- **Voice silent without `HAVE_OPUS`.** `voice.c:16-19` documents the manual `pacman -S` step. Build script does not check or auto-set the flag.
- **netupnp.c shadowed variable.** `netupnp.c:120-121` shadows outer `u16 port` parameter with inner local. Compiles cleanly but is a confusion point. No functional error today.

### Phase A: To complete

- Wire `p2pIceAddPeerCandidate` from presence/invite delivery (likely in `group_session.c`).
- Publish local STUN reflexive in presence pings so peers can inject as ICE candidates.
- Replace placeholder kbps in `groupSessionRecomputeAuthority` with real measurement (ENet `peer->outgoingBandwidth` or sliding-window byte count).
- Extend UPnP to map p2p probe ports (27102, 27103) in addition to the ENet port.
- Add a configurable public TURN fallback when `bestRelayCand()` is NULL.
- Detect Opus via CMake `find_package` and auto-set `HAVE_OPUS` rather than relying on manual pacman.
- Unify the hole-punch flow: `netStartClientWithHolePunch` should consume `p2pPairGetEndpoint` rather than running its own STUN + punch in parallel.

---

## 6. Test Framework

### Phase A: Current implementation state

Catch2 v2 single-header (`port/include/catch.hpp`) drives a `pd-tests` CMake target alongside `pd` and `pd-server`. Catch2 main is generated in `tests/main.cpp:12` via `#define CATCH_CONFIG_MAIN`. All other test files include `catch.hpp` without that define.

Build integration in `CMakeLists.txt:840-979`. Source list `SRC_TESTS` is explicit (no GLOB), pulls in:

- 33 test_*.cpp files plus `test_versions_pin.c`.
- Pure-C mirrors: `actionmap_pure.c`, `inputctx_pure.c`, `inputlayer_pure.c`, `manifest_pure.c`, `menupool_pure.c`, `savebuffer_pure.c`, `scene_pure.c`.
- 8 cherry-picked pure source files from `port/src/`: `netbuf.c`, `connectcode.c`, `bondgun_cache.c`, `catalog_checked.c`, `spawn_predicate.c`, `bodies_headcount.c`, `options_forced.c`, `catalog_mgr_weapons_pure.c`.
- `tests/stubs.c` for symbols the cherry-picked sources reference but the test binary should not invoke.

`pd-tests` defines `PD_TESTS=1`, `AVOID_UB=1`, `_LANGUAGE_C=1`, `PAL=0`, `VERSION=2`, `ROM_SIZE=32`, `PIRACYCHECKS=0`, `MATCHING=0`. Static linkage on Windows (`-Wl,-Bstatic -lwinpthread`).

Test inventory (counts as of the read):

- **Catalog**: `test_catalog_checked`, `test_catalog_mgr_weapons_api`, `test_catalog_provider_static`, `test_loader_pdbase_scan`, `test_weapon_findbyid_migrated`, `test_weapon_direct_reads_audit`, `test_bondgun_cache`, `test_bodies_headcount`, `test_spawn_predicate`, `test_spawn_weapon_resolved`, `test_spawn_weapon_mode`.
- **Input**: `test_actionmap_flush`, `test_input_authority`, `test_input_layer_stack`, `test_right_stick_scroll`, `test_social_toggle_imc`, `test_editor_tool_hotkeys`.
- **Menu**: `test_menu_stack`, `test_menu_reachability`, `test_menu_graph`.
- **Scene**: `test_scene_dispatch`, `test_cutscene_layer`, `test_vehicle_observer_layer`.
- **Network**: `test_netbuf`, `test_net_lifecycle_static`, `test_connectcode`, `test_manifest`.
- **Save**: `test_savebuffer`, `test_save_migration`.
- **Modding social**: `test_public_mods_static`.
- **Misc**: `test_smoke`, `test_versions`, `test_versions_pin.c`, `test_random_pool`, `test_options_forced`, `test_swarm_boid_sim`.

Dev runner: `devtools/run-pd-tests.ps1` accepts a session id and a Catch2 selector or a `-Scope` alias. Aliases: `catalog`, `catalog-provider`, `catalog-identity`, `input`, `manifest`, `save`, `netbuf`, `connectcode`, `network-lifecycle`, `spawn`. Builds via `build-session.ps1 -Target tests` into `.claude/session-builds/<session>/`, then runs `pd-tests.exe`.

### Phase A: What's done right (code evidence)

- The `pd-tests` binary is self-contained: no SDL, no GL, no ImGui, no ENet. Compile time stays low. Tests run in seconds when built clean.
- Pure-C mirrors keep the test binary globals-free. `actionmap_pure.c`, `inputctx_pure.c`, `menupool_pure.c`, `inputlayer_pure.c`, `scene_pure.c`, `manifest_pure.c`, `savebuffer_pure.c` are all small (74 to 437 lines) and reflect the live algorithm.
- Static source-text checks (e.g. `test_weapon_direct_reads_audit.cpp`, `test_connectcode.cpp:193-266`, `test_menu_graph.cpp:66-83`) are a pragmatic way to enforce architectural invariants without linking the full game.
- Per-session isolated builds (`build-session.ps1` + `run-pd-tests.ps1`) prevent concurrent-session conflicts in shared `Build/`.
- Scope aliases shorten the friction for targeted runs.

### Phase A: What's wrong or incomplete (code evidence)

- **Pure mirrors drift from production sources.** Each of `manifest_pure.c`, `savebuffer_pure.c`, `actionmap_pure.c`, etc. has a header comment giving a manual `diff` command for drift audit. Drift is not enforced by CI.
- **F10 `.pdbase` test pins shape only.** `tests/test_loader_pdbase_scan.cpp` is static-text, no behavioral assertion. Same finding as Section 1.
- **Right-stick scroll test is unrooted.** Same finding as Sections 2 and 3. Test is the spec; runtime is not asserted.
- **`tasks-current.md` activity suggests build verification gaps.** Multiple recent tasks (S587 to S590) report "BUILD PENDING" with `pd_headers` 600s timeouts in isolated build wrappers. The framework exists; reliable runs against fresh builds appear bottlenecked on the header generation phase. (This is a process observation visible in the tasks file, not a code-only observation, but the underlying behavior is in `devtools/build-headless.ps1` ninja invocation.)
- **No coverage for several visible runtime modules.** The Forge editor (`port/src/forge/*.c`, 7 files), social shell (`social_share.c`, `social_store.c`), updater (`port/src/updater.c`), updater_standalone, voice (`voice.c`), spectator, lobby portrait baking, theme decode, and the bulk of `pdgui_menu_*.cpp` have no test files in this list.

### Phase A: To complete

- Add a CI-time drift check for each `*_pure.c` that diffs the live source slice against the test mirror at build time.
- Replace static-only `test_loader_pdbase_scan.cpp` with behavioral coverage when F11 lands.
- Pin the right-stick scroll runtime to its spec.
- Fix the `pd_headers` generation watchdog so isolated test builds complete in the 600s window. This is currently the practical limit on test verification cadence per session.
- Extend coverage by domain: forge serializer round-trip, social public-mod registry, updater HTTP error paths, theme decode, menu graph completeness.

---

## 7. Dev Tooling

### Phase A: Current implementation state

23 scripts under `devtools/` plus the WPF dev window app under `dev-window-v2/`.

Major scripts:

- `_build-env-prelude.ps1`: idempotent env setup (TEMP, TMP, PATH prepend `/c/msys64/mingw64/bin`, MSYSTEM).
- `build-env.sh`: bash equivalent.
- `build-headless.ps1` (988 lines): the canonical build entry. Smart-clean detection (compiler change, generator change, missing CMakeCache, `-Clean` flag). ccache probe before launcher inject. State persists in `Build/.last_build_state.json`. Writes per-step stdout/stderr/exit-code logs at `_build-headless-<timestamp>-<step>.*`. Heartbeat with PID list + ninja_log size. Captures CMake error log on configure failure. SP-9 truncation guard before auto-commit (skips commit if any file shrank net by 20+ lines without compensating additions). Self-test mode (`-SelfTest`) verifies the wrapper itself.
- `build-session.ps1`: per-session isolated build under `.claude/session-builds/<session-id>/`. Queue, lock, watchdog, PID tracking. `-Tail`, `-List`, `-Remove`.
- `release.ps1` (1021 lines): version sync (max of CMakeLists + git tags + 1 patch), dual-channel (stable / nightly prerelease), gh CLI integration, dev-tag prune (newest 10), no-BOM UTF-8 CMakeLists rewrite, `-DryRun`, `-SkipBuild`.
- `run-pd-tests.ps1`: scope-alias runner over `build-session.ps1`. ListScopes, ListTags, ListTests, BuildTimeoutSeconds.
- `keygen.ps1`, `ensure-keypair.sh`, `sign-release.ps1`: Ed25519 keypair management for release signing (paired with `port/include/updater_pubkey.h`).
- `cursor-build.ps1`: Cursor IDE-specific build wrapper.
- `git-snapshot.sh`, `git-verify-snapshot.sh`: pre/post-op snapshot helpers per `CLAUDE.md` Git Safety section.
- `check-worktree.sh`, `cleanup-worktrees.sh`: worktree hygiene.
- `version-util.ps1`: sourced helper, `Get-NextReleaseSemVer` and `Set-CMakeListsSemVer`.

Dev Window v2 (`devtools/dev-window-v2/`): WPF GUI for build / run / version / status / git Pull and Push. DPI-aware (S255). Async RunspacePool (1-3 threads) for non-blocking UI updates (S361). Pre-build git sync (S257). Single-exe consistency rules (S270).

In-game playtest dashboard exists as a runtime overlay (referenced in `port/include/devwindow*` paths and `pdmain.c` per grep). Mike tests in-game; AI builds via `build-headless.ps1` and reads logs.

Updater standalone (`port/src/updater_standalone/updater_gui.c`): a separate GUI updater, `Updater.exe`, packaged into release zips per `release.ps1`. Recovery path if in-process self-update fails.

### Phase A: What's done right (code evidence)

- Build invariants encoded once: `_build-env-prelude.ps1` is the single source of truth, dot-sourced everywhere.
- Self-test mode for the wrapper itself (`build-headless.ps1 -SelfTest`).
- SP-9 truncation guard prevents auto-commit when files shrink unexpectedly.
- Per-step exit-file logging means a killed wrapper still leaves diagnosable artifacts on disk.
- ccache probe avoids hangs in sandbox environments where ccache cannot launch the compiler.
- Per-session isolated builds eliminate the shared-`Build/` race between concurrent sessions.
- Release pipeline knows about both client and server, includes `Updater.exe`, excludes ROMs, generates source archives via gh.
- All scripts redirect worktree paths back to the main working copy (`build-headless.ps1:87-92`). Worktree builds are explicitly disallowed.
- Ed25519 keypair generation is automated on first build (`build-headless.ps1:852-864`).
- No-BOM UTF-8 CMakeLists writer in `release.ps1:90-93` prevents the dirty-tree-after-release bug.

### Phase A: What's wrong or incomplete (code evidence)

- **`pd_headers` generation step appears watchdog-prone.** Multiple recent tasks-current.md entries (S587-S590) report `pd_headers` ninja step running for 478-589 seconds against a 600s watchdog with empty stderr and `(no live child process rows collected)` heartbeat. This is a process artifact visible in those logs and in the wrapper code at `build-headless.ps1:902-910` where `pd_headers` is invoked with `-j1 -v`. Without binary verification, source-only changes accumulate.
- **The `pd_headers` invocation uses `-j1`.** `build-headless.ps1:904`. This is intentional (the comment says single-job avoids generated-header race conditions) but the per-step watchdog path may be hitting it because some header-generation tool is blocking on Python or Git invocation rather than running.
- **Multi-thousand-line PowerShell scripts.** `build-headless.ps1` is 988 lines, `release.ps1` is 1021 lines. This is mostly appropriate for the surface area, but logic like the SP-9 guard is in-line in `build-headless.ps1:763-804` rather than in a sourced helper, so a future bug in the regex would silently change the guard.
- **No code-level test for the build scripts.** PowerShell parser checks happen, and the `-SelfTest` flag does limited regression for the wrapper itself, but the smart-clean heuristic, the ccache probe path, and the SP-9 truncation guard are not unit-tested.
- **Worktree-redirect logic is duplicated.** `build-headless.ps1:85-92` redirects worktree to main project. The same logic appears in `build-session.ps1` and `cursor-build.ps1`. If one drifts, builds in worktrees go to different places.

### Phase A: To complete

- Investigate the `pd_headers` 600s stall. Likely Python tool or Git probe blocking under the wrapper's launcher policy. Surface real progress (which generator script is running) in the heartbeat log.
- Factor the SP-9 truncation guard into a callable function in `version-util.ps1` (or a new `_build-safety.ps1`) so it can be unit-tested.
- Centralize the worktree-redirect into one helper called by every script.
- Add a smoke test that the smart-clean heuristic correctly forces a clean on generator change.

---

## 8. Save / Wire Format

### Phase A: Current implementation state

**Save format**: PC-native JSON files. Replaces N64 EEPROM. `port/include/savefile.h:41`: `SAVE_VERSION = 2`. Four save types: agent (`saveagent`), MP player (`savemplayer`), MP setup (`savempsetup`), system (`savesystem`). Files: `agent_<name>.json`, `player_<name>.json`, `mpsetup_<name>.json`, `system.json`.

`saveagent` struct (`savefile.h:55-110`): version, name, totaltime, autodifficulty/autostageindex/thumbnail, `besttimes[60][3]` (60 stages, 3 difficulties), `coopcompletions[3]` (bitmask), `firingrangescores[9]`, `weaponsfound[6]`, control mode arrays, audio volumes, `challengecompleted[128]`. Generous allocations explicitly to escape N64 bit-pack limits.

`savemplayer` (`savefile.h:116-157`): `head_id[CATALOG_ID_LEN]` and `body_id[CATALOG_ID_LEN]` are the SA-4 string-IDs, not raw indices. Stats are u32 (no truncation). Medals u32. Playtime u32 seconds.

`savempsetup` (`savefile.h:172-194`): `stage_id[CATALOG_ID_LEN]`, weapons[8] (expanded from 6), `bots[SAVE_MAX_BOTS]` (32), `playerTeams[SAVE_MAX_PLAYERSLOTS]` (8).

JSON parsing: minimal hand-written tokenizer in `savefile.c:40-100+`, same approach as `modmgr.c` and `updater.c`.

**Save migration framework** (`savemigrate.c`): chain-based. `saveMigrateRegister(type, fromVersion, toVersion, fn)`. 32-slot registry. `saveMigrateBackup(filepath, version)` copies to `<path>.v<N>.bak` (refuses to overwrite an existing backup). `saveMigrateCheck`: returns 0 (target match), 1 (migration available), -1 (file is newer, downgrade case). The framework is wired and ready; SAVE_VERSION = 2 currently.

**Wire format**: per `constraints.md`, `NET_PROTOCOL_VER = 46` (latest bump 2026-04-28 for mandatory mod-transfer digest). `MPSETUP_VERSION = 2`. The version constants are pinned by `tests/test_versions_pin.c` reading the live header. Mixed-version play rejected at ENet auth handshake (`netServerEvConnect` in `port/src/net/net.c:1560` per the constraint ledger).

`mpsetupfileLoadWad` migration rule (per `tests/test_save_migration.cpp:208-224`): `if (version < 2)` block clamps weapons[i] >= 0x27 to `MPWEAPON_DISABLED` and zeroes the random-filter mask. Static guard in the test file pins the live loader's gate.

Bit-pack primitives (`savebuffer.c` lines 381-471, mirrored in `tests/savebuffer_pure.c`): `savebufferOr` (set bits), `savebufferReadBits`, `savebufferClear`. Tests cover 1-bit, 8-bit, 13-bit cross-byte, multi-field roundtrip mirroring `mpsetupfileSaveWad`, 64-bit `wpnRndPacked`, boundary widths, alternation patterns, zero-buffer reads.

### Phase A: What's done right (code evidence)

- Save format is human-readable JSON; mod-friendly, debuggable, extensible.
- Each save file is self-versioned. Schema can grow without offset math.
- Migration framework exists and is wired even though only one version step (1 to 2) is in flight.
- `tests/test_save_migration.cpp` uses pure-helper logic plus a static-text guard reading `src/game/mplayer/mplayer.c` to pin the live loader's `if (version < 2)` block. Future loaders that drop the version gate will fail the test.
- Wire protocol version is pinned by test (`test_versions_pin.c`).
- Bit-pack primitives have exhaustive unit tests covering boundary widths up to 63 bits and 64-bit roundtrip.
- Catalog ID strings replace raw indices at all save-file string boundaries (`head_id`, `body_id`, `stage_id`).

### Phase A: What's wrong or incomplete (code evidence)

- **`tests/savebuffer_pure.c` is hand-synced.** Header comment at line 12-17 documents a manual `diff` command for drift audit. If `src/game/savebuffer.c` lines 381-471 evolve, the test mirror will silently drift.
- **`test_save_migration.cpp` migration helper duplicates the live rule.** Lines 54-71 reimplement `migrate_v1_to_v2` and `migrate_random_filter_mask_v1_to_v2`. The test at lines 208-224 pins the live block via static text. If the live block evolves with new clamps not mirrored in the helper, the static text changes and the helper goes stale together (the test catches the change but cannot say which side moved).
- **Save migration registry is under-exercised.** `savemigrate.c` framework supports chains (1->2->3->...), but only one migration is in the wild. The cross-migration glue path is not behaviorally tested.
- **Multiple version constants live in different files.** `SAVE_VERSION = 2` is in `savefile.h`. `MPSETUP_VERSION` is in `port/include/mpsetups.h`. `NET_PROTOCOL_VER` is in `port/include/net/net.h`. The constraint ledger is the only place that lists all three together with reasoning. A code reader inspecting one file does not see the others.
- **Save file directory placement and listing.** `saveListAgents` API exists at `savefile.h:237` but the implementation surface and disk scan logic was not read in this session. The header documents the intent; behavior verification requires a fresh read.

### Phase A: To complete

- Replace `tests/savebuffer_pure.c` and `tests/manifest_pure.c` hand-sync with a CI-time `diff` check that fails the build on drift.
- Add a chained-migration behavioral test (set up SAVE_VERSION = 4 fixture, register dummy 1->2, 2->3, 3->4 migrations, assert each ran in order).
- Co-locate version constants in a comment header in each version-bearing file referencing the others, so a code-only reader can find related constants without context/.

---

## 9. Surfaced Extras (not in the 8-pillar list)

The following exist in code and are worth flagging for the same status read.

### 9.1 Updater (`port/src/updater.c`, `port/src/updater_standalone/`)

`updater.c` uses libcurl for HTTPS to GitHub Releases API. Mini JSON tokenizer (same pattern as `savefile.c` and `modmgr.c`). SDL_mutex-protected shared state. SDL_atomic cancel flag (SEC-23). Mozilla CA bundle embedded as `cacert_blob.h` to escape `SSL_CTX_load_verify_store` issues on MSYS2 libcurl. Ed25519 signature verification via `ed25519.h` and embedded `updater_pubkey.h`. Two channels: stable and dev (`Updates.ShowDevReleases` config). `UPDATER_DEFAULT_PROTECTED = "mods,data,extracted,saves"`.

Standalone `Updater.exe` in `port/src/updater_standalone/updater_gui.c` plus `updater.manifest`. Packaged into release zips per `release.ps1` so users have a recovery path if in-process self-update breaks.

### 9.2 Property Handler DLL (`tools/pdmod_prophandler/`)

Windows Shell extension. Implements `IInitializeWithStream` to surface `mod.json` fields in Explorer details pane on `.pdmod` files. Local zip and JSON parsers (`pdmod_zip_inflate.cpp`, `pdmod_json_min.cpp`). Standalone DLL, registered via `regsvr32`. Not loaded by the game (zero `LoadLibrary / dlopen` in port/src/).

### 9.3 Forge level editor (`port/src/forge/*.c`)

7 files: `forge_ai.c`, `forge_core.c`, `forge_gametype.c`, `forge_logic.c`, `forge_runtime.c`, `forge_serialize.c`, `forge_undo.c`. Plus `pdgui_forge_editor.cpp` and `pdgui_forge_hud.cpp`. Five new `ACTION_FORGE_*` actions wired (per code grep). Editor mode toggle, freefly camera, catalog placement.

### 9.4 Social shell (`port/src/social_share.c`, `social_store.c`)

Public-mod registry, sharing surface. `pdgui_friends.cpp` UI. Phase 2 connectivity work on dedicated UDP sockets (port 27109 PDSHR per constraint ledger), Ed25519 signed.

### 9.5 Voice (`port/src/voice.c`)

`HAVE_OPUS` gated. UDP port 27108, `"pd-voice-v1"` Ed25519 domain. SDL capture device, per-peer `OpusDecoder`, friend allowlist + per-friend mute.

### 9.6 Spectator (referenced from `pdgui_spectator.cpp`)

Mentioned in test `test_input_authority.cpp`, `test_vehicle_observer_layer.cpp`. Spectator does not consume a player slot. v42 protocol added `CLC_SPECTATE_REQUEST 0x16`, `SVC_SPECTATE_ACK 0x6a`, `SVC_STATE_FRAME 0x6b` at 10 Hz (per constraint ledger).

These extras represent code surface that has been built and is shipped (or near-shipped) but is not on the explicit pillars list. They each have their own incomplete edges and could each merit a similar code-vs-context audit.

---

# PHASE B: Context Comparison

This phase reads `context/`. For each pillar I compare what code shows against what context claims.

## Cross-cutting comparison observations

Three observations apply across multiple pillars before the per-pillar comparison.

**Observation 1: `infrastructure.md` is significantly behind the live code.** The header line is "Last updated: 2026-04-18, S361 Dev Window v2 async RunspacePool. Wire protocol at v37." Today's code has wire protocol v46 (per `constraints.md` and `port/include/net/net.h`), session log is at S590 (today), and many of the post-S361 work (S483 spawn weapon mode, S486 in-client shipping pivot, S507 v46 distribution digest, S511 v46 cutscene, S484 catalog F1-F10 weapons manager, the entire menu graph migration, the testing framework, etc.) is not reflected. The Phase Status table lists D8 NAT Traversal as DONE, AP (Asset Provider) as Phase 1+2+3 done with Phase 4 remaining, R-5 server GUI redesign as planned, but does not mention the catalog manager scaffold, the testing framework as a phase, or the menu graph as a phase. This file is a snapshot, not a live tracker.

**Observation 2: `constraints.md` is current and rich.** The constraints ledger has entries dated 2026-04-27 and 2026-04-28, including S483-S511. It matches code state for catalog universality (S485, S487, S488, S577, S578), spawn-weapon sentinels (S483 / S483b / S483c), wire protocol (v46 at top), and the in-client shipping pivot (S486). This is the single most reliable context file for current invariants.

**Observation 3: `tasks-current.md` is a frenetic punch list with many "BUILD PENDING" entries.** S587, S588, S589, S590 each report a slice of work landed source-checked but not binary-verified, blocked on a 600s `pd_headers` watchdog in the isolated build wrapper. The build infrastructure itself is healthy (`build-headless.ps1 -SelfTest` passes), but the `pd_headers` step is currently the practical bottleneck for test verification cadence.

## Per-pillar context comparison

### 1. Catalog system

**Where code matches context.** `constraints.md` lines 39-41 (S485, S487, S488) describe the catalog-owned asset lifecycle, typed identity helpers, and typed manifest/screen/stage boundaries. The code reflects this: `catalogStageIdByStagenum / catalogModelIdByModelnum / catalogWeaponIdBy*` are present, `catalogLoadTypedAsset / catalogReleaseTypedAsset / catalogRetainTypedAsset` are the typed entry points, and the untyped wrappers are not used by production code (per `tests/test_catalog_provider_static.cpp`). S577 / S578 (FileProvider / RomProvider catalog ownership) are pinned by the same test.

The S484 weapons manager F1-F10 work is reflected in the recent commit log (which is not context, but the code itself). The catalog-mgr-weapons static tests `test_weapon_findbyid_migrated.cpp` and `test_weapon_direct_reads_audit.cpp` exist in the test list, matching what infrastructure.md does not cover at all but `constraints.md` indirectly endorses through the catalog-owned-lifecycle constraint.

**Where context claims more than code shows.** `infrastructure.md` does not mention the catalog manager scaffold or S484 weapons F1-F10 at all. Closest entry is "AP Phase 1+2+3 DONE; Phase 4 remains" at line 80, but Phase 4 is described as "retire `filenum` as public identity" which is a different (parallel) track from S484's data-move-to-`.pdbase` plan. The `full-release-roadmap-2026-04-27.md` design doc (per the index entry) was not read in this session, and may carry the missing detail.

**Where code shows more than context describes.** The F10 `.pdbase` loader scaffold lives in `port/src/loader_pdbase.c` with header `port/include/loader_pdbase.h`. Neither file is mentioned in `infrastructure.md` or `constraints.md`. The scaffold exists in code, is tested by `test_loader_pdbase_scan.cpp` with shape-only static checks, and is documented internally to the file. Context does not yet describe the .pdbase format, the migration plan, or the loader contract.

The 41-vs-86 catalog/manager slot gap (Section 1.3) is not described in any context file read. It is a design choice consistent with S484 Phase 2 scope but is not surfaced anywhere a future reader could find without reading the code.

### 2. Input system

**Where code matches context.** `context/designs/input-authority-and-menu-pool-2026-04-13.md` (referenced in README.md index) presumably documents the ADR for input authority + menu pool. `constraints.md` line 47 documents that mouse capture is driven by the input context stack and that no menu may call `SDL_SetRelativeMouseMode / SDL_ShowCursor` directly. Line 56 documents the layer-transition flush rule (S489, B-266) including the cutscene reference implementation. Line 57 documents the C-button decoupling (presentation-only).

Code reflects all these: `inputCtxSyncMouseMode`, the suppression predicate `gameplayInputSuppressed()`, the layer flushing pattern in `inputlayer.c:117` for cutscene, and the C-button actions retained but hidden in the controller tab.

**Where context claims more than code shows.** I was unable to read `input-authority-and-menu-pool-2026-04-13.md` directly in this session. The README.md description says "Phase 1 shipped S250; Phase 2 queued." Code shows Phase 2 is partially shipped (`menupool` exists, IMC ownership migration is described as "queued" in Section 1.0 of menu pool but the code at `menupool.c:43-48` has comments suggesting the migration completed for renderers). Mismatch may exist between what the ADR says is queued and what is actually in production.

**Where code shows more than context describes.** The right-stick scroll spec test (`test_right_stick_scroll.cpp:14-18`) is not documented in any context file I read. The runtime decoupling from spec is a real test gap that no context entry warns about.

The 10 raw F-key handlers in `pdgui_backend.cpp:1288-1379` are not documented as a constraint or known issue. They are flagged only by an inline comment at line 1284 ("any new raw SDL hotkey if-block added below must also be added to registerDebugShortcuts()"). This is a soft contract enforced by reviewer attention, not by a static test.

### 3. Menu / UI / UX

**Where code matches context.** `constraints.md` line 46 documents that ImGui is the sole menu system, the legacy stack is plumbing-only, and that menu work targets the ImGui layer. Line 64 documents the menu pool as the structural dedup layer with key `menu_type_t`, `s_Pool[MENU_TYPE_COUNT]`, force-close sites enumerated. Line 63 documents `matchConfigMaxBotsForHumans` (relevant to room screen, but not a menu invariant per se).

Code reflects this. The legacy `menuPushDialog / menuPopDialog` calls remain as plumbing, which matches the constraint that says "retained as plumbing." The 31 ImGui menus + 1 retired matches "ImGui sole menu system."

`README.md` index references `designs/d5-full-menu-overhaul.md` for menu work and `designs/menu-inventory.md` for "120 screens: status, file path, D5 phase." Code shows 31 menus, plus internal sub-renderers, which can plausibly aggregate to ~120 screens depending on how each menu's panels count. The inventory file would be the place to verify screen-by-screen.

**Where context claims more than code shows.** The README.md says D5 Phase 5 (lobby scene) is DONE with portraits S352, hover preview / drop shadow / team border S356. Code shows a `LobbyPortrait` struct mentioned in commits and references to `pdguiRoomScreenReset()`, but the live state of hover preview was not directly verified in this session. Could be done as claimed; could be partially done.

`designs/forge-level-editor-2026-04-16.md` (per README.md index) presumably covers Forge polish; code shows `forge_*.c` files with presumably more functionality than the F0 description in `infrastructure.md` line 73. The S313 polish session (per session-log.md S313 entry, 2026-04-17) added bots tab, map variant editing, weapon pad preview, controller nav, mod dependency save modal, ghost preview, grid snap visualization, all in The Grid editor. None of this is explicitly documented in `infrastructure.md` line 73, which only says "F0 shipped S307."

**Where code shows more than context describes.** The MENUITEMTYPE_LIST / PLAYERSTATS / RANKING deferral at `pdgui_menu_warning.cpp:1247-1258` is documented inline ("DEFERRED:") but is not surfaced in `tasks-current.md` or any of the design docs I read. A reader of context would not know that the in-match inventory and ranking screens are placeholders.

The `pdgui_menu_audiomod.cpp` is in the file list but absent from all primitive counts (action bar, widgets, nav). I cannot tell from code alone whether it is a stub or just uses a different layout style. Context does not flag this.

### 4. Modding

**Where code matches context.** `component-mod-architecture.md` (per the README.md index) is the canonical doc. `constraints.md` line 88 (Removed: "Numeric asset lookups") and line 97 (Removed: "chrslots bitmask") match the catalog-string-only architecture. Line 60-61 (`mods/` tree depth cap, two levels) matches the `modmgrScanDirectory` and `s_scanModChromeStyles` two-layer scanners.

`tasks-current.md` S587 (2026-04-29) describes the Public Mods publishing slice with safe ID validation, registry-backed UI, JSON escaping. Code shows `social_share.c` with `shareModIdIsSafe`, `findMyPublicMod`, `writeJsonString`, and `tests/test_public_mods_static.cpp` exists. This is current.

Property Handler DLL (`tools/pdmod_prophandler/`) is not described in `infrastructure.md` but exists as compiled tooling.

**Where context claims more than code shows.** `infrastructure.md` lists D14b "Mod Distribution DONE" (line 72) with D3R-9 + D3R-10 + A-7 + S-9. Code shows D3R-9 (network distribution) and netdistrib are wired, but the SHA-256 mismatch between archive-mod and folder-mod paths (Section 4) is a real gap that "DONE" overstates. The compatibility check unnecessarily triggers redistribution.

**Where code shows more than context describes.** The `.pdmod` archive cannot deliver INI-based components (Section 4) is not documented in `component-mod-architecture.md` or anywhere I read in context. This is a real architectural gap (folder vs archive parity).

The dead `modmgrWriteManifest / modmgrReadManifest` legacy serialiser at `modmgr.c:2456-2554` is not flagged in context as deprecated or to-be-removed. It is just orphaned code.

### 5. Connectivity / online

**Where code matches context.** `constraints.md` line 21 documents `NET_PROTOCOL_VER = 46`, the v46 distribution digest, v45 spawn-weapon mode, v44 weapon cull bump, v43 attacker_id, v42 spectator wire, v41 achievement toast, etc. Each version bump has a stated reason. Code reflects all of these in the relevant `port/src/net/` files and tests.

`network-architecture.md` and `designs/nat-traversal-architecture.md` exist in the index. The latter document I read is dated 2026-03-30 and labeled "Status: Design (not yet implemented)". Code shows STUN, UPnP, hole-punch, ICE, TURN, presence, 6-tier orchestrator all present and substantially wired. The doc's status line is severely behind code reality.

`infrastructure.md` line 65 says D8 NAT traversal is DONE (S83). Code shows D8 was the foundation, but ICE peer candidate exchange is not wired (Section 5), kbps measurement is a placeholder, and TURN has no public fallback. The DONE label is partially correct: STUN, UPnP, basic hole-punch are wired. ICE is not fully bridged for peer candidate exchange.

`designs/hosting-modes-listen-vs-dedicated.md` (per index) presumably covers the listen-vs-dedicated split. Code matches the constraint (`g_NetDedicated == 0` for listen, `== 1` for dedicated, `g_NetLocalClient == NULL` on dedicated, ROM/mod check skipped on dedicated).

`constraints.md` line 16 (S486) documents the in-client online connectivity shipping scope. Code is consistent: server target is buildable but listen-host work has the priority. `tasks-current.md` S586 also reaffirms this scope.

**Where context claims more than code shows.** `nat-traversal-architecture.md` says "Status: Design (not yet implemented)" but code is well past that. The doc has a STUN architecture diagram that is broadly correct in shape but understates how much is wired. ICE and TURN are not in the design doc but exist in code. Documentation drift.

`infrastructure.md` line 65 D8 DONE oversimplifies. The STUN tier reports the local reflexive but does not transmit it to the peer (Section 5). This is closer to "STUN client is wired" than "NAT traversal is solved."

**Where code shows more than context describes.** The 6-tier P2P state machine (LAN, DIRECT, STUN, UPNP, ICE, TURN) is more elaborate than what `nat-traversal-architecture.md` describes (which is STUN + hole-punch). The presence service implementation (UDP 27105, Ed25519, v2 pubkey-bound) is mentioned in `constraints.md` line 21 but no design doc was read describing the protocol-level shape.

The voice module (`port/src/voice.c`, HAVE_OPUS gated) is referenced indirectly in `constraints.md` line 21 (PDVOC port 27108) but no design doc was located.

### 6. Test framework

**Where code matches context.** `designs/testing-framework-2026-04-26.md` is the ADR. The CMake target shape, the explicit source list, the Catch2 v2 single-header choice, the `tests/main.cpp` `CATCH_CONFIG_MAIN` pattern, the run command via `run-pd-tests.ps1`, the scope aliases match exactly. The doc was written 3 days ago in code-time and is current.

**Where context claims more than code shows.** The doc enumerates an aspirational test list at lines 125-142 (test_random_pool, etc.). Code shows all of those plus more (33+ test files). Context understates the scope of what landed since the doc was written.

**Where code shows more than context describes.** The pure-C mirror discipline (each `*_pure.c` with a manual `@SYNC` header comment) is documented in the test-file headers, not in the framework ADR. A reader of the ADR would not know this is the drift-management strategy.

`tasks-current.md` recent entries describe the build wrapper improvements (S504, S579, S582, S573, S581) including the queued `build-session.ps1`, the per-step logs, the watchdog. These match `devtools/run-pd-tests.ps1` and `build-session.ps1` code.

### 7. Dev tooling

**Where code matches context.** `infrastructure.md` lines 41-49 list the major build tools (build-headless.ps1, build-env.sh, dev-window-v2.ps1, release.ps1) and reference S247 build-env self-heal, S257 git-sync-before-build, S361 dev-window async RunspacePool. Code reflects all of these.

`tasks-current.md` Build infrastructure side quest (S504 / S579 / S582) describes the queued isolated builds, per-session locking, watchdog with 60s default, log capture. Code shows `build-session.ps1` with these features.

`CLAUDE.md` Git Safety Protocol section documents the no-bare-stash, commit-first, pre-op snapshot rule. `devtools/git-snapshot.sh` and `git-verify-snapshot.sh` exist as helpers.

**Where context claims more than code shows.** No major mismatch surfaced. Dev tooling is the most accurately documented pillar in context.

**Where code shows more than context describes.** The SP-9 truncation guard regex inside `build-headless.ps1:763-804` is not described as a callable invariant in any context file I read. The SP-9 systemic-bug entry probably exists in `systemic-bugs.md` (per `tasks-current.md` references) but I did not read it.

The `Updater.exe` standalone build under `port/src/updater_standalone/` is referenced in `release.ps1` but I did not see a context file describing the dual-updater architecture (in-process self-update + external recovery executable). The constraint that release zips include both is enforced by `release.ps1` only.

### 8. Save / wire format

**Where code matches context.** `constraints.md` line 14 says "Save migration framework (SAVE_VERSION) exists for future format changes." Line 15 documents `MPSETUP_VERSION = v2` (bumped 2026-04-26) with the migration rule. Line 21 documents `NET_PROTOCOL_VER = v46`. All three are pinned in code via `tests/test_versions_pin.c` and verified in tests.

`designs/testing-framework-2026-04-26.md` references the savebuffer test as "Bit-pack drift" coverage (one of the four bug classes the framework targets). Code shows `test_savebuffer.cpp` and `savebuffer_pure.c` matching this.

**Where context claims more than code shows.** No major mismatch. The save format and migration framework are accurately described in context.

**Where code shows more than context describes.** The tri-file version sprawl (SAVE_VERSION in `savefile.h`, MPSETUP_VERSION in `mpsetups.h`, NET_PROTOCOL_VER in `net.h`) is described in `constraints.md` but only collectively. A code reader looking at one file does not see comments pointing to the others. Minor cross-reference gap.

The `saveListAgents` API surface is declared in `savefile.h` but the disk-scan implementation was not read in this session. Context does not describe the directory layout in detail beyond the file naming convention.

---

## Cross-cutting context observations

**Observation A: Two design docs are stale relative to code.**
- `designs/nat-traversal-architecture.md` (2026-03-30, "Status: Design (not yet implemented)") is far behind code reality. Code has 6 tiers, presence service, voice, ICE peer-candidate API hooks. Doc has STUN + hole-punch.
- `infrastructure.md` (2026-04-18, S361) is ~40 sessions and 2 weeks behind today's code. It does not mention testing framework as a phase, S484 catalog manager, S486 in-client shipping pivot, the bulk of the menu graph migration, or many recent constraints from `constraints.md`.

**Observation B: `constraints.md` is the most current context file.** Has entries dated 2026-04-27 and 2026-04-28, with detailed reasoning and code references. If only one context file could be trusted as a current source-of-truth ledger, it would be this one.

**Observation C: Some real architectural gaps are in code only.**
- The 41-vs-86 MPWEAPON-vs-WEAPON catalog-vs-manager slot gap.
- The MENUITEMTYPE_LIST/PLAYERSTATS/RANKING DEFERRED placeholders.
- The 10 raw SDLK_F* handlers in `pdgui_backend.cpp` bypassing actionmap.
- The right-stick scroll spec-vs-runtime decoupling.
- The `.pdmod` cannot deliver INI-based components.
- The SHA-256 mismatch between archive and folder mods.
- The `pd_headers` 600s build watchdog stall blocking test verification.

These are not flagged as "to do" or "known limitation" in any context file I read. They are visible only by reading code.

**Observation D: Some context-claimed-DONE work has real edges.** D8 NAT Traversal DONE understates the ICE peer-exchange gap. D14b Mod Distribution DONE understates the folder-vs-archive SHA-256 mismatch. D5 menu phases DONE understates the menu graph and item-type deferrals.

This is a normal kind of drift in a fast-moving project. The pillars are largely shipped; the edges accumulate.

**Observation E: The session-log shows verification cadence has slowed.** S587 through S590 each report source-checked but not binary-verified slices, blocked on the `pd_headers` 600s watchdog. The framework for verifying changes is in place. The throughput of verification has dropped because the build wrapper itself is hitting an apparent stall at the generated-headers step. Resolving the `pd_headers` stall would unblock several follow-up verifications.

---

## Summary

| Pillar | Code state | Context state | Largest gap |
|--------|------------|---------------|-------------|
| Catalog | Manager router shipped F1-F10; data still in Layer A; F10 loader is a no-op scaffold | `constraints.md` matches; `infrastructure.md` does not mention catalog manager | F11+ data move not started; loader header lies to its body |
| Input | Action map, IMC, layer stack all wired; flush discipline solid; static tests strong | ADR exists; constraint ledger matches | 10 raw F-key bypasses, scroll spec decoupled from runtime, layer IMC pointers deferred |
| Menu / UI / UX | Pool + ctx + nav abstraction solid; menu graph partial overlay; 3 item types deferred | README and constraint ledger match; design docs may be more aspirational than the reality | LIST/PLAYERSTATS/RANKING placeholders, action bar adoption uneven, graph not yet the layer |
| Modding | `.pdmod` reader, manifest, VFS, network distrib, Property Handler all real | `component-mod-architecture.md` matches at high level | `.pdmod` cannot deliver INI components; SHA-256 mismatch between path types; netdistrib skips `g_ModRegistry` |
| Connectivity | 6-tier P2P, presence, voice, connect codes wired; netbuf safe; static tests verify parse-before-commit | `constraints.md` v46 matches; `nat-traversal-architecture.md` is severely behind code | ICE peer-candidate exchange not bridged; STUN reflexive not transmitted; kbps placeholder |
| Test framework | `pd-tests` Catch2 binary, ~33 test files, scope aliases, isolated session builds | `testing-framework-2026-04-26.md` matches well | Pure mirrors hand-synced; F10 loader test is shape-only; `pd_headers` watchdog blocks verification |
| Dev tooling | Build wrapper with self-test, smart-clean, ccache probe, SP-9 guard; release pipeline; per-session builds | `infrastructure.md` and `tasks-current.md` accurate | `pd_headers` 600s stall is the practical bottleneck right now |
| Save / wire | SAVE_VERSION 2 JSON files, migration framework, NET_PROTOCOL_VER v46, MPSETUP_VERSION v2 pinned | `constraints.md` is the canonical source | Pure mirrors hand-synced; chained-migration path under-tested |

**Summary in two sentences.** The infrastructure pillars are substantially built, with strong test discipline at the primitive layer, real safety nets at the input and catalog seams, and a working release pipeline. The remaining work is the long tail of edges (data moves, item types, signaling bridges, hand-synced test mirrors, build watchdog stall) plus closing the documentation drift in `infrastructure.md` and `nat-traversal-architecture.md` so future readers have an accurate map.
