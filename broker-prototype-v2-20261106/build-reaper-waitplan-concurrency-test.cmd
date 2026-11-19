@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-window-v2.c reaper-waitset-v2.c reaper-registry-v2.c reaper-waitplan-v2.c reaper-waitplan-concurrency-test.c /link /OUT:reaper-waitplan-concurrency-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-waitplan-concurrency-test.exe
exit /b %errorlevel%
