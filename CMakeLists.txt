cmake_minimum_required(VERSION 3.20)
project(OSGui VERSION 0.5.0 LANGUAGES CXX)

option(OSGUI_BUILD_DEMO "Build the Win32/OpenGL demo" ON)
option(OSGUI_BUILD_TESTS "Build the core automated tests" ON)
option(OSGUI_BUILD_DX11 "Build the DirectX 11 renderer on Windows" ON)
option(OSGUI_BUILD_GLFW_BACKEND "Build the cross-platform GLFW backend" OFF)
option(OSGUI_BUILD_EXAMPLES "Compile the reusable example windows" ON)

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

if(OSGUI_BUILD_EXAMPLES)
    add_library(osgui_examples STATIC examples/osgui_examples.cpp examples/osgui_examples.h)
    target_include_directories(osgui_examples PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/examples")
    target_link_libraries(osgui_examples PUBLIC osgui)
    target_compile_features(osgui_examples PUBLIC cxx_std_11)
endif()

if(OSGUI_BUILD_GLFW_BACKEND)
    find_package(glfw3 CONFIG REQUIRED)
    add_library(osgui_glfw STATIC osgui_impl_glfw.cpp osgui_impl_glfw.h)
    target_link_libraries(osgui_glfw PUBLIC osgui glfw)
    target_compile_features(osgui_glfw PUBLIC cxx_std_11)
endif()

add_library(osgui_opengl3 STATIC osgui_impl_opengl3.cpp osgui_impl_opengl3.h)
target_link_libraries(osgui_opengl3 PUBLIC osgui)
target_compile_features(osgui_opengl3 PUBLIC cxx_std_11)
if(MSVC)
    target_compile_options(osgui_opengl3 PRIVATE /W4)
else()
    target_compile_options(osgui_opengl3 PRIVATE -Wall -Wextra -Wpedantic)
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
    if(MSVC)
        target_compile_options(osgui_win32_opengl2 PRIVATE /W4)
    endif()

    if(OSGUI_BUILD_DX11)
        add_library(osgui_dx11 STATIC osgui_impl_dx11.cpp osgui_impl_dx11.h)
        target_link_libraries(osgui_dx11 PUBLIC osgui d3d11 d3dcompiler)
        target_compile_features(osgui_dx11 PUBLIC cxx_std_11)
        if(MSVC)
            target_compile_options(osgui_dx11 PRIVATE /W4)
        endif()
    endif()

    if(OSGUI_BUILD_DEMO)
        add_executable(osgui_demo WIN32 main.cpp osgui_demo.cpp osgui_theme_editor.cpp)
        target_link_libraries(osgui_demo PRIVATE osgui_win32_opengl2)
        if(MSVC)
            target_compile_options(osgui_demo PRIVATE /W4)
        endif()
    endif()
endif()

if(OSGUI_BUILD_TESTS)
    enable_testing()
    add_executable(osgui_tests tests/osgui_tests.cpp)
    target_link_libraries(osgui_tests PRIVATE osgui)
    target_compile_features(osgui_tests PRIVATE cxx_std_11)
    add_test(NAME osgui_core COMMAND osgui_tests)
endif()
