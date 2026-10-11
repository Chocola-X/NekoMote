# render_vk: Vulkan 1.0 render backend consuming the same backend-agnostic
# RenderFrame + ui::Painter protocols as render_gl. SPIR-V shaders are compiled
# at build time from GLSL via glslangValidator; the runtime only needs libvulkan.

set(SHADER_PAIRS
    "model.vert" "model_vert"
    "model.frag" "model_frag"
    "blit.vert"  "blit_vert"
    "blit.frag"  "blit_frag"
    "ui.vert"    "ui_vert"
    "rect.frag"  "rect_frag"
    "text.frag"  "text_frag"
    "icon.frag"  "icon_frag"
    "tri.frag"   "tri_frag"
)

set(SPIRV_FILES)
set(SPIRV_NAMES)
math(EXPR pair_count "9")
foreach(i RANGE 1 ${pair_count})
    math(EXPR src_idx "(${i} - 1) * 2")
    math(EXPR name_idx "${i} * 2 - 1")
    list(GET SHADER_PAIRS ${src_idx} shader_file)
    list(GET SHADER_PAIRS ${name_idx} spv_name)
    set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_file}")
    set(spv_file "${CMAKE_CURRENT_BINARY_DIR}/${spv_name}.spv")
    add_custom_command(
        OUTPUT ${spv_file}
        COMMAND ${NEKOMOTE_GLSLANG_VALIDATOR}
            -V --target-env vulkan1.0 -o ${spv_file} ${shader_source}
        DEPENDS ${shader_source}
        COMMENT "Compiling SPIR-V: ${spv_name}"
    )
    list(APPEND SPIRV_FILES ${spv_file})
    list(APPEND SPIRV_NAMES ${spv_name})
endforeach()

set(SPIRV_HEADER ${CMAKE_CURRENT_BINARY_DIR}/spirv_shaders.hpp)
add_custom_command(
    OUTPUT ${SPIRV_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DSHADER_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -DSHADER_NAMES="${SPIRV_NAMES}"
        -DOUTPUT=${SPIRV_HEADER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/generate_spirv.cmake
    DEPENDS ${SPIRV_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/generate_spirv.cmake
    COMMENT "Generating SPIR-V shader header"
)

add_library(nekomote_render_vk STATIC renderer.cpp ui_renderer.cpp)
add_library(nekomote::render_vk ALIAS nekomote_render_vk)
target_include_directories(nekomote_render_vk
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nekomote_render_vk
    PUBLIC nekomote::model Vulkan::Vulkan
    PRIVATE nekomote::ui nekomote_warnings)
target_sources(nekomote_render_vk PRIVATE ${SPIRV_HEADER})
