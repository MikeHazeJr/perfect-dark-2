# Perfect Dark 2: build glue for bgfx, bx and bimg.
#
# Phase 2a step 1 of the 2.0 renderer (decision D5): classic Perfect Dark on
# bgfx's OpenGL backend. See context/designs/rendering/renderer-2.0-scope.md
# (D2, D5, D6, R10) and context/designs/rendering/bgfx-classic-gl.md.
#
# What this builds, all from the pinned submodules in third_party/bgfx:
#   pd_bx, pd_bimg, pd_bgfx  static libraries. bgfx is built with only its
#                            OpenGL 4.3 renderer, single-threaded, with the
#                            RenderDoc loader replaced by a stub and the NVAPI
#                            and DXGI sources left out, so the release binary
#                            contains no code that loads renderdoc.dll,
#                            nvapi64.dll or amd_ags_x64.dll (D6).
#   pd_shaderc               a lean shaderc host tool: only the GLSL path
#                            (fcpp preprocessor, no glslang, tint, DXC or
#                            glsl-optimizer). For GLSL 4.30 output shaderc
#                            passes the preprocessed source through
#                            unchanged, so no optimizer touches the math.
#   pd_bgfx_compile_shaders  a function that compiles .sc shaders ahead of
#                            time into C headers at build time.
#
# The top-level CMakeLists.txt adds this directory before it sets the game's
# global definitions (-DVERSION=..., -DPAL=...), include directories (which
# include a sys/ folder) and warning flags, so none of those reach third-party
# code.
#
# Pinned versions: the trio tested together by bgfx.cmake v1.139.9155-513
#   bx   36ad6131f4d292f62db64236d1d7f9f70eb26b13
#   bimg b43fea9eae0e6a98118454a6d17c6cb25f5e6403
#   bgfx 651c2031169bae79d81d5035295e1a4b886797ba

set(PD_BGFX_ROOT "${CMAKE_SOURCE_DIR}/third_party/bgfx")
set(BX_DIR   "${PD_BGFX_ROOT}/bx")
set(BIMG_DIR "${PD_BGFX_ROOT}/bimg")
set(BGFX_DIR "${PD_BGFX_ROOT}/bgfx")
set(BX_DIR   "${BX_DIR}"   PARENT_SCOPE)
set(BIMG_DIR "${BIMG_DIR}" PARENT_SCOPE)
set(BGFX_DIR "${BGFX_DIR}" PARENT_SCOPE)

# --- Fetch the pinned submodules on first configure --------------------------

set(_pd_bgfx_markers
  "${BX_DIR}/src/bx.cpp"
  "${BIMG_DIR}/src/image.cpp"
  "${BGFX_DIR}/src/bgfx.cpp"
  "${BGFX_DIR}/tools/shaderc/shaderc.cpp")

function(_pd_bgfx_sources_present out)
  set(present TRUE)
  foreach(marker IN LISTS _pd_bgfx_markers)
    if(NOT EXISTS "${marker}")
      set(present FALSE)
    endif()
  endforeach()
  set(${out} ${present} PARENT_SCOPE)
endfunction()

_pd_bgfx_sources_present(_pd_bgfx_present)
if(NOT _pd_bgfx_present)
  if(PD_GIT_EXECUTABLE)
    set(_pd_git "${PD_GIT_EXECUTABLE}")
  else()
    find_program(_pd_git git)
  endif()
  if(NOT _pd_git)
    message(FATAL_ERROR "bgfx: the third_party/bgfx submodules are missing and git was not found. "
      "Run: git submodule update --init --depth 1 -- third_party/bgfx/bx third_party/bgfx/bimg third_party/bgfx/bgfx")
  endif()
  message(STATUS "bgfx: fetching the pinned third_party/bgfx submodules")
  execute_process(
    COMMAND "${_pd_git}" -C "${CMAKE_SOURCE_DIR}" submodule update --init --depth 1 --
            third_party/bgfx/bx third_party/bgfx/bimg third_party/bgfx/bgfx
    RESULT_VARIABLE _pd_git_rc)
  _pd_bgfx_sources_present(_pd_bgfx_present)
  if(NOT _pd_git_rc EQUAL 0 OR NOT _pd_bgfx_present)
    message(FATAL_ERROR "bgfx: could not fetch the third_party/bgfx submodules (git exit ${_pd_git_rc}). "
      "Run: git submodule update --init --depth 1 -- third_party/bgfx/bx third_party/bgfx/bimg third_party/bgfx/bgfx")
  endif()
endif()

# --- Common third-party flags ------------------------------------------------

# Optimised, quiet, line-table debug info; no -ffast-math anywhere.
set(PD_BGFX_COMPILE_OPTIONS -O2 -g1 -w -fno-strict-aliasing)

# --- Source patches ----------------------------------------------------------
#
# Upstream files stay pristine in the submodules. A patched file is read,
# changed at exact anchors and written to the build tree, and the copy is
# compiled instead. Configure fails if an anchor is missing or not unique.

function(_pd_bgfx_patch_once text anchor replacement out)
  string(FIND "${text}" "${anchor}" first)
  if(first EQUAL -1)
    message(FATAL_ERROR "bgfx patch: anchor not found:\n${anchor}")
  endif()
  string(LENGTH "${anchor}" anchor_len)
  math(EXPR after "${first} + ${anchor_len}")
  string(SUBSTRING "${text}" ${after} -1 rest)
  string(FIND "${rest}" "${anchor}" second)
  if(NOT second EQUAL -1)
    message(FATAL_ERROR "bgfx patch: anchor is not unique:\n${anchor}")
  endif()
  string(REPLACE "${anchor}" "${replacement}" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

# Reads src with CRLF normalised (a CRLF checkout must patch the same way).
function(_pd_bgfx_read_source src out)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}" "${CMAKE_CURRENT_LIST_FILE}")
  file(READ "${src}" text)
  string(REPLACE "\r" "" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

function(_pd_bgfx_write_patched dst text)
  file(WRITE "${dst}.tmp" "${text}")
  file(COPY_FILE "${dst}.tmp" "${dst}" ONLY_IF_DIFFERENT)
endfunction()

# --- bx ----------------------------------------------------------------------

# bx's headers, including its MinGW compat folder, stay private to the
# third-party targets: compat/mingw replaces <dirent.h>, which the game uses.
# The game only ever sees bgfx/include.
add_library(pd_bx_headers INTERFACE)
target_include_directories(pd_bx_headers SYSTEM INTERFACE
  "${BX_DIR}/include"
  "${BX_DIR}/3rdparty"
  "${BX_DIR}/include/compat/mingw")
target_compile_definitions(pd_bx_headers INTERFACE
  BX_CONFIG_DEBUG=0
  __STDC_LIMIT_MACROS
  __STDC_FORMAT_MACROS
  __STDC_CONSTANT_MACROS)

# Patch: bx/src/debug.cpp constructs its dbghelp symbol resolver as a
# namespace-scope static, so linking bx would load dbghelp.dll and call
# SymInitialize (with a Microsoft symbol-server path) before main() in every
# renderer mode, colliding with the game's own crash handler (port/src/crash.c).
# The patched copy constructs it on first use (a callstack dump) instead, and
# never gives dbghelp the symbol-server path, so even that dump cannot download
# symbols or write a symbol cache under %TEMP%. Nothing in this build reaches
# the resolver anyway: its only caller is bx's assert handler, and
# BX_CONFIG_DEBUG=0 compiles every BX_ASSERT out (tools/bgfx_dependency_audit.py).
_pd_bgfx_read_source("${BX_DIR}/src/debug.cpp" _pd_bx_debug_text)
_pd_bgfx_patch_once("${_pd_bx_debug_text}"
  "s_dbgHelpSymbolResolve.resolve("
  "dbgHelpSymbolResolve().resolve("
  _pd_bx_debug_text)
_pd_bgfx_patch_once("${_pd_bx_debug_text}"
  "\tstatic DbgHelpSymbolResolve s_dbgHelpSymbolResolve;\n"
  "\t// PD2 patch (third_party/bgfx-pd/CMakeLists.txt): built on first use, not before main().\n\tstatic DbgHelpSymbolResolve& dbgHelpSymbolResolve()\n\t{\n\t\tstatic DbgHelpSymbolResolve s_dbgHelpSymbolResolve;\n\t\treturn s_dbgHelpSymbolResolve;\n\t}\n"
  _pd_bx_debug_text)
_pd_bgfx_patch_once("${_pd_bx_debug_text}"
  "if (getEnv(symCache + 4, &len, \"TEMP\") )"
  "if (false && getEnv(symCache + 4, &len, \"TEMP\") ) // PD2 patch: local symbols only, no symbol server"
  _pd_bx_debug_text)
set(_pd_bx_debug_patched "${CMAKE_CURRENT_BINARY_DIR}/patched/bx_debug.cpp")
_pd_bgfx_write_patched("${_pd_bx_debug_patched}" "${_pd_bx_debug_text}")

file(GLOB _pd_bx_sources "${BX_DIR}/src/*.cpp")
list(REMOVE_ITEM _pd_bx_sources "${BX_DIR}/src/amalgamated.cpp" "${BX_DIR}/src/debug.cpp")
add_library(pd_bx STATIC ${_pd_bx_sources} "${_pd_bx_debug_patched}")
target_compile_options(pd_bx PRIVATE ${PD_BGFX_COMPILE_OPTIONS})
target_include_directories(pd_bx PRIVATE "${BX_DIR}/src")
target_link_libraries(pd_bx PRIVATE pd_bx_headers)
if(WIN32)
  target_link_libraries(pd_bx INTERFACE psapi)
endif()

# --- bimg (core only: image.cpp, image_gnf.cpp and the ASTC decoder) ---------

file(GLOB _pd_astc_sources "${BIMG_DIR}/3rdparty/astc-encoder/source/*.cpp")
add_library(pd_bimg STATIC
  "${BIMG_DIR}/src/image.cpp"
  "${BIMG_DIR}/src/image_gnf.cpp"
  ${_pd_astc_sources})
target_compile_options(pd_bimg PRIVATE ${PD_BGFX_COMPILE_OPTIONS})
target_link_libraries(pd_bimg PRIVATE pd_bx_headers)
target_include_directories(pd_bimg PRIVATE
  "${BIMG_DIR}/include"
  "${BIMG_DIR}/3rdparty/astc-encoder/include")
target_link_libraries(pd_bimg INTERFACE pd_bx)

# --- bgfx (OpenGL 4.3 renderer only, single-threaded) ------------------------

# Every renderer_*.cpp is compiled: a disabled renderer compiles to the stub
# rendererCreate/rendererDestroy pair that bgfx.cpp's renderer table names.
# Left out on purpose:
#   amalgamated.cpp     we build the files individually
#   debug_renderdoc.cpp replaced by renderdoc_stub.cpp (never loads renderdoc.dll)
#   nvapi.cpp           only the D3D11/D3D12 renderers use it (nvapi64.dll)
#   dxgi.cpp            only the D3D11/D3D12 renderers use it
#   renderer_gl.cpp     replaced by the patched copy below
file(GLOB _pd_bgfx_sources "${BGFX_DIR}/src/*.cpp")
foreach(_pd_skip amalgamated.cpp debug_renderdoc.cpp nvapi.cpp dxgi.cpp renderer_gl.cpp)
  list(REMOVE_ITEM _pd_bgfx_sources "${BGFX_DIR}/src/${_pd_skip}")
endforeach()

# Patch: polygon offset for fast3d decal draws (ZMODE_DEC). The GL renderer
# fast3d replaces uses glPolygonOffset(-2, -2) for decals; bgfx has no depth
# bias state. The patch maps one unused state bit to a hook that the game
# implements (port/fast3d/gfx_bgfx_ext.h).
set(_pd_gl_patched "${CMAKE_CURRENT_BINARY_DIR}/patched/renderer_gl.cpp")
_pd_bgfx_read_source("${BGFX_DIR}/src/renderer_gl.cpp" _pd_gl_text)

set(_pd_anchor_head "#include \"bgfx_p.h\"\n")
set(_pd_patch_head "#include \"bgfx_p.h\"\n// PD2 patch (third_party/bgfx-pd/CMakeLists.txt): fast3d decal polygon offset.\n#include \"gfx_bgfx_ext.h\"\n")
_pd_bgfx_patch_once("${_pd_gl_text}" "${_pd_anchor_head}" "${_pd_patch_head}" _pd_gl_text)

set(_pd_anchor_state "\t\t\t\tif ( ( (0\n\t\t\t\t\t | BGFX_STATE_ALPHA_REF_MASK\n")
set(_pd_patch_state "\t\t\t\t// PD2 patch: fast3d decal polygon offset, applied like any other state bit\n\t\t\t\t// (a view change resets changedFlags to every bit).\n\t\t\t\tif (PD_BGFX_STATE_POLYGON_OFFSET & changedFlags)\n\t\t\t\t{\n\t\t\t\t\tpdBgfxGlPolygonOffset(0 != (PD_BGFX_STATE_POLYGON_OFFSET & newFlags) );\n\t\t\t\t}\n\n${_pd_anchor_state}")
_pd_bgfx_patch_once("${_pd_gl_text}" "${_pd_anchor_state}" "${_pd_patch_state}" _pd_gl_text)

_pd_bgfx_write_patched("${_pd_gl_patched}" "${_pd_gl_text}")

add_library(pd_bgfx STATIC
  ${_pd_bgfx_sources}
  "${_pd_gl_patched}"
  "${CMAKE_CURRENT_SOURCE_DIR}/renderdoc_stub.cpp")
target_compile_options(pd_bgfx PRIVATE ${PD_BGFX_COMPILE_OPTIONS})
target_link_libraries(pd_bgfx PRIVATE pd_bx_headers)
target_include_directories(pd_bgfx SYSTEM PUBLIC "${BGFX_DIR}/include")
target_include_directories(pd_bgfx PRIVATE
  "${BIMG_DIR}/include"
  "${BGFX_DIR}/src"
  "${BGFX_DIR}/3rdparty"
  "${BGFX_DIR}/3rdparty/khronos"
  "${BGFX_DIR}/3rdparty/directx-headers/include/directx"
  "${BGFX_DIR}/3rdparty/directx-headers/include"
  "${CMAKE_SOURCE_DIR}/port/fast3d")
target_compile_definitions(pd_bgfx PRIVATE
  BGFX_CONFIG_RENDERER_OPENGL=43
  BGFX_CONFIG_MULTITHREADED=0
  BGFX_CONFIG_DEBUG=0
  BGFX_CONFIG_DEBUG_ANNOTATION=0
  BGFX_CONFIG_PROFILER=0
  BGFX_CONFIG_MAX_VIEWS=4096
  BGFX_CONFIG_MAX_TEXTURES=16384
  BGFX_CONFIG_MAX_FRAME_BUFFERS=256
  # One handle per distinct transient vertex layout, never freed (bgfx_p.h
  # findOrCreateVertexLayout); fast3d makes one per combiner layout.
  BGFX_CONFIG_MAX_VERTEX_LAYOUTS=1024
  # Scissor rects per frame; fast3d and ImGui also reuse an unchanged rect.
  BGFX_CONFIG_MAX_RECT_CACHE=16384
  # port/src/system.c already exports NvOptimusEnablement and
  # AmdPowerXpressRequestHighPerformance (both 1); bgfx's copies would clash.
  BGFX_CONFIG_PREFER_DISCRETE_GPU=0)
target_link_libraries(pd_bgfx INTERFACE pd_bimg pd_bx)
if(WIN32)
  target_link_libraries(pd_bgfx INTERFACE gdi32 user32)
endif()

# --- fcpp and the lean shaderc host tool -------------------------------------

set(_pd_fcpp_dir "${BGFX_DIR}/3rdparty/fcpp")
add_library(pd_fcpp STATIC
  "${_pd_fcpp_dir}/cpp1.c" "${_pd_fcpp_dir}/cpp2.c" "${_pd_fcpp_dir}/cpp3.c"
  "${_pd_fcpp_dir}/cpp4.c" "${_pd_fcpp_dir}/cpp5.c" "${_pd_fcpp_dir}/cpp6.c")
target_compile_options(pd_fcpp PRIVATE ${PD_BGFX_COMPILE_OPTIONS})
target_compile_definitions(pd_fcpp PRIVATE NINCLUDE=64 NWORK=65536 NBUFF=65536 OLD_PREPROCESSOR=0)
target_include_directories(pd_fcpp PUBLIC "${_pd_fcpp_dir}")

file(GLOB _pd_shaderc_sources "${BGFX_DIR}/tools/shaderc/*.cpp")
# Without glsl-optimizer, shaderc_glsl.cpp defines a duplicate
# compileSPIRVShader; shaderc_lean.cpp provides compileGLSLShader instead.
list(REMOVE_ITEM _pd_shaderc_sources "${BGFX_DIR}/tools/shaderc/shaderc_glsl.cpp")
add_executable(pd_shaderc
  ${_pd_shaderc_sources}
  "${CMAKE_CURRENT_SOURCE_DIR}/shaderc_lean.cpp"
  "${BGFX_DIR}/src/shader.cpp"
  "${BGFX_DIR}/src/shader_dxbc.cpp"
  "${BGFX_DIR}/src/shader_spirv.cpp"
  "${BGFX_DIR}/src/vertexlayout.cpp")
target_compile_options(pd_shaderc PRIVATE ${PD_BGFX_COMPILE_OPTIONS})
target_include_directories(pd_shaderc PRIVATE
  "${BGFX_DIR}/tools/shaderc"
  "${BIMG_DIR}/include"
  "${BGFX_DIR}/include"
  "${BGFX_DIR}/src"
  "${BGFX_DIR}/3rdparty")
target_compile_definitions(pd_shaderc PRIVATE
  SHADERC_CONFIG_HAS_D3DCOMPILER=0
  SHADERC_CONFIG_HAS_DXC=0
  SHADERC_CONFIG_HAS_TINT=0
  SHADERC_CONFIG_HAS_GLSLANG=0
  SHADERC_CONFIG_HAS_GLSL_OPTIMIZER=0)
target_link_libraries(pd_shaderc PRIVATE pd_bx_headers pd_fcpp pd_bimg pd_bx)
# A self-contained host tool: no MinGW runtime DLLs needed to run it.
target_link_options(pd_shaderc PRIVATE -static)
set_target_properties(pd_shaderc PROPERTIES OUTPUT_NAME "pd-shaderc")

# --- Ahead-of-time shader compilation ----------------------------------------

# pd_bgfx_compile_shaders(<target> VARYING <varying.def.sc>
#                         VERTEX <vs.sc...> FRAGMENT <fs.sc...>)
# Compiles each shader for GLSL 4.30 into <build>/generated/bgfx_shaders/
# <name>.bin.h holding `static const uint8_t <name>_glsl[]`, and makes
# <target> depend on the headers. Call it from the directory that defines
# <target>.
function(pd_bgfx_compile_shaders target)
  cmake_parse_arguments(ARG "" "VARYING" "VERTEX;FRAGMENT" ${ARGN})
  set(out_dir "${CMAKE_BINARY_DIR}/generated/bgfx_shaders")
  file(MAKE_DIRECTORY "${out_dir}")
  set(headers)
  foreach(kind IN ITEMS VERTEX FRAGMENT)
    if(kind STREQUAL "VERTEX")
      set(type vertex)
    else()
      set(type fragment)
    endif()
    foreach(sc IN LISTS ARG_${kind})
      get_filename_component(name "${sc}" NAME_WE)
      set(header "${out_dir}/${name}.bin.h")
      add_custom_command(
        OUTPUT "${header}"
        COMMAND $<TARGET_FILE:pd_shaderc>
                -f "${sc}" -o "${header}"
                --type ${type} --platform windows -p 430
                -i "${BGFX_DIR}/src"
                --varyingdef "${ARG_VARYING}"
                --bin2c "${name}_glsl"
        DEPENDS pd_shaderc "${sc}" "${ARG_VARYING}" "${BGFX_DIR}/src/bgfx_shader.sh"
        COMMENT "shaderc ${name} (GLSL 4.30)"
        VERBATIM)
      list(APPEND headers "${header}")
    endforeach()
  endforeach()
  target_sources(${target} PRIVATE ${headers})
  target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
