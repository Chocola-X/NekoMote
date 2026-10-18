add_library(nekomote_ui STATIC easing.cpp font.cpp layout.cpp menu.cpp)
add_library(nekomote::ui ALIAS nekomote_ui)
target_include_directories(nekomote_ui PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(nekomote_ui PUBLIC nekomote::application)
target_link_libraries(nekomote_ui PRIVATE nekomote_warnings nekomote::asset_io)
target_compile_features(nekomote_ui PUBLIC cxx_std_20)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG_UI REQUIRED IMPORTED_TARGET fontconfig)
target_link_libraries(nekomote_ui PRIVATE PkgConfig::FONTCONFIG_UI)

target_link_libraries(nekomote_ui PRIVATE nekomote_stb_headers)
