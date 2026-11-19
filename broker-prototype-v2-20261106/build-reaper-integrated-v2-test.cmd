@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-integrated-v2-test.c /link /OUT:reaper-integrated-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-integrated-v2-test.exe
set RC=%errorlevel%
del /q reaper-integrated-v2-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-integrated-v2-test.obj 2>nul
exit /b %RC%
