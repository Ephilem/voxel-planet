#include "DebugDraw.h"
#include "DebugDrawModule.h"

namespace vp::DebugDraw {

static DebugDrawBuffer* s_ctx = nullptr;

void init(DebugDrawBuffer* buffer) { s_ctx = buffer; }
void shutdown()                    { s_ctx = nullptr; }

void Line(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) {
    assert(s_ctx && "DebugDraw::init() not called");
    s_ctx->lines.push_back({start, color});
    s_ctx->lines.push_back({end, color});
}

void Aabb(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color) {
    assert(s_ctx && "DebugDraw::init() not called");
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
        Line(corners[edges[i][0]], corners[edges[i][1]], color);
    }
}

void Point(const glm::vec3& position, const glm::vec4& color) {
    assert(s_ctx && "DebugDraw::init() not called");
    s_ctx->points.push_back({position, color});
}

void Arrow(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color) {
    assert(s_ctx && "DebugDraw::init() not called");
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

const std::vector<vp::DebugVertex>& GetLines() {
    return s_ctx->lines;
}

const std::vector<DebugVertex>& GetPoints() {
    return s_ctx->points;
}

}