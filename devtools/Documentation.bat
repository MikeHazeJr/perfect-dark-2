@echo off
rem Launch the PD Documentation reader without showing a CMD window.
start "" /b powershell -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0doc-reader.ps1"
