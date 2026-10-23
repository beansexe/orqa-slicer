@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DEVICE_STATUS_VS=%%v"
if not defined DEVICE_STATUS_VS exit /b 1
call "%DEVICE_STATUS_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist artifacts\flashforge-device-status mkdir artifacts\flashforge-device-status
cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /I integration integration\devices\FlashforgeDeviceStatus.cpp tests\flashforge-device-status-tests.cpp /Foartifacts\flashforge-device-status\ /Feartifacts\flashforge-device-status\flashforge-device-status-tests.exe
if errorlevel 1 exit /b 1
artifacts\flashforge-device-status\flashforge-device-status-tests.exe
exit /b %errorlevel%
