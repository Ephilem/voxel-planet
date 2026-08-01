#version 450

#include "planet_utils.glsl"

layout (location = 0) out vec3 fragWorldPos;
layout (location = 1) out vec2 fragUV;
layout (location = 2) flat out uint fragTextureSlot;
layout (location = 3) flat out vec3 fragNormal;
layout (location = 4) flat out vec3 debugFragLocalPos;
layout (location = 5) out float v_clip_w;

layout (set = 0, binding = 0) uniform PlanetSurfaceUBO {
    mat4 view;
    mat4 projection;
    float planetRadius;
    float farPlane;
    float _pad0;
    float _pad1;

    // Anchor frame (planet/world space)
    vec4 anchorX;             // tangent right
    vec4 anchorY;             // up (radial)
    vec4 anchorZ;             // tangent forward
    vec4 anchorCameraPos;     // anchor position relative to camera (FO)

    // Anchor in cube-face space : (faceU, faceV, faceIndex, planetRadius)
    vec4 anchorFacePos;

    // Camera position in world space. During the flat terrain phase this replaces the whole
    // anchor frame above, which stays in the block but is no longer read.
    vec4 cameraWorldPos;
} ubo;

struct PlanetChunkOUB {
    ivec4 coord; // node coord : x = u, y = v, z = alt, w = LOD level
};
layout (set = 1, binding = 0, std430) readonly buffer ChunkCoordBuffer {
    PlanetChunkOUB chunks[];
} oub;

struct TerrainFace3d {
    uint packed1; // x:5 | z:5 | y:9 | faceIndex:3 | padding:10
    uint packed2; // width:9 | height:9 | textureSlot:14
};
layout (set = 2, binding = 0, std430) readonly buffer FaceBuffer {
    TerrainFace3d faces[];
} faceBuffer;

const vec3 QUAD_CORNERS[6][4] =  {
    {vec3(0, 0, 0), vec3(0, 0, 1), vec3(0, 1, 1), vec3(0, 1, 0)},
    {vec3(1, 0, 0), vec3(1, 1, 0), vec3(1, 1, 1), vec3(1, 0, 1)},
    {vec3(0, 0, 0), vec3(1, 0, 0), vec3(1, 0, 1), vec3(0, 0, 1)},
    {vec3(0, 1, 0), vec3(0, 1, 1), vec3(1, 1, 1), vec3(1, 1, 0)},
    {vec3(0, 0, 0), vec3(0, 1, 0), vec3(1, 1, 0), vec3(1, 0, 0)},
    {vec3(1, 0, 1), vec3(1, 1, 1), vec3(0, 1, 1), vec3(0, 0, 1)}
};
const vec2 FACE_QUAD_UVS[6][4] = {
    {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)},
    {vec2(0, 0), vec2(0, 1), vec2(1, 1), vec2(1, 0)},
    {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)},
    {vec2(0, 0), vec2(0, 1), vec2(1, 1), vec2(1, 0)},
    {vec2(0, 0), vec2(0, 1), vec2(1, 1), vec2(1, 0)},
    {vec2(1, 0), vec2(1, 1), vec2(0, 1), vec2(0, 0)},
};
const uint QUAD_INDICES[6] = uint[6](0, 1, 2, 0, 2, 3);
const ivec2 FACE_SCALE_AXES[6] = ivec2[6](
    ivec2(2, 1), ivec2(2, 1), ivec2(0, 2), ivec2(0, 2), ivec2(0,1), ivec2(0, 1)
);

const vec3 LOCAL_FACE_NORMALS[6] = {
    vec3(- 1, 0, 0), vec3(1, 0, 0),
    vec3(0, - 1, 0), vec3(0, 1, 0),
    vec3(0, 0, - 1), vec3(0, 0, 1)
};

void main() {
    uint faceIndex = gl_VertexIndex / 6u;
    uint cornerIndex = QUAD_INDICES[gl_VertexIndex % 6u];

    /////////////////////////////////////////////////
    /// Decode face data packed on CPU

    TerrainFace3d face = faceBuffer.faces[faceIndex];

    uint voxelX = (face.packed1 >> 0u) & 0x1Fu;
    uint voxelZ = (face.packed1 >> 5u)  & 0x1Fu;
    uint voxelY = (face.packed1 >> 10u) & 0x1FFu;
    uint faceDir = (face.packed1 >> 19u) & 0x7u;
    uint packedWidth = (face.packed2 >> 0u) & 0x1FFu;
    uint packedHeight = (face.packed2 >> 9u) & 0x1FFu;
    uint textureSlot = (face.packed2 >> 18u) & 0x3FFFu;

    float faceWidth  = float(packedWidth + 1u) / 16.0;
    float faceHeight = float(packedHeight + 1u) / 16.0;

    // Calculate the local position of the vertex
    vec3 localVoxelPos = vec3(float(voxelX), float(voxelY) / 16.0, float(voxelZ));
    vec3 cornerOffset = QUAD_CORNERS[faceDir][cornerIndex];

    ivec2 scaleAxes = FACE_SCALE_AXES[faceDir];
    cornerOffset[scaleAxes.x] *= faceWidth;
    cornerOffset[scaleAxes.y] *= faceHeight;
    if (faceDir == 3u) cornerOffset.y /= 16.0;

    vec3 localPos = localVoxelPos + cornerOffset;
    debugFragLocalPos = localPos;

    // Calculate UVs and texture slot for the fragment shader
    fragUV = vec2(FACE_QUAD_UVS[faceDir][cornerIndex].x * faceWidth,
                  FACE_QUAD_UVS[faceDir][cornerIndex].y * faceHeight);
    fragTextureSlot = textureSlot;

    /////////////////////////////////////////////////
    /// Calculate position of the vertex

    PlanetChunkOUB chunk = oub.chunks[gl_InstanceIndex];

    // Flat terrain phase: a cube face is a plain grid, with no cube to sphere mapping at all.
    // This has to stay the exact mirror of lod_node_corner() in planet_lod_common.glsl, or the
    // traversal would cull and pick LOD levels against positions that are never drawn here.
    // planet__local_to_world() in planet_utils.glsl is the equiangular version to come back to.
    //
    // A node at level L covers CHUNK_SIZE << L on each axis, so both its grid position and its
    // voxels scale by the same factor.
    float nodeScale = float(1 << chunk.coord.w);
    vec3 nodeOrigin = vec3(float(chunk.coord.x), float(chunk.coord.z), float(chunk.coord.y)) * CHUNK_SIZE_F;

    vec3 worldPos = (nodeOrigin + localPos) * nodeScale - ubo.cameraWorldPos.xyz;

    fragNormal = LOCAL_FACE_NORMALS[faceDir];

    fragWorldPos = worldPos;
    gl_Position  = ubo.projection * ubo.view * vec4(worldPos, 1.0);
    v_clip_w     = gl_Position.w;
}