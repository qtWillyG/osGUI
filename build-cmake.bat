@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT_DIR=%~dp0"
if "!ROOT_DIR:~-1!"=="\" set "ROOT_DIR=!ROOT_DIR:~0,-1!"
set "BUILD_DIR=!ROOT_DIR!\build"
set "INSTALL_DIR=!BUILD_DIR!\install"
set "BUILD_TYPE=Release"

if not "%~1"=="" set "BUILD_TYPE=%~1"
if /I not "!BUILD_TYPE!"=="Debug" if /I not "!BUILD_TYPE!"=="Release" (
    echo OSGui: configuration must be Debug or Release.
    exit /b 2
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo OSGui: CMake 3.20 or newer is required.
    exit /b 1
)

where cl >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
    )
    if defined VSINSTALL call "!VSINSTALL!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

where cl >nul 2>nul
if errorlevel 1 (
    echo OSGui: MSVC was not found. Install the Desktop development with C++ workload.
    exit /b 1
)

where nmake >nul 2>nul
if errorlevel 1 (
    echo OSGui: nmake was not found after initializing the MSVC environment.
    exit /b 1
)

pushd "%ROOT_DIR%"

echo OSGui: removing the previous build tree...
cmake -E remove_directory "%BUILD_DIR%"
if errorlevel 1 goto :failure

echo OSGui: configuring !BUILD_TYPE!...
cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ^
    -DBUILD_TESTING=ON ^
    -DOSGUI_BUILD_TESTS=ON ^
    -DOSGUI_BUILD_DEMO=ON ^
    -DOSGUI_BUILD_EXAMPLES=ON ^
    -DOSGUI_BUILD_OPENGL3=ON ^
    -DOSGUI_BUILD_WIN32_OPENGL2=ON ^
    -DOSGUI_BUILD_DX11=ON ^
    -DOSGUI_BUILD_GLFW_BACKEND=OFF ^
    -DOSGUI_INSTALL=ON ^
    -DOSGUI_WARNINGS_AS_ERRORS=ON
if errorlevel 1 goto :failure

echo OSGui: building !BUILD_TYPE!...
cmake --build "%BUILD_DIR%" --parallel
if errorlevel 1 goto :failure

echo OSGui: running tests...
ctest --test-dir "%BUILD_DIR%" --output-on-failure
if errorlevel 1 goto :failure

echo OSGui: verifying install rules...
cmake --install "%BUILD_DIR%" --prefix "%INSTALL_DIR%"
if errorlevel 1 goto :failure

popd
echo OSGui: !BUILD_TYPE! build, tests, and local install completed successfully.
exit /b 0

:failure
set "EXIT_CODE=!ERRORLEVEL!"
popd
echo OSGui: build pipeline failed with exit code !EXIT_CODE!.
exit /b !EXIT_CODE!
