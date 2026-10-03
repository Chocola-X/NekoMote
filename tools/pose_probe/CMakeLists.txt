add_executable(nekomote_pose main.cpp)
target_link_libraries(nekomote_pose PRIVATE nekomote::runtime nekomote::asset_io
    nlohmann_json::nlohmann_json)
