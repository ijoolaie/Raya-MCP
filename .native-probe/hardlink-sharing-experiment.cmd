@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 .native-probe\hardlink-sharing-experiment.c /Fe:.native-probe\hardlink-sharing-experiment.exe
if errorlevel 1 exit /b %errorlevel%
.native-probe\hardlink-sharing-experiment.exe
exit /b %errorlevel%
