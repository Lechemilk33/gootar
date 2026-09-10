@echo off
REM Starts the librarian UI on this machine and opens it.
REM
REM It is a local app that happens to use a browser as its window. Nothing is
REM served to the internet and nothing leaves this computer - the address is
REM 127.0.0.1, which is your own machine talking to itself.
setlocal
cd /d "%~dp0"

if not exist node_modules (
    echo   Installing UI dependencies, one time only...
    call npm install --no-audit --no-fund
)

echo.
echo   Starting the librarian on http://127.0.0.1:3000
echo   Leave this window open while you use it. Ctrl+C to stop.
echo.

start "" http://127.0.0.1:3000
call npm run dev
