@echo off
rem Launch the PD Build Tool GUI.
rem The script hides its own console via ConsoleHelper::HideConsole().
rem 'pause' keeps this window open so any startup errors are visible.
powershell -ExecutionPolicy Bypass -File "%~dp0build-gui.ps1"
pause
