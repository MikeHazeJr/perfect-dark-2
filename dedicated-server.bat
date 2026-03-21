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
    echo [WARNING] Could not detect public IP.
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
echo IMPORTANT: UDP port %PORT% must be forwarded in your router!
echo   Router settings: forward UDP port %PORT% to this PC.
echo   If using Hamachi/VPN: use the VPN IP instead of public IP.
echo   Disable Hamachi if not using it (can interfere with routing).
echo.
echo Starting server...
echo.

REM --- Launch the game in host mode ---
cd /d "%~dp0build\server"
PerfectDarkServer.exe --port %PORT% --maxclients %MAX_CLIENTS%

echo.
echo Server stopped.
pause
