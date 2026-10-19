cmake_minimum_required(VERSION 3.25)

add_executable(nekomote_import main.cpp)
target_link_libraries(nekomote_import
    PRIVATE
        nekomote::importer
        nekomote_warnings
        nlohmann_json::nlohmann_json)
target_compile_features(nekomote_import PRIVATE cxx_std_20)

install(TARGETS nekomote_import RUNTIME DESTINATION bin COMPONENT runtime)
