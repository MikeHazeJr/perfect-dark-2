# MSYS2 may live on a different drive or below a hosted runner's temp root.
# Use the selected compiler's prefix for every exact static archive lookup;
# find_library(winpthread) may select libwinpthread.dll.a instead of the .a.
get_filename_component(_pd_mingw_bin "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(_msys_lib "${_pd_mingw_bin}/../lib" ABSOLUTE)
set(PD_WINPTHREAD_STATIC_LIB "${_msys_lib}/libwinpthread.a")
set(PD_WINPTHREAD_RUNTIME_DLL "${_pd_mingw_bin}/libwinpthread-1.dll")
if(NOT EXISTS "${PD_WINPTHREAD_STATIC_LIB}")
  message(FATAL_ERROR "Missing static winpthread library: ${PD_WINPTHREAD_STATIC_LIB}")
endif()
if(NOT EXISTS "${PD_WINPTHREAD_RUNTIME_DLL}")
  message(FATAL_ERROR "Missing winpthread runtime DLL: ${PD_WINPTHREAD_RUNTIME_DLL}")
endif()
message(STATUS "MinGW dependency libraries: ${_msys_lib}")
