add_executable(nekomote_audio_convert main.cpp)
target_link_libraries(nekomote_audio_convert PRIVATE nekomote::importer)
install(TARGETS nekomote_audio_convert RUNTIME DESTINATION bin COMPONENT runtime)
