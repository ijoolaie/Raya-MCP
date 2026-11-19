@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC supervisor-control-v2.c supervisor-control-v2-test.c /link /OUT:supervisor-control-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
supervisor-control-v2-test.exe
exit /b %errorlevel%