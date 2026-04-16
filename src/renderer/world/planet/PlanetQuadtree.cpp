//
// Created by raph on 15/04/2026.
//

#include "PlanetQuadtree.h"

#include "core/debug/DebugDraw.h"


using namespace vp;

void PlanetQuadtree::build(float planetRadius, const glm::vec3 &cameraPos, CubeFace face, int crustDepth,
                           float splitFactor) {

    m_face = face;
    m_planetRadius = planetRadius;
    m_cameraPos = cameraPos;
    m_splitFactor = splitFactor;
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

glm::vec3 PlanetQuadtree::node_to_sphere(const glm::vec2 &pos, float planetRadius) const {
    glm::vec3 cubePos;
    switch (m_face) {
        case CubeFace::PosX: cubePos = { planetRadius,    pos.y, pos.x }; break;
        case CubeFace::NegX: cubePos = {-planetRadius,    pos.y, pos.x }; break;
        case CubeFace::PosY: cubePos = { pos.x,   planetRadius,  pos.y }; break;
        case CubeFace::NegY: cubePos = { pos.x,  -planetRadius,  pos.y }; break;
        case CubeFace::PosZ: cubePos = { pos.x,   pos.y, planetRadius  }; break;
        case CubeFace::NegZ: cubePos = { pos.x,   pos.y,-planetRadius  }; break;
    }
    return glm::normalize(cubePos) * planetRadius;
}

void PlanetQuadtree::debug_viz(PlanetQuadtreeNode &node, glm::vec3 planetWorldPos) {
    if (node.isLeaf) {
        glm::vec3 spherePos = node_to_sphere(node.center, m_planetRadius);
        // DebugDraw::Point(spherePos + planetWorldPos, {1.0f, 0.0f, 0.0f, 1.0f});
        // Draw rect
        glm::vec3 upLeftPoint = node_to_sphere(node.center + (node.size * 0.5f), m_planetRadius);
        glm::vec3 upRightPoint = node_to_sphere(node.center + glm::vec2(node.size * 0.5f, -node.size * 0.5f), m_planetRadius);
        glm::vec3 downLeftPoint = node_to_sphere(node.center + glm::vec2(-node.size * 0.5f, node.size * 0.5f), m_planetRadius);
        glm::vec3 downRightPoint = node_to_sphere(node.center + glm::vec2(-node.size * 0.5f), m_planetRadius);
        DebugDraw::Line(upLeftPoint + planetWorldPos, upRightPoint + planetWorldPos, {0.0f, 1.0f, 0.0f, 1.0f});
        DebugDraw::Line(upRightPoint + planetWorldPos, downRightPoint + planetWorldPos, {0.0f, 1.0f, 0.0f, 1.0f});
        DebugDraw::Line(downRightPoint + planetWorldPos, downLeftPoint + planetWorldPos, {0.0f, 1.0f, 0.0f, 1.0f});
        DebugDraw::Line(downLeftPoint + planetWorldPos, upLeftPoint + planetWorldPos, {0.0f, 1.0f, 0.0f, 1.0f});
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

    glm::vec3 sphereCenter = node_to_sphere(m_nodes[nodeIndex].center, m_planetRadius);
    float dist = glm::distance(m_cameraPos, sphereCenter);

    if (dist > m_nodes[nodeIndex].size * m_splitFactor) return;

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
