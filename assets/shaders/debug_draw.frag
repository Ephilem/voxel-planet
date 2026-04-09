#version 450

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec4 inColor;
layout(location = 1) in float v_clip_w;

layout(push_constant) uniform PushConstants {
    mat4 view_proj;
    float far_plane;
} pc;


void main() {
    fragColor = inColor;
    // logarithmic depth buffer
    gl_FragDepth = log2(max(1e-6, 1.0 + v_clip_w)) / log2(1.0 + pc.far_plane);
}