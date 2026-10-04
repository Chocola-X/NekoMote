add_executable(nekomote_player main.cpp)
target_link_libraries(nekomote_player PRIVATE nekomote::asset_io nekomote::application nekomote::audio nekomote::render_gl glfw OpenGL::GL)
install(TARGETS nekomote_player RUNTIME DESTINATION bin)
