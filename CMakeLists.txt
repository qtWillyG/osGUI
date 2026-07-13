cmake_minimum_required(VERSION 3.20)
project(OSGui VERSION 0.2.0 LANGUAGES CXX)

option(OSGUI_BUILD_DEMO "Build the Win32/OpenGL demo" ON)

add_library(osgui STATIC
    osgui.cpp
    osgui.h
)
target_include_directories(osgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(osgui PUBLIC cxx_std_11)
if(MSVC)
    target_compile_options(osgui PRIVATE /W4)
else()
    target_compile_options(osgui PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_library(osgui_win32_opengl2 STATIC
        osgui_impl_win32.cpp
        osgui_impl_win32.h
        osgui_impl_opengl2.cpp
        osgui_impl_opengl2.h
    )
    target_link_libraries(osgui_win32_opengl2 PUBLIC osgui opengl32 gdi32 user32)
    target_compile_features(osgui_win32_opengl2 PUBLIC cxx_std_11)
    target_compile_options(osgui_win32_opengl2 PRIVATE /W4)

    if(OSGUI_BUILD_DEMO)
        add_executable(osgui_demo WIN32 main.cpp osgui_demo.cpp)
        target_link_libraries(osgui_demo PRIVATE osgui_win32_opengl2)
        target_compile_options(osgui_demo PRIVATE /W4)
    endif()
endif()
