#version 450

struct ChunkInstance {
    ivec3 chunkCoord;
    uint  packed; // face (3) | level (5), mirrors the PlanetSurfaceChunkInstance bitfields
};

layout(std140, set = 0, binding = 0) uniform PlanetAnchor {
    ivec4 chunk; // xyz = anchor key, w = face
    vec4  tangentU;
    vec4  tangentV;
    vec4  up;
    vec4  quu;
    vec4  quv;
    vec4  qvv;
    vec4  camRelAnchor;
} A;

layout(std430, set = 0, binding = 1) readonly buffer Vertices  { uvec2 vertices[]; };
layout(std430, set = 0, binding = 2) readonly buffer Instances { ChunkInstance instances[]; };

layout(push_constant) uniform PushConstants {
    mat4 viewProj;

    float startFade;
    float endFade;
} pc;

layout(location = 0) flat out uint outFace;
layout(location = 1) flat out uint outTextureSlot;
layout(location = 2) out vec2 outUv;
layout(location = 3) out vec3 outNormal;
layout(location = 4) out float outDistance;
layout(location = 5) out vec3 outPos;
layout(location = 6) flat out vec3 outUp;
const int CHUNK_SIZE = 32;
const float VERTEX_SCALE = 31.0; // mirrors PLANET_SURFACE_VERTEX_SCALE: positions are fixed point

// Mirrors PlanetSurfaceChunkMesher::encode_normal: octahedral, 8 bits per axis
vec3 decode_normal(uint o) {
    vec2 e = vec2(o & 0xFFu, (o >> 8) & 0xFFu) / 255.0 * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

void main() {
    uvec2 raw = vertices[gl_VertexIndex];
    ChunkInstance inst = instances[gl_InstanceIndex];

    // Single anchor for now: chunks of another face are collapsed
    if (int(inst.packed & 7u) != A.chunk.w) {
        gl_Position = vec4(0.0);
        return;
    }

    // Mirrors the PlanetSurfaceChunkVertex bitfields
    // raw.x: x 0-9, y 10-19, z 20-29
    // raw.y: textureSlot 0-11, face 12-14, normal 15-30
    vec3 local = vec3(raw.x & 0x3FFu, (raw.x >> 10) & 0x3FFu, (raw.x >> 20) & 0x3FFu) / VERTEX_SCALE;

    // Integer key difference: where the large numbers vanish
    ivec3 dChunk = inst.chunkCoord - A.chunk.xyz;
    vec3 n = vec3(dChunk * CHUNK_SIZE) + local; // voxels from the anchor corner

    vec3 d = A.tangentU.xyz * n.x + A.tangentV.xyz * n.y;
    vec3 q = A.quu.xyz * (n.x * n.x) + A.quv.xyz * (n.x * n.y) + A.qvv.xyz * (n.y * n.y);
    float h = n.z; // voxelSize = 1 m at LOD0

    vec3 pos = d + q + A.up.xyz * h + d * (h * A.up.w); // last term: the trapezoid flare

    gl_Position = pc.viewProj * vec4(pos - A.camRelAnchor.xyz, 1.0);

    uint face = (raw.y >> 12) & 7u;
    outFace = face;
    outTextureSlot = raw.y & 0xFFFu;

    vec2 uv;
    if (face < 2u) uv = vec2(local.y, local.z);
    else if (face < 4u) uv = vec2(local.x, local.z);
    else uv = vec2(local.x, local.y);
    outUv = uv;

    // lattice -> world: positions map through J = (tangentU, tangentV, up), normals through its inverse transpose
    // (the quadratic and flare terms are ignored, negligible for the shading)
    mat3 J = mat3(A.tangentU.xyz, A.tangentV.xyz, A.up.xyz);
    outNormal = normalize(transpose(inverse(J)) * decode_normal((raw.y >> 15) & 0xFFFFu));

    outDistance = length(pos - A.camRelAnchor.xyz);
    outPos = pos - A.camRelAnchor.xyz;
    outUp  = A.up.xyz;
}
