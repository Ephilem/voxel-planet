#pragma once

#include <blockingconcurrentqueue.h>
#include <concurrentqueue.h>
#include <unordered_set>

#include "core/world/planet/generator/PlanetTerrainSampler.h"
#include "planet_rendering_types.h"

namespace vp {
/**
 * Generates tile heightmaps on worker threads
 */
class PlanetTileGenerator {
public:
    explicit PlanetTileGenerator(PlanetTerrainParams params, uint16_t resolution, double planetRadius,
                                 unsigned workerCount = 0);
    ~PlanetTileGenerator();

    PlanetTileGenerator(const PlanetTileGenerator&) = delete;
    PlanetTileGenerator& operator=(const PlanetTileGenerator&) = delete;

    struct PlanetTileResult {
        PlanetTileKey key;
        PlanetTileData data;
    };

    /**
     * Request. Need to call submit to send a part
     * @param priority lower is generated first, when the frame budget forces a choice.
     *        Camera distance in meters is the intended value
     */
    void request(const PlanetTileKey& key, float priority = 0.f);

    /// Call once per frame, after every request() of that frame.
    void submit_pending(uint32_t maxSubmit = 64);
    uint32_t drain(std::vector<PlanetTileResult>& out, uint32_t maxDrain = 32);

    [[nodiscard]] size_t in_flight() const { return m_inFlight.size(); }

    [[nodiscard]] size_t pending() const { return m_pending.size(); }

    [[nodiscard]] size_t worker_count() const { return m_workers.size(); }

    struct Stats {
        uint32_t requested = 0;    // cumulative, deduplicated requests accepted into m_pending
        uint32_t submitted = 0;    // cumulative, handed to the workers
        uint32_t completed = 0;    // cumulative, drained back on the main thread
        uint32_t dedupRejects = 0; // cumulative, request() calls dropped as already known

        uint32_t requestedThisFrame = 0;
        uint32_t submittedThisFrame = 0;
        uint32_t completedThisFrame = 0;

        uint32_t peakPending = 0;
        uint32_t peakInFlight = 0;
    };

    [[nodiscard]] const Stats& stats() const { return m_stats; }

    /// Resets the per-frame counters. Call once per frame, before the first request()
    void begin_frame();

private:
    void worker_loop(std::stop_token stop);
    void generate(const PlanetTileKey& key, PlanetTileData& out, const PlanetTerrainSampler& sampler,
                  PlanetTerrainSampler::BatchScratch& scratch) const;

    PlanetTerrainParams m_params;
    uint16_t m_resolution;
    double m_planetRadius;

    moodycamel::BlockingConcurrentQueue<PlanetTileKey> m_requestQueue;
    moodycamel::ConcurrentQueue<PlanetTileResult> m_resultQueue;

    struct PendingTile {
        PlanetTileKey key;
        float priority = 0.f;
    };

    // Main thread only, no synchronization needed
    std::vector<PendingTile> m_pending;             // waiting for submit
    std::unordered_set<PlanetTileKey> m_pendingSet; // mirrors m_pending, for O(1) dedup
    std::unordered_set<PlanetTileKey> m_inFlight;   // being treated

    // enqueue_bulk needs contiguous keys, and m_pending carries priorities alongside
    std::vector<PlanetTileKey> m_keyScratch;

    std::vector<std::jthread> m_workers;

    Stats m_stats;
};
} // namespace vp
