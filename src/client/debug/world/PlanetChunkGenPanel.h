#pragma once
#include <array>
#include <cstdint>

#include "../IDebugPanel.h"

/**
 * Live view of the voxel chunk generation pipeline.
 *
 * The pipeline is a queue with three stages, and only the cumulative counters at each stage are
 * observable: request() fills m_pending, submit_pending() moves a slice of it to the workers, and
 * drain() brings the results back. A chunk stuck in any of them looks exactly like a chunk that
 * was never asked for, so what the panel really shows is the *difference* between the stages, ie
 * where work accumulates
 */
class PlanetChunkGenPanel : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Chunk Generation"; }

    const std::string category() const override { return "World"; }

private:
    /// Counters and health checks for one planet's generator. Only the primary planet feeds the
    /// panel's history and stall state, which are single buffers
    void render_generator_section(flecs::entity planet, bool primary);

    /// The loader lives on a child entity (the player), not on the planet, because it is the
    /// observer position that drives what gets requested
    void render_loader_section(flecs::world& ecs);

    /**
     * Rolling window of the pipeline, one entry per rendered frame.
     *
     * Rates alone cannot tell a saturated pipeline from an idle one: both sit at zero submissions
     * once the loader stops moving. The shape over time separates them, so the per frame counters
     * and the two queue depths are kept side by side, on the same time axis
     */
    struct History {
        static constexpr int CAPACITY = 240;

        std::array<float, CAPACITY> requested{};
        std::array<float, CAPACITY> submitted{};
        std::array<float, CAPACITY> completed{};
        /// Waiting for submit_pending(), ie the backlog the frame budget has not released yet
        std::array<float, CAPACITY> pending{};
        /// Handed to the workers and not drained back
        std::array<float, CAPACITY> inFlight{};

        int cursor = 0;
        int filled = 0;

        void push(float rq, float sb, float cp, float pd, float fl) {
            requested[cursor] = rq;
            submitted[cursor] = sb;
            completed[cursor] = cp;
            pending[cursor] = pd;
            inFlight[cursor] = fl;
            cursor = (cursor + 1) % CAPACITY;
            if (filled < CAPACITY)
                ++filled;
        }

        void clear() {
            requested.fill(0.f);
            submitted.fill(0.f);
            completed.fill(0.f);
            pending.fill(0.f);
            inFlight.fill(0.f);
            cursor = 0;
            filled = 0;
        }
    };

    History m_history;
    bool m_historyPaused = false;

    /// Cumulative completions at the previous frame, used to detect a pipeline that moved rather
    /// than one that merely has work in it
    uint32_t m_prevCompleted = 0;

    /**
     * Frames in a row with work in flight and nothing coming back.
     *
     * This is the discriminator between "the workers are busy" and "the results are never
     * collected". Generation of a chunk is a few milliseconds, so a stall measured in hundreds of
     * frames is not slowness, it is a stage of the pipeline that nobody is draining
     */
    uint32_t m_stallFrames = 0;
    uint32_t m_worstStallFrames = 0;

    /// Store size at the previous frame, so a store that never grows while chunks complete is
    /// visible as a delta pinned at zero
    size_t m_prevStoreSize = 0;

    /// Memoised sphere walk for the loader section. The count is the same cubic loop the loader
    /// runs, so recomputing it every frame would put the cost of a radius 32 refresh into the UI
    int m_cachedRadius = -1;
    int m_cachedAltitude = -1;
    uint32_t m_cachedCandidates = 0;
};
