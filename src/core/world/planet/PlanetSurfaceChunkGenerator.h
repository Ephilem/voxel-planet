#pragma once
#include <blockingconcurrentqueue.h>
#include <concurrentqueue.h>
#include <flecs.h>
#include <thread>
#include <unordered_set>
#include <vector>

#include "generator/PlanetTerrainSampler.h"
#include "planet_components.h"
#include "planet_types.h"

namespace vp {
class PlanetSurfaceChunkGenerator {
public:
    PlanetSurfaceChunkGenerator(PlanetTerrainParams params, double planetRadius, uint8_t maxLevel,
                                unsigned workerCount = 0);
    ~PlanetSurfaceChunkGenerator();

    PlanetSurfaceChunkGenerator(const PlanetSurfaceChunkGenerator&) = delete;
    PlanetSurfaceChunkGenerator& operator=(const PlanetSurfaceChunkGenerator&) = delete;

    struct Result {
        PlanetSurfaceChunkKey key;
        std::shared_ptr<PlanetSurfaceVoxelChunk> chunk; // nullptr = empty
    };

    void request(const PlanetSurfaceChunkKey& key, float priority = 0.f);

    /**
     * Each by frame, submit a limited number of pending requests to the worker threads
     * @param maxSubmit Maximum number of requests to submit. The rest will be kept for the next frame
     */
    void submit_pending(uint32_t maxSubmit = 32);

    uint32_t drain(std::vector<Result>& out, uint32_t maxDrain = 16);

    void begin_frame();

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

private:
    void worker_loop(std::stop_token stop);
    void generate(const PlanetSurfaceChunkKey& key, PlanetSurfaceVoxelChunk& out, const PlanetTerrainSampler& sampler,
                  PlanetTerrainSampler::BatchScratch& scratch) const;

    PlanetTerrainParams m_params;
    double m_planetRadius;
    uint8_t m_maxLevel;

    moodycamel::BlockingConcurrentQueue<PlanetSurfaceChunkKey> m_requestQueue;
    moodycamel::ConcurrentQueue<Result> m_resultQueue;

    struct Pending {
        PlanetSurfaceChunkKey key;
        float priority;
    };

    std::vector<Pending> m_pending;
    std::unordered_set<PlanetSurfaceChunkKey> m_pendingSet;
    std::unordered_set<PlanetSurfaceChunkKey> m_inFlight;
    std::vector<PlanetSurfaceChunkKey> m_keyScratch;

    std::vector<std::jthread> m_workers;
    Stats m_stats;
};

} // namespace vp
