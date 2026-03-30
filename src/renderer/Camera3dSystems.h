#pragma once

#include <flecs.h>

#include "Renderer.h"
#include "rendering_components.h"
#include "core/main_components.h"

class Camera3dSystems {
public:
    // Platform state is needed to get the as
    static void Register(flecs::world& ecs);

    static void update_camera_view_system(Camera3d& camera, const glm::vec3& position, const Orientation& orientation);
    static void update_camera_third_person_system(Camera3d& camera, const glm::vec3& cameraPos, const glm::vec3& target);
    static void update_camera_projection_system(Camera3d& camera, const Camera3dParameters &parameters, const Renderer &renderer);
};
