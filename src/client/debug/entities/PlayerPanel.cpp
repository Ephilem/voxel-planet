//
// Created by raph on 22/03/2026.
//

#include "PlayerPanel.h"

#include "imgui.h"
#include "core/main_components.h"
#include "renderer/rendering_components.h"

void PlayerPanel::render(flecs::world &ecs) {
    ImGui::Separator();
    ImGui::InputFloat3("Position", m_teleportPos);

    if (ImGui::Button("Teleport")) {
        ecs.each([this](const Camera3d&, Position& position) {
            position.x = m_teleportPos[0];
            position.y = m_teleportPos[1];
            position.z = m_teleportPos[2];
        });
    }
}
