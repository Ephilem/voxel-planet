#pragma once
#include <deque>
#include <semaphore>
#include <unordered_set>
#include <flecs.h>
#include <vector>

#include "world_components.h"
#include "core/main_components.h"
#include "core/TracyIntegration.h"

class WorldGenerator;



struct ChunkCandidate {
    glm::ivec3 pos;
    float priority;
};

struct TaskGeneratingInput {
    glm::ivec3 chunkCoord;
    WorldGenerator* generator;
    float priority = 0.0f;
};

struct TaskGeneratingOutput {
    std::shared_ptr<std::array<uint8_t, (32 * 32 * 32)>> voxels;
    std::unordered_map<AssetID, uint8_t> textureIDs;
    glm::ivec3 chunkCoord;
    bool success = false;
    bool empty = true;
};

// alignas(64) to avoid false sharing between worker threads and main thread when pushing/polling results
struct alignas(64) GenerationWorkerResult {
    VOXEL_LOCKABLE(std::mutex, m_resultMutex);
    std::vector<TaskGeneratingOutput> results;
    std::atomic<int> pendingCount{0};
};

/**
 * Class with the responsibility to manage chunk loading, unloading, and overall chunk lifecycle.
 */
class ChunkManager {
public:
    ChunkManager() = default;
    ~ChunkManager();

    void init(flecs::world& ecs);
    static void Register(flecs::world& ecs);

    // Public API
    std::array<flecs::entity, 6> get_neighboring_chunks(const glm::ivec3 &chunkPos) const;
    flecs::entity get_chunk_entity(const glm::ivec3& chunkPos) const;
    bool can_mesh(const glm::ivec3& chunkPos) const;

private:
    std::deque<glm::ivec3> m_unloadQueue;
    std::unordered_set<glm::ivec3, IVec3Hash> m_unloadQueueSet;

    std::unordered_map<glm::ivec3, flecs::entity, IVec3Hash> m_loadedChunks;
    std::unordered_set<glm::ivec3, IVec3Hash> m_emptyChunks;
    std::unordered_set<glm::ivec3, IVec3Hash> m_loadingChunks;

    std::unordered_set<glm::ivec3, IVec3Hash> m_cancelledChunks;

    static constexpr int MAX_CHUNKS_PER_FRAME = 50;
    static constexpr int MAX_UNLOADS_PER_FRAME = 50;
    static constexpr int MAX_GENERATION_RESULTS_PER_FRAME = 100;
    static constexpr size_t GENERATION_BATCH_SIZE = 4;

    void shutdown();

    // ECS Systems
    void update_chunks_system(flecs::entity e, ChunkLoader& loader, const Position& position, WorldGenerator* generator);
    void poll_generation_results_system(flecs::iter& it);
    void process_unload_queue_system(flecs::iter& it);

    // Actions
    void request_chunks_in_radius(const glm::ivec3& center, const glm::ivec3& oldCenter, int radius, WorldGenerator* generator);
    void enqueue_chunks_generation(const std::vector<ChunkCandidate>& candidates, WorldGenerator* generator);
    void cancel_chunk_generation(const glm::ivec3& chunkPos);
    void update_unload_queue(const ChunkLoader& loader, const glm::ivec3& centerChunk);

    std::vector<TaskGeneratingOutput> poll_generation_results(size_t maxResults);

    // Helpers
    static glm::ivec3 world_pos_to_chunk_pos(const glm::vec3& worldPos) {
        return {
            static_cast<int>(floor(worldPos.x / CHUNK_SIZE)),
            static_cast<int>(floor(worldPos.y / CHUNK_SIZE)),
            static_cast<int>(floor(worldPos.z / CHUNK_SIZE))
        };
    }

    bool is_chunk_processed(const glm::ivec3& pos) const {
        return m_loadedChunks.contains(pos) || m_emptyChunks.contains(pos);
    }

    bool is_chunk_in_progress(const glm::ivec3& pos) const {
        return m_loadingChunks.contains(pos);
    }

    bool is_chunk_cancelled(const glm::ivec3& pos) const {
        return m_cancelledChunks.contains(pos);
    }

    static float calculate_priority(const glm::ivec3& chunkPos, const glm::ivec3& center);
    bool is_chunk_still_needed(const glm::ivec3& chunkPos, const flecs::world &world) const;

    // Generation threads
    std::vector<std::thread> m_generationThreads;
    VOXEL_LOCKABLE(std::mutex, m_generationMutex);
    std::counting_semaphore<> m_generationSemaphore{0};
    std::vector<TaskGeneratingInput> m_generationQueue; // min-heap managed via push_heap/pop_heap/make_heap
    std::atomic<bool> m_stopGeneration{false};

    std::vector<std::unique_ptr<GenerationWorkerResult>> m_workerResults; // one per worker thread

    void generation_worker_loop(size_t id);
};

inline bool operator>(const TaskGeneratingInput& a, const TaskGeneratingInput& b) {
    return a.priority > b.priority;
}