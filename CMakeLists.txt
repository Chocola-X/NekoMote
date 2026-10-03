cmake_minimum_required(VERSION 3.25)

project(nekomote
    VERSION 0.1.0
    DESCRIPTION "NekoMote: native 2D model runtime and live wallpaper for Linux"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(NEKOMOTE_BUILD_PREVIEW "Build the GLFW preview application" ON)
option(NEKOMOTE_BUILD_TESTS   "Build unit tests" ON)
option(NEKOMOTE_ENABLE_ASAN   "Enable AddressSanitizer + UBSan" OFF)
option(NEKOMOTE_WERROR        "Treat warnings as errors" ON)

if(NEKOMOTE_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(nekomote_warnings INTERFACE)
target_compile_options(nekomote_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor
    -Wold-style-cast -Wcast-align -Wdouble-promotion -Wformat=2
    -Wnull-dereference -Woverloaded-virtual -Wpointer-arith
    -Wmissing-declarations)
if(NEKOMOTE_WERROR)
    target_compile_options(nekomote_warnings INTERFACE -Werror)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(src/model)

if(NEKOMOTE_BUILD_PREVIEW)
    find_package(OpenGL REQUIRED)
    add_subdirectory(apps/preview)
endif()

if(NEKOMOTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()
