#pragma once
#include <array>
#include <cmath>
#include <queue>
#include <vector>

#include <glm/glm.hpp>

#include "core/math/frustrum.h"
#include "core/world/world_components.h"
#include "core/world/planet/planet_types.h"
#include "renderer/world/planet/planet_rendering_types.h"

namespace vp {
    const uint32_t INVALID_NODE = 0xFFFFFFFF;

    /// Four corners then the center
    constexpr int NODE_SAMPLE_COUNT = 5;

    struct PlanetQuadtreeNode {
        CubemapFace face = FACE_UNKNOWN;
        uint8_t level = 0;
        uint32_t x = 0, y = 0;

        uint32_t firstChild = INVALID_NODE; // index of the first of the 4 contiguous children

        /// Node bounds in planet space, swept over the height range
        glm::vec3 boundsLo{0.f};
        glm::vec3 boundsHi{0.f};

        [[nodiscard]] bool is_leaf() const { return firstChild == INVALID_NODE; }
    };
    static_assert(sizeof(PlanetQuadtreeNode) <= 48, "PlanetQuadtreeNode grew, re-profile update()");

    struct PlanetLodParams {
        double planetRadius = 667544.0;
        uint8_t maxLevel = 10;

        double splitFactor = 3.0;

        /// Merge at splitFactor * mergeHysteresis
        double mergeHysteresis = 1.2;


        float morphRange = 0.5;

        double minNodeHeight = -2000.0;
        double maxNodeHeight = 5000.0;

        double faceCullAngleDeg = 90.0;

        uint32_t chunkSize = CHUNK_SIZE;
    };

    /**
     * Six quadtrees, one per cube face, sharing a single node pool.
     * The tree only decides what to draw
     */
    class PlanetQuadtrees {
    public:
        explicit PlanetQuadtrees();
        ~PlanetQuadtrees() = default;

        /**
         * Split and merge so the subdivision matches the camera
         * @param cameraPosPlanet camera in planet space, meters, origin at the planet center
         */
        void update(const glm::dvec3& cameraPosPlanet, const PlanetLodParams& params);

        [[nodiscard]] uint32_t root(CubemapFace face) const { return m_roots[face]; }
        [[nodiscard]] const PlanetQuadtreeNode& node(uint32_t index) const { return m_nodes[index]; }
        [[nodiscard]] size_t live_node_count() const { return m_nodes.size() - m_freeNodes.size() * 4; }

        /// Center of a node on the sphere, in planet space
        [[nodiscard]] glm::dvec3 node_center(uint32_t index, const PlanetLodParams& params) const;

        /// Length of a node edge on the sphere, in meters
        [[nodiscard]] double node_size(uint32_t index, const PlanetLodParams& params) const;

        // Debug helpers
        void set_frozen(bool frozen) { m_frozen = frozen; }
        [[nodiscard]] bool is_frozen() const { return m_frozen; }

        struct Stats {
            uint32_t leafCount = 0;
            uint32_t splits = 0;
            uint32_t merges = 0;
            uint32_t culledFaces = 0;
            uint32_t culledNodes = 0;
            uint32_t balanceSplits = 0; // splits forced by the 2:1 neighbour rule
            uint32_t frustumCulledNodes = 0; // subtrees skipped when collecting the draw list
            uint32_t collectedTiles = 0; // leaves that made it into the draw list
        };

        [[nodiscard]] const Stats& stats() const { return m_stats; }

        /// What debug_draw colors each node by
        enum class DebugMode {
            Level,
            Face,
        };

        /**
         * Draws the outline of every leaf
         * @param originRender planet center in render space, ie the planet GlobalTransform position
         * @param segmentsPerEdge subdivisions per edge, so the outline follows the curvature
         */
        void debug_draw(const glm::vec3& originRender, const PlanetLodParams& params,
                        DebugMode mode = DebugMode::Level, int segmentsPerEdge = 6) const;

        /**
         * Collect planet tiles for rendering of these tiles
         *
         * @param frustum in camera relative space, ie the same space as the emitted
         *                originSpacePos. Null disables the test
         */
        void collect_node(uint32_t index, const PlanetLodParams &params, const glm::dvec3 &cameraPosPlanet,
                          std::vector<PlanetTileDrawItem> &out, const Frustrum *frustum = nullptr);

        /// Resets the per collect counters. update() cannot do it, since it returns early
        /// when frozen while collection still runs every frame
        void begin_collect() {
            m_stats.frustumCulledNodes = 0;
            m_stats.collectedTiles = 0;
        }

    private:
        void update_node(uint32_t index, const glm::dvec3& cameraPosPlanet, const PlanetLodParams& params);

        /// Distance from the camera to the node bounds, meters. Zero inside the node
        [[nodiscard]] double node_distance(uint32_t index, const glm::dvec3& cameraPosPlanet,
                                           const PlanetLodParams& params) const;

        /// True when every corner of the node is hidden by the planet itself
        [[nodiscard]] bool below_horizon(uint32_t index, const glm::dvec3& cameraPosPlanet,
                                         const PlanetLodParams& params) const;

        void debug_draw_node(uint32_t index, const glm::vec3& originRender, const PlanetLodParams& params,
                             DebugMode mode, int segmentsPerEdge) const;

        /// Corner of a node on the sphere. cu,cv in [0,1] inside the node
        [[nodiscard]] glm::dvec3 node_point(uint32_t index, double cu, double cv,
                                            const PlanetLodParams& params) const;

        /// Fills boundsLo/boundsHi. Called once per node, on creation
        static void init_node_bounds(PlanetQuadtreeNode& node, const PlanetLodParams& params);

        /// Rebuilds every node's bounds, after a change to the radius or the height range
        void rebuild_bounds(const PlanetLodParams& params);

        void split(uint32_t index, const PlanetLodParams& params);
        void merge(uint32_t index);

        std::vector<PlanetQuadtreeNode> m_nodes;
        std::array<uint32_t, 6> m_roots{};
        std::queue<uint32_t> m_freeNodes; // indices of the first of 4 contiguous freed children

        /// The values the cached bounds were built from. Bounds are geometry plus radius
        /// and height range, so a live tweak of any of the three has to invalidate them
        double m_boundsRadius = 0.0;
        double m_boundsMinHeight = 0.0;
        double m_boundsMaxHeight = 0.0;

        Stats m_stats;
        bool m_frozen = false;
    };
}
