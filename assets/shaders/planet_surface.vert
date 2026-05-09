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
    ivec4 foPosMM;       // xyz = FO position in mm, absolute to planet center
    float planetRadius;
    float farPlane;
    float _pad0;
    float _pad1;
} ubo;

struct PlanetChunkOUB {
    ivec4 coord;            // x=face, y=chunkX, z=chunkY, w=altitude
    ivec4 cornerMM;         // xyz = chunk corner (0,0,0) sphere position in mm, absolute
    vec4  cornerOffsets[8]; // xyz = offset from cornerMM (in metres), indexed by dx | dy<<1 | dz<<2
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
    /// Calculate the position of the chunk on the planet and relative to the camera

    PlanetChunkOUB chunk = oub.chunks[gl_InstanceIndex];
    int cubeFace = chunk.coord.x;

    // Chunk base position relative to FO, exact integer subtraction in mm → small float, no precision loss.
    ivec3 chunkRelFO_mm = chunk.cornerMM.xyz - ubo.foPosMM.xyz;
    vec3 chunkBase = vec3(chunkRelFO_mm) * 0.001;

    // Trilinear interpolation between the 8 precomputed sphere corners (offsets from cornerMM).
    // localPos is in [0, CHUNK_SIZE]^3 — y can exceed CHUNK_SIZE for sub-voxel tops but stays small.
    vec3 t = localPos / CHUNK_SIZE_F;
    vec3 c00 = mix(chunk.cornerOffsets[0].xyz, chunk.cornerOffsets[1].xyz, t.x);
    vec3 c10 = mix(chunk.cornerOffsets[2].xyz, chunk.cornerOffsets[3].xyz, t.x);
    vec3 c01 = mix(chunk.cornerOffsets[4].xyz, chunk.cornerOffsets[5].xyz, t.x);
    vec3 c11 = mix(chunk.cornerOffsets[6].xyz, chunk.cornerOffsets[7].xyz, t.x);
    vec3 c0  = mix(c00, c10, t.y);
    vec3 c1  = mix(c01, c11, t.y);
    vec3 chunkOffset = mix(c0, c1, t.z);

    vec3 cameraRelPos = chunkBase + chunkOffset;

    // Normal frame from the radial direction at this vertex. cornerMM gives the planet-centre
    // direction, valid as a chunk-wide approximation for blocky voxels.
    vec3 up = normalize(vec3(chunk.cornerMM.xyz));
    vec3 right, forward;
    planet__chunk_rotation(cubeFace, up, right, forward);
    fragNormal = mat3(right, up, forward) * LOCAL_FACE_NORMALS[faceDir];

    fragWorldPos = cameraRelPos;
    gl_Position = ubo.projection * ubo.view * vec4(cameraRelPos, 1.0);
    v_clip_w = gl_Position.w;
}