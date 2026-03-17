# Task Tracker

## Quick Context
Project: Perfect Dark PC port (C11, CMake + MinGW). User (Mike) compiles on Windows — AI cannot.
Read README.md for full project context. Read the relevant system file for whatever task is active below.

## Purpose
Track the current task, its steps, and progress. Updated at each step start/stop to ensure continuity across AI session resets.

---

## Current Task: Capsule Collision System — Extended Testing

### Status: WAITING FOR USER
User confirmed stationary jumping works. Full movement testing requires user at PC.

### Steps

| # | Step | Status | Notes |
|---|------|--------|-------|
| 1 | Implement capsuleSweep() | DONE | 16-step sweep, cdTestVolume at each step |
| 2 | Implement capsuleFindFloor() | DONE | BG floor + binary search prop surfaces |
| 3 | Implement capsuleFindCeiling() | DONE | BG ceiling + binary search upward |
| 4 | Create capsule.h header | DONE | API, struct capsulecast, constants |
| 5 | Add capsule.c to CMakeLists.txt | DONE | Line 279, before collision.c |
| 6 | Integrate floor detection in bondwalk.c | DONE | ~line 994-1028, replaces prop surface hack |
| 7 | Integrate pre-move sweep in bondwalk.c | DONE | ~line 1204-1248, clamps vertical movement |
| 8 | Integrate ceiling detection in bondwalk.c | DONE | ~line 1259-1302, replaces legacy ceiling-only |
| 9 | Test stationary jumping | DONE | User confirmed: "jump seems to work much better" |
| 10 | Test movement during jumping | WAITING | User needs to be at PC |
| 11 | Test standing on props (desks, crates) | WAITING | User needs to test |
| 12 | Test jumping near ceilings/overhangs | WAITING | User needs to test |
| 13 | Test collisions with moving objects | WAITING | cdTestVolume tests CDTYPE_ALL, should work |

### Blockers
- User must compile and test on Windows PC (cannot compile from Linux VM)

---

## Previous Task: Context Restructuring

### Status: COMPLETE

### Steps

| # | Step | Status | Notes |
|---|------|--------|-------|
| 1 | Create context/ folder | DONE | |
| 2 | Create context/README.md | DONE | Index, architectural facts, usage guide |
| 3 | Create context/collision.md | DONE | Capsule system, legacy system, integration points |
| 4 | Create context/movement.md | DONE | Jump physics, vertical pipeline, ground/ceiling detection |
| 5 | Create context/networking.md | DONE | All phases 1-10 + C1-C12, message tables, damage authority |
| 6 | Create context/build.md | DONE | CMake, MSYS2/MinGW, static linking, mod loading |
| 7 | Create context/roadmap.md | DONE | D1-D8 phases, dependencies, TODOs |
| 8 | Create context/tasks.md | DONE | This file |

---

## Session Fixes Log (Post-D1 Runtime)

| Fix | File | Issue | Status |
|-----|------|-------|--------|
| FIX-1 | CMakeLists.txt | Missing -static-libgcc | APPLIED, TESTED OK |
| FIX-2 | botinv.c | Bot weapon table misaligned | APPLIED, NEEDS TESTING |
| FIX-3 | main.c | Jump height config not registered | APPLIED, NEEDS TESTING |
| FIX-4 | fs.c | Default mod directories | APPLIED, NEEDS TESTING |
| FIX-5 | CMakeLists.txt | Static link libwinpthread | APPLIED, TESTED OK |
| FIX-6 | setup.c | Character screen layout overlap | APPLIED, NEEDS TESTING |
| FIX-7 | mplayer.c | Missing g_NetMode guard in mpEndMatch | APPLIED, NEEDS TESTING |
| FIX-8 | mplayer.c | Missing g_NetMode guard in mpHandicapToDamageScale | APPLIED, NEEDS TESTING |
| FIX-9 | mplayer.c | Missing server chrslots setup in mpStartMatch | APPLIED, NEEDS TESTING |
| FIX-10 | menutick.c | Misplaced chrslots loop + missing condition | APPLIED, NEEDS TESTING |
| FIX-11 | ingame.c | Missing save dialog guard | APPLIED, NEEDS TESTING |
| FIX-12 | netmenu.c | Join screen button overlap | APPLIED, NEEDS TESTING |

### Additional Session Fixes

| Fix | File | Issue | Status |
|-----|------|-------|--------|
| FIX-7b | setup.c | Dr Carroll head crash protection | APPLIED, NEEDS TESTING |
| FIX-8b | menu.c, player.c | Null model protection | APPLIED, NEEDS TESTING |
| FIX-9b | setup.c | Simulant spawning debug logging | APPLIED, NEEDS TESTING |
| FIX-10b | setup.c | Character select screen redesign | APPLIED, NEEDS TESTING |
| FIX-11b | ingame.c | Pause menu scrolling | APPLIED, NEEDS TESTING |

---

## Known Issues (Outstanding)

| Issue | Description | Likely Fix | Status |
|-------|-------------|------------|--------|
| Bots not moving/respawning | Multiple root causes | FIX-2 + FIX-8 + FIX-9 | NEEDS TESTING |
| End-game crash | Crash at match end | FIX-7 + FIX-2 + FIX-9 | NEEDS TESTING |
| Jump not working (non-capsule) | Pre-capsule issue | FIX-3 + capsule system | Capsule approach working |
| Character screen layout | Overlap of list and preview | FIX-6 + FIX-10b | NEEDS TESTING |
| Invisible character | Missing model file | FIX-8b (crash prevented) | Model availability TBD |
| Simulants not spawning | chrslots bits or allocation | FIX-9b logging added | NEEDS LOG OUTPUT |
| Blue geometry at Paradox | Visual bug at center pit | Not investigated | LOW PRIORITY |

---

## How To Use This File
1. **Starting a new task**: Add a new "Current Task" section at the top with steps
2. **At each step start**: Update the step status to IN PROGRESS
3. **At each step completion**: Update to DONE with notes
4. **On blockers**: Note the blocker and what's needed to unblock
5. **On session reset**: Read this file FIRST to know where we left off
6. **Move completed tasks**: Shift finished tasks down under "Previous Task" sections
