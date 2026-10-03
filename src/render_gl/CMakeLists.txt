add_library(nekomote_render_gl STATIC renderer.cpp)
add_library(nekomote::render_gl ALIAS nekomote_render_gl)
target_include_directories(nekomote_render_gl PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(nekomote_render_gl PUBLIC nekomote::model PRIVATE nekomote_gl_loader nekomote_warnings)
