#pragma once

#include <glm/glm.hpp>

struct Velocity : glm::vec3 { using glm::vec3::vec3; };

struct RigidBody : glm::vec3 {
    glm::vec3 haftExtent;

    bool onGround = false;
    bool noClip = false;
};

struct Gravity : glm::vec3 { using glm::vec3::vec3; };