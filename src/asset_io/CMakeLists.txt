cmake_minimum_required(VERSION 3.25)

find_package(PNG 1.6 REQUIRED)
find_package(ZLIB 1.2.11 REQUIRED)

add_library(nekomote_asset_io STATIC)
add_library(nekomote::asset_io ALIAS nekomote_asset_io)

target_sources(nekomote_asset_io
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/asset_file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/model_container.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/validation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/image.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/read_only_file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/session_asset.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/audio.cpp)

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
        nlohmann_json::nlohmann_json PNG::PNG ZLIB::ZLIB ${NEKOMOTE_FLAC_TARGET})

add_library(nekomote_stb STATIC stb.cpp)
target_link_libraries(nekomote_stb PUBLIC nekomote_stb_headers)
target_link_libraries(nekomote_asset_io PRIVATE nekomote_stb)

# FreeMote import and WAVE/FLAC conversion are offline facilities. Playback
# links asset_io only; source-format parsing and encode calls stay offline.
add_library(nekomote_importer STATIC import_emote.cpp audio_import.cpp asset_bundle.cpp)
add_library(nekomote::importer ALIAS nekomote_importer)
target_link_libraries(nekomote_importer PUBLIC nekomote::asset_io
    PRIVATE nlohmann_json::nlohmann_json ZLIB::ZLIB ${NEKOMOTE_FLAC_TARGET})
