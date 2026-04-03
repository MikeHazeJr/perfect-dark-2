# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Recently Completed (S130–S131 — 2026-04-02/03)

| Item | Status |
|------|--------|
| **Catalog Universality Migration Phases A–G** | **DONE (S119–S130)** — wire protocol v27, all net_hash removed, SAVE-COMPAT stripped, catalog-ID-native data model, server manifest model, menu stack arch, spawn hardening. Playtest verification pending. |
| **Comprehensive bug audit** (19 findings) | **DONE (S130)** — 4 critical/high fixed immediately. See `audit-comprehensive-bugs.md`. |
| **Systemic sweep 1: sprintf→snprintf** (344 sites, 36 files) | **DONE (S131)** |
| **Systemic sweep 2: network array bounds** | **DONE (S131)** — one unguarded site fixed in netmsgSvcAuthRead (id/maxclients). |
| **Systemic sweep 3: fread/fwrite, strcpy→strncpy, realloc NULL** | **DONE (S131)** — B-77/B-87/B-88/B-89/B-85 all fixed. |
| **v0.0.25 released as pre-release** | **DONE** |

---

## Phase G — Playtest Verification Pending

All code is complete. These items need in-game confirmation.

**Success criteria**: zero CATALOG-ASSERT in logs, zero type=16, all MP game modes run to completion with bots, menu transitions clean.

### Known Playtest Issues (current codebase)

| Issue | Severity | Notes |
|-------|----------|-------|
| End match → lobby transition broken | HIGH | Can't return to lobby cleanly after match ends |
| Post-match menus janky (buttons non-interactive) | HIGH | Mouse/input state issue after match end |
| Killfeed only shows player kills | MED | Bot kills not appearing in killfeed |
| Some maps don't spawn enemies | MED | Likely navmesh/pad coverage gaps |
| Room/menu navigation janky | MED | Back/Esc behavior inconsistent |
| Settings text overlaps tabs | MED | Relative positioning needed in settings screen |
| Scroll indicators too small / scrollbox-in-scrollbox UX | LOW | Scrollable lists hard to navigate |
| B-19: Bot spawn stacking on Skedar Ruins | MED | Partial fix (S125 F.1 anti-repeat) — needs Skedar-specific test |
| B-21: Menu double-press / hierarchy | MED | Likely fixed Phase E (S124) — needs playtest |
| B-60: Stray 'g'+'s' behind Video/Audio tabs | LOW | Visual glitch in Settings |

---

## Open Bug Fixes — Tier 2 (Fix Before Public Release)

| Bug | Description | File | Effort |
|-----|-------------|------|--------|
| **B-78** | Chat rebroadcast without rate limiting — DoS amplification | netmsg.c | S |
| **B-79** | Chunk ordering ignored in mod distribution — silent data corruption | netdistrib.c | M |
| **B-80** | archive_bytes not validated at BEGIN time (companion to B-74) | netdistrib.c | XS |
| **B-81** | JSON tokenizer unbounded recursion on deep nesting — crafted save crash | savefile.c | S–M |

---

## Open Bug Fixes — Tier 3 (Quality Pass)

| Bug | Description | File | Effort |
|-----|-------------|------|--------|
| **B-72** | SVC_LOBBY_STATE raw stagenum (display-only, LOW priority) | netmsg.c | S |
| **B-82** | Audio sample rate 22020 Hz (should be 22050?) | audio.c | S |
| **B-83** | Incomplete shutdown sequence on quit | main.c | M |
| **B-84** | Dead `tmp[1024]` variable in chat handler | netmsg.c | XS |
| **B-86** | enet_peer_send return value unchecked | netdistrib.c | XS |

---

## D3R Backlog

| Item | Status |
|------|--------|
| **D3R-7**: Modding Hub — 6 files | OPEN — needs build + playtest |
| **D3R-8**: Bot customizer Advanced toggle | OPEN |
| **Bot name dictionary** | OPEN — replace random name generator with word-list dictionary |

---

## NEXT UP: Phase D5 — Settings, Graphics & QoL

**Goal**: FOV slider (60–110°), graphics settings (resolution, fullscreen, VSync), rebindable controls. Audio: Music / Gameplay / UI volume sliders with independent buses, master volume.
**Why first**: High bang-for-buck. Infrastructure (video.c, audio.c, input.c) already exists. Makes it feel like a real PC game.
**Plan file**: [d5-settings-plan.md](d5-settings-plan.md)

---

## Backlog (priority order)

| Phase | Description | Status |
|-------|-------------|--------|
| **D13** | Update System — GitHub Releases API, SHA-256, self-replace | Code written (S50), needs libcurl + build test |
| **D14a** | Counter-Operative Mode — NPC possession mechanic | PLANNED |
| **D15** | Map Editor, Character Creator, Skin System | PLANNED |
| **D16** | Master Server / Server Pool | PLANNED (after content tools) |
| **R-2 through R-5** | Room Architecture — demand-driven rooms, protocol | R-1 done, R-2–R-5 planned |
| **L-series** | Lobby/Room UX — social lobby, room create/join, interior | Depends on R-2/R-3 |
| **B-12 Phase 3** | Remove chrslots — dynamic participant system | Phase 1 coded (S26), next protocol bump |
