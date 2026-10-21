find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET "wayland-client>=1.20")
pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
pkg_check_modules(WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET "egl>=1.5")
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(protocol_sources)
foreach(protocol wlr-layer-shell-unstable-v1 xdg-shell)
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
endforeach()
add_library(nekomote_layer_shell_protocols STATIC ${protocol_sources})
target_link_libraries(nekomote_layer_shell_protocols PRIVATE PkgConfig::WAYLAND_CLIENT)

add_executable(nekomote_layer_shell main.cpp options.cpp host.cpp)
add_dependencies(nekomote_layer_shell nekomote_layer_shell_protocols)
target_include_directories(nekomote_layer_shell PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nekomote_layer_shell PRIVATE nekomote::abi nekomote_warnings
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
install(FILES protocols/wlr-layer-shell-unstable-v1.xml protocols/xdg-shell.xml
    protocols/README.md DESTINATION share/nekomote/licenses/layer-shell COMPONENT layer_shell)

if(NEKOMOTE_BUILD_TESTS)
    add_executable(nekomote_layer_shell_option_tests
        ${PROJECT_SOURCE_DIR}/tests/layer_shell/test_options.cpp options.cpp)
    target_include_directories(nekomote_layer_shell_option_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nekomote_layer_shell_option_tests PRIVATE
        Catch2::Catch2WithMain nekomote_warnings)
    if(NEKOMOTE_BUILD_VULKAN)
        target_compile_definitions(nekomote_layer_shell_option_tests PRIVATE NEKOMOTE_HAS_VULKAN)
    endif()
    add_test(NAME layer_shell_options COMMAND nekomote_layer_shell_option_tests)
    pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
    add_executable(nekomote_layer_shell_no_protocol_server
        ${PROJECT_SOURCE_DIR}/tests/layer_shell/no_layer_shell_server.c)
    target_link_libraries(nekomote_layer_shell_no_protocol_server PRIVATE
        PkgConfig::WAYLAND_SERVER nekomote_warnings)
    target_compile_features(nekomote_layer_shell_no_protocol_server PRIVATE c_std_11)
endif()
