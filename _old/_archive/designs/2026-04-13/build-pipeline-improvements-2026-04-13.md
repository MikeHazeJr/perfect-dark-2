# Build Pipeline Improvements Plan

> **Date**: 2026-04-13
> **Author**: AI (S221 investigation session)
> **Status**: IMPLEMENTED (S224, 2026-04-13) -- see implementation notes below

---

## Implementation Summary (S224 — 2026-04-13)

**Implemented:**
- Phase 0: ccache 4.12.3 installed (`pacman -S mingw-w64-x86_64-ccache`). 100% cache-hit rate on second build.
- Phase 1: Ninja generator in all three scripts (`build-headless.ps1`, `dev-window-v2.ps1`, `release.ps1`). Unix Makefiles + make.exe plumbing removed.
- Phase 1: Unified `Build/` directory — pd and pd-server share one CMake dir (no more double ImGui compile).
- Phase 1: ccache via `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`.
- Phase 2: PCH for C core-4 headers (`types.h`, `ultra64.h`, `data.h`, `constants.h`) via `target_precompile_headers`. No conflict with `-include "versioninfo.h"` force-include.
- Smart clean-build detection in `build-headless.ps1` (heuristics: CMakeCache missing, generator/compiler/branch changed, or explicit `-Clean`). State in `Build/.last_build_state.json`.
- Auto-commit now opt-in in `build-headless.ps1` (pass `-AutoCommit`; default OFF).
- `release.ps1` always commits+pushes before build and uses Ninja + unified `Build/`.

**Rolled back:**
- mold linker: `-fuse-ld=mold` fails on MinGW. GCC's `collect2.exe` looks for `ld.mold.exe` which doesn't exist in MSYS2 (only `mold.exe`). Rolled back to GNU ld. mold package remains installed. Link time is ~1.5s (8% of build) — low priority.

**Skipped per scope:**
- `-O1` vs `-Og`: Mike explicitly chose to keep `-Og` for debuggability.
- DLL copy commands: left for separate static-link session.
- Unity build, sccache, lld: not attempted.

## Measured Build Times (S224 — after implementation)

| Metric | Time | Notes |
|--------|------|-------|
| Configure (Ninja) | 2.4s | First configure |
| **Clean build (ccache cold, 6% hit)** | **54s pd + 6s server** | ccache warms up; subsequent runs fast |
| **Warm ccache (clean Build/, 100% hit)** | **9.4s pd + 1.1s server** | ✓ meets <10s target |
| **Incremental (system.c touch)** | **1.7s** | ✓ meets <5s target |
| No-op rebuild | 0.1s | Ninja depfile check |
| PCH build (cold PCH, warm ccache) | 38.6s | PCH itself compiles once then cached |

---

---

## 1. Baseline Measurements

Hardware: 24-core Windows 11 desktop, MSYS2 MinGW GCC 15.2.0, GNU ld 2.45.

### Current state (using `make` via Unix Makefiles generator)

The build-headless.ps1 and build-gui.ps1 both use `-G "Unix Makefiles"` with `/usr/bin/make.exe`.
The canonical recipe in CLAUDE.md mentions Ninja, but the actual scripts never use it.
Client and server build into **separate** directories, so shared files (ImGui, etc.) compile twice.

### Measured with Ninja (same build directory for both targets, 24 cores)

| Metric | Time | Notes |
|--------|------|-------|
| CMake configure | 2.5s | First-time configure |
| CMake reconfigure (no changes) | 0.65s | GLOB_RECURSE re-check |
| **Client clean build** | **32.8s** | 689 compile steps + 1 link |
| **Server clean build** (after client) | **9.2s** | 60 new steps (shared objs cached) |
| **Both targets together** | **36.0s** | Single build dir, 749 steps |
| No-op rebuild | 0.05-0.09s | Ninja depfile check only |
| Touch 1 .c file (player.c) | 2.65s | 1 compile + 1 link |
| Touch types.h (352 includers) | 18.7s | 361 recompiles + 1 link |
| Link alone (client exe) | 1.4-1.5s | GNU ld, 49 MB exe with -g |

### Key ratios

- **Compile : Link** = 92% : 8% -- compile dominates; linker is not the bottleneck
- **ImGui alone** = 24s of compile time (imgui.cpp 10s + widgets 8.2s + draw 5.9s) = **67% of compile wall-clock** on clean build (as tail items on critical path)
- **types.h fan-out** = 362/680 TUs (53%) -- touching any "core 4" header (types.h, ultra64.h, data.h, constants.h) triggers >50% recompile

### Slowest compilation units (>3.5s each)

| File | Time |
|------|------|
| port/fast3d/imgui/imgui.cpp | 10.0s |
| port/fast3d/imgui/imgui_widgets.cpp | 8.2s |
| port/fast3d/imgui/imgui_draw.cpp | 5.9s |
| src/game/propobj.c | 5.5s |
| port/fast3d/imgui/imgui_demo.cpp | 5.3s |
| src/game/chraction.c | 4.7s |
| port/fast3d/imgui/imgui_tables.cpp | 4.4s |
| port/fast3d/gfx_pc.cpp | 4.4s |
| port/src/system.c | 4.2s |
| port/fast3d/gfx_opengl.cpp | 4.0s |
| port/src/updater.c | 3.8s |
| port/fast3d/pdgui_filebrowser.cpp | 3.8s |
| port/fast3d/pdgui_backend.cpp | 3.8s |
| port/src/net/netmsg.c | 3.8s |
| port/fast3d/pdgui_menu_mainmenu.cpp | 3.7s |

---

## 2. Ranked Improvements

### Tier 1: High impact, low effort, zero risk

#### 1a. Switch from `make` to Ninja generator
- **Estimated savings**: 5-15% on clean builds, **dramatically** better incremental builds
- **Effort**: Change one string in build-headless.ps1 and build-gui.ps1
- **Risk**: Near zero -- Ninja is already installed (MSYS2 `mingw-w64-x86_64-ninja 1.13.2`)
- **Why**: Ninja has superior dependency tracking, minimal-rebuild logic, and better parallelism scheduling. Make's recursive glob checking and makefile parsing add overhead. No-op rebuild: 0.05s (Ninja) vs 0.5s+ (make). Ninja also has native support for depfiles (`.d` files) which makes incremental builds more reliable.
- **Side effect**: Enables single build directory for both targets (see 1b).

**Implementation:**
```powershell
# build-headless.ps1 -- change ONE line:
# Old:
$configArgs = "-G `"Unix Makefiles`" -DCMAKE_MAKE_PROGRAM=`"$MakeExe`" ..."
# New:
$configArgs = "-G Ninja -DCMAKE_C_COMPILER=`"$CC`" ..."
# Also: remove $MakeExe variable (no longer needed)
# Change build args from "-- -j$Cores -k" to ""
# (Ninja auto-detects cores and has -k built in)
```

#### 1b. Unified build directory (client + server in one dir)
- **Estimated savings**: ~9s on "build all" (server's shared files like ImGui don't recompile)
- **Effort**: Merge `build/client` and `build/server` into `build/` or `Build/`
- **Risk**: Low -- Ninja handles multiple targets in one dir natively
- **Why**: Currently ImGui compiles **twice** (once per build dir) taking 24s total. With a shared dir, it compiles once.
- **Dependency**: Requires Ninja (1a) or at least a single cmake configure for both targets.

**Implementation:**
```powershell
# Replace separate client/server dirs with one:
$BuildDir = Join-Path $ProjectDir "Build"
# Configure once:
cmake -G Ninja -DCMAKE_C_COMPILER="..." -B "$BuildDir" -S "$ProjectDir"
# Build both:
ninja -C "$BuildDir" pd pd-server
# Or one at a time:
ninja -C "$BuildDir" pd          # client only
ninja -C "$BuildDir" pd-server   # server only (fast -- shared objs cached)
```

#### 1c. Install and enable ccache (or sccache)
- **Estimated savings**: Clean builds after first run go from 33s to ~5-8s (warm cache hit rate ~90%+)
- **Effort**: One `pacman -S` + two cmake flags
- **Risk**: Near zero -- transparent wrapper, no code changes
- **Why**: PD2's "all builds are clean builds" constraint means the build-gui always deletes `build/`. ccache survives clean builds because it caches by content hash, not by path. This is the **single biggest win** for the GUI build flow.
- **Note**: Both `mingw-w64-x86_64-ccache` and `mingw-w64-x86_64-sccache` are available in MSYS2. ccache is simpler to set up; sccache supports distributed caching but is overkill here.

**Installation:**
```bash
# In MSYS2 MINGW64 shell:
pacman -S mingw-w64-x86_64-ccache
```

**CMake integration:**
```powershell
# Add to cmake configure args:
$configArgs = "... -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache ..."
```

**Or system-wide (no cmake changes):**
```bash
# In MSYS2 MINGW64 shell:
# ccache wraps gcc/g++ transparently when /mingw64/lib/ccache/bin is in PATH
export PATH="/mingw64/lib/ccache/bin:$PATH"
```

### Tier 2: Medium impact, medium effort

#### 2a. Precompiled header for the "Core 4" headers
- **Estimated savings**: 15-25% on clean build (~5-8s), **50%+ on types.h-triggered incremental**
- **Effort**: ~10 lines of CMake, testing required
- **Risk**: Medium -- PCH with mixed C/C++ needs separate PCH per language; GCC PCH can be finicky
- **Why**: `types.h`, `ultra64.h`, `data.h`, `constants.h` are included by 320-362 of 680 TUs and rarely change. A PCH for these saves parsing them 300+ times per build.

**Candidate headers (included in >50% of game TUs):**
| Header | Includers | % of src/game/ |
|--------|-----------|----------------|
| types.h | 362 | 53% |
| ultra64.h | 350 | 51% |
| data.h | 337 | 50% |
| constants.h | 323 | 47% |
| bss.h | 281 | 41% |

**Implementation sketch (CMake 3.16+):**
```cmake
# In CMakeLists.txt, after defining the pd target:
target_precompile_headers(pd PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/src/include/types.h>"
  "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/include/ultra64.h>"
  "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/src/include/data.h>"
  "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/src/include/constants.h>"
)
# Note: only for C language -- C++ port files have different include patterns
```

**Caveats:**
- Must not include `bss.h` in PCH (it has `extern` declarations that vary per TU)
- PCH invalidation on header change still triggers full rebuild of PCH consumers
- The `-include "versioninfo.h"` force-include in compile flags may interact oddly -- test required

#### 2b. Consider `-O1` instead of `-Og` for dev builds
- **Estimated savings**: Potentially 20-40% compile time reduction
- **Effort**: One-line change in CMakeLists.txt
- **Risk**: Medium -- `-O1` may change debugging experience; need Mike's preference
- **Why**: `-Og` (optimize for debugging) is *slower to compile* than `-O1` in GCC because it runs optimization passes but constrains them to preserve debug info. `-O1` is faster to compile and produces faster code. The `-g` flag (always present) ensures debug symbols regardless of -O level.
- **Trade-off**: Slightly less debuggable code (variables may be optimized out), but significantly faster compilation.
- **Note**: The current `-fno-inline-functions` flag also adds compile time by creating more function call overhead for the optimizer to consider.

### Tier 3: Lower priority / longer term

#### 3a. Replace GNU `ld` with `lld` or `mold`
- **Estimated savings**: Link time from 1.4s to ~0.3-0.5s (70% reduction)
- **Effort**: Install package + one cmake flag
- **Risk**: Low-medium -- lld/mold with MinGW is well-tested but less battle-hardened than GNU ld for this specific project
- **Why**: Both `mingw-w64-x86_64-lld` and `mingw-w64-x86_64-mold` are available. However, link time is only 8% of clean build, so the absolute savings are ~1s. More impactful for single-file incremental builds where link is 50% of the time.
- **Implementation**: `pacman -S mingw-w64-x86_64-mold` + `-DCMAKE_LINKER_TYPE=MOLD` or `-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold"`

#### 3b. Unity build for C++ port code only
- **Estimated savings**: ~5-10s on clean build for the port/fast3d C++ files
- **Effort**: CMake property per-source
- **Risk**: Medium -- need to test for ODR violations in C++ port code
- **Why**: Unity build for the full codebase fails (conflicting types in decompiled C code). But the C++ port code under `port/fast3d/` is modern and may be unity-compatible. The ImGui files alone (24s critical path) would benefit hugely.
- **Note**: Requires `set_source_files_properties(... PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)` for problem files.

#### 3c. Split ImGui into a static library target
- **Estimated savings**: Avoids double-compile when building both targets with separate dirs
- **Effort**: ~20 lines of CMake
- **Risk**: Low
- **Why**: If the unified build dir approach (1b) isn't adopted, this is the alternative. Create `add_library(imgui STATIC ...)` and link it to both pd and pd-server. ImGui files never change (vendored), so this library only builds once.
- **Note**: Moot if 1b is adopted.

---

## 3. What Was Investigated and Ruled Out

### GLOB_RECURSE migration to explicit source lists
- **Finding**: Reconfigure with GLOB_RECURSE costs 0.65s. Not measurable during builds.
- **Verdict**: Not worth the maintenance burden of keeping explicit lists in sync. GLOB_RECURSE is fine.

### CMAKE_UNITY_BUILD for the full codebase
- **Finding**: Fails immediately with conflicting type definitions (`du`/`fu` in `guint.h`), `-Werror=return-type` failures in decompiled stub functions, and macro collisions.
- **Verdict**: Not feasible for the C game code without significant refactoring.

### Asset generation bottleneck
- **Finding**: 191 JSON files processed by Python scripts. Asset headers are generated via `add_custom_command` with proper `DEPENDS` on the JSON files -- they only re-run when the JSON changes. No unnecessary rebuilds observed.
- **Verdict**: Asset generation is not a bottleneck.

### Parallelism bottleneck
- **Finding**: 24 cores, 689 compile steps. Ninja saturates all cores for the first ~25s, then the last few slow TUs (ImGui) become the tail. No serialization points except the link step (which correctly depends on all objects).
- **Verdict**: Parallelism is already good. The bottleneck is the critical path through the slowest TUs.

---

## 4. Recommended Implementation Order

```
Phase 0 (5 min): Install ccache
  pacman -S mingw-w64-x86_64-ccache

Phase 1 (30 min): Switch to Ninja + unified build dir + ccache
  - Modify build-headless.ps1: -G Ninja, single build dir, ccache launcher
  - Modify build-gui.ps1 (or dev-window.ps1): same changes
  - Test: clean build, incremental build, both-targets build

Phase 2 (1-2 hours): PCH for core headers
  - Add target_precompile_headers for types.h, ultra64.h, data.h, constants.h
  - Test with both C and C++ files
  - Measure delta

Phase 3 (optional, 15 min): Try mold linker
  - pacman -S mingw-w64-x86_64-mold
  - Add -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=mold" to cmake args
  - Measure delta
```

### Expected cumulative impact

| Improvement | Clean build | Incremental (1 .c) | Types.h touched |
|-------------|------------|---------------------|-----------------|
| Baseline (Ninja) | 36s | 2.7s | 18.7s |
| + ccache (warm) | **~6s** | **~1.5s** | **~4s** |
| + PCH | **~4s** | **~1.5s** | **~3s** |
| + mold | ~3.5s | ~1.3s | ~2.8s |

For the GUI build tool (which always cleans):
| Improvement | Full "build all" |
|-------------|-----------------|
| Current (make, two dirs, no cache) | ~70-90s estimated |
| Ninja + unified dir + ccache (warm) | **~6-8s** |

---

## 5. Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| Ninja not handling some edge case on Windows | Ninja 1.13.2 is mature; test build-headless.ps1 first |
| ccache cache corruption | `ccache -C` to clear; doesn't affect source code |
| PCH interacting with `-include "versioninfo.h"` | Test in isolation branch first |
| mold producing different binary | Verify PerfectDark.exe runs correctly after mold-linked build |
| Unified build dir confusing existing tooling | Only build-headless.ps1 and build-gui.ps1 reference build paths |

---

## 6. Open Questions for Mike

1. **Is the "all builds are clean builds" constraint still desired for the GUI build tool?** With ccache, clean builds are nearly as fast as incremental. But even without that constraint, Ninja incremental builds are fast and reliable.

2. **Compile optimization level preference**: Currently `-Og`. Would `-O1` be acceptable for day-to-day development? (Faster compile, slightly less debug-friendly.)

3. **DLL copy overhead**: The post-build DLL copy steps (16 DLLs) run on every build even when unchanged. Should these be converted to symlinks or `copy_if_different` only? (Currently uses `copy_if_different` in CMake custom commands, which is correct -- this is a non-issue.)

4. **Should devtools/build-headless.ps1 always auto-commit before building?** The current script does `git add -A && git commit` before every build. This interacts oddly with worktree-based development. Consider making auto-commit opt-in.
