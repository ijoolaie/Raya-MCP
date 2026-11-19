@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
if not exist .build-wakefailure mkdir .build-wakefailure
cl.exe /nologo /W4 /WX /TC /Fo.build-wakefailure\ reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-native-wakefailure-retention-test.c /link /OUT:reaper-native-wakefailure-retention-test.exe
if errorlevel 1 goto failed
set /a RUN=0
:again
reaper-native-wakefailure-retention-test.exe
if errorlevel 1 goto failed
set /a RUN+=1
if %RUN% LSS 10 goto again
set RC=0
goto cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q reaper-native-wakefailure-retention-test.exe 2>nul
if exist .build-wakefailure rmdir /s /q .build-wakefailure
exit /b %RC%