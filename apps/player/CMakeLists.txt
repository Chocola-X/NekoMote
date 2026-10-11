add_executable(nekomote_player main.cpp)
target_link_libraries(nekomote_player PRIVATE
    nekomote::asset_io nekomote::application nekomote::audio
    nekomote::render_gl nekomote::ui
    nekomote_gl_loader glfw OpenGL::GL nlohmann_json::nlohmann_json)
target_include_directories(nekomote_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NEKOMOTE_BUILD_VULKAN)
    target_compile_definitions(nekomote_player PRIVATE NEKOMOTE_HAS_VULKAN)
    target_include_directories(nekomote_player PRIVATE ${PROJECT_SOURCE_DIR}/src/render_vk)
    target_link_libraries(nekomote_player PRIVATE nekomote::render_vk Vulkan::Vulkan)
endif()

install(TARGETS nekomote_player RUNTIME DESTINATION bin)
