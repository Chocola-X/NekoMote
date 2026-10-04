add_executable(nekomote_audio_probe main.cpp)
target_link_libraries(nekomote_audio_probe PRIVATE nekomote::audio nekomote::asset_io)
