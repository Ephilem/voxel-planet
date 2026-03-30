#include "DebugDrawManager.h"

DebugDrawManager* DebugDrawManager::instance = nullptr;

void DebugDrawManager::Register(flecs::world &ecs) {
    GetInstance()->init(ecs);
}

void DebugDrawManager::init(flecs::world &ecs) {
    ecs.system("DebugDrawManager-ClearBuffers")
        .kind(flecs::OnLoad)
        .run([](flecs::iter& it) {
            GetInstance()->m_lines.clear();
        });
}

void DebugDrawManager::Line(const glm::vec3 &start, const glm::vec3 &end, const glm::vec4 &color) {
    auto instance = GetInstance();
    instance->m_lines.push_back({start, color});
    instance->m_lines.push_back({end, color});
}

void DebugDrawManager::Aabb(const glm::vec3 &min, const glm::vec3 &max, const glm::vec4 &color) {
    glm::vec3 corners[8] = {
        {min.x, min.y, min.z},
        {max.x, min.y, min.z},
        {max.x, max.y, min.z},
        {min.x, max.y, min.z},
        {min.x, min.y, max.z},
        {max.x, min.y, max.z},
        {max.x, max.y, max.z},
        {min.x, max.y, max.z},
    };

    int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    for (int i = 0; i < 12; i++) {
        const glm::vec3& start = corners[edges[i][0]];
        const glm::vec3& end = corners[edges[i][1]];
        Line(start, end, color);
    }
}

const std::vector<DebugVertex>& DebugDrawManager::GetLines() {
    const auto instance = GetInstance();
    return instance->m_lines;
}
