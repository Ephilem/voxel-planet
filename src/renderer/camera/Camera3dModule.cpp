#include "Camera3dModule.h"

#include <GLFW/glfw3.h>

#include "camera3d_systems.h"
#include "core/TracyIntegration.h"
#include "core/physics/physics_components.h"
#include "platform/inputs/input_state.h"
#include "renderer/Renderer.h"
#include "renderer/rendering_components.h"

using namespace vp;

void Camera3dModule::register_components(flecs::world &ecs) {
    ecs.component<Camera3d>();
    ecs.component<Camera3dParameters>();
}

void Camera3dModule::register_systems(flecs::world &ecs) {
    ecs.system<Camera3dParameters>("ToggleCameraViewSystem")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, Camera3dParameters &parameters) {
            const auto* actions = e.world().get<InputActionState>();
            if (!actions || !actions->is_action_pressed(ActionInputType::ToggleCameraView)) return;
            parameters.viewType = (parameters.viewType == CameraViewType::FirstPerson)
                ? CameraViewType::ThirdPerson
                : CameraViewType::FirstPerson;
        });

    ecs.system<Camera3d, const Position, const Orientation, const Camera3dParameters>("UpdateCameraViewSystem")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, Camera3d &camera, const Position &position, const Orientation &orientation, const Camera3dParameters &parameters) {
            VOXEL_ZONE_N("Camera-UpdateView");
            glm::vec3 playerPos = position;
            glm::vec3 eyePos = position;
            auto type = parameters.viewType;
            if (type == CameraViewType::FirstPerson) {
                if (auto* body = e.get<RigidBody>()) {
                    eyePos.y += body->haftExtent.y * 0.75f;
                }
                systems::update_camera_view_system(camera, eyePos, orientation);
            } else if (type == CameraViewType::ThirdPerson) {
                float distance = 10.0f;
                float yawRad = glm::radians(orientation.yaw);
                float pitchRad = glm::radians(orientation.pitch);
                eyePos.x -= distance * cos(pitchRad) * sin(yawRad);
                eyePos.y -= distance * sin(pitchRad);
                eyePos.z -= distance * cos(pitchRad) * cos(yawRad);
                systems::update_camera_third_person_system(camera, eyePos, playerPos);
            }
        });

    ecs.observer<Camera3d, const Camera3dParameters>("UpdateCameraProjectionSystem")
        .event(flecs::OnSet)
        .each([](flecs::entity e, Camera3d &camera, const Camera3dParameters &parameters) {
            VOXEL_ZONE_N("Camera-UpdateProjection");
            const auto* renderer = e.world().get<Renderer>();
            if (renderer) {
                systems::update_camera_projection_system(camera, parameters, *renderer);
            }
        });
}
