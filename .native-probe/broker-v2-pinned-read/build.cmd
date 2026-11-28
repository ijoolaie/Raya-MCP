@echo off
setlocal
set "HERE=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 2
for /f "tokens=*" %%V in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSROOT=%%V"
if not defined VSROOT exit /b 2
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
pushd "%HERE%"
cl /nologo /W4 /WX /TC broker-v2.c /link Advapi32.lib Ole32.lib /OUT:broker-v2.exe
if errorlevel 1 goto fail
cl /nologo /W4 /WX /TC configure-virtual-service-account.c /link Advapi32.lib /OUT:configure-virtual-service-account.exe
if errorlevel 1 goto fail
cl /nologo /W4 /WX /TC broker-v2-security-negative.c /link Advapi32.lib /OUT:broker-v2-security-negative.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
