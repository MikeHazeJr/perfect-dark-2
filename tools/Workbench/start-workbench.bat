@echo off
rem Perfect Dark 2 Workbench - canonical checkout only on the default port.
cd /d "%~dp0"
echo Verifying canonical Workbench root for http://127.0.0.1:8378 ...
node "%~dp0server.js"
pause
