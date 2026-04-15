#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "core/math/aabb.h"

namespace vp {
    enum class CubeFace : uint8_t {
        PosX = 0, NegX = 1,
        PosY = 2, NegY = 3,
        PosZ = 4, NegZ = 5,
    };

    struct PlanetQuadtreeNode {
        glm::vec2 center;
        float size; // size of the grid
        uint8_t level;

        // -1 if nothing
        int32_t childrens[4] = {-1, -1, -1, -1};

        bool isLeaf = false;
    };

    class PlanetQuadtree {
    public:
        void build(float planetRadius, const glm::vec3& cameraPos, CubeFace face, int crustDepth, float splitThreshold);

        void debug_viz(glm::vec3 planetWorldPos) {
            if (m_nodes.empty()) return;
            debug_viz(m_nodes[0], planetWorldPos);
        }

        glm::vec3 node_to_sphere(const PlanetQuadtreeNode& node, float planetRadius) const;

    private:
        std::vector<PlanetQuadtreeNode> m_nodes;
        uint8_t m_maxDepth = 2;
        CubeFace m_face = CubeFace::PosY;
        float m_planetRadius = 0.0f;

        void debug_viz(PlanetQuadtreeNode &node, glm::vec3 planetWorldPos);
        void subdivide(int32_t nodeIndex);
    };
}
