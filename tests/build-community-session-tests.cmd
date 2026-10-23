@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MD /DNOMINMAX /I integration tests\community-session-tests.cpp /Foartifacts\community-session-tests.obj /Feartifacts\community-session-tests.exe crypt32.lib
if errorlevel 1 exit /b 1
artifacts\community-session-tests.exe
exit /b %errorlevel%
