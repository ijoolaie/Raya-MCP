@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC pipe-completion-context-v2.c pipe-completion-context-v2-test.c /link /OUT:pipe-completion-context-v2-test.exe
if errorlevel 1 exit /b %errorlevel%
pipe-completion-context-v2-test.exe
exit /b %errorlevel%
