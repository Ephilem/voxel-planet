#include "PlanetQuadtrees.h"

#include <cmath>

#include "core/debug/DebugDraw.h"
#include "core/world/planet/planet_transform.h"

using namespace vp;

namespace {
    /// Node extent in face coordinates, ie [-1,1] split 2^level times
    double node_extent(uint8_t level) {
        return 2.0 / double(1u << level);
    }

    /// Face coordinates of a node corner. cu,cv in [0,1] inside the node
    void node_uv(const PlanetQuadtreeNode &n, double cu, double cv, double &u, double &v) {
        const double extent = node_extent(n.level);
        u = -1.0 + (double(n.x) + cu) * extent;
        v = -1.0 + (double(n.y) + cv) * extent;
    }

    glm::vec4 level_color(uint8_t level) {
        static constexpr glm::vec4 COLORS[] = {
            {0.35f, 0.35f, 0.40f, 1.f}, // 0, coarse
            {0.20f, 0.45f, 0.95f, 1.f},
            {0.15f, 0.80f, 0.85f, 1.f},
            {0.20f, 0.90f, 0.35f, 1.f},
            {0.75f, 0.95f, 0.20f, 1.f},
            {1.00f, 0.85f, 0.15f, 1.f},
            {1.00f, 0.55f, 0.10f, 1.f},
            {1.00f, 0.25f, 0.15f, 1.f}, // 7 and above, fine
        };
        constexpr int count = int(sizeof(COLORS) / sizeof(COLORS[0]));
        return COLORS[level < count ? level : count - 1];
    }

    glm::vec4 face_color(CubemapFace face) {
        static constexpr glm::vec4 COLORS[6] = {
            {1.0f, 0.2f, 0.2f, 1.f}, // +X
            {0.6f, 0.1f, 0.1f, 1.f}, // -X
            {0.2f, 1.0f, 0.2f, 1.f}, // +Y
            {0.1f, 0.6f, 0.1f, 1.f}, // -Y
            {0.2f, 0.4f, 1.0f, 1.f}, // +Z
            {0.1f, 0.2f, 0.6f, 1.f}, // -Z
        };
        return face < 6 ? COLORS[face] : glm::vec4(1.f);
    }
}

PlanetQuadtrees::PlanetQuadtrees() {
    m_nodes.reserve(1024);

    for (uint8_t face = 0; face < 6; ++face) {
        m_roots[face] = uint32_t(m_nodes.size());
        m_nodes.push_back({CubemapFace(face), 0, 0, 0, INVALID_NODE});
    }
}

glm::dvec3 PlanetQuadtrees::node_point(uint32_t index, double cu, double cv,
                                       const PlanetLodParams &params) const {
    const PlanetQuadtreeNode &n = m_nodes[index];
    double u, v;
    node_uv(n, cu, cv, u, v);
    return face_uv_to_direction(n.face, u, v) * params.planetRadius;
}

glm::dvec3 PlanetQuadtrees::node_center(uint32_t index, const PlanetLodParams &params) const {
    return node_point(index, 0.5, 0.5, params);
}

double PlanetQuadtrees::node_size(uint32_t index, const PlanetLodParams &params) const {
    // The tangent warp cancels against the atan of the projection, so every node at a
    // level covers the same arc and the size is a plain division. Measuring the chord
    // between corners instead would read 10% short at level 0
    return planet_node_size(params.planetRadius, m_nodes[index].level);
}

void PlanetQuadtrees::split(uint32_t index) {
    const PlanetQuadtreeNode parent = m_nodes[index];

    uint32_t first;
    if (!m_freeNodes.empty()) {
        first = m_freeNodes.front();
        m_freeNodes.pop();
    } else {
        first = uint32_t(m_nodes.size());
        m_nodes.resize(m_nodes.size() + 4);
    }

    for (uint32_t i = 0; i < 4; ++i) {
        PlanetQuadtreeNode &child = m_nodes[first + i];
        child.face = parent.face;
        child.level = uint8_t(parent.level + 1);
        child.x = parent.x * 2 + (i & 1u);
        child.y = parent.y * 2 + (i >> 1u);
        child.firstChild = INVALID_NODE;
    }

    m_nodes[index].firstChild = first;
    ++m_stats.splits;
}

void PlanetQuadtrees::merge(uint32_t index) {
    const uint32_t first = m_nodes[index].firstChild;
    if (first == INVALID_NODE) return;

    for (uint32_t i = 0; i < 4; ++i) {
        merge(first + i); // depth first, so grandchildren are recycled too
    }

    m_nodes[index].firstChild = INVALID_NODE;
    m_freeNodes.push(first);
    ++m_stats.merges;
}

void PlanetQuadtrees::update(const glm::dvec3 &cameraPosPlanet, const PlanetLodParams &params) {
    if (m_frozen) return;

    m_stats = {};

    const double camLen = glm::length(cameraPosPlanet);
    // The camera exactly at the center has no direction, so nothing can be culled
    const glm::dvec3 camDir = camLen > 1e-9 ? cameraPosPlanet / camLen : glm::dvec3(0.0, 1.0, 0.0);
    const double cosFaceCull = std::cos(glm::radians(params.faceCullAngleDeg));

    for (uint8_t face = 0; face < 6; ++face) {
        const uint32_t rootIndex = m_roots[face];

        const glm::dvec3 faceDir = glm::normalize(node_center(rootIndex, params));
        if (camLen > 1e-9 && glm::dot(faceDir, camDir) < cosFaceCull) {
            merge(rootIndex);
            ++m_stats.culledFaces;
            continue;
        }

        update_node(rootIndex, cameraPosPlanet, params);
    }
}

void PlanetQuadtrees::update_node(uint32_t index, const glm::dvec3 &cameraPosPlanet, const PlanetLodParams &params) {
    const double size = node_size(index, params);
    const double distance = node_distance(index, cameraPosPlanet, params);

    if (distance > size && below_horizon(index, cameraPosPlanet, params)) {
        merge(index);
        ++m_stats.culledNodes;
        ++m_stats.leafCount;
        return;
    }

    const PlanetQuadtreeNode &n = m_nodes[index];
    const bool canSplit = n.level < params.maxLevel;
    const bool wantsSplit = distance < params.splitFactor * size;
    // Merging further out than the split distance leaves a dead band where neither
    // fires, so a camera sitting on the threshold stops flickering
    const bool wantsMerge = distance > params.splitFactor * params.mergeHysteresis * size;

    if (n.is_leaf()) {
        if (canSplit && wantsSplit) {
            split(index);
        } else {
            ++m_stats.leafCount;
            return;
        }
    } else if (wantsMerge || !canSplit) {
        merge(index);
        ++m_stats.leafCount;
        return;
    }

    const uint32_t first = m_nodes[index].firstChild;
    for (uint32_t i = 0; i < 4; ++i) {
        update_node(first + i, cameraPosPlanet, params);
    }
}

double PlanetQuadtrees::node_distance(uint32_t index, const glm::dvec3 &cameraPosPlanet,
                                      const PlanetLodParams &params) const {
    const double rLo = params.planetRadius + params.minNodeHeight;
    const double rHi = params.planetRadius + params.maxNodeHeight;

    glm::dvec3 lo(std::numeric_limits<double>::max());
    glm::dvec3 hi(std::numeric_limits<double>::lowest());

    // The four corners, plus the center: on the sphere the middle of the node
    // bulges outward from the plane of the corners, so it has to be sampled too
    for (int i = 0; i < 5; ++i) {
        const double cu = i < 4 ? double(i & 1) : 0.5;
        const double cv = i < 4 ? double(i >> 1) : 0.5;
        const glm::dvec3 dir = glm::normalize(node_point(index, cu, cv, params));
        lo = glm::min(lo, glm::min(dir * rLo, dir * rHi));
        hi = glm::max(hi, glm::max(dir * rLo, dir * rHi));
    }

    const glm::dvec3 d = glm::max(glm::max(lo - cameraPosPlanet, cameraPosPlanet - hi),
                                  glm::dvec3(0.0));
    return glm::length(d);
}

bool PlanetQuadtrees::below_horizon(uint32_t index, const glm::dvec3 &cameraPosPlanet,
                                     const PlanetLodParams &params) const {
    // Cesium's occlusion test, in units of the occluding sphere. A point is hidden
    // when it sits inside the tangent cone AND behind the plane of the horizon ring
    const double R = params.planetRadius + params.minNodeHeight;
    if (R <= 0.0) return false;

    const glm::dvec3 cv = cameraPosPlanet / R; // center to camera, sphere units
    const double vhSq = glm::dot(cv, cv) - 1.0;
    if (vhSq <= 0.0) return false; // camera below the occluding sphere, nothing hides

    const double rHi = (params.planetRadius + params.maxNodeHeight) / R;

    // The node is hidden only if all of its corners are. Testing the highest
    // point of each corner is what keeps mountains from popping in late
    for (int i = 0; i < 4; ++i) {
        const glm::dvec3 t = glm::normalize(node_point(index, double(i & 1), double(i >> 1), params)) * rHi;
        const glm::dvec3 vt = t - cv;            // camera to the corner
        const double dot = -glm::dot(vt, cv);    // positive when heading away from the camera

        // Behind the plane of the horizon ring, then inside the tangent cone
        if (dot <= vhSq) return false;
        if (dot * dot <= vhSq * glm::dot(vt, vt)) return false;
    }
    return true;
}

void PlanetQuadtrees::debug_draw(const glm::vec3 &originRender, const PlanetLodParams &params,
                                 DebugMode mode, int segmentsPerEdge) const {
    for (uint8_t face = 0; face < 6; ++face) {
        debug_draw_node(m_roots[face], originRender, params, mode, segmentsPerEdge);
    }
}

void PlanetQuadtrees::debug_draw_node(uint32_t index, const glm::vec3 &originRender,
                                      const PlanetLodParams &params, DebugMode mode,
                                      int segmentsPerEdge) const {
    const PlanetQuadtreeNode &n = m_nodes[index];

    if (!n.is_leaf()) {
        for (uint32_t i = 0; i < 4; ++i) {
            debug_draw_node(n.firstChild + i, originRender, params, mode, segmentsPerEdge);
        }
        return;
    }

    const glm::vec4 color = mode == DebugMode::Face ? face_color(n.face) : level_color(n.level);
    const int segments = segmentsPerEdge < 1 ? 1 : segmentsPerEdge;

    // Walk the four edges in node space, so the outline stays on the sphere
    // instead of cutting through it as a straight quad would
    for (int edge = 0; edge < 4; ++edge) {
        for (int s = 0; s < segments; ++s) {
            const double t0 = double(s) / segments;
            const double t1 = double(s + 1) / segments;

            double u0, v0, u1, v1;
            switch (edge) {
                case 0: u0 = t0, v0 = 0.0, u1 = t1, v1 = 0.0; break; // bottom
                case 1: u0 = 1.0, v0 = t0, u1 = 1.0, v1 = t1; break; // right
                case 2: u0 = t0, v0 = 1.0, u1 = t1, v1 = 1.0; break; // top
                default: u0 = 0.0, v0 = t0, u1 = 0.0, v1 = t1; break; // left
            }

            const glm::vec3 p0 = originRender + glm::vec3(node_point(index, u0, v0, params));
            const glm::vec3 p1 = originRender + glm::vec3(node_point(index, u1, v1, params));
            DebugDraw::Line(p0, p1, color);
        }
    }
}
