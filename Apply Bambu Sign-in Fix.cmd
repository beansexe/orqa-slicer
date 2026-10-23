@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\apply-bambu-login-fix.ps1"
echo.
pause
