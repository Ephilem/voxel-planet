#include "TeleportPanel.h"

#include "imgui.h"
#include "core/main_components.h"

void TeleportPanel::render(flecs::world& ecs) {
    ImGui::InputFloat3("Position", pos);

    if (ImGui::Button("Teleport")) {
        ecs.each([this](Position& position) {
            position.x = pos[0];
            position.y = pos[1];
            position.z = pos[2];
        });
    }
}
