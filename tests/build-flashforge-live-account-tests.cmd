@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /DNDEBUG /MD /W4 /WX /I integration integration\FlashforgeAccount.cpp integration\FlashforgeAccountCloud.cpp integration\printers\PrinterCatalog.cpp integration\printing\FlashforgeCloudConnection.cpp integration\printing\FlashforgeCloudPrint.cpp tests\flashforge-live-account-tests.cpp /Foartifacts\ /Feartifacts\flashforge-live-account-tests.exe
if errorlevel 1 exit /b 1
artifacts\flashforge-live-account-tests.exe
exit /b %errorlevel%
