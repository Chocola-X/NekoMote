add_executable(nekomote_playback_probe main.cpp)
target_link_libraries(nekomote_playback_probe PRIVATE nekomote::runtime nekomote::asset_io
    nlohmann_json::nlohmann_json)

target_include_directories(nekomote_playback_probe PRIVATE ${CMAKE_SOURCE_DIR}/tools)
