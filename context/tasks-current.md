# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. Completed work lives in `session-log.md`,
> `bugs.md`, `daily-logs/`, or `_archive/tasks-archive.md`.
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Done — 2026-04-18 (S361 — Dev Window v2 async RunspacePool, dev direct `7d6ec8fb`)

Dev-tool only — no game code change. Release pipeline produced v0.0.120.

- Three separate `Add-Type -Language CSharp` calls consolidated into one guarded block (avoids triple cold-compile on startup).
- Persistent `BgPool` RunspacePool (1–3 threads, ReuseThread apartment) added at startup, disposed on window close.
- `Update-StatusBar` (2 s `MainTimer`) reuses `BgPool` instead of creating a fresh runspace every tick — kills the ongoing UI stutter.
- `Populate-DocList` scans `context/` + `docs/` (~170 files) on `BgPool`; `Loaded` event no longer blocks first paint.
- New `Start-AsyncPoolAction` helper wraps `Invoke-GitPull` / `Invoke-GitPush` / `Invoke-PruneWorktrees` and the Check button.
- `Toggle-Server` / `Toggle-Game` pre-update button text + log line; `UseShellExecute=$false` skips Shell32 lookup; errors surface in a MessageBox.

---

## Done — 2026-04-17 (S360 — Updater Mozilla CA bundle fix, `claude/blissful-curie-2387ee` → merge `3da8191c`)

**Build verified.** Release pipeline produced v0.0.119.

Testers logged `SSL peer certificate or SSH remote key was not OK` on every update check. Root cause: MSYS2's statically-linked `libcurl.a` is built against OpenSSL **without** winstore integration — `CURLSSLOPT_NATIVE_CA` compiles but is a runtime no-op, and no external CA file ships with the exe.

- New `port/src/cacert.pem` (223,837 bytes, copied from mingw64 `ca-bundle.crt`).
- `CMakeLists.txt` generates `${BINARY_DIR}/port/include/cacert_blob.h` via `file(READ ... HEX)` + `REGEX REPLACE` at configure time.
- `updater.c`: new `curlSetupTLS()` helper replaces the two inline SSL blocks in `curlGet()` and the file-download path. Passes `CURLOPT_CAINFO_BLOB` with the embedded bundle. `CURLSSLOPT_NATIVE_CA` kept as harmless secondary.

Zero-DLL compliant. Independent of whatever cert store exists on the tester's machine.

**Verify**: Launch the updater on a fresh Windows install with no user CA store — update check succeeds over HTTPS.

---

## Done — 2026-04-17 (S359 — B-163 secondary crash-site guards, `claude/determined-austin-d54582`, commit `5122a663`)

**Build verified.** Clean 774/774. Release pipeline produced v0.0.118.

S317+S318 addressed the door-creation AV at PC+0x161258. Tester logs on build `acdf4062` confirmed a **second** AV at PC+0x15f83a along the prop/chr-creation path during stage 0x26 setup. `setupCreateObject` had four unguarded `obj->model->scale` dereferences after `setupLoadModeldef` could return NULL via the FIX-B.2 reject path.

FIX-B.2 pattern applied to every remaining unguarded site:
- `src/game/setup.c::setupCreateObject` — early return with WARNING if `g_ModelStates[modelnum].modeldef == NULL` after `setupLoadModeldef`.
- `src/lib/model.c::modelAllocateRwData` — defensive NULL/rootnode guard; all callers inherit crash-proofing.
- `port/src/net/netmsg.c:2213` — guard `laptopDeploy` NULL return before dereferencing `obj`.
- `src/game/body.c::bodyAllocateModel` — guard `headmodeldef` NULL in both random-head and specific-headnum paths.

B-163 bugs.md entry already captured this under "S327 addendum (magical-zhukovsky)" label — matches `5122a663` fix description exactly.

---

## Done — 2026-04-17 (S358 — B-161 title/intro model NULL-guards, dev direct, commit `be0935da`)

**Build verified.** Release pipeline produced v0.0.117.

With `modeldefLoad` now rejecting torn modeldefs at load time (S323 root-cause fix), the title/intro screens needed matching NULL-handling so a rejected logo model could no longer AV on subsequent frames.

- `titleInitNintendoLogo` / `titleInitRareLogo` / `titleInitPdLogo` handle `modeldefLoad()` returning NULL — cascade to next intro mode or SKIP.
- Exit/render paths guard against NULL `g_TitleModel*`.

Completes the B-161 defensive sweep — every path from `modeldefLoad` root chokepoint through `setupCreateDoor` / `setupCreateObject` / `modelAllocateRwData` / `body.c::bodyAllocateModel` / `netmsg.c::laptopDeploy` / `title.c::titleInit*Logo` now converts AV into a WARNING log line.

**Verify**: Cold-boot on a system with a deliberately torn intro logo modeldef — no AV; log shows `TITLE: intro model load failed — skipping to next mode`.

---

## Done — 2026-04-17 (S356 — Forge Door Lifecycle + D5 Phase 5C+5D, `dazzling-vaughan-204b7c`)

**Build verified.** Clean 776/776 (full rebuild on dev). Zero errors.

- **Forge doors**: `s_spawn_door()` in forge_runtime.c builds live `doorobj` from catalog modeldef — pool of 16 from MEMPOOL_STAGE, slide-vector computation (yaw-aware), DOORFLAG_0080/AUTOMATIC, propActivate/Enable. `forgeRuntimeFindDoorByUid()` in forge_runtime.h.
- **OPEN_DOOR/CLOSE_DOOR**: forge_logic.c now calls `doorsRequestMode(door, DOORMODE_OPENING/CLOSING)` via `forgeRuntimeFindDoorByUid`; data-only fallback if no live doorobj.
- **Phase 5C**: Hover on human lobby row → tooltip with live charpreview FBO; falls back to baked thumbnail or player name. FBO suppressed in baking pipeline while hover active.
- **Phase 5D**: Drop shadow on portrait thumbnail; team-color tinting on portrait border when teams on.

---

## Done — 2026-04-17 (S355 — Wave 5 Cross-Audit: S352 + S353 + S354, `goofy-pike-572f8a`)

**Build verified.** Clean 774/774 (worktree) + 4/4 incremental on dev post-merge. Zero errors.
`PerfectDark.exe` 52,810,516 / `PerfectDarkServer.exe` 22,925,709.

S352 and S354: CLEAN — no bugs found across all 10 audit items.
S353 bug fixed: `NET_PROP_DIRTY_MAXSYNCID` raised from 512 → `NET_PROP_MAP_SIZE` (2048) in `port/src/net/netmsg.c`. Props at slots 512–2047 were silently skipping dirty marks, preventing the 120-tick heartbeat CRC from firing on large stages after events on those props. Static array grows 512→2048 bytes; no wire format change; no protocol bump.

---

## Done — 2026-04-17 (S354 — Forge Runtime Wire-In: Props, Weapons, Geometry, Doors, Zones, `practical-varahamihira-3b5f5d`)

**Build verified.** Single file change: `port/src/forge/forge_runtime.c` (+358/-27).

Wired the remaining Forge (The Grid) object types into the live engine:
- **WEAPON_PAD**: `s_spawn_weapon_pad()` — catalog resolve → `weaponCreate` → `func0f08ae0c` → `modelSetScale(1.0f)` → `setup0f0923d4`.
- **PROP / GEOMETRY / INTERACTABLE**: `s_spawn_prop()` — catalog resolve → `objInit` from MEMPOOL_STAGE pool → `modelSetScale(1.0f)` → `setup0f0923d4`. Collision auto-generated from model bbox.
- **ZONE**: `s_register_zone()` stores in `s_zone_rt[]`; per-tick edge-triggered enter/exit in `forgeRuntimeTick` fires `forgeChannelSet` + `forgeLogicFireEvent`. Teleporter type directly sets player position.
- **DOOR**: Catalog entries spawn as static props (visual only). Full `doorobj` pool lifecycle deferred to separate session.

New state: `s_prop_pool` (MEMPOOL_STAGE, 64 slots), `s_prop_count`, `s_zone_rt[128]`, `s_zone_count`.
New includes: `game/propobj.h`, `game/modeldef.h`, `game/setuputils.h`, `lib/model.h`, `lib/memp.h`.

---

## Done — 2026-04-17 (S353 — Prop Sync Event-Driven + Killfeed Verification, `pedantic-saha-8d7ff0`)

**Build verified.** Clean 585/585 (dev). `PerfectDark.exe` 52,772,152 / `PerfectDarkServer.exe` 22,924,028.

- **Prop sync dirty flags**: `s_PropDirtyFlags[512]` + `s_PropDirtyCount` in `netmsg.c`. Each SvcProp*Write marks dirty; 120-tick heartbeat skips entirely if nothing dirty (O(1) vs O(N_props)).
- **Killfeed bot kills**: verified working — no code change needed. All `ampchr && vmpchr` combinations fire `pdguiKillfeedPush`; roadmap entries marked DONE.

---

## Done — 2026-04-17 (S352 — D5 Phase 5: Lobby Player Portraits, `confident-brahmagupta-a3f5a3`)

**Build verified.** Clean 774/774 (worktree) + 585/585 (dev post-merge), zero errors.
`PerfectDark.exe` 52,770,947 / `PerfectDarkServer.exe` 22,922,823.

D5 Phase 5 portrait system wired into `pdgui_menu_room.cpp` (+276 LOC / -43 LOC):

- **Per-slot portrait baking pipeline**: `LobbyPortrait` struct + `s_LobbyPortraits[8]` (baked GL textures via shared charpreview FBO, one per frame, bot-modal guarded).
- **Human row overhaul**: row height 50px; portrait thumbnail (44px) left-aligned; baked texture with Y-flip UVs or initials circle fallback; state badge dot (yellow/green/blue/grey); name + role badge on line 1, body name + state text on line 2.
- **Join fade-in**: `s_LobbyPortraitAlpha[]` ramps 0→1 over ~25 frames per slot.
- **Lifecycle**: reset on every room open (`IsWindowAppearing`) and `pdguiRoomScreenReset`; portrait invalidated when player's body/head IDs change.
- **Solo mode**: baking skipped; initials placeholder; instant alpha.

---

## Done — 2026-04-17 (S353b — Killfeed Network Broadcast + Prop Snapshot Supplement, `quizzical-murdock-95eb09`)

**Build verified.** Clean 774/774 objects, zero errors.

Follow-up to S353. **S353b** shipped ahead of S353 primary's dirty-flag rewrite (superseded by S353/S355).

- **Prop sync**: Replaced CRC polling (`netPropSyncChecksum` + `SVC_PROP_SYNC` write) with per-prop `{hidden, damage}` snapshot dirty detection. Server now only triggers `NET_RESYNC_FLAG_PROPS` when a prop actually diverged. `SVC_PROP_SYNC` read handler still consumes bytes for old-server compat.
- **Killfeed bot kills**: Fixed gap where `mpstatsRecordDeath` never broadcast kill events to network clients. `netDistribSendKillFeed` now called from both suicide and normal kill paths (server only). `SVC_LOBBY_KILL_FEED` extended to reach `CLSTATE_GAME` clients. Client-side read handler now calls `pdguiKillfeedPush` with team lookup from `g_MpAllChrConfigPtrs[]`.

---

## Done — 2026-04-17 (S351 — D5 Phase 4: UI Texture Mod Overrides, `dazzling-heisenberg-f84acc`)

**Build verified.** Clean 585/585 objects, zero errors.
`PerfectDark.exe` 52,759,787 / `PerfectDarkServer.exe` 22,922,823.

New API in `port/include/pdgui_theme.h`:
- `pdguiThemeScanModUiTextures(mod_dir)` — parse mod.json `"type": "ui"` components; register `catalog_id`/`path` overrides via `s_registerModTexture`.
- `pdguiThemeApplyEnabledModUiTextures()` — apply overrides from all enabled mods.

Wired into `pdguiThemeLateInit()` (after base textures load) and `modmgrApplyChanges()`
(after chrome rescan). Two-pass cjson parser handles key-order independence.

---

## Done — 2026-04-17 (S350 — Wave 3 Cross-Audit: S348 Discord, `vigorous-benz-f8cb68`)

**Build verified.** Clean 776/776 objects, zero errors (includes S351 sources). `PerfectDark.exe` 52,759,787 / `PerfectDarkServer.exe` 22,922,823. Note: S351's `pdgui_theme.cpp` had a latent GCC stray-'#' error (single-line `extern "C" { #include }`) — fixed inline during build verify.

1 bug fixed in `port/src/discord.c`:
- **JSON injection in SET_ACTIVITY payload**: `details`/`state` strings inserted raw via `%s` into JSON. A mod stage slug containing `"` or `\` would corrupt the pipe frame. Fix: added `disc_json_str()` escape helper — escapes `\` and `"` before both `_snprintf` branches in `disc_send_activity`.

All other audit items confirmed clean: IPC protocol, PIPE_NOWAIT handling, fail-silent reconnect, thread safety, memory management, MinGW compatibility, dedicated server exclusion.

---

## Done — 2026-04-17 (S349 — Cross-Audit S346+S347, `cool-poitras-b287e7`)

**Build verified.** Clean 774/774 objects, zero errors.
`PerfectDark.exe` 52,861,458 / `PerfectDarkServer.exe` 22,907,957.

**S346 (AP Phase 4) audit — CLEAN:** `assetprovider_internal.h` included by exactly 5 allowed
callers; no game code calls `romProviderHandle()`; stage handle fields populated with `fileid > 0`
guard; `assetHandleIsNull()` used correctly in `assetLoadToNew`; `catalogGetBodyHandle` /
`catalogGetHeadHandle` / `catalogGetPropHandle` all null-guard with `memset + CATALOG-FATAL + g_CatalogFailure`
pattern; deprecated SA-5a bridge functions marked `[DEPRECATED]` + `[MIGRATION BRIDGE]`.

**S347 (blue tint sweep) audit — 2 missed literals fixed:**
- `pdgui_menu_moddinghub.cpp:2109` chrome-tool preview border `IM_COL32(90,120,170,220)` → `pdguiImU32TitleGlow(220)`
- `pdgui_menu_agentcreate.cpp:324` body-name label `IM_COL32(140,160,200,180)` → `pdguiImU32TintInfo(180)`

Special Agent badge `IM_COL32(80,160,255,255)` in `pdgui_menu_solomission.cpp` intentionally left
(semantic difficulty color, not a PD accent).  Forge HUD blue/cyan literals are editor-mode colors,
not candidates for theme theming.

---

## Done — 2026-04-17 (S348 — D7 Discord Rich Presence, `ecstatic-cartwright-11459d`)

**Build verified.** Clean 774/774 objects, zero errors.
`PerfectDark.exe` 52,883,797 / `PerfectDarkServer.exe` 22,906,762.

D7 implemented as a thin Windows IPC client — no external library, zero new DLL
dependencies.  New files: `port/src/discord.c` + `port/include/discord.h`.  Wired
into `port/src/main.c` (init/shutdown) and `port/src/pdmain.c` (tick).

Presence states: Main Menu / Solo Mission (stage + difficulty) / Combat Simulator
(stage + scenario + counts) / Co-op / Counter-Op / The Grid editor / Lobby /
Dedicated Server.

**Setup required before presence appears in Discord:**
1. Register app at https://discord.com/developers/applications
2. Copy Application ID → replace `"0"` in `port/include/discord.h` → `DISCORD_APP_ID`
3. Upload art assets in Rich Presence → Art Assets tab:
   `pd2_logo`, `icon_solo`, `icon_combat`, `icon_coop`, `icon_counterop`, `icon_forge`

**Playtest items:**
- Launch PD2 with Discord open — verify presence shows "In Main Menu".
- Start a solo mission — verify presence shows stage name + difficulty.
- Start a Combat Simulator match — verify presence shows stage + scenario + counts.
- Close Discord mid-session — verify game does not crash or log spam.
- Reopen Discord — verify presence reconnects within 30 seconds.

---

## Done — 2026-04-17 (S347 — Blue Tint Sweep + Gamepad Audit, `gracious-poitras-6eeeb6`)

**Build verified.** Clean 773/773 (worktree) + 775/775 (dev post-merge). Merge commit to dev.

**Task 1 — Blue tint sweep (6 files, 13 sites):** `pdgui_menu_modmgr.cpp`, `pdgui_menu_moddinghub.cpp`, `pdgui_menu_agentselect.cpp`, `pdgui_menu_agentcreate.cpp`, `pdgui_countdown.cpp`, `pdgui_menu_mainmenu.cpp` — all hardcoded `IM_COL32` PD-blue accent literals replaced with `pdguiImU32TintInfo` / `pdguiImU32TitleGlow` / `pdguiPalImU32(PDPAL_TITLEBG,…)` accessors.

**Task 2 — ImGuiKey_Gamepad audit:** No dead checks found. M0.2 (S181–183) already removed the ~130 redundant gamepad key checks. The 3 remaining `AddKeyEvent(ImGuiKey_Gamepad*,false)` calls in `pdgui_menu_mainmenu.cpp` are B-131 input-flush fixes and must stay.

---

## Done — 2026-04-17 (S346 — Asset Provider Phase 4, `crazy-wilbur-ae5c6d`)

**Build verified.** Clean 773/773, zero errors. `PerfectDark.exe` + `PerfectDarkServer.exe` linked.

`romProviderHandle()` retired from all game code — now internal to catalog/provider layer only.

Changes:
- `port/include/assetprovider_internal.h` — new internal header for `romProviderHandle`/`romProviderFilenum`
- `port/include/assetprovider.h` — removed `romProviderHandle`/`romProviderFilenum` from public API
- `port/include/assetload.h` + `port/src/assetload.c` — new `assetLoadRomToNew()` for game code
- `port/include/assetcatalog.h` — 5 stage handle fields in `catalog_stage_result_t`; `catalogGetBodyHandle`/`catalogGetHeadHandle`/`catalogGetPropHandle` declared; SA-5a filenum fns demoted to `[MIGRATION BRIDGE]`
- `port/src/assetcatalog_api.c` — stage handle population; 3 new handle accessor impls
- `port/src/assetcatalog_base.c` + `assetcatalog_base_extended.c` — use `assetprovider_internal.h`
- `src/game/file.c` — `fileLoadToNew` calls `assetLoadRomToNew`
- `src/game/modeldef.c` — calls `assetLoadRomToNew`
- `src/game/lang.c` + `langreset.c` — reverted to `fileLoadToNew` (lang file IDs are runtime-computed)
- `src/game/setup.c` — uses `stage.setup_handle`/`mpsetup_handle`/`pads_handle`
- `src/game/tilesreset.c` — uses `stage.tile_handle`

**Pending (Phase 5 scope)**: Migrate SA-5a deprecated bridge calls (`catalogGetBodyFilenumByIndex` etc.) to handle-based model load APIs — requires `modeldefLoadByHandle` overloads.

---

## Done — 2026-04-17 (S344 — Audit S339+S340, `optimistic-mcclintock-47fc8d`)

**Build verified.** Clean 4/4 objects, zero errors. Merge commit to dev.

4 bugs fixed:
- `server_gui.cpp`: Stage ID InputText width reserves room for Apply button when dirty
- `server_gui.cpp`: Force Start button also requires `roomGetActiveCount() > 0`
- `server_gui.cpp`: Ban button tooltip clarifies no IP block (same as kick currently)
- `pdgui_menu_theme_editor.cpp`: `renderLivePreview` cursor height uses full drawn dialog height (`headerH + 6*scale`)

---

## Done — 2026-04-17 (S340 — Content-Inset Sweep, `elated-lichterman-9c8f69`)

**Build verified.** Clean 585/585, zero errors. Merge commit `56338aaa` on dev.

7 `pdgui_menu_*.cpp` files patched with `pdguiSetCursorBelowTitle`:
- audiomod, logviewer, moddinghub, modmgr, theme_editor, update: added call
- mpingame: decl only (pill windows manage their own padding)
- forge: no-op shim, no render sites — skipped

---

## Done — 2026-04-17 (S339 — R-5 Server GUI Redesign, `thirsty-jemison-84f6ce`)

**Build verified.** Clean 252/252 server, 521/521 game. Zero errors.
`PerfectDark.exe` 52,773,095 / `PerfectDarkServer.exe` 22,905,738.

Redesigned `port/fast3d/server_gui.cpp` and extended `port/src/server_bridge.c`.

New layout:
- **Status bar**: uptime HH:MM:SS, tick Hz, memory MB, player count, room count, connect code.
- **Players tab**: 6-column table (Name/State/Ping/Team/Kick/Ban). New `netServerBanClient` bridge.
- **Rooms tab**: hub state summary + 6-column room table (ID/Name/Players/State/Stage/Scenario).
- **Operator tab**: match control (game mode, stage ID input, scenario, force start/end) + server control (shutdown, restart-on-update).
- **Updates tab**: unchanged.
- **Log panel** (bottom): filter row [All][NET][ERROR][WARN][CHAT][HUB] + auto-scroll toggle.

Bridge additions: `netServerBanClient`, `serverGetMemoryMB`, `serverGetStageId/Set`, `serverGetScenario/Set`.
`CMakeLists.txt`: `target_link_libraries(pd-server psapi)` for Windows memory query.

**Playtest items:**
- Launch PerfectDarkServer.exe — verify status bar shows uptime ticking, tick Hz ~60, memory MB.
- Connect a client — verify Players tab shows name/state/ping/team.
- Click a filter button in log panel — verify only matching lines shown.
- Operator tab: change stage ID + Force Start — verify log shows new stage_id.

---

## Done — 2026-04-17 (S338 — D-MEM M5+M6: separate pool regions + mutex, `ecstatic-bouman-5d30e4`)

**Build verified.** Clean 775/775, zero errors. `PerfectDark.exe` 52,677,661 / `PerfectDarkServer.exe` 22,919,068.

D-MEM now **fully complete** (M0–M6 + MEM-1/2/3). Changes:
- `src/lib/memp.c` — M5: PERMANENT/STAGE/POOL_8 each get dedicated address regions; M6: MEMP_LOCK/UNLOCK guards on all mutation functions
- `src/include/lib/memp.h` — `mempSetLockFns()` added
- `port/src/pdmain.c` — SDL_mutex registered with mempSetLockFns after mempSetHeap

Bug fix: `mempGetStageFree()` was reading expansion pool (never set up, always returned 0). Now reads onboard STAGE pool — fixes spurious modelcatalog ERROR on boot.

**Playtest:** stage transitions + multiplayer — no behavioral change expected, verify no crashes.

---

## Done — 2026-04-17 (S337 — Dev Window v2 improvements)

Three fixes to `devtools/dev-window-v2/dev-window-v2.ps1` (commit `4d117d67`, worktree `peaceful-williams-59ec2f`):

1. **Prune Worktrees button** — runs `git worktree prune -v`, shows result dialog, logs to Log tab. Button disabled during builds/releases. Worktree count shown in status bar (orange when >20).
2. **Progress bar ActualWidth fix** — `UpdateLayout()` called before `ActualWidth` reads in Start-Build and Start-PushRelease so the 12% git-sync fill actually renders.
3. **HUD "0%" consistency** — spinner path now prefixes `"0% - "` on LblProgressText, matching non-spinner path. Removed dead green background assignment on step transitions.

---

## Done — 2026-04-17 (S336 — Audit S329 B-12 + S332 Modeldef/Audio)

Two bugs fixed, merged to dev (`e1081911`):

1. **`pdguiPauseGetChrSlots` u32 truncation** — pause menu dropped bots 24-31 silently. Fixed: return `u64`, use `1ull<<i`.
2. **weapon modeldef NULL crash** — `player.c::playerChrInitialise` called `modelAllocateRwData(NULL)` on torn weapon mod asset. Fixed: NULL guard + WARNING.

Stale `chrslots` comment in `netmanifest.c` also cleaned up.

**Playtest items:** (1) Pause menu with 32 bots — verify count shows all 32. (2) Torn weapon mod — verify WARNING not crash.

---

## Done — 2026-04-17 (S335 — Wave 1 audit: S328 + S331, `quirky-mcnulty-60c44a` worktree)

**Build verified.** Clean 773/773, zero errors. `PerfectDark.exe` 52,768,999 bytes, `PerfectDarkServer.exe` 22,887,046 bytes.

Three fixes:
1. `port/src/net/netmanifest.c` — `s_manifestAddWeapon` now emits `LOG_WARNING` when a weapon has a catalog ID (`wcan != NULL`) but the entry is missing, matching the body/head helper behavior.
2. `port/src/main.c` — Added `statsShutdown()` to `cleanup()`; stats are now flushed to disk on normal game exit regardless of whether a match finished.
3. `src/game/lv.c` — Distance stat keys renamed from `"distance_units"` + `"mp.distance_units_sample"` / `"solo.distance_units_sample"` to `"mp.distance_units"` / `"solo.distance_units"` — consistent with all other namespaced stats.

**Playtest items:** solo walk then X-quit → `playerstats.json` has `solo.distance_units`; no `distance_units` or `_sample` keys.

---

## Done — 2026-04-17 (S334 — Audit S327 Asset Provider + S330 Memory)

Fixed one bug: `pak.c:pak0f11d9c4` malloc null-check (`5773c465`). S327 and S330 otherwise clean.

---

## Done — 2026-04-17 (S333 — Merge S329 + S332 into dev)

**Build verified.** Clean 585/585, zero errors. `PerfectDark.exe` 52,660,473 bytes, `PerfectDarkServer.exe` 22,901,400 bytes.

Two worktree branches merged into `dev`:
- `exciting-meitner-bc8c70` (S329, B-12 chrslots removal + protocol v37) — merge commit `37bdb4b6`
- `magical-mahavira-f3726f` (S332, B-161 modeldef chokepoint + B-141 audio pacing) — merge commit `12170710`

Context conflicts in `session-log.md` and `tasks-current.md` resolved by keeping both sets of content chronologically.

---

## Done — 2026-04-17 (B-12 Phase 3 — remove chrslots, protocol v37, `exciting-meitner-bc8c70` worktree)

**Build verified.** Clean link [474/474], zero errors. Wire format is a breaking protocol change.

Removed the legacy `u64 chrslots` bitmask end-to-end and made the participant
pool (`g_MpParticipants`) the sole source of match slot state. Constants
`BOT_SLOT_OFFSET`, `CHRSLOTS_PLAYER_MASK`, and `CHRSLOTS_BOT_MASK` are gone;
`MpParticipant.legacy_slot` is gone; the `mpParticipantsTo/FromLegacyChrslots`
shims are replaced with wire-only helpers `mpParticipantsEncodeActiveMask` /
`mpParticipantsDecodeActiveMask`. `NET_PROTOCOL_VER` bumped 36 → 37.

44 mpconfigs.c initializers updated (chrslots placeholder dropped). 60+
runtime callsites migrated: challenge.c, mplayer.c, mpscenarios, menutick.c,
menuitem.c, menu.c, mainmenu.c, ingame.c, setup.c, lv.c, pdmain.c, net.c,
netmsg.c, matchsetup.c, server_stubs.c, pdgui_bridge.c. The dedicated server
now links `participant.c` directly (slot state is no longer stubbed).

### Playtest verification

- **Combat Sim 4 humans + 32 bots** — host from the room screen, connect 3 remote
  clients, hit Start. All 4 humans + 32 bots spawn. Log shows `MATCHSETUP:
  activeMask=0x...` and `NET: Combat Sim setup: ... activeMask=0x...`.
- **Client-side bot spawn** — on any connected client, verify bots appear
  (`SIMULANT: spawning started activeBots=N maxsim=32`). Before the fix,
  `mpParticipantsFromLegacyChrslots` ran after the `chrslots` read; now the
  pool is decoded from the wire active mask in-place.
- **Challenges / quick-team sim** — go to Challenges, select a challenge that
  gifts bots. Bot difficulties and slot count match. `challengePerformSanityChecks`
  clears + rebuilds bot participants using the new API.
- **Save/load MP setup** — save a custom MP config with 16+ bots and reload it.
  Bot participants are re-added via `mpAddParticipantAt` purely from bot
  difficulty (chrslots storage is gone).
- **Pre-v37 client rejected** — connecting a pre-patch client to a v37 server
  should fail handshake with "protocol mismatch (got 36, expected 37)".

---


---

## Done — 2026-04-17 (S323 Batch H — FIX-B.1 deep manifest scanner discovery logging, `zealous-saha-02c1f5` worktree)

**Build verified.** Clean 768/768 link, both executables rebuilt. FIX-B.1 — the last open item of the Master Orchestration Plan — is now closed.

**What changed** (`port/src/net/netmanifest.c` only):
- `s_manifestAddBody/Head/Model` and new `s_manifestAddWeapon` helpers gained a `scan_source` parameter. Each helper pre-checks dedup via new `s_manifestHasEntry()`; when an entry is newly added, logs `MANIFEST-SP: <scan>-scan discovered <kind> '<id>' (<kind>num=N)`. Scan sources: `props` / `intro` / `ailist`.
- Body/head WARNINGs when a scan references a bodynum/headnum that doesn't resolve in the catalog (non-sentinel values only). Makes mod-character gaps visible instead of surfacing only as `manifestEnsureLoaded` late-adds with torn modeldefs (the S312 fingerprint).
- `manifestBuildMission` props loop migrated to the unified helpers; prop-object switch now calls `s_manifestAddModel(..., "props")` instead of inline `manifestAddEntry`. Removed unused `char id[64]` local.
- Per-phase counter snapshots + single summary line at end of `manifestBuildMission`:
  `MANIFEST-SP: scan stage=0x%02x joanna=N props+=M intro+=P ailist+=Q (total=T)`

Scanners already mirrored `stageLoadAllAilistModels` exactly (S298), so no functional coverage extension was needed — the gap was observability, not coverage.

Commit: `e029eec3` on `claude/zealous-saha-02c1f5`.

### Playtest verification

- **Solo Crash Site / Deep Sea / Villa (cinematic missions)** — tail `pd-client.log` during mission load. Expect a burst of `MANIFEST-SP: ailist-scan discovered body '...' (bodynum=...)` / `head '...' (headnum=...)` lines, followed by the summary `MANIFEST-SP: scan stage=0x<hex> joanna=2 props+=N intro+=P ailist+=Q (total=T)` with `ailist+=` non-zero.
- **Mod-character mission** — if a mod body/head is referenced by ailists but not in the catalog, expect new WARNING `MANIFEST-SP: ailist-scan body bodynum=N not in catalog`. Any such warning identifies a catalog-registration gap.
- **Late-add correlation** — any lingering `MANIFEST-SP: late-add '...' (missed by pre-scan)` lines should now also have a corresponding missing-discovery audit trail (either absent `<scan>-scan discovered` line for that asset, or a `bodynum not in catalog` WARNING).

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

## Open — 2026-04-17 (S323 — B-161 + B-141 root-cause fixes, `magical-mahavira-f3726f` worktree)

### Playtest verification

**B-161 (modeldef torn-load reject at the single chokepoint):**
- **Mission 1 → Mission 2 seamless advance** — Defection → Next Mission → Investigation. Walk 1-2 minutes. No AV expected. If a modeldef was torn at load time, log now shows `MODELDEF: file %u loaded torn — parts=%d root=%p scale=%.3f -- rejecting` at ERROR (caller will handle the NULL as missing-asset via existing FIX-B.2 / S308 guards).
- **CI Training boot** — load CI Training normally. No AV; no new MODELDEF reject lines on healthy assets. Scale clamp WARNING (`... loaded with degenerate scale ... clamping to 1.0`) is acceptable for AllInOneMods models if they ship with bad scale.
- **MP arena transitions** — any stage with props: no modeldef-reject lines in log (all MP stage assets should load clean). If any fire, report the file id.

**B-141 (audio three-tier pacing):**
- **60-second arena play** — tail pd-client.log for `AUDIO[B-141]: 30s summary`. Expect `underruns=0` in a non-hitching run. `buffered(samples) min` should hover 2800-3000 (up from 900-1100). `drops` should stay 0 (queue won't hit the 8192-sample drop threshold).
- **Startup** — from a cold boot through title → mission load, audio should be audible essentially immediately. Under the old path, the queue took 36 seconds to fill from 0 to 1100; now it reaches 2500 in ~115ms (7 frames of fast-fill 736).
- **Stall recovery** — briefly hitch the main thread (alt-tab, heavy pause-menu render). Queue drains; after hitch ends, fast-fill kicks in automatically and restores cushion in ~1s.

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
- ~~**Gameplay-preference pd.ini → per-agent**~~ — **DONE S341**: `[Game]` block added to `prefs_agent.c` with all 6 keys + pd.ini baseline capture + reset-on-agent-select.
- ~~**Audio.ModPlaylist / ModShuffle / ModTrackId → per-agent**~~ — **DONE S341**: `[Audio]` extended with `ModPlaylist` (semicolon-delimited) + `ModShuffle`.

---

## Open — 2026-04-17 (S341 — `tender-borg-b2fc3b` worktree)

### Playtest checklist

1. **[Game] round-trip**: Sign in as agent, change HUD centering / SkipIntro / screen shake in Settings → exit → relaunch → same agent; values persist via sidecar.
2. **Reset on sign-out**: Open Agent Select screen; game prefs revert to pd.ini defaults (not previous agent's values).
3. **Audio playlist per-agent**: Two agents with different CS playlists; switching applies each agent's playlist.
4. **Stage loading**: SP mission + MP match — no regressions after assetLoadToNew migration in lang/setup/tiles/modeldef.

### Asset Provider Phase 3 follow-up

- **Phase 4**: retire raw filenum from public catalog API now that all call sites use provider handles
- **FileProvider inflate/preprocess**: generic `assetLoadToNew` path skips rzipInflate + `romdataFilePreprocess` — OK for Phase 3 (mod assets still go through legacy pipeline), needs resolution in Phase 4
- **lang/tiles catalog handles**: `langGetFileId()` + `stage.tilefileid` are still ROM integers. Phase 4 should introduce `langGetHandle()` / `catalogGetTileHandle()` so FileProvider lang/tile packs work.

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
- **3D gizmo handles** -- the Properties tab edits t