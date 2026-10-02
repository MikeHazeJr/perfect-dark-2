@echo off
setlocal
set "PD_READINESS_PS=%ProgramFiles%\PowerShell\7\pwsh.exe"
if not exist "%PD_READINESS_PS%" set "PD_READINESS_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%PD_READINESS_PS%" -NoProfile -File "%~dp0test-interactive-readiness.ps1"
set "PD_READINESS_EXIT=%ERRORLEVEL%"
echo.
echo Readiness result: %~dp0..\..\.claude\pd-initial-integration\readiness\latest.json
echo Exit code: %PD_READINESS_EXIT% ^(2 means desktop prerequisites were unavailable.^)
echo This window stays open. The JSON file can be read after you close it.
pause
exit /b %PD_READINESS_EXIT%
