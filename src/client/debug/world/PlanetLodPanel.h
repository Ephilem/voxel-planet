#pragma once
#include <array>
#include <cstdint>

#include "../IDebugPanel.h"

/**
 * Tuning and live stats for the planet quadtree LOD.
 *
 * The tree state it shows is the one the renderer never sees: nodes waiting on a
 * generation, or holding no slot at all, are invisible in the final image and in the
 * wireframe alike, which is what makes a stalled refinement so hard to read
 */
class PlanetLodPanel : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Planet LOD"; }

    const std::string category() const override { return "World"; }

private:
    /// Atlas residency and slot resolution. Both are global, not per planet, so they are drawn
    /// once after the per-planet sections
    void render_atlas_section(flecs::world& ecs);

    /**
     * Rolling window of slot resolution, one entry per rendered frame.
     *
     * The instantaneous ratios cannot tell a healthy atlas from one that re-resolves a
     * different set of tiles every frame: both read the same on a single frame. What separates
     * them is the shape over time, so the counters are kept per frame and drawn as a plot,
     * with the frames where the tree split or merged marked on top
     */
    struct SlotHistory {
        static constexpr int CAPACITY = 240;

        std::array<float, CAPACITY> exact{};
        std::array<float, CAPACITY> fallback{};
        std::array<float, CAPACITY> missing{};
        std::array<float, CAPACITY> uploads{};
        /// Leaves added or removed since the previous sample, ie how much the tree moved
        std::array<float, CAPACITY> topologyDelta{};

        int cursor = 0;
        int filled = 0;

        void push(float e, float f, float m, float u, float d) {
            exact[cursor] = e;
            fallback[cursor] = f;
            missing[cursor] = m;
            uploads[cursor] = u;
            topologyDelta[cursor] = d;
            cursor = (cursor + 1) % CAPACITY;
            if (filled < CAPACITY)
                ++filled;
        }

        void clear() {
            exact.fill(0.f);
            fallback.fill(0.f);
            missing.fill(0.f);
            uploads.fill(0.f);
            topologyDelta.fill(0.f);
            cursor = 0;
            filled = 0;
        }
    };

    SlotHistory m_history;
    bool m_historyPaused = false;

    // Previous frame's raw counters, used to detect a change rather than a level
    uint32_t m_prevExact = 0;
    uint32_t m_prevFallback = 0;
    uint32_t m_prevMissing = 0;
    uint32_t m_prevLeafCount = 0;
    uint32_t m_prevEvictions = 0;

    /// Leaves summed over every planet, captured in render() so the atlas section, which runs
    /// after the per planet loop and has no quadtree of its own, can correlate against it
    uint32_t m_lastLeafCount = 0;

    /**
     * Worst churn seen while the tree was quiet, ie with no split and no merge.
     *
     * This is the discriminator for premature eviction. A tile whose slice is dropped while it
     * is still in the draw list has to fall back to an ancestor even though nothing about the
     * subdivision changed, so any large value here is resolution moving on its own
     */
    uint32_t m_worstQuietSwing = 0;
    uint32_t m_quietEvictions = 0;
    uint32_t m_quietFrames = 0;
};
