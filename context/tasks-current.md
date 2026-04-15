# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. Completed work lives in `session-log.md`,
> `bugs.md`, `daily-logs/`, or `_archive/tasks-archive.md`.
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Open — 2026-04-13+

> Carried forward from the 2026-04-13 stabilization drop. Forensic detail in
> `scratch/archive/2026-04-13/`.

### Playtest verification of the 2026-04-13 drop

Mike to confirm each on next build. Bug/feature → commit on `dev`:

- **Issue 7** room-settings sync (`287b0bc4`) — non-leader sees leader's bot/player count / arena / timelimit real-time.
- **B-140 Issue B** two-panel Select Tunes (`287b0bc4`) — add/remove mod tracks, hover preview, leader's playlist syncs to room.
- **Issue 2/8** theme rescan (`287b0bc4`) — newly-enabled mod themes appear in Settings → Video without restart.
- **Mod Apply follow-up (S260, uncommitted)** — verify Apply now rebuilds enabled mod manifests/audio before title return (themes + mod songs populate; Select Tunes add/remove works) and MP dialog close no longer steals main-menu input context.
- **S263 systemic pipeline fixes** — verify: Deep Sea coop transition clamps stage index; Counter-Op selected anti player is honored online (not forced to slot 1); co-op/anti launch has no double-transition side effects; team-mode endscreen rankings show player rows (no placeholder '?' entries).
- **B-142** false kills (`4d1e13c1`) — fresh 32-bot Chicago match, idle 30 s, pause → kill counter 0/0.
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
| **Manifest gap follow-ups** | See `scratch/archive/2026-04-13/manifest-gap-report-2026-04-13.md`. (1) Title/menu stage has no manifest — Skin Editor mod chars silently missing; fix: `manifestBuildForMenu()`. (2) SP pre-scan timing — split `manifestBuildMission()` into pre/post-load phases (partially mitigated by `manifestSPRescanSetup`). (3) Cutscene cinema models never in manifest — safety net only. |

### Audit follow-ups (S262, uncommitted)

- **Campaign end-path OOB guard** — `src/game/menutick.c`: Deep Sea next-stage path increments `g_MissionConfig.stageindex` and indexes `g_SoloStages[]` without `NUM_SOLOSTAGES` clamp. Align with guards in `endscreen.c`.
- **Endscreen menu index safety (SP-1)** — `src/game/endscreen.c`: guard `g_MpPlayerNum` before `g_Menus[g_MpPlayerNum]` writes in `endscreenPushCoop/Anti`.
- **Team rankings data/UI mismatch** — `port/fast3d/pdgui_menu_endscreen.cpp`: `buildRankings` consumes `mpGetTeamRankings()` rows with `mpchr=NULL`; use per-player rankings + team sort for display consistency.
- **Room-scope teardown hygiene (SP-14 follow-up)** — `port/src/net/net.c`: review/reset lifecycle for `g_NetMatchRoomId` at stage end.

---

## Build / Release

| Item | Status | Detail |
|------|--------|--------|
| **Git sync before Build/Release (dev-window-v2)** | DONE (S257) | `Invoke-GitSyncBeforeBuild` + `release.ps1` index-safe commit before `pull --rebase`. See `session-log` S257, `CRITICAL-PROCEDURES.md`. |
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
| **FIX-B.1** | Deep manifest scanner (cinematics + AI scripts) | OPEN — `netmanifest.c`, `setup.c` |

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
