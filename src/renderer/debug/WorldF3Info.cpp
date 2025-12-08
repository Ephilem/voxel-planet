#include "WorldF3Info.h"
#include "imgui.h"
#include "../../core/main_components.h"
#include "../rendering_components.h"

void WorldF3Info::register_ecs(flecs::world &ecs) {
    ecs.system<const Camera3d, const Position, const Orientation>("WorldF3Info-DisplaySystem")
        .kind(flecs::PreStore)
        .each([this](flecs::iter& it, size_t, const Camera3d& camera, const Position& position, const Orientation& orientation) {
            if (!this->m_visible) return;

            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
                                           ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoNav |
                                           ImGuiWindowFlags_NoMove;

            const float PAD = 10.0f;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImVec2 work_pos = viewport->WorkPos;
            ImVec2 window_pos;
            window_pos.x = work_pos.x + viewport->WorkSize.x - PAD;
            window_pos.y = work_pos.y + PAD;
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.35f);

            if (ImGui::Begin("World F3 Info", nullptr, window_flags)) {
                ImGui::Text("Voxel Planet Debug Screen");
                ImGui::Separator();

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

                glm::vec3 forward = glm::normalize(glm::vec3(
                    camera.viewMatrix[0][2],
                    camera.viewMatrix[1][2],
                    camera.viewMatrix[2][2]
                )) * -1.0f;

                ImGui::Separator();
                ImGui::Text("Looking: %.2f, %.2f, %.2f", forward.x, forward.y, forward.z);
            }
            ImGui::End();
        });
}
