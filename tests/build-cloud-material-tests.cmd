@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /DNDEBUG /MD /W4 /WX /I integration tests\cloud-material-tests.cpp /Foartifacts\ /Feartifacts\cloud-material-tests.exe
if errorlevel 1 exit /b 1
artifacts\cloud-material-tests.exe
exit /b %errorlevel%
