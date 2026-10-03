cmake_minimum_required(VERSION 3.25)

add_library(nekomote_asset_io STATIC)
add_library(nekomote::asset_io ALIAS nekomote_asset_io)

target_sources(nekomote_asset_io
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/import_emote.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/asset_file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/validation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/image.cpp)

target_include_directories(nekomote_asset_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(nekomote_asset_io PUBLIC cxx_std_20)
target_link_libraries(nekomote_asset_io
    PUBLIC
        nekomote::model
        nekomote_warnings
    PRIVATE
        nlohmann_json::nlohmann_json)

add_library(nekomote_stb STATIC stb.cpp)
target_include_directories(nekomote_stb SYSTEM PUBLIC ${stb_SOURCE_DIR})
target_link_libraries(nekomote_asset_io PRIVATE nekomote_stb)
