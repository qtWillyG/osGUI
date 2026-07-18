@echo off
setlocal EnableExtensions

rem Convenience entry point for the canonical clean CMake build and test flow.
call "%~dp0build-cmake.bat" %*
set "EXIT_CODE=%ERRORLEVEL%"
exit /b %EXIT_CODE%
