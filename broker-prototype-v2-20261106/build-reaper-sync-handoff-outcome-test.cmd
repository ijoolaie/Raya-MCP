@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-sync-v2.c reaper-sync-handoff-outcome-test.c /link /OUT:reaper-sync-handoff-outcome-test.exe
if errorlevel 1 goto :cleanup
for /L %%i in (1,1,25) do (
 reaper-sync-handoff-outcome-test.exe
 if errorlevel 1 goto :failed
)
set RC=0
goto :cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q reaper-sync-handoff-outcome-test.exe reaper-core-v2.obj reaper-sync-v2.obj reaper-sync-handoff-outcome-test.obj 2>nul
if not defined RC set RC=1
exit /b %RC%
