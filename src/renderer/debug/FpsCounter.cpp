//
// Created by raph on 24/11/25.
//

#include "FpsCounter.h"

#include "imgui.h"
#include "core/GameState.h"
#include "core/log/Logger.h"


void FpsCounter::register_ecs(flecs::world &ecs) {
     ecs.system<GameState>("FPSCounter-FPSUpdateSystem")
        .kind(flecs::OnUpdate)
        .each([this](flecs::iter& it, size_t, GameState& gameState) {
            float fps = static_cast<float>(1.0 / gameState.deltaTime);
            this->m_currentFPS = fps;

            this->m_fpsHistory[this->m_fpsHistoryIndex] = fps;
            this->m_fpsHistoryIndex = (this->m_fpsHistoryIndex + 1) % this->m_fpsHistory.size();
        });

    ecs.system<GameState>("FPSCounter-FPSDisplaySystem")
        .kind(flecs::PreStore) // Or wherever you render ImGui
        .each([this](flecs::iter& it, size_t, GameState& gameState) {
            if (!this->m_visible) return;

            ImGuiWindowFlags window_flags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoMove;

            const float PAD = 10.0f;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImVec2 work_pos = viewport->WorkPos;
            ImVec2 window_pos = ImVec2(
                work_pos.x + PAD,
                work_pos.y + PAD
            );
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);

            if (ImGui::Begin("FPS Counter", nullptr, window_flags)) {
                float fps = this->m_currentFPS;
                float frameTime = gameState.deltaTime * 1000.0f;

                // Color-code based on performance
                ImVec4 color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
                if (fps < 58.0f) color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
                if (fps < 30.0f) color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red

                ImGui::TextColored(color, "FPS: %.0f", fps);
                ImGui::Text("Frame: %.2f ms", frameTime);

                // Calculate average FPS
                float avgFPS = 0.0f;
                for (float f : this->m_fpsHistory) avgFPS += f;
                avgFPS /= this->m_fpsHistory.size();
                ImGui::Text("Avg: %.0f", avgFPS);

                // FPS Graph
                ImGui::PlotLines("##fps_graph",
                    this->m_fpsHistory.data(),
                    this->m_fpsHistory.size(),
                    this->m_fpsHistoryIndex,
                    nullptr,
                    0.0f,
                    120.0f,
                    ImVec2(200, 60)
                );
            }
            ImGui::End();
        });
}
