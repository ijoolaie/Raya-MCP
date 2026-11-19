@echo off
setlocal
cd /d "%~dp0" || exit /b 10
where node.exe >nul 2>nul || (echo node.exe was not found on PATH.& exit /b 11)
node.exe raya-admin-panel\stdio-launcher.mjs
exit /b %ERRORLEVEL%