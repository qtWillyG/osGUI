@echo off
setlocal

where cl >nul 2>nul
if errorlevel 1 (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo OSGui: run this file from a Visual Studio Developer Command Prompt.
        exit /b 1
    )
)

cl /nologo /EHsc /O2 /MD main.cpp osgui.cpp osgui_demo.cpp ^
   osgui_impl_win32.cpp osgui_impl_opengl2.cpp ^
   /Fe:osgui_demo.exe ^
   /link user32.lib gdi32.lib opengl32.lib /SUBSYSTEM:WINDOWS
