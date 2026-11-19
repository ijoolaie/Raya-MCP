@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /WX /TC reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-window-v2.c reaper-waitset-v2.c reaper-waitplan-v2.c reaper-native-waitfailed-retention-test.c /link /OUT:reaper-native-waitfailed-retention-test.exe
if errorlevel 1 exit /b %errorlevel%
set /a RUN=0
:again
reaper-native-waitfailed-retention-test.exe
if errorlevel 1 goto failed
set /a RUN+=1
if %RUN% LSS 25 goto again
set RC=0
goto cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q reaper-native-waitfailed-retention-test.exe reaper-core-v2.obj reaper-registry-v2.obj reaper-integrated-v2.obj reaper-window-v2.obj reaper-waitset-v2.obj reaper-waitplan-v2.obj reaper-native-waitfailed-retention-test.obj 2>nul
exit /b %RC%
