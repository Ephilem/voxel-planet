//
// Created by raph on 15/04/2026.
//

#include "PlanetQuadtree.h"

#include "core/debug/DebugDraw.h"


using namespace vp;

void PlanetQuadtree::build(float planetRadius, const glm::vec3 &cameraPos, CubeFace face, int crustDepth,
    float splitThreshold) {

    m_face = face;
    m_planetRadius = planetRadius;
    m_nodes.clear();

    // create root
    PlanetQuadtreeNode root {
        .center = glm::vec2(0.0f, 0.0f),
        .size = planetRadius * 2.0f,
        .level = 0,
        .isLeaf = true
    };

    m_nodes.push_back(root);
    subdivide(0);
}

glm::vec3 PlanetQuadtree::node_to_sphere(const PlanetQuadtreeNode &node, float planetRadius) const {
    glm::vec3 cubePos;
    switch (m_face) {
        case CubeFace::PosX: cubePos = { planetRadius,    node.center.y, node.center.x }; break;
        case CubeFace::NegX: cubePos = {-planetRadius,    node.center.y, node.center.x }; break;
        case CubeFace::PosY: cubePos = { node.center.x,   planetRadius,  node.center.y }; break;
        case CubeFace::NegY: cubePos = { node.center.x,  -planetRadius,  node.center.y }; break;
        case CubeFace::PosZ: cubePos = { node.center.x,   node.center.y, planetRadius  }; break;
        case CubeFace::NegZ: cubePos = { node.center.x,   node.center.y,-planetRadius  }; break;
    }
    return glm::normalize(cubePos) * planetRadius;
}

void PlanetQuadtree::debug_viz(PlanetQuadtreeNode &node, glm::vec3 planetWorldPos) {
    if (node.isLeaf) {
        glm::vec3 spherePos = node_to_sphere(node, m_planetRadius);
        DebugDraw::Point(spherePos + planetWorldPos, {1.0f, 0.0f, 0.0f, 1.0f});
    } else {
        for (int i = 0; i < 4; i++) {
            if (node.childrens[i] != -1) {
                debug_viz(m_nodes[node.childrens[i]], planetWorldPos);
            }
        }
    }
}

void PlanetQuadtree::subdivide(int32_t nodeIndex) {
    uint8_t level = m_nodes[nodeIndex].level + 1;
    if (level > m_maxDepth) return;

    m_nodes[nodeIndex].isLeaf = false;

    for (int i = 0; i < 4; i++) {
        glm::vec2 offset = glm::vec2(
            (i & 1) ? 0.5f : -0.5f,
            (i & 2) ? 0.5f : -0.5f
        ) * m_nodes[nodeIndex].size * 0.5f;

        PlanetQuadtreeNode child {
            .center = m_nodes[nodeIndex].center + offset,
            .size = m_nodes[nodeIndex].size * 0.5f,
            .level = level,
            .isLeaf = true
        };

        // TODO test if we can subdivise (camera not too far of the children)

        m_nodes.push_back(child);
        m_nodes[nodeIndex].childrens[i] = static_cast<int32_t>(m_nodes.size() - 1);
    }

    for (int i = 0; i < 4; i++) {
        if (m_nodes[nodeIndex].childrens[i] != -1)
            subdivide(m_nodes[nodeIndex].childrens[i]);
    }
}
