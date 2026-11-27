@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
for /f %%V in ('node -p "process.versions.node"') do set "NODE_VERSION=%%V"
if not defined NODE_VERSION (
  echo ERROR: Node.js is required.
  exit /b 1
)
if not defined LOCALAPPDATA (
  echo ERROR: LOCALAPPDATA is not defined.
  exit /b 1
)
set "NODE_GYP_CACHE_ROOT=%LOCALAPPDATA%\node-gyp\Cache"
if defined RAYA_NODE_GYP_CACHE_ROOT set "NODE_GYP_CACHE_ROOT=%RAYA_NODE_GYP_CACHE_ROOT%"
set "NODE_INCLUDE_DIR=%NODE_GYP_CACHE_ROOT%\%NODE_VERSION%\include\node"
set "NODE_LIB_DIR=%NODE_GYP_CACHE_ROOT%\%NODE_VERSION%\x64"
if not exist "%NODE_INCLUDE_DIR%\node_api.h" (
  echo ERROR: Node headers not found: "%NODE_INCLUDE_DIR%\node_api.h"
  echo Install/cache headers for this exact Node version with node-gyp, or set RAYA_NODE_GYP_CACHE_ROOT.
  exit /b 1
)
if not exist "%NODE_LIB_DIR%\node.lib" (
  echo ERROR: Node import library not found: "%NODE_LIB_DIR%\node.lib"
  echo Expected the x64 node.lib for Node %NODE_VERSION%.
  exit /b 1
)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: Visual Studio Installer vswhere.exe was not found.
  exit /b 1
)
set "VS_INSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL (
  echo ERROR: Visual Studio C++ x64 Build Tools were not found.
  exit /b 1
)
set "VCVARS=%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo ERROR: vcvars64.bat not found: "%VCVARS%"
  exit /b 1
)
call "%VCVARS%"
if errorlevel 1 exit /b %errorlevel%
set "OUT_DIR=%SCRIPT_DIR%build\Release"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
pushd "%SCRIPT_DIR%"
cl.exe /nologo /W4 /LD /I "%NODE_INCLUDE_DIR%" /DNODE_GYP_MODULE_NAME=napi_probe /Fo"%OUT_DIR%\dircreate_pinned.obj" handle-relative-probe-phase1-dircreate-pinned.c /link /LIBPATH:"%NODE_LIB_DIR%" node.lib /OUT:"%OUT_DIR%\napi_probe_raya_phase1_dircreate_pinned.node"
if errorlevel 1 (
  popd
  exit /b %errorlevel%
)
cl.exe /nologo /W4 /LD /I "%NODE_INCLUDE_DIR%" /DNODE_GYP_MODULE_NAME=napi_probe /Fo"%OUT_DIR%\hardlink_guard.obj" handle-relative-hardlink-guard.c /link /LIBPATH:"%NODE_LIB_DIR%" node.lib /OUT:"%OUT_DIR%\napi_probe_raya_phase1_hardlink_guard.node"
set "BUILD_RESULT=%errorlevel%"
popd
exit /b %BUILD_RESULT%
