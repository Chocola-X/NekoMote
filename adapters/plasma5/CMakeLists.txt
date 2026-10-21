# Plasma 5 has a distinct package/QML entry point. The native item and renderer
# are shared with Plasma 6; no wallpaper/application policy is duplicated here.
find_package(Qt5 5.12 REQUIRED COMPONENTS Qml Quick OpenGL)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
if(NEKOMOTE_BUILD_PLASMA_PROBE)
    message(FATAL_ERROR "Plasma 6 probes are not Plasma 5 hosts; set NEKOMOTE_BUILD_PLASMA_PROBE=OFF")
endif()
foreach(target nekomote_model nekomote_runtime nekomote_asset_io nekomote_stb
               nekomote_application nekomote_render_gl nekomote_abi nekomote_audio
               nekomote_ui nekomote_gl_loader)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
if(TARGET nekomote_render_vk)
    set_target_properties(nekomote_render_vk PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(nekomote_plasma_qml SHARED ../plasma/wallpaper_item.cpp ../plasma/wallpaper_item.hpp)
add_library(nekomote::plasma_qml ALIAS nekomote_plasma_qml)
set_target_properties(nekomote_plasma_qml PROPERTIES AUTOMOC ON INSTALL_RPATH "")
target_include_directories(nekomote_plasma_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../plasma)
target_link_libraries(nekomote_plasma_qml PUBLIC nekomote::host_qt_quick
    Qt5::Quick Qt5::OpenGL PRIVATE nekomote_warnings)

add_library(nekomote_plasma_qmlplugin MODULE plugin.cpp)
set_target_properties(nekomote_plasma_qmlplugin PROPERTIES AUTOMOC ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/NekoMote/Wallpaper
    INSTALL_RPATH "$ORIGIN/${NEKOMOTE_QML_LIBRARY_RPATH}")
target_link_libraries(nekomote_plasma_qmlplugin PRIVATE nekomote::plasma_qml Qt5::Qml
    nekomote_warnings)
configure_file(qmldir ${CMAKE_CURRENT_BINARY_DIR}/NekoMote/Wallpaper/qmldir COPYONLY)

add_custom_target(nekomote_plasma_package ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/../plasma/package
        ${CMAKE_CURRENT_BINARY_DIR}/wallpapers/uk.nekopara.nekomote
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/package
        ${CMAKE_CURRENT_BINARY_DIR}/wallpapers/uk.nekopara.nekomote
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/containment
        ${CMAKE_CURRENT_BINARY_DIR}/plasmoids/org.nekomote.validation)
install(TARGETS nekomote_plasma_qml LIBRARY DESTINATION lib COMPONENT plasma)
install(TARGETS nekomote_plasma_qmlplugin LIBRARY DESTINATION ${NEKOMOTE_INSTALL_QMLDIR}/NekoMote/Wallpaper COMPONENT plasma)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/NekoMote/Wallpaper/qmldir
    DESTINATION ${NEKOMOTE_INSTALL_QMLDIR}/NekoMote/Wallpaper COMPONENT plasma)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/wallpapers/uk.nekopara.nekomote
    DESTINATION share/plasma/wallpapers COMPONENT plasma)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/plasmoids/org.nekomote.validation
    DESTINATION share/plasma/plasmoids COMPONENT plasma)
set(NEKOMOTE_STAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/stage)
set(NEKOMOTE_STAGE_DIR ${NEKOMOTE_STAGE_DIR} PARENT_SCOPE)
add_custom_target(nekomote_plasma_stage
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${NEKOMOTE_STAGE_DIR}
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${NEKOMOTE_STAGE_DIR}
        --component plasma
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/plasma_stage_manifest.py
        ${NEKOMOTE_STAGE_DIR}
    DEPENDS nekomote_plasma_qml nekomote_plasma_qmlplugin nekomote_plasma_package)

if(NEKOMOTE_BUILD_TESTS)
    add_executable(nekomote_plasma5_package_test ../../tests/plasma/test_plasma5_package.cpp)
    set_target_properties(nekomote_plasma5_package_test PROPERTIES AUTOMOC ON)
    target_link_libraries(nekomote_plasma5_package_test PRIVATE Qt5::Qml Qt5::Quick nekomote_warnings)
    add_dependencies(nekomote_plasma5_package_test nekomote_plasma_stage)
    add_test(NAME plasma5_package_import COMMAND nekomote_plasma5_package_test
        ${NEKOMOTE_STAGE_DIR} ${NEKOMOTE_STAGE_DIR}/${NEKOMOTE_INSTALL_QMLDIR})
    set_tests_properties(plasma5_package_import PROPERTIES LABELS "qt" TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
