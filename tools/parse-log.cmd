@echo off
REM tools/parse-log.cmd — Windows batch wrapper for tools/parse-log.sh
REM
REM Auto-detects MSYS2 installation and passes all arguments to parse-log.sh.
REM Can be run from any directory.
REM
REM Usage:
REM   tools\parse-log.cmd [--target client|server|both] [--filter <pattern>]
REM   tools\parse-log.cmd --crash
REM   tools\parse-log.cmd --summary

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
    echo.
    exit /b 4
)

set "BASH_EXE=!MSYS2_DIR!\usr\bin\bash.exe"

REM ---------------------------------------------------------------------------
REM Convert PROJECT_DIR to MSYS2/Unix path format (C:\foo\bar -> /c/foo/bar)
REM ---------------------------------------------------------------------------
set "WIN_PATH=%PROJECT_DIR%"
set "UNIX_PATH=%WIN_PATH:\=/%"

for /f "tokens=1 delims=:" %%D in ("%UNIX_PATH%") do set "DRIVE=%%D"
for /f %%L in ('powershell -NoProfile -Command "\"!DRIVE!\".ToLower()"') do set "DRIVE_LC=%%L"

for /f "tokens=1,* delims=:" %%A in ("%UNIX_PATH%") do set "PATH_BODY=%%B"
set "MSYS2_PATH=/!DRIVE_LC!!PATH_BODY!"

REM ---------------------------------------------------------------------------
REM Pass all arguments through to parse-log.sh
REM ---------------------------------------------------------------------------
"!BASH_EXE!" -lc "cd '!MSYS2_PATH!' && bash tools/parse-log.sh %*"

exit /b %ERRORLEVEL%
