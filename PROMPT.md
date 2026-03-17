# Perfect Dark Mike — Quick-Start Prompt

Copy everything below this line into a new chat to get up and running immediately.

---

## Project Overview

You are working on `perfect_dark-mike`, a PC-only port of Perfect Dark (N64) that merges two forks:
- **perfect_dark-netplay**: Online multiplayer via ENet (server-authoritative bots, co-op, reconnect, etc.)
- **perfect_dark-AllInOneMods**: Mod content (GoldenEye X, Kakariko Village, Dark Noon, Goldfinger 64, extra characters/stages)

The project lives at the root of this repository. It is a C/C++ codebase built with CMake + MinGW/GCC on Windows.

## Critical File: context.md

**READ `context.md` IN THE PROJECT ROOT BEFORE DOING ANYTHING.** It contains:
- All completed work (network replication phases 1-8, N64 strip, character screen redesign, etc.)
- All known bugs and their status
- Pending work items with specific file paths and line numbers
- Full architecture notes (netcode, damage authority, message types, etc.)
- Future roadmap (mod manager, NAT traversal, dedicated server, etc.)

The context.md is your single source of truth. Update it as you work.

## Directory Structure

```
perfect_dark-mike/              # The working project (PC-only merged port)
  context.md                    # MASTER CONTEXT — read first, update always
  PROMPT.md                     # This file (quick-start prompt)
  CMakeLists.txt                # Build system (CMake, MinGW/GCC, Windows target)
  src/
    game/                       # Game logic (AI, player, menus, multiplayer, etc.)
    include/                    # Headers (types.h, constants.h, data.h, lang.h)
    lib/                        # Engine libraries (audio, animation, collision, etc.)
  port/
    src/                        # PC port layer (video, audio, input, filesystem, config)
      net/                      # Netplay code (net.c, netmsg.c, netmenu.c, netbuf.c)
      mod.c                     # Mod runtime loader
      fs.c                      # Filesystem + mod directory resolution
      main.c                    # Entry point, config registration
      optionsmenu.c             # PC-specific menu options (jump height, FOV, etc.)
    include/                    # Port headers
  build/
    mods/                       # Mod data directories (loaded at runtime)
      mod_allinone/             # Main mod pack (extra characters, stages)
      mod_gex/                  # GoldenEye X maps/characters
      mod_kakariko/             # Kakariko Village map
      mod_dark_noon/            # Dark Noon map
      mod_goldfinger_64/        # Goldfinger 64 maps/characters
  include/PR/                   # N64 SDK headers (mostly stubs for PC)
  cmake/                        # CMake modules (FindSDL2.cmake, etc.)

perfect_dark-netplay/           # Reference: unmodified netplay fork
  perfect_dark-port-net/        # The actual source (compare against mike for diffs)

perfect_dark-AllInOneMods/      # Reference: unmodified AllInOneMods fork
  perfect_dark-allinone-latest/ # The actual source
```

## Build System

- **CMake + MinGW/GCC on Windows** (user builds on their Windows machine, not in sandbox)
- `cmake -B build && cmake --build build`
- Output: `build/pd.x86_64.exe`
- Dependencies: SDL2, zlib, OpenGL (linked in CMakeLists.txt)
- N64 platform has been fully stripped — zero `PLATFORM_N64` references remain

## Key Architecture Concepts

- **`g_NetMode`**: `NETMODE_NONE(0)`, `NETMODE_SERVER(1)`, `NETMODE_CLIENT(2)`
- **Server-authoritative**: All damage, bot AI, game state runs on server only. Clients receive updates.
- **Bots**: `chr->aibot != NULL`, AI runs in `botTickUnpaused` (server only), weapon models synced to clients
- **Protocol version**: 18 (defined in `port/include/net/net.h`)
- **Mod system**: Runtime loader reads `modconfig.txt` from mod directories. Bodies/heads/stages are in static arrays (`g_MpBodies[]`, `g_MpHeads[]`, etc. in `src/game/mplayer/mplayer.c`)
- **Character select**: Uses `MENUITEMTYPE_LIST` for scrollable body selection + `MENUITEMTYPE_CAROUSEL` for head. 3D preview renders alongside.

## How to Use Reference Versions

When investigating bugs or verifying correctness, compare mike's files against the reference versions:
```
# Diff a specific file against the netplay reference:
diff src/game/somefile.c ../perfect_dark-netplay/perfect_dark-port-net/src/game/somefile.c

# Diff against the AllInOneMods reference:
diff src/game/somefile.c ../perfect_dark-AllInOneMods/perfect_dark-allinone-latest/src/game/somefile.c
```

## Current Status (as of 2026-03-16)

The build compiles and runs. First multiplayer test revealed runtime issues — see the "Session: 2026-03-16" section at the bottom of `context.md` for:
- Completed fixes (libgcc DLL, bot weapon table, jump config, mod dir defaults, libwinpthread static link, character screen layout)
- Known issues still being investigated (end-game crash, Skedar/Dr Carroll model linking)
- Pending work items with specific file paths

## Rules of Engagement

1. **Always read `context.md` first** — it has everything you need
2. **Always update `context.md`** when you complete work or discover new issues
3. **Compare against reference versions** when investigating N64 strip damage or mod integration issues
4. **Don't build in the sandbox** — the user builds on their Windows machine. Make code changes, explain what to test.
5. **Be thorough** — missing functionality causes runtime crashes. When fixing one thing, check for related issues nearby.
