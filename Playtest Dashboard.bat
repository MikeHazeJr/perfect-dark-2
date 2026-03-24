@echo off
rem Launch the PD Playtest Dashboard without showing a CMD window.
start "" /b powershell -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0playtest-dashboard.ps1"
