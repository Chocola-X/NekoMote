#version 450

layout(location = 0) in vec2 texture_uv;
layout(location = 1) in vec4 vertex_color;

layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(set = 0, binding = 1) uniform sampler2D alpha_mask;

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

layout(location = 0) out vec4 output_color;

void main() {
    vec4 sampled = texture(atlas, texture_uv);
    if (pc.mask_write != 0) {
        output_color = vec4(0.0, sampled.a, 0.0, 0.0);
        return;
    }
    vec4 col = sampled * vertex_color;
    col.a *= pc.global_alpha;
    if (pc.use_mask != 0)
        col.a *= texture(alpha_mask, gl_FragCoord.xy / pc.mask_output_size).g;
    if (pc.protection_pass != 0 && col.a < 0.0000001) discard;
    output_color = vec4(pc.premultiply != 0 ? col.rgb * col.a : col.rgb, col.a);
}
