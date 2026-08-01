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
        /// A job for this node's own geometry is pending
        NODE_REQ_MESH = 1 << 1,
        /// The node and its whole subtree are uniform: the traversal skips it outright
        NODE_EMPTY = 1 << 2,
        NODE_FINEST = 1 << 3,
        /// A subdivision for this node is pending. Separate from NODE_REQ_MESH on purpose: a
        /// node that is too coarse on screen and owns nothing asks for both at once, and a
        /// single shared bit would let whichever request won the race lock out the other
        NODE_REQ_SPLIT = 1 << 4,
        /// The slot holds a real node. A freed slot is all zeroes, and zero is a perfectly valid
        /// coordinate, so nothing else distinguishes the two
        NODE_ALIVE = 1 << 5,
        /**
         * This node's own geometry came back empty. Stop asking for it, but keep descending.
         *
         * Distinct from NODE_EMPTY because a node can now have a mesh job and a subdivision in
         * flight at the same time: that is what draws it coarsely while its subtree is being
         * built. Folding the two together would let an empty mesh result mark a node whose
         * subdivision is still running as uniform, and the traversal would then skip the whole
         * subtree the moment it published.
         */
        NODE_NO_MESH = 1 << 6,
    };

    /**
     * A single octree node, as stored in the GPU node buffer
     */
    struct GpuNode {
        uint32_t level : 4;
        uint32_t face : 3;
        uint32_t flags : 8; // NodeFlags
        uint32_t alt : 9; // biased by NODE_ALT_BIAS

        /// Bumped every time the slot is recycled, and echoed back by every request the traversal
        /// emits for it. A request is read back MAX_FRAMES_IN_FLIGHT frames late, by which time
        /// the slot may hold a different node or no node at all, and comparing coordinates is not
        /// enough to notice: a freed slot reads as a valid coordinate, and the same coordinate is
        /// routinely recreated. Without this the tree acts on dead slots, and the geometry it
        /// allocates for them is unreachable and never released
        uint32_t generation : 8 = 0;

        uint32_t u : 16;
        uint32_t v : 16;

        // These two default to their "absent" sentinel rather than to zero, and that matters.
        // A dead slot is produced with GpuNode{} in several places, and zero is a perfectly
        // valid block index: without this a zeroed node claims to own the children at block 0,
        // and node 0 lives in block 0, so destroying it recurses into itself forever.
        uint32_t childPtr : 24 = NODE_INVALID_PTR; // index of the first of 8 consecutive children
        uint32_t childMask : 8 = 0; // bit i set if child i is worth visiting

        uint32_t meshId : 24 = NODE_INVALID_MESH; // VoxelChunkMesh::drawSlotIndex
        uint32_t meshFlags : 8 = 0;
    };
    static_assert(sizeof(GpuNode) == 16, "GpuNode should be 16 bytes");

    /**
     * Build a live node.
     * @param c Coordinate of the node
     * @param flags Extra NodeFlags, NODE_ALIVE is always added
     * @param generation Generation of the slot the node is about to occupy, from the node
     *                   already there. Passing 0 for a slot that has been used before makes
     *                   stale requests indistinguishable from fresh ones again
     */
    inline GpuNode make_node(const PlanetNodeCoord &c, uint8_t flags = 0, uint8_t generation = 0) {
        GpuNode n{};
        n.level = c.level;
        n.face = static_cast<uint32_t>(c.face);
        n.flags = flags | NODE_ALIVE;
        n.alt = static_cast<uint32_t>(c.alt + NODE_ALT_BIAS) & 0x1FFu;
        n.generation = generation;
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

    /// True if the node points at a block of children.
    /// The mask is part of the test on purpose: a published subdivision always keeps at least
    /// one live child, so a valid pointer with an empty mask can only be a corrupt node, and
    /// treating it as childless is what stops the damage from spreading down a recursion.
    inline bool node_has_children(const GpuNode &n) {
        return n.childPtr != NODE_INVALID_PTR && n.childMask != 0;
    }

    enum LodRequestType : uint32_t {
        /// The node is the right size on screen but owns no geometry yet
        REQ_MESH = 0,
        /// The node is too big on screen and must be subdivided
        REQ_CHILDREN = 1,
        /// The node has shrunk enough to stand in for its whole subtree: release the children
        REQ_MERGE = 2
    };

    struct GpuLodRequest {
        uint32_t nodeIndex;
        uint32_t type; // LodRequestType
        uint32_t priority; // higher is more urgent
        /// GpuNode::generation as it stood when the request was emitted. See that field for why
        uint32_t generation;
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