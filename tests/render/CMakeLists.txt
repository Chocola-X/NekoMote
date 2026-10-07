add_executable(nekomote_render_tests test_renderer.cpp)
target_link_libraries(nekomote_render_tests PRIVATE nekomote::render_gl nekomote::asset_io glfw nekomote_gl_loader Catch2::Catch2WithMain)
add_test(NAME renderer_gpu COMMAND nekomote_render_tests)
set_tests_properties(renderer_gpu PROPERTIES LABELS gpu WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

if(NEKOMOTE_BUILD_ABI)
    target_link_libraries(nekomote_render_tests PRIVATE nekomote::abi)
    target_compile_definitions(nekomote_render_tests PRIVATE NEKOMOTE_HAS_ABI=1)
endif()
