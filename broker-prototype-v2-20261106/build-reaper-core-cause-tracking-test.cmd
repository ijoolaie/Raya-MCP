@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-recovery-policy-v2.c reaper-core-cause-tracking-test.c /link /OUT:reaper-core-cause-tracking-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-core-cause-tracking-test.exe
exit /b %errorlevel%
