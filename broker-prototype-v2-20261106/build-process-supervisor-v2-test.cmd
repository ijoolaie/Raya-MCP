@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC process-supervisor-v2.c process-supervisor-v2-test.c /link /OUT:process-supervisor-v2-test.exe
if errorlevel 1 goto :failed
for /L %%i in (1,1,25) do (
 process-supervisor-v2-test.exe
 if errorlevel 1 goto :failed
)
set RC=0
goto :cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q process-supervisor-v2-test.exe process-supervisor-v2.obj process-supervisor-v2-test.obj 2>nul
exit /b %RC%
