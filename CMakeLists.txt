cmake_minimum_required(VERSION 3.20)

project(
    OSGui
    VERSION 2.0.0
    DESCRIPTION "A modern immediate-mode GUI toolkit for native C++ applications"
    LANGUAGES CXX
)

include(CTest)
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(_osgui_windows_default OFF)
if(WIN32)
    set(_osgui_windows_default ON)
endif()

option(OSGUI_BUILD_DEMO "Build the Win32/OpenGL showcase application" ${_osgui_windows_default})
option(OSGUI_BUILD_TESTS "Build the core automated tests when BUILD_TESTING is enabled" ${BUILD_TESTING})
option(OSGUI_BUILD_EXAMPLES "Build the reusable example windows" ON)
option(OSGUI_BUILD_OPENGL3 "Build the loader-driven OpenGL 3 renderer" ON)
option(OSGUI_BUILD_WIN32_OPENGL2 "Build the combined Win32/OpenGL compatibility backend" ${_osgui_windows_default})
option(OSGUI_BUILD_DX11 "Build the DirectX 11 renderer" ${_osgui_windows_default})
option(OSGUI_BUILD_GLFW_BACKEND "Build the optional GLFW platform backend" OFF)
option(OSGUI_INSTALL "Generate install rules and a find_package(OSGui) package" ON)
option(OSGUI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(osgui_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(OSGUI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(OSGUI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

function(osgui_configure_library target)
    target_compile_features(${target} PUBLIC cxx_std_11)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            CXX_STANDARD_REQUIRED ON
            DEBUG_POSTFIX d
            POSITION_INDEPENDENT_CODE ON
    )
    osgui_enable_warnings(${target})
endfunction()

function(osgui_configure_executable target)
    target_compile_features(${target} PRIVATE cxx_std_11)
    set_target_properties(
        ${target}
        PROPERTIES
            CXX_EXTENSIONS OFF
            CXX_STANDARD_REQUIRED ON
    )
    osgui_enable_warnings(${target})
endfunction()

set(OSGUI_BUILD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(OSGUI_INSTALL_INCLUDE_DIR "${CMAKE_INSTALL_INCLUDEDIR}/osgui")
set(OSGUI_INSTALL_TARGETS)
set(OSGUI_INSTALL_HEADERS osgui.h)

add_library(osgui STATIC osgui.cpp osgui.h)
add_library(OSGui::osgui ALIAS osgui)
add_library(OSGui::Core ALIAS osgui)
target_include_directories(
    osgui
    PUBLIC
        "$<BUILD_INTERFACE:${OSGUI_BUILD_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:${OSGUI_INSTALL_INCLUDE_DIR}>"
)
osgui_configure_library(osgui)
list(APPEND OSGUI_INSTALL_TARGETS osgui)

if(OSGUI_BUILD_EXAMPLES)
    add_library(osgui_examples STATIC examples/osgui_examples.cpp examples/osgui_examples.h)
    add_library(OSGui::osgui_examples ALIAS osgui_examples)
    add_library(OSGui::Examples ALIAS osgui_examples)
    target_include_directories(
        osgui_examples
        PUBLIC
            "$<BUILD_INTERFACE:${OSGUI_BUILD_INCLUDE_DIR}>"
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/examples>"
            "$<INSTALL_INTERFACE:${OSGUI_INSTALL_INCLUDE_DIR}>"
    )
    target_link_libraries(osgui_examples PUBLIC osgui)
    osgui_configure_library(osgui_examples)
    list(APPEND OSGUI_INSTALL_TARGETS osgui_examples)
    list(APPEND OSGUI_INSTALL_HEADERS examples/osgui_examples.h)
endif()

if(OSGUI_BUILD_OPENGL3)
    add_library(osgui_opengl3 STATIC osgui_impl_opengl3.cpp osgui_impl_opengl3.h)
    add_library(OSGui::osgui_opengl3 ALIAS osgui_opengl3)
    add_library(OSGui::OpenGL3 ALIAS osgui_opengl3)
    target_include_directories(
        osgui_opengl3
        PUBLIC
            "$<BUILD_INTERFACE:${OSGUI_BUILD_INCLUDE_DIR}>"
            "$<INSTALL_INTERFACE:${OSGUI_INSTALL_INCLUDE_DIR}>"
    )
    target_link_libraries(osgui_opengl3 PUBLIC osgui)
    osgui_configure_library(osgui_opengl3)
    list(APPEND OSGUI_INSTALL_TARGETS osgui_opengl3)
    list(APPEND OSGUI_INSTALL_HEADERS osgui_impl_opengl3.h)
endif()

if(OSGUI_BUILD_GLFW_BACKEND)
    find_package(glfw3 CONFIG REQUIRED)
    if(TARGET glfw)
        set(OSGUI_GLFW_TARGET glfw)
    elseif(TARGET glfw3::glfw)
        set(OSGUI_GLFW_TARGET glfw3::glfw)
    else()
        message(FATAL_ERROR "glfw3 was found, but neither glfw nor glfw3::glfw is available")
    endif()

    add_library(osgui_glfw STATIC osgui_impl_glfw.cpp osgui_impl_glfw.h)
    add_library(OSGui::osgui_glfw ALIAS osgui_glfw)
    add_library(OSGui::GLFW ALIAS osgui_glfw)
    target_include_directories(
        osgui_glfw
        PUBLIC
            "$<BUILD_INTERFACE:${OSGUI_BUILD_INCLUDE_DIR}>"
            "$<INSTALL_INTERFACE:${OSGUI_INSTALL_INCLUDE_DIR}>"
    )
    target_link_libraries(osgui_glfw PUBLIC osgui PRIVATE ${OSGUI_GLFW_TARGET})
    osgui_configure_library(osgui_glfw)
    list(APPEND OSGUI_INSTALL_TARGETS osgui_glfw)
    list(APPEND OSGUI_INSTALL_HEADERS osgui_impl_glfw.h)
endif()

if(WIN32 AND OSGUI_BUILD_WIN32_OPENGL2)
    add_library(
        osgui_win32_opengl2
        STATIC
        osgui_impl_win32.cpp
        osgui_impl_win32.h
        osgui_impl_opengl2.cpp
        osgui_impl_opengl2.h
    )
    add_library(OSGui::osgui_win32_opengl2 ALIAS osgui_win32_opengl2)
    add_library(OSGui::Win32OpenGL2 ALIAS osgui_win32_opengl2)
    target_include_directories(
        osgui_win32_opengl2
        PUBLIC
            "$<BUILD_INTERFACE:${OSGUI_BUILD_INCLUDE_DIR}>"
            "$<INSTALL_INTERFACE:${OSGUI_INSTALL_INCLUDE_DIR}>"
    )
    target_link_libraries(osgui_win32_opengl2 PUBLIC osgui PRIVATE opengl32 gdi32 user32)
    osgui_configure_library(osgui_win32_opengl2)
    list(APPEND OSGUI_INSTALL_TARGETS osgui_win32_opengl2)
    list(APPEND OSGUI_INSTALL_HEADERS osgui_impl_win32.h osgui_impl_opengl2.h)
endif()

if(WIN32 AND OSGUI_BUILD_DX11)
    add_library(osgui_dx11 STATIC osgui_impl_dx11.cpp osgui_impl_dx11.h)
    add_library(OSGui::osgui_dx11 ALIAS osgui_dx11)
    add_library(OSGui::DX11 ALIAS osgui_dx11)
    target_include_directories(
        osgui_dx11
        PUBLIC
            "$<BUILD_INTERFACE:${OSGUI_BUILD_INCLUDE_DIR}>"
            "$<INSTALL_INTERFACE:${OSGUI_INSTALL_INCLUDE_DIR}>"
    )
    target_link_libraries(osgui_dx11 PUBLIC osgui PRIVATE d3d11 d3dcompiler)
    osgui_configure_library(osgui_dx11)
    list(APPEND OSGUI_INSTALL_TARGETS osgui_dx11)
    list(APPEND OSGUI_INSTALL_HEADERS osgui_impl_dx11.h)
endif()

if(OSGUI_BUILD_DEMO)
    if(NOT WIN32)
        message(STATUS "OSGUI_BUILD_DEMO is enabled, but the showcase is only available on Windows")
    elseif(NOT TARGET osgui_win32_opengl2)
        message(FATAL_ERROR "OSGUI_BUILD_DEMO requires OSGUI_BUILD_WIN32_OPENGL2=ON")
    else()
        add_executable(osgui_demo WIN32 main.cpp osgui_demo.cpp osgui_theme_editor.cpp)
        target_link_libraries(osgui_demo PRIVATE osgui_win32_opengl2)
        osgui_configure_executable(osgui_demo)
    endif()
endif()

if(BUILD_TESTING AND OSGUI_BUILD_TESTS)
    add_executable(osgui_tests tests/osgui_tests.cpp)
    target_link_libraries(osgui_tests PRIVATE osgui)
    osgui_configure_executable(osgui_tests)
    add_test(NAME osgui_core COMMAND osgui_tests)
    set_tests_properties(osgui_core PROPERTIES LABELS "core" TIMEOUT 30)
endif()

if(OSGUI_INSTALL)
    set(OSGUI_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/OSGui")

    install(
        TARGETS ${OSGUI_INSTALL_TARGETS}
        EXPORT OSGuiTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${OSGUI_INSTALL_INCLUDE_DIR}"
    )
    install(FILES ${OSGUI_INSTALL_HEADERS} DESTINATION "${OSGUI_INSTALL_INCLUDE_DIR}")
    install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/OSGui")

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/OSGuiConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/OSGuiConfig.cmake"
        INSTALL_DESTINATION "${OSGUI_INSTALL_CMAKEDIR}"
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/OSGuiConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )

    install(
        EXPORT OSGuiTargets
        FILE OSGuiTargets.cmake
        NAMESPACE OSGui::
        DESTINATION "${OSGUI_INSTALL_CMAKEDIR}"
    )
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/OSGuiConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/OSGuiConfigVersion.cmake"
        DESTINATION "${OSGUI_INSTALL_CMAKEDIR}"
    )

    export(
        EXPORT OSGuiTargets
        FILE "${CMAKE_CURRENT_BINARY_DIR}/OSGuiTargets.cmake"
        NAMESPACE OSGui::
    )
endif()
