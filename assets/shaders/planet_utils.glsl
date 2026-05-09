#ifndef PLANET_UTILS_GLSL
#define PLANET_UTILS_GLSL

#define CHUNK_SIZE_F 32.0

#define PI_4 0.7853981633

float planet__equiangular(float s) {
    return tan(s);
}

// tan(a + b) where tanA = tan(a) is precomputed and b is a small angle.
// Avoids catastrophic cancellation when adding a small b to a large a before tan().
float planet__tan_add(float tanA, float b) {
    float tanB = tan(b);
    return (tanA + tanB) / (1.0 - tanA * tanB);
}



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
    float u = planet__equiangular(float(cx) * CHUNK_SIZE_F / radius);
    float v = planet__equiangular(float(cy) * CHUNK_SIZE_F / radius);
    vec3 dir = normalize(planet__face_to_cube_dir(face, u, v));
    return dir * (radius + float(altitude) * CHUNK_SIZE_F);
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

// Transform a position of a single block within a chunk (localPos) to planet space. Useful to compute the final position of the vertex
vec3 planet__local_to_world(int face, int cx, int cy, int altitude, float radius, vec3 localPos) {
    float face_x = float(cx) * CHUNK_SIZE_F + localPos.x;  // right
    float face_z = float(cy) * CHUNK_SIZE_F + localPos.z;  // forward
    float r      = radius + float(altitude) * CHUNK_SIZE_F + localPos.y;  // radial

    float u = planet__equiangular(face_x / radius);
    float v = planet__equiangular(face_z / radius);

    return normalize(planet__face_to_cube_dir(face, u, v)) * r;
}


#endif