@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RESTORE_VS=%%v"
if not defined RESTORE_VS exit /b 1
call "%RESTORE_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "RESTORE_TIDY=tools\code-checks\Scripts\clang-tidy.exe"
if not exist "%RESTORE_TIDY%" exit /b 1
"%RESTORE_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy integration/RestoredAccountSessions.hpp --quiet -- -x c++ -std=c++17 -Iintegration -Iupstream/OrcaSlicer/src
if errorlevel 1 exit /b 1
"%RESTORE_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy tests/restored-account-sessions-tests.cpp --quiet -- -std=c++17 -Iintegration -Iupstream/OrcaSlicer/src
exit /b %errorlevel%
