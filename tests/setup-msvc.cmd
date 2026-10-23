@echo off
rem Called by test scripts so compiler discovery works on other Windows PCs.
set "ORQA_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "ORQA_VS_ROOT="
if not exist "%ORQA_VSWHERE%" (
  echo Visual Studio C++ Build Tools are required. Install Desktop development with C++.
  exit /b 1
)
for /f "usebackq tokens=*" %%v in (`"%ORQA_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ORQA_VS_ROOT=%%v"
if not defined ORQA_VS_ROOT (
  echo No Visual Studio installation with C++ tools was found.
  exit /b 1
)
call "%ORQA_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist artifacts mkdir artifacts
exit /b 0
