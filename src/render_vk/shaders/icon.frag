#version 450

layout(location = 0) in vec2 v_uv;

layout(set = 0, binding = 0) uniform sampler2D u_texture;

layout(push_constant) uniform PC {
    vec4 params0;
    vec4 params1;
} pc;

layout(location = 0) out vec4 frag;

void main() {
    vec4 tint = pc.params1;
    vec4 tex = texture(u_texture, v_uv);
    frag = vec4(tex.rgb * tint.rgb, tex.a * tint.a);
}
