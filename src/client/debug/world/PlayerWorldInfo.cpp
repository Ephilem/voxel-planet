#include "PlayerWorldInfo.h"

#include <glm/gtc/quaternion.hpp>

#include "client/player/player_components.h"
#include "core/physics/physics_components.h"
#include "core/TracyIntegration.h"
#include "core/world/spatial/spatial_components.h"
#include "core/world/spatial/spatial_utils.h"
#include "imgui.h"
#include "renderer/rendering_components.h"

void PlayerWorldInfo::pre_render() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 25), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
}

void PlayerWorldInfo::render(flecs::world& ecs) {
    VOXEL_ZONE_N("WorldF3Info-Display");

    ecs.each([](flecs::entity e, const Camera3d& camera, const vp::Transform& transform, const RigidBody& body,
                const PlayerController& ctrl, const vp::CellCoord& gridCell) {
        constexpr float kMetersPerWorldUnit = 1.0f;
        const glm::vec3 positionMeters = transform.pos * kMetersPerWorldUnit;
        const glm::quat orientation = glm::normalize(transform.rot);
        const glm::vec3 orientationDeg = glm::degrees(glm::eulerAngles(orientation));

        const vp::Grid* playerGrid = vp::get_first_ancestor_grid(e).get<vp::Grid>();
        const glm::dvec3 inGridPosition =
            playerGrid ? glm::dvec3(transform.pos) + (gridCell * playerGrid->cellSize) : glm::dvec3(transform.pos);

        ImGui::Text("Position: %.0lf, %.0lf, %.0lf", inGridPosition.x, inGridPosition.y, inGridPosition.z);
        ImGui::Text("Transform: %0.3f m, %0.3f m, %0.3f m", positionMeters.x, positionMeters.y, positionMeters.z);
        ImGui::Text("World Cell: %ld %ld %ld", gridCell.x, gridCell.y, gridCell.z);
        ImGui::Separator();
        ImGui::Text("Orientation:");
        ImGui::Text("  Pitch: %.2f°", orientationDeg.x);
        ImGui::Text("  Yaw: %.2f°", orientationDeg.y);
        ImGui::Text("  Roll: %.2f°", orientationDeg.z);

        ImGui::Separator();
        ImGui::Text("Player:");
        ImGui::Text("  Mode: %s", ctrl.mode == ControllerMode::Walking ? "Walking" : "FreeCam");
        if (ctrl.mode == ControllerMode::FreeCam) {
            ImGui::Text("  Speed Multiplier: %.2fx", ctrl.freeCamSpeedMultiplier);
        }
        ImGui::Text("  On Ground: %s", body.onGround ? "Yes" : "No");

        const glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);

        ImGui::Separator();
        ImGui::Text("Looking: %.2f, %.2f, %.2f", forward.x, forward.y, forward.z);
    });
}
