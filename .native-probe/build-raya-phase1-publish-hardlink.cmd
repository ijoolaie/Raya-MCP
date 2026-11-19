@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /LD /I "C:\Users\Ali\local-mcp-bridge\.native-probe\node-gyp-cache\22.23.2\include\node" /DNODE_GYP_MODULE_NAME=publish_hardlink_probe handle-relative-publish-hardlink-probe.c /link /LIBPATH:"C:\Users\Ali\local-mcp-bridge\.native-probe\node-gyp-cache\22.23.2\x64" node.lib /OUT:"build\Release\napi_probe_raya_phase1_publish_hardlink.node"
exit /b %errorlevel%
