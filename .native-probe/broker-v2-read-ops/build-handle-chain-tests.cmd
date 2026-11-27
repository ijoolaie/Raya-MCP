@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul || (echo ERROR: Run from an x64 Native Tools Command Prompt.& exit /b 2)
cl.exe /nologo /W4 /WX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 relative-path.c handle-chain.c handle-chain-tests.c /Fe:handle-chain-tests.exe
if errorlevel 1 exit /b %errorlevel%
handle-chain-tests.exe
exit /b %errorlevel%
