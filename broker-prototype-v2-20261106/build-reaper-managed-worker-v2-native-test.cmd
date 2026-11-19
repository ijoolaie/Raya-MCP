@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-window-v2.c reaper-waitset-v2.c reaper-waitplan-v2.c reaper-recovery-policy-v2.c pipe-completion-context-v2.c pipe-io-owner-v2.c pipe-io-finalizer-v2.c reaper-managed-worker-v2.c reaper-managed-worker-v2-native-test.c /link /OUT:reaper-managed-worker-v2-native-test.exe
if errorlevel 1 goto :failed
for /L %%i in (1,1,10) do (
  reaper-managed-worker-v2-native-test.exe
  if errorlevel 1 goto :failed
)
set RC=0
goto :cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q reaper-managed-worker-v2-native-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-window-v2.obj reaper-waitset-v2.obj reaper-waitplan-v2.obj reaper-recovery-policy-v2.obj pipe-completion-context-v2.obj pipe-io-owner-v2.obj pipe-io-finalizer-v2.obj reaper-managed-worker-v2.obj reaper-managed-worker-v2-native-test.obj 2>nul
exit /b %RC%
