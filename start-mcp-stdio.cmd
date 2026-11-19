@echo off
setlocal
cd /d "%~dp0" || exit /b 10
if not defined MCP_ALLOWED_ROOTS set "MCP_ALLOWED_ROOTS=%CD%"
if defined ProgramFiles set "PATH=%ProgramFiles%\nodejs;%PATH%"
where node.exe >nul 2>nul || (echo node.exe was not found on PATH.& exit /b 11)
rem PowerShell execution is intentionally not enabled here; opt in explicitly in the parent environment if required.
node.exe server.mjs
exit /b %ERRORLEVEL%