#version 450

// vertex input
layout(location = 0) in uint inPackedPositionUV;
layout(location = 1) in uint inPackedTextureSlotFaceIndex;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragUV;
layout(location = 2) flat out uint fragTextureSlot;
layout(location = 3) flat out vec3 fragNormal;

layout(location = 4) out vec3 debugFragLocalPos; // For debugging

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
    // Unpack position and UV
    vec3 inPosition;
    inPosition.x = (inPackedPositionUV >> 0u) & 0x3FFu;
    inPosition.y = (inPackedPositionUV >> 10u) & 0x3FFu;
    inPosition.z = (inPackedPositionUV >> 20u) & 0x3FFu;

    // UV one bit each (for 4 possible values)
    fragUV.x = float((inPackedPositionUV >> 30u) & 0x1u);
    fragUV.y = float((inPackedPositionUV >> 31u) & 0x1u);

    uint textureSlot = (inPackedTextureSlotFaceIndex >> 0u) & 0x1FFFu;
    uint faceIndex = (inPackedTextureSlotFaceIndex >> 13u) & 0x7u;

    vec3 localPos = inPosition.xyz * (32.0 / 1023.0);
    debugFragLocalPos = localPos;

    mat4 model = oub.objects[gl_InstanceIndex].model;
    vec4 worldPos = model * vec4(localPos, 1.0);
    fragWorldPos = worldPos.xyz;

    vec3 normals[6] = vec3[](
        vec3(-1, 0, 0), vec3(1, 0, 0),   // -X, +X
        vec3(0, -1, 0), vec3(0, 1, 0),   // -Y, +Y
        vec3(0, 0, -1), vec3(0, 0, 1)    // -Z, +Z
    );
    fragNormal = normals[faceIndex];

    fragTextureSlot = textureSlot;

    gl_Position = global_ubo.projection * global_ubo.view * worldPos;
}