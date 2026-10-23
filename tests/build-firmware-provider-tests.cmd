@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /DNDEBUG /MD /EHsc /W4 /WX /I integration /I upstream\OrcaSlicer\deps_src integration\printers\PrinterCatalog.cpp tests\firmware-provider-tests.cpp /Foartifacts\ /Feartifacts\firmware-provider-tests.exe
if errorlevel 1 exit /b 1
artifacts\firmware-provider-tests.exe
exit /b %errorlevel%
