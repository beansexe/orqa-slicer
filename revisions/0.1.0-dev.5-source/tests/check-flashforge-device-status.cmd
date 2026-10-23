@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DEVICE_STATUS_VS=%%v"
if not defined DEVICE_STATUS_VS exit /b 1
call "%DEVICE_STATUS_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "DEVICE_STATUS_TIDY=tools\code-checks\Scripts\clang-tidy.exe"
if not exist "%DEVICE_STATUS_TIDY%" exit /b 1
"%DEVICE_STATUS_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy integration/devices/FlashforgeDeviceStatus.cpp --quiet -- -std=c++17 -Iintegration
if errorlevel 1 exit /b 1
"%DEVICE_STATUS_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy integration/devices/FlashforgeDeviceStatus.hpp --quiet -- -x c++ -std=c++17 -Iintegration
if errorlevel 1 exit /b 1
"%DEVICE_STATUS_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy tests/flashforge-device-status-tests.cpp --quiet -- -std=c++17 -Iintegration
exit /b %errorlevel%
