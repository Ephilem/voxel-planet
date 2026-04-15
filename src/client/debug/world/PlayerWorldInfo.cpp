#include "PlayerWorldInfo.h"

#include <glm/gtc/quaternion.hpp>

#include "imgui.h"
#include "client/player/player_components.h"
#include "renderer/rendering_components.h"
#include "core/TracyIntegration.h"
#include "core/physics/physics_components.h"
#include "core/world/spatial/spatial_components.h"

void PlayerWorldInfo::pre_render() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 25), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
}

void PlayerWorldInfo::render(flecs::world& ecs) {
    VOXEL_ZONE_N("WorldF3Info-Display");

    ecs.each([](flecs::entity e, const Camera3d& camera, const Transform& transform, const RigidBody& body, const PlayerController& ctrl, const CellCoord& gridCell) {
        constexpr float kMetersPerWorldUnit = 1.0f;
        const glm::vec3 positionMeters = transform.pos * kMetersPerWorldUnit;
        const glm::vec3 orientationDeg = transform.rot;

        const Grid* playerGrid = e.parent().get<Grid>();
        const glm::dvec3 inGridPosition = playerGrid
            ? glm::dvec3(transform.pos) + (gridCell * playerGrid->cellSize)
            : glm::dvec3(transform.pos);

        ImGui::Text("Position: %.0lf, %.0lf, %.0lf", inGridPosition.x, inGridPosition.y, inGridPosition.z);
        ImGui::Text("Transform: %0.3f m, %0.3f m, %0.3f m", positionMeters.x, positionMeters.y, positionMeters.z);
        ImGui::Text("World Cell: %ld %ld %ld", gridCell.x, gridCell.y, gridCell.z);
        ImGui::Separator();
        ImGui::Text("Orientation:");
        ImGui::Text("  Pitch: %.2f°", orientationDeg.x);
        ImGui::Text("  Yaw: %.2f°", orientationDeg.y);
        ImGui::Text("  Roll: %.2f°", orientationDeg.z);

        // ImGui::Separator();
        // ImGui::Text("Camera:");
        // ImGui::Text("  Near: %.2f", camera.nearClip);
        // ImGui::Text("  Far: %.2f", camera.farClip);
        // ImGui::Text("  Aspect: %.2f", camera.aspect_ratio);

        ImGui::Separator();
        ImGui::Text("Player:");
        ImGui::Text("  Mode: %s", ctrl.mode == ControllerMode::Walking ? "Walking" : "FreeCam");
        if (ctrl.mode == ControllerMode::FreeCam) {
            ImGui::Text("  Speed Multiplier: %.2fx", ctrl.freeCamSpeedMultiplier);
        }
        ImGui::Text("  On Ground: %s", body.onGround ? "Yes" : "No");

        glm::vec3 forward = glm::normalize(glm::vec3(
            camera.viewMatrix[0][2],
            camera.viewMatrix[1][2],
            camera.viewMatrix[2][2]
        )) * -1.0f;

        ImGui::Separator();
        ImGui::Text("Looking: %.2f, %.2f, %.2f", forward.x, forward.y, forward.z);
    });
}
