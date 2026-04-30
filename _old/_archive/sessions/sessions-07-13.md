# Session Archive: Sessions 7–13

> **Period**: 2026-03-21
> **Theme**: Build stabilization, branch reconciliation, networking polish, model loading crash chain
> Back to [index](README.md)

---

## Session 13 — 2026-03-21

**Focus**: Fix client crash — model loading at boot before subsystems ready

- **True root cause**: `catalogValidateAll()` ran before `texInit()`/`langInit()`. Model loading touched uninitialized texture/skeleton systems → 151 VEH cycles → heap corruption → silent death.
- **Fix**: Removed `catalogValidateAll()` from boot. Models validated lazily via `catalogGetSafeBody()`/`catalogGetSafeHead()` during gameplay.
- Kept defensive improvements: `fileLoadToNew` pre-check for non-existent ROM files, `modeldefLoad` clears `g_LoadType` on NULL, `catalogValidateOne` pre-check.

**Key insight**: Boot-time model validation was doomed by init ordering. Correct architecture is lazy/on-demand validation after full init.

**Files**: pdmain.c, file.c, modeldef.c, modelcatalog.c

---

## Session 12 — 2026-03-21

**Focus**: Client crash-on-launch diagnosis; server log filename bug

- `catalogValidateAll()` called before `mempSetHeap()` → 151 consecutive access violations on uninitialized pool state.
- **Fix**: Moved to pdmain.c after `mempSetHeap()`. Added `mempGetStageFree()` defensive guard.
- **Server log fix**: `sysInit()` now checks `g_NetDedicated` variable (not just CLI flag).

**Files**: main.c, pdmain.c, modelcatalog.c, system.c

---

## Session 11 — 2026-03-21

**Focus**: Log channel filter system, always-on logging, build tool polish

- **Always-on logging**: Removed `--log` gate. Filename by mode: pd-server/pd-host/pd-client.log.
- **8-channel filter**: Network, Game, Combat, Audio, Menu, Save, Mods, System. ~30 prefix mappings. Zero changes to 470+ existing log sites.
- **LOG_VERBOSE level**: Below LOG_NOTE, off by default, `--verbose` CLI flag.
- **ImGui debug menu**: Log Filters section with All/None presets, per-channel checkboxes, verbose toggle.
- **Build tool**: Layout restructure, Handel Gothic font, version labels (dev/stable from GitHub), CMake error surfacing, Settings restructure (Edit menu, tabs, ROM path, extraction tools).
- **CMake icon.rc fix**: EXISTS guards on both executable targets.

**Files**: system.h, system.c, pdgui_debugmenu.cpp, build-gui.ps1, CMakeLists.txt, release.ps1

---

## Session 10 — 2026-03-21

**Focus**: Connect code system, ROM missing dialog, build tooling

- **Connect code**: IP+port → 6 PD-themed words. 256-word table. Encode/decode in connectcode.c/h.
- Server GUI + lobby overlay show connect code with Copy button.
- Client join accepts raw IP:port OR word-based connect codes (auto-detect).
- **ROM missing dialog**: Custom SDL_ShowMessageBox with "Open Folder" button. Cross-platform.

**Files**: connectcode.c/h, server_gui.cpp, pdgui_lobby.cpp, netupnp.c, pdgui_menu_network.cpp, netmenu.c, romdata.c

---

## Session 9 — 2026-03-21

**Focus**: Version format simplification, build tool polish, exe renaming, release packaging

- Version format: 4-field → 3-field semantic versioning
- Dark menu theme, taskbar visibility, GitHub release checking, offline cache
- Executable renaming to PerfectDark.exe / PerfectDarkServer.exe
- release.ps1 restructuring
- Began connect code implementation

---

## Session 8 — 2026-03-21

**Focus**: Updates tab, download button UX, spawn pads, branch consolidation

- Wired "Updates" tab as 5th tab in Settings menu
- Refactored update UI for inline Settings rendering + floating picker from notification banner
- Fixed download button UX (failure state, retry, null safety)
- Expanded spawn pad arrays from 24 → MAX_MPCHRS (32)
- **Consolidated to single-branch workflow**: Merged `release` into `dev`. Removed branch switcher. Channel derived from version field.

**Decision**: Single branch forever. `dev > 0` = Dev channel, `dev = 0` = Stable.

---

## Session 7 — 2026-03-21

**Focus**: Branch reconciliation, multi-bug triage, full system audit

**Problem**: 5 issues after building from `release` branch: missing Update tab, libcurl DLLs, server crash on join, can't add bots, can't reach lobby.

**Root causes**:
1. **Branch divergence** — `release` missing half the UI work from `dev`
2. **MATCH_MAX_SLOTS struct mismatch** — C had 32, C++ had 16 → all fields after `slots[]` at wrong offsets → bot failure, server crash, lobby unreachable
3. **Static libcurl OpenGL crash** — 30 transitive deps caused Windows GDI to init before GPU's OpenGL ICD

**Fixes**: Switched to dev branch. Cherry-picked strncpy + 32-slot expansion with MATCH_MAX_SLOTS sync. Switched to dynamic libcurl.

**Full audit results**: Menu (PASS), Lobby/Network (PASS), Spawn System (PASS with pad array caveat), Update Tab (wiring needed).

**Decision**: Dev branch is primary. Dynamic curl permanent. MATCH_MAX_SLOTS always via MAX_MPCHRS.
