add_executable(nekomote_player main.cpp asset_catalog.cpp ui.cpp)
target_link_libraries(nekomote_player PRIVATE nekomote::asset_io nekomote::application nekomote::audio nekomote::render_gl nekomote_gl_loader glfw OpenGL::GL nlohmann_json::nlohmann_json)
target_include_directories(nekomote_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FONTCONFIG IMPORTED_TARGET fontconfig)
    if(FONTCONFIG_FOUND)
        target_link_libraries(nekomote_player PRIVATE PkgConfig::FONTCONFIG)
        target_compile_definitions(nekomote_player PRIVATE NEKOMOTE_HAVE_FONTCONFIG)
    endif()
endif()

target_include_directories(nekomote_player PRIVATE
    ${stb_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include)

install(TARGETS nekomote_player RUNTIME DESTINATION bin)

if(NEKOMOTE_BUILD_TESTS)
    add_executable(nekomote_ui_tests test_ui.cpp ui.cpp)
    target_link_libraries(nekomote_ui_tests PRIVATE
        nekomote_gl_loader
        OpenGL::GL
        Catch2::Catch2WithMain
        nekomote_warnings)
    if(FONTCONFIG_FOUND)
        target_link_libraries(nekomote_ui_tests PRIVATE PkgConfig::FONTCONFIG)
        target_compile_definitions(nekomote_ui_tests PRIVATE NEKOMOTE_HAVE_FONTCONFIG)
    endif()
    target_include_directories(nekomote_ui_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${stb_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(nekomote_ui_tests PRIVATE cxx_std_20)
endif()
