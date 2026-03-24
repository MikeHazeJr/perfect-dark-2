# Session Archive: Sessions 1–6

> **Period**: 2026-03-17 to 2026-03-21
> **Theme**: Foundation — N64 strip, mod manager, ImGui, char select, networking, strncpy audit
> Back to [index](README.md)

---

## Session 6 — 2026-03-21

**Focus**: strncpy null-termination audit, ADR-001, build tool commit

- Re-applied netmsg.c fixes lost during context compaction (3 strncpy + 1 strcpy→snprintf)
- **Propagation check**: Found 17 additional strncpy instances across 8 files (net.c, updater.c, modmgr.c, fs.c, libultra.c, config.c, input.c, optionsmenu.c) — all fixed
- Created [ADR-001](ADR-001-lobby-multiplayer-architecture-audit.md)
- Build tool changes committed: static linking fix, headless server, auto-stash, version increment, release overwrite

**Decision**: strncpy null-termination is project-wide standard; future code should prefer snprintf

---

## Sessions 4–5 — 2026-03-20

**Focus**: Architecture audit, build tool improvements, release tooling

- Complete architecture audit of lobby/multiplayer systems
- Fixed CLC_MAP_VOTE_START ID collision (0x09 → 0x0A)
- CMakeLists.txt: Fixed static linking path resolution (SDL2, zlib, libcurl)
- build-gui.ps1: Server headless launch, auto-stash branch switching, version increment buttons
- release.ps1: Release overwrite support, DLL warning cleanup
- 3 verified false positives in the audit

**Decisions**: Server launches headless by default. Auto-stash on branch switch. Release overwrite: delete existing then recreate.

---

## Session 3 — 2026-03-19

**Focus**: Phase 3 dedicated server, lobby system

- Completed Phase 3: Dedicated-server-only multiplayer model
- New multiplayer menu (server browser, direct IP connect)
- Lobby system rewrite with leader election
- Lobby screen with game mode selection
- Server GUI (4-panel layout) and headless mode
- CLC_LOBBY_START protocol for match launching
- Renamed "Network Game" to "Multiplayer", removed stale host menus

---

## Session 2 — 2026-03-18

**Focus**: ImGui debug menu, styling, build system

- PD-authentic styling with pixel-accurate shimmer from menugfx.c
- 7 built-in palettes including Black & Gold
- F12 debug menu with mouse capture/release
- Build tool: colored progress bar, gated run buttons, process monitoring
- Font size 16pt → 24pt

---

## Session 1 — 2026-03-17

**Focus**: Menu system Phase 2 (agent create, delete, typed dialogs, network audit)

- Agent Create screen with 3D character preview (FBO render-to-texture)
- Agent Select enhancements (contextual actions, delete confirmation, duplicate)
- Typed dialog system (MENUDIALOGTYPE_DANGER + MENUDIALOGTYPE_SUCCESS)
- Network audit began (ENet protocol, message catalog)
- Skedar/DrCaroll mesh fix (duplicate array indices in g_MpBodies)
