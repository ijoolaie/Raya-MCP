@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC generation-ledger-v2.c generation-cng-v2.c generation-ledger-v2-test.c /link /OUT:generation-ledger-v2-test.exe bcrypt.lib
if errorlevel 1 exit /b %errorlevel%
generation-ledger-v2-test.exe
exit /b %errorlevel%

