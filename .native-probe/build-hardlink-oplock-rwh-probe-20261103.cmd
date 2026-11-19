@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /DUNICODE /D_UNICODE hardlink-oplock-rwh-probe-20261103.c /link /OUT:hardlink-oplock-rwh-probe-20261103.exe
exit /b %errorlevel%
