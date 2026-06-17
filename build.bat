@echo off
REM Build the osGUI Win32+OpenGL example with MSVC.
REM Run from a "Developer Command Prompt", or this script will locate vcvars.

where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    if exist "%VCVARS%" (
        call "%VCVARS%"
    ) else (
        echo Could not find cl.exe or vcvars64.bat. Open a Developer Command Prompt.
        exit /b 1
    )
)

cl /nologo /EHsc /O2 /MD ^
   main.cpp osgui.cpp osgui_demo.cpp osgui_impl_win32.cpp osgui_impl_opengl2.cpp ^
   /Fe:osgui_demo.exe ^
   /link user32.lib gdi32.lib opengl32.lib /SUBSYSTEM:WINDOWS

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build OK -^> osgui_demo.exe
) else (
    echo.
    echo Build FAILED
)
