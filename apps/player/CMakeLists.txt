add_executable(nekomote_player main.cpp asset_catalog.cpp ui.cpp)
target_link_libraries(nekomote_player PRIVATE nekomote::asset_io nekomote::application nekomote::audio nekomote::render_gl nekomote_gl_loader glfw OpenGL::GL nlohmann_json::nlohmann_json)
target_include_directories(nekomote_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FONTCONFIG IMPORTED_TARGET fontconfig)
    if(FONTCONFIG_FOUND)
        target_link_libraries(nekomote_player PRIVATE PkgConfig::FONTCONFIG)
    endif()
endif()

target_include_directories(nekomote_player PRIVATE
    ${stb_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include)

install(TARGETS nekomote_player RUNTIME DESTINATION bin)
