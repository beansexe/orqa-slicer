@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PICKER_VS=%%v"
if not defined PICKER_VS exit /b 1
call "%PICKER_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\prepare-printer-picker-check.ps1
if errorlevel 1 exit /b 1
set "PICKER_TIDY=tools\code-checks\Scripts\clang-tidy.exe"
if not exist "%PICKER_TIDY%" exit /b 1
"%PICKER_TIDY%" -p artifacts/printer-picker --config-file=upstream/OrcaSlicer/.clang-tidy integration/printers/PrinterPickerDialog.cpp --quiet
if errorlevel 1 exit /b 1
"%PICKER_TIDY%" -p artifacts/printer-picker --config-file=upstream/OrcaSlicer/.clang-tidy integration/printers/PrinterPickerDialog.hpp --quiet
exit /b %errorlevel%
