#pragma once
#include <cstdint>
#include <vector>

#include "planet_render_types.h"

namespace vp {
    /**
     * CPU mirror of the GPU node buffer, and allocator of node slots
     *
     * Slots are only ever handed out as blocks of 8 consecutive nodes, because a parent
     * addresses its children with a single childPtr plus an 8 bit mask
     */
    class PlanetLodStore {
    public:
        /// Number of nodes in one allocation block
        static constexpr uint32_t BLOCK_SIZE = 8;

        PlanetLodStore() = default;

        /**
         * @param maxNodes Node capacity, rounded up to a multiple of BLOCK_SIZE
         *                 Must match the capacity given to PlanetLodGpuBuffers::init()
         */
        void init(uint32_t maxNodes);

        /**
         * Allocate 8 nodes side by side
         * @return Index of the first allocated node, or NODE_INVALID_PTR if the store is full
         */
        uint32_t allocate_block();

        /**
         * Release a block and recycle its nodes. The whole block is flagged dirty so the GPU sees
         * the nodes disappear on the next upload
         * @param first First index of the 8 nodes, as returned by allocate_block()
         */
        void free_block(uint32_t first);

        /**
         * Retire a single node: clear it, bump its generation and flag it dirty.
         *
         * The generation is what makes a slot recognisable across a reuse. Requests come back
         * MAX_FRAMES_IN_FLIGHT frames late and job results later still, and a cleared node reads
         * as a perfectly valid level 0 node at the origin, so neither the contents nor the
         * coordinate can be used to tell a live slot from a recycled one
         *
         * @param index Node index
         */
        void recycle(uint32_t index);

        /**
         * Access a node for reading
         * @param index Node index
         * @return The node
         */
        const GpuNode &at(uint32_t index) const { return m_nodes[index]; }

        /**
         * Access a node for writing. The caller is responsible for calling mark_dirty()
         * afterwards, since the store cannot tell whether the change is meant to be visible
         * to the GPU yet
         * @param index Node index
         * @return The node
         */
        GpuNode &at(uint32_t index) { return m_nodes[index]; }

        /**
         * Queue a node for upload to the GPU at the end of the frame
         * @param index Node index
         */
        void mark_dirty(uint32_t index);

        /// Nodes modified since the last clear_dirty(), in no particular order
        const std::vector<uint32_t> &dirty() const { return m_dirty; }

        /// Drop the dirty list. Call it once the changes have been uploaded
        void clear_dirty();

        /// Base pointer of the node array, for bulk copies to the GPU
        const GpuNode *data() const { return m_nodes.data(); }

        /// Total number of node slots, always a multiple of BLOCK_SIZE
        uint32_t capacity() const { return static_cast<uint32_t>(m_nodes.size()); }

        /// Number of blocks currently handed out, for diagnostics
        uint32_t used_blocks() const { return m_neverDistributedIndex - static_cast<uint32_t>(m_freeBlocks.size()); }

    private:
        std::vector<GpuNode> m_nodes;
        std::vector<uint32_t> m_freeBlocks;

        uint32_t m_neverDistributedIndex = 0; // number of blocks ever handed out
        uint32_t m_maxBlocks = 0;

        std::vector<uint32_t> m_dirty;
        std::vector<uint8_t> m_inDirty; // membership flag per node, avoids duplicates in m_dirty
    };
}