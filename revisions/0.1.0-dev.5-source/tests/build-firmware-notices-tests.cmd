@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "FIRMWARE_VS=%%v"
if not defined FIRMWARE_VS exit /b 1
call "%FIRMWARE_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist artifacts mkdir artifacts
cl /nologo /std:c++17 /O2 /DNDEBUG /MD /EHsc /W4 /I integration\firmware /I integration\printers integration\firmware\FirmwareNotices.cpp integration\printers\PrinterCatalog.cpp tests\firmware-notices-tests.cpp /Foartifacts\ /Feartifacts\firmware-notices-tests.exe
if errorlevel 1 exit /b 1
artifacts\firmware-notices-tests.exe
exit /b %errorlevel%
