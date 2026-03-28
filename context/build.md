# Build System

## Status: STABLE
Build system is fully functional. User compiles on Windows via build.bat or Build Tool GUI.

## Toolchain
- **Compiler**: MinGW GCC (MSYS2), path: `C:\msys64\mingw64\bin\cc.exe`
- **Build generator**: CMake with Unix Makefiles generator
- **Make**: `C:\msys64\usr\bin\make.exe`
- **C standard**: C11 (`set(CMAKE_C_STANDARD 11)` in CMakeLists.txt)
- **Platform**: PC-only (x86_64 Windows). All N64 platform guards stripped (Phase D1 complete)
- **Output**: `build/pd.x86_64.exe`

## Build Methods

### build.bat
Located in project root. Commands: `clean`, `configure`, `build`, `copy`, `all` (default = all).
- `configure`: Runs CMake to generate build files
- `build`: Compiles with `-j%NUMBER_OF_PROCESSORS%` parallel jobs
- `copy`: Copies post-batch-addin files (DLLs, data, mods) into build directory
- `all`: Full pipeline — configure + build + copy

### Build Tool GUI (build-gui.ps1 + "Build Tool.bat")
PowerShell WinForms application. User's primary build method.

**Launch**: Double-click `Build Tool.bat` in project root → runs `powershell -ExecutionPolicy Bypass -File build-gui.ps1`

**Features** (updated 2026-03-18):
- Configure + Build buttons with async C# line reader for non-blocking output
- **Progress bar**: Blue (`0, 96, 191`) during compile → Green (`0, 191, 96`) on success → Red (`191, 0, 0`) on failure
- **-k (keep-going) flag**: Build uses `--build ... -- -j$cores -k` so all errors are reported, not just the first
- **Run Game / Run+Log**: Only enabled after successful build (or if existing exe found in build/)
- **Game process monitoring**: 2-second timer polls process state, shows "Game Running"/"Game Stopped"
- **Run with logging**: Launches with `> game_output.log 2>&1`, button to open log after game exits

**Key state vars**: `$script:BuildSucceeded`, `$script:GameProcess`, `$script:GameRunning`

### Manual CMake
```
cmake -G "Unix Makefiles" -DCMAKE_MAKE_PROGRAM="C:/msys64/usr/bin/make.exe" -DCMAKE_C_COMPILER="C:/msys64/mingw64/bin/cc.exe" -B build -S .
cmake --build build -- -j%NUMBER_OF_PROCESSORS%
```

## Important: Cannot Compile from Linux VM
The build requires MSYS2/MinGW on Windows. The AI sandbox runs Linux and cannot compile this project. All compilation must be done by the user on their Windows machine.

## Important: No GitHub Push Access from Linux VM
The AI sandbox has READ-ONLY access to the Git repository (fetch/clone works, push does not). `gh` CLI is not authenticated here. All git push, tag deletion, release management, and GitHub operations that require write access must be done by the user on their Windows machine where `gh auth login` has been completed. Do NOT attempt to install or authenticate gh in the sandbox — it will fail. Instead, provide the user with ready-to-run commands for their PowerShell.

## Static Linking
- `-static-libgcc` — prevents `libgcc_s_seh-1.dll` dependency (FIX-1)
- `-static-libstdc++` — prevents libstdc++ DLL dependency
- `-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic` — statically links libwinpthread (FIX-5)
- **SDL2, zlib, libcurl**: Now statically linked on Windows (no DLLs needed at runtime). CMakeLists.txt uses find_library to locate .a files in MSYS2 paths.

### Transitive Dependency Conflicts — Verified Non-Issues (2026-03-28)

**SDL2 + OpenSSL version conflict**: Not possible. MSYS2 ships only OpenSSL 3.x (1.1 removed ~2022). More importantly, `libSDL2.a` from MSYS2 is compiled *without* OpenSSL support — SDL_net is a separate package not linked here. Only curl links `libssl.a`/`libcrypto.a`, and both are the same 3.x version.

**zlib duplicate symbols**: Not an issue. SDL2 (PNG loading) and curl (compressed transfers) both reference zlib symbols as *undefined externals* — they do not embed their own copies. A single `libz.a` satisfies both. The linker sees one definition set.

**pthread/winpthread**: `-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic` provides exactly one static copy. SDL2's thread references also resolve to this copy. No duplicates.

**winmm listed twice**: SDL2's transitive deps (line 243) and EXTRA_LIBRARIES (line 354) both reference winmm. Both are `.dll.a` import stubs — the linker deduplicates import references transparently.

To verify in MSYS2 shell:
```bash
# Confirm no SSL symbols in SDL2
nm /mingw64/lib/libSDL2.a | grep -i ssl
# Confirm zlib references are undefined (U), not defined (T/D)
nm /mingw64/lib/libSDL2.a | grep -i " deflate\| inflate" | head -5
# Confirm OpenSSL version
pacman -Qi mingw-w64-x86_64-openssl | grep Version
# Confirm no OpenSSL 1.1 coexistence
ls /mingw64/lib/libssl* /mingw64/lib/libcrypto*
```

## Directory Structure
```
perfect_dark-mike/
├── build/              # Build output (generated)
│   ├── pd.x86_64.exe  # Main executable
│   ├── data/           # Game data (ROM, assets)
│   └── mods/           # Mod directories
├── src/                # Game source code
│   ├── game/           # Gameplay (bondwalk.c, player.c, bot.c, etc.)
│   ├── lib/            # Libraries (collision.c, capsule.c, etc.)
│   └── include/        # Headers
├── port/               # PC port code
│   ├── src/            # Port implementations (net/, fs.c, mod.c, main.c)
│   └── include/        # Port headers
├── include/            # Shared headers (PR/, ultra64.h)
├── tools/              # Build tools
├── build.bat           # Build script
├── CMakeLists.txt      # Build configuration
└── context/            # Project context (this folder)
```

## Mod Loading
- **Default directories**: Compiled into exe via fsInit() fallbacks (FIX-4)
  - `mods/mod_allinone`, `mods/mod_gex`, `mods/mod_kakariko`, `mods/mod_dark_noon`, `mods/mod_goldfinger_64`
- **CLI override**: `--moddir`, `--gexmoddir`, `--kakarikomoddir`, `--darknoonmoddir`, `--goldfinger64moddir`
- **Runtime loader**: `port/src/mod.c` parses `modconfig.txt`, loads textures/models/stages

## CMakeLists.txt Key Additions
- `src/lib/capsule.c` added to source list (line 279, before collision.c)
- Static linking flags on Windows EXTRA_LIBRARIES (line 247)
- VERSION=2 (ntsc-final), VERSION >= VERSION_NTSC_1_0 is true

## Dependencies
- **MSYS2/MinGW**: Compiler toolchain
- **SDL2**: Window/input/audio (currently DLL)
- **zlib**: Compression (currently DLL)
- **ENet**: Networking (statically linked)

## Version System

Version numbers flow: **Dev Window UI** → `CMakeLists.txt` (via `Set-ProjectVersion`) → **cmake configure** → `build/*/port/include/versioninfo.h` (via `configure_file`) → C preprocessor macros `VERSION_MAJOR/MINOR/PATCH` → `VERSION_STRING` macro → window title, updater user-agent, server title.

### CMake CACHE pitfall (fixed 2026-03-26)
Version variables are declared as `CACHE STRING` in CMakeLists.txt:
```cmake
set(VERSION_SEM_MAJOR 0 CACHE STRING "Semantic version major")
```
CMake rule: if a CACHE entry already exists, `set(... CACHE ...)` is **silently ignored** on reconfigure. So editing CMakeLists.txt version numbers does NOT take effect on incremental builds — the old CMakeCache.txt values win.

**Fix**: `Get-BuildSteps -Ver $ver` now appends `-DVERSION_SEM_MAJOR=X -DVERSION_SEM_MINOR=Y -DVERSION_SEM_PATCH=Z` to BOTH cmake configure commands (client and server). Command-line `-D` flags always override the cache and update it.

Only release builds (`Start-PushRelease`) pass the version. Regular BUILD button builds use the cache as-is (consistent with expectation that the cache reflects the last deliberate change).

### Files involved
- `devtools/dev-window.ps1` — `Get-BuildSteps` (cmake args), `Set-ProjectVersion` (edits CMakeLists.txt), `Start-PushRelease` (orchestration)
- `CMakeLists.txt` — declares CACHE vars, runs `configure_file` → `versioninfo.h`
- `port/include/versioninfo.h.in` — template: `@VERSION_SEM_MAJOR@` etc.
- `port/include/updateversion.h` — `VERSION_STRING` macro (string concatenation of `VERSION_MAJOR/MINOR/PATCH`)
- `port/src/video.c` — window title uses `VERSION_STRING`; initial title was hardcoded `v0.0.2` (fixed 2026-03-26)
- `port/src/server_main.c`, `updater.c` — also use `VERSION_STRING`

## Known Issues
- None currently

## Session Fixes (Build-Related)
- **FIX-1**: Added `-static-libgcc` to prevent DLL dependency
- **FIX-5**: Static linked libwinpthread
- **FIX-4**: Default mod directories compiled into exe (no BAT file needed)
- **FIX-13**: Added `#include "system.h"` to game_1531a0.c, mplayer/ingame.c, lib/mempc.c (sysLogPrintf/LOG_NOTE/LOG_WARNING)
- **Build -k flag**: Added keep-going flag to build command for full error reporting
- **Build GUI update**: Progress bar colors, gated run buttons, process monitoring (2026-03-18)

## .gitignore Additions (2026-03-18)
- `build_errors.log`
- `build/game_output.log`
