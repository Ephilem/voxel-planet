#version 450
#extension GL_ARB_shader_draw_parameters : require

// vertex input
layout(location = 0) in vec4 inPosition;

// vertex output
layout(location = 0) out vec3 fragWorldPos;

layout(set = 0, binding = 0) uniform global_uniform_object {
    mat4 view;
    mat4 projection;
    float time;
} global_ubo;

struct TerrainOUB {
    mat4 model;
};

layout(set = 1, binding = 0, std430) readonly buffer object_uniform_buffer {
    TerrainOUB objects[];
} oub;

void main() {
    mat4 model = oub.objects[gl_InstanceIndex].model;

    vec4 worldPos = model * inPosition;
    fragWorldPos = worldPos.xyz;

    gl_Position = global_ubo.projection * global_ubo.view * worldPos;
}