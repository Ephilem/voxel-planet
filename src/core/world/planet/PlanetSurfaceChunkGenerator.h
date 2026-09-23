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
    PlanetSurfaceChunkGenerator(PlanetTerrainParams params, double planetRadius,
                                flecs::ref<const PlanetVoxelRegistry> registry, unsigned workerCount = 0);
    ~PlanetSurfaceChunkGenerator();

    PlanetSurfaceChunkGenerator(const PlanetSurfaceChunkGenerator&) = delete;
    PlanetSurfaceChunkGenerator& operator=(const PlanetSurfaceChunkGenerator&) = delete;

    struct PlanetSurfaceChunkGenerationResult {
        PlanetSurfaceChunkKey key;
        std::shared_ptr<PlanetSurfaceVoxelChunk> chunk; // nullptr = empty
    };

    void request(const PlanetSurfaceChunkKey& key, float priority = 0.F);

    void cancel(const PlanetSurfaceChunkKey& key);

    template <class Fn> void reprioritize(Fn&& priorityCalculationMethod) {
        std::erase_if(m_pending, [&](Pending& p) {
            const std::optional<float> prio = priorityCalculationMethod(p.key);
            if (!prio) {
                m_pendingSet.erase(p.key);
                return true;
            }
            p.priority = *prio;
            return false;
        });

        for (const auto& k : m_inFlight) {
            if (!priorityCalculationMethod(k).has_value()) {
                m_cancelled.insert(k);
            }
        }
    }

    /**
     * Each by frame, submit a limited number of pending requests to the worker threads
     * @param maxSubmit Maximum number of requests to submit. The rest will be kept for the next frame
     */
    void submit_pending(uint32_t maxInFlight = 32);

    uint32_t drain(std::vector<PlanetSurfaceChunkGenerationResult>& out, uint32_t maxDrain = 16);

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

    [[nodiscard]] size_t pending_count() const { return m_pending.size(); }

    [[nodiscard]] size_t in_flight_count() const { return m_inFlight.size(); }

private:
    void worker_loop(std::stop_token stop);
    void generate(const PlanetSurfaceChunkKey& key, PlanetSurfaceVoxelChunk& out, const PlanetTerrainSampler& sampler,
                  PlanetTerrainSampler::BatchScratch& scratch) const;

    PlanetTerrainParams m_params;
    double m_planetRadius;
    mutable flecs::ref<const PlanetVoxelRegistry>
        m_registry; // flecs::ref because it can be reallocated. flecs::ref prevent that problem

    moodycamel::BlockingConcurrentQueue<PlanetSurfaceChunkKey> m_requestQueue;
    moodycamel::ConcurrentQueue<PlanetSurfaceChunkGenerationResult> m_resultQueue;

    struct Pending {
        PlanetSurfaceChunkKey key;
        float priority;
    };

    std::vector<Pending> m_pending;
    std::unordered_set<PlanetSurfaceChunkKey> m_cancelled;
    std::unordered_set<PlanetSurfaceChunkKey> m_pendingSet;
    std::unordered_set<PlanetSurfaceChunkKey> m_inFlight;
    std::vector<PlanetSurfaceChunkKey> m_keyScratch;

    std::vector<std::jthread> m_workers;
    Stats m_stats;
};

} // namespace vp
