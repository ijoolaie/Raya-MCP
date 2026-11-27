@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul || (echo ERROR: Run from an x64 Native Tools Command Prompt.& exit /b 2)
cl.exe /nologo /W4 /WX /std:c11 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0602 relative-path.c handle-chain.c directory-enumeration-tests.c /Fe:directory-enumeration-tests.exe
if errorlevel 1 exit /b %errorlevel%
directory-enumeration-tests.exe
exit /b %errorlevel%
