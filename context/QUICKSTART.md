# AI Session Quick Start — Perfect Dark 2

> **Read this file FIRST on every cold start.** It is the single onboarding document.
> After reading this, you should be able to contribute productively without re-reading the entire context system.
> For deep dives, follow the links to domain files. Updated: 2026-04-06 (S157+).

---

## 1. What Is This Project?

**Perfect Dark 2** is a PC port of Perfect Dark (N64 FPS, Rare 2000). It has evolved beyond a straight port into a platform: networked multiplayer, full mod system, asset catalog, ImGui menu replacement, and a long-term vision toward a standalone open-source game engine.

- **Developer**: Mike (sole dev, game director). Builds on Windows via MSYS2/MinGW. AI writes code, Mike compiles and tests in-game.
- **Language**: C11 (game code in `src/`), C++ (port/renderer in `port/`). No C++ in `src/game/` or `src/lib/`.
- **Build**: CMake + MinGW/GCC. Two build tools — `devtools/dev-window.ps1` (GUI) and `devtools/build-headless.ps1` (headless/CI). Both must produce identical builds.
- **Rendering**: fast3d GBI translator (N64 display lists → OpenGL), Dear ImGui v1.91.8 overlay.
- **Networking**: ENet (UDP), server-authoritative. Protocol **v31**. 60Hz tick.
- **Platform**: Windows x86_64 only. No N64, no Switch. Modern hardware — no legacy constraints.
- **Repo**: `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike` — GitHub: `https://github.com/MikeHazeJr/perfect-dark-2`
- **Branch**: `dev` (default). `stable` for releases only. No `main`.

---

## 2. The One Rule That Governs Everything

**The Asset Catalog is the Single Source of Truth.** Every asset reference — bodies, heads, weapons, stages, models, textures, sounds, animations, game modes, language banks, props, HUD elements — uses catalog ID strings in `"namespace:readable_name"` format (e.g., `"base:dark_combat"`, `"base:arena_felicity"`). No exceptions. No integer identity at boundaries. No net_hash. No carve-outs.

This is not a guideline. It is the architectural foundation. Every bug from the April 1st playtest traced back to incomplete catalog adoption. The game director's binding decisions:

- **D-1 FULL**: Every asset type in scope.
- **D-2 FULL**: Model numbers — full migration.
- **D-3 FULL**: `mainChangeToStage` refactored to catalog ID.
- **Zero half measures**: Catalog is sole source of truth for identity AND state.

If you're about to write code that uses an integer asset index at any boundary (wire, save, public API, config struct), **stop**. Use the catalog ID string.

---

## 3. Session Protocol — MANDATORY

Before writing ANY code:

1. Read this file (you're doing it now)
2. Read `context/constraints.md` — active constraints you must respect, removed constraints you must NOT work around
3. Read `context/session-log.md` (last 2–3 sessions) — what was done, what's next
4. Read `context/tasks-current.md` — active punch list
5. **Summarize to Mike**: where we are, what's next, any blockers
6. **Present all active work fronts** — don't assume the last task is the next task
7. **Confirm direction** before starting work

Load domain files (collision.md, networking.md, etc.) ONLY when the current task requires them.

---

## 4. Critical Constraints (Abridged)

These are the most commonly relevant. Full list in `context/constraints.md`.

| Constraint | Detail |
|-----------|--------|
| **Catalog ID everywhere** | `char[64]` strings at ALL boundaries. No integer identity on wire/save/API. |
| **`bool` is `s32`** | Defined in `types.h`. **Never include `<stdbool.h>`** in game code. |
| **C11 game / C++ port** | No C++ in `src/game/` or `src/lib/`. |
| **Legacy N64 menus are DEAD** | `menuPush`/`menuPop`/`menuTick`/`menumgr.c` are deprecated trash. Do NOT fix, extend, integrate with, or reference them. ALL menu work targets `pdgui_menu_*.cpp` (ImGui). The legacy system will be stripped entirely. |
| **ImGui is sole menu system** | All menus in `pdgui_menu_*.cpp`. Push/pop via input context stack. |
| **No raw IP in UI** | 4-word sentence connect codes only. |
| **Server is ROM-agnostic** | Server has no ROM. `g_NumStages == 0` is normal. Server gets everything from host manifest. Don't log warnings about missing ROM data on server. |
| **Server is not a player** | `g_NetLocalClient = NULL` on dedicated server. Always NULL-guard. |
| **Protocol v31** | All wire fields use catalog ID strings or session refs. net_hash is dead. |
| **Zero-config networking** | Players must NEVER need to touch router settings, port forwarding, or UPnP. STUN + hole punch is the solution. All client joins use `netStartClientWithHolePunch()`, never raw `netStartClient()`. |
| **MAX_MPCHRS=36** | MAX_PLAYERS=4 (local), MAX_BOTS=32. chrslots is u64. |
| **30 agent save slots** | Hardcoded in filelist struct. Cannot change without save migration. |
| **Zero DLL dependencies** | Executables must be fully static — no runtime DLLs. Release is: exes + data/ (README only) + mods/ (self-generating). libcurl static linking is currently broken (CMakeLists.txt copies ~15 DLLs as workaround). Needs fix. |
| **Clean builds only** | Every build deletes build dirs first. No incremental. |
| **No worktrees** | Work in main copy only. Never create worktrees. Prune stale ones. |
| **Self-generating content** | Base UI mods (`mods/base-ui/`, `mods/pd-modern-ui/`) create themselves at runtime if missing. Don't rely on the release zip bundling them. Textures auto-extract from ROM on first launch. |

**Removed constraints** (do NOT work around these — they're gone): N64 platform guards, 4MB memory mode, N64 dead code, micro-optimizations, host-based MP, N64 collision workarounds, 4-player bot limit, net_hash wire format, legacy N64 menu system, integer-native match config, g_ModNum, modconfig.txt, shadow arrays, fileSlots 2D array, UPnP-only networking.

---

## 5. Architecture at a Glance

```
src/                    Original decompiled game code (C)
  game/                 Game logic: player, props, menus, multiplayer
  lib/                  Engine: collision, capsule physics, model loading
  include/              Game headers
port/                   PC port additions (C/C++)
  fast3d/               Rendering: GBI translator, ImGui backend, PD-authentic styling
  src/                  Port source: networking, input, main loop
    net/                ENet integration, message handlers, manifest, session catalog
  include/              Port headers
include/PR/             N64 SDK headers (ultratypes.h, gbi.h)
context/                Project context encyclopedia (THIS directory)
devtools/               Build tools, cleanup scripts, dev window
mods/                   Mod content (component-based, each asset = own folder + .ini)
tools/                  Log parser, utilities
```

**Key subsystems you'll touch most**:
- `port/src/net/netmsg.c` — Wire protocol messages (SVC_* server→client, CLC_* client→server)
- `port/src/net/netmanifest.c` — Asset manifest building/validation
- `port/src/assetcatalog.c` + `assetcatalog_base.c` — Catalog core + base game registration
- `port/fast3d/pdgui_menu_*.cpp` — ImGui menu screens
- `port/fast3d/pdgui_style.cpp` / `pdgui_theme.cpp` — PD-authentic visual styling
- `src/game/mplayer/` — Multiplayer game logic, bot management, match setup

---

## 6. Current State (v0.0.50, S178)

### What's Done
- **M0.1 Catalog Signature Migration — COMPLETE** (S167–S178): All 5 sub-phases done. Stages (a), bodies/heads (b), weapons (c), remaining types (d), and catalog as data provider (e). 15 data accessor functions, ROM arrays internalized. Wire protocol v32.
- **Catalog Universality Phases A–G**: Wire protocol fully migrated to catalog ID strings (v27→v32).
- **D5 Phases 1–2 COMPLETE**: Input context stack + controller navigation. Phase 3 in progress (61/120 screens remaining).
- **M1.1–M1.3 COMPLETE**: Campaign mission select, solo mission flow, pause menu + options.
- **M2.1–M2.2 COMPLETE**: Combat sim UI (arenas, weapons, bots, game modes) + match flow (endscreen, B-117 fix, auto-save).
- **Lobby Unification (U-1–U-10)**: Complete. `matchsetup.cpp` retired.
- **178 sessions** of development. 5 systemic sweeps. Comprehensive bug audit.

### What's Active
- **D5 Phase 3 — Full Menu Roster Port**: 61 of 120 screens remaining. Solo pause done (S164), options sub-menu done (S174).
- **M0.2 — Input System Unification**: Spec done (S163). Action maps and rebinding not started.
- **B-112**: Chr pointer corruption in 31-bot matches. Root cause unknown. VEH + guards in place.

### What's Planned (Priority Order per Roadmap)
1. M0.2 Input System Unification (action maps, rebinding)
2. M1.2 Solo Mission Flow completion (briefings, mission complete/failed screens)
3. M2.3 Stats & Progression (persistent stats wiring, achievements)
4. M3 Online Multiplayer (lobby polish, room list, leader election)
5. M4 Mod Platform (browser, creation tools, theme system)
6. M5 Forge (level editor)
7. M6 Polish & Release (collision feel, audio, accessibility, v1.0.0)

---

## 7. Build & Test

**Mike builds.** AI does NOT compile. AI writes code, Mike verifies.

**Headless build** (for AI build-check when needed):
```powershell
powershell -File devtools/build-headless.ps1 -Target all
```

**Build environment** (if running in sandbox/code session):
```bash
export TEMP="C:/Users/mikeh/AppData/Local/Temp"
export TMP="$TEMP"
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -G Ninja -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
ninja -C Build pd pd-server
```

**Expected outputs**: `Build/PerfectDark.exe` (~43-46 MB), `Build/PerfectDarkServer.exe` (~21-23 MB).

**Release assets** (GitHub Releases): Only THREE artifacts in a release — the zip bundle and the two source archives. No bare executables, no `.sha256` files.

| Asset | Include | Why |
|-------|---------|-----|
| `PerfectDark-vX.Y.Z-win64.zip` | YES | Complete distribution — client + server + data + mods in one package |
| Source code (zip) | YES | Auto-generated by GitHub |
| Source code (tar.gz) | YES | Auto-generated by GitHub |
| `PerfectDark.exe` (bare) | NO | Useless without data bundle |
| `PerfectDark.exe.sha256` | NO | No bare exe = no hash needed |
| `PerfectDarkServer.exe` (bare) | NO | Included in the zip already |
| `PerfectDarkServer.exe.sha256` | NO | No bare exe = no hash needed |

**Logs**: Build folder. Use `tools/parse-log.sh --crash` for crash diagnosis, `--summary` for overview.

---

## 8. Context System Rules

Context files are project infrastructure, not optional docs. **A code change without a context update is incomplete.**

| When | Do |
|------|-----|
| Decision made | Update `constraints.md` or domain file **immediately** |
| Bug found | Add to `bugs.md` **immediately** |
| Bug reveals pattern | Add to `systemic-bugs.md` |
| Task completed | Update `tasks-current.md` **immediately** |
| Session ends | Update `session-log.md` with focus, decisions, next steps |

**Do not batch context updates.** If the conversation is cleared mid-task, the next session must pick up from context files alone.

**Canonical location**: `perfect_dark-mike/context/`. If parent-level copies exist, `context/` wins.

---

## 9. Pre-Task Sanity Check

Before starting significant work, run through:

1. **Constraint check**: Does this assume a constraint that's been removed?
2. **Root cause check**: Am I fixing a symptom or the underlying problem?
3. **Scope check**: Is this the simplest approach for modern hardware?
4. **Cascade check**: Will this conflict with things already modernized?
5. **Effort check**: Is this proportional to its importance?

**Init ordering audit**: After any change that adds or moves init/shutdown calls, verify the full boot sequence resolves correctly. Systems must init in dependency order:
1. SDL_Init → 2. inputInit (SDL event watch) → 3. inputCtxInit + push gameplay → 4. pdguiInit (ImGui) → 5. texInit/texReset → 6. pdguiThemeLateInit → 7. game logic.
Frame loop: SDL_PollEvent → pdguiProcessEvent → inputCtxDispatch → game tick → render → inputCtxEndFrame.
Shutdown: reverse order. Document any ordering dependencies discovered.

**Rabbit Hole Protocol**: If you're going deeper than expected — **stop**. Explain, present options, recommend one, let Mike decide.

---

## 10. Working Style & Collaboration

> **Read this carefully.** This section preserves the collaborative spirit built over 160+ sessions. A cold start should feel like a continuation, not a stranger introducing themselves.

**This is a partnership.** Mike is the game director and sole developer. AI is the engineering partner. We say "we" not "you" — the work is collaborative. Mike trusts the AI to make good decisions, take initiative, and push back when something is wrong. He expects creative contribution, not just execution.

- **Mike thinks in systems, phases, and player experience.** He values depth over shortcuts, root cause over patches.
- **No half measures** on systems/architecture. This is a platform foundation.
- **Event-driven over polling.** Don't check every frame for something that changes on specific events.
- **When the fix is clear, just do it** — don't announce intent and wait.
- **Update context as you go**, not as a follow-up.
- **Run a build check** before reporting any code task as complete.
- **Send progress updates** every 5 minutes during long work. Never go silent.
- **Timestamps on every response.**
- **No worktrees.** Work in the main copy. Prune stale ones proactively.
- **Merge verification**: If a code session uses a worktree anyway, verify line counts post-merge (truncation risk).
- **Protect .git internals** — never write to `.git/` directly.
- **Audit everything twice**: Every code change gets verified for function, interoperability, UX, and bugs before reporting done. Use a second session/model for audit when appropriate.
- **Don't touch legacy menus**: The N64 menu system (`menuPush`/`menuPop`/`menuTick`/`menumgr.c`) is deprecated. Don't fix it, extend it, or integrate with it. Build new, strip old.
- **C headers included from C++ need `extern "C"` guards**: If a port header (`port/include/*.h`) declares C functions and gets included from `.cpp` files, it MUST have `#ifdef __cplusplus extern "C" {` guards. Missing guards cause linker errors (name mangling). Check `fs.h`, `config.h` as examples of this fix.
- **Dev window auto-configures git identity**: The dev window sets `user.email` and `user.name` repo-level on startup if missing. No manual git config needed.
- **Release auto-commits**: The dev window's build/release flow auto-commits pending changes before building. If git identity is missing, the pipeline stops with a clear error instead of silently failing.
- **When Mike says "out of curiosity..."** — he's probing the architecture. Answer thoroughly.
- **When Mike says "I refuse to accept..."** — he's setting a binding constraint. Log it.
- **Proactive context saves**: When the conversation gets long, suggest saving state and starting fresh. The context files must be good enough that a new session picks up the rhythm, not just the facts.

---

## 11. Key Domain Files (Load When Needed)

| File | When |
|------|------|
| `constraints.md` | Always (already read at session start) |
| `tasks-current.md` | Always (already read at session start) |
| `session-log.md` | Always (last 2-3 sessions at session start) |
| `plan-catalog-id-migration.md` | Any catalog migration work |
| `networking.md` / `network-system-audit.md` | Netcode work |
| `collision.md` / `movement.md` | Physics/movement work |
| `imgui.md` | Menu/UI work |
| `component-mod-architecture.md` | Mod system work |
| `designs/match-startup-pipeline.md` | Match startup work |
| `designs/d5-visual-layer-plan.md` | D5.0 visual layer work |
| `designs/d5-full-menu-overhaul.md` | D5 master plan — input stack, controller nav, full menu port, themes, planned features |
| `designs/implementation-plan-mods-and-d5.md` | Mod + D5 roadmap |
| `designs/engine-vision-roadmap.md` | Long-term architecture |
| `bugs.md` | Bug reference |
| `systemic-bugs.md` | Architectural bug patterns |
| `qc-tests.md` | In-game test checklist |

---

## 12. Release Milestones

| Version | Goal | Status |
|---------|------|--------|
| **v0.1.0 "Foundation"** | Stable SP + local MP with mod support | Near — needs QC pass |
| **v0.2.0 "Connected"** | Multiplayer with friends, dedicated server | In progress |
| **v0.3.0 "Community"** | Social hub, content sharing | Planned |
| **v0.4.0 "Federation"** | Mesh networking, cross-server | Planned |
| **v0.5.0 "Studio"** | Full mod creation pipeline | Planned |
| **v1.0.0 "Forge"** | Complete creative platform + level editor | Planned |

---

## 13. Index Domain Warning

Three index spaces exist and MUST NOT be confused:

| Domain | Array | Size | Valid Range |
|--------|-------|------|-------------|
| Stage table index (`g_StageIndex`) | `g_Stages[87]` | 87 | 0–86 |
| Solo stage index | `g_SoloStages[21]` / `besttimes[21][3]` | 21 | 0–20 |
| Stagenum | Logical ID (e.g., 0x5e) | N/A | Arbitrary |

Mod stages have stage table indices 61–86 but NO solo stage index. Flowing a stage table index into `g_SoloStages[]` or `besttimes[]` is OOB.

---

## 14. Systemic Bug Patterns (Top 3)

1. **SP-1: MAX_PLAYERS arrays indexed by bot mpindex** — Arrays sized 8 indexed with values 8–31. Bounds-check and SKIP, never modulo-alias.
2. **SP-2: AVOID_UB modulo hack** — `% MAX_PLAYERS` silently corrupts wrong player's state. Bounds-check and skip.
3. **SP-8: prop->chr without NULL check** — PROPTYPE_PLAYER chr can be NULL during transitions. Always guard.

Full list in `systemic-bugs.md`.

---

---

## 15. Collaboration Principles

These aren't rules — they're the foundation of how this project works. Built over 160+ sessions.

1. **Single Source of Truth, everywhere.** Catalog for assets. Context stack for input. Context files for project state. One authority per system, zero parallel paths. Mike will not accept convolusion.

2. **Design before construction.** Read the guideline docs. Understand the architecture. Check the constraints. THEN write code. Research is not wasted time — it prevents wrong assumptions and rework.

3. **Every system should be extensible.** The input context stack makes adding new input contexts trivial. The catalog makes adding new asset types trivial. Build infrastructure that makes the NEXT thing easy.

4. **The game director's decisions are binding.** D-1 FULL, D-2 FULL, D-3 FULL. Zero half measures. When Mike makes a decision, it becomes a constraint. Document it immediately.

5. **Context is code.** Updating context files is equal priority to writing code. A code change without a context update is an incomplete change. If context is cleared right now, the next session must pick up in under a minute.

6. **Fix the class, not the instance.** After fixing a bug, always do a propagation check. Does this same problem exist anywhere else? Systemic fixes over spot patches.

7. **The vision is real.** PD2 → standalone engine → open UGC platform. Every architectural decision should serve this trajectory. Don't build walls that block the path forward.

---

**You are now onboarded. Read `constraints.md`, `session-log.md` (last 2-3), and `tasks-current.md`, then summarize to Mike and confirm direction.**
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  