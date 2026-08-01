#pragma once
#include <cstdint>

#include "core/world/planet/planet_components.h"

namespace vp {
    inline constexpr uint8_t PLANET_MAX_LOD = 8;

    /// Value for GpuNode::childPtr, meaning "this node has no children"
    inline constexpr uint32_t NODE_INVALID_PTR = 0xFFFFFF;

    /// Value for GpuNode::meshId, meaning "no geometry allocated for this node"
    inline constexpr uint32_t NODE_INVALID_MESH = 0xFFFFFF;

    /// GpuNode::alt is stored unsigned on 9 bits, so it is biased by this amount
    /// Representable altitude range is therefore [-256, 255] nodes at the node's own level
    inline constexpr int32_t NODE_ALT_BIAS = 256;

    /// GpuNode::u and GpuNode::v hold 16 bits read as signed, so a cube face is centred on the
    /// origin rather than starting at one of its corners. The player can then stand near
    /// coordinate zero, where float32 still has centimetre precision, instead of half a million
    /// metres away where it no longer does
    inline constexpr int32_t NODE_UV_MIN = -32768;
    inline constexpr int32_t NODE_UV_MAX = 32767;

    /// Read the low 16 bits of a packed field as a signed value
    inline constexpr int32_t node_sign_extend_16(uint32_t value) {
        return static_cast<int16_t>(static_cast<uint16_t>(value));
    }

    enum NodeFlags : uint8_t {
        NODE_HAS_MESH = 1 << 0,
        /// A request for this node is already pending
        NODE_REQUESTED = 1 << 1,
        /// The node is uniform (full air or full solid): it will never get a mesh nor children
        NODE_EMPTY = 1 << 2,
        NODE_FINEST = 1 << 3,
    };

    /**
     * A single octree node, as stored in the GPU node buffer
     */
    struct GpuNode {
        uint32_t level : 4;
        uint32_t face : 3;
        uint32_t flags : 8; // NodeFlags
        uint32_t alt : 9; // biased by NODE_ALT_BIAS
        uint32_t _pad0 : 8;

        uint32_t u : 16;
        uint32_t v : 16;

        uint32_t childPtr : 24; // index of the first of 8 consecutive children
        uint32_t childMask : 8; // bit i set if child i is worth visiting

        uint32_t meshId : 24; // VoxelChunkMesh::drawSlotIndex or NODE_INVALID_MESH
        uint32_t meshFlags : 8;
    };
    static_assert(sizeof(GpuNode) == 16, "GpuNode should be 16 bytes");

    inline GpuNode make_node(const PlanetNodeCoord &c, uint8_t flags = 0) {
        GpuNode n{};
        n.level = c.level;
        n.face = static_cast<uint32_t>(c.face);
        n.flags = flags;
        n.alt = static_cast<uint32_t>(c.alt + NODE_ALT_BIAS) & 0x1FFu;
        n.u = static_cast<uint32_t>(c.u) & 0xFFFFu;
        n.v = static_cast<uint32_t>(c.v) & 0xFFFFu;
        n.childPtr = NODE_INVALID_PTR;
        n.childMask = 0;
        n.meshId = NODE_INVALID_MESH;
        n.meshFlags = 0;
        return n;
    }

    inline PlanetNodeCoord node_coord(const GpuNode &n) {
        return {
            static_cast<CubeFace>(n.face),
            static_cast<unsigned char>(n.level),
            node_sign_extend_16(n.u),
            node_sign_extend_16(n.v),
            static_cast<int32_t>(n.alt) - NODE_ALT_BIAS
        };
    }

    /// True if the node points at a block of children
    inline bool node_has_children(const GpuNode &n) { return n.childPtr != NODE_INVALID_PTR; }

    enum LodRequestType : uint32_t {
        /// The node is the right size on screen but owns no geometry yet
        REQ_MESH = 0,
        /// The node is too big on screen and must be subdivided
        REQ_CHILDREN = 1
    };

    struct GpuLodRequest {
        uint32_t nodeIndex;
        uint32_t type; // LodRequestType
        uint32_t priority; // higher is more urgent
        uint32_t _pad0;
    };

    static_assert(sizeof(GpuLodRequest) == 16, "GpuLodRequest should be 16 bytes");

    /// Number of requests emitted this frame, atomically incremented by the traversal
    inline constexpr uint32_t LOD_COUNTER_OFFSET_REQUEST_COUNT = 0;
    /// Requests dropped because the queue was already full
    inline constexpr uint32_t LOD_COUNTER_OFFSET_REQUEST_OVERFLOW = 4;
    /// Node count in traversal scratch queue 0
    inline constexpr uint32_t LOD_COUNTER_OFFSET_QUEUE_0_COUNT = 16;
    /// Node count in traversal scratch queue 1
    inline constexpr uint32_t LOD_COUNTER_OFFSET_QUEUE_1_COUNT = 32;
    /// Number of nodes selected for rendering this frame
    inline constexpr uint32_t LOD_COUNTER_OFFSET_RENDER_COUNT = 48;

    /// Total size of the counters buffer
    inline constexpr uint32_t LOD_COUNTER_BUFFER_SIZE = 64;

    /// Byte offset of the counter of traversal queue `index` (0 or 1).
    inline constexpr uint32_t queue_count(int index) {
        return index == 0 ? LOD_COUNTER_OFFSET_QUEUE_0_COUNT : LOD_COUNTER_OFFSET_QUEUE_1_COUNT;
    }
}