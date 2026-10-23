@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BAMBU_STATUS_VS=%%v"
if not defined BAMBU_STATUS_VS exit /b 1
call "%BAMBU_STATUS_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist artifacts\bambu-homepage-status mkdir artifacts\bambu-homepage-status
cl /nologo /std:c++17 /EHsc /O2 /DNDEBUG /MD /W4 /WX /I integration /I upstream\OrcaSlicer\deps_src tests\bambu-homepage-status-tests.cpp /Foartifacts\bambu-homepage-status\ /Feartifacts\bambu-homepage-status\bambu-homepage-status-tests.exe
if errorlevel 1 exit /b 1
artifacts\bambu-homepage-status\bambu-homepage-status-tests.exe
exit /b %errorlevel%
