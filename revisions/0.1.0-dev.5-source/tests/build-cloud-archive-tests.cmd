@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
tools\python-bootstrap\tools\python.exe tests\make-cloud-archive-fixtures.py artifacts\cloud-archive-fixtures
if errorlevel 1 exit /b 1
set "CLOUD_DEPS=upstream\OrcaSlicer\deps\build\OrcaSlicer_dep\usr\local"
cl /nologo /std:c++17 /EHsc /O2 /DNDEBUG /MD /W4 /WX /DXML_STATIC /DPNG_STATIC /I integration /I upstream\OrcaSlicer\deps_src\miniz /I "%CLOUD_DEPS%\include" integration\printing\FlashforgeArchiveInspector.cpp tests\cloud-archive-tests.cpp /Foartifacts\ /Feartifacts\cloud-archive-tests.exe /link upstream\OrcaSlicer\build\deps_src\miniz\Release\miniz_static.lib "%CLOUD_DEPS%\lib\expat.lib" "%CLOUD_DEPS%\lib\libpng16_static.lib" "%CLOUD_DEPS%\lib\zlib.lib"
if errorlevel 1 exit /b 1
artifacts\cloud-archive-tests.exe artifacts\cloud-archive-fixtures
exit /b %errorlevel%
