@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
if not exist "build\Release" mkdir "build\Release"
cl.exe /nologo /W4 /LD /I "C:\Users\Ali\local-mcp-bridge\.native-probe\node-gyp-cache\22.23.2\include\node" /DNODE_GYP_MODULE_NAME=rename_directory_probe handle-relative-rename-directory-probe.c /link /LIBPATH:"C:\Users\Ali\local-mcp-bridge\.native-probe\node-gyp-cache\22.23.2\x64" node.lib /OUT:"build\Release\napi_probe_raya_phase1_rename_directory.node"
exit /b %errorlevel%
