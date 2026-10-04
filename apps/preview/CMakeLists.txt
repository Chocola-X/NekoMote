cmake_minimum_required(VERSION 3.25)

add_executable(nekomote_preview main.cpp)

target_link_libraries(nekomote_preview
    PRIVATE
        nekomote::asset_io
        nekomote::runtime
        nekomote::render_gl
        glfw
        OpenGL::GL
        nlohmann_json::nlohmann_json)

target_compile_features(nekomote_preview PRIVATE cxx_std_20)
