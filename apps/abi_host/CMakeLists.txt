find_package(ZLIB REQUIRED)

add_executable(nekomote_abi_host main.c)
target_link_libraries(nekomote_abi_host
    PRIVATE
        nekomote::abi
        glfw
        OpenGL::GL
        nekomote_gl_loader
        ZLIB::ZLIB)
if(NEKOMOTE_BUILD_VULKAN)
    target_compile_definitions(nekomote_abi_host PRIVATE NEKOMOTE_HAS_VULKAN)
    target_link_libraries(nekomote_abi_host PRIVATE nekomote::host_vulkan)
endif()
target_compile_features(nekomote_abi_host PRIVATE c_std_11)
target_compile_options(nekomote_abi_host PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2
    -Wno-double-promotion)

install(TARGETS nekomote_abi_host RUNTIME DESTINATION bin COMPONENT runtime)
