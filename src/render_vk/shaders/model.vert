#version 450

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;

layout(push_constant) uniform PC {
    vec2 output_size;
    float origin_x;
    float origin_y;
    float scale;
    int flip_x;
    vec2 projection;
    int premultiply;
    int protection_pass;
    int mask_write;
    int use_mask;
    vec2 mask_output_size;
    float global_alpha;
} pc;

layout(location = 0) out vec2 texture_uv;
layout(location = 1) out vec4 vertex_color;

void main() {
    // Keep the GL/WebGL operation order: rounding affects edge coverage.
    vec2 centered = position * pc.scale + (vec2(pc.origin_x, pc.origin_y) - pc.output_size * 0.5);
    if (pc.flip_x != 0) centered.x = -centered.x;
    // Positive-height Vulkan 1.0 viewport; internal row order follows GL.
    // Host GPU presentation/capture converts these rows to top-left output.
    gl_Position = vec4(centered * pc.projection, 0.0, 1.0);
    texture_uv = uv;
    vertex_color = color;
}
