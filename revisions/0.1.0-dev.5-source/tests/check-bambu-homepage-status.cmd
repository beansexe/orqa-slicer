@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BAMBU_STATUS_VS=%%v"
if not defined BAMBU_STATUS_VS exit /b 1
call "%BAMBU_STATUS_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "BAMBU_STATUS_TIDY=tools\code-checks\Scripts\clang-tidy.exe"
if not exist "%BAMBU_STATUS_TIDY%" exit /b 1
"%BAMBU_STATUS_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy integration/BambuHomepageStatus.hpp --quiet -- -x c++ -std=c++17 -Iintegration -Iupstream/OrcaSlicer/deps_src
if errorlevel 1 exit /b 1
"%BAMBU_STATUS_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy tests/bambu-homepage-status-tests.cpp --quiet -- -std=c++17 -Iintegration -Iupstream/OrcaSlicer/deps_src
exit /b %errorlevel%
