#pragma once
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include "PlanetLodStore.h"
#include "planet_render_types.h"

namespace vp {
    struct LodJobResult {
        /// Node the job was started for.
        uint32_t nodeIndex = NODE_INVALID_PTR;
        /// Coordinate the job was started for, used to detect a recycled slot.
        PlanetNodeCoord coord;
        /// True if the node turned out uniform: no geometry, and no point subdividing it.
        bool empty = false;
        /// VoxelChunkMesh::drawSlotIndex of the uploaded geometry, ignored when empty.
        uint32_t meshId = NODE_INVALID_MESH;
    };

    /**
     * State machine of the LOD octree
     *
     * The GPU picks what to load: the traversal shader emits requests, the tree turns them into
     * generation jobs, and publishes the results back into the node buffer
     *
     * Its one invariant is that a node the GPU may render always has either its own mesh or a
     * complete set of children
     *
     * This class is not thread safe
     */
    class PlanetLodTree {
    public:
        using SubmitJobFn = std::function<void(uint32_t nodeIndex, const PlanetNodeCoord &coord, uint32_t priority)>;
        using ReleaseMeshFn = std::function<void(uint32_t meshId)>;

        struct Config {
            uint8_t rootLevel = PLANET_MAX_LOD;
            int rootRadius = 7;

            int rootAltMin = 0;
            int rootAltMax = 0;

            uint32_t maxNodes = 1u << 18;

            uint32_t maxJobInFlight = 256;
        };

        /**
         * Allocate the node store and reset the tree.
         * @param config Configuration
         */
        void init(const Config &config);

        void set_submit_job(SubmitJobFn fn) { m_submitJob = std::move(fn); }
        void set_release_mesh(ReleaseMeshFn fn) { m_releaseMesh = std::move(fn); }

        /**
         * Turn the requests emitted by the traversal shader into generation jobs
         * @param requests Requests read back from the GPU, expected sorted by descending priority
         */
        void ingest_requests(std::span<const GpuLodRequest> requests);

        /**
         * Take in a finished job. Results whose node slot has been recycled in the meantime are
         * discarded, and their geometry released through the mesh release callback
         * @param result Job outcome
         */
        void on_job_done(const LodJobResult &result);

        /**
         * Move the root disc so it stays centred on the player. Roots that fall outside the
         * radius are destroyed with their whole subtree, missing ones are created empty and
         * will be filled by the GPU asking for them
         *
         * @param face Cube face the player currently stands on
         * @param rootU Player position along u, in root nodes
         * @param rootV Player position along v, in root nodes
         */
        void update_roots(CubeFace face, int32_t rootU, int32_t rootV);

        const std::vector<uint32_t> &root_indices() const { return m_rootIndices; }

        PlanetLodStore &store() { return m_store; }
        const PlanetLodStore &store() const { return m_store; }

        uint32_t jobs_in_flight() const { return m_inFlight; }

    private:
        /**
         * A subdivision whose children exist in the store but are not visible to the GPU yet
         * It is published only once every child has been resolved, so the parent never points
         * at incomplete geometry
         */
        struct PendingSubdivision {
            uint32_t parentIndex = NODE_INVALID_PTR;
            uint32_t childBlock = NODE_INVALID_PTR;
            uint8_t satisfiedMask = 0;
            uint8_t emptyMask = 0; // bit i set if child i turned out uniform
        };

        /// Start a job for a node that is the right size on screen but owns no geometry
        void handle_mesh_request(uint32_t nodeIndex, uint32_t priority);

        /// Allocate and start the 8 children of a node that is too coarse on screen
        void handle_children_request(uint32_t nodeIndex, uint32_t priority);

        /// Write childPtr into the parent, or turn it uniform when every child came back empty
        void publish_subdivision(size_t pendingIndex);

        /// Recursively release the children and geometry of a node, then zero it
        void destroy_subtree(uint32_t nodeIndex);

        /// Allocate a block and place a root node in its first slot
        uint32_t create_root(const PlanetNodeCoord &coord);

        /// Clear NODE_REQUESTED on the GPU side so the shader is allowed to ask again
        void rearm(uint32_t nodeIndex);

        /// Forward a job to the worker pool and account for it in the in flight budget
        void submit_job(uint32_t nodeIndex, const PlanetNodeCoord &coord, uint32_t priority);

        Config m_config;
        PlanetLodStore m_store;

        SubmitJobFn m_submitJob;
        ReleaseMeshFn m_releaseMesh;

        std::unordered_map<PlanetNodeCoord, uint32_t, PlanetNodeCoordHash> m_roots;
        std::vector<uint32_t> m_rootIndices;

        std::vector<PendingSubdivision> m_pending;
        std::unordered_map<uint32_t, size_t> m_childToPending; // child node index -> m_pending slot

        uint32_t m_inFlight = 0;

        bool m_hasCenter = false;
        CubeFace m_centerFace = PosX;
        int32_t m_centerU = 0;
        int32_t m_centerV = 0;
    };
}