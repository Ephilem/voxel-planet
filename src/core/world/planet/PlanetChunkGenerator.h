#pragma once

#include <flecs.h>

#include "planet_components.h"
#include "core/TracyIntegration.h"
#include "core/world/world_components.h"

namespace vp {
    struct ChunkGenInput {
        flecs::entity chunkEntity;
        SurfaceChunkCoord coord;
        glm::dvec3 sphereDir;
        PlanetGenerationConfig config;
        float priority = 0.0f;
    };

    struct ChunkGenOutput {
        flecs::entity chunkEntity;
        SurfaceChunkCoord coord;
        VoxelChunk chunk;
        bool success = false;
        bool empty = true;
    };

    class PlanetChunkGenerator {
    public:
        PlanetChunkGenerator();
        ~PlanetChunkGenerator();

        void enqueue(const ChunkGenInput& input);
        void enqueues(const ChunkGenInput* inputs, size_t count);

        std::vector<ChunkGenOutput> poll_results(size_t max = 0);
    private:
        static constexpr std::chrono::microseconds DRAIN_TIME_BUDGET{500};
        static constexpr int MAX_UNLOADS_PER_FRAME = 50;
        static constexpr int MAX_GENERATION_RESULTS_PER_FRAME = 100;
        static constexpr size_t GENERATION_BATCH_SIZE = 4;

        // --- Generation threads | Enqueue and inputs ---
        std::vector<std::thread> m_generationThreads;
        VOXEL_LOCKABLE_N(std::mutex, m_enqueueGenerationMutex, "EnqueueGen");
        std::counting_semaphore<> m_generationSemaphore{0};
        std::vector<ChunkGenInput> m_generationQueue; // min-heap
        std::atomic<bool> m_stopGeneration{false};

        // --- Generation Thread | Results ---
        struct alignas(64) GenerationWorkerResult {
            VOXEL_LOCKABLE(std::mutex, m_resultMutex);
            std::vector<ChunkGenOutput> results;
            std::atomic<int> pendingCount{0};
        };
        std::vector<std::unique_ptr<GenerationWorkerResult>> m_generationWorkerResults;

        void worker_loop(size_t workerId);
    };

}
