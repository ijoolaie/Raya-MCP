@echo off
setlocal
cd /d "%~dp0"
where cl.exe >nul 2>nul || (echo ERROR: Run from an x64 Native Tools Command Prompt.& exit /b 2)
cl.exe /nologo /W4 /WX /std:c11 /D_CRT_SECURE_NO_WARNINGS relative-path.c handle-chain.c read-frame.c root-registry.c root-security-policy-tests.c /Fe:root-security-policy-tests.exe
if errorlevel 1 exit /b %errorlevel%
root-security-policy-tests.exe
exit /b %errorlevel%