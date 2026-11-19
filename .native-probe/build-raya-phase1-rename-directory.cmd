@echo off
setlocal
cd /d "%~dp0" || exit /b 10
if not defined VSINSTALLDIR (echo Run from a Visual Studio x64 Developer Command Prompt.& exit /b 20)
call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
if not defined NODE_GYP_CACHE_ROOT set "NODE_GYP_CACHE_ROOT=%LOCALAPPDATA%\node-gyp\Cache"
set "NODE_GYP_VERSION=22.23.2"
set "NODE_INCLUDE=%NODE_GYP_CACHE_ROOT%\%NODE_GYP_VERSION%\include\node"
set "NODE_LIB=%NODE_GYP_CACHE_ROOT%\%NODE_GYP_VERSION%\x64"
if not exist "%NODE_INCLUDE%\node_api.h" (echo Node headers missing; set NODE_GYP_CACHE_ROOT to the node-gyp cache root.& exit /b 21)
if not exist "%NODE_LIB%\node.lib" (echo node.lib missing from the configured node-gyp cache.& exit /b 22)
if not exist "build\Release" mkdir "build\Release"
cl.exe /nologo /W4 /LD /I "%NODE_INCLUDE%" /DNODE_GYP_MODULE_NAME=rename_directory_probe handle-relative-rename-directory-probe.c /link /LIBPATH:"%NODE_LIB%" node.lib /OUT:"build\Release\napi_probe_raya_phase1_rename_directory.node"
exit /b %errorlevel%