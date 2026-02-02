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
    uint packed1;  // x:6, y:6, z:6, faceIndex:3, width:5, height:5, padding:1
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

const ivec2 FACE_SCALE_AXES[6] = ivec2[6](
    ivec2(2, 1), // Face 0 (-X)
    ivec2(1, 2), // Face 1 (+X)
    ivec2(0, 2), // Face 2 (-Y)
    ivec2(2, 0), // Face 3 (+Y)
    ivec2(1, 0), // Face 4 (-Z)
    ivec2(0, 1)  // Face 5 (+Z)
);

void main() {
    uint faceIndex = gl_VertexIndex / 6u;
    uint cornerIndex = QUAD_INDICES[gl_VertexIndex % 6u];

    TerrainFace3d face = faceBuffer.faces[faceIndex];

    // Unpack data from packed integers
    uint voxelX = (face.packed1 >> 0u) & 0x3Fu;
    uint voxelY = (face.packed1 >> 6u) & 0x3Fu;
    uint voxelZ = (face.packed1 >> 12u) & 0x3Fu;
    uint faceDir = (face.packed1 >> 18u) & 0x7u;
    uint packedWidth = (face.packed1 >> 21u) & 0x1Fu;
    uint packedHeight = (face.packed1 >> 26u) & 0x1Fu;
    uint textureSlot = (face.packed2 >> 0u) & 0xFFFFu;

    // Actual dimensions (stored as value-1)
    float faceWidth = float(packedWidth + 1u);
    float faceHeight = float(packedHeight + 1u);

    vec3 voxelPos = vec3(float(voxelX), float(voxelY), float(voxelZ));
    vec3 cornerOffset = QUAD_CORNERS[faceDir][cornerIndex];

    // Scale corner offset by face dimensions
    ivec2 scaleAxes = FACE_SCALE_AXES[faceDir];
    cornerOffset[scaleAxes.x] *= faceWidth;
    cornerOffset[scaleAxes.y] *= faceHeight;

    vec3 localPos = voxelPos + cornerOffset;
    debugFragLocalPos = localPos;

    // Scale UVs for texture tiling
    vec2 uv = QUAD_UVS[cornerIndex];
    fragUV = vec2(uv.x * faceWidth, uv.y * faceHeight);
    fragNormal = FACE_NORMALS[faceDir];
    fragTextureSlot = textureSlot;

    mat4 model = oub.objects[gl_InstanceIndex].model;
    vec4 worldPos = model * vec4(localPos, 1.0);
    fragWorldPos = worldPos.xyz;

    gl_Position = global_ubo.projection * global_ubo.view * worldPos;
}