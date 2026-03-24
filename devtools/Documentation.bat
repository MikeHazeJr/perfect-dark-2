@echo off
rem Launch the PD Documentation reader.
rem 'pause' keeps this window open so any startup errors are visible.
powershell -ExecutionPolicy Bypass -File "%~dp0doc-reader.ps1"
pause
