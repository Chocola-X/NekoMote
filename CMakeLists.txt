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

option(NEKOMOTE_USE_SYSTEM_DEPS "Use installed dependencies; never download vendored sources" OFF)
option(NEKOMOTE_BUILD_ABI     "Build the C ABI (no window or display required)" ON)
option(NEKOMOTE_BUILD_PREVIEW "Build the GLFW preview application" ON)
option(NEKOMOTE_BUILD_PLAYER  "Build interactive native player and PulseAudio device service" ${NEKOMOTE_BUILD_PREVIEW})
option(NEKOMOTE_BUILD_TOOLS   "Build offline tools (importer, ...)" ON)
option(NEKOMOTE_BUILD_TESTS   "Build unit tests" ON)
option(NEKOMOTE_BUILD_QT_QUICK "Build the isolated OpenGL Qt Quick diagnostic host" OFF)
set(NEKOMOTE_QT_MAJOR "6" CACHE STRING "Qt major for Qt Quick hosts and Plasma adapters (5 or 6)")
set_property(CACHE NEKOMOTE_QT_MAJOR PROPERTY STRINGS 5 6)
if(NOT NEKOMOTE_QT_MAJOR MATCHES "^[56]$")
    message(FATAL_ERROR "NEKOMOTE_QT_MAJOR must be 5 or 6")
endif()
option(NEKOMOTE_BUILD_PLASMA   "Build the Plasma wallpaper QML module and probe" OFF)
option(NEKOMOTE_BUILD_LAYER_SHELL "Build the standalone Wayland layer-shell wallpaper" OFF)
option(NEKOMOTE_BUILD_X11 "Build the standalone X11/EWMH wallpaper" OFF)
option(NEKOMOTE_BUILD_PLASMA_PROBE "Build local Plasma/KPackage diagnostic hosts" ON)
option(NEKOMOTE_BUILD_VULKAN  "Build the Vulkan render backend (requires Vulkan SDK)" OFF)
option(NEKOMOTE_ENABLE_ASAN   "Enable AddressSanitizer + UBSan" OFF)
option(NEKOMOTE_WERROR        "Treat warnings as errors" ON)
if(NEKOMOTE_BUILD_TESTS)
    enable_testing()
endif()

if(NEKOMOTE_BUILD_PLAYER AND NOT NEKOMOTE_BUILD_PREVIEW)
    message(FATAL_ERROR "NEKOMOTE_BUILD_PLAYER requires NEKOMOTE_BUILD_PREVIEW for the GL backend")
endif()
if(NEKOMOTE_BUILD_QT_QUICK AND NOT NEKOMOTE_BUILD_ABI)
    message(FATAL_ERROR "NEKOMOTE_BUILD_QT_QUICK requires NEKOMOTE_BUILD_ABI")
endif()
if(NEKOMOTE_BUILD_PLASMA AND NOT NEKOMOTE_BUILD_QT_QUICK)
    message(FATAL_ERROR "NEKOMOTE_BUILD_PLASMA requires NEKOMOTE_BUILD_QT_QUICK")
endif()
if(NEKOMOTE_BUILD_LAYER_SHELL AND NOT NEKOMOTE_BUILD_ABI)
    message(FATAL_ERROR "NEKOMOTE_BUILD_LAYER_SHELL requires NEKOMOTE_BUILD_ABI")
endif()
if(NEKOMOTE_BUILD_X11 AND NOT NEKOMOTE_BUILD_ABI)
    message(FATAL_ERROR "NEKOMOTE_BUILD_X11 requires NEKOMOTE_BUILD_ABI")
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

# Keep the archive layout by default. Distribution packages can use the Qt import directory.
set(NEKOMOTE_INSTALL_QMLDIR "qml" CACHE STRING "QML install directory relative to prefix")
if(IS_ABSOLUTE "${NEKOMOTE_INSTALL_QMLDIR}")
    message(FATAL_ERROR "NEKOMOTE_INSTALL_QMLDIR must be relative to CMAKE_INSTALL_PREFIX")
endif()
file(RELATIVE_PATH NEKOMOTE_QML_LIBRARY_RPATH
    "/${NEKOMOTE_INSTALL_QMLDIR}/NekoMote/Wallpaper" "/lib")

include(cmake/Dependencies.cmake)
if(NEKOMOTE_BUILD_QT_QUICK)
    if(NEKOMOTE_QT_MAJOR STREQUAL "5")
        find_package(Qt5 5.12 REQUIRED COMPONENTS Qml Quick OpenGL)
    else()
        find_package(Qt6 6.4 REQUIRED COMPONENTS Qml Quick OpenGL)
    endif()
endif()

add_subdirectory(src/model)
add_subdirectory(src/runtime)
add_subdirectory(src/asset_io)
add_subdirectory(src/application)
add_subdirectory(src/ui)

add_subdirectory(tools/import_emote)
add_subdirectory(tools/audio_convert)
add_subdirectory(tools/model_convert)
add_subdirectory(tools/png_recompress)

if(NEKOMOTE_BUILD_TOOLS)
    add_subdirectory(tools/pose_probe)
    add_subdirectory(tools/playback_probe)
    add_subdirectory(tools/session_probe)
    add_subdirectory(tools/runtime_audit)
endif()

if(NEKOMOTE_BUILD_PREVIEW OR NEKOMOTE_BUILD_ABI)
    add_subdirectory(src/render_gl)
endif()
if(NEKOMOTE_BUILD_VULKAN)
    add_subdirectory(src/render_vk)
endif()
if(NEKOMOTE_BUILD_ABI)
    add_subdirectory(src/abi)
endif()
if(NEKOMOTE_BUILD_QT_QUICK)
    add_subdirectory(src/host_qt_quick)
    add_subdirectory(apps/qt_quick_host)
endif()
if(NEKOMOTE_BUILD_ABI OR NEKOMOTE_BUILD_PLAYER OR NEKOMOTE_BUILD_PLASMA)
    add_subdirectory(src/audio)
endif()
if(NEKOMOTE_BUILD_PLASMA)
    if(NEKOMOTE_QT_MAJOR STREQUAL "5")
        add_subdirectory(adapters/plasma5)
    else()
        add_subdirectory(adapters/plasma)
    endif()
endif()
if(NEKOMOTE_BUILD_LAYER_SHELL)
    add_subdirectory(adapters/layer_shell)
endif()
if(NEKOMOTE_BUILD_X11)
    add_subdirectory(adapters/x11)
endif()
if(NEKOMOTE_BUILD_PREVIEW)
    find_package(OpenGL REQUIRED)
    add_subdirectory(apps/preview)
    if(NEKOMOTE_BUILD_ABI)
        add_subdirectory(apps/abi_host)
    endif()
    if(NEKOMOTE_BUILD_PLAYER)
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
    add_subdirectory(tests/unit)
    if(NEKOMOTE_BUILD_PLASMA)
        add_executable(nekomote_plasma_module_test tests/plasma/test_qml_module.cpp)
        target_link_libraries(nekomote_plasma_module_test PRIVATE
            Qt${NEKOMOTE_QT_MAJOR}::Qml Qt${NEKOMOTE_QT_MAJOR}::Quick nekomote_warnings)
        add_dependencies(nekomote_plasma_module_test nekomote_plasma_stage)
        add_test(NAME plasma_module_import COMMAND nekomote_plasma_module_test ${NEKOMOTE_STAGE_DIR}
                ${NEKOMOTE_STAGE_DIR}/${NEKOMOTE_INSTALL_QMLDIR})
        set_tests_properties(plasma_module_import PROPERTIES LABELS "qt" TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
    if(NEKOMOTE_BUILD_PREVIEW)
        add_subdirectory(tests/render)
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(TARGET nekomote_plasma_wallpaper_layers)
        find_program(NEKOMOTE_FLOCK_LAYERS flock REQUIRED)
        add_test(NAME plasma_wallpaper_layers
            COMMAND ${NEKOMOTE_FLOCK_LAYERS} ${CMAKE_SOURCE_DIR}/build/gpu-validation.lock
                $<TARGET_FILE:nekomote_plasma_wallpaper_layers>)
        set_tests_properties(plasma_wallpaper_layers PROPERTIES LABELS "qt;gpu;integration"
            SKIP_RETURN_CODE 77 TIMEOUT 60
            # This screenshot replay measures pixels/input, not presentation FPS.
            ENVIRONMENT "NEKOMOTE_TEST_ASSETS_DIR=${PROJECT_SOURCE_DIR}/assets;QT_SCALE_FACTOR=1;QSG_NO_VSYNC=1")
        if(NEKOMOTE_BUILD_VULKAN)
            add_test(NAME plasma_wallpaper_layers_vulkan
                COMMAND ${NEKOMOTE_FLOCK_LAYERS} ${CMAKE_SOURCE_DIR}/build/gpu-validation.lock
                    $<TARGET_FILE:nekomote_plasma_wallpaper_layers>)
            set_tests_properties(plasma_wallpaper_layers_vulkan PROPERTIES
                LABELS "qt;gpu;vulkan;integration" SKIP_RETURN_CODE 77 TIMEOUT 90
                ENVIRONMENT "NEKOMOTE_TEST_ASSETS_DIR=${PROJECT_SOURCE_DIR}/assets;NEKOMOTE_TEST_BACKEND=vulkan;NEKOMOTE_TEST_DUAL_BACKEND=1;NEKOMOTE_VK_VALIDATION=1;QT_FORCE_STDERR_LOGGING=1;VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;QT_SCALE_FACTOR=1;QSG_NO_VSYNC=1")
        endif()
    endif()
    if(TARGET nekomote_plasma_item_safety)
        find_program(NEKOMOTE_FLOCK flock REQUIRED)
        add_test(NAME plasma_item_safety
            COMMAND ${NEKOMOTE_FLOCK} ${CMAKE_SOURCE_DIR}/build/gpu-validation.lock
                $<TARGET_FILE:nekomote_plasma_item_safety>)
        set_tests_properties(plasma_item_safety PROPERTIES LABELS "qt;gpu" TIMEOUT 30)
    endif()
    if(TARGET nekomote_plasma_config_reject)
        add_test(NAME plasma_config_reject
            COMMAND $<TARGET_FILE:nekomote_plasma_config_reject>)
        set_tests_properties(plasma_config_reject PROPERTIES LABELS "qt" TIMEOUT 15)
    endif()
    if(TARGET nekomote_plasma_package_probe AND Python3_Interpreter_FOUND)
        find_program(NEKOMOTE_FLOCK2 flock REQUIRED)
        add_test(NAME plasma_package_probe
            COMMAND ${NEKOMOTE_FLOCK2} ${CMAKE_SOURCE_DIR}/build/gpu-validation.lock
                ${Python3_EXECUTABLE} -m unittest tests.plasma.test_package_probe
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        set_tests_properties(plasma_package_probe PROPERTIES
            LABELS "qt;gpu" TIMEOUT 180
            ENVIRONMENT "NEKOMOTE_TEST_MODEL=${CMAKE_SOURCE_DIR}/assets/characters/coconut/casual/model.nkm;NEKOMOTE_TEST_SESSION=${CMAKE_SOURCE_DIR}/assets/characters/coconut/casual/session.nks.json;NEKOMOTE_PLASMA_PROBE=$<TARGET_FILE:nekomote_plasma_package_probe>;NEKOMOTE_PLASMA_STAGE=${CMAKE_BINARY_DIR}/adapters/plasma/stage;NEKOMOTE_EVIDENCE_DIR=${CMAKE_BINARY_DIR}/evidence/plasma-tests")
    endif()
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
        if(NEKOMOTE_BUILD_QT_QUICK)
            add_test(NAME python_qt_quick_host
                COMMAND ${Python3_EXECUTABLE} -m unittest tests.reference_capture.test_qt_quick_host
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
            set_tests_properties(python_qt_quick_host PROPERTIES
                LABELS "python;gpu"
                ENVIRONMENT "NEKOMOTE_QT_QUICK_HOST=$<TARGET_FILE:nekomote_qt_quick_host>")
        endif()
    endif()
endif()
