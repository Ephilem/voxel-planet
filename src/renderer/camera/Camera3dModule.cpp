#include "Camera3dModule.h"

#include <GLFW/glfw3.h>

#include "camera3d_systems.h"
#include "core/physics/physics_components.h"
#include "core/TracyIntegration.h"
#include "core/world/spatial/spatial_components.h"
#include "platform/inputs/input_state.h"
#include "renderer/Renderer.h"
#include "renderer/rendering_components.h"

using namespace vp;

void Camera3dModule::register_components(flecs::world& ecs) {
    ecs.component<Camera3d>();
    ecs.component<Camera3dParameters>();
}

void Camera3dModule::register_systems(flecs::world& ecs) {
    ecs.system<Camera3dParameters>("ToggleCameraViewSystem")
        .kind(flecs::OnUpdate)
        .each([](flecs::entity e, Camera3dParameters& parameters) {
            const auto* actions = e.world().try_get<InputActionState>();
            if (!actions || !actions->is_action_pressed(ActionInputType::ToggleCameraView))
                return;
            parameters.viewType = (parameters.viewType == CameraViewType::FirstPerson) ? CameraViewType::ThirdPerson
                                                                                       : CameraViewType::FirstPerson;
        });

    // PostUpdate: must run after PlanetModule-SurfaceAlign, which builds Transform.rot (body) this frame
    ecs.system<Camera3d, const Transform, const Camera3dParameters>("UpdateCameraViewSystem")
        .kind(flecs::PostUpdate)
        .each([](flecs::entity e, Camera3d& camera, const Transform& transform, const Camera3dParameters& parameters) {
            VOXEL_ZONE_N("Camera-UpdateView");

            // Recomputed every frame: the swapchain size changes on resize, and it is a few flops
            if (const auto* renderer = e.world().try_get<Renderer>(); renderer && renderer->backend) {
                systems::update_camera_projection_system(camera, parameters, *renderer);
            }

            glm::vec3 playerPos = glm::vec3(0.f);
            glm::vec3 eyePos = glm::vec3(0.f);
            auto type = parameters.viewType;

            // Transform.rot is the body rotation (local up aligned), the head pitch and roll are added on top of it
            const glm::quat body = glm::normalize(transform.rot);
            const glm::vec3 up = body * glm::vec3(0.f, 1.f, 0.f);
            const LookAngles angles = e.has<LookAngles>() ? *e.try_get<LookAngles>() : LookAngles{};
            const glm::quat orientation = body *
                                          glm::angleAxis(glm::radians(angles.pitch), glm::vec3(1.f, 0.f, 0.f)) *
                                          glm::angleAxis(glm::radians(angles.roll), glm::vec3(0.f, 0.f, -1.f));

            if (type == CameraViewType::FirstPerson) {
                if (auto* rigidBody = e.try_get<RigidBody>()) {
                    eyePos += up * (rigidBody->haftExtent.y * 0.75f);
                }
                systems::update_camera_view_system(camera, eyePos, orientation);
            } else if (type == CameraViewType::ThirdPerson) {
                float distance = 10.0f;
                glm::vec3 forward = glm::normalize(orientation * glm::vec3(0.f, 0.f, -1.f));

                eyePos = playerPos - forward * distance;
                systems::update_camera_third_person_system(camera, eyePos, playerPos, up);
            }
        });
}
