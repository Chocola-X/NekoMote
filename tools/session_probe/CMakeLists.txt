add_executable(nekomote_session_probe main.cpp)
target_link_libraries(nekomote_session_probe PRIVATE nekomote::application nekomote::asset_io
    nlohmann_json::nlohmann_json)
target_include_directories(nekomote_session_probe PRIVATE ${CMAKE_SOURCE_DIR}/tools)
