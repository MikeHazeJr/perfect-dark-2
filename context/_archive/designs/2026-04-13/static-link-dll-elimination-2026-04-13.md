# Static Link / DLL Elimination — 2026-04-13

**Goal**: `PerfectDark.exe` and `PerfectDarkServer.exe` ship as single executables with zero external DLL dependencies. Users should be able to move the `.exe` to an empty folder and double-click it without copying any DLLs alongside.

**Status**: DONE. DLL copy block removed. SDL2 static deps completed. See verification steps below.

---

## DLL Audit Table

| DLL (was dynamic) | Static replacement | MSYS2 package | Has `.a`? | License | Status |
|---|---|---|---|---|---|
| `SDL2.dll` | `libSDL2.a` + `libSDL2main.a` | `mingw-w64-x86_64-SDL2` | Yes | zlib/libpng | **STATIC** (pre-existing) |
| `zlib1.dll` | `libz.a` | `mingw-w64-x86_64-zlib` | Yes | zlib | **STATIC** (pre-existing) |
| `libcurl-4.dll` | `libcurl.a` | `mingw-w64-x86_64-curl` | Yes | MIT/curl | **STATIC** (pre-existing) |
| `libssl-3-x64.dll` | `libssl.a` | `mingw-w64-x86_64-openssl` | Yes | Apache 2.0 | **STATIC** (pre-existing) |
| `libcrypto-3-x64.dll` | `libcrypto.a` | `mingw-w64-x86_64-openssl` | Yes | Apache 2.0 | **STATIC** (pre-existing) |
| `libnghttp2-14.dll` | `libnghttp2.a` | `mingw-w64-x86_64-libnghttp2` | Yes | MIT | **STATIC** (pre-existing) |
| `libnghttp3-9.dll` | `libnghttp3.a` | `mingw-w64-x86_64-nghttp3` | Yes | MIT | **STATIC** (conditional) |
| `libngtcp2-16.dll` | `libngtcp2.a` | `mingw-w64-x86_64-ngtcp2` | Yes | MIT | **STATIC** (conditional) |
| `libngtcp2_crypto_ossl-0.dll` | `libngtcp2_crypto_ossl.a` | `mingw-w64-x86_64-ngtcp2` | Yes | MIT | **STATIC** (conditional) |
| `libssh2-1.dll` | `libssh2.a` | `mingw-w64-x86_64-libssh2` | Yes | BSD-3 | **STATIC** (conditional) |
| `libbrotlidec.dll` | `libbrotlidec.a` | `mingw-w64-x86_64-brotli` | Yes | MIT | **STATIC** (conditional) |
| `libbrotlicommon.dll` | `libbrotlicommon.a` | `mingw-w64-x86_64-brotli` | Yes | MIT | **STATIC** (conditional) |
| `libidn2-0.dll` | `libidn2.a` | `mingw-w64-x86_64-libidn2` | Yes | LGPL 2.1 | **STATIC** (conditional) |
| `libpsl-5.dll` | `libpsl.a` | `mingw-w64-x86_64-libpsl` | Yes | MIT | **STATIC** (conditional) |
| `libzstd.dll` | `libzstd.a` | `mingw-w64-x86_64-zstd` | Yes | BSD/GPL | **STATIC** (conditional) |
| `libunistring-5.dll` | `libunistring.a` | `mingw-w64-x86_64-libunistring` | Yes | LGPL 3 | **STATIC** (conditional) |
| `libintl-8.dll` | `libintl.a` | `mingw-w64-x86_64-gettext` | Yes | LGPL 2.1 | **STATIC** (conditional) |
| `libiconv-2.dll` | `libiconv.a` | `mingw-w64-x86_64-libiconv` | Yes | LGPL 2 | **STATIC** (conditional) |
| `libgcc_s_seh-1.dll` | `-static-libgcc` linker flag | GCC built-in | N/A | GPL+exception | **STATIC** (pre-existing) |
| `libstdc++-6.dll` | `-static-libstdc++` linker flag | GCC built-in | N/A | GPL+exception | **STATIC** (pre-existing) |
| `libwinpthread-1.dll` | `-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic` | MinGW runtime | N/A | MIT/custom | **STATIC** (pre-existing) |
| `opengl32.dll` | **CARVE-OUT — see below** | Windows system | N/A | Proprietary | **DYNAMIC (intentional)** |

### Windows system DLLs (not shipped — always present on Windows)

These are NOT our responsibility and must stay dynamic:

`kernel32`, `user32`, `gdi32`, `ws2_32`, `winmm`, `imm32`, `ole32`, `oleaut32`, `version`, `setupapi`, `crypt32`, `bcrypt`, `advapi32`, `dbghelp`, `iphlpapi`, `wldap32`, `secur32`, `shell32`, `dinput8`, `dxguid`, `uuid`, `cfgmgr32`

---

## Carve-Out: opengl32.dll

**Cannot and must not be statically linked.** `opengl32.dll` is a Windows system component that acts as the ICD (Installable Client Driver) loader — it dispatches to the actual GPU driver (NVIDIA, AMD, Intel). There is no static version and there never will be. This is the correct design: every Windows installation includes `opengl32.dll`. It does not need to be shipped.

**Expected `objdump` output**: `opengl32.dll` appears in the import table. This is correct and not a defect.

---

## Changes Made (S224)

### 1. SDL2 static deps expanded (CMakeLists.txt lines 247 and 253)

**Before**:
```cmake
list(APPEND LIBS imm32 version setupapi ole32 oleaut32 gdi32 winmm)
```

**After**:
```cmake
list(APPEND LIBS imm32 version setupapi ole32 oleaut32 gdi32 winmm dinput8 dxguid shell32 user32 uuid)
```

SDL2's own pkg-config specifies `-ldinput8 -ldxguid -luser32 -lshell32 -luuid` for static builds. These are all Windows system import libs (`.dll.a` stubs — they do NOT pull in any MSYS2 DLLs). Adding them ensures the linker satisfies SDL2's full symbol set without fallback to dynamic resolution.

**Note**: `dxerr8` appears in SDL2's pkg-config output but is removed from modern MinGW toolchains (the symbols were merged into dxguid). Omitting it is correct.

### 2. DLL copy block removed (was CMakeLists.txt lines 576-614)

The entire `if(WIN32)` block that copied `libcurl-4.dll` and 16 transitive DLLs to the build output directory was removed. It was dead code: all of those libraries are already statically linked. Replaced with a comment block explaining the current static link status.

---

## Conditional Libraries (if(EXISTS ...) pattern)

The curl chain in CMakeLists.txt conditionally links libs if their `.a` files exist on disk. If a MSYS2 package isn't installed, the lib is silently skipped:

| Library | Required? | Fallback behavior |
|---|---|---|
| `libnghttp3.a`, `libngtcp2*.a` | Optional (HTTP/3 / QUIC) | curl falls back to HTTP/2 via nghttp2 |
| `libssh2.a` | Optional (SCP/SFTP URLs) | Those URL schemes fail at runtime; updater only uses HTTPS |
| `libpsl.a` | Optional (Public Suffix List) | Cookie handling is slightly less strict |
| `libzstd.a` | Optional (Zstandard compression) | Brotli and gzip still work |
| `libunistring.a`, `libiconv.a`, `libintl.a` | Optional (Unicode normalization for IDN) | ASCII domain names work fine |

**Practical impact**: For the updater (HTTPS to api.github.com), the minimum required chain is: `libcurl.a + libssl.a + libcrypto.a + libnghttp2.a`. All others are optional enhancements.

**To install everything**:
```bash
pacman -S mingw-w64-x86_64-curl mingw-w64-x86_64-openssl mingw-w64-x86_64-libnghttp2 \
          mingw-w64-x86_64-nghttp3 mingw-w64-x86_64-ngtcp2 mingw-w64-x86_64-libssh2 \
          mingw-w64-x86_64-brotli mingw-w64-x86_64-libidn2 mingw-w64-x86_64-libpsl \
          mingw-w64-x86_64-zstd mingw-w64-x86_64-libunistring mingw-w64-x86_64-gettext \
          mingw-w64-x86_64-libiconv mingw-w64-x86_64-zlib mingw-w64-x86_64-SDL2
```

---

## License Notes

All statically linked LGPL libraries (libidn2, libunistring, libintl, libiconv) are licensed LGPL 2.1 or LGPL 3. Static linking of LGPL libraries is permitted provided the user can relink with a modified version of the library. Since this project is open source on GitHub, users can rebuild from source — this satisfies the LGPL relinking requirement.

---

## Exe Size

Statically linking all third-party libraries increases exe size significantly. The `libssl.a`/`libcrypto.a` pair alone adds ~4–6 MB. The full chain (including SDL2) may add 15–25 MB vs. the old dynamic build.

**Measure after first build**:
```powershell
# In build directory:
(Get-Item PerfectDark.exe).Length / 1MB
(Get-Item PerfectDarkServer.exe).Length / 1MB
```

Record the result here once known.

---

## Verification Steps for Mike

After building, run:

**1. Move exe to empty folder and double-click:**
```
mkdir C:\temp\pd_test
copy Build\PerfectDark.exe C:\temp\pd_test\
# Copy your data/ and mods/ dirs too, then:
C:\temp\pd_test\PerfectDark.exe
```
Game should launch without any DLL error dialogs.

**2. Check import table:**
```bash
# In MSYS2 shell:
objdump -p Build/PerfectDark.exe | grep "DLL Name"
```

Expected output (ONLY these, or a subset — no MSYS2 DLLs):
```
DLL Name: KERNEL32.dll
DLL Name: USER32.dll
DLL Name: GDI32.dll
DLL Name: OPENGL32.dll
DLL Name: WINMM.dll
DLL Name: WS2_32.dll
DLL Name: DBGHELP.dll
DLL Name: IPHLPAPI.dll
DLL Name: CRYPT32.dll
DLL Name: BCRYPT.dll
DLL Name: WLDAP32.dll
DLL Name: SECUR32.dll
DLL Name: IMM32.dll
DLL Name: VERSION.dll
DLL Name: SETUPAPI.dll
DLL Name: OLE32.dll
DLL Name: OLEAUT32.dll
DLL Name: SHELL32.dll
DLL Name: DINPUT8.dll
```

**If any MSYS2 DLL appears** (e.g., `libcurl-4.dll`, `SDL2.dll`, `libssl-3-x64.dll`, `zlib1.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`), the static `.a` for that library was not found by CMake and it fell back to dynamic. Check CMake configure output for `WARNING: SDL2: Static .a not found` or similar messages.
