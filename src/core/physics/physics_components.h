#pragma once

#include <glm/glm.hpp>

struct Velocity : glm::vec3 {
    using glm::vec3::vec3;

    Velocity& operator=( const glm::vec3& v ) {
        this->x = v.x;
        this->y = v.y;
        this->z = v.z;
        return *this;
    }
};

struct Movement {
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, 0.0f);
    float speed = 1.0f;
};

struct Acceleration : glm::vec3 { using glm::vec3::vec3; };

struct RigidBody : glm::vec3 {
    glm::vec3 haftExtent;

    bool onGround = false;
    bool noClip = false;
};

struct Gravity : glm::vec3 { using glm::vec3::vec3; };