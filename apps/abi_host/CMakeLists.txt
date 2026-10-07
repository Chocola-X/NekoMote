add_executable(nekomote_abi_host main.c)
target_link_libraries(nekomote_abi_host
    PRIVATE
        nekomote::abi
        glfw
        OpenGL::GL
        nekomote_gl_loader
        pulse-simple
        pulse
        pthread)
target_compile_features(nekomote_abi_host PRIVATE c_std_11)
target_compile_options(nekomote_abi_host PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2
    -Wno-format-truncation -Wno-double-promotion)
