#pragma once

#include <glm/glm.hpp>

struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    AABB()
        : min(glm::vec3(0.0f)), max(glm::vec3(0.0f)) {}

    AABB(const glm::vec3& min, const glm::vec3& max)
        : min(min), max(max) {}

    glm::vec3 center() const {
        return (min + max) * 0.5f;
    }

    glm::vec3 size() const {
        return max - min;
    }

    bool intersects(AABB& other) const {
        if (this->size() == glm::vec3(0.0f) || other.size() == glm::vec3(0.0f)) {
            return false; // Treat zero-size AABBs as non-intersecting
        }

        return (min.x < other.max.x && max.x > other.min.x) &&
               (min.y < other.max.y && max.y > other.min.y) &&
               (min.z < other.max.z && max.z > other.min.z);
    }

    static AABB from_chunk(const glm::ivec3& chunkPos, int chunkSize) {
        glm::vec3 min = glm::vec3(chunkPos) * static_cast<float>(chunkSize);
        glm::vec3 max = min + glm::vec3(chunkSize);
        return {min, max};
    }

    static AABB Zero() {
        return {glm::vec3(0.0f), glm::vec3(0.0f)};
    }

    AABB operator+(const glm::vec3& offset) const {
        return {min + offset, max + offset};
    }
};