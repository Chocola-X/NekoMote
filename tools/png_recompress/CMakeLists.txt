add_executable(nekomote_png_recompress main.cpp)
target_link_libraries(nekomote_png_recompress PRIVATE nekomote::asset_io)
install(TARGETS nekomote_png_recompress RUNTIME DESTINATION bin COMPONENT runtime)
