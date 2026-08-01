#ifndef PLANET_LOD_NODE_GLSL
#define PLANET_LOD_NODE_GLSL

// Mirror of vp::GpuNode in src/renderer/world/planet/planet_render_types.h
//
//   x : level:4 | face:3 | flags:8 | alt:9 (biased) | pad:8
//   y : u:16 | v:16
//   z : childPtr:24 | childMask:8
//   w : meshId:24 | meshFlags:8
struct LodNode {
    uint x;
    uint y;
    uint z;
    uint w;
};

#define NODE_LEVEL_SHIFT 0u
#define NODE_LEVEL_MASK  0xFu

#define NODE_FACE_SHIFT  4u
#define NODE_FACE_MASK   0x7u

#define NODE_FLAGS_SHIFT 7u
#define NODE_FLAGS_MASK  0xFFu

#define NODE_ALT_SHIFT   15u
#define NODE_ALT_MASK    0x1FFu
#define NODE_ALT_BIAS    256

// Mirror of vp::NodeFlags.
#define NODE_HAS_MESH  1u
#define NODE_REQUESTED 2u
#define NODE_EMPTY     4u
#define NODE_FINEST    8u

// NODE_REQUESTED already shifted into place inside word x, for the atomics
#define NODE_REQUESTED_BIT (NODE_REQUESTED << NODE_FLAGS_SHIFT)

#define NODE_INVALID_PTR  0xFFFFFFu
#define NODE_INVALID_MESH 0xFFFFFFu

uint node_level(LodNode node) { return (node.x >> NODE_LEVEL_SHIFT) & NODE_LEVEL_MASK; }
uint node_face(LodNode node) { return (node.x >> NODE_FACE_SHIFT) & NODE_FACE_MASK; }
uint node_flags(LodNode node) { return (node.x >> NODE_FLAGS_SHIFT) & NODE_FLAGS_MASK; }

int node_alt(LodNode node) { return int((node.x >> NODE_ALT_SHIFT) & NODE_ALT_MASK) - NODE_ALT_BIAS; }

// u and v are read as signed 16 bit values, so a cube face is centred on the origin. Mirror of
// vp::node_sign_extend_16(). The subtraction is done in int space to stay portable: converting
// an out of range uint straight to int is not something the spec pins down.
int node_sign_extend_16(uint value) {
    uint bits = value & 0xFFFFu;
    return (bits & 0x8000u) != 0u ? int(bits) - 65536 : int(bits);
}

int node_u(LodNode node) { return node_sign_extend_16(node.y); }
int node_v(LodNode node) { return node_sign_extend_16(node.y >> 16u); }

uint node_child_ptr(LodNode node) { return node.z & 0xFFFFFFu; }
uint node_child_mask(LodNode node) { return (node.z >> 24u) & 0xFFu; }

uint node_mesh_id(LodNode node) { return node.w & 0xFFFFFFu; }

bool node_has_children(LodNode node) { return node_child_ptr(node) != NODE_INVALID_PTR; }

#endif
