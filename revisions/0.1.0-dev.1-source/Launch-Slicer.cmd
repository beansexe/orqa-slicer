@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0manage.ps1" Run
if errorlevel 1 (
    echo.
    echo The slicer could not launch. Review the message above.
    pause
    exit /b 1
)
