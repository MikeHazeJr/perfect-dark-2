@echo off
REM tools/build.cmd — Windows batch wrapper for tools/build.sh
REM
REM Auto-detects MSYS2 installation and passes all arguments to build.sh.
REM Can be run from any directory.
REM
REM Usage:
REM   tools\build.cmd [--target client|server|both] [--source-dir <path>]

setlocal EnableDelayedExpansion

REM ---------------------------------------------------------------------------
REM Resolve script and project directories
REM ---------------------------------------------------------------------------
set "TOOLS_DIR=%~dp0"
REM Remove trailing backslash
if "%TOOLS_DIR:~-1%"=="\" set "TOOLS_DIR=%TOOLS_DIR:~0,-1%"

pushd "%TOOLS_DIR%\.."
set "PROJECT_DIR=%CD%"
popd

REM ---------------------------------------------------------------------------
REM Detect MSYS2 installation
REM ---------------------------------------------------------------------------
set "MSYS2_DIR="

REM Check standard install locations
for %%P in (C:\msys64 C:\msys32) do (
    if "!MSYS2_DIR!"=="" (
        if exist "%%P\usr\bin\bash.exe" (
            set "MSYS2_DIR=%%P"
        )
    )
)

REM Check user-local locations
if "!MSYS2_DIR!"=="" (
    for %%P in ("%LOCALAPPDATA%\msys64" "%USERPROFILE%\msys64" "%APPDATA%\msys64") do (
        if "!MSYS2_DIR!"=="" (
            if exist "%%~P\usr\bin\bash.exe" (
                set "MSYS2_DIR=%%~P"
            )
        )
    )
)

REM Check PATH for bash.exe (non-standard location)
if "!MSYS2_DIR!"=="" (
    for /f "delims=" %%F in ('where bash.exe 2^>nul') do (
        if "!MSYS2_DIR!"=="" (
            REM bash.exe path -> parent -> parent -> parent = potential MSYS2 root
            for %%G in ("%%~dpF..\..\..") do (
                if exist "%%~fG\usr\bin\bash.exe" (
                    set "MSYS2_DIR=%%~fG"
                )
            )
        )
    )
)

if "!MSYS2_DIR!"=="" (
    echo.
    echo ERROR: MSYS2 not found.
    echo.
    echo Install MSYS2 from https://www.msys2.org/ to C:\msys64
    echo Then open the MSYS2 MINGW64 shell and install required packages:
    echo.
    echo   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake make
    echo   pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkg-config
    echo.
    exit /b 4
)

set "BASH_EXE=!MSYS2_DIR!\usr\bin\bash.exe"

REM ---------------------------------------------------------------------------
REM Convert PROJECT_DIR to MSYS2/Unix path format (C:\foo\bar -> /c/foo/bar)
REM ---------------------------------------------------------------------------
set "WIN_PATH=%PROJECT_DIR%"

REM Replace backslashes with forward slashes
set "UNIX_PATH=%WIN_PATH:\=/%"

REM Extract drive letter and convert to lowercase
for /f "tokens=1 delims=:" %%D in ("%UNIX_PATH%") do set "DRIVE=%%D"
for /f %%L in ('powershell -NoProfile -Command "\"!DRIVE!\".ToLower()"') do set "DRIVE_LC=%%L"

REM Rebuild as MSYS2 path
for /f "tokens=1,* delims=:" %%A in ("%UNIX_PATH%") do set "PATH_BODY=%%B"
set "MSYS2_PATH=/!DRIVE_LC!!PATH_BODY!"

echo MSYS2:   !MSYS2_DIR!
echo Project: !PROJECT_DIR!
echo.

REM ---------------------------------------------------------------------------
REM Pass all arguments through to build.sh
REM ---------------------------------------------------------------------------
"!BASH_EXE!" -lc "cd '!MSYS2_PATH!' && TEMP=/tmp bash tools/build.sh %*"

exit /b %ERRORLEVEL%
