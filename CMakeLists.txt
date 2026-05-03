cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    # ARM Macs start at OS 11.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
  else()
    # SDL2 requires at least this version for everything to work properly.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.9" CACHE STRING "Minimum OS X deployment version")
  endif()
endif()

project(pd C CXX)

include(TargetArch)
include(FindPkgConfig)
include(AssetUtils)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

# On first build set the build type to RelWithDebInfo if it's not already set
if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
  endif()
endif()

# Options

set(ROMID "ntsc-final" CACHE STRING "Which ROM to build an executable for: ntsc-final, pal-final or jpn-final")
option(FORCE_CROSSCOMPILE "Force cross compiling" NO)

# Determine target arch

target_architecture(TARGET_ARCH)
if(TARGET_ARCH STREQUAL "i386")
  # for historical reasons
  set(TARGET_ARCH "i686")
endif()

# TODO
set(TARGET_IS_BIG_ENDIAN FALSE)

if((TARGET_ARCH MATCHES "unknown") OR (TARGET_ARCH STREQUAL ""))
  message(FATAL_ERROR "Unknown target arch")
elseif(TARGET_ARCH MATCHES "64")
  set(TARGET_IS_64BIT TRUE)
else()
  set(TARGET_IS_64BIT FALSE)
endif()

message(STATUS "Target arch: ${TARGET_ARCH}")

# Determine target OS and platform identifier ($arch-$os)

if(WIN32)
  # Windows
  set(TARGET_OS "windows")
elseif(APPLE)
  # OSX
  set(TARGET_OS "osx")
elseif(NINTENDO_SWITCH)
  # Switch
  set(TARGET_OS "nswitch")
elseif(UNIX)
  # Generic *nix; assume Linux
  set(TARGET_OS "linux")
else()
  # ???
  message(FATAL_ERROR "Unknown target OS or compiler")
endif()

set(TARGET_PLATFORM "${TARGET_ARCH}-${TARGET_OS}")

message(STATUS "Target platform: ${TARGET_PLATFORM}")

# Calculate version info and binary name

set(VERSION_IS_PAL 0)

if(ROMID STREQUAL "ntsc-final")
  set(VERSION 2)
  set(BIN_REGION "")
elseif(ROMID STREQUAL "pal-final")
  set(VERSION 4)
  set(BIN_REGION ".pal")
  set(VERSION_IS_PAL 1)
elseif(ROMID STREQUAL "jpn-final")
  set(VERSION 5)
  set(BIN_REGION ".jpn")
else()
  message(FATAL_ERROR "Unknown ROM ID: ${ROMID}")
endif()

# git commit hash
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# git branch name
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(VERSION_TARGET "${TARGET_PLATFORM}")
set(VERSION_ARCH "${TARGET_ARCH}")
set(VERSION_ROMID "${ROMID}")
set(VERSION_BUILD "${CMAKE_BUILD_TYPE}")

if((VERSION_BRANCH STREQUAL "") OR (VERSION_BRANCH STREQUAL "HEAD"))
  set(VERSION_BRANCH "port-custom")
endif()

# Semantic version (D13 Update System)
# Version is Major.Minor.Revision. Channel (stable/dev) is determined by the
# GitHub release prerelease flag, not by the version number itself.
set(VERSION_SEM_MAJOR 0 CACHE STRING "Semantic version major")
set(VERSION_SEM_MINOR 0 CACHE STRING "Semantic version minor")
set(VERSION_SEM_PATCH 190 CACHE STRING "Semantic version revision")

set(VERSION_SEM_STRING "${VERSION_SEM_MAJOR}.${VERSION_SEM_MINOR}.${VERSION_SEM_PATCH}")

message(STATUS "Version: ${VERSION_ROMID}, v${VERSION_SEM_STRING}, hash ${VERSION_HASH}, branch ${VERSION_BRANCH}")

configure_file("${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in" "${CMAKE_BINARY_DIR}/port/include/versioninfo.h" @ONLY)

# Embed Mozilla CA bundle for libcurl (D13 updater TLS).
# Testers hit "SSL peer certificate or SSH remote key was not OK" because the
# statically-linked libcurl.a in MSYS2 is built against OpenSSL WITHOUT the
# winstore integration (SSL_CTX_load_verify_store is absent), so
# CURLSSLOPT_NATIVE_CA is effectively a no-op for this backend and there is
# no external CA file to fall back on. Embedding the Mozilla bundle as a
# CURLOPT_CAINFO_BLOB makes verification self-contained, zero-DLL compliant,
# and independent of whatever cert store exists on the tester's machine.
set(_CACERT_SRC "${CMAKE_SOURCE_DIR}/port/src/cacert.pem")
set(_CACERT_OUT "${CMAKE_BINARY_DIR}/port/include/cacert_blob.h")
# Skip regeneration when output is already up-to-date. file(WRITE) always
# overwrites and bumps mtime, so unconditional generation forced a recompile
# of every TU including cacert_blob.h plus a full relink of pd / pd-server /
# Updater on every cmake configure -- the dominant build-time cost since
# 2026-04-17 because dev-window-v2 reconfigures on every build.
set(_cacert_needs_regen TRUE)
if(EXISTS "${_CACERT_OUT}")
  if(EXISTS "${_CACERT_SRC}")
    if(NOT "${_CACERT_SRC}" IS_NEWER_THAN "${_CACERT_OUT}")
      set(_cacert_needs_regen FALSE)
    endif()
  else()
    # No source file but output exists -- keep the existing fallback blob.
    set(_cacert_needs_regen FALSE)
  endif()
endif()
if(_cacert_needs_regen)
  if(EXISTS "${_CACERT_SRC}")
    file(READ "${_CACERT_SRC}" _cacert_hex HEX)
    string(LENGTH "${_cacert_hex}" _cacert_hex_len)
    math(EXPR _cacert_bytes "${_cacert_hex_len} / 2")
    string(REGEX REPLACE "(..)" "0x\\1," _cacert_c_body "${_cacert_hex}")
    file(WRITE "${_CACERT_OUT}"
      "/* Auto-generated from port/src/cacert.pem by CMake. Do not edit. */\n"
      "#ifndef CACERT_BLOB_H\n"
      "#define CACERT_BLOB_H\n\n"
      "#include <stddef.h>\n\n"
      "static const unsigned char cacert_pem_data[] = {\n"
      "${_cacert_c_body}\n"
      "};\n\n"
      "static const size_t cacert_pem_size = ${_cacert_bytes};\n\n"
      "#endif /* CACERT_BLOB_H */\n"
    )
    message(STATUS "CACERT: Embedded Mozilla CA bundle (${_cacert_bytes} bytes)")
  else()
    message(WARNING "CACERT: ${_CACERT_SRC} not found; updater TLS will fall back to system CA store")
    file(WRITE "${_CACERT_OUT}"
      "/* Auto-generated fallback (no cacert.pem present). */\n"
      "#ifndef CACERT_BLOB_H\n#define CACERT_BLOB_H\n#include <stddef.h>\n"
      "static const unsigned char cacert_pem_data[1] = { 0 };\n"
      "static const size_t cacert_pem_size = 0;\n"
      "#endif /* CACERT_BLOB_H */\n"
    )
  endif()
else()
  message(STATUS "CACERT: cacert_blob.h is up-to-date; skipping regeneration")
endif()

set(ASSET_DIR "src/assets/${ROMID}")
set(GENERATED_DIR "src/generated/${ROMID}")

set(ENV{ROMID} ${ROMID})

# Build folder postfix

set(BUILD_ID "${VERSION_ROMID}-${VERSION_TARGET}")

# Compiler defs

add_definitions(
  -DMATCHING=0
  -DPIRACYCHECKS=0
  -DROM_SIZE=32
  -DAVOID_UB=1
  -D_LANGUAGE_C=1
  -DPAL=${VERSION_IS_PAL}
  -DVERSION=${VERSION}
)

if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

# Compiler flags

# Include directories
include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/include/PR")
include_directories("${CMAKE_SOURCE_DIR}/src/include")
include_directories("${CMAKE_BINARY_DIR}/src/generated/${ROMID}")
include_directories("${CMAKE_SOURCE_DIR}/src/lib/ultra/audio")
include_directories("${CMAKE_SOURCE_DIR}/port/include")
include_directories("${CMAKE_BINARY_DIR}/port/include")
include_directories("${CMAKE_SOURCE_DIR}/port/fast3d/imgui")  # Dear ImGui (D3d)
include_directories("${CMAKE_SOURCE_DIR}/port/external/miniupnpc/include")  # UPnP (auto port forwarding)
include_directories("${CMAKE_SOURCE_DIR}/port/external/miniupnpc/src")      # UPnP internal headers

# Set Release optimization to -Og until I fix the -O2 issues
if(NOT (CMAKE_BUILD_TYPE STREQUAL "Debug"))
  add_compile_options(-Og)
endif()

# Debug info — explicit -g ensures DWARF symbols are embedded regardless
# of CMAKE_BUILD_TYPE. Without this, addr2line returns ?? on MinGW.
add_compile_options(-g)

# Common CFLAGS
add_compile_options(
  -fno-inline-functions
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -fstack-protector-strong
)
# NOTE: versioninfo.h used to be force-included here via -include, which
# invalidated every TU's ccache entry on every commit (VERSION_HASH changes).
# Files that need VERSION_* macros must now #include "versioninfo.h" directly.

# Common warning flags
add_compile_options(
  -Wall
  -Wno-address
  -Wno-int-in-bool-context
  -Wno-misleading-indentation
  -Wno-missing-braces
  -Wno-multichar
  -Wno-tautological-compare
  -Wno-unused-but-set-variable
  -Wno-unused-value
  -Wno-unused-variable
)

# C specific flags
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>)
# Promote these to hard errors for C — catches missing forward decls and missing returns
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Werror=return-type>)

# C++ specific flags
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>)

# Platform-specific flags
if (APPLE)
  # Make it look for libs in the executable folder manually since CMake's rpath system is wack
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rpath @executable_path")
else()
  # This warning is not recognized by XCode/Clang for some reason.
  add_compile_options(-Wno-format-truncation)
endif()

# Dependencies

set(LIBS )

# SDL2 -- static on Windows so no SDL2.dll is needed at runtime
# MSYS2: pacman -S mingw-w64-x86_64-SDL2
find_package(SDL2 REQUIRED)
if(NINTENDO_SWITCH)
  if(NOT SDL2_LIBRARY OR NOT SDL2_INCLUDE_DIR)
    set(SDL2_LIBRARY "SDL2")
    set(SDL2_INCLUDE_DIR "${DEVKITPRO}/portlibs/switch/include/SDL2")
  endif()
endif()
include_directories(SYSTEM "${SDL2_INCLUDE_DIR}")
set(_msys_lib "C:/msys64/mingw64/lib")
if(WIN32)
  # Direct file check for static .a (find_library can resolve to .dll.a stubs)
  if(EXISTS "${_msys_lib}/libSDL2.a" AND EXISTS "${_msys_lib}/libSDL2main.a")
    message(STATUS "SDL2: Static linking via ${_msys_lib}/libSDL2.a")
    list(APPEND LIBS mingw32 "${_msys_lib}/libSDL2main.a" "${_msys_lib}/libSDL2.a")
    list(APPEND LIBS imm32 version setupapi ole32 oleaut32 gdi32 winmm dinput8 dxguid shell32 user32 uuid bcrypt)
  elseif(SDL2_INCLUDE_DIR)
    get_filename_component(_sdl2_lib "${SDL2_INCLUDE_DIR}/../lib" ABSOLUTE)
    if(EXISTS "${_sdl2_lib}/libSDL2.a" AND EXISTS "${_sdl2_lib}/libSDL2main.a")
      message(STATUS "SDL2: Static linking via ${_sdl2_lib}/libSDL2.a")
      list(APPEND LIBS mingw32 "${_sdl2_lib}/libSDL2main.a" "${_sdl2_lib}/libSDL2.a")
      list(APPEND LIBS imm32 version setupapi ole32 oleaut32 gdi32 winmm dinput8 dxguid shell32 user32 uuid bcrypt)
    else()
      message(WARNING "SDL2: No libSDL2.a at ${_sdl2_lib} or ${_msys_lib}, falling back to dynamic")
      list(APPEND LIBS "${SDL2_LIBRARY}")
    endif()
  else()
    message(WARNING "SDL2: Static .a not found, falling back to dynamic (will need SDL2.dll)")
    list(APPEND LIBS "${SDL2_LIBRARY}")
  endif()
else()
  list(APPEND LIBS "${SDL2_LIBRARY}")
endif()

# libopus -- OPTIONAL Phase 5 voice codec (BSD-licensed). Static-link when
# available; voice.c falls back to the scaffold (PTT no-op) when not.
# MSYS2: pacman -S mingw-w64-x86_64-opus
# Decision logged in context/audits/connectivity-libopus-decision-2026-04-25.md
function(pd_include_system_dirs)
  set(_skip_dirs ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES} ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
  if(WIN32 AND CMAKE_C_COMPILER)
    get_filename_component(_compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_compiler_prefix "${_compiler_bin}/.." ABSOLUTE)
    list(APPEND _skip_dirs "${_compiler_prefix}/include")
  endif()

  foreach(_dir IN LISTS ARGN)
    if(NOT _dir)
      continue()
    endif()

    get_filename_component(_abs_dir "${_dir}" ABSOLUTE)
    set(_is_implicit FALSE)

    foreach(_skip IN LISTS _skip_dirs)
      if(NOT _skip)
        continue()
      endif()
      get_filename_component(_abs_skip "${_skip}" ABSOLUTE)
      if(_abs_dir STREQUAL _abs_skip)
        set(_is_implicit TRUE)
        break()
      endif()
    endforeach()

    if(NOT _is_implicit)
      include_directories(SYSTEM "${_dir}")
    endif()
  endforeach()
endfunction()

find_package(PkgConfig QUIET)
set(HAVE_OPUS 0)
if(PkgConfig_FOUND)
  pkg_check_modules(OPUS QUIET opus)
  if(OPUS_FOUND)
    set(HAVE_OPUS 1)
    add_definitions(-DHAVE_OPUS=1)
    pd_include_system_dirs(${OPUS_INCLUDE_DIRS})
    if(WIN32 AND EXISTS "${_msys_lib}/libopus.a")
      message(STATUS "OPUS: Static linking via ${_msys_lib}/libopus.a")
      list(APPEND LIBS "${_msys_lib}/libopus.a")
    else()
      list(APPEND LIBS ${OPUS_LIBRARIES})
      message(STATUS "OPUS: Linking via pkg-config (libopus ${OPUS_VERSION})")
    endif()
  else()
    message(STATUS "OPUS: not found via pkg-config; voice codec disabled (Phase 5 scaffold remains active)")
  endif()
else()
  message(STATUS "OPUS: pkg-config not available; voice codec disabled (Phase 5 scaffold remains active)")
endif()

# ZLib -- static on Windows so no zlib1.dll is needed at runtime
# MSYS2: pacman -S mingw-w64-x86_64-zlib
find_package(ZLIB REQUIRED)
if(ZLIB_INCLUDE_DIR)
  pd_include_system_dirs("${ZLIB_INCLUDE_DIR}")
endif()
if(WIN32)
  if(EXISTS "${_msys_lib}/libz.a")
    message(STATUS "ZLIB: Static linking via ${_msys_lib}/libz.a")
    list(APPEND LIBS "${_msys_lib}/libz.a")
  elseif(ZLIB_INCLUDE_DIR AND EXISTS "${ZLIB_INCLUDE_DIR}/../lib/libz.a")
    get_filename_component(_z_a "${ZLIB_INCLUDE_DIR}/../lib/libz.a" ABSOLUTE)
    message(STATUS "ZLIB: Static linking via ${_z_a}")
    list(APPEND LIBS "${_z_a}")
  else()
    message(WARNING "ZLIB: No libz.a at ${_msys_lib}, falling back to dynamic (will need zlib1.dll)")
    list(APPEND LIBS "${ZLIB_LIBRARY}")
  endif()
else()
  list(APPEND LIBS "${ZLIB_LIBRARY}")
endif()

# libcurl -- static linking on Windows via MSYS2 MinGW64; dynamic fallback elsewhere.
# MSYS2: pacman -S mingw-w64-x86_64-curl
add_definitions(-DCURL_STATICLIB)
if(WIN32)
  if(EXISTS "${_msys_lib}/libcurl.a")
    message(STATUS "CURL: Static linking via ${_msys_lib}/libcurl.a")
    list(APPEND LIBS "${_msys_lib}/libcurl.a")
    foreach(_slib libssl.a libcrypto.a)
      if(EXISTS "${_msys_lib}/${_slib}")
        list(APPEND LIBS "${_msys_lib}/${_slib}")
      endif()
    endforeach()
    foreach(_slib libbrotlidec.a libbrotlicommon.a)
      if(EXISTS "${_msys_lib}/${_slib}")
        list(APPEND LIBS "${_msys_lib}/${_slib}")
      endif()
    endforeach()
    foreach(_slib libnghttp3.a libngtcp2_crypto_ossl.a libngtcp2.a libnghttp2.a)
      if(EXISTS "${_msys_lib}/${_slib}")
        list(APPEND LIBS "${_msys_lib}/${_slib}")
      endif()
    endforeach()
    foreach(_slib libssh2.a libidn2.a libpsl.a libzstd.a libunistring.a libintl.a libiconv.a)
      if(EXISTS "${_msys_lib}/${_slib}")
        list(APPEND LIBS "${_msys_lib}/${_slib}")
      endif()
    endforeach()
  else()
    find_package(CURL REQUIRED)
    if(CURL_INCLUDE_DIRS)
      pd_include_system_dirs("${CURL_INCLUDE_DIRS}")
    elseif(CURL_INCLUDE_DIR)
      pd_include_system_dirs("${CURL_INCLUDE_DIR}")
    endif()
    if(CURL_LIBRARIES)
      list(APPEND LIBS "${CURL_LIBRARIES}")
    elseif(CURL_LIBRARY)
      list(APPEND LIBS "${CURL_LIBRARY}")
    endif()
  endif()
else()
  find_package(CURL REQUIRED)
  if(CURL_INCLUDE_DIRS)
    pd_include_system_dirs("${CURL_INCLUDE_DIRS}")
  elseif(CURL_INCLUDE_DIR)
    pd_include_system_dirs("${CURL_INCLUDE_DIR}")
  endif()
  if(CURL_LIBRARIES)
    list(APPEND LIBS "${CURL_LIBRARIES}")
  elseif(CURL_LIBRARY)
    list(APPEND LIBS "${CURL_LIBRARY}")
  endif()
endif()

# GL
if(NOT GL_LIBRARY)
  if(WIN32)
    set(GL_LIBRARY opengl32)
  elseif(NINTENDO_SWITCH)
    set(GL_LIBRARY EGL glapi drm_nouveau)
  elseif(APPLE)
    set(GL_LIBRARY "-framework OpenGL")
  else()
    set(GL_LIBRARY GL)
  endif()
endif()
list(APPEND LIBS "${GL_LIBRARY}")

# Platform-specific extra libs
if(NOT EXTRA_LIBRARIES)
  if(WIN32)
    set(EXTRA_LIBRARIES ws2_32 winmm dbghelp iphlpapi crypt32 bcrypt wldap32 secur32 -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
    add_definitions(-DMINIUPNP_STATICLIB)
  elseif(NINTENDO_SWITCH)
    set(EXTRA_LIBRARIES stdc++ pthread)
  else()
    set(EXTRA_LIBRARIES stdc++ m dl)
  endif()
endif()
list(APPEND LIBS "${EXTRA_LIBRARIES}")

# Sources
#
# CONFIGURE_DEPENDS makes CMake re-evaluate the glob on every build so new
# port/src/*.c, port/*.cpp, and src/game/*.c files land in incremental
# builds without a manual reconfigure. Without this, adding a new emitter
# (e.g. romextract_pdhead.c during catalog universality pivot Step 2,
# 2026-05-03) link-fails on a stale Build/ tree because CMake never saw it.

file(GLOB_RECURSE SRC_PORT     CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/*.c")
file(GLOB_RECURSE SRC_PORT_CXX CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/*.cpp")

file(GLOB_RECURSE SRC_GAME     CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/*.c")

file(GLOB SRC_LIB_MP3 "${CMAKE_SOURCE_DIR}/src/lib/mp3/*.c")
file(GLOB SRC_LIB_NAUDIO "${CMAKE_SOURCE_DIR}/src/lib/naudio/*.c")
file(GLOB SRC_LIB_ULTRA_AUDIO "${CMAKE_SOURCE_DIR}/src/lib/ultra/audio/*.c")
file(GLOB SRC_LIB_GU "${CMAKE_SOURCE_DIR}/src/lib/ultra/gu/*.c")
set(SRC_LIB
  src/lib/ultra/io/vimodentsclan1.c
  src/lib/ultra/io/vimodepallan1.c
  src/lib/ultra/io/vimodempallan1.c
  src/lib/ultra/io/vitbl.c
  src/lib/ailist.c
  src/lib/anim.c
  src/lib/args.c
  src/lib/audiodma.c
  src/lib/audiomgr.c
  src/lib/base.c
  src/lib/capsule.c
  src/lib/collision.c
  src/lib/meshcollision.c
  src/lib/debughud.c
  src/lib/dma.c
  src/lib/fault.c
  src/lib/joy.c
  src/lib/lib_2f490_c.c
  src/lib/lib_2fc60.c
  src/lib/lib_3e3e0.c
  src/lib/lib_04f60nb.c
  src/lib/lib_17ce0.c
  src/lib/lib_034d0.c
  src/lib/lib_39c80.c
  src/lib/lib_15850.c
  src/lib/mema.c
  src/lib/memp.c
  src/lib/mempc.c
  src/lib/model.c
  src/lib/modelasm_c.c
  src/lib/mp3.c
  src/lib/mtx_c.c
  src/lib/mtx.c
  src/lib/music.c
  src/lib/path.c
  src/lib/profile.c
  src/lib/rdp.c
  src/lib/rmon.c
  src/lib/rng_c.c
  src/lib/rzip_c.c
  src/lib/snd.c
  src/lib/speaker.c
  src/lib/varsinit.c
  src/lib/vi.c
  src/textureconfig.c
)
list(PREPEND SRC_LIB "${CMAKE_SOURCE_DIR}")

# Exclude server-only files from client sources (they have their own main/globals/stubs)
list(FILTER SRC_PORT EXCLUDE REGEX ".*/server_main\\.c$")
list(FILTER SRC_PORT EXCLUDE REGEX ".*/server_stubs\\.c$")
list(FILTER SRC_PORT EXCLUDE REGEX ".*/server_bridge\\.c$")
list(FILTER SRC_PORT_CXX EXCLUDE REGEX ".*/server_gui\\.cpp$")

# Exclude standalone updater-utility sources (they have their own WinMain
# and link against a minimal subset of the project's libs — built as pd-updater)
list(FILTER SRC_PORT EXCLUDE REGEX ".*/updater_standalone/.*")

set(SRC
  ${SRC_GAME}
  ${SRC_PORT}
  ${SRC_PORT_CXX}
  ${SRC_LIB_MP3}
  ${SRC_LIB_NAUDIO}
  ${SRC_LIB_ULTRA_AUDIO}
  ${SRC_LIB_GU}
  ${SRC_LIB}
)

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/dist/windows/icon.rc")
  list(APPEND SRC "${CMAKE_SOURCE_DIR}/dist/windows/icon.rc")
endif()

# Asset files

generate_asset_headers("lang/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mklang" "en" JSON_HEADERS)
generate_asset_headers("pads/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mkpads" "" JSON_HEADERS)
generate_asset_headers("tiles/" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mktiles" "" JSON_HEADERS)
generate_asset_headers("animations.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mkanims" "" JSON_HEADERS)
generate_asset_headers("sequences.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mksequences" "" JSON_HEADERS)
generate_asset_headers("textures.json" "${CMAKE_SOURCE_DIR}/tools/assetmgr/mktextures" "" JSON_HEADERS)

add_custom_target(pd_headers ALL DEPENDS ${JSON_HEADERS})

# Game client target

set(BIN_NAME "PerfectDark")
add_executable(pd ${SRC})
add_dependencies(pd pd_headers)
target_link_libraries(pd ${LIBS})
set_target_properties(pd PROPERTIES OUTPUT_NAME "${BIN_NAME}")

# Stable release builds omit dev-only debug hotkeys (F6/F7/F12), dev HUD banners,
# and the Settings Debug tab. Prerelease/dev/local builds leave this OFF.
option(PD_STABLE_RELEASE "Stable channel: disable PD_DEV_BUILD features" OFF)
if(NOT PD_STABLE_RELEASE)
  target_compile_definitions(pd PRIVATE PD_DEV_BUILD=1)
endif()

# Precompiled header for the "core 4" game headers (C only -- C++ port files differ).
# types.h, ultra64.h, data.h, constants.h are included by >50% of game TUs.
# PCH saves parsing them ~350x per clean build (~5-8s on 24 cores).
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
  target_precompile_headers(pd PRIVATE
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/src/include/types.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/include/ultra64.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/src/include/data.h>"
    "$<$<COMPILE_LANGUAGE:C>:${CMAKE_SOURCE_DIR}/src/include/constants.h>"
  )
endif()

if(WIN32)
  set_target_properties(pd PROPERTIES WIN32_EXECUTABLE TRUE)
  # 8 MB stack: default 2 MB is too small for 31-bot matches where deep
  # collision/AI call chains consume significant stack per bot tick.
  target_link_options(pd PRIVATE -Wl,--stack,8388608)
endif()

# Dedicated server target -- MINIMAL source list, decoupled from game rendering.
# Only includes: networking, system, filesystem, server GUI, and stubs.

file(GLOB SRC_SERVER_MINIUPNPC "${CMAKE_SOURCE_DIR}/port/external/miniupnpc/src/*.c")

set(SRC_SERVER
  # Server entry point + comprehensive stubs for ALL game dependencies
  ${CMAKE_SOURCE_DIR}/port/src/server_main.c
  ${CMAKE_SOURCE_DIR}/port/src/server_stubs.c
  ${CMAKE_SOURCE_DIR}/port/src/server_bridge.c
  ${CMAKE_SOURCE_DIR}/port/src/net/net_server_callbacks.c

  # Core networking ONLY (not netmenu.c -- that's client UI)
  ${CMAKE_SOURCE_DIR}/port/src/net/net.c
  ${CMAKE_SOURCE_DIR}/port/src/net/netmsg.c
  ${CMAKE_SOURCE_DIR}/port/src/net/netbuf.c
  ${CMAKE_SOURCE_DIR}/port/src/net/netlobby.c
  ${CMAKE_SOURCE_DIR}/port/src/net/netupnp.c
  ${CMAKE_SOURCE_DIR}/port/src/net/netstun.c
  ${CMAKE_SOURCE_DIR}/port/src/net/netholepunch.c
  ${CMAKE_SOURCE_DIR}/port/src/net/netdistrib.c
  ${CMAKE_SOURCE_DIR}/port/src/net/netmanifest.c
  ${CMAKE_SOURCE_DIR}/port/src/net/sessioncatalog.c
  ${CMAKE_SOURCE_DIR}/port/src/scene_transition.c

  # Asset catalog -- server needs it to enumerate/serve mod components (D3R-9)
  ${CMAKE_SOURCE_DIR}/port/src/assetcatalog.c
  ${CMAKE_SOURCE_DIR}/port/src/assetcatalog_base.c
  ${CMAKE_SOURCE_DIR}/port/src/assetcatalog_base_extended.c
  ${CMAKE_SOURCE_DIR}/port/src/assetcatalog_scanner.c
  ${CMAKE_SOURCE_DIR}/port/src/assetcatalog_api.c
  ${CMAKE_SOURCE_DIR}/port/src/assetcatalog_deps.c

  # INV-1: pure validators backing the _Checked catalog accessor variants.
  # assetcatalog_api.c references catalogCheckedValidate*; without this
  # source link, pd-server fails at the linker stage.
  ${CMAKE_SOURCE_DIR}/port/src/catalog_checked.c

  # Catalog Gate 3 F1: catalog manager for character heads.
  # assetcatalog_api.c (F2) routes catalogGetHeadX accessors through the
  # manager, so the server build needs this link too. Manager file is
  # PD_SERVER-aware (random-gender pickers + modeldef lazy-load gated).
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_heads.c
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_heads_pure.c

  # Catalog Gate 3 Bodies F1: catalog manager for character bodies.
  # assetcatalog_api.c (F2) routes catalogGetBodyX accessors through the
  # manager, so the server build needs this link too. Manager file is
  # PD_SERVER-aware (modeldef lazy-load gated).
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_bodies.c
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_bodies_pure.c

  # Catalog Gate 3 Arenas F1: catalog manager for MP arenas.
  # The pd-server build includes the manager so loader_pdbase.c
  # (which links into pd-server's manager-side accessors) compiles
  # cleanly. Manager has no modeldef cache, no rng dependency.
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_arenas.c
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_arenas_pure.c

  # INV-4: pure helpers for matchOptionsForceBit / matchOptionsRestoreUserOriginal.
  # matchsetup.c references them at matchStart restore; without this
  # source link pd-server would fail.
  ${CMAKE_SOURCE_DIR}/port/src/options_forced.c

  # UPnP library
  ${SRC_SERVER_MINIUPNPC}

  # Minimal system (no pdsched, no console, no mpsetups, no romdata,
  # no modmgr, no libultra -- all stubbed in server_stubs.c)
  ${CMAKE_SOURCE_DIR}/port/src/system.c
  ${CMAKE_SOURCE_DIR}/port/src/config.c
  ${CMAKE_SOURCE_DIR}/port/src/fs.c
  ${CMAKE_SOURCE_DIR}/port/src/utils.c
  ${CMAKE_SOURCE_DIR}/port/src/crash.c
  ${CMAKE_SOURCE_DIR}/port/src/crashbreadcrumb.c

  # Server GUI (ImGui, SDL2, OpenGL -- self-contained)
  ${CMAKE_SOURCE_DIR}/port/fast3d/server_gui.cpp
  ${CMAKE_SOURCE_DIR}/port/fast3d/glad/glad.c

  # ENet (external library)
  ${CMAKE_SOURCE_DIR}/port/external/enet.c

  # C.5: navmesh spawn fallback for SP maps used as MP arenas
  ${CMAKE_SOURCE_DIR}/port/src/navspawn.c

  # Update system (D13) -- server has its own update path + --check-update CLI
  ${CMAKE_SOURCE_DIR}/port/src/updater.c
  ${CMAKE_SOURCE_DIR}/port/src/sha256.c
  ${CMAKE_SOURCE_DIR}/port/src/ed25519.c
  ${CMAKE_SOURCE_DIR}/port/src/savemigrate.c

  # SPF-1: Server Platform Foundation (hub lifecycle, room system, identity, phonetic)
  ${CMAKE_SOURCE_DIR}/port/src/hub.c
  ${CMAKE_SOURCE_DIR}/port/src/room.c
  ${CMAKE_SOURCE_DIR}/port/src/identity.c
  ${CMAKE_SOURCE_DIR}/port/src/phonetic.c

  # Wave 3 Batch A: server admin + persistent bans (MASTER-C2)
  ${CMAKE_SOURCE_DIR}/port/src/server_admin.c
  ${CMAKE_SOURCE_DIR}/port/src/server_bans.c

  # Connect code (IP:port ↔ word-based codes)
  ${CMAKE_SOURCE_DIR}/port/src/connectcode.c

  # B-12 Phase 3: participant pool (authoritative slot store). Server needs
  # mpAddParticipantAt / mpIsParticipantActive / wire encode helpers.
  ${CMAKE_SOURCE_DIR}/src/game/mplayer/participant.c
)

# ImGui sources for server GUI
file(GLOB SRC_SERVER_IMGUI "${CMAKE_SOURCE_DIR}/port/fast3d/imgui/*.cpp")
list(APPEND SRC_SERVER ${SRC_SERVER_IMGUI})

if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/dist/windows/icon.rc")
  list(APPEND SRC_SERVER "${CMAKE_SOURCE_DIR}/dist/windows/icon.rc")
endif()

set(SERVER_BIN_NAME "PerfectDarkServer")
add_executable(pd-server ${SRC_SERVER})
add_dependencies(pd-server pd_headers)
target_link_libraries(pd-server ${LIBS})
if(WIN32)
    target_link_libraries(pd-server psapi)
endif()
set_target_properties(pd-server PROPERTIES OUTPUT_NAME "${SERVER_BIN_NAME}")
# Server defines -- used by shared headers
target_compile_definitions(pd-server PRIVATE PD_SERVER=1)

# ---------------------------------------------------------------------------
# logview: standalone log filter CLI tool
# ---------------------------------------------------------------------------
add_executable(logview "${CMAKE_SOURCE_DIR}/tools/logview.c")
target_include_directories(logview PRIVATE "${CMAKE_SOURCE_DIR}/port/include")
set_target_properties(logview PROPERTIES OUTPUT_NAME "logview")

# ---------------------------------------------------------------------------
# pd-updater: standalone Updater.exe that ships next to PerfectDark.exe.
# If a release breaks the main client/server executables, the end user can
# still launch Updater.exe to pull and install a fixed build from GitHub
# Releases. Self-contained Win32 GUI -- no MFC/WPF, no external deps beyond
# libcurl (statically linked, same chain as the game).
# ---------------------------------------------------------------------------
if(WIN32)
  set(SRC_PD_UPDATER
    ${CMAKE_SOURCE_DIR}/port/src/updater_standalone/updater_gui.c
    ${CMAKE_SOURCE_DIR}/port/src/sha256.c
    ${CMAKE_SOURCE_DIR}/port/src/ed25519.c
  )
  # Embed the application manifest (Common Controls v6 + DPI awareness)
  if(EXISTS "${CMAKE_SOURCE_DIR}/port/src/updater_standalone/updater_gui.rc")
    list(APPEND SRC_PD_UPDATER
      ${CMAKE_SOURCE_DIR}/port/src/updater_standalone/updater_gui.rc)
  endif()

  add_executable(pd-updater ${SRC_PD_UPDATER})
  add_dependencies(pd-updater pd_headers)
  set_target_properties(pd-updater PROPERTIES
    OUTPUT_NAME "Updater"
    WIN32_EXECUTABLE TRUE)

  target_include_directories(pd-updater PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/PR"
    "${CMAKE_SOURCE_DIR}/port/include"
    "${CMAKE_BINARY_DIR}/port/include")

  target_compile_definitions(pd-updater PRIVATE
    CURL_STATICLIB
    _LANGUAGE_C=1)

  # Reuse the same static libcurl + TLS + compression chain as the main
  # client; this keeps the updater zero-DLL compliant.
  set(_updater_libs "")
  if(EXISTS "${_msys_lib}/libcurl.a")
    list(APPEND _updater_libs "${_msys_lib}/libcurl.a")
    foreach(_slib libssl.a libcrypto.a)
      if(EXISTS "${_msys_lib}/${_slib}")
        list(APPEND _updater_libs "${_msys_lib}/${_slib}")
      endif()
    endforeach()
    foreach(_slib libbrotlidec.a libbrotlicommon.a)
      if(EXISTS "${_msys_lib}/${_slib}")
        list(APPEND _updater_libs "${_msys_lib}/${_slib}")
      endif()
    endforeach()
    foreach(_slib libnghttp3.a libngtcp2_crypto_ossl.a libngtcp2.a libnghttp2.a)
      if(EXISTS "${_msys_lib}/${_slib}")
        list(APPEND _updater_libs "${_msys_lib}/${_slib}")
      endif()
    endforeach()
    foreach(_slib libssh2.a libidn2.a libpsl.a libzstd.a libunistring.a libintl.a libiconv.a)
      if(EXISTS "${_msys_lib}/${_slib}")
        list(APPEND _updater_libs "${_msys_lib}/${_slib}")
      endif()
    endforeach()
    if(EXISTS "${_msys_lib}/libz.a")
      list(APPEND _updater_libs "${_msys_lib}/libz.a")
    endif()
  else()
    find_package(CURL REQUIRED)
    list(APPEND _updater_libs CURL::libcurl)
  endif()

  # --start-group / --end-group wraps the static libcurl dependency chain so
  # ld resolves circular refs (libngtcp2_crypto_ossl depends on libssl quic
  # symbols, which come from an earlier archive in the line). Without the
  # group the link fails with undefined SSL_set_quic_tls_cbs.
  target_link_libraries(pd-updater PRIVATE
    -Wl,--start-group
    ${_updater_libs}
    -Wl,--end-group
    # Common Controls (list view, progress bar)
    comctl32
    # Winsock + crypto + LDAP + secur32 for libcurl's static TLS chain
    ws2_32 winmm crypt32 bcrypt wldap32 secur32 iphlpapi
    # GUI + shell
    gdi32 user32 shell32 ole32 uuid
    # Statically link the C++/pthread runtime so there are no DLL deps
    -static-libgcc -static-libstdc++
    -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()

# Additional commands for specific platforms.

if(NINTENDO_SWITCH)
  add_custom_command(TARGET pd POST_BUILD
    COMMAND nacptool --create "Perfect Dark" "fgsfds and contributors" "${VERSION_ROMID}/${VERSION_BRANCH}/${VERSION_HASH}" "${BIN_NAME}.nacp"
    COMMAND aarch64-none-elf-strip -o ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.elf
    COMMAND elf2nro ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.nro --icon=${CMAKE_SOURCE_DIR}/dist/nswitch/icon.jpg --nacp=${BIN_NAME}.nacp
  )
endif()

# S484 F12.4: deploy base/*.pdbase archives into the runtime data root.
# fsFileLoad("base/weapons.pdbase", ...) resolves against fsGetBaseDir()
# (typically "<BuildDir>/data"), so the source-tree base/ files have to
# be copied into <BuildDir>/data/base/ for the loader to find them.
#
# Why per-file add_custom_command(OUTPUT ...) + custom_target(ALL ...) and
# NOT add_custom_command(TARGET pd POST_BUILD ...): POST_BUILD only fires
# when the target itself is relinked. An incremental rebuild that doesn't
# touch any pd source skips POST_BUILD, leaving stale Build/data/base/
# (this is what bit Mike's first F12 playtest -- pd was already linked,
# POST_BUILD never ran, the .pdbase deploy was missed). The pattern
# below makes ninja track each source file as a real dependency: the
# copy fires whenever the source changes OR the destination is missing.
if(EXISTS "${CMAKE_SOURCE_DIR}/base")
  file(GLOB_RECURSE PDBASE_SRC_FILES "${CMAKE_SOURCE_DIR}/base/*")
  set(PDBASE_DEPLOYED_FILES)
  foreach(src ${PDBASE_SRC_FILES})
    file(RELATIVE_PATH rel "${CMAKE_SOURCE_DIR}/base" "${src}")
    set(dst "${CMAKE_BINARY_DIR}/data/base/${rel}")
    add_custom_command(OUTPUT "${dst}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${src}" "${dst}"
      DEPENDS "${src}"
      COMMENT "Deploying base/${rel} -> Build/data/base/${rel}"
      VERBATIM)
    list(APPEND PDBASE_DEPLOYED_FILES "${dst}")
  endforeach()
  add_custom_target(pdbase_deploy ALL DEPENDS ${PDBASE_DEPLOYED_FILES})
  add_dependencies(pd pdbase_deploy)
endif()

# Windows: no DLL copy step — all third-party libraries are statically linked.
# SDL2, zlib, libcurl (+ full TLS chain), libgcc, libstdc++, and libwinpthread
# are all compiled into PerfectDark.exe and PerfectDarkServer.exe.
# The only runtime dependency on Windows is opengl32.dll, which is a system DLL
# present on every Windows installation and must NOT be statically linked.
# See context/designs/static-link-dll-elimination-2026-04-13.md for the full audit.

# Priority M / B-238 / M-2.1: Windows Shell property handler for .pdmod.
# Built when targeting Windows; emits Build/PD2ModPropHandler.dll. The
# subdirectory's CMakeLists.txt no-ops on non-Windows.
add_subdirectory("${CMAKE_SOURCE_DIR}/tools/pdmod_prophandler")

# ---------------------------------------------------------------------------
# pd-tests: unit/roundtrip/state-machine test runner.
#
# Self-contained: does NOT link the full game (no SDL, no GL, no ImGui, no
# ENet). Cherry-picks a handful of pure source files from src/ and port/src/
# and links them against tests/main.cpp + tests/test_*.cpp under Catch2.
#
# Build:  ninja -C Build pd-tests
# Run:    ./Build/pd-tests          (all tests)
#         ./Build/pd-tests "[tag]"  (subset by tag)
#
# See context/designs/testing-framework-2026-04-26.md for the framework
# design, coverage roadmap, and conventions.
# ---------------------------------------------------------------------------
set(SRC_TESTS
  # Catch2 entry point + test cases
  ${CMAKE_SOURCE_DIR}/tests/main.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_smoke.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_netbuf.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_net_lifecycle_static.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_versions.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_versions_pin.c
  ${CMAKE_SOURCE_DIR}/tests/test_connectcode.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_savebuffer.cpp
  ${CMAKE_SOURCE_DIR}/tests/savebuffer_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_save_migration.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_manifest.cpp
  ${CMAKE_SOURCE_DIR}/tests/manifest_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_random_pool.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_spawn_weapon_mode.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_swarm_boid_sim.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_bondgun_cache.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_catalog_checked.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_spawn_predicate.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_bodies_headcount.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_integrated_head_guard.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_options_forced.cpp

  # Cohort 2: state-machine + reachability + right-stick scroll spec.
  # Pure-C mirrors of inputctx + menupool live under tests/ so the runner
  # stays SDL-free; @SYNC markers point back to the real source lines.
  ${CMAKE_SOURCE_DIR}/tests/inputctx_pure.c
  ${CMAKE_SOURCE_DIR}/tests/menupool_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_input_authority.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_menu_stack.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_menu_reachability.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_menu_graph.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_right_stick_scroll.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_social_toggle_imc.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_spawn_weapon_resolved.cpp

  # Cohort 1 (input universality and transitions, 2026-04-27): action map
  # flush + classifier invariants. Pure-C mirror at tests/actionmap_pure.{c,h}.
  ${CMAKE_SOURCE_DIR}/tests/actionmap_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_actionmap_flush.cpp

  # Phase 2 fix #1 (input-menu pillar, 2026-05-01): canonical Press-vs-Hold
  # contract. ampWasTap mirrors actionWasTap; bondmove.c sites migrated.
  ${CMAKE_SOURCE_DIR}/tests/test_press_vs_hold.cpp

  # Phase 2 fix #2 (input-menu pillar, 2026-05-01): innermost-scrollable
  # walker for the right-stick scroll target.
  ${CMAKE_SOURCE_DIR}/tests/nested_scroll_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_nested_scroll.cpp

  # Cohort 2 (input universality and transitions, 2026-04-27): typed
  # Input Layer Stack. Push / pop / abort / exclusivity invariants via
  # pure-C mirror at tests/inputlayer_pure.{c,h}.
  ${CMAKE_SOURCE_DIR}/tests/inputlayer_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_input_layer_stack.cpp

  # Cohort 3 (input universality and transitions, 2026-04-27): Scene
  # Manager dispatcher. SceneEvent -> inputLayer push/pop translation
  # invariants via pure-C mirror at tests/scene_pure.{c,h}.
  ${CMAKE_SOURCE_DIR}/tests/scene_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_scene_dispatch.cpp

  # Cohort 4 (input universality and transitions, 2026-04-27): cutscene
  # flash fix invariants (K.2 + K.6). Mission 1 obj 2 bug repro.
  ${CMAKE_SOURCE_DIR}/tests/test_cutscene_layer.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_vehicle_observer_layer.cpp
  ${CMAKE_SOURCE_DIR}/tests/test_catalog_provider_static.cpp

  # Linker stubs for symbols the cherry-picked sources reference but the
  # test binary should not actually invoke.
  ${CMAKE_SOURCE_DIR}/tests/stubs.c

  # Cherry-picked code under test. Only fully pure files (no globals,
  # no SDL/GL/ENet dependencies) live here. Everything else is tested
  # through stubbed callees or test-local copies, see the design doc.
  ${CMAKE_SOURCE_DIR}/port/src/net/netbuf.c
  ${CMAKE_SOURCE_DIR}/port/src/connectcode.c
  ${CMAKE_SOURCE_DIR}/port/src/bondgun_cache.c
  ${CMAKE_SOURCE_DIR}/port/src/catalog_checked.c
  ${CMAKE_SOURCE_DIR}/port/src/spawn_predicate.c
  ${CMAKE_SOURCE_DIR}/port/src/bodies_headcount.c
  ${CMAKE_SOURCE_DIR}/port/src/options_forced.c

  # S484 F1: catalog manager for weapons (pure validators + variant spec).
  # Live router lives in port/src/catalog_mgr_weapons.c and pulls in
  # g_Weapons[]/g_AibotWeaponPreferences[]/invaimsettings_default/
  # invnoisesettings_silent; only the pure layer is linked here so
  # pd-tests stays globals-free.
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_weapons_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_catalog_mgr_weapons_api.cpp

  # Catalog Gate 3 F1: catalog manager for character heads (pure validators).
  # Live router lives in port/src/catalog_mgr_heads.c and pulls in
  # g_HeadsAndBodies[]; only the pure layer is linked here so pd-tests
  # stays globals-free.
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_heads_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_catalog_mgr_heads_api.cpp

  # Catalog Gate 3 Bodies F1: catalog manager for character bodies (pure validators).
  # Live router lives in port/src/catalog_mgr_bodies.c and pulls in
  # g_HeadsAndBodies[]; only the pure layer is linked here so pd-tests
  # stays globals-free.
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_bodies_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_catalog_mgr_bodies_api.cpp

  # Catalog Gate 3 Arenas F1: catalog manager for MP arenas (pure validators).
  # Live router lives in port/src/catalog_mgr_arenas.c and walks
  # ASSET_ARENA catalog rows; only the pure layer is linked here so
  # pd-tests stays globals-free.
  ${CMAKE_SOURCE_DIR}/port/src/catalog_mgr_arenas_pure.c
  ${CMAKE_SOURCE_DIR}/tests/test_catalog_mgr_arenas_api.cpp

  # Catalog Gate 3 Arenas F11 + F12: pin the .pdbase archive shape +
  # the parser / parity-check static contract. Static-text grep against
  # base/arenas.pdbase + port/src/loader_pdbase.c + catalog_mgr_arenas.c
  # + main.c.
  ${CMAKE_SOURCE_DIR}/tests/test_loader_pdbase_arenas.cpp

  # Catalog Gate 3 Arenas F13: grep-guard test. Pins that no NEW
  # direct g_MpArenas[idx].field reads exist outside the registration
  # loop in assetcatalog_base.c and the static literal definitions
  # (setup.c + server_stubs.c). The 2026-04-26 selector-pool migration
  # eliminated the live-UI consumers; the 2026-05-02 Gate 3 data
  # migration retired the parity bridge.
  ${CMAKE_SOURCE_DIR}/tests/test_arena_direct_reads_audit.cpp

  # Catalog Phase 3 Pass B Slice 10 (2026-05-02): voice retag
  # static-text pins. Verifies the seven voice-config slot set,
  # the registration loop's category-passing shape, the PD_SERVER
  # guard, and the g_NumAudioRussMappings symbol surface.
  ${CMAKE_SOURCE_DIR}/tests/test_audio_voice_retag.cpp

  # S484 F2: pin weaponFindById's migration to the catalog manager.
  ${CMAKE_SOURCE_DIR}/tests/test_weapon_findbyid_migrated.cpp

  # S484 F3+: pin per-file absence of direct g_Weapons[] reads. The
  # list of audited files grows as each F-step lands.
  ${CMAKE_SOURCE_DIR}/tests/test_weapon_direct_reads_audit.cpp

  # S484 F10: pin the .pdbase loader scaffold's static contract.
  ${CMAKE_SOURCE_DIR}/tests/test_loader_pdbase_scan.cpp

  # Phase 3 Pass B Slice 13: pin UI chrome paths to base/ui/textures.
  # Static-text grep against pdgui_theme.cpp + pdgui_theme.h.  Audit at
  # context/audits/catalog-phase3-passb-slice13-uichrome-2026-05-02.md.
  ${CMAKE_SOURCE_DIR}/tests/test_uichrome_paths_pin.cpp

  # Phase 3 Pass D: pin self-heal hardening surfaces.  Static-text grep
  # against romextract.{c,h} + main.c locks the LOUDFAIL.LOAD channel,
  # the DATA INTEGRITY summary line, the data/_quarantine/ migration,
  # the toast-emit cap, and the boot wiring order.  Audit at
  # context/audits/catalog-phase3-passd-self-heal-2026-05-02.md.
  ${CMAKE_SOURCE_DIR}/tests/test_romextract_passd.cpp
)

add_executable(pd-tests ${SRC_TESTS})
add_dependencies(pd-tests pd_headers)
target_include_directories(pd-tests PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/include/PR
  ${CMAKE_SOURCE_DIR}/src/include
  ${CMAKE_SOURCE_DIR}/port/include
  ${CMAKE_BINARY_DIR}/port/include
  ${CMAKE_BINARY_DIR}/src/generated/${ROMID}
)
target_compile_definitions(pd-tests PRIVATE
  PD_TESTS=1
  AVOID_UB=1
  _LANGUAGE_C=1
  PAL=0
  VERSION=2
  ROM_SIZE=32
  PIRACYCHECKS=0
  MATCHING=0
)
# Tests don't need the full PD warning fence, but match enough so genuine
# bugs in test code aren't silently dropped.
target_compile_options(pd-tests PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>
)
if(WIN32)
  target_link_libraries(pd-tests PRIVATE -static-libgcc -static-libstdc++
    -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()
set_target_properties(pd-tests PROPERTIES OUTPUT_NAME "pd-tests")
