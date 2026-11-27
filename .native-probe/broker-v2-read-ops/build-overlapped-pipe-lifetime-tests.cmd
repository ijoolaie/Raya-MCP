@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul || (echo ERROR: Run from an x64 Native Tools Command Prompt.& exit /b 2)
cl.exe /nologo /W4 /WX /std:c11 /D_CRT_SECURE_NO_WARNINGS relative-path.c read-frame.c overlapped-pipe-lifetime-tests.c /Fe:overlapped-pipe-lifetime-tests.exe
if errorlevel 1 exit /b %errorlevel%
overlapped-pipe-lifetime-tests.exe
exit /b %errorlevel%
