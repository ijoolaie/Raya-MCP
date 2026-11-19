@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-window-v2.c reaper-waitset-v2.c reaper-waitplan-v2.c reaper-integrated-concurrency-test.c /link /OUT:reaper-integrated-concurrency-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-integrated-concurrency-test.exe
set RC=%errorlevel%
del /q reaper-integrated-concurrency-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-window-v2.obj reaper-waitset-v2.obj reaper-waitplan-v2.obj reaper-integrated-concurrency-test.obj 2>nul
exit /b %RC%
