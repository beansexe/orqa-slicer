@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DEVICE_PANEL_VS=%%v"
if not defined DEVICE_PANEL_VS exit /b 1
call "%DEVICE_PANEL_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\prepare-flashforge-device-panel-check.ps1
if errorlevel 1 exit /b 1
set "DEVICE_PANEL_TIDY=tools\code-checks\Scripts\clang-tidy.exe"
if not exist "%DEVICE_PANEL_TIDY%" exit /b 1
"%DEVICE_PANEL_TIDY%" -p artifacts/flashforge-device-panel --config-file=upstream/OrcaSlicer/.clang-tidy integration/devices/FlashforgeDevicePanel.cpp --quiet
if errorlevel 1 exit /b 1
"%DEVICE_PANEL_TIDY%" -p artifacts/flashforge-device-panel --config-file=upstream/OrcaSlicer/.clang-tidy integration/devices/FlashforgeDevicePanel.hpp --quiet
exit /b %errorlevel%
