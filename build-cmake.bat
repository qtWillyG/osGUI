@echo off
setlocal EnableDelayedExpansion

where cl >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
    if defined VSINSTALL call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat"
    where cl >nul 2>nul
    if errorlevel 1 (
        echo OSGui: run this file from a Visual Studio Developer Command Prompt.
        exit /b 1
    )
)

cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build build
