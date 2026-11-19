@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC pending-operation-journal-v1.c pending-operation-journal-v1-test.c /link /OUT:pending-operation-journal-v1-test.exe
if errorlevel 1 exit /b %errorlevel%
pending-operation-journal-v1-test.exe
exit /b %errorlevel%
