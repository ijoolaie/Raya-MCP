@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-sync-v2.c reaper-worker-v2.c reaper-worker-v2-test.c /link /OUT:reaper-worker-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-worker-v2-test.exe
exit /b %errorlevel%
