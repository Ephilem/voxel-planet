#version 450

layout (location = 0) out vec3 fragWorldPos;
layout (location = 1) out vec2 fragUV;
layout (location = 2) flat out uint fragTextureSlot;
layout (location = 3) flat out vec3 fragNormal;
layout (location = 4) out vec3 debugFragLocalPos;

layout (set = 0, binding = 0) uniform global_uniform_object {
    mat4 view;
    mat4 projection;
    float time;
} global_ubo;

struct TerrainOUB {
    mat4 model;
};

layout (set = 1, binding = 0, std430) readonly buffer object_uniform_buffer {
    TerrainOUB objects[];
} oub;

struct TerrainFace3d {
    uint packed1;  // x:6, y:6, z:6, faceIndex:3, padding:11
    uint packed2;  // textureSlot:16, padding:16
};

layout (set = 2, binding = 0, std430) readonly buffer face_buffer {
    TerrainFace3d faces[];
} faceBuffer;

// Lookup tables for quad corners and UVs
const vec3 QUAD_CORNERS[6][4] = {
    // Face 0: -X
    {vec3(0, 1, 0), vec3(0, 1, 1), vec3(0, 0, 1), vec3(0, 0, 0)},
    // Face 1: +X
    {vec3(1, 0, 1), vec3(1, 1, 1), vec3(1, 1, 0), vec3(1, 0, 0)},
    // Face 2: -Y
    {vec3(0, 0, 1), vec3(1, 0, 1), vec3(1, 0, 0), vec3(0, 0, 0)},
    // Face 3: +Y
    {vec3(1, 1, 0), vec3(1, 1, 1), vec3(0, 1, 1), vec3(0, 1, 0)},
    // Face 4: -Z
    {vec3(1, 0, 0), vec3(1, 1, 0), vec3(0, 1, 0), vec3(0, 0, 0)},
    // Face 5: +Z
    {vec3(0, 1, 1), vec3(1, 1, 1), vec3(1, 0, 1), vec3(0, 0, 1)}
};

const vec2 QUAD_UVS[4] = {
    vec2(0, 0), // bottom-left
    vec2(1, 0), // bottom-right
    vec2(1, 1), // top-right
    vec2(0, 1)   // top-left
};

const vec3 FACE_NORMALS[6] = {
    vec3(- 1, 0, 0), vec3(1, 0, 0), // -X, +X
    vec3(0, - 1, 0), vec3(0, 1, 0), // -Y, +Y
    vec3(0, 0, - 1), vec3(0, 0, 1)    // -Z, +Z
};

const uint QUAD_INDICES[6] = uint[6](0, 1, 2, 0, 2, 3);

void main() {
    // Vertex pulling logic - 6 vertices per face, no index buffer needed
    uint faceIndex = gl_VertexIndex / 6u;
    uint cornerIndex = QUAD_INDICES[gl_VertexIndex % 6u];

    TerrainFace3d face = faceBuffer.faces[faceIndex];

    // Unpack data from packed integers
    uint voxelX = (face.packed1 >> 0u) & 0x3Fu; // 6 bits
    uint voxelY = (face.packed1 >> 6u) & 0x3Fu; // 6 bits
    uint voxelZ = (face.packed1 >> 12u) & 0x3Fu; // 6 bits
    uint faceDir = (face.packed1 >> 18u) & 0x7u; // 3 bits (0-5)
    uint textureSlot = (face.packed2 >> 0u) & 0xFFFFu; // 16 bits

    // Get local position of the vertex
    vec3 voxelPos = vec3(float(voxelX), float(voxelY), float(voxelZ));
    vec3 cornerOffset = QUAD_CORNERS[faceDir][cornerIndex];
    vec3 localPos = voxelPos + cornerOffset;

    debugFragLocalPos = localPos;

    fragUV = QUAD_UVS[cornerIndex];
    fragNormal = FACE_NORMALS[faceDir];
    fragTextureSlot = textureSlot;

    // Transform to world position
    mat4 model = oub.objects[gl_InstanceIndex].model;
    vec4 worldPos = model * vec4(localPos, 1.0);
    fragWorldPos = worldPos.xyz;

    gl_Position = global_ubo.projection * global_ubo.view * worldPos;
}