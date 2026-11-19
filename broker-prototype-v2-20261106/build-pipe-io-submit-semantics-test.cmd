@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC pipe-io-submit-semantics-test.c /link /OUT:pipe-io-submit-semantics-test.exe
exit /b %errorlevel%
