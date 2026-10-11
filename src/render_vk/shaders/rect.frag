#version 450

layout(location = 0) in vec2 v_uv;

layout(push_constant) uniform PC {
    vec4 params0;
    vec4 params1;
    vec4 params2;
    vec4 params3;
} pc;

layout(location = 0) out vec4 frag;

void main() {
    vec2 viewport = pc.params0.xy;
    vec2 rect_size = pc.params0.zw;
    vec4 fill = pc.params1;
    vec4 border = pc.params2;
    float border_width = pc.params3.x;
    float radius = pc.params3.y;
    float pixel_scale = pc.params3.z;

    vec2 half_size = rect_size * 0.5;
    vec2 p = v_uv * rect_size - half_size;
    vec2 inner = half_size - vec2(radius);
    vec2 q = abs(p) - inner;
    float dist = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
    float aa = 1.5 / pixel_scale;
    float outer_alpha = 1.0 - smoothstep(-aa, aa, dist);
    if (border.a <= 0.0 || border_width <= 0.0) {
        frag = vec4(fill.rgb, fill.a * outer_alpha);
        return;
    }
    float inner_alpha = 1.0 - smoothstep(-aa, aa, dist + border_width);
    float border_alpha = max(0.0, outer_alpha - inner_alpha);
    vec3 rgb = fill.rgb * inner_alpha + border.rgb * border_alpha;
    float alpha = fill.a * inner_alpha + border.a * border_alpha;
    frag = vec4(rgb, alpha);
}
