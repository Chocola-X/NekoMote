find_package(X11 REQUIRED COMPONENTS Xrandr Xext)
find_package(OpenGL REQUIRED COMPONENTS OpenGL GLX)

add_executable(nekomote_x11 main.cpp options.cpp host.cpp)
target_link_libraries(nekomote_x11 PRIVATE nekomote::abi nekomote_warnings
    X11::X11 X11::Xrandr X11::Xext OpenGL::OpenGL OpenGL::GLX)
target_compile_features(nekomote_x11 PRIVATE cxx_std_20)
if(NEKOMOTE_BUILD_VULKAN)
    target_compile_definitions(nekomote_x11 PRIVATE NEKOMOTE_HAS_VULKAN VK_USE_PLATFORM_XLIB_KHR)
    target_link_libraries(nekomote_x11 PRIVATE nekomote::host_vulkan)
endif()
install(TARGETS nekomote_x11 RUNTIME DESTINATION bin COMPONENT x11)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION share/licenses/nekomote COMPONENT x11)

if(NEKOMOTE_BUILD_TESTS)
    add_executable(nekomote_x11_probe EXCLUDE_FROM_ALL
        ${PROJECT_SOURCE_DIR}/tests/x11/probe.cpp)
    target_link_libraries(nekomote_x11_probe PRIVATE nekomote_warnings X11::X11 X11::Xext)
    add_executable(nekomote_x11_option_tests
        ${PROJECT_SOURCE_DIR}/tests/x11/test_options.cpp options.cpp)
    target_include_directories(nekomote_x11_option_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nekomote_x11_option_tests PRIVATE
        Catch2::Catch2WithMain nekomote_warnings)
    if(NEKOMOTE_BUILD_VULKAN)
        target_compile_definitions(nekomote_x11_option_tests PRIVATE NEKOMOTE_HAS_VULKAN)
    endif()
    add_test(NAME x11_options COMMAND nekomote_x11_option_tests)
endif()
