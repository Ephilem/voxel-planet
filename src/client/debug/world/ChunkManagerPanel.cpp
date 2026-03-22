#include "ChunkManagerPanel.h"

#include "imgui.h"
#include "implot.h"
#include "core/TracyIntegration.h"
#include "core/world/ChunkManager.h"

static ImPlotColormap chunkColormap = -1;

void ChunkManagerPanel::render(flecs::world &ecs) {
    VOXEL_ZONE_N("ChunkManagerPanel-Display");
    const auto* cm = ecs.get<ChunkManager>();
    if (!cm) return;

    if (ImGui::BeginTable("##layout", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextColumn();
        render_stats(ecs, cm);
        ImGui::TableNextColumn();
        render_slice_view(ecs, cm);
        ImGui::EndTable();
    }

    ImGui::Separator();
    render_controls(ecs);
}

void ChunkManagerPanel::render_stats(flecs::world &ecs, const ChunkManager* cm) {
    const ChunkManagerStats stats = cm->get_stats();

    m_accGenerated += stats.chunksGenerated - m_lastTotalGenerated;
    m_accUnloaded += stats.chunksUnloaded - m_lastTotalUnloaded;
    m_lastTotalGenerated = stats.chunksGenerated;
    m_lastTotalUnloaded = stats.chunksUnloaded;
    m_accFrameCount++;

    const auto now = std::chrono::steady_clock::now();
    const float elapsed = std::chrono::duration<float>(now - m_lastSampleTime).count();
    float lastAvgGenerated = 0.0f;
    float lastAvgUnloaded = 0.0f;
    if (elapsed >= SAMPLE_INTERVAL && m_accFrameCount > 0) {
        lastAvgGenerated = static_cast<float>(m_accGenerated) / m_accFrameCount;
        lastAvgUnloaded = static_cast<float>(m_accUnloaded) / m_accFrameCount;

        m_cgsHistory[m_cgsHistoryIndex] = lastAvgGenerated;
        m_cgsHistoryIndex = (m_cgsHistoryIndex + 1) % HISTORY_SIZE;

        m_cusHistory[m_cusHistoryIndex] = lastAvgUnloaded;
        m_cusHistoryIndex = (m_cusHistoryIndex + 1) % HISTORY_SIZE;

        m_accGenerated = 0;
        m_accUnloaded = 0;
        m_accFrameCount = 0;
        m_lastSampleTime = now;
    }

    ImGui::Text("Chunks generated/frame (avg): %.2f",
                m_cgsHistory[(m_cgsHistoryIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE]);
    if (ImPlot::BeginPlot("##cgf", ImVec2(500, 160))) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 50.0, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, HISTORY_SIZE, ImGuiCond_Always);
        ImPlot::PlotLine("gen/frame", m_cgsHistory.data(), HISTORY_SIZE, 1.0, 0.0, 0, m_cgsHistoryIndex);
        ImPlot::EndPlot();
    }
    ImGui::Text("Chunks unloaded/frame (avg): %.2f",
                m_cusHistory[(m_cusHistoryIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE]);
    if (ImPlot::BeginPlot("##cuf", ImVec2(500, 160))) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 50.0, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, HISTORY_SIZE, ImGuiCond_Always);
        ImPlot::PlotLine("unload/frame", m_cusHistory.data(), HISTORY_SIZE, 1.0, 0.0, 0, m_cusHistoryIndex);
        ImPlot::EndPlot();
    }

    ImGui::Separator();

    ImGui::TextColored({0.3f, 1.0f, 0.3f, 1.0f}, "Loaded:    %zu", stats.loadedChunkCount);
    ImGui::TextColored({0.5f, 0.5f, 0.5f, 1.0f}, "Empty:     %zu", stats.emptyChunkCount);
    ImGui::TextColored({1.0f, 0.8f, 0.0f, 1.0f}, "Loading:   %zu", stats.loadingChunkCount);
    ImGui::TextColored({0.3f, 0.6f, 1.0f, 1.0f}, "Candidate: %zu", stats.candidateChunkCount);
    ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Cancelled: %zu", stats.cancelledChunkCount);
}

void ChunkManagerPanel::render_slice_view(flecs::world &ecs, const ChunkManager* cm) {
    if (chunkColormap == -1) {
        const ImVec4 colors[] = {
            {0.12f, 0.12f, 0.12f, 1.0f},
            {0.2f, 0.9f, 0.3f, 1.0f},
            {0.45f, 0.45f, 0.45f, 1.0f},
            {1.0f, 0.8f, 0.0f, 1.0f},
            {0.3f, 0.6f, 1.0f, 1.0f},
            {1.0f, 0.3f, 0.3f, 1.0f},
        };
        chunkColormap = ImPlot::AddColormap("ChunkStates", colors, 6);
    }

    const int dim = 2 * m_viewRadius + 1;
    std::vector<float> grid(dim * dim, 0.0f);

    glm::ivec3 center = cm->get_current_center();
    center.y += m_sliceY;

    for (int z = 0; z < dim; z++)
        for (int x = 0; x < dim; x++) {
            glm::ivec3 pos = center + glm::ivec3(x - m_viewRadius, 0, z - m_viewRadius);
            grid[z * dim + x] = static_cast<float>(cm->get_chunk_state(pos));
        }

    ImPlot::PushColormap(chunkColormap);
    if (ImPlot::BeginPlot("##slice", ImVec2(320, 320),
                          ImPlotFlags_Equal | ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxes("X", "Z",
                          ImPlotAxisFlags_NoDecorations,
                          ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_Invert);

        const double r = m_viewRadius + 0.5;
        ImPlot::SetupAxisLimits(ImAxis_X1, -r, r, ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -r, r, ImGuiCond_Always);

        ImPlot::PlotHeatmap("##chunks", grid.data(), dim, dim,
                            0.0, 5.0, nullptr,
                            ImPlotPoint(-r, -r),
                            ImPlotPoint(r, r));

        double cx = 0.0, cy = 0.0;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 8.0f, ImVec4(1, 1, 1, 1), 2.0f);
        ImPlot::PlotScatter("##player", &cx, &cy, 1);

        ImPlot::EndPlot();
    }
    ImPlot::PopColormap();
    ImGui::Spacing();

    auto dot = [](ImVec4 c, const char* label) {
        ImGui::ColorButton("##", c, ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
        ImGui::SameLine();
        ImGui::Text("%s", label);
    };

    dot({0.2f, 0.9f, 0.3f, 1}, "Loaded");
    ImGui::SameLine(0, 16);
    dot({1.0f, 0.8f, 0.0f, 1}, "Loading");
    ImGui::SameLine(0, 16);
    dot({0.3f, 0.6f, 1.0f, 1}, "Candidate");
    dot({0.45f, 0.45f, 0.45f, 1}, "Empty");
    ImGui::SameLine(0, 16);
    dot({1.0f, 0.3f, 0.3f, 1}, "Cancelled");
}

void ChunkManagerPanel::render_controls(flecs::world &ecs) {
}
