@echo off
setlocal
powershell.exe -STA -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\manage-versions.ps1"
if errorlevel 1 (
    echo.
    echo The revision manager could not open. Review the message above.
    pause
    exit /b 1
)
