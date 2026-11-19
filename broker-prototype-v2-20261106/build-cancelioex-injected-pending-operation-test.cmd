@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC cancelioex-injected-pending-operation-test.c /link /OUT:cancelioex-injected-pending-operation-test.exe
if errorlevel 1 exit /b %errorlevel%
cancelioex-injected-pending-operation-test.exe
exit /b %errorlevel%
