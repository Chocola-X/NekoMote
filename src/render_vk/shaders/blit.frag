#version 450

layout(location = 0) in vec2 texture_uv;
layout(set = 0, binding = 0) uniform sampler2D scene;

layout(push_constant) uniform PC {
    float global_alpha;
} pc;

layout(location = 0) out vec4 output_color;

void main() {
    vec4 c = texture(scene, texture_uv);
    c.a *= pc.global_alpha;
    if (c.a <= 0.003) discard;
    output_color = vec4(c.rgb * c.a, c.a);
}
