#include "Camera3dModule.h"

#include <GLFW/glfw3.h>
#include <glm/ext/matrix_transform.hpp>

#include "camera3d_systems.h"
#include "client/world/planet/planet_client_components.h"
#include "core/TracyIntegration.h"
#include "core/physics/physics_components.h"
#include "core/world/spatial/spatial_components.h"
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
                const auto *actions = e.world().get<InputActionState>();
                if (!actions || !actions->is_action_pressed(ActionInputType::ToggleCameraView)) return;
                parameters.viewType = (parameters.viewType == CameraViewType::FirstPerson)
                                          ? CameraViewType::ThirdPerson
                                          : CameraViewType::FirstPerson;
            });

    ecs.system<Camera3d, const Transform, const Camera3dParameters>("UpdateCameraViewSystem")
            .kind(flecs::OnUpdate)
            .each([](flecs::entity e, Camera3d &camera, const Transform &transform,
                     const Camera3dParameters &parameters) {
                         VOXEL_ZONE_N("Camera-UpdateView");
                glm::vec3 playerPos = glm::vec3(0.f);
                glm::vec3 eyePos = glm::vec3(0.f);
                auto type = parameters.viewType;

                glm::vec3 worldUp = glm::vec3(0.f, 1.f, 0.f);
                if (const auto *planetUp = e.get<PlanetUpVector>())
                    worldUp = planetUp->up;

                const glm::quat orientation = glm::normalize(transform.rot);

                if (type == CameraViewType::FirstPerson) {
                    if (auto *body = e.get<RigidBody>()) {
                        eyePos.y += body->haftExtent.y * 0.75f;
                    }
                    systems::update_camera_view_system(camera, eyePos, orientation, worldUp);
                } else if (type == CameraViewType::ThirdPerson) {
                    float distance = 10.0f;
                    glm::vec3 forward = glm::normalize(orientation * glm::vec3(0.f, 0.f, -1.f));

                    eyePos = playerPos - forward * distance;
                    systems::update_camera_third_person_system(camera, eyePos, playerPos);
                }
            });

    ecs.observer<Camera3d, const Camera3dParameters>("UpdateCameraProjectionSystem")
            .event(flecs::OnSet)
            .each([](flecs::entity e, Camera3d &camera, const Camera3dParameters &parameters) {
                VOXEL_ZONE_N("Camera-UpdateProjection");
                const auto *renderer = e.world().get<Renderer>();
                if (renderer) {
                    systems::update_camera_projection_system(camera, parameters, *renderer);
                }
            });
}
