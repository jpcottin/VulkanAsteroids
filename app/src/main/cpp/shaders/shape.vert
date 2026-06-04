#version 450

// 2x2 linear transform + NDC translation + RGBA colour, all per-draw push constants.
layout(push_constant) uniform PC {
    vec4 mtx;    // m00, m01, m10, m11
    vec2 trans;  // NDC translation
    vec2 _pad;
    vec4 color;
} pc;

layout(location = 0) in vec2 inPos;

void main() {
    vec2 p = vec2(pc.mtx.x * inPos.x + pc.mtx.y * inPos.y,
                  pc.mtx.z * inPos.x + pc.mtx.w * inPos.y);
    gl_Position = vec4(p + pc.trans, 0.0, 1.0);
}
