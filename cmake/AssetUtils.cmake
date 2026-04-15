# Ninja on Windows runs custom commands via cmd.exe with a minimal environment; a bare `python3`
# is often missing (Windows uses `python` or py.exe). Resolve once at configure time so build rules
# embed an absolute path.
if(NOT PD_PYTHON_EXECUTABLE)
  find_program(PD_PYTHON_EXECUTABLE
    NAMES python3 python3.exe python python.exe py py.exe
    HINTS
      "C:/msys64/mingw64/bin"
      "C:/msys64/usr/bin"
    DOC "Python 3 for mklang/mkanims asset header generation")
endif()
if(NOT PD_PYTHON_EXECUTABLE)
  message(FATAL_ERROR "Python 3 not found (tried python3, python, py). Install Python 3 or MSYS2: pacman -S mingw-w64-x86_64-python. Or re-run CMake with -DPD_PYTHON_EXECUTABLE=C:/path/to/python.exe")
endif()
message(STATUS "Asset tools Python: ${PD_PYTHON_EXECUTABLE}")

# execute a header generator (execcmd) for every json file in jsonpath, collect headers in headerlist
# note that this reads ROMID
macro(generate_asset_headers jsonpath execcmd extraarg headerlist)
  set(TMP_JSON "")

  if (${jsonpath} MATCHES "\.json")
    # is a json file
    list(APPEND TMP_JSON "${CMAKE_SOURCE_DIR}/${ASSET_DIR}/${jsonpath}")
  else()
    # is a directory
    file(GLOB TMP_JSON "${CMAKE_SOURCE_DIR}/${ASSET_DIR}/${jsonpath}*.json")
  endif()

  # Convert execcmd to a path relative to CMAKE_BINARY_DIR.
  # Unix Makefiles on Windows mangles absolute paths (C:/...) in COMMAND args,
  # so we use relative paths with WORKING_DIRECTORY set to CMAKE_BINARY_DIR.
  # The Python scripts write output relative to CWD (e.g. src/generated/<romid>/file.h),
  # so CWD MUST be the build dir for the generated headers to land in the right place.
  file(RELATIVE_PATH REL_EXECCMD "${CMAKE_BINARY_DIR}" "${execcmd}")

  foreach(JSON ${TMP_JSON})
    unset(HEADERNAME)
    string(REPLACE ".json" ".h" HEADERNAME ${JSON})
    string(REPLACE "${CMAKE_SOURCE_DIR}/${ASSET_DIR}" "${CMAKE_BINARY_DIR}/${GENERATED_DIR}" HEADERNAME ${HEADERNAME})

    # Convert JSON path to relative to build dir as well
    file(RELATIVE_PATH REL_JSON "${CMAKE_BINARY_DIR}" "${JSON}")

    add_custom_command(
      OUTPUT  ${HEADERNAME}
      DEPENDS ${JSON}
      COMMAND "${PD_PYTHON_EXECUTABLE}" "${REL_EXECCMD}" "${REL_JSON}" ${extraarg} --headers-only --romid=${ROMID}
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    list(APPEND ${headerlist} "${HEADERNAME}")
  endforeach()

  unset(TMP_JSON)
endmacro()
