@echo off
title Perfect Dark 2 - Dedicated Server
echo ============================================
echo   Perfect Dark 2 - Dedicated Server v0.0.2
echo ============================================
echo.

REM --- Configuration ---
set PORT=27100
set MAX_CLIENTS=8
set PROFILE=0

REM --- Detect public IP ---
echo Detecting public IP...
for /f "tokens=*" %%i in ('powershell -Command "(Invoke-WebRequest -Uri 'https://api.ipify.org' -UseBasicParsing -TimeoutSec 5).Content" 2^>nul') do set PUBLIC_IP=%%i

if "%PUBLIC_IP%"=="" (
    echo [WARNING] Could not detect public IP. Players on LAN can still connect.
    echo           Check your IP manually at https://whatismyip.com
) else (
    echo.
    echo ============================================
    echo   PUBLIC IP: %PUBLIC_IP%
    echo   PORT:      %PORT%
    echo.
    echo   Players connect to: %PUBLIC_IP%:%PORT%
    echo ============================================
)

echo.
echo NAT punch-through is used for connectivity.
echo.
echo Starting server...
echo.

REM --- Launch the game in host mode ---
cd /d "%~dp0build"
pd.x86_64.exe --host --port %PORT% --maxclients %MAX_CLIENTS% --file %PROFILE%

echo.
echo Server stopped.
pause
