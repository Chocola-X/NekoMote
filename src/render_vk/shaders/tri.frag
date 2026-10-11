#version 450

layout(push_constant) uniform PC {
    vec4 params0;
    vec4 params1;
} pc;

layout(location = 0) out vec4 frag;

void main() {
    frag = pc.params1;
}
