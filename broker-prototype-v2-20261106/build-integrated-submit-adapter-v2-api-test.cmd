@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /DRAYA_ADAPTER_TESTING /TC dedicated-io-submitter-v2.c reaper-recovery-policy-v2.c reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-window-v2.c reaper-waitset-v2.c reaper-waitplan-v2.c pipe-completion-context-v2.c pipe-io-owner-v2.c pipe-io-finalizer-v2.c reaper-managed-worker-v2.c managed-pipe-io-v2.c integrated-submit-adapter-v2.c integrated-submit-adapter-v2-api-test.c /link /OUT:integrated-submit-adapter-v2-api-test.exe
if errorlevel 1 goto :failed
for /L %%i in (1,1,10) do (
 integrated-submit-adapter-v2-api-test.exe --strict-dual-ack
 if errorlevel 1 goto :failed
 integrated-submit-adapter-v2-api-test.exe --not-issued-notification
 if errorlevel 1 goto :failed
 integrated-submit-adapter-v2-api-test.exe --managed-worker-wait-failed
 if errorlevel 1 goto :failed
 integrated-submit-adapter-v2-api-test.exe --quarantine-publication-failed
 if errorlevel 1 goto :failed
 integrated-submit-adapter-v2-api-test.exe
 if errorlevel 1 goto :failed
)
set RC=0
goto :cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q integrated-submit-adapter-v2-api-test.exe dedicated-io-submitter-v2.obj reaper-recovery-policy-v2.obj reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-window-v2.obj reaper-waitset-v2.obj reaper-waitplan-v2.obj pipe-completion-context-v2.obj pipe-io-owner-v2.obj pipe-io-finalizer-v2.obj reaper-managed-worker-v2.obj managed-pipe-io-v2.obj integrated-submit-adapter-v2.obj integrated-submit-adapter-v2-api-test.obj 2>nul
exit /b %RC%
