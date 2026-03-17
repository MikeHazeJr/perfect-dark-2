@echo off
setlocal enabledelayedexpansion

:: Perfect Dark PC Port - Build Script (MinGW/MSYS2)
:: Usage: build.bat [clean|configure|build|copy|all]
::   No arguments = full build (configure + build + copy)

set "PROJECT_DIR=%~dp0"
set "BUILD_DIR=%PROJECT_DIR%build"
set "ADDIN_DIR=%PROJECT_DIR%..\post-batch-addin"
set "CMAKE=cmake"
set "MAKE=C:\msys64\usr\bin\make.exe"

:: Add MSYS2 MinGW to PATH if not already there
set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"

:: Parse command
set "CMD=%~1"
if "%CMD%"=="" set "CMD=all"

if /i "%CMD%"=="clean"     goto :do_clean
if /i "%CMD%"=="configure" goto :do_configure
if /i "%CMD%"=="build"     goto :do_build
if /i "%CMD%"=="copy"      goto :do_copy
if /i "%CMD%"=="all"       goto :do_all
echo Unknown command: %CMD%
echo Usage: build.bat [clean^|configure^|build^|copy^|all]
exit /b 1

:: ============================================================
:: CLEAN - Remove build directory
:: ============================================================
:do_clean
echo [CLEAN] Removing build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    echo [CLEAN] Done.
) else (
    echo [CLEAN] Build directory does not exist, nothing to clean.
)
exit /b 0

:: ============================================================
:: CONFIGURE - Generate build files with CMake
:: ============================================================
:do_configure
echo [CONFIGURE] Generating build files...
%CMAKE% -G "Unix Makefiles" ^
    -DCMAKE_MAKE_PROGRAM="%MAKE%" ^
    -DCMAKE_C_COMPILER="C:/msys64/mingw64/bin/cc.exe" ^
    -B "%BUILD_DIR%" ^
    -S "%PROJECT_DIR%"
if errorlevel 1 (
    echo [CONFIGURE] FAILED
    exit /b 1
)
echo [CONFIGURE] Done.
exit /b 0

:: ============================================================
:: BUILD - Compile the project
:: ============================================================
:do_build
echo [BUILD] Compiling...
%CMAKE% --build "%BUILD_DIR%" -- -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo [BUILD] FAILED
    exit /b 1
)
echo [BUILD] Done. Executable: %BUILD_DIR%\pd.x86_64.exe
exit /b 0

:: ============================================================
:: COPY - Copy addin files into build directory
:: ============================================================
:do_copy
if not exist "%ADDIN_DIR%" (
    echo [COPY] Addin directory not found: %ADDIN_DIR%
    exit /b 1
)
echo [COPY] Copying addin files to build directory...

:: DLLs
for %%F in ("%ADDIN_DIR%\*.dll") do (
    copy /y "%%F" "%BUILD_DIR%\" >nul
    echo   %%~nxF
)

:: Launcher bat
if exist "%ADDIN_DIR%\PD(All in One Mod)[US](64bit).bat" (
    copy /y "%ADDIN_DIR%\PD(All in One Mod)[US](64bit).bat" "%BUILD_DIR%\" >nul
    echo   PD(All in One Mod)[US](64bit).bat
)

:: Data folder (ROM etc)
if exist "%ADDIN_DIR%\data" (
    xcopy /e /i /y /q "%ADDIN_DIR%\data" "%BUILD_DIR%\data\" >nul
    echo   data\
)

:: Mods folder
if exist "%ADDIN_DIR%\mods" (
    xcopy /e /i /y /q "%ADDIN_DIR%\mods" "%BUILD_DIR%\mods\" >nul
    echo   mods\
)

echo [COPY] Done.
exit /b 0

:: ============================================================
:: ALL - Full build: configure + build + copy
:: ============================================================
:do_all
echo ============================================
echo  Perfect Dark PC Port - Full Build
echo ============================================
echo.

call :do_configure
if errorlevel 1 exit /b 1
echo.

call :do_build
if errorlevel 1 exit /b 1
echo.

call :do_copy
if errorlevel 1 exit /b 1

echo.
echo ============================================
echo  BUILD COMPLETE
echo  Run: cd build ^& "PD(All in One Mod)[US](64bit).bat"
echo ============================================
exit /b 0
