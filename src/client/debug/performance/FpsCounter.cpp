#include "FpsCounter.h"

#include "imgui.h"
#include "core/GameState.h"
#include "core/TracyIntegration.h"

void FpsCounter::render(flecs::world& ecs) {
    VOXEL_ZONE_N("FpsCounter-Display");

    const auto* gameState = ecs.get<GameState>();
    if (!gameState) return;

    float fps = static_cast<float>(1.0 / gameState->deltaTime);
    m_currentFPS = fps;
    m_fpsHistory[m_fpsHistoryIndex] = fps;
    m_fpsHistoryIndex = (m_fpsHistoryIndex + 1) % m_fpsHistory.size();

    float avgFPS = 0.0f;
    for (float f : m_fpsHistory) avgFPS += f;
    avgFPS /= static_cast<float>(m_fpsHistory.size());

    ImVec4 color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    if (avgFPS < 58.0f) color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
    if (avgFPS < 30.0f) color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

    ImGui::TextColored(color, "Avg. FPS: %.0f", avgFPS);
    ImGui::Text("Fps: %.0f", m_currentFPS);
    ImGui::Text("Frame: %.2f ms", gameState->deltaTime * 1000.0f);

    ImGui::PlotLines("##fps_graph",
        m_fpsHistory.data(),
        static_cast<int>(m_fpsHistory.size()),
        static_cast<int>(m_fpsHistoryIndex),
        nullptr,
        0.0f,
        120.0f,
        ImVec2(200, 60)
    );
}
