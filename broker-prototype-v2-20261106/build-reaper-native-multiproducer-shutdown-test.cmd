@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
if not exist .build-multiproducer mkdir .build-multiproducer
cl.exe /nologo /W4 /WX /TC /Fo.build-multiproducer\ reaper-core-v2.c reaper-registry-v2.c reaper-integrated-v2.c reaper-window-v2.c reaper-waitset-v2.c reaper-waitplan-v2.c pipe-completion-context-v2.c reaper-native-multiproducer-shutdown-test.c /link /OUT:reaper-native-multiproducer-shutdown-test.exe
if errorlevel 1 goto failed
set /a RUN=0
:again
reaper-native-multiproducer-shutdown-test.exe
if errorlevel 1 goto failed
set /a RUN+=1
if %RUN% LSS 10 goto again
set RC=0
goto cleanup
:failed
set RC=%errorlevel%
:cleanup
del /q reaper-native-multiproducer-shutdown-test.exe 2>nul
if exist .build-multiproducer rmdir /s /q .build-multiproducer
exit /b %RC%