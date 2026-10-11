#version 450

layout(location = 0) out vec2 texture_uv;

void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    // Intermediate passes share logical row order; the host flips final output.
    texture_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
