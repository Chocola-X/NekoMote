#version 450

layout(location = 0) in vec2 v_uv;

layout(set = 0, binding = 0) uniform sampler2D u_atlas;

layout(push_constant) uniform PC {
    vec4 params0;
    vec4 params1;
} pc;

layout(location = 0) out vec4 frag;

void main() {
    vec4 color = pc.params1;
    float a = texture(u_atlas, v_uv).r;
    frag = vec4(color.rgb, color.a * a);
}
