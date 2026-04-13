# Smoke Verify — 2026-04-13 (Post-S224/S225 Merge)

**Date**: 2026-04-13  
**Session**: S227  
**Branch verified**: `dev` (HEAD `02c95682`)  
**Merges under test**: S224 build-pipeline overhaul + S225 static-link DLL elimination  
**Verified by**: claude/great-khorana worktree (build ran on main project)

---

## Build Timings

| Metric | Measured | Target | Result |
|--------|----------|--------|--------|
| Configure | 2.6s | — | ✓ |
| **Build #1 — pd (ccache cold)** | **41.5s** | — | ✓ (ref: 54s, faster due to PCH) |
| **Build #1 — pd-server (cold)** | **7.9s** | — | ✓ (ref: 6s) |
| **Build #1 — Total** | **49.4s** | — | ✓ |
| **Build #2 — pd (ccache warm)** | **30.7s** | <12s | ❌ BLOCKER |
| **Build #2 — pd-server (warm)** | **2.0s** | — | ✓ |
| **Build #2 — Total warm** | **32.7s** | <12s | ❌ BLOCKER |
| **Incremental (system.c touch)** | **3.6s** | <5s | ✓ |

---

## BLOCKER: ccache Warm Build Regression

**Expected**: <12s warm (pd: 9.4s, server: 1.1s — S224 reference)  
**Actual**: 32.7s warm (pd: 30.7s, server: 2.0s)  
**Root cause**: PCH (precompiled headers) added in S225 breaks ccache

### ccache Stats — Cold Build
```
Cacheable calls:    120 / 555 (21.62%)
  Hits:               7 / 120 ( 5.83%)
  Misses:           113 / 120 (94.17%)
Uncacheable calls:  435 / 555 (78.38%)
```

### ccache Stats — Warm Build  
```
Cacheable calls:    240 / 1110 (21.62%)
  Hits:             127 /  240 (52.92%)
  Misses:           113 /  240 (47.08%)
Uncacheable calls:  870 / 1110 (78.38%)
```

**Analysis**: 78% of compilation units are "uncacheable" by ccache. This is caused by
`target_precompile_headers(pd PRIVATE ...)` added in S225's CMakeLists.txt. GCC PCH 
(`-fpch-preprocess` / `-include` of `.gch` files) is not well-supported by ccache — when 
a TU includes a PCH, ccache may mark it uncacheable.  

**The S224 reference timings (9.4s warm) were measured on the main project *before* the 
PCH merge** — the session log explicitly notes "Build Verification (main project, Ninja + 
ccache, **no PCH**)".

**Fix options** (for Mike to decide):
1. **Remove PCH** from CMakeLists.txt (`target_precompile_headers` block). Trades ~5s 
   PCH compile for 100% ccache hit rate on warm builds. Net: warm build returns to ~9.4s.
2. **Set `CCACHE_SLOPPINESS=pch_defines,time_macros`** or use `ccache --set-config 
   sloppiness=pch_defines,time_macros`. Tells ccache to ignore PCH timestamps when 
   hashing. May restore cache hits without removing PCH.
3. **Set `CCACHE_PCH_EXTERNAL=true`** if ccache 4.x supports it. Treats PCH as an 
   external pre-built artefact.

**File to check**: `CMakeLists.txt` — `target_precompile_headers(pd PRIVATE ...)` block 
(added in commit `955dffa2`).

---

## DLL Audit — PerfectDark.exe

```
DLL Name: ADVAPI32.dll
DLL Name: bcrypt.dll
DLL Name: CRYPT32.dll
DLL Name: dbghelp.dll
DLL Name: GDI32.dll
DLL Name: IMM32.dll
DLL Name: IPHLPAPI.DLL
DLL Name: KERNEL32.dll
DLL Name: msvcrt.dll
DLL Name: ole32.dll
DLL Name: OLEAUT32.dll
DLL Name: Secur32.dll
DLL Name: SETUPAPI.dll
DLL Name: SHELL32.dll
DLL Name: USER32.dll
DLL Name: VERSION.dll
DLL Name: WINMM.dll
DLL Name: WLDAP32.dll
DLL Name: WS2_32.dll
```

**PASS** — 19 entries, all Windows system DLLs. Zero MSYS2/MinGW DLLs.  
Note: `opengl32.dll` absent from import table — SDL2 loads it dynamically at runtime 
via `SDL_GL_LoadLibrary`. This is correct and expected.

---

## DLL Audit — PerfectDarkServer.exe

```
DLL Name: ADVAPI32.dll
DLL Name: bcrypt.dll
DLL Name: CRYPT32.dll
DLL Name: dbghelp.dll
DLL Name: GDI32.dll
DLL Name: IMM32.dll
DLL Name: IPHLPAPI.DLL
DLL Name: KERNEL32.dll
DLL Name: msvcrt.dll
DLL Name: ole32.dll
DLL Name: OLEAUT32.dll
DLL Name: Secur32.dll
DLL Name: SETUPAPI.dll
DLL Name: SHELL32.dll
DLL Name: USER32.dll
DLL Name: VERSION.dll
DLL Name: WINMM.dll
DLL Name: WLDAP32.dll
DLL Name: WS2_32.dll
```

**PASS** — 19 entries, identical set to client. All Windows system DLLs.

---

## Standalone Executable Test

Copied `PerfectDark.exe` to an empty temp dir. `ldd` against isolated exe — all 31 
transitive DLL dependencies resolve to `C:\WINDOWS\System32\` or `C:\WINDOWS\SYSTEM32\`.  
No MSYS2 paths (`/c/msys64/...`). No "not found" entries.

```
ADVAPI32.dll  => C:\WINDOWS\System32\ADVAPI32.dll
CRYPT32.dll   => C:\WINDOWS\System32\CRYPT32.dll
GDI32.dll     => C:\WINDOWS\System32\GDI32.dll
IMM32.dll     => C:\WINDOWS\System32\IMM32.dll
IPHLPAPI.DLL  => C:\WINDOWS\SYSTEM32\IPHLPAPI.DLL
KERNEL32.DLL  => C:\WINDOWS\System32\KERNEL32.DLL
KERNELBASE.dll => C:\WINDOWS\System32\KERNELBASE.dll
[... 24 more Windows system DLLs ...]
```

**PASS** — exe is fully self-contained. No "missing DLL" dialogs expected on any 
Windows 10/11 machine.

---

## Exe Sizes

| Executable | Bytes | MiB | Notes |
|------------|-------|-----|-------|
| `PerfectDark.exe` | 50,932,539 | 48.6 MiB | Matches S224 reference (48.6 MB) |
| `PerfectDarkServer.exe` | 22,806,981 | 21.7 MiB | First measurement |

---

## Transient Warnings (Non-Blocking)

Pre-existing warnings, present in both builds, not regressions:

1. `enet.h:1129` — `gettime_offset` defined but not used (`-Wunused-function`)
2. `updater.c:10`, `updater.h:199`, `server_gui.cpp:14` — `/*` within comment (`-Wcomment`)

---

## Summary

| Check | Result |
|-------|--------|
| Cold build (pd) | ✓ 41.5s |
| Cold build (server) | ✓ 7.9s |
| **Warm build <12s** | **❌ BLOCKER — 30.7s (PCH breaks ccache)** |
| Incremental <5s | ✓ 3.6s |
| DLL audit — client | ✓ PASS |
| DLL audit — server | ✓ PASS |
| Standalone exe | ✓ PASS |
| Exe size (client) | ✓ 48.6 MiB (matches reference) |
| Build errors | ✓ Zero |

**One blocker**: warm ccache regression from 9.4s → 30.7s due to PCH + ccache incompatibility.  
**All static-link / DLL elimination work is confirmed working** — zero MSYS2 DLLs.

---

## L0-BUILD Fix Applied — S231 (2026-04-13)

**Fix**: Added `$env:CCACHE_SLOPPINESS = "pch_defines,time_macros"` to all three build scripts:
- `devtools/build-headless.ps1` — top-level env section
- `devtools/dev-window-v2/dev-window-v2.ps1` — top-level env + `psi.EnvironmentVariables` subprocess block
- `devtools/release.ps1` — top-level env section

**Rationale**: `pch_defines` tells ccache to ignore PCH defines variation; `time_macros` tells ccache to ignore `__TIME__`/`__DATE__` timestamps. Together these restore cache hit rate for TUs that use `cmake_pch.h`.

**Expected result**: warm `pd` build drops from 30.7s → ~9.4s (pre-PCH reference). Fallback if still > 12s: remove `target_precompile_headers(pd PRIVATE ...)` block from CMakeLists.txt.

**Mike: run warm-build verify** — two consecutive `.\devtools\build-headless.ps1 -Target client` runs. Record second-run time here and update summary table above.
