@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ACCOUNT_VS=%%v"
if not defined ACCOUNT_VS exit /b 1
call "%ACCOUNT_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
if not exist artifacts mkdir artifacts
cl /nologo /std:c++17 /EHsc /W4 /I integration integration\FlashforgeAccount.cpp tests\account-tests.cpp /Foartifacts\ /Feartifacts\account-tests.exe
if errorlevel 1 exit /b 1
artifacts\account-tests.exe
exit /b %errorlevel%
