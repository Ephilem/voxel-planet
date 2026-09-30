#pragma once

#include <glm/glm.hpp>

struct Velocity : glm::vec3 {
    using glm::vec3::vec3;

    Velocity& operator=(const glm::vec3& v) {
        this->x = v.x;
        this->y = v.y;
        this->z = v.z;
        return *this;
    }
};

struct RigidBody {
    glm::vec3 haftExtent{0.f};

    bool onGround = false;
    bool noClip = false;
};

struct Gravity : glm::vec3 {
    using glm::vec3::vec3;
};
