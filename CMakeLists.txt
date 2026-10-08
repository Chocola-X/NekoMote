cmake_minimum_required(VERSION 3.25)

project(nekomote
    VERSION 0.1.0
    DESCRIPTION "NekoMote: native 2D model runtime and live wallpaper for Linux"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(NEKOMOTE_BUILD_ABI     "Build the C ABI (no window or display required)" ON)
option(NEKOMOTE_BUILD_PREVIEW "Build the GLFW preview application" ON)
option(NEKOMOTE_BUILD_PLAYER  "Build interactive native player and PulseAudio device service" ${NEKOMOTE_BUILD_PREVIEW})
option(NEKOMOTE_BUILD_TOOLS   "Build offline tools (importer, ...)" ON)
option(NEKOMOTE_BUILD_TESTS   "Build unit tests" ON)
option(NEKOMOTE_BUILD_QT_QUICK "Build the isolated OpenGL Qt Quick diagnostic host" OFF)
option(NEKOMOTE_ENABLE_ASAN   "Enable AddressSanitizer + UBSan" OFF)
option(NEKOMOTE_WERROR        "Treat warnings as errors" ON)

if(NEKOMOTE_BUILD_PLAYER AND NOT NEKOMOTE_BUILD_PREVIEW)
    message(FATAL_ERROR "NEKOMOTE_BUILD_PLAYER requires NEKOMOTE_BUILD_PREVIEW for the GL backend")
endif()
if(NEKOMOTE_BUILD_QT_QUICK AND NOT NEKOMOTE_BUILD_ABI)
    message(FATAL_ERROR "NEKOMOTE_BUILD_QT_QUICK requires NEKOMOTE_BUILD_ABI")
endif()

if(NEKOMOTE_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(nekomote_warnings INTERFACE)
target_compile_options(nekomote_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    -Wcast-align -Wdouble-promotion -Wformat=2
    -Wnull-dereference -Wpointer-arith -Wmissing-declarations
    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
    $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
    $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
if(NEKOMOTE_WERROR)
    target_compile_options(nekomote_warnings INTERFACE -Werror)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(src/model)
add_subdirectory(src/runtime)
add_subdirectory(src/asset_io)
add_subdirectory(src/application)

if(NEKOMOTE_BUILD_TOOLS)
    add_subdirectory(tools/import_emote)
    add_subdirectory(tools/pose_probe)
    add_subdirectory(tools/playback_probe)
    add_subdirectory(tools/session_probe)
    add_subdirectory(tools/runtime_audit)
endif()

if(NEKOMOTE_BUILD_PREVIEW OR NEKOMOTE_BUILD_ABI)
    add_subdirectory(src/render_gl)
endif()
if(NEKOMOTE_BUILD_ABI)
    add_subdirectory(src/abi)
endif()
if(NEKOMOTE_BUILD_QT_QUICK)
    add_subdirectory(src/host_qt_quick)
    add_subdirectory(apps/qt_quick_host)
endif()
if(NEKOMOTE_BUILD_PREVIEW)
    find_package(OpenGL REQUIRED)
    add_subdirectory(apps/preview)
    if(NEKOMOTE_BUILD_ABI)
        add_subdirectory(apps/abi_host)
    endif()
    if(NEKOMOTE_BUILD_PLAYER)
        add_subdirectory(src/audio)
        add_subdirectory(apps/player)
        if(NEKOMOTE_BUILD_TOOLS)
            add_subdirectory(tools/audio_probe)
        endif()
    endif()
    if(NEKOMOTE_BUILD_TOOLS)
        add_subdirectory(tools/frame_render)
    endif()
endif()

if(NEKOMOTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
    if(NEKOMOTE_BUILD_PREVIEW)
        add_subdirectory(tests/render)
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME python_tools
            COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests/reference_capture
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        set_tests_properties(python_tools PROPERTIES LABELS python)
        if(NEKOMOTE_BUILD_PREVIEW AND NEKOMOTE_BUILD_ABI)
            add_test(NAME python_abi_host
                COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests/abi_host
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
            set_tests_properties(python_abi_host PROPERTIES
                LABELS "python;gpu"
                ENVIRONMENT "NEKOMOTE_ABI_HOST=$<TARGET_FILE:nekomote_abi_host>")
        endif()
    endif()
endif()
