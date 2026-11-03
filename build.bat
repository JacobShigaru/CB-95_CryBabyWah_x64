@echo off
rem ---------------------------------------------------------------------------
rem  CryBabyWah - build script (MSVC x64, no CMake required)
rem
rem    build.bat          build VST3 plug-in + test host + demo tools
rem    build.bat clean    remove build outputs
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion
set ROOT=%~dp0
cd /d "%ROOT%"

if /I "%~1"=="clean" (
    if exist build rmdir /s /q build
    if exist dist rmdir /s /q dist
    echo clean: done
    exit /b 0
)

rem --- locate the Visual Studio toolchain ------------------------------------
set VCVARS=
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
)
if defined VSPATH if exist "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=!VSPATH!\VC\Auxiliary\Build\vcvars64.bat
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat

if not defined VCVARS (
    echo ERROR: Visual Studio C++ build tools not found.
    echo Install "Build Tools for Visual Studio" with the C++ workload.
    exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed
    exit /b 1
)

if not exist build mkdir build
if not exist build\obj mkdir build\obj

set CFLAGS=/nologo /std:c++17 /EHsc /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /I third_party\vst3sdk
set LIBS=/link gdiplus.lib user32.lib gdi32.lib

echo [1/4] plug-in DLL
cl %CFLAGS% src\plugin\WahPlugin.cpp src\plugin\Factory.cpp src\plugin\WahView.cpp ^
    src\plugin\Iids.cpp src\ui\PedalUI.cpp /Fo:build\obj\ /Fe:build\CryBabyWah.dll /LD %LIBS%
if errorlevel 1 exit /b 1

echo [2/4] test host
cl %CFLAGS% tools\host.cpp src\plugin\Iids.cpp /Fo:build\obj\ /Fe:build\wah_host.exe %LIBS%
if errorlevel 1 exit /b 1

echo [3/4] offline renderer + UI preview
cl %CFLAGS% tools\render.cpp /Fo:build\obj\ /Fe:build\wah_render.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tools\ui_preview.cpp src\ui\PedalUI.cpp /Fo:build\obj\ /Fe:build\ui_preview.exe %LIBS%
if errorlevel 1 exit /b 1

echo [4/4] VST3 bundle
if exist dist\CryBabyWah.vst3 rmdir /s /q dist\CryBabyWah.vst3
mkdir dist\CryBabyWah.vst3\Contents\x86_64-win
mkdir dist\CryBabyWah.vst3\Contents\Resources
copy /y build\CryBabyWah.dll dist\CryBabyWah.vst3\Contents\x86_64-win\CryBabyWah.vst3 >nul
rem moduleinfo.json is optional (VST3 3.6.10+); the DLL factory is authoritative

echo.
echo done: dist\CryBabyWah.vst3
exit /b 0


