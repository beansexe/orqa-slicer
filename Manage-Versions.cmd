@echo off
setlocal
set "ORQA_ENTRY_ROOT=%~dp0"
:find_workspace
if exist "%ORQA_ENTRY_ROOT%manage.ps1" if exist "%ORQA_ENTRY_ROOT%sources.lock.json" if exist "%ORQA_ENTRY_ROOT%versions\selected.json" goto workspace_found
for %%I in ("%ORQA_ENTRY_ROOT%..") do set "ORQA_ENTRY_PARENT=%%~fI\"
if /I "%ORQA_ENTRY_PARENT%"=="%ORQA_ENTRY_ROOT%" goto workspace_missing
set "ORQA_ENTRY_ROOT=%ORQA_ENTRY_PARENT%"
goto find_workspace
:workspace_found
if /I "%~1"=="--check" goto check_workspace
powershell.exe -STA -NoProfile -ExecutionPolicy Bypass -File "%ORQA_ENTRY_ROOT%scripts\manage-versions.ps1"
if errorlevel 1 goto launch_failed
exit /b 0
:check_workspace
echo Orqa workspace: %ORQA_ENTRY_ROOT%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ORQA_ENTRY_ROOT%manage.ps1" List
exit /b %errorlevel%
:workspace_missing
echo The main Orqa slicer folder could not be found.
echo Open Manage-Versions.cmd from the folder containing your installed versions.
:launch_failed
echo.
echo The revision manager could not open. Review the message above.
pause
exit /b 1
