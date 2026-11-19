@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-integrated-v2-test.c /link /OUT:reaper-integrated-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-integrated-v2-test.exe
set RC=%errorlevel%
del /q reaper-integrated-v2-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-integrated-v2-test.obj 2>nul
if not "%RC%"=="0" exit /b %RC%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-integrated-capacity-test.c /link /OUT:reaper-integrated-capacity-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-integrated-capacity-test.exe
set RC=%errorlevel%
del /q reaper-integrated-capacity-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-integrated-capacity-test.obj 2>nul
if not "%RC%"=="0" exit /b %RC%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-integrated-uncertain-test.c /link /OUT:reaper-integrated-uncertain-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-integrated-uncertain-test.exe
set RC=%errorlevel%
del /q reaper-integrated-uncertain-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-integrated-uncertain-test.obj 2>nul
if not "%RC%"=="0" exit /b %RC%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-integrated-shutdown-race-test.c /link /OUT:reaper-integrated-shutdown-race-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-integrated-shutdown-race-test.exe
set RC=%errorlevel%
del /q reaper-integrated-shutdown-race-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-integrated-shutdown-race-test.obj 2>nul
exit /b %RC%
