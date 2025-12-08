#version 450

// vertex input
layout(location = 0) in vec3 inPosition;

// vertex output
layout(location = 0) out vec3 fragWorldPos;

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 view;
    mat4 projection;
    float time;
} global_ubo;

void main() {
    fragWorldPos = inPosition;
    gl_Position = global_ubo.projection * global_ubo.view * vec4(inPosition, 1.0);
}