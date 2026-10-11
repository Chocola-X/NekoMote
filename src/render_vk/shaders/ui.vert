#version 450

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

layout(push_constant) uniform PC {
    vec4 params0;
} pc;

layout(location = 0) out vec2 v_uv;

void main() {
    v_uv = a_uv;
    vec2 viewport = pc.params0.xy;
    vec2 ndc = (a_pos / viewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
