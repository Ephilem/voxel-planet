#pragma once
#include <glm/fwd.hpp>

enum class ControllerMode : glm::uint8_t {
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
    float freeCamSpeed           = 20.0f;
    float freeCamFastMult        = 4.0f;
    float freeCamSpeedMultiplier = 1.0f;
};

