#pragma once

#include <glm/glm.hpp>

#include "core/math/aabb.h"

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

enum class ControllerMode : uint8_t {
    Walking,
    FreeCam,
};

struct PlayerController {
    ControllerMode mode = ControllerMode::FreeCam;

    // Walking
    float walkSpeed     = 5.0f;
    float sprintSpeed   = 12.0f;
    float jumpForce     = 6.0f;
    float groundAccel   = 30.0f;
    float airAccel      = 5.0f;
    float groundFriction = 50.0f;

    // FreeCam
    float freeCamSpeed       = 20.0f;
    float freeCamFastMult    = 4.0f;
};

struct Acceleration : glm::vec3 { using glm::vec3::vec3; };

struct RigidBody : glm::vec3 {
    glm::vec3 haftExtent;

    bool onGround = false;
    bool noClip = false;

    AABB box() const {
        return AABB{-haftExtent * glm::vec3(2), haftExtent * glm::vec3(2)};
    }
};

struct Gravity : glm::vec3 { using glm::vec3::vec3; };