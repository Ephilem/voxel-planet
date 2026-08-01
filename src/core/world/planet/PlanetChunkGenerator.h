#pragma once

#include <chrono>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

#include <flecs.h>

#include "planet_components.h"
#include "core/TracyIntegration.h"
#include "core/world/world_components.h"

namespace vp {
    /// Opaque token the caller uses to recognise its own job when the result comes back. The
    /// generator never looks inside it. See vp::PlanetLodJobs for the LOD side of the contract
    using ChunkGenJobId = uint64_t;

    struct ChunkGenInput {
        ChunkGenJobId jobId = 0;
        PlanetNodeCoord coord;
        glm::dvec3 sphereDir; // unused during the flat terrain phase
        PlanetGenerationConfig config;
        float priority = 0.0f;
    };

    struct ChunkGenOutput {
        ChunkGenJobId jobId = 0;
        PlanetNodeCoord coord;
        /// Unallocated by default: an empty node never touches it, and allocating one costs
        /// 64 KB plus the zeroing
        VoxelChunk chunk{VoxelChunk::Unallocated{}};
        /// Carried over from the input, so the meshing hop can keep the same ordering the
        /// generation used. Without it the terrain stops filling in from where the player looks
        float priority = 0.0f;
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
