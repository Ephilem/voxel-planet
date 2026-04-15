#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "core/math/aabb.h"


struct PlanetOctreeNode {
    glm::vec3 center;
    float size;
    uint16_t level;
    bool isLeaf = true;

    // Index for each childs
    int32_t children[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    inline AABB get_debug_aabb() const {
        return AABB{
            center - glm::vec3(size * 0.5f),
            center + glm::vec3(size * 0.5f)
        };
    }
};


class PlanetOctree {
public:
    /**
     * Builds the octree based on the camera position and the planet radius.
     * @param planetRadius radius of the planet in world units (meters)
     * @param cameraPos position of the camera in world space
     * @param maxDepth maximum depth of the octree. Higher values allow for more detail but increase memory usage and traversal time.
     * @param splitThreshold distance threshold for splitting nodes. Nodes closer than this distance to the camera will be split, while farther nodes will remain as leaves.
     *                       This helps to optimize the octree by only subdividing areas that are close enough to the camera to require more detail.
     */
    void build(float planetRadius, const glm::vec3& cameraPos, int maxDepth, float splitThreshold);


    // Debug methods
    const std::vector<PlanetOctreeNode>& get_nodes() const {
        return m_nodes;
    }
private:
    void subdivide_node(int32_t nodeIndex, float planetRadius, const glm::vec3& cameraPos, int currentDepth, int maxDepth, float splitThreshold);

    std::vector<PlanetOctreeNode> m_nodes;
};