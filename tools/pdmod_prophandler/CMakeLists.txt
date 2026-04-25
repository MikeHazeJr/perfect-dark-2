#
# tools/pdmod_prophandler/CMakeLists.txt -- Priority M / B-238 / M-2.1
#
# Builds PD2ModPropHandler.dll, the Windows Shell property handler for
# .pdmod archives. Standalone target -- depends on system zlib only, with
# no link to the engine. Included from the top-level CMakeLists.txt so
# the DLL builds alongside the game and server.
#

if(NOT WIN32)
    message(STATUS "PD2ModPropHandler: skipped (Windows-only target)")
    return()
endif()

add_library(PD2ModPropHandler SHARED
    PD2ModPropHandler.cpp
    pdmod_zip_inflate.cpp
    pdmod_json_min.cpp
)

target_compile_features(PD2ModPropHandler PRIVATE cxx_std_17)

# The handler links to libz statically (matches the engine's choice in the
# parent CMakeLists.txt) so the DLL has zero non-system runtime
# dependencies beyond the standard Windows COM stack.
find_package(ZLIB REQUIRED)
target_include_directories(PD2ModPropHandler PRIVATE ${ZLIB_INCLUDE_DIRS})
set(_msys_lib "${ZLIB_INCLUDE_DIR}/../lib")
if(EXISTS "${_msys_lib}/libz.a")
    target_link_libraries(PD2ModPropHandler PRIVATE "${_msys_lib}/libz.a")
else()
    target_link_libraries(PD2ModPropHandler PRIVATE ZLIB::ZLIB)
endif()

# COM + shell property libraries (already #pragma comment'd in the cpp,
# but listing them here keeps the CMake target self-contained).
target_link_libraries(PD2ModPropHandler PRIVATE
    propsys
    shlwapi
    ole32
    uuid
)

# MinGW build: link standard libraries statically + strip so the DLL is
# truly portable (no msvcrt / libgcc dependencies).
if(MINGW)
    target_link_options(PD2ModPropHandler PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

set_target_properties(PD2ModPropHandler PROPERTIES
    OUTPUT_NAME "PD2ModPropHandler"
    PREFIX ""
    SUFFIX ".dll"
)
