@echo off
setlocal
cd /d "%~dp0" || exit /b 10
if not defined VSINSTALLDIR (echo Run from a Visual Studio x64 Developer Command Prompt.& exit /b 20)
call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 hardlink-oplock-filter-probe-20261103.c /link /OUT:hardlink-oplock-filter-probe-20261103.exe
exit /b %errorlevel%