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
set(VERSION_SEM_PATCH 8 CACHE STRING "Semantic version revision")

set(VERSION_SEM_STRING "${VERSION_SEM_MAJOR}.${VERSION_SEM_MINOR}.${VERSION_SEM_PATCH}")

message(STATUS "Version: ${VERSION_ROMID}, v${VERSION_SEM_STRING}, hash ${VERSION_HASH}, branch ${VERSION_BRANCH}")

configure_file("${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in" "${CMAKE_BINARY_DIR}/port/include/versioninfo.h" @ONLY)

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

# Debug info ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â explicit -g ensures DWARF symbols are embedded regardless
# of CMAKE_BUILD_TYPE. Without this, addr2line returns ?? on MinGW.
add_compile_options(-g)

# Common CFLAGS
add_compile_options(
  -fno-inline-functions
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -include "versioninfo.h"
)

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
    list(APPEND LIBS imm32 version setupapi ole32 oleaut32 gdi32 winmm)
  elseif(SDL2_INCLUDE_DIR)
    get_filename_component(_sdl2_lib "${SDL2_INCLUDE_DIR}/../lib" ABSOLUTE)
    if(EXISTS "${_sdl2_lib}/libSDL2.a" AND EXISTS "${_sdl2_lib}/libSDL2main.a")
      message(STATUS "SDL2: Static linking via ${_sdl2_lib}/libSDL2.a")
      list(APPEND LIBS mingw32 "${_sdl2_lib}/libSDL2main.a" "${_sdl2_lib}/libSDL2.a")
      list(APPEND LIBS imm32 version setupapi ole32 oleaut32 gdi32 winmm)
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

# ZLib -- static on Windows so no zlib1.dll is needed at runtime
# MSYS2: pacman -S mingw-w64-x86_64-zlib
find_package(ZLIB REQUIRED)
if(ZLIB_INCLUDE_DIR)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIR}")
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

# libcurl -- dynamic linking on Windows (ships libcurl-4.dll with the game).
# Static linking of curl + OpenSSL + 30 transitive deps causes the Windows GDI
# subsystem to initialize before the GPU's OpenGL ICD, resulting in GL 1.1
# fallback. Dynamic linking avoids this entirely and curl handles its own SSL
# via the system-installed OpenSSL DLLs or Windows Schannel.
# MSYS2: pacman -S mingw-w64-x86_64-curl
find_package(CURL REQUIRED)
if(CURL_INCLUDE_DIRS)
  include_directories(SYSTEM "${CURL_INCLUDE_DIRS}")
elseif(CURL_INCLUDE_DIR)
  include_directories(SYSTEM "${CURL_INCLUDE_DIR}")
endif()
if(CURL_LIBRARIES)
  list(APPEND LIBS "${CURL_LIBRARIES}")
elseif(CURL_LIBRARY)
  list(APPEND LIBS "${CURL_LIBRARY}")
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
    set(EXTRA_LIBRARIES ws2_32 winmm dbghelp iphlpapi -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
    add_definitions(-DMINIUPNP_STATICLIB)
  elseif(NINTENDO_SWITCH)
    set(EXTRA_LIBRARIES stdc++ pthread)
  else()
    set(EXTRA_LIBRARIES stdc++ m dl)
  endif()
endif()
list(APPEND LIBS "${EXTRA_LIBRARIES}")

# Sources

file(GLOB_RECURSE SRC_PORT "${CMAKE_SOURCE_DIR}/port/*.c")
file(GLOB_RECURSE SRC_PORT_CXX "${CMAKE_SOURCE_DIR}/port/*.cpp")

file(GLOB_RECURSE SRC_GAME "${CMAKE_SOURCE_DIR}/src/game/*.c")

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
if(WIN32)
  set_target_properties(pd PROPERTIES WIN32_EXECUTABLE TRUE)
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
  ${CMAKE_SOURCE_DIR}/port/src/net/netdistrib.c

  # Asset catalog -- server needs it to enumerate/serve mod components (D3R-9)
  ${CMAKE_SOURCE_DIR}/port/src/assetcatalog.c
  ${CMAKE_SOURCE_DIR}/port/src/assetcatalog_scanner.c

  # UPnP library
  ${SRC_SERVER_MINIUPNPC}

  # Minimal system (no pdsched, no console, no mpsetups, no romdata,
  # no modmgr, no libultra -- all stubbed in server_stubs.c)
  ${CMAKE_SOURCE_DIR}/port/src/system.c
  ${CMAKE_SOURCE_DIR}/port/src/config.c
  ${CMAKE_SOURCE_DIR}/port/src/fs.c
  ${CMAKE_SOURCE_DIR}/port/src/utils.c
  ${CMAKE_SOURCE_DIR}/port/src/crash.c

  # Server GUI (ImGui, SDL2, OpenGL -- self-contained)
  ${CMAKE_SOURCE_DIR}/port/fast3d/server_gui.cpp
  ${CMAKE_SOURCE_DIR}/port/fast3d/glad/glad.c

  # ENet (external library)
  ${CMAKE_SOURCE_DIR}/port/external/enet.c

  # Update system (D13) -- server has its own update path + --check-update CLI
  ${CMAKE_SOURCE_DIR}/port/src/updater.c
  ${CMAKE_SOURCE_DIR}/port/src/sha256.c
  ${CMAKE_SOURCE_DIR}/port/src/savemigrate.c

  # SPF-1: Server Platform Foundation (hub lifecycle, room system, identity, phonetic)
  ${CMAKE_SOURCE_DIR}/port/src/hub.c
  ${CMAKE_SOURCE_DIR}/port/src/room.c
  ${CMAKE_SOURCE_DIR}/port/src/identity.c
  ${CMAKE_SOURCE_DIR}/port/src/phonetic.c

  # Connect code (IP:port ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â word-based codes)
  ${CMAKE_SOURCE_DIR}/port/src/connectcode.c
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
set_target_properties(pd-server PROPERTIES OUTPUT_NAME "${SERVER_BIN_NAME}")
# Server defines -- used by shared headers
target_compile_definitions(pd-server PRIVATE PD_SERVER=1)

# Additional commands for specific platforms.

if(NINTENDO_SWITCH)
  add_custom_command(TARGET pd POST_BUILD
    COMMAND nacptool --create "Perfect Dark" "fgsfds and contributors" "${VERSION_ROMID}/${VERSION_BRANCH}/${VERSION_HASH}" "${BIN_NAME}.nacp"
    COMMAND aarch64-none-elf-strip -o ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.elf
    COMMAND elf2nro ${CMAKE_BINARY_DIR}/${BIN_NAME}.stripped.elf ${CMAKE_BINARY_DIR}/${BIN_NAME}.nro --icon=${CMAKE_SOURCE_DIR}/dist/nswitch/icon.jpg --nacp=${BIN_NAME}.nacp
  )
endif()

# Windows: copy required runtime DLLs next to the executables after build.
# Release packaging (release.ps1) handles DLLs separately for distribution.
if(WIN32)
  get_filename_component(_MINGW_BIN "${CMAKE_C_COMPILER}" DIRECTORY)
  set(_RUNTIME_DLLS
    "libcurl-4.dll"
    # libcurl transitive dependencies (MSYS2 MinGW64 dynamic curl):
    "libnghttp2-14.dll"
    "libnghttp3-9.dll"
    "libidn2-0.dll"
    "libbrotlidec.dll"
    "libbrotlicommon.dll"
    "libpsl-5.dll"
    "libssh2-1.dll"
    "libngtcp2-16.dll"
    "libngtcp2_crypto_ossl-0.dll"
    "libzstd.dll"
    "libssl-3-x64.dll"
    "libcrypto-3-x64.dll"
    "libunistring-5.dll"
    "libintl-8.dll"
    "libiconv-2.dll"
    "zlib1.dll"
  )
  foreach(_dll ${_RUNTIME_DLLS})
    if(EXISTS "${_MINGW_BIN}/${_dll}")
      add_custom_command(TARGET pd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_MINGW_BIN}/${_dll}" "$<TARGET_FILE_DIR:pd>"
        COMMENT "Copying ${_dll} to client build output")
      add_custom_command(TARGET pd-server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${_MINGW_BIN}/${_dll}" "$<TARGET_FILE_DIR:pd-server>"
        COMMENT "Copying ${_dll} to server build output")
    else()
      message(WARNING "${_dll} not found in ${_MINGW_BIN} -- game will not run without it")
    endif()
  endforeach()
endif()