@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC cancelioex-failure-policy-test.c /link /OUT:cancelioex-failure-policy-test.exe
if errorlevel 1 exit /b %errorlevel%
cancelioex-failure-policy-test.exe
exit /b %errorlevel%
