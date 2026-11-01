@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 delete-share-holder.c /link /OUT:delete-share-holder.exe
exit /b %errorlevel%
