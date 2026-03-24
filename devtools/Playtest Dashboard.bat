@echo off
rem Launch the PD Playtest Dashboard.
rem The script hides its own console via ConsoleSuppressor::HideConsole().
rem 'pause' keeps this window open so any startup errors are visible.
powershell -ExecutionPolicy Bypass -File "%~dp0playtest-dashboard.ps1"
pause
