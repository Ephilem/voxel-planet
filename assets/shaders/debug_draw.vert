#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 view_proj;
    float far_plane;
} pc;

layout(location = 0) out vec4 outColor;
layout(location = 1) out float v_clip_w;

void main() {
    gl_Position = pc.view_proj * vec4(inPosition, 1.0);
    gl_PointSize = 8.0;
    outColor = inColor;
    v_clip_w = gl_Position.w; // eye space depth for point size attenuation
}