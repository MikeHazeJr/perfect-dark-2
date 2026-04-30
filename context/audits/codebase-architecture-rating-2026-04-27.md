# Codebase Architecture Rating (Client Only)

**Date:** 2026-04-27
**Scope:** `pd` target only (`PerfectDark.exe`). `pd-server` and `pd-tests` exist but were rated only insofar as their existence reveals client architecture (e.g. shared net source proves a clean transport seam).
**Method:** Code-only read. Source files (`.c`/`.cpp`/`.h`), `CMakeLists.txt`, build scripts, inline source comments. No `.md`, no `context/`, no design docs, no audits, no tracker, no memory.

## Headline rating: **675 / 1000**

A real architecture is visible in the port layer (`port/`), sitting on top of an N64-decomp baseline (`src/`) that has not yet been modernized. The new code is unusually disciplined for a fork of this lineage: typed event scenes, handle-based input layer stack, function-pointer-table rendering and asset providers, FNV-1a hash catalog with generation counters, version-pinned wire protocol, Catch2 test runner. The legacy code underneath still carries ROM-address-derived globals, hand-rolled mempool sub-allocation, magic-string stage allocation tables, and 50-header includes per file. The port and the decomp coexist but they have not converged.

## Methodology recap

Files actually read (representative):
- Build: [CMakeLists.txt](CMakeLists.txt:1)
- Entry / boot: [port/src/main.c](port/src/main.c:1), [port/src/pdmain.c](port/src/pdmain.c:1), [port/src/system.c](port/src/system.c:1)
- Input + scene: [port/src/scene.c](port/src/scene.c:1), [port/src/inputlayer.c](port/src/inputlayer.c:1), [port/src/inputctx.c](port/src/inputctx.c:1), [port/include/scene.h](port/include/scene.h:1), [port/include/inputlayer.h](port/include/inputlayer.h:1), [port/include/actionmap.h](port/include/actionmap.h:1)
- Rendering: [port/fast3d/gfx_api.h](port/fast3d/gfx_api.h:1), [port/fast3d/gfx_rendering_api.h](port/fast3d/gfx_rendering_api.h:1), [port/fast3d/gfx_window_manager_api.h](port/fast3d/gfx_window_manager_api.h:1), [port/fast3d/gfx_pc.cpp](port/fast3d/gfx_pc.cpp:1), [port/fast3d/pdgui_backend.cpp](port/fast3d/pdgui_backend.cpp:1)
- Net: [port/src/net/net.c](port/src/net/net.c:1), [port/include/net/net.h](port/include/net/net.h:1)
- Catalog + mods: [port/src/assetcatalog.c](port/src/assetcatalog.c:1), [port/include/assetprovider.h](port/include/assetprovider.h:1), [port/src/catalog_mgr_weapons.c](port/src/catalog_mgr_weapons.c:1), [port/src/modmgr.c](port/src/modmgr.c:1), [port/src/menupool.c](port/src/menupool.c:1)
- Config: [port/src/config.c](port/src/config.c:1)
- Legacy game: [src/game/chr.c](src/game/chr.c:1), [src/game/inv.c](src/game/inv.c:1), [src/game/player.c](src/game/player.c:1), [port/fast3d/pdgui_menu_mainmenu.cpp](port/fast3d/pdgui_menu_mainmenu.cpp:1)

Volumes for context (lines, excluding vendored externals):
- `src/game` C: ~234,605
- `src/lib` C: ~27,971
- `port` C: ~86,931
- `port` C++: ~123,758
- `port` headers: ~39,932

The port layer is comparable in size to the inherited decomp. This is a heavy port, not a thin shim.

---

## Dimension 1: Architecture (66 / 100)

**Strong:**
- Function-pointer-table abstractions are crisp where they exist. [port/fast3d/gfx_rendering_api.h:17](port/fast3d/gfx_rendering_api.h:17) defines `GfxRenderingAPI` as a 30+ slot vtable; [port/fast3d/gfx_window_manager_api.h:20](port/fast3d/gfx_window_manager_api.h:20) defines `GfxWindowManagerAPI` symmetrically; [port/fast3d/gfx_pc.cpp:241](port/fast3d/gfx_pc.cpp:241) consumes both via `static struct GfxWindowManagerAPI* gfx_wapi; static struct GfxRenderingAPI* gfx_rapi;`. A second backend (Vulkan, D3D12) drops in by replacing one file.
- Asset provider polymorphism: [port/include/assetprovider.h:55](port/include/assetprovider.h:55) defines `asset_provider_t` with `resolve_size/load/unload/describe` slots and an opaque `u64 opaque[2]`; [port/include/assetprovider.h:96](port/include/assetprovider.h:96) and [port/include/assetprovider.h:105](port/include/assetprovider.h:105) declare `romProvider()` and `fileProvider()` as singletons. `ArchiveProvider` is anticipated but absent.
- Layered input stack: [port/include/inputlayer.h:42](port/include/inputlayer.h:42) declares 7 `LayerType`s (BOOT, GAMEPLAY, CUTSCENE, MENU, VEHICLE_DRIVER, VEHICLE_TURRET, OBSERVER); [port/src/inputlayer.c:21](port/src/inputlayer.c:21) implements handle-based push/pop with generation counters at [port/src/inputlayer.c:284](port/src/inputlayer.c:284) (`handleIsValid`); [port/src/scene.c:128](port/src/scene.c:128) routes 14 `SceneEvent`s into layer transitions.
- Service-locator emerging in [port/src/catalog_mgr_weapons.c:36](port/src/catalog_mgr_weapons.c:36); pure validators isolated in `catalog_mgr_weapons_pure.c` for SDL-free unit testing.

**Weak:**
- The legacy/port boundary is not tight. [src/game/chr.c:1](src/game/chr.c:1) pulls in `<ultra64.h>` plus 30+ project headers; [src/game/player.c:1](src/game/player.c:1) does the same with 50+ includes. Cross-file globals like `g_StageNum`, `g_Vars`, `g_ChrSlots` flow freely across the layer line. New port code is forced to declare `extern` for these (e.g. `extern struct prop *g_Rooms` inline at [port/src/pdmain.c:788](port/src/pdmain.c:788)).
- Two main loops. [port/src/main.c:159](port/src/main.c:159) is the SDL/PC entry, with a 50-call subsystem init sequence ([port/src/main.c:185](port/src/main.c:185) through [port/src/main.c:316](port/src/main.c:316)). Then [port/src/pdmain.c:428](port/src/pdmain.c:428) `mainProc` runs the legacy loop. The split is historically motivated (PC-level vs game-level boot) but is not encoded as a contract anywhere.
- Stage allocations are a hardcoded magic-string table at [port/src/pdmain.c:147](port/src/pdmain.c:147) with strings like `"-mgfx96 -mvtx96 -ma140"` parsed via `argFindByPrefix`. Adding a stage means editing this table and a sibling 4MB table.

**To 100:** Treat the legacy/port boundary as a real interface, not a porous one. Push `g_StageNum`, `g_NetMode`, `g_Vars`, `g_StageAllocations*` behind named getters/setters so dependency direction is enforceable. Collapse the two-loop structure into one explicit phase machine with named states. Replace the magic-string allocation table with structured per-stage budgets. Wire the asset provider's anticipated `ArchiveProvider` so `.pdmod` archives flow through the same path as ROM and loose files.

---

## Dimension 2: Connectedness (60 / 100)

**Strong:**
- Net layer has one wire protocol with versioning at [port/include/net/net.h:12](port/include/net/net.h:12) (`NET_PROTOCOL_VER 46`) and an explicit changelog in source ([port/include/net/net.h:12](port/include/net/net.h:12) through [port/include/net/net.h:147](port/include/net/net.h:147), v27 to v46). One `struct netclient` type at [port/include/net/net.h:299](port/include/net/net.h:299) is the canonical connection record across both `pd` and `pd-server` (CMakeLists shows `port/src/net/net.c` is shared between both targets at [CMakeLists.txt:631](CMakeLists.txt:631)).
- Scene event vocabulary at [port/include/scene.h:35](port/include/scene.h:35) gives 14 named transitions; `sceneFire(SCENE_EVENT_STAGE_TEARDOWN, NULL)` at [port/src/pdmain.c:643](port/src/pdmain.c:643) and [port/src/pdmain.c:663](port/src/pdmain.c:663) is the same call shape every caller uses.
- Action map is one type. [port/include/actionmap.h:54](port/include/actionmap.h:54) declares 103 `InputAction` values; everything from cutscene skip ([port/include/actionmap.h:196](port/include/actionmap.h:196), `ACTION_SKIP_CUTSCENE`) to skin editor brush size ([port/include/actionmap.h:235](port/include/actionmap.h:235)) flows through one stack of `InputMappingContext`s with explicit priorities ([port/include/actionmap.h:553](port/include/actionmap.h:553) through [port/include/actionmap.h:564](port/include/actionmap.h:564), Gameplay = 0 up to TextInput = 30).

**Weak:**
- Subsystem ticking is a serial roll-call. [port/src/pdmain.c:696](port/src/pdmain.c:696) lists eleven `xTick()` calls in order: `p2pTick(); presenceTick(); groupSessionTick(); chatTick(); fileTransferTick(); pdguiToastTick(); spectatorTick(); theaterTick(); listeningRoomTick(); shareTick(); voiceTick();`. There is no event bus, no scheduler, no priority. Every new tickable subsystem requires editing this function.
- Log routing is by string-prefix sniff. [port/src/system.c:119](port/src/system.c:119) `sysLogClassifyMessage` checks 50+ prefixes (`"NET:"`, `"UPNP:"`, `"STAGE:"`, `"DAMAGE:"`, etc.) against the formatted log string. The classifier is a parallel registry that every new module must remember to update; otherwise the message routes to channel 0 and bypasses the `Debug.LogChannelMask` config gate.
- Config is decoupled from registration order via S305 pending-replay at [port/src/config.c:114](port/src/config.c:114), which is correct but defensive. The fact that `configRegister*` calls are sprinkled across 30+ subsystems with no central listing means a typo in a key name fails silently.
- `inputctx` and `inputlayer` are two parallel stacks. [port/src/inputctx.c:55](port/src/inputctx.c:55) introduces `s_MenuLayerHandle` as a "bridge" between them. The intended architecture is one stack per layer, but the migration is incomplete; both still exist.

**To 100:** One typed event bus with publish/subscribe, replacing the serial tick list and the string-prefix log classifier. Make every subsystem register itself for a tick category (gameplay, ui, net, audio) and let the dispatcher iterate a vector. Collapse `inputctx` into `inputlayer` so there is exactly one input stack. Make config registration a compile-time list (table-of-keys) rather than runtime calls scattered through constructor attributes.

---

## Dimension 3: Cohesion (61 / 100)

**Strong:**
- Catalog manager is single-purpose. [port/src/catalog_mgr_weapons.c](port/src/catalog_mgr_weapons.c:1) is 162 lines, all weapon resolution. Mirror file `catalog_mgr_weapons_pure.c` holds testable validators with no globals, exercised by `tests/test_catalog_mgr_weapons_api.cpp` per [CMakeLists.txt:935](CMakeLists.txt:935).
- Scene dispatcher is 297 lines ([port/src/scene.c](port/src/scene.c:1)) and does exactly one thing: translate events to layer ops.
- p2p connectivity is correctly factored: each tier in its own file with a small dispatcher. [port/src/net/p2p.c](port/src/net/p2p.c:1) is 370 lines; tier files are 131 to 377 lines (`p2p_direct.c`, `p2p_ice.c`, `p2p_lan.c`, `p2p_stun.c`, `p2p_turn.c`, `p2p_upnp.c`). One responsibility per file.
- Asset catalog separation: registration ([port/src/assetcatalog_base.c](port/src/assetcatalog_base.c:1), `_base_extended.c`, `_scanner.c`), query ([port/src/assetcatalog_api.c](port/src/assetcatalog_api.c:1), `_resolve.c`), load ([port/src/assetcatalog_load.c](port/src/assetcatalog_load.c:1)), cache ([port/src/assetcatalog_cache.c](port/src/assetcatalog_cache.c:1)), deps ([port/src/assetcatalog_deps.c](port/src/assetcatalog_deps.c:1)).

**Weak:**
- pdmain.c at 1046 lines mixes the main loop, hardcoded stage allocation tables (`g_StageAllocations8Mb` at [port/src/pdmain.c:147](port/src/pdmain.c:147), `g_StageAllocations4Mb` at [port/src/pdmain.c:243](port/src/pdmain.c:243)), `mainEndStage` at [port/src/pdmain.c:923](port/src/pdmain.c:923), and a 100-line embedded B-193 diagnostic logger at [port/src/pdmain.c:777](port/src/pdmain.c:777) through [port/src/pdmain.c:895](port/src/pdmain.c:895). The diagnostic walks `extern struct room *g_Rooms`, `extern struct bgcmd *g_BgCommands`, etc. inline, polluting the tick body with hypothesis-driven debug code that should live behind a flag in its own module.
- Some menu files are god files. [port/fast3d/pdgui_menu_mainmenu.cpp](port/fast3d/pdgui_menu_mainmenu.cpp:1) is 6361 lines. [port/fast3d/pdgui_menu_room.cpp](port/fast3d/pdgui_menu_room.cpp:1) is 4543 lines. [port/fast3d/pdgui_menu_solomission.cpp](port/fast3d/pdgui_menu_solomission.cpp:1) is 3687 lines. [port/fast3d/pdgui_theme.cpp](port/fast3d/pdgui_theme.cpp:1) is 3298 lines.
- Net.c at 2864 lines includes 50+ headers covering transport, hudmsg, participant, player, playermgr, bot, chr, bondgun, multiple game stubs, lv, menu, pdmode, mplayer, lib/main, lib/vi, config, system, breadcrumb, console, fs, romdata, utils, room, audio, sha256, server_bans, scene_transition, input, inputctx, menupool, scene. Net code reaching into game and UI by header, not by abstraction.
- Legacy decomp files: [src/game/chr.c](src/game/chr.c:1) is 6773 lines; [src/game/player.c](src/game/player.c:1) is 7514 lines; [src/game/menu.c](src/game/menu.c:1) is 6864 lines; [src/game/mplayer/setup.c](src/game/mplayer/setup.c:1) is 6648 lines. These are inherited monoliths, not new bloat.
- [port/include/net/net.h](port/include/net/net.h:12) embeds a 135-line protocol changelog (v27 through v46) as a single `#define NET_PROTOCOL_VER 46` comment block. Useful as documentation, but it shows wire format churn at a heavy cadence.

**To 100:** Move B-193 and similar embedded diagnostics into their own modules behind a `pddiag_*` namespace toggleable per build. Split `pdgui_menu_mainmenu.cpp` along its tab boundaries (Play/Settings) into separate files; same treatment for `_room.cpp` and `_solomission.cpp`. Split `pdmain.c` into `pdmain_loop.c`, `pdmain_stage_alloc.c` (with structured tables, not magic strings). Make `net.c` consume a small game-side facade instead of including 50 game headers directly.

---

## Dimension 4: Modernity (74 / 100)

**Strong:**
- Toolchain. [CMakeLists.txt:20](CMakeLists.txt:20) sets `CMAKE_C_STANDARD 11`, [CMakeLists.txt:22](CMakeLists.txt:22) sets `CMAKE_CXX_STANDARD 20`. PCH for `types.h`, `ultra64.h`, `data.h`, `constants.h` at [CMakeLists.txt:602](CMakeLists.txt:602). Auto-discovery via `file(GLOB_RECURSE)` at [CMakeLists.txt:490](CMakeLists.txt:490).
- Static linking discipline. [CMakeLists.txt:301](CMakeLists.txt:301) statically links SDL2 from MSYS2; [CMakeLists.txt:393](CMakeLists.txt:393) statically links zlib; [CMakeLists.txt:411](CMakeLists.txt:411) statically links libcurl with a full TLS chain (libssl, libcrypto, libnghttp2/3, brotli, idn2, psl, zstd). Embedded Mozilla CA bundle at [CMakeLists.txt:163](CMakeLists.txt:163). [CMakeLists.txt:830](CMakeLists.txt:830) confirms `opengl32.dll` is the only allowed dynamic dep.
- Crypto and integrity. [port/src/sha256.c](port/src/sha256.c:1) and [port/src/ed25519.c](port/src/ed25519.c:1) are linked into the client via [CMakeLists.txt:684](CMakeLists.txt:684) (server) and the same pool feeds `pd-updater`. `NET_PROTOCOL_VER 46` at [port/include/net/net.h:12](port/include/net/net.h:12) appends a 32-byte SHA-256 of the `.pdmod` archive on `SVC_DISTRIB_BEGIN` per the v46 description.
- Test runner. `pd-tests` at [CMakeLists.txt:856](CMakeLists.txt:856) cherry-picks pure files and builds them against Catch2 (`port/include/catch.hpp`), with state-machine, reachability, roundtrip, and pin tests covering input, menu, save migration, manifest, catalog, options, and the new catalog manager (visible across [CMakeLists.txt:856](CMakeLists.txt:856) through [CMakeLists.txt:947](CMakeLists.txt:947)).
- C++ stdlib used pragmatically. [port/fast3d/gfx_pc.cpp:11](port/fast3d/gfx_pc.cpp:11) imports `<map>`, `<set>`, `<unordered_map>`, `<vector>`, `<list>`, `<stack>`. [port/fast3d/gfx_pc.cpp:100](port/fast3d/gfx_pc.cpp:100) `std::map<ColorCombinerKey, struct ColorCombiner>` for the shader pool. [port/fast3d/gfx_pc.cpp:253](port/fast3d/gfx_pc.cpp:253) `std::map<int, FBInfo>` for framebuffers.
- Hash-table catalog at [port/src/assetcatalog.c:67](port/src/assetcatalog.c:67) (FNV-1a) plus reflected-CRC32 lookup table at [port/src/assetcatalog.c:85](port/src/assetcatalog.c:85). Open addressing with linear probing, 70% load factor rehash trigger ([port/src/assetcatalog.c:175](port/src/assetcatalog.c:175)), generation counter at [port/src/assetcatalog.c:56](port/src/assetcatalog.c:56) for stale-cache detection.
- Modern Win32 timing. [port/src/system.c:284](port/src/system.c:284) probes `CreateWaitableTimerExA` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` and falls back gracefully.
- Update system. [port/src/updater.c](port/src/updater.c:1) plus standalone Updater.exe with its own Win32 GUI ([CMakeLists.txt:740](CMakeLists.txt:740) through [CMakeLists.txt:818](CMakeLists.txt:818)).

**Weak:**
- Mixed type vocabulary. New code uses `bool`, `s32`, `u8`, `f32`. Legacy code uses the same `s32/u8/f32` plus raw `int`. The decomp baseline keeps `var8005d9b0`, `var8005d9bc`, `var8005d9c0`, `var8005d9c4` at [port/src/pdmain.c:116](port/src/pdmain.c:116) through [port/src/pdmain.c:121](port/src/pdmain.c:121); these are ROM-address variable names preserved for matching, with ~790 occurrences across `src/game`. Modernization is in flight, not finished.
- Memory model. `mempAlloc(MEMPOOL_STAGE)` at [port/src/main.c:329](port/src/main.c:329) allocates 64 MB up front via `sysMemZeroAlloc`, then sub-allocates from that single arena. This is the N64 mempool model on top of malloc. Modern target hardware does not need this; it is preserved for legacy compatibility.
- Constructor attribute. [port/src/main.c:395](port/src/main.c:395) `PD_CONSTRUCTOR static void gameConfigInit(void)` uses GCC `__attribute__((constructor))` for config registration. Works on GCC/Clang; brittle if MSVC support ever returns.
- C++ files use raw pointers and manual lifetime. No `std::unique_ptr`, no `std::shared_ptr` visible in `port/fast3d/`. ImGui style globals, manual cleanup.
- 296 `extern "C"` boundaries across 76 fast3d files (counted via grep `^extern "C"`). The C/C++ wall is real; every C++ file rewrites the boundary.

**To 100:** Finish the type-vocabulary unification (one project type system, not "s32 here, int there, bool sometimes"). Replace the legacy mempool with direct malloc / a real allocator (jemalloc/mimalloc/std::pmr). Move config registration to a compile-time table so subsystem init order does not matter and the constructor-attribute trick can be retired. Adopt RAII discipline in C++ port files. Migrate the remaining `var8005...` decomp variables to named identifiers as files get touched.

---

## Dimension 5: Practicality (67 / 100)

**Strong:**
- One build command from a known shell: `ninja -C Build pd pd-server pd-tests`. CMakeLists.txt's auto-discovery makes adding a new file effortless ([CMakeLists.txt:490](CMakeLists.txt:490)).
- Test runner is fast: `pd-tests` is a pure subset of the codebase that does not link SDL/GL/ENet, per [CMakeLists.txt:849](CMakeLists.txt:849).
- Channeled logging. [port/src/system.c:73](port/src/system.c:73) defines 14 channels (Network, Game, Combat, Audio, Menu, Save, Mods, System, Match, Catalog, Distrib, Render, Benchmark, TestScen). Verbose toggle plus mask makes log triage tractable.
- CLI surface is rich. [port/src/main.c:170](port/src/main.c:170) `--no-crash-handler`, [port/src/main.c:180](port/src/main.c:180) `--dedicated`, [port/src/main.c:230](port/src/main.c:230) `--no-update-check`, [port/src/main.c:343](port/src/main.c:343) `--no-sound`, [port/src/main.c:345](port/src/main.c:345) `--boot-stage`, [port/src/main.c:347](port/src/main.c:347) `--profile`, [port/src/main.c:349](port/src/main.c:349) `--skip-intro`, plus `--bench-pdmod` at [port/src/main.c:191](port/src/main.c:191). Iteration paths exist.
- Build-time vs runtime gating: `PD_DEV_BUILD` from [CMakeLists.txt:594](CMakeLists.txt:594) leaves dev hotkeys (F6/F7/F12) and Debug tab in non-stable builds.
- Embedded Common Controls v6 manifest ([CMakeLists.txt:746](CMakeLists.txt:746)) and HiDPI handling for the standalone Updater. Production polish.

**Weak:**
- File naming for legacy code is opaque. `src/game/game_175f50.c`, `src/game/game_1531a0.c`, `src/lib/lib_2f490_c.c`, `src/lib/lib_04f60nb.c` are ROM-offset-derived. Finding "where is the music handler" requires either grep or already knowing the address. Approximately 25 such files in `src/game/` and `src/lib/`. The decomp project never resolved these and the port inherited them.
- 196 files in `src/game` plus subdirs makes "what controls X" a research task. Rename effort versus risk has been deferred.
- 50+ headers per game/net/player file. [src/game/player.c:1](src/game/player.c:1) and [src/game/chr.c:1](src/game/chr.c:1) compile slowly without PCH and force re-parsing of the included world on every TU.
- `extern struct ...` declarations sprinkled inside function bodies. [port/src/pdmain.c:787](port/src/pdmain.c:787) inlines `extern u8 *g_BgPrimaryData; extern struct room *g_Rooms; extern s32 g_BgNumRoomLoadCandidates;` mid-function. This works but undermines the "header is the contract" model.
- The two-tier directory model (src/ for legacy, port/ for new) is not enforced. Some new files live in `src/` (e.g. catalog manager glue gets pulled into both). A clear "all new code in port/" rule with mechanical enforcement would help.

**To 100:** Rename all `game_<address>.c` and `lib_<address>.c` files to their actual purpose (one PR per file, with a header tombstone if any external tool depends on the old name). Mechanically enforce that `src/` is read-only legacy and all new code goes in `port/`. Add `compile_commands.json` generation if not already on. Move all `extern` declarations into proper headers.

---

## Dimension 6: Extensibility (74 / 100)

**Strong:**
- Asset catalog is string-keyed. [port/include/net/net.h:218](port/include/net/net.h:218) shows `char stage_id[CATALOG_ID_LEN]` as the primary identity, with the integer `stagenum` marked `DEPRECATED: Use stage_id instead`. The wire format encodes catalog session refs (u16) per the v30/v31/v32 entries in [port/include/net/net.h:138](port/include/net/net.h:138) through [port/include/net/net.h:147](port/include/net/net.h:147). This is the load-bearing extensibility decision: any mod-added asset gets a stable identity that survives mod set changes.
- Mod manifest pipeline. [port/src/modmgr.c](port/src/modmgr.c:1) parses `mod.json` per mod, registers components into the catalog, persists enable state to `.modstate`, and rebuilds caches via `modmgrCatalogChanged` at [port/src/main.c:320](port/src/main.c:320). The flow is: scan -> register -> resolve -> cache, with `modmgrInit()` at [port/src/main.c:279](port/src/main.c:279) and `assetCatalogScanComponents` at [port/src/main.c:298](port/src/main.c:298).
- Provider abstraction at [port/include/assetprovider.h:55](port/include/assetprovider.h:55) means adding a new asset source (zip archive, HTTP, embedded) is a single new singleton.
- Scene event vocabulary at [port/include/scene.h:35](port/include/scene.h:35) is extensible: new game phases get added by enum + dispatcher entry.
- Action map enum at [port/include/actionmap.h:54](port/include/actionmap.h:54) is sentinel-terminated (`ACTION_COUNT` at [port/include/actionmap.h:248](port/include/actionmap.h:248)). New actions are end-of-enum additions; IMC priority slots ([port/include/actionmap.h:553](port/include/actionmap.h:553)) accommodate new contexts.
- Forge level editor in [port/src/forge/](port/src/forge/forge_core.c:1) is itself a mod-style overlay: own runtime, AI, gametype, logic, serialize, undo. Demonstrates the architecture supports user-created stages.
- Theme and skin systems. [port/fast3d/pdgui_theme_loader.cpp](port/fast3d/pdgui_theme_loader.cpp:1) (1761 lines) and [port/fast3d/pdgui_skin_editor.cpp](port/fast3d/pdgui_skin_editor.cpp:1) (1762 lines) ship as full-featured user-asset pipelines.
- Test scenarios infra. [CMakeLists.txt:875](CMakeLists.txt:875) `test_swarm_boid_sim.cpp`, [port/src/swarm_test.c](port/src/swarm_test.c:1), action `ACTION_TESTSCEN_CYCLE_COUNT` at [port/include/actionmap.h:177](port/include/actionmap.h:177). Performance and stress harnesses are first-class.

**Weak:**
- Stage table is hardcoded. `g_StageAllocations8Mb` at [port/src/pdmain.c:147](port/src/pdmain.c:147) is a static array; mods cannot add a stage without recompiling the executable. The catalog has stage entries (the v32 wire change), but the per-stage memory budget remains hardcoded.
- Original game systems are not modular. `src/game/player.c`, `src/game/chr.c`, `src/game/bondgun.c` are not behind any registration hook. A "new weapon class" mod cannot register new bondgun behaviors without patching the file.
- Menu pool registry has a fixed cap. [port/src/menupool.c:86](port/src/menupool.c:86) `MENUPOOL_REGISTRY_CAP 96`. Comment says "headroom for late-registered mod dialogs" but it is still a static cap.
- The 50+ extern `struct menudialogdef` declarations in [port/src/menupool.c:22](port/src/menupool.c:22) through [port/src/menupool.c:81](port/src/menupool.c:81) encode "every dialog that exists" by name. New mod dialogs cannot self-register without code changes.

**To 100:** Promote the stage budget into a data file (per-stage budget JSON loaded at boot). Allow mods to add stages by dropping a stage manifest plus assets; the catalog already supports the identity, the loader needs to grow. Open the menu pool to runtime registration so mods can drop in their own dialogs. Provide registration hooks in the legacy game systems (e.g. weapon behavior class pointers) so mods do not have to fork `bondgun.c`.

---

## Dimension 7: Vision (70 / 100)

**Strong directional signal:**
- "Catalog Universality" thread visible in code: stage_id, scenario_id, weapon catalog session refs, body_id, head_id at [port/include/net/net.h:308](port/include/net/net.h:308) and [port/include/net/net.h:309](port/include/net/net.h:309). The v27 to v46 wire-protocol changelog at [port/include/net/net.h:12](port/include/net/net.h:12) shows a year-long, coordinated migration to string identity.
- Cohort-based migration visible in source comments. [port/include/inputlayer.h:13](port/include/inputlayer.h:13) "Cohort 3 introduces the Scene Manager... Cohort 4 wires the cutscene flash fix." [port/include/scene.h:8](port/include/scene.h:8) "Cohort 3 deliberately ships the dispatcher only. Real callsite wiring lands in Cohort 4 onward so each migration can be bisected independently." This is mature engineering discipline.
- Service-locator pattern emerging. [port/src/catalog_mgr_weapons.c:8](port/src/catalog_mgr_weapons.c:8) "Phase 2 (F1-F10): this manager is a thin pass-through router... F3-F8 migrate remaining direct table accesses through the manager. F11-F13 (next session) replace the legacy backing tables with manager-owned data sourced from .pdbase JSON files." The `_pure.c` companion file pattern and matching `tests/` pin (`test_catalog_mgr_weapons_api.cpp` at [CMakeLists.txt:935](CMakeLists.txt:935)) is meant to scale to all asset domains.
- pd / pd-server / pd-tests / pd-updater four-target architecture at [CMakeLists.txt:587](CMakeLists.txt:587), [CMakeLists.txt:716](CMakeLists.txt:716), [CMakeLists.txt:949](CMakeLists.txt:949), [CMakeLists.txt:752](CMakeLists.txt:752). Clear separation of concerns: game client, dedicated server, test runner, recovery utility.
- Build-id and version pinning. [CMakeLists.txt:127](CMakeLists.txt:127) through [CMakeLists.txt:131](CMakeLists.txt:131) declares `VERSION_SEM_MAJOR/MINOR/PATCH` as cache strings; the updater consumes them. There is a release plan, not just a build.

**Entropy:**
- Many active fronts visible from code prefixes alone: B-12, B-129, B-193, B-256, INV-1, S195, S303, S305, S309, S311, S313, S483, S484, P5, P10, D3R, D5, D7, D13, K.2, K.6, M0.2, MASTER-C2, SEC-5, SEC-7, SEC-14. Code comments reference at least 30 distinct work threads. This is a feature, not a bug, but the reader has to map prefixes to themes manually.
- Mid-flight migrations leave artifacts. [port/src/catalog_mgr_weapons.c:60](port/src/catalog_mgr_weapons.c:60) returns `g_Weapons[weapon_id]` directly: "this manager is a thin pass-through router over the legacy g_Weapons[] global." The end state is manager-owned, but today's state is router-only. A reader sees the seam.
- Two parallel input stacks (`inputctx` and `inputlayer`). [port/src/inputctx.c:46](port/src/inputctx.c:46) describes a "bridge" between them. Both are in production. The desired one-stack end state is in code comments but not in code.
- Diagnostic embedments stay in production code. [port/src/pdmain.c:777](port/src/pdmain.c:777) "B-193 Phase 4: portal-walker branch state" is in `mainTick`; the bug it diagnoses is presumably resolved or being tracked. The instrumentation should retire when the bug closes; right now it is permanent overhead.
- Comments routinely point at design docs in `context/designs/...`. The code is meaningful in isolation but the architectural narrative lives elsewhere.

**To 100:** Land the catalog manager rollouts (F11+) so the legacy tables go away; eliminate the seam. Collapse `inputctx` into `inputlayer`. Move embedded diagnostics behind a build flag and prune resolved ones. Make the work-prefix taxonomy machine-discoverable (e.g. an enum of phase tags) rather than ambient. Add an architecture overview that lives in `port/include/README` (not loaded by this audit) so the vision is in the source tree alongside the code.

---

## Aggregate calculation

Equal weighting across the seven dimensions:

| Dimension | Score |
| --- | ---: |
| Architecture | 66 |
| Connectedness | 60 |
| Cohesion | 61 |
| Modernity | 74 |
| Practicality | 67 |
| Extensibility | 74 |
| Vision | 70 |
| **Mean** | **67.4** |

Scaled to 1000: **674**. Rounded to **675 / 1000**.

This is solid mid-third territory: not a thin port, not a polished native rewrite. A real architecture exists but is not yet uniform across the codebase. The trajectory is up and to the right.

---

## Top moves toward 1000

In priority order. Each is a code-level, file-specific change with a measurable architecture consequence.

1. **Eliminate the legacy/port boundary leak.** Replace direct `extern` reads of `g_StageNum`, `g_NetMode`, `g_Vars`, `g_StageAllocations*`, `g_BgPrimaryData`, `g_Rooms`, `g_Chrnums`, `g_ChrSlots`, etc. with named getters declared in proper headers. Concrete starting point: every inline `extern` at [port/src/pdmain.c:787](port/src/pdmain.c:787) should be hoisted to a header and replaced with an accessor. Estimated ceiling lift: +6 architecture, +4 connectedness, +3 practicality.

2. **One typed event bus, retire the serial tick list.** Replace [port/src/pdmain.c:696](port/src/pdmain.c:696) (the eleven-call sequence) with `tickBusDispatch(TICK_PHASE_GAMEPLAY)` plus subscribers registering at init. Apply the same to `mainTick`'s 30+ subsystem touches. Estimated ceiling lift: +6 connectedness, +3 architecture, +3 cohesion.

3. **Collapse `inputctx` into `inputlayer`.** [port/src/inputctx.c:46](port/src/inputctx.c:46) says the bridge is a stopgap. Make `g_LayerMenu` carry the data `InputContext` carries, retire `s_Stack[INPUTCTX_MAX_STACK]`, retire `inputctxPush/Pop`, route every consumer through `inputLayerPush/Pop`. Estimated ceiling lift: +5 architecture, +5 cohesion, +3 connectedness.

4. **Replace string-prefix log routing with structured channel call sites.** [port/src/system.c:119](port/src/system.c:119) is a parallel registry that rots. Make `sysLogPrintf(LOG_NOTE | LOG_CH_NETWORK, "auth ok")` the call shape. Estimated ceiling lift: +3 connectedness, +2 modernity.

5. **Split god-files at natural seams.** [port/fast3d/pdgui_menu_mainmenu.cpp](port/fast3d/pdgui_menu_mainmenu.cpp:1) -> `pdgui_menu_play.cpp` + `pdgui_menu_settings.cpp`. [port/fast3d/pdgui_menu_room.cpp](port/fast3d/pdgui_menu_room.cpp:1) similarly. Move B-193 inline diagnostic at [port/src/pdmain.c:777](port/src/pdmain.c:777) to `port/src/diag_b193.c` behind a `PD_DIAG_B193` flag. Move stage allocation tables out of `pdmain.c`. Estimated ceiling lift: +6 cohesion, +2 practicality.

6. **Promote stage budgets and stage definitions to data files.** Today [port/src/pdmain.c:147](port/src/pdmain.c:147) hardcodes 90 stage strings. Move to `assets/stages/<id>.json` resolved through the catalog. Mod-added stages then need no code change. Estimated ceiling lift: +6 extensibility, +2 vision.

7. **Finish the catalog manager rollout.** Turn [port/src/catalog_mgr_weapons.c:59](port/src/catalog_mgr_weapons.c:59) from `return g_Weapons[weapon_id]` into a real manager-owned read. Apply the same pattern to bodies, heads, scenarios, models. Each retirement of a legacy `g_*` table moves vision from "in flight" to "landed." Estimated ceiling lift: +5 vision, +3 architecture, +3 modernity.

8. **Rename ROM-address files.** `src/game/game_175f90.c` -> `src/game/post_stage_start.c` (or whatever its content is). Approximately 25 files. Each rename is mechanical, but they add up to a much more readable tree. Estimated ceiling lift: +5 practicality.

9. **Replace the legacy mempool with a real allocator.** [port/src/main.c:329](port/src/main.c:329) allocates 64 MB up front and sub-allocates for the rest of the process. On modern hardware, direct malloc + a small arena per stage is simpler and faster. Estimated ceiling lift: +3 modernity, +2 architecture.

10. **One project type vocabulary.** Pick `int32_t/uint8_t/float/bool` (or `s32/u8/f32/bool`) and unify. The existing `PR/ultratypes.h` is the typedef target. New code should not reach for `int` and old code should migrate as it gets touched. Estimated ceiling lift: +3 modernity, +2 cohesion.

If items 1 to 4 land cleanly, the score climbs into the high 700s. Items 5 to 7 push into the 800s. Items 8 to 10 plus the ongoing migrations (cohorts, catalog managers, wire versions) close the rest of the gap. A 1000 outcome looks like: one input stack, one event bus, one allocator, one type vocabulary, one mod plug-in surface, and zero hardcoded magic-string tables. The bones for all of that are visible in the current code.
