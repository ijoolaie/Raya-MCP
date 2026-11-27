@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul || (echo ERROR: Run from an x64 Native Tools Command Prompt.& exit /b 2)
cl.exe /nologo /W4 /WX /std:c11 read-frame.c relative-path.c deadline-tests.c /Fe:deadline-tests.exe
if errorlevel 1 exit /b %errorlevel%
deadline-tests.exe
exit /b %errorlevel%
