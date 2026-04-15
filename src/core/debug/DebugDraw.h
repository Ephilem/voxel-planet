#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "core/math/aabb.h"

namespace vp {
    struct DebugVertex;
    struct DebugDrawBuffer;
}

namespace vp::DebugDraw {
    void init(DebugDrawBuffer* buffer);
    void shutdown();

    void Line(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color);
    void Aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    void Point(const glm::vec3& pos, const glm::vec4& color);
    void Arrow(const glm::vec3& origin, const glm::vec3& dir, float len, const glm::vec4& color);
    void Sphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 16);
    void Circle(const glm::vec3& center, float radius, const glm::vec4& color, int segments = 16, const glm::vec3& normal = {0, 1, 0});

    inline void Aabb(const AABB& aabb, const glm::vec4& color) { Aabb(aabb.min, aabb.max, color); }

    const std::vector<DebugVertex>& GetLines();
    const std::vector<DebugVertex>& GetPoints();
}