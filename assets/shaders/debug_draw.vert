#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 view_proj;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = pc.view_proj * vec4(inPosition, 1.0);
    outColor = inColor;
}