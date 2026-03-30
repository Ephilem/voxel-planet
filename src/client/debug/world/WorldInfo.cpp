#include "WorldInfo.h"

#include "imgui.h"
#include "core/main_components.h"
#include "renderer/rendering_components.h"
#include "core/TracyIntegration.h"
#include "core/physics/physics_components.h"

void WorldInfo::pre_render() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 25), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);
}

void WorldInfo::render(flecs::world& ecs) {
    VOXEL_ZONE_N("WorldF3Info-Display");

    ecs.each([](const Camera3d& camera, const Position& position, const Orientation& orientation, const RigidBody& body) {
        ImGui::Text("Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
        ImGui::Text("Orientation:");
        ImGui::Text("  Pitch: %.2f°", orientation.pitch);
        ImGui::Text("  Yaw: %.2f°", orientation.yaw);
        ImGui::Text("  Roll: %.2f°", orientation.roll);

        ImGui::Separator();
        ImGui::Text("Camera:");
        ImGui::Text("  Near: %.2f", camera.nearClip);
        ImGui::Text("  Far: %.2f", camera.farClip);
        ImGui::Text("  Aspect: %.2f", camera.aspect_ratio);

        ImGui::Separator();
        ImGui::Text("Player:");
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
