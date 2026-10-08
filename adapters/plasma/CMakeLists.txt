# Plasma wallpaper QML module and isolated probe. Default OFF; requires Qt and ABI.
find_package(Qt6 6.9.1 EXACT REQUIRED COMPONENTS Qml Quick OpenGL)

add_library(nekomote_plasma_qml STATIC wallpaper_item.cpp wallpaper_item.hpp)
add_library(nekomote::plasma_qml ALIAS nekomote_plasma_qml)
set_target_properties(nekomote_plasma_qml PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON)
target_include_directories(nekomote_plasma_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nekomote_plasma_qml PUBLIC
    nekomote::host_qt_quick Qt6::Quick Qt6::OpenGL)
# Qt6 QML type registration generates free functions without prior declarations,
# so -Wmissing-declarations (in nekomote_warnings) is omitted for this target.
target_compile_options(nekomote_plasma_qml PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    -Wcast-align -Wdouble-promotion -Wformat=2
    -Wnull-dereference -Wpointer-arith
    -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
if(NEKOMOTE_WERROR)
    target_compile_options(nekomote_plasma_qml PRIVATE -Werror)
endif()

qt_add_qml_module(nekomote_plasma_qml
    URI NekoMote.Wallpaper
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/NekoMote/Wallpaper
    QML_FILES
        package/contents/ui/main.qml
        package/contents/config/main.qml)

# qt_add_qml_module generates registration/cache code that triggers
# -Wmissing-declarations. Disable for generated files via COMPILE_FLAGS
# which appends after all target-level COMPILE_OPTIONS.
file(GLOB NEKOMOTE_QML_GENERATED_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/nekomote_plasma_qml_qmltyperegistrations.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmlcache/*.cpp)
set_source_files_properties(${NEKOMOTE_QML_GENERATED_SOURCES} PROPERTIES
    COMPILE_FLAGS -Wno-missing-declarations)

add_executable(nekomote_plasma_probe probe/main.cpp)
set_target_properties(nekomote_plasma_probe PROPERTIES AUTOMOC ON)
target_compile_definitions(nekomote_plasma_probe PRIVATE
    NEKOMOTE_QML_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(nekomote_plasma_probe PRIVATE
    nekomote::plasma_qml nekomote_plasma_qmlplugin nekomote_plasma_qmlplugin_init
    Qt6::Qml Qt6::Quick Qt6::OpenGL nekomote_warnings)
qt_import_qml_plugins(nekomote_plasma_probe)

if(NEKOMOTE_BUILD_TESTS)
    add_executable(nekomote_plasma_item_safety ../../tests/render/plasma_item_safety.cpp)
    target_link_libraries(nekomote_plasma_item_safety PRIVATE
        nekomote::plasma_qml nekomote_plasma_qmlplugin nekomote_plasma_qmlplugin_init
        Qt6::Qml Qt6::Quick nekomote_warnings)
    qt_import_qml_plugins(nekomote_plasma_item_safety)
endif()
