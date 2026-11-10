add_executable(nekomote_anchor_audit main.cpp)
target_link_libraries(nekomote_anchor_audit PRIVATE nekomote::application nekomote::asset_io
    nlohmann_json::nlohmann_json)
