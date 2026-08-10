#version 450

#include "planet_transform.glsl"

struct TileInstance {
    vec3  originSpacePos;
    float extent;
    vec2  nodeFaceOrigin;
    vec2  uvOffset;
    uint  packed;      // face (3 bits) | level (5 bits)
    uint  atlasSlot;   // slice actually sampled, may be an ancestor's
    float uvScale;     // 1 when the tile owns its slice, halved per fallback level
    float morph;
};

layout(std430, set = 0, binding = 0) readonly buffer TileInstances {
    TileInstance instances[];
};

layout(set = 0, binding = 1) uniform texture2DArray tileAtlas;
layout(set = 0, binding = 2) uniform sampler tileSampler;

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  camPosPlanet;
    float radius;
} pc;

layout(location = 0) out vec2 outGridUV;
layout(location = 1) flat out uint outLevel;
layout(location = 2) out float height;

const uint GRID_RES = 33u;
const uint INVALID_ATLAS_SLOT = 0xFFFFu;

// Mirrors planet_tile_encode_height in planet_rendering_types.h
const float PLANET_HEIGHT_SCALE = 16384.0;

void main() {
    TileInstance inst = instances[gl_InstanceIndex];

    uint face  = inst.packed & 7u;
    uint level = (inst.packed >> 3) & 31u;

    uint ix = gl_VertexIndex % GRID_RES;
    uint iy = gl_VertexIndex / GRID_RES;
    vec2 st = vec2(ix, iy) / float(GRID_RES - 1u);

    vec2 uv     = inst.nodeFaceOrigin + st * inst.extent;
    vec3 dir    = face_uv_to_direction(face, uv);
    vec3 dirRef = face_uv_to_direction(face, inst.nodeFaceOrigin);

    if (inst.atlasSlot == INVALID_ATLAS_SLOT) {
        height = 0.0;
    } else {
        vec2 atlasUV = inst.uvOffset + st * inst.uvScale;

        float h01 = texture(sampler2DArray(tileAtlas, tileSampler),
        vec3(atlasUV, float(inst.atlasSlot))).r;

        height = (h01 - 0.5) * 2.0 * PLANET_HEIGHT_SCALE;
    }

    vec3 posRelCam = inst.originSpacePos + (dir * (pc.radius + height) - dirRef * pc.radius);

    gl_Position = pc.viewProj * vec4(posRelCam, 1.0);

    outGridUV = st;
    outLevel  = level;
}