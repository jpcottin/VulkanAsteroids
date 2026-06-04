#version 450

layout(push_constant) uniform PC {
    vec4 mtx;
    vec2 trans;
    vec2 _pad;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}
