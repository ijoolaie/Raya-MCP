@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 .native-probe\hardlink-acl-repro-v1.c /Fe:.native-probe\hardlink-acl-repro-v1.exe Advapi32.lib
if errorlevel 1 exit /b %errorlevel%
.native-probe\hardlink-acl-repro-v1.exe
exit /b %errorlevel%
