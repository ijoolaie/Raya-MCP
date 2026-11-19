@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-window-v2.c reaper-waitset-v2.c reaper-waitplan-v2.c reaper-integrated-fault-injection-test.c /link /OUT:reaper-integrated-fault-injection-test.exe
if errorlevel 1 goto :cleanup
for /L %%i in (1,1,25) do (
  reaper-integrated-fault-injection-test.exe
  if errorlevel 1 goto :failed
)
set RC=0
goto :cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q reaper-integrated-fault-injection-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-window-v2.obj reaper-waitset-v2.obj reaper-waitplan-v2.obj reaper-integrated-fault-injection-test.obj 2>nul
if not defined RC set RC=1
exit /b %RC%
