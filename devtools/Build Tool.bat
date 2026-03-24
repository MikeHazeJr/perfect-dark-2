@echo off
rem Launch the PD Build Tool GUI without showing a CMD window.
rem We use 'start /b' with powershell -WindowStyle Hidden so the console
rem closes immediately and only the WinForms GUI remains visible.
start "" /b powershell -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0build-gui.ps1"
