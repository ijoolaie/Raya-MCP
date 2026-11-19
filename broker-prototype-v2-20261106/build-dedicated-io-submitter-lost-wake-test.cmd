@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC dedicated-io-submitter-lost-wake-test.c /link /OUT:dedicated-io-submitter-lost-wake-test.exe
if errorlevel 1 goto :failed
dedicated-io-submitter-lost-wake-test.exe
if errorlevel 1 goto :failed
set RC=0
goto :cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q dedicated-io-submitter-lost-wake-test.exe dedicated-io-submitter-lost-wake-test.obj 2>nul
exit /b %RC%
