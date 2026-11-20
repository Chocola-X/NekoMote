find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET "wayland-client>=1.20")
pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
pkg_check_modules(WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET "egl>=1.5")
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(protocol_sources)
# ext-image-capture-source's generated type table references the foreign-toplevel
# handle interface. Link its definition; the host never binds that protocol.
set(protocol_xmls)
foreach(protocol wlr-layer-shell-unstable-v1 xdg-shell xdg-output-unstable-v1
        ext-foreign-toplevel-list-v1 ext-image-capture-source-v1 ext-image-copy-capture-v1)
    set(xml ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}.xml)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client-protocol.h)
    set(source ${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c)
    add_custom_command(OUTPUT ${header}
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header}
        DEPENDS ${xml} VERBATIM)
    add_custom_command(OUTPUT ${source}
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${source}
        DEPENDS ${xml} VERBATIM)
    list(APPEND protocol_sources ${source} ${header})
    list(APPEND protocol_xmls ${xml})
endforeach()
add_library(nekomote_layer_shell_protocols STATIC ${protocol_sources})
target_link_libraries(nekomote_layer_shell_protocols PRIVATE PkgConfig::WAYLAND_CLIENT)

add_executable(nekomote_layer_shell main.cpp options.cpp host.cpp desktop_gaze.cpp)
add_dependencies(nekomote_layer_shell nekomote_layer_shell_protocols)
target_include_directories(nekomote_layer_shell PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nekomote_layer_shell PRIVATE nekomote::abi nekomote::host_gpu nekomote::host_session nekomote_warnings
    nekomote_layer_shell_protocols PkgConfig::WAYLAND_CLIENT PkgConfig::WAYLAND_EGL
    PkgConfig::WAYLAND_CURSOR PkgConfig::EGL)
target_compile_features(nekomote_layer_shell PRIVATE cxx_std_20)
if(NEKOMOTE_BUILD_VULKAN)
    target_link_libraries(nekomote_layer_shell PRIVATE nekomote::host_vulkan)
    target_compile_definitions(nekomote_layer_shell PRIVATE
        NEKOMOTE_HAS_VULKAN VK_USE_PLATFORM_WAYLAND_KHR)
endif()
install(TARGETS nekomote_layer_shell RUNTIME DESTINATION bin COMPONENT layer_shell)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION share/licenses/nekomote COMPONENT layer_shell)
install(FILES ${protocol_xmls}
    protocols/README.md DESTINATION share/nekomote/licenses/layer-shell COMPONENT layer_shell)

if(NEKOMOTE_BUILD_TESTS)
    add_executable(nekomote_layer_shell_option_tests
        ${PROJECT_SOURCE_DIR}/tests/layer_shell/test_options.cpp options.cpp)
    target_include_directories(nekomote_layer_shell_option_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nekomote_layer_shell_option_tests PRIVATE
        Catch2::Catch2WithMain nekomote_warnings nekomote::abi nekomote::host_gpu)
    if(NEKOMOTE_BUILD_VULKAN)
        target_compile_definitions(nekomote_layer_shell_option_tests PRIVATE NEKOMOTE_HAS_VULKAN)
    endif()
    add_test(NAME layer_shell_options COMMAND nekomote_layer_shell_option_tests)
    add_executable(nekomote_desktop_gaze_math_tests
        ${PROJECT_SOURCE_DIR}/tests/layer_shell/test_desktop_gaze_math.cpp)
    target_include_directories(nekomote_desktop_gaze_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nekomote_desktop_gaze_math_tests PRIVATE Catch2::Catch2WithMain nekomote_warnings)
    target_compile_features(nekomote_desktop_gaze_math_tests PRIVATE cxx_std_20)
    add_test(NAME layer_shell_desktop_gaze_math COMMAND nekomote_desktop_gaze_math_tests)
    pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
    set(server_headers)
    foreach(protocol xdg-output-unstable-v1 ext-image-capture-source-v1 ext-image-copy-capture-v1)
        set(xml ${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}.xml)
        set(header ${CMAKE_CURRENT_BINARY_DIR}/${protocol}-server-protocol.h)
        add_custom_command(OUTPUT ${header}
            COMMAND ${WAYLAND_SCANNER} server-header ${xml} ${header}
            DEPENDS ${xml} VERBATIM)
        list(APPEND server_headers ${header})
    endforeach()
    add_executable(nekomote_desktop_gaze_wire_tests
        ${PROJECT_SOURCE_DIR}/tests/layer_shell/test_desktop_gaze_wire.cpp
        ${PROJECT_SOURCE_DIR}/tests/layer_shell/gaze_compositor.cpp desktop_gaze.cpp ${server_headers})
    add_dependencies(nekomote_desktop_gaze_wire_tests nekomote_layer_shell_protocols)
    target_include_directories(nekomote_desktop_gaze_wire_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(nekomote_desktop_gaze_wire_tests PRIVATE Catch2::Catch2WithMain
        nekomote_layer_shell_protocols PkgConfig::WAYLAND_CLIENT PkgConfig::WAYLAND_SERVER nekomote_warnings)
    target_compile_features(nekomote_desktop_gaze_wire_tests PRIVATE cxx_std_20)
    add_test(NAME layer_shell_desktop_gaze_wire COMMAND nekomote_desktop_gaze_wire_tests)
    set_tests_properties(layer_shell_desktop_gaze_wire PROPERTIES TIMEOUT 15)
    add_executable(nekomote_layer_shell_no_protocol_server
        ${PROJECT_SOURCE_DIR}/tests/layer_shell/no_layer_shell_server.c)
    target_link_libraries(nekomote_layer_shell_no_protocol_server PRIVATE
        PkgConfig::WAYLAND_SERVER nekomote_warnings)
    target_compile_features(nekomote_layer_shell_no_protocol_server PRIVATE c_std_11)
endif()
