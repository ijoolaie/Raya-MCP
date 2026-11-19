@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC protocol-v2.c protocol-v2-test.c /link /OUT:protocol-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC response-v2.c response-v2-test.c /link /OUT:response-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC /c pipe-io-v2.c
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC session-v2.c session-v2-test.c /link /OUT:session-v2-test.exe
exit /b %errorlevel%
