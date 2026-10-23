@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CATALOG_VS=%%v"
if not defined CATALOG_VS exit /b 1
call "%CATALOG_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "CATALOG_TIDY=tools\code-checks\Scripts\clang-tidy.exe"
if not exist "%CATALOG_TIDY%" exit /b 1
"%CATALOG_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy integration/printers/PrinterCatalog.cpp --quiet -- -std=c++17 -Iintegration
if errorlevel 1 exit /b 1
"%CATALOG_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy integration/printers/PrinterCatalog.hpp --quiet -- -x c++ -std=c++17 -Iintegration
if errorlevel 1 exit /b 1
"%CATALOG_TIDY%" --config-file=upstream/OrcaSlicer/.clang-tidy tests/printer-catalog-tests.cpp --quiet -- -std=c++17 -Iintegration
exit /b %errorlevel%
