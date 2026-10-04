add_executable(nekomote_frame_render main.cpp)
target_link_libraries(nekomote_frame_render PRIVATE nekomote::asset_io nekomote::runtime
    nekomote::render_gl glfw nlohmann_json::nlohmann_json)
