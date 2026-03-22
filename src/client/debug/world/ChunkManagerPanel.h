#pragma once
#include "../IDebugPanel.h"

#include <array>
#include <chrono>

#include "core/world/ChunkManager.h"

class ChunkManagerPanel : public IDebugPanel {
public:
    // void pre_render() override;
    void render(flecs::world &ecs) override;

    const std::string name() const override { return "Chunk Manager Debug"; }
    const std::string category() const override { return "World"; }

    void render_stats(flecs::world &ecs, const ChunkManager* cm);
    void render_slice_view(flecs::world &ecs, const ChunkManager* cm);
    void render_controls(flecs::world &ecs);

    // Slice view
    int m_sliceY = 0;
    int m_viewRadius = 8;

    static constexpr float SAMPLE_INTERVAL = 0.1f;
    static constexpr int   HISTORY_SIZE    = 600;

    std::chrono::steady_clock::time_point m_lastSampleTime = std::chrono::steady_clock::now();

    uint64_t m_lastTotalGenerated = 0;
    uint64_t m_accGenerated       = 0;
    int      m_accFrameCount      = 0;

    uint64_t m_lastTotalUnloaded  = 0;
    uint64_t m_accUnloaded        = 0;

    std::array<float, HISTORY_SIZE> m_cgsHistory{};
    int m_cgsHistoryIndex = 0;

    std::array<float, HISTORY_SIZE> m_cusHistory{};
    int m_cusHistoryIndex = 0;
};
