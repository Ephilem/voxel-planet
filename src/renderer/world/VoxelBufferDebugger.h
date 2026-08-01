#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "VoxelBuffer.h"
#include "core/world/planet/planet_components.h"
#include "renderer/rendering_components.h"

namespace vp {
    /**
     * ImGui view over the geometry arena of a VoxelBuffer
     *
     * The arena is a suballocator: face regions of FACES_PER_REGION quads handed out in runs,
     * plus a draw slot per mesh. Both run out silently, and the two interesting failures are not
     * visible from a simple used-versus-total counter:
     *
     *   - fragmentation, where plenty of regions are free but no run is long enough
     *   - rounding waste, where a chunk of 1010 faces takes two whole regions
     *
     * This panel surfaces both, and lists the allocations that weigh the most so a runaway node
     * can be traced back to its coordinate
     */
    class VoxelBufferDebugger {
    public:
        /// One allocated draw slot, as the panel needs to see it
        struct SlotInfo {
            uint32_t drawSlot = UINT32_MAX;
            uint32_t faceCount = 0;
            uint32_t faceRegionStart = 0;
            uint32_t faceRegionCount = 0;
            PlanetNodeCoord coord;
        };

        /**
         * Draw the panel. Cheap enough to call every frame, everything is recomputed from the
         * buffer's own free lists
         *
         * @param title Window title, so several buffers can be inspected side by side
         * @param buffer Arena to inspect
         * @param meshes Uploaded meshes indexed by draw slot. Entries that are not allocated are
         *               skipped, so passing the renderer's whole slot table is fine
         * @param coords Node coordinate of each draw slot, same indexing as meshes. May be
         *               shorter or empty, in which case the coordinate column stays blank
         */
        void draw(const char *title,
                  const VoxelBuffer &buffer,
                  std::span<const VoxelChunkMesh> meshes,
                  std::span<const PlanetNodeCoord> coords);

    private:
        /// Rows shown in the allocation table, largest first
        static constexpr int TOP_ALLOCATION_COUNT = 24;

        /// Buckets of the occupancy strip. One pixel per bucket at a typical panel width
        static constexpr int OCCUPANCY_BUCKETS = 384;

        /// Bars in the face count histogram
        static constexpr int HISTOGRAM_BUCKETS = 32;

        /**
         * Rebuild the per region used flags from the buffer's free list
         * @param buffer Arena to inspect
         */
        void rebuild_region_map(const VoxelBuffer &buffer);

        /**
         * Draw the occupancy strip, one bucket per span of regions, shaded by how full it is
         * @param label Caption drawn above the strip
         */
        void draw_occupancy_strip(const char *label);

        /// Reused across frames so the panel allocates nothing in steady state
        std::vector<uint8_t> m_regionUsed;
        std::vector<float> m_occupancy;
        std::vector<SlotInfo> m_allocations;
        std::vector<float> m_histogram;
    };
}
