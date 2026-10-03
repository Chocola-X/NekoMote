cmake_minimum_required(VERSION 3.25)

add_executable(nekomote_import main.cpp)
target_link_libraries(nekomote_import
    PRIVATE
        nekomote::asset_io
        nekomote_warnings)
target_compile_features(nekomote_import PRIVATE cxx_std_20)
