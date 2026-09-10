@echo off
REM Double-clickable launcher for setup.ps1.
REM
REM PowerShell blocks unsigned scripts by default, so this runs it with that
REM policy bypassed for this one process only - nothing about the machine's
REM settings is changed.
setlocal
echo.
echo   Starting Gootar setup...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
echo.
pause
