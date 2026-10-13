# Plasma wallpaper QML module and isolated probe. Default OFF; requires Qt and ABI.
find_package(Qt6 6.4 REQUIRED COMPONENTS Qml Quick OpenGL)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(NEKOMOTE_BUILD_PLASMA_PROBE)
    find_package(KF6Config CONFIG REQUIRED)
    find_package(KF6Package CONFIG REQUIRED)
    find_package(Plasma CONFIG REQUIRED)
    find_package(PlasmaQuick CONFIG REQUIRED)
endif()

# Only this optional shared leaf needs PIC from the static core dependencies.
foreach(target nekomote_model nekomote_runtime nekomote_asset_io nekomote_stb
               nekomote_application nekomote_render_gl nekomote_abi
               nekomote_audio nekomote_ui nekomote_gl_loader)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
if(TARGET nekomote_render_vk)
    set_target_properties(nekomote_render_vk PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(nekomote_plasma_qml SHARED wallpaper_item.cpp wallpaper_item.hpp)
add_library(nekomote::plasma_qml ALIAS nekomote_plasma_qml)
set_target_properties(nekomote_plasma_qml PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
    INSTALL_RPATH "")
target_include_directories(nekomote_plasma_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nekomote_plasma_qml PUBLIC
    nekomote::host_qt_quick Qt6::Quick Qt6::OpenGL)
target_link_libraries(nekomote_plasma_qml PRIVATE nekomote_warnings)

if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    qt_policy(SET QTP0001 NEW)
endif()
qt_add_qml_module(nekomote_plasma_qml
    URI NekoMote.Wallpaper
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/NekoMote/Wallpaper
)

# QML plugin needs to find libnekomote_plasma_qml.so at runtime via relative RPATH.
# Install layout: <prefix>/qml/NekoMote/Wallpaper/plugin.so, <prefix>/lib/libnekomote_plasma_qml.so
# $ORIGIN/../../../lib resolves from qml/NekoMote/Wallpaper/ to <prefix>/lib/
set_target_properties(nekomote_plasma_qmlplugin PROPERTIES
    INSTALL_RPATH "$ORIGIN/../../../lib")

# Limit the generated free-function exception to Qt registration output.
set_source_files_properties(
    ${CMAKE_CURRENT_BINARY_DIR}/nekomote_plasma_qml_qmltyperegistrations.cpp
    PROPERTIES COMPILE_OPTIONS -Wno-missing-declarations)

add_custom_target(nekomote_plasma_package ALL
    COMMAND ${CMAKE_COMMAND} -E rm -f
        ${CMAKE_CURRENT_BINARY_DIR}/wallpapers/uk.nekopara.nekomote/contents/config/main.qml
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/package
        ${CMAKE_CURRENT_BINARY_DIR}/wallpapers/uk.nekopara.nekomote)

if(NEKOMOTE_BUILD_PLASMA_PROBE)
    add_executable(nekomote_plasma_probe probe/main.cpp)
    set_target_properties(nekomote_plasma_probe PROPERTIES AUTOMOC ON)
    target_compile_definitions(nekomote_plasma_probe PRIVATE
        NEKOMOTE_QML_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}"
        NEKOMOTE_WALLPAPER_QML="${CMAKE_CURRENT_SOURCE_DIR}/probe/scene.qml"
        NEKOMOTE_CONFIG_QML="${CMAKE_CURRENT_SOURCE_DIR}/package/contents/ui/config.qml")
    target_link_libraries(nekomote_plasma_probe PRIVATE
        nekomote::plasma_qml
        Qt6::Qml Qt6::Quick Qt6::OpenGL nekomote_warnings)
    add_dependencies(nekomote_plasma_probe nekomote_plasma_qmlplugin nekomote_plasma_package)

    if(NEKOMOTE_BUILD_TESTS)
        add_executable(nekomote_plasma_wallpaper_layers ../../tests/render/plasma_wallpaper_layers.cpp)
        target_link_libraries(nekomote_plasma_wallpaper_layers PRIVATE
            nekomote::plasma_qml Qt6::Qml Qt6::Quick nekomote_warnings)
        target_compile_definitions(nekomote_plasma_wallpaper_layers PRIVATE
            NEKOMOTE_QML_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}")
        add_dependencies(nekomote_plasma_wallpaper_layers nekomote_plasma_qmlplugin)
        add_executable(nekomote_plasma_item_safety ../../tests/render/plasma_item_safety.cpp)
        target_link_libraries(nekomote_plasma_item_safety PRIVATE
            nekomote::plasma_qml
            Qt6::Qml Qt6::Quick nekomote_warnings)
        target_compile_definitions(nekomote_plasma_item_safety PRIVATE
            NEKOMOTE_QML_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}")
        add_dependencies(nekomote_plasma_item_safety nekomote_plasma_qmlplugin)

        add_executable(nekomote_plasma_config_reject ../../tests/render/plasma_config_reject.cpp)
        target_link_libraries(nekomote_plasma_config_reject PRIVATE
            nekomote::plasma_qml
            Qt6::Qml Qt6::Quick nekomote_warnings)
        target_compile_definitions(nekomote_plasma_config_reject PRIVATE
            NEKOMOTE_QML_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}")
        add_dependencies(nekomote_plasma_config_reject nekomote_plasma_qmlplugin)
    endif()

    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
    add_executable(nekomote_plasma_package_probe probe/package_main.cpp)
    target_compile_definitions(nekomote_plasma_package_probe PRIVATE
        NEKOMOTE_PACKAGE_PATH="${CMAKE_CURRENT_BINARY_DIR}/wallpapers/uk.nekopara.nekomote"
        NEKOMOTE_CONTAINMENT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/probe/containment"
        NEKOMOTE_QML_IMPORT_PATH="${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(nekomote_plasma_package_probe PRIVATE
        nekomote::plasma_qml
        Plasma::Plasma Plasma::PlasmaQuick KF6::Package
        Qt6::Widgets Qt6::Quick nekomote_warnings)
    add_dependencies(nekomote_plasma_package_probe nekomote_plasma_qmlplugin nekomote_plasma_package)

endif() # NEKOMOTE_BUILD_PLASMA_PROBE

# Install layout:
#   <prefix>/lib/libnekomote_plasma_qml.so
#   <prefix>/qml/NekoMote/Wallpaper/{qmldir,qmltypes,plugin.so}
#   <prefix>/share/plasma/wallpapers/uk.nekopara.nekomote/
#   <prefix>/share/plasma/plasmoids/org.nekomote.validation/
install(TARGETS nekomote_plasma_qml
    LIBRARY DESTINATION lib
    COMPONENT plasma)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/NekoMote/Wallpaper/qmldir
    ${CMAKE_CURRENT_BINARY_DIR}/NekoMote/Wallpaper/nekomote_plasma_qml.qmltypes
    DESTINATION qml/NekoMote/Wallpaper
    COMPONENT plasma)
install(TARGETS nekomote_plasma_qmlplugin
    LIBRARY DESTINATION qml/NekoMote/Wallpaper
    COMPONENT plasma)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/wallpapers/uk.nekopara.nekomote
    DESTINATION share/plasma/wallpapers
    COMPONENT plasma)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/probe/containment/
    DESTINATION share/plasma/plasmoids/org.nekomote.validation
    COMPONENT plasma)

# Deployable stage: self-contained directory with correct RPATH via cmake --install.
# Independent hosts load via QML import + relative RPATH, not build-tree paths.
set(NEKOMOTE_STAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/stage)
add_custom_target(nekomote_plasma_stage
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${NEKOMOTE_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
        --prefix ${NEKOMOTE_STAGE_DIR} --component plasma
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/plasma_stage_manifest.py
        ${NEKOMOTE_STAGE_DIR}/_gen_manifest.py
    COMMAND ${Python3_EXECUTABLE} ${NEKOMOTE_STAGE_DIR}/_gen_manifest.py
        ${NEKOMOTE_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${NEKOMOTE_STAGE_DIR}/_gen_manifest.py
    DEPENDS nekomote_plasma_qml nekomote_plasma_qmlplugin nekomote_plasma_package
    COMMENT "Staging deployable Plasma wallpaper package via cmake --install")

add_custom_target(nekomote_plasma_stage_clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${NEKOMOTE_STAGE_DIR}
    COMMENT "Cleaning Plasma stage")
