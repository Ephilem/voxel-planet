#pragma once

#include <glm/glm.hpp>

#include "core/math/aabb.h"

struct Velocity : glm::vec3 {
    using glm::vec3::vec3;

    Velocity& operator=(const glm::vec3& v) {
        this->x = v.x;
        this->y = v.y;
        this->z = v.z;
        return *this;
    }
};

struct Acceleration : glm::vec3 {
    using glm::vec3::vec3;
};

struct RigidBody : glm::vec3 {
    glm::vec3 haftExtent;

    bool onGround = false;
    bool noClip = false;

    AABB box() const { return AABB{-haftExtent * glm::vec3(2), haftExtent * glm::vec3(2)}; }
};

struct Gravity : glm::vec3 {
    using glm::vec3::vec3;
};