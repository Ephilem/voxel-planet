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
} pc;

layout(location = 0) flat out uint outFace;
layout(location = 1) flat out uint outTextureSlot;

const int CHUNK_SIZE = 32;

void main() {
    uvec2 raw = vertices[gl_VertexIndex];
    ChunkInstance inst = instances[gl_InstanceIndex];

    // Single anchor for now: chunks of another face are collapsed
    if (int(inst.packed & 7u) != A.chunk.w) {
        gl_Position = vec4(0.0);
        return;
    }

    // Mirrors the PlanetSurfaceChunkVertex bitfields: x 0-7, y 8-15, z 16-23, face 24-26
    ivec3 local = ivec3(raw.x & 0xFFu, (raw.x >> 8) & 0xFFu, (raw.x >> 16) & 0xFFu);

    // Integer key difference: where the large numbers vanish
    ivec3 dChunk = inst.chunkCoord - A.chunk.xyz;
    vec3 n = vec3(dChunk * CHUNK_SIZE + local); // voxels from the anchor corner

    vec3 d = A.tangentU.xyz * n.x + A.tangentV.xyz * n.y;
    vec3 q = A.quu.xyz * (n.x * n.x) + A.quv.xyz * (n.x * n.y) + A.qvv.xyz * (n.y * n.y);
    float h = n.z; // voxelSize = 1 m at LOD0

    vec3 pos = d + q + A.up.xyz * h + d * (h * A.up.w); // last term: the trapezoid flare

    gl_Position = pc.viewProj * vec4(pos - A.camRelAnchor.xyz, 1.0);

    outFace        = (raw.x >> 24) & 7u;
    outTextureSlot = raw.y & 0xFFFFu;
}
