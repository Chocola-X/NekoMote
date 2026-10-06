add_executable(nekomote_player main.cpp asset_catalog.cpp)
target_link_libraries(nekomote_player PRIVATE nekomote::asset_io nekomote::application nekomote::audio nekomote::render_gl glfw OpenGL::GL nlohmann_json::nlohmann_json)
target_include_directories(nekomote_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS nekomote_player RUNTIME DESTINATION bin)
