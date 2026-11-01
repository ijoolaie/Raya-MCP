@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
set GYP_MSVS_VERSION=2022
"C:\Program Files\nodejs\node.exe" "C:\Program Files\nodejs\node_modules\npm\node_modules\node-gyp\bin\node-gyp.js" configure build --directory="C:\Users\Ali\local-mcp-bridge\.native-probe" --devdir="C:\Users\Ali\local-mcp-bridge\.native-probe\node-gyp-cache"
exit /b %errorlevel%
