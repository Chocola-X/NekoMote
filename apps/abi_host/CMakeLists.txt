find_package(ZLIB REQUIRED)

set(ABI_HOST_SOURCES main.c)
if(NEKOMOTE_BUILD_VULKAN)
    list(APPEND ABI_HOST_SOURCES vk_host_c.c)
endif()

add_executable(nekomote_abi_host ${ABI_HOST_SOURCES})
target_link_libraries(nekomote_abi_host
    PRIVATE
        nekomote::abi
        glfw
        OpenGL::GL
        nekomote_gl_loader
        ZLIB::ZLIB)
if(NEKOMOTE_BUILD_VULKAN)
    target_compile_definitions(nekomote_abi_host PRIVATE NEKOMOTE_HAS_VULKAN)
    target_link_libraries(nekomote_abi_host PRIVATE Vulkan::Vulkan)
endif()
target_compile_features(nekomote_abi_host PRIVATE c_std_11)
target_compile_options(nekomote_abi_host PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2
    -Wno-double-promotion)

install(TARGETS nekomote_abi_host RUNTIME DESTINATION bin COMPONENT runtime)
