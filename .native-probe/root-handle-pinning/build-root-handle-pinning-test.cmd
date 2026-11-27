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
cl /nologo /W4 /WX /TC /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0602 root-handle-pinning-test.c /link Advapi32.lib
if errorlevel 1 (
  set "RC=%ERRORLEVEL%"
  popd
  exit /b %RC%
)
set "RC=0"
for /L %%I in (1,1,10) do (
  echo === root pin run %%I of 10 ===
  root-handle-pinning-test.exe
  if errorlevel 1 set "RC=1"
)
del /q root-handle-pinning-test.exe root-handle-pinning-test.obj 2>nul
popd
exit /b %RC%
