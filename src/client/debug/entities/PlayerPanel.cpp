//
// Created by raph on 22/03/2026.
//

#include "PlayerPanel.h"

#include "core/main_components.h"
#include "core/world/spatial/spatial_components.h"
#include "imgui.h"
#include "client/render/camera/camera3d_components.h"

namespace vp::client {
void PlayerPanel::render(flecs::world& ecs) {
    ImGui::Separator();
    ImGui::InputFloat3("Position", m_teleportPos);

    if (ImGui::Button("Teleport")) {
        ecs.each([this](const Camera3d&, core::Transform& transform, const core::Player&) {
            auto& pos = transform.pos;
            pos.x = m_teleportPos[0];
            pos.y = m_teleportPos[1];
            pos.z = m_teleportPos[2];
        });
    }
}
} // namespace vp::client
