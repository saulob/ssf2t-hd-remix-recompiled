@echo off
rem Builds the TRG Launcher library and demo with Visual Studio's compiler.
rem   build.bat            Release build in build\
rem   build.bat Debug      Debug build
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

where cl >nul 2>nul
if not errorlevel 1 goto build
rem (no if-blocks here: the ")" in "ProgramFiles(x86)" would end them)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto novs
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul

:build
cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "%~dp0build" || exit /b 1
echo.
echo Built: %~dp0build\examples\demo\trg_launcher_demo.exe
exit /b 0

:novs
echo Visual Studio with the C++ tools was not found.
exit /b 1
