@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-handoff-model-test.c /link /OUT:reaper-handoff-model-test.exe
if errorlevel 1 exit /b %errorlevel%
reaper-handoff-model-test.exe
exit /b %errorlevel%
