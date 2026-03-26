# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Awaiting Build Test / Playtest

| Item | Status |
|------|--------|
| **Collision Rewrite** (S48) | DISABLED -- original collision restored. Mesh code preserved for Phase 2 redesign. |
| **Data copy fix** (S48) | Rewritten with Split-Path parent traversal (no Resolve-Path/.. issues). Error popup on failure. Needs verify. |
| **SPF-1/3**: Hub, rooms, identity, lobby, join-by-code (S47d–S49) | Run `.\devtools\build-headless.ps1 -Target server`, then end-to-end join test (J-1) |
| **Player Stats** (playerstats.c): String-keyed counters, JSON persistence (S49) | Needs client build test. `port/src/playerstats.c` |
| **D3R-7**: Modding Hub -- 6 files (S40) | Needs client build test |
| **MEM-1**: Asset load state fields in asset_entry_t (S47a) | Needs full cmake pass |
| **B-13**: Prop scale fix -- modelGetEffectiveScale (S26) | Needs build test |
| **B-12 Phase 1**: Dynamic participant system (S26) | Needs build test |

---

## Bugs Still Open

| Bug | Severity | Status |
|-----|----------|--------|
| [B-17](bugs.md) Mod stages load wrong maps | HIGH | Structurally fixed (S32). Needs broader testing across all mod maps. |
| B-18 Pink sky on Skedar Ruins | MEDIUM | Reported S48. Possible missing texture or clear color issue. Needs investigation. |
| B-19 Bot spawn stacking on Skedar Ruins | MEDIUM | Investigated: mod stages lack INTROCMD_SPAWN entries in setup file. Fallback picks pad 0. Needs g_SpawnPoints population from arena pad data. |
| B-20 Mission 1 objective crash | HIGH | **FIXED (S48)** -- NULL modeldef guard added in modelmgrInstantiateModel. Root cause: objective completion spawns chr whose body filenum fails to load. |
| B-21 Menu double-press / hierarchy issues | MEDIUM | Escape and other inputs registering multiple times, menu state confusion. |
| B-24 Connect code byte-order reversal | CRITICAL | **FIXED (S49)** -- `pdgui_menu_mainmenu.cpp` byte extraction corrected (MSB→LSB order). |
| B-25 Server max clients = 8 | MEDIUM | **FIXED (S49)** -- `NET_MAX_CLIENTS` decoupled from `MAX_PLAYERS`, now 32. |
| B-26 Player name shows "Player1" | HIGH | **FIXED (S49)** -- `netClientReadConfig()` falls back to identity profile name. |

---

## Active Work Tracks

### Memory Modernization (D-MEM)

| Task | Status |
|------|--------|
| MEM-1: Load state fields | CODED (S47a) -- needs build test |
| MEM-2: assetCatalogLoad/Unload | PENDING |
| MEM-3: ref_count + eviction | PENDING |

### B-12: Participant System

| Phase | Status |
|-------|--------|
| Phase 1: Parallel pool | CODED -- needs build test |
| Phase 2: Callsite migration | DONE (S47b) -- build pass |
| Phase 3: Remove chrslots + protocol v22 | READY -- depends on Phase 2 QC |

### Join Flow (J-series) — Next Steps

| Phase | Task | Details |
|-------|------|---------|
| J-1 | **Verify end-to-end join** | Build server target, start server, enter code in client, verify CLSTATE_LOBBY + match start. |
| J-2 | **Server GUI connect code** | Add connect code display + Copy button to server_gui.cpp Server tab. |
| J-3 | **SVC_ROOM_LIST protocol** | Broadcast room state from server to clients so lobby UI shows real room data. |
| J-4 | **Server history UI** | Populate Recent Servers section. Display connect codes (re-encode), not raw IPs. |
| J-5 | **Lobby handoff polish** | Verify main menu → lobby overlay transition. Progress indicator during CONNECTING/AUTH. |

See [join-flow-plan.md](join-flow-plan.md) for full audit.

---

### Asset Catalog Expansion

| Task | Status |
|------|--------|
| S46b: Full enumeration (anims, SFX, textures) | PENDING |

---

## Prioritized Next Up

| # | Task | Details |
|---|------|---------|
| 1 | **J-1: Verify end-to-end join** | Build server target + end-to-end test. See join-flow-plan.md. |
| 2 | **J-2: Server GUI connect code** | Add code display to server_gui.cpp. |
| 3 | **Menu Replacement Group 1** | Solo mission flow (11 menus). See [menu-replacement-plan.md](context/menu-replacement-plan.md). |
| 2 | **Catalog Phase C-1/C-2** | ROM hash + base game catalog population. See [catalog-loading-plan.md](context/catalog-loading-plan.md). |
| 3 | **Catalog Phase C-4** | Intercept fileLoadToNew — catalog resolve before ROM load. Critical gateway. |
| 4 | **Menu Replacement Group 2** | End screens (13 menus). |
| 5 | **Menu Replacement Group 4** | Multiplayer setup (68 menus, largest group). |
| 6 | **Catalog Phase C-8** | Mod diff-based re-cataloging. |
| 7 | **Collision Rewrite Design** | Proper design. HIGH PRIORITY but design-first. |
| 8 | **B-19: Bot spawn stacking** | Populate g_SpawnPoints from arena pad data. |
| 6 | **B-20: Mission 1 crash** | Objective completion triggers loading that may bypass catalog. |
| 7 | **B-18: Pink sky** | Investigate Skedar Ruins sky rendering. |
| 8 | **B-12 Phase 3** | Remove chrslots field, legacy shims, BOT_SLOT_OFFSET. Protocol bump to v22. |
| 9 | **Bot Customizer Integration** | Wire into lobby flow (D3R-8 already coded). |
| 10 | **D5: Settings/Graphics/QoL** | FOV slider, resolution, audio volumes. |

---

## Pause Menu UX (S26 feedback)

| Issue | What Mike Wants |
|-------|----------------|
| End Game confirm/cancel too small | Separate overlay dialog. B cancels to pause menu. |
| Settings B-button exits to main menu | Should back out one level only |
| OG Paused text behind ImGui (B-15) | Suppress legacy pause rendering. Low priority. |
| Scroll-hidden buttons | Prefer docked/always-visible, minimize scrolling |

---

## Backlog

- Systemic bug audit: SP-1 remaining files (activemenu.c, player.c, endscreen.c, menu.c)
- S46b: Full asset catalog enumeration
- ~~Update tab UX: button sizing~~ **DONE S49** -- CalcTextSize-based buttons, per-row Download/Rollback, staged release "Switch" support. Version policy design still pending.
- ~~Version baking~~ **DONE S49** -- always-clean builds, version from UI boxes, -DVERSION_SEM_* flags injected every build
- **Network benchmark → dynamic player cap**: Measure bandwidth/latency at server start, call `hubSetMaxSlots()` to lower `g_NetMaxClients` below 32. Do not hardcode player counts.
- TODO-1: SDL2/zlib still DLL (low priority)
