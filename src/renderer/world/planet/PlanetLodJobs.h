#pragma once
#include <cstdint>
#include <vector>

#include "planet_render_types.h"
#include "core/world/planet/planet_components.h"

namespace vp {
    /**
     * Routing table between the LOD tree and the worker pools
     *
     * The generator and the mesher carry an opaque token back and forth and know nothing about
     * the octree. This is what turns that token back into the node the job was started for
     *
     * A token embeds a generation counter alongside its slot, so a result arriving after the
     * slot has been reused is recognised as stale and dropped instead of being applied to
     * whichever job took the slot in the meantime. Token 0 is never valid
     */
    class PlanetLodJobs {
    public:
        struct Job {
            uint32_t nodeIndex = NODE_INVALID_PTR;
            PlanetNodeCoord coord;
            /// GpuNode::generation of the slot when the job was opened, so the tree can tell a
            /// result for this node from one for whatever took the slot in the meantime
            uint8_t generation = 0;
        };

        /**
         * Open a job and mint the token to hand to the workers
         * @param nodeIndex Node the job is going to fill
         * @param coord Coordinate of that node at the time the job starts
         * @param generation Generation of that node slot at the time the job starts
         * @return Token, valid until close() accepts it
         */
        uint64_t open(uint32_t nodeIndex, const PlanetNodeCoord &coord, uint8_t generation);

        /**
         * Look a job up without closing it, for the hop between generation and meshing
         * @param token Token returned by open()
         * @param out Receives the job on success
         * @return False if the token is stale, in which case out is untouched
         */
        bool peek(uint64_t token, Job &out) const;

        /**
         * Close a job and release its slot
         * @param token Token returned by open()
         * @param out Receives the job on success
         * @return False if the token is stale, in which case nothing is released
         */
        bool close(uint64_t token, Job &out);

        /// Jobs currently open, for diagnostics
        size_t open_count() const { return m_slots.size() - m_free.size(); }

    private:
        struct Slot {
            Job job;
            /// Bumped on every close, which is what invalidates the tokens of the previous user
            uint32_t generation = 1;
            bool open = false;
        };

        static uint32_t token_slot(uint64_t token) { return static_cast<uint32_t>(token >> 32); }
        static uint32_t token_generation(uint64_t token) { return static_cast<uint32_t>(token); }

        /// Slot a token points at, or nullptr when the token is stale or was never issued
        const Slot *resolve(uint64_t token) const;

        std::vector<Slot> m_slots;
        std::vector<uint32_t> m_free;
    };
}
