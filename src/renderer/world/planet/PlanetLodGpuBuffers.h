#pragma once
#include <span>
#include <vector>

#include <nvrhi/nvrhi.h>

#include "PlanetLodStore.h"
#include "planet_render_types.h"
#include "renderer/rendering_components.h"
#include "renderer/vulkan/VulkanBackend.h"

namespace vp {
    /**
     * Owns every GPU buffer of the LOD system, and the readback of the requests emitted by the
     * traversal compute shader.
     *
     * Expected call order, once per frame:
     *   1. read_requests(frame)      -> feed PlanetLodTree::ingest_requests()
     *   2. upload_dirty(cmd, store)  -> push the node changes made by the tree
     *   3. reset_counters(cmd)
     *   4. seed_roots(cmd, roots)
     *   5. the traversal dispatches, ping ponging over scratch(0) and scratch(1)
     *   6. snapshot_requests(cmd, frame)
     */
    class PlanetLodGpuBuffers {
    public:
        static constexpr uint32_t MAX_REQUESTS = 2048;

        /// Maximum number of nodes the traversal can select for rendering in one frame.
        static constexpr uint32_t MAX_RENDER = 2048;

        static constexpr uint32_t REQUESTS_READBACK_COUNT = MAX_FRAMES_IN_FLIGHT + 1;

        /// Byte offset of the request array inside a readback staging buffer, which holds the
        /// counters first and the requests right after.
        static constexpr uint64_t READBACK_REQUESTS_OFFSET = LOD_COUNTER_BUFFER_SIZE;

        /**
         * @param backend  Vulkan backend be able to instantiate buffers
         * @param maxNodes Node capacity, must match the value given to PlanetLodStore::init().
         *                 Also sizes the two traversal scratch queues, since a single BFS level
         *                 can in the worst case hold every live node.
         */
        PlanetLodGpuBuffers(VulkanBackend* backend, uint32_t maxNodes);

        /**
         * Push every node modified by the CPU to the GPU
         * Clearing the dirty list from the Lod store is to the caller responsibility
         *
         * Dirty indices are sorted and contiguous runs merged into a single copy. That pays off
         * because PlanetLodStore hands out children in blocks of 8: publishing a subdivision
         * dirties 8 consecutive nodes, which collapses into one 128 byte write
         *
         * @param cmd Command list to record the copies into. Must be the one submitted before
         *            the traversal dispatches, so the shader sees the new nodes
         * @param store CPU mirror holding the node data and the dirty list
         */
        void upload_dirty(nvrhi::CommandListHandle cmd, const PlanetLodStore& store);

        /**
         * Zero every counter
         * @param cmd Command list
         */
        void reset_counters(nvrhi::CommandListHandle cmd);

        /**
         * Fill the first traversal queue with the roots to visit
         * @param cmd Command list
         * @param rootIndices Node indices of the live roots, from PlanetLodTree::root_indices()
         */
        void seed_roots(nvrhi::CommandListHandle cmd, const std::vector<uint32_t>& rootIndices);

        /**
         * Copy the counters and the request queue into this frame's staging buffer, so they can
         * be read back later. Must run after the traversal dispatches
         * @param cmd Command list to record the copies into
         * @param frame Monotonically increasing frame counter, shared with read_requests()
         */
        void snapshot_requests(nvrhi::CommandListHandle cmd, uint64_t frame);

        /**
         * Read the requests the traversal emitted MAX_FRAMES_IN_FLIGHT frames ago
         *
         * @param frame Same frame counter passed to snapshot_requests()
         * @return Requests of frame (frame - MAX_FRAMES_IN_FLIGHT)
         *         The span points into an internal vector and stays valid only until
         *         the next call to read_requests()
         */
        std::span<const GpuLodRequest> read_requests(uint64_t frame);

        /// Node buffer, GpuNode[maxNodes]
        nvrhi::IBuffer* nodes() const { return m_nodes; }

        /// Every atomic counter of the pipeline
        nvrhi::IBuffer* counters() const { return m_counters; }

        /// Request queue, GpuLodRequest[MAX_REQUESTS]
        nvrhi::IBuffer* requests() const { return m_requestsQueue; }

        /**
         * Traversal scratch queue, ping ponged between LOD levels
         * @param index 0 or 1
         */
        nvrhi::IBuffer* scratch(int index) const { return m_scratchBuffer[index]; }

        /// Node indices selected for rendering this frame, uint32[MAX_RENDER]
        nvrhi::IBuffer* render_queue() const { return m_renderQueue; }

        /// Dispatch arguments, one entry per LOD level
        nvrhi::IBuffer* dispatch_args() const { return m_dispatchArgs; }

        /**
         * Byte offset of the dispatch arguments of one LOD level, to pass to dispatchIndirect()
         * @param level LOD level being dispatched
         * @return Offset in bytes inside the dispatch args buffer
         */
        static uint32_t dispatch_args_offset(uint32_t level) { return level * DISPATCH_ARGS_STRIDE; }

    private:
        static constexpr uint32_t DISPATCH_ARGS_STRIDE = sizeof(VkDispatchIndirectCommand);

        void init_gpu();

        VulkanBackend* m_backend;
        uint32_t m_maxNodes = 0;

        nvrhi::BufferHandle m_nodes;
        nvrhi::BufferHandle m_counters;
        nvrhi::BufferHandle m_requestsQueue;

        nvrhi::BufferHandle m_scratchBuffer[2]; // ping-pong octree traversal
        nvrhi::BufferHandle m_renderQueue;

        nvrhi::BufferHandle m_dispatchArgs; // filled and managed by the GPU
        nvrhi::BufferHandle m_readback[REQUESTS_READBACK_COUNT];

        std::vector<GpuLodRequest> m_scratchCpu; // requests copied out of the mapped staging buffer
        std::vector<uint32_t> m_sortedDirty; // scratch for run merging in upload_dirty()
    };
}
