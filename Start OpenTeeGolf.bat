@echo off
rem Double-click to start OpenTeeGolf, then play at http://localhost:8080
cd /d "%~dp0"
rem The release zip carries its own Node.js in node\; otherwise use the installed one
set "NODE=node"
if exist "%~dp0node\node.exe" set "NODE=%~dp0node\node.exe"
rem (after an update the game page is already open, so no new tab)
if /i not "%~1"=="--no-browser" start "" http://localhost:8080
"%NODE%" server.js
pause
