cmake_minimum_required(VERSION 3.25)

add_executable(nekomote_preview main.cpp)

target_link_libraries(nekomote_preview
    PRIVATE
        nekomote::model
        glfw
        OpenGL::GL)

target_compile_features(nekomote_preview PRIVATE cxx_std_20)
