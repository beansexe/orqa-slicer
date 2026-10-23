@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RESTORE_VS=%%v"
if not defined RESTORE_VS exit /b 1
call "%RESTORE_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist artifacts\restored-account-sessions mkdir artifacts\restored-account-sessions
cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /I integration /I upstream\OrcaSlicer\src tests\restored-account-sessions-tests.cpp /Foartifacts\restored-account-sessions\ /Feartifacts\restored-account-sessions\restored-account-sessions-tests.exe
if errorlevel 1 exit /b 1
artifacts\restored-account-sessions\restored-account-sessions-tests.exe
exit /b %errorlevel%
