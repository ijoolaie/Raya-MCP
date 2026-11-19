@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC completion-classify-v2.c completion-classify-v2-test.c /link /OUT:completion-classify-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
completion-classify-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC pipe-completion-context-v2.c getoverlappedresult-live-probe-test.c /link /OUT:getoverlappedresult-live-probe-test.exe
if errorlevel 1 exit /b %errorlevel%
getoverlappedresult-live-probe-test.exe
exit /b %errorlevel%
