#version 450

#include "planet_transform.glsl"

struct TileInstance {
    vec3  originSpacePos;
    float extent;
    vec2  nodeFaceOrigin;
    uint  packed;        // face (3 bits) | level (5 bits)
    float morph;
};

layout(std430, set = 0, binding = 0) readonly buffer TileInstances {
    TileInstance instances[];
};

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  camPosPlanet;
    float radius;
} pc;

layout(location = 0) out vec2 outGridUV;
layout(location = 1) out float outLevel;

const uint GRID_RES = 33u;

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

    float height = 0.0;
    vec3 posRelCam = inst.originSpacePos + (dir * (pc.radius + height) - dirRef * pc.radius);

    gl_Position = pc.viewProj * vec4(posRelCam, 1.0);

    outGridUV = st;
    outLevel  = float(level);
}
