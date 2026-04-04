//
// Created by raph on 04/04/2026.
//

#include "PlanetOctree.h"

void PlanetOctree::build(float planetRadius, const glm::vec3 &cameraPos, int maxDepth, float splitThreshold) {
    m_nodes.clear();

    PlanetOctreeNode root {
        .center = glm::vec3(0.0f, 0.0f, 0.0f),
        .size = planetRadius * 2.0f,
        .level = 0,
        .isLeaf = true
    };

    m_nodes.push_back(root);
    subdivide_node(0, planetRadius, cameraPos, 0, maxDepth, splitThreshold);
}

void PlanetOctree::subdivide_node(int32_t nodeIndex, float planetRadius, const glm::vec3 &cameraPos, int currentDepth,
    int maxDepth, float splitThreshold) {

    if (currentDepth >= maxDepth) return;

    float nodeSize = m_nodes[nodeIndex].size;
    glm::vec3 nodeCenter = m_nodes[nodeIndex].center;

    float levelThreshold = splitThreshold / (float)(1 << currentDepth);

    float halfSize = nodeSize * 0.5f;
    glm::vec3 closest = glm::clamp(cameraPos, nodeCenter - halfSize, nodeCenter + halfSize);
    float dist = glm::distance(cameraPos, closest);

    if (dist > levelThreshold) return;

    m_nodes[nodeIndex].isLeaf = false;

    int32_t childIndices[8];
    for (int i = 0; i < 8; i++) {
        glm::vec3 offset = glm::vec3(
            (i & 1) ? 0.5f : -0.5f,
            (i & 2) ? 0.5f : -0.5f,
            (i & 4) ? 0.5f : -0.5f
        ) * nodeSize * 0.5f;

        PlanetOctreeNode child {
            .center = nodeCenter + offset,
            .size = nodeSize * 0.5f,
            .level = static_cast<uint16_t>(currentDepth + 1),
            .isLeaf = true
        };

        m_nodes.push_back(child);
        childIndices[i] = static_cast<int32_t>(m_nodes.size() - 1);
        m_nodes[nodeIndex].children[i] = childIndices[i];
    }

    for (int i = 0; i < 8; i++) {
        subdivide_node(childIndices[i], planetRadius, cameraPos, currentDepth + 1, maxDepth, splitThreshold);
    }
}
