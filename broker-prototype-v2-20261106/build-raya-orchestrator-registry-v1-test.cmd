@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC raya-orchestrator-registry-v1.c raya-orchestrator-registry-v1-test.c /link /OUT:raya-orchestrator-registry-v1-test.exe
if errorlevel 1 exit /b %errorlevel%
raya-orchestrator-registry-v1-test.exe
set RESULT=%errorlevel%
del /q raya-orchestrator-registry-v1-test.exe raya-orchestrator-registry-v1.obj raya-orchestrator-registry-v1-test.obj 2>nul
exit /b %RESULT%
