add_executable(nekomote_runtime_audit main.cpp)
target_link_libraries(nekomote_runtime_audit PRIVATE nekomote::runtime nekomote::importer
    nlohmann_json::nlohmann_json)
