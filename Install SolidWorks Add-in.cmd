@echo off
setlocal
if not exist "%~dp0artifacts\solidworks-0.1.0.16\install.ps1" (
  echo Build the SolidWorks add-in first: run scripts\build-solidworks.ps1 in PowerShell.
  echo See docs\solidworks-workflow.md for setup instructions.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell.exe -Verb RunAs -WindowStyle Hidden -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%~dp0artifacts\solidworks-0.1.0.16\install.ps1""'"
