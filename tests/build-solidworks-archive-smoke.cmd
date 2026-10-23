@echo off
setlocal
cd /d "%~dp0.."
call "%~dp0setup-msvc.cmd"
if errorlevel 1 exit /b 1
set "SW_DEPS=upstream\OrcaSlicer\deps\build\OrcaSlicer_dep\usr\local"
cl /nologo /std:c++17 /EHsc /O2 /DNDEBUG /MD /W4 /WX /DXML_STATIC /DPNG_STATIC /I integration /I upstream\OrcaSlicer\deps_src\miniz /I "%SW_DEPS%\include" integration\printing\FlashforgeArchiveInspector.cpp tests\solidworks-archive-smoke.cpp /Foartifacts\solidworks-tests\ /Feartifacts\solidworks-tests\archive-smoke.exe /link upstream\OrcaSlicer\build\deps_src\miniz\Release\miniz_static.lib "%SW_DEPS%\lib\expat.lib" "%SW_DEPS%\lib\libpng16_static.lib" "%SW_DEPS%\lib\zlib.lib"
if errorlevel 1 exit /b 1
artifacts\solidworks-tests\archive-smoke.exe artifacts\solidworks-slice-smoke
exit /b %errorlevel%
