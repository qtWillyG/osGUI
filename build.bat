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

cl /nologo /EHsc /O2 /W4 /MD main.cpp osgui.cpp osgui_demo.cpp osgui_theme_editor.cpp ^
   osgui_impl_win32.cpp osgui_impl_opengl2.cpp ^
   /Fe:osgui_demo.exe ^
   /link user32.lib gdi32.lib opengl32.lib /SUBSYSTEM:WINDOWS
