#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <flecs.h>

#include "math/aabb.h"

struct DebugVertex {
    glm::vec3 position;
    glm::vec4 color;
};

class DebugDrawManager {
public:
    // Ecs connection
    static void Register(flecs::world& ecs);

    // Draw commands
    static void Line(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color);

    static void Aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color);
    static void Aabb(const AABB& aabb, const glm::vec4& color) { Aabb(aabb.min, aabb.max, color); }

    static void Point(const glm::vec3& position, const glm::vec4& color);
    static void Arrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color);

    // Retrieval buffer access (so the pass can upload the data to the GPU)
    static const std::vector<DebugVertex>& GetLines();
    static const std::vector<DebugVertex>& GetPoints();

private:
    static DebugDrawManager* instance;
    void init(flecs::world& ecs);
    static DebugDrawManager* GetInstance() {
        if (!instance) {
            instance = new DebugDrawManager();
        }
        return instance;
    }
    // Pairs of vertices for lines
    std::vector<DebugVertex> m_lines;
    std::vector<DebugVertex> m_points;
};
