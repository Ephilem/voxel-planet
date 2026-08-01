#pragma once
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

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

            uint32_t maxJobInFlight = 2048;
        };

        /**
         * Allocate the node store and reset the tree.
         * @param config Configuration
         */
        void init(const Config &config);

        void set_submit_job(SubmitJobFn fn) { m_submitJob = std::move(fn); }
        void set_release_mesh(ReleaseMeshFn fn) { m_releaseMesh = std::move(fn); }

        /**
         * Resize the root disc at runtime, for bring up and debugging. Forgetting the current
         * centre is what makes the next update_roots() rebuild instead of early returning, which
         * destroys the roots that fall outside the new radius
         * @param radius Root disc radius, in root nodes. 0 keeps a single root
         */
        void set_root_radius(int radius) {
            if (radius == m_config.rootRadius) return;
            m_config.rootRadius = radius;
            m_hasCenter = false;
        }

        int root_radius() const { return m_config.rootRadius; }

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

        /**
         * Backstop for the subtrees the traversal never gets to judge
         *
         * Merging is normally the shader's call, delivered as a LOD_REQ_MERGE and applied by
         * ingest_requests(). That is the accurate path, and the only one that should ever fire
         * for something on screen. But a node outside the frustum, or beyond the render
         * distance, is dropped by the traversal before any decision is reached: it emits nothing
         * at all, so it would keep its children forever. Fly in a straight line and everything
         * behind the player is exactly that
         *
         * This pass sweeps those up with a crude distance test. Give it a wide margin: it is not
         * meant to compete with the shader, only to catch what the shader never looks at
         *
         * A node only collapses if it owns a mesh of its own, which is what keeps the invariant
         * intact: geometry is never removed before its replacement is already there, so a merge
         * never opens a hole
         *
         * @param cameraWorldPos Camera position, in the frame the node grid lives in
         * @param keepFactor Children are kept while the camera is within nodeSize * keepFactor
         * @return Number of nodes released
         */
        uint32_t collapse_distant(const glm::vec3 &cameraWorldPos, float keepFactor);

        const std::vector<uint32_t> &root_indices() const { return m_rootIndices; }

        PlanetLodStore &store() { return m_store; }
        const PlanetLodStore &store() const { return m_store; }

        uint32_t jobs_in_flight() const { return m_inFlight; }

        /// Jobs the tree accepts at once. Requests over it are rearmed rather than dropped, so
        /// sitting at the cap reads as a sustained request count while the backlog drains
        uint32_t max_jobs_in_flight() const { return m_config.maxJobInFlight; }

        /// Nodes released on the GPU's own request since the last reset, for diagnostics
        uint32_t collapsed_by_gpu() const { return m_collapsedByGpu; }

        /**
         * Subdivisions waiting on their eight children.
         *
         * A high count on its own says nothing: a subdivision only publishes once all eight of
         * its children are back, and the worker pools return them a few at a time, so partly
         * satisfied entries pile up whenever the pipeline is busy. Age is the honest measure of a
         * strand, not count.
         */
        size_t pending_subdivisions() const { return m_pending.size(); }

        /// Child to subdivision mappings alive right now, always 8 per pending subdivision
        size_t pending_children() const { return m_childToPending.size(); }

        /**
         * Frames the longest waiting subdivision has been open.
         *
         * Growing without bound is the real symptom of a lost job result: the entry can never
         * complete, its block never comes back, and the parent stays flagged as requested so the
         * traversal never asks about it again.
         *
         * @return Age in frames, 0 when nothing is pending
         */
        uint64_t oldest_pending_age() const;

        void reset_collapse_stats() { m_collapsedByGpu = 0; }

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
            uint64_t openedFrame = 0; // ingest tick it was created on, for oldest_pending_age()
        };

        /// Start a job for a node that is the right size on screen but owns no geometry
        void handle_mesh_request(uint32_t nodeIndex, uint32_t priority);

        /// Allocate and start the 8 children of a node that is too coarse on screen
        void handle_children_request(uint32_t nodeIndex, uint32_t priority);

        /// Write childPtr into the parent, or turn it uniform when every child came back empty
        void publish_subdivision(size_t pendingIndex);

        /// Drop a pending entry and repair the child index map after the swap and pop
        void erase_pending(size_t pendingIndex);

        /**
         * Abandon the subdivision a node has in flight, if any.
         *
         * Pending children are not linked to their parent yet, so destroy_subtree() walks right
         * past them. Without this they would be leaked, and worse: their block goes back to the
         * allocator while m_childToPending still points at it, so a later job landing on a
         * recycled slot would be credited to an unrelated subdivision
         *
         * @param parentIndex Node whose pending subdivision is dropped
         */
        void drop_pending_subdivision(uint32_t parentIndex);

        /// Recursively release the children and geometry of a node, then zero it
        void destroy_subtree(uint32_t nodeIndex);

        /// Free the child block of a node and unlink it, leaving the node itself alone
        uint32_t collapse_children(uint32_t nodeIndex);

        /// Recursive half of collapse_distant()
        uint32_t collapse_node(uint32_t nodeIndex, const glm::vec3 &cameraWorldPos, float keepFactor);

        /// Live descendants of a node, for the released counter reported by collapse_distant()
        uint32_t count_subtree(uint32_t nodeIndex) const;

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
        uint32_t m_collapsedByGpu = 0;

        /// Ticks once per ingest_requests(), which is once per rendered frame. Only used to date
        /// pending subdivisions
        uint64_t m_frame = 0;

        bool m_hasCenter = false;
        CubeFace m_centerFace = PosX;
        int32_t m_centerU = 0;
        int32_t m_centerV = 0;
    };
}