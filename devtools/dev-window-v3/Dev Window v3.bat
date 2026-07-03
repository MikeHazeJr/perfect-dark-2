@echo off
REM Detach so this CMD window closes immediately (no stuck console behind the GUI).
cd /d "%~dp0"
start "" /MIN powershell.exe -NoLogo -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "%~dp0dev-window-v3.ps1"
exit /b 0
