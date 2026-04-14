@echo off
REM Launch Perfect Dark 2 Build GUI (WinForms). Repo root = this file's directory.
cd /d "%~dp0"
start "" powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0devtools\build-gui.ps1"
