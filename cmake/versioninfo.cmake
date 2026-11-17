# B-1252: writes versioninfo.h at build time (the pd_versioninfo target in
# CMakeLists.txt), so the commit and branch a binary reports are the ones it
# was built from. CMakeLists.txt reads them once at configure time, and an
# incremental build after a commit does not configure again. configure_file
# leaves the header untouched when nothing changed, so only the files that
# include it rebuild after a commit.
#
# Inputs (-D): SRC_DIR, OUT_FILE, VERSION_TARGET, VERSION_ARCH, VERSION_ROMID,
# VERSION_BUILD, VERSION_SEM_MAJOR, VERSION_SEM_MINOR, VERSION_SEM_PATCH.

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${SRC_DIR}"
  OUTPUT_VARIABLE VERSION_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY "${SRC_DIR}"
  OUTPUT_VARIABLE VERSION_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if((VERSION_BRANCH STREQUAL "") OR (VERSION_BRANCH STREQUAL "HEAD"))
  set(VERSION_BRANCH "port-custom")
endif()

configure_file("${SRC_DIR}/port/include/versioninfo.h.in" "${OUT_FILE}" @ONLY)
