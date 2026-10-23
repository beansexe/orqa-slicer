@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /I integration tests\fast-start-warning-tests.cpp /Foartifacts\ /Feartifacts\fast-start-warning-tests.exe
if errorlevel 1 exit /b 1
artifacts\fast-start-warning-tests.exe
exit /b %errorlevel%
