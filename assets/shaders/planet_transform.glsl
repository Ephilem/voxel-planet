// Mirror of core/world/planet/planet_transform.h. Both sides must stay in sync:
// the CPU projects node bounds for culling with the same math the GPU uses to
// place vertices, and any drift between them shows up as popping or bad culling.

#ifndef PLANET_TRANSFORM_GLSL
#define PLANET_TRANSFORM_GLSL

#define FACE_POS_X 0u
#define FACE_NEG_X 1u
#define FACE_POS_Y 2u
#define FACE_NEG_Y 3u
#define FACE_POS_Z 4u
#define FACE_NEG_Z 5u

/// Tangent warp, s in [-1, 1]. A raw normalize would shrink a 1 m voxel down to
/// 0.44 m near a face corner, this keeps it at 0.84 m
float face_warp(float s) {
    return tan(s * 0.78539816339744830961); // s * pi/4
}

float face_warp_inv(float t) {
    return atan(t) * 1.27323954473516268615; // * 4/pi
}

/// Face coordinates in [-1, 1] to unit direction
vec3 face_uv_to_direction(uint face, vec2 uv) {
    float a = face_warp(uv.x);
    float b = face_warp(uv.y);

    vec3 dir;
    if (face == FACE_POS_X) dir = vec3(1.0, b, -a);
    else if (face == FACE_NEG_X) dir = vec3(-1.0, b, a);
    else if (face == FACE_POS_Y) dir = vec3(a, 1.0, -b);
    else if (face == FACE_NEG_Y) dir = vec3(a, -1.0, b);
    else if (face == FACE_POS_Z) dir = vec3(a, b, 1.0);
    else dir = vec3(-a, b, -1.0);

    return normalize(dir);
}

#endif
