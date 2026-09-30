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

    // RigidBody is optional: FreeCam removes it
    ecs.each([](flecs::entity e, const Camera3d&, const vp::Transform& transform, const PlayerController& ctrl,
                const vp::CellCoord& gridCell) {
        constexpr float kMetersPerWorldUnit = 1.0f;
        const glm::vec3 positionMeters = transform.pos * kMetersPerWorldUnit;
        const glm::quat orientation = glm::normalize(transform.rot);
        const vp::LookAngles angles = e.has<vp::LookAngles>() ? *e.try_get<vp::LookAngles>() : vp::LookAngles{};
        const glm::vec3 up = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

        const vp::Grid* playerGrid = vp::get_first_ancestor_grid(e).try_get<vp::Grid>();
        const glm::dvec3 inGridPosition =
            playerGrid ? glm::dvec3(transform.pos) + (gridCell * playerGrid->cellSize) : glm::dvec3(transform.pos);

        ImGui::Text("Position: %.0lf, %.0lf, %.0lf", inGridPosition.x, inGridPosition.y, inGridPosition.z);
        ImGui::Text("Transform: %0.3f m, %0.3f m, %0.3f m", positionMeters.x, positionMeters.y, positionMeters.z);
        ImGui::Text("World Cell: %ld %ld %ld", gridCell.x, gridCell.y, gridCell.z);
        ImGui::Separator();
        ImGui::Text("Orientation:");
        ImGui::Text("  Yaw (body): %.2f°", angles.yaw);
        ImGui::Text("  Pitch (head): %.2f°", angles.pitch);
        ImGui::Text("  Roll (head): %.2f°", angles.roll);
        ImGui::Text("  Local up: %.3f, %.3f, %.3f", up.x, up.y, up.z);

        ImGui::Separator();
        ImGui::Text("Player:");
        ImGui::Text("  Mode: %s", ctrl.mode == ControllerMode::Walking ? "Walking" : "FreeCam");
        if (ctrl.mode == ControllerMode::FreeCam) {
            ImGui::Text("  Speed Multiplier: %.2fx", ctrl.freeCamSpeedMultiplier);
        }
        if (const auto* body = e.try_get<RigidBody>()) {
            ImGui::Text("  On Ground: %s", body->onGround ? "Yes" : "No");
        }

        const glm::quat look = orientation * glm::angleAxis(glm::radians(angles.pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 forward = look * glm::vec3(0.0f, 0.0f, -1.0f);

        ImGui::Separator();
        ImGui::Text("Looking: %.2f, %.2f, %.2f", forward.x, forward.y, forward.z);
    });
}
