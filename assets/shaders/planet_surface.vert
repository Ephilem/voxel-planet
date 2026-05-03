#version 450

#include "planet_utils.glsl"

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragUV;
layout(location = 2) flat out uint fragTextureSlot;
layout(location = 3) flat out vec3 fragNormal;
layout(location = 4) flat out vec3 debugFragLocalPos;
layout(location = 5) out float v_clip_w;

// Set 0: per-frame data
layout(set = 0, binding = 0) uniform PlanetSurfaceUBO {
    mat4  view;
    mat4  projection;
    vec3  foPositionInPlanet; // floating origin position in planet-relative coords (float precision)
    float planetRadius;
    float farPlane;

    float _padding[3];
} ubo;

// Set 1: per-chunk coord (immutable after upload)
struct PlanetChunkOUB {
    ivec4 coord; // x=face, y=chunkX, z=chunkY, w=altitude
};
layout(set = 1, binding = 0, std430) readonly buffer ChunkCoordBuffer {
    PlanetChunkOUB chunks[];
} oub;

// Set 2: face geometry data (same format as flat terrain)
struct TerrainFace3d {
    uint packed1; // x:5 | y:9 | z:5 | faceIndex:3 | padding:10
    uint packed2; // width:9 | height:9 | textureSlot:14
};
layout(set = 2, binding = 0, std430) readonly buffer FaceBuffer {
    TerrainFace3d faces[];
} faceBuffer;

// Same quad tables as simple.vert
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
const vec2 QUAD_UVS[4] = {vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,1)};
const uint QUAD_INDICES[6] = uint[6](0, 1, 2, 0, 2, 3);
const ivec2 FACE_SCALE_AXES[6] = ivec2[6](
    ivec2(2,1), ivec2(1,2), ivec2(0,2), ivec2(2,0), ivec2(1,0), ivec2(0,1)
);

void main() {
    uint faceIndex   = gl_VertexIndex / 6u;
    uint cornerIndex = QUAD_INDICES[gl_VertexIndex % 6u];

    TerrainFace3d face = faceBuffer.faces[faceIndex];

    // packed1: x:5 | y:5 | z:9 | faceIndex:3 | padding:10
    uint voxelX  = (face.packed1 >> 0u)  & 0x1Fu;
    uint voxelY  = (face.packed1 >> 5u)  & 0x1Fu;
    uint voxelZ  = (face.packed1 >> 10u) & 0x1FFu;
    uint faceDir = (face.packed1 >> 19u) & 0x7u;
    uint packedWidth  = (face.packed2 >> 0u)  & 0x1FFu;
    uint packedHeight = (face.packed2 >> 9u)  & 0x1FFu;
    uint textureSlot  = (face.packed2 >> 18u) & 0x3FFFu;

    float faceWidth  = float(packedWidth  + 1u) / 16.0;
    float faceHeight = float(packedHeight + 1u) / 16.0;

    // localPos: X=right, Y=forward, Z=altitude (up). Z is in sub-voxel units.
    vec3 voxelPos    = vec3(float(voxelX), float(voxelY), float(voxelZ) / 16.0);
    vec3 cornerOffset = QUAD_CORNERS[faceDir][cornerIndex];

    ivec2 scaleAxes = FACE_SCALE_AXES[faceDir];
    cornerOffset[scaleAxes.x] *= faceWidth;
    cornerOffset[scaleAxes.y] *= faceHeight;
    // +Z face (faceDir==5): the z corner offset is in sub-voxel space already via voxelZ encoding
    if (faceDir == 5u) cornerOffset.z /= 16.0;

    vec3 localPos = voxelPos + cornerOffset;
    debugFragLocalPos = localPos;

    fragUV          = vec2(QUAD_UVS[cornerIndex].x * faceWidth, QUAD_UVS[cornerIndex].y * faceHeight);
    fragTextureSlot = textureSlot;

    // Chunk coord from OUB
    PlanetChunkOUB chunk = oub.chunks[gl_InstanceIndex];
    int cubeFace = chunk.coord.x;
    int cx       = chunk.coord.y;
    int cy       = chunk.coord.z;
    int alt      = chunk.coord.w;

    // Compute world position (planet-relative) then subtract FO
    vec3 worldPosPlanet = planet__local_to_world(cubeFace, cx, cy, alt, ubo.planetRadius, localPos);
    vec3 cameraRelPos   = worldPosPlanet - ubo.foPositionInPlanet;

    // Normal in planet-space for lighting (sphere surface normal of the voxel face)
    vec3 chunkOrigin = planet__chunk_origin(cubeFace, cx, cy, alt, ubo.planetRadius);
    vec3 up          = normalize(chunkOrigin);
    vec3 right, forward;
    planet__chunk_rotation(up, right, forward);
    mat3 rot = mat3(right, forward, up);

    // Transform the local voxel face normal into world space
    const vec3 LOCAL_FACE_NORMALS[6] = {
        vec3(-1,0,0), vec3(1,0,0),
        vec3(0,-1,0), vec3(0,1,0),
        vec3(0,0,-1), vec3(0,0,1)
    };
    fragNormal   = rot * LOCAL_FACE_NORMALS[faceDir];
    fragWorldPos = cameraRelPos;

    gl_Position = ubo.projection * ubo.view * vec4(cameraRelPos, 1.0);

    v_clip_w = gl_Position.w;
}