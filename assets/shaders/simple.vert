#version 450
#extension GL_ARB_shader_draw_parameters : require

// vertex input
layout(location = 0) in vec4 inPosition;
layout(location = 1) in uint inTextureSlot;
layout(location = 2) in uint inFaceIndex;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragUV;
layout(location = 2) flat out uint fragTextureSlot;
layout(location = 3) flat out vec3 fragNormal;

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
    vec3 localPos = inPosition.xyz * 16.0;
    mat4 model = oub.objects[gl_InstanceIndex].model;

    vec4 worldPos = model * vec4(localPos, 1.0);
    fragWorldPos = worldPos.xyz;

    // Face index: 0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z
    if (inFaceIndex == 0u || inFaceIndex == 1u) {
        // Left/Right faces: use YZ
        fragUV = fract(localPos.yz);
    } else if (inFaceIndex == 2u || inFaceIndex == 3u) {
        // Bottom/Top faces: use XZ
        fragUV = fract(localPos.xz);
    } else {
        // Front/Back faces: use XY
        fragUV = fract(localPos.xy);
    }

    vec3 normals[6] = vec3[](
        vec3(-1, 0, 0), vec3(1, 0, 0),   // -X, +X
        vec3(0, -1, 0), vec3(0, 1, 0),   // -Y, +Y
        vec3(0, 0, -1), vec3(0, 0, 1)    // -Z, +Z
    );
    fragNormal = normals[inFaceIndex];

    fragTextureSlot = inTextureSlot;

    gl_Position = global_ubo.projection * global_ubo.view * worldPos;
}