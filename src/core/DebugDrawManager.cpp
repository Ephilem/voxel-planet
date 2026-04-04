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
            GetInstance()->m_points.clear();
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

void DebugDrawManager::Point(const glm::vec3& position, const glm::vec4& color) {
    auto instance = GetInstance();
    instance->m_points.push_back({position, color});
}

void DebugDrawManager::Arrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color) {
    glm::vec3 dir = glm::normalize(direction);
    glm::vec3 tip = origin + dir * length;

    Line(origin, tip, color);

    glm::vec3 up = glm::abs(glm::dot(dir, glm::vec3(0, 1, 0))) < 0.99f
                   ? glm::vec3(0, 1, 0)
                   : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(dir, up));
    up = glm::cross(right, dir);

    float headLen   = length * 0.2f;
    float headWidth = length * 0.1f;
    glm::vec3 base  = tip - dir * headLen;

    Line(tip, base + right * headWidth, color);
    Line(tip, base - right * headWidth, color);
    Line(tip, base + up    * headWidth, color);
    Line(tip, base - up    * headWidth, color);
}


const std::vector<DebugVertex>& DebugDrawManager::GetLines() {
    const auto instance = GetInstance();
    return instance->m_lines;
}

const std::vector<DebugVertex> & DebugDrawManager::GetPoints() {
    const auto instance = GetInstance();
    return instance->m_points;
}
