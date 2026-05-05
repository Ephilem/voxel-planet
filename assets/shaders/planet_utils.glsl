#ifndef PLANET_UTILS_GLSL
#define PLANET_UTILS_GLSL

#define CHUNK_SIZE_F 32.0

// Mirrors CubeFace enum order: PosX=0, NegX=1, PosY=2, NegY=3, PosZ=4, NegZ=5
vec3 planet__face_to_cube_dir(int face, float u, float v) {
    switch (face) {
        case 0: return vec3(1.0, v, u);  // PosX
        case 1: return vec3(-1.0, v, -u);  // NegX
        case 2: return vec3(u, 1.0, -v);  // PosY
        case 3: return vec3(u, -1.0, v);  // NegY
        case 4: return vec3(u, v, 1.0);  // PosZ
        case 5: return vec3(-u, v, -1.0);  // NegZ
    }
    return vec3(0.0);
}

// Returns the world-space position of the (0,0,0) corner of a planet chunk,
// relative to the planet center. Equivalent to planet_chunk_to_world() on CPU
vec3 planet__chunk_origin(int face, int cx, int cy, int altitude, float radius) {
    float u = float(cx) * CHUNK_SIZE_F / radius;
    float v = float(cy) * CHUNK_SIZE_F / radius;
    vec3 dir = normalize(planet__face_to_cube_dir(face, u, v));
    float r = radius + float(altitude) * CHUNK_SIZE_F;
    return dir * r;
}

// Builds the local rotation frame for a chunk from its sphere-surface normal.
// up      = radial direction (outward from planet center)
// right   = tangent along cube-face u axis
// forward = tangent along cube-face v axis
// The chunk's local Y axis (altitude axis) aligns with 'up'.
void planet__chunk_rotation(int face, vec3 up, out vec3 right, out vec3 forward) {
    // tangents directs dérivés de face_to_cube_dir, projetés sur le plan tangent
    const vec3 U_TAN[6] = {
        vec3(0, 0, 1), vec3(0, 0,-1), // PosX, NegX
        vec3(1, 0, 0), vec3(1, 0, 0), // PosY, NegY
        vec3(1, 0, 0), vec3(-1,0, 0), // PosZ, NegZ
    };
    const vec3 V_TAN[6] = {
        vec3(0, 1, 0), vec3(0, 1, 0), // PosX, NegX
        vec3(0, 0,-1), vec3(0, 0, 1), // PosY, NegY
        vec3(0, 1, 0), vec3(0, 1, 0), // PosZ, NegZ
    };
    vec3 u = U_TAN[face];
    vec3 v = V_TAN[face];
    right   = normalize(u - dot(u, up) * up);
    forward = normalize(v - dot(v, up) * up);
}

// local voxel position -> world position relative to planet center.
vec3 planet__local_to_world(int face, int cx, int cy, int altitude, float radius, vec3 localPos) {
    vec3 chunkOrigin = planet__chunk_origin(face, cx, cy, altitude, radius);
    vec3 up = normalize(chunkOrigin);
    vec3 right, forward;
    planet__chunk_rotation(face, up, right, forward);

    // localPos axes: X=right, Y=up (altitude), Z=forward
    return chunkOrigin + mat3(right, up, forward) * localPos;
}

#endif