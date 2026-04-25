# AI Session Quick Start -- Perfect Dark 2

> **Read this file FIRST on every cold start.** After this, you can contribute productively.
> For deep dives, follow links to domain files. Updated: 2026-04-14 (S258 — session log split; see [INDEX.md](INDEX.md)).

---

## 1. What Is This Project?

**Perfect Dark 2** is a PC port of Perfect Dark (N64 FPS, Rare 2000), evolved into a modding platform with networked multiplayer, an asset catalog, a unified input system, and an ImGui-only menu system.

- **Developer**: Mike (sole dev, game director). Builds on Windows via MSYS2/MinGW. AI writes code, Mike compiles and tests.
- **Language**: C11 (game code in `src/`), C++ (port/renderer in `port/`). No C++ in `src/game/` or `src/lib/`.
- **Build**: CMake + MinGW/GCC. `devtools/dev-window.ps1` (GUI) or `devtools/build-headless.ps1` (headless).
- **Rendering**: fast3d GBI translator (N64 display lists -> OpenGL), Dear ImGui v1.91.8 overlay.
- **Networking**: ENet (UDP), server-authoritative. Protocol **v32**. 60Hz tick.
- **Platform**: Windows x86_64 only. No N64, no Switch. Modern hardware.
- **Repo**: GitHub: `https://github.com/MikeHazeJr/perfect-dark-2`
- **Branch**: `dev` (default). `stable` for releases only. No `main`.

---

## 2. The Core Architectural Rule

**The Asset Catalog is the Single Source of Truth.** Every asset reference uses catalog ID strings in `"namespace:readable_name"` format (e.g., `"base:dark_combat"`, `"base:arena_felicity"`). No integer identity at boundaries (wire, save, public API). No net_hash. No exceptions.

If you're about to write code that uses an integer asset index at any boundary, **stop**. Use the catalog ID string.

---

## 3. Session Protocol

Before writing ANY code:
1. Read this file (done)
2. Read `context/constraints.md` -- active constraints to respect, removed constraints to NOT work around
3. Read `context/session-log.md` (last few sessions; older tiers in `context/_archive/`) -- what was done, what's next
4. Read `context/tasks-current.md` -- active punch list
5. Summarize to Mike: where we are, what's next, any blockers
6. Confirm direction before starting

---

## 4. Critical Constraints (Abridged)

Full list in `context/constraints.md`.

| Constraint | Detail |
|-----------|--------|
| **Catalog ID everywhere** | `char[64]` strings at ALL boundaries. No integer identity on wire/save/API. |
| **`bool` is `s32`** | Defined in `types.h`. Never include `<stdbool.h>` in game code. |
| **C11 game / C++ port** | No C++ in `src/game/` or `src/lib/`. |
| **ImGui is sole menu system** | All menus in `pdgui_menu_*.cpp`. Legacy `menuPush`/`menuPop` retained as plumbing only. P10 D5.7 complete -- zero native rendering paths remain. |
| **Input context stack** | Mouse capture owned by `inputctx.c`. No direct `SDL_SetRelativeMouseMode` calls from menus. |
| **Action map system** | All input via `actionPressed()`/`actionHeld()`/`actionValue()`. No CK_* constants (deleted). |
| **No raw IP in UI** | 4-word sentence connect codes only. |
| **Server is not a player** | `g_NetLocalClient = NULL` on dedicated server. Always NULL-guard. |
| **Protocol v32** | All wire fields use catalog ID strings or session refs. net_hash is dead. |
| **MAX_MPCHRS=36** | MAX_PLAYERS=4 (local splitscreen), MAX_BOTS=32. chrslots is u64. |
| **Clean builds only** | Every build deletes build dirs first. No incremental. |

**Removed constraints** (do NOT work around these): N64 platform guards, 4MB memory, N64 dead code, micro-optimizations, host-based MP, N64 collision workarounds, 4-player bot limit, net_hash wire format, legacy N64 menu system, integer-native match config, g_ModNum, modconfig.txt, shadow arrays, fileSlots 2D array.

---

## 5. Architecture

```
src/                    Original decompiled game code (C)
  game/                 Game logic: player, props, menus, multiplayer
  lib/                  Engine: collision, capsule physics, model loading
  include/              Game headers (types.h, constants.h, bss.h, data.h)
port/                   PC port additions (C/C++)
  fast3d/               Rendering: GBI translator, ImGui backend + all pdgui_menu_*.cpp
  src/                  Port source: networking, input, main loop, actionmap
    net/                ENet: net.c, netmsg.c, netmanifest.c, netdistrib.c, sessioncatalog.c
  include/              Port headers (pdgui.h, net/*.h, actionmap.h, inputctx.h)
include/PR/             N64 SDK headers (ultratypes.h, gbi.h)
context/                Project context encyclopedia (THIS directory)
mods/                   Mod content (component-based, each asset = own folder + .ini)
```

**Key files you'll touch most**:
- `port/src/net/netmsg.c` -- Wire protocol (SVC_* server->client, CLC_* client->server)
- `port/src/actionmap.cpp` -- Unified input system (action maps, bindings, device detection)
- `port/fast3d/pdgui_menu_*.cpp` -- ImGui menu screens
- `port/src/assetcatalog.c` + `assetcatalog_base.c` -- Catalog core + base game registration
- `src/game/mplayer/` -- Multiplayer game logic, bot management

---

## 6. Current State (v0.0.56, S185)

### What's Done
- **M0-M2 COMPLETE**: Catalog ID migration (all asset types), action map input system, campaign flow, combat sim flow
- **P1-P10 COMPLETE**: ImGui is sole menu system. All legacy native rendering removed. 59/120 screens ported.
- **Deep Audit COMPLETE**: 47 bugs fixed (5 critical security, 7 high, 10 medium, 9 low) + dead code removal
- **Infrastructure COMPLETE**: D1 (N64 strip), D3R (component mods), D8 (NAT), D9 (dedicated server), catalog universality A-G, match startup pipeline A-F
- **185+ sessions** of development. 5 systemic sweeps.

### What's Next (v0.1.0 prep)
1. D5 Phase 3 -- Remaining 61 menu screen ports
2. D5 Phase 4 -- Theme system (auto-extract textures, mod themes)
3. B-112 root cause -- Chr pointer corruption in 31-bot matches
4. D13 build test -- Update system (code written, needs libcurl static link)
5. Build verification + QC pass

### Open Bugs (High Priority)
- **B-112**: Chr pointer corruption in 31-bot matches (guards in place, root cause unknown)
- **B-118**: CI intro cutscene crash (56 models missed by SP manifest pre-scan)
- **B-78**: Chat rebroadcast DoS amplification
- **B-81**: JSON tokenizer unbounded recursion

---

## 7. Build & Test

**Mike builds.** AI does NOT compile. AI writes code, Mike verifies.

```powershell
# Headless build (for AI build-check):
powershell -File devtools/build-headless.ps1 -Target all
```

```bash
# Manual build:
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -G Ninja -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
ninja -C Build pd pd-server
```

**Outputs**: `Build/PerfectDark.exe` (~43-46 MB), `Build/PerfectDarkServer.exe` (~21-23 MB).

---

## 8. Working Style

- **Context files are code.** A code change without a context update is incomplete.
- **Update as you go.** Bug found -> bugs.md immediately. Decision made -> constraints.md immediately.
- **Rabbit hole protocol**: If mid-task you're going deeper than expected, stop and present options.
- **Proactive saves**: If conversation is long, suggest saving state to session-log.md.
- **Propagation check**: After fixing a bug, check if the same pattern exists elsewhere.

---

## 9. Context File Index

| File | Purpose |
|------|---------|
| [constraints.md](constraints.md) | Active/removed constraints -- CHECK BEFORE EVERY CHANGE |
| [session-log.md](session-log.md) | Recent session history |
| [tasks-current.md](tasks-current.md) | Active punch list |
| [roadmap.md](roadmap.md) | Milestone status + what remains for v0.1.0 |
| [bugs.md](bugs.md) | Open bug tracker |
| [systemic-bugs.md](systemic-bugs.md) | Architectural bug patterns |
| [infrastructure.md](infrastructure.md) | D-phase execution status |
| [roadmap.md](roadmap.md) → "Release Milestones" | Release version targets (v0.1.0 → v1.0.0) |
| [build.md](build.md) | Build system details |
| [networking.md](networking.md) | ENet protocol reference |
| [collision.md](collision.md) | Capsule sweep / physics |
| [imgui.md](imgui.md) | ImGui integration details |
| [component-mod-architecture.md](component-mod-architecture.md) | Mod system design |

Load domain files ONLY when the current task requires them.
