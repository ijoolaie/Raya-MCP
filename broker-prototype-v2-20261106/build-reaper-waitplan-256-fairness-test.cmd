@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-window-v2.c reaper-waitset-v2.c reaper-waitplan-v2.c reaper-waitplan-256-fairness-test.c /link /OUT:reaper-waitplan-256-fairness-test.exe
if errorlevel 1 goto :cleanup
for /L %%i in (1,1,10) do (
 reaper-waitplan-256-fairness-test.exe
 if errorlevel 1 goto :failed
)
set RC=0
goto :cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q reaper-waitplan-256-fairness-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-window-v2.obj reaper-waitset-v2.obj reaper-waitplan-v2.obj reaper-waitplan-256-fairness-test.obj 2>nul
if not defined RC set RC=1
exit /b %RC%
