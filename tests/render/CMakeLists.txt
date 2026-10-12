add_executable(nekomote_render_tests test_renderer.cpp)
target_link_libraries(nekomote_render_tests PRIVATE nekomote::render_gl nekomote::asset_io glfw nekomote_gl_loader Catch2::Catch2WithMain)
add_test(NAME renderer_gpu COMMAND nekomote_render_tests)
set_tests_properties(renderer_gpu PROPERTIES LABELS gpu WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

if(NEKOMOTE_BUILD_ABI)
    target_link_libraries(nekomote_render_tests PRIVATE nekomote::abi)
    target_compile_definitions(nekomote_render_tests PRIVATE NEKOMOTE_HAS_ABI=1)
endif()

if(NEKOMOTE_BUILD_VULKAN)
    add_executable(nekomote_vk_tests test_vk.cpp)
    target_include_directories(nekomote_vk_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/player ${PROJECT_SOURCE_DIR}/src/render_vk)
    target_link_libraries(nekomote_vk_tests PRIVATE
        nekomote::render_vk nekomote::render_gl nekomote::ui glfw
        nekomote_gl_loader Vulkan::Vulkan Catch2::Catch2WithMain nekomote_warnings)
    if(NEKOMOTE_BUILD_ABI)
        target_link_libraries(nekomote_vk_tests PRIVATE nekomote::abi)
        target_compile_definitions(nekomote_vk_tests PRIVATE NEKOMOTE_HAS_ABI=1)
    endif()
    add_test(NAME renderer_vulkan_gpu COMMAND nekomote_vk_tests)
    set_tests_properties(renderer_vulkan_gpu PROPERTIES
        LABELS "gpu;vulkan" WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
