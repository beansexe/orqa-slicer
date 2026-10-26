@echo off
setlocal
set "ORQA_ENTRY_ROOT=%~dp0"
set "ORQA_SOURCE_ROOT="
:find_workspace
if not defined ORQA_SOURCE_ROOT if exist "%ORQA_ENTRY_ROOT%manage.ps1" if exist "%ORQA_ENTRY_ROOT%sources.lock.json" set "ORQA_SOURCE_ROOT=%ORQA_ENTRY_ROOT%"
if exist "%ORQA_ENTRY_ROOT%manage.ps1" if exist "%ORQA_ENTRY_ROOT%sources.lock.json" if exist "%ORQA_ENTRY_ROOT%versions\selected.json" goto workspace_found
if exist "%ORQA_ENTRY_ROOT%manage.ps1" if exist "%ORQA_ENTRY_ROOT%sources.lock.json" if exist "%ORQA_ENTRY_ROOT%.git" goto workspace_found
for %%I in ("%ORQA_ENTRY_ROOT%..") do set "ORQA_ENTRY_PARENT=%%~fI\"
if /I "%ORQA_ENTRY_PARENT%"=="%ORQA_ENTRY_ROOT%" goto source_workspace
set "ORQA_ENTRY_ROOT=%ORQA_ENTRY_PARENT%"
goto find_workspace
:source_workspace
if not defined ORQA_SOURCE_ROOT goto workspace_missing
set "ORQA_ENTRY_ROOT=%ORQA_SOURCE_ROOT%"
:workspace_found
if /I "%~1"=="--check" goto check_workspace
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ORQA_ENTRY_ROOT%manage.ps1" Run
if errorlevel 1 goto launch_failed
exit /b 0
:check_workspace
echo Orqa workspace: %ORQA_ENTRY_ROOT%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ORQA_ENTRY_ROOT%manage.ps1" List
exit /b %errorlevel%
:workspace_missing
echo The main Orqa slicer folder could not be found.
echo Open Launch-Slicer.cmd from the folder containing your installed versions.
:launch_failed
echo.
echo The slicer could not launch. Review the message above.
pause
exit /b 1
