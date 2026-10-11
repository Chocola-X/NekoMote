add_library(nekomote_abi STATIC abi.cpp wallpaper_instance.cpp)
add_library(nekomote::abi ALIAS nekomote_abi)
target_include_directories(nekomote_abi PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(nekomote_abi
    PUBLIC
        nekomote::application
        nekomote::asset_io
    PRIVATE
        nekomote::audio
        nekomote::render_gl
        nekomote::ui
        nekomote_warnings)
if(NEKOMOTE_BUILD_VULKAN)
    target_compile_definitions(nekomote_abi PRIVATE NEKOMOTE_HAS_VULKAN)
    target_link_libraries(nekomote_abi PRIVATE nekomote::render_vk)
endif()
target_compile_features(nekomote_abi PUBLIC cxx_std_20)
