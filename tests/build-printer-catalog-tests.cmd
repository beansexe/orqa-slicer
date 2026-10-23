@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CATALOG_VS=%%v"
if not defined CATALOG_VS exit /b 1
call "%CATALOG_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist artifacts\printer-catalog mkdir artifacts\printer-catalog
cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /I integration integration\printers\PrinterCatalog.cpp tests\printer-catalog-tests.cpp /Foartifacts\printer-catalog\ /Feartifacts\printer-catalog\printer-catalog-tests.exe
if errorlevel 1 exit /b 1
artifacts\printer-catalog\printer-catalog-tests.exe
exit /b %errorlevel%
