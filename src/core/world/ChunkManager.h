#pragma once
#include <chrono>
#include <deque>
#include <semaphore>
#include <unordered_set>
#include <flecs.h>
#include <vector>
#include <atomic>
#include <thread>

#include "world_components.h"
#include "core/main_components.h"
#include "core/TracyIntegration.h"

class WorldGenerator;

struct ChunkKey {
    glm::ivec3 pos;
    int lod;

    bool operator==(const ChunkKey& other) const {
        return pos == other.pos && lod == other.lod;
    }
};

struct IChunkKeyHash {
    std::size_t operator()(const ChunkKey& k) const {
        return static_cast<std::size_t>(k.pos.x) * 73856093 ^
               static_cast<std::size_t>(k.pos.y) * 19349663 ^
               static_cast<std::size_t>(k.pos.z) * 83492791 ^
               static_cast<std::size_t>(k.lod) * 2654435761u;
    }
};

struct ChunkCandidate {
    ChunkKey key;
    float priority;
};

inline bool operator>(const ChunkCandidate& a, const ChunkCandidate& b) {
    return a.priority > b.priority;
}

struct TaskGeneratingInput {
    glm::ivec3 chunkCoord;
    WorldGenerator* generator;
    int lod = 0;
    float priority = 0.0f;
};

struct TaskGeneratingOutput {
    std::shared_ptr<std::array<uint8_t, (32 * 32 * 32)>> voxels;
    std::unordered_map<AssetID, uint8_t> textureIDs;
    glm::ivec3 chunkCoord;
    int lod = 0;
    bool success = false;
    bool empty = true;
};

// alignas(64) to avoid false sharing between worker threads and main thread when pushing/polling results
struct alignas(64) GenerationWorkerResult {
    VOXEL_LOCKABLE(std::mutex, m_resultMutex);
    std::vector<TaskGeneratingOutput> results;
    std::atomic<int> pendingCount{0};
};

struct ChunkManagerStats {
    size_t loadedChunkCount = 0;
    size_t loadingChunkCount = 0;
    size_t emptyChunkCount = 0;
    size_t cancelledChunkCount = 0;
    size_t candidateChunkCount = 0;
    size_t unloadChunkCount = 0;

    uint64_t chunksGenerated = 0;
    uint64_t chunksUnloaded = 0;
};

enum class ChunkState { None, Loaded, Empty, Loading, Candidate, Cancelled };

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
    flecs::entity get_chunk_entity(const ChunkKey &key) const;
    flecs::entity get_chunk_entity(const glm::ivec3 &chunkPos) const {
        return get_chunk_entity(ChunkKey{chunkPos, 0});
    }
    bool can_mesh(const glm::ivec3& chunkPos, uint8_t lod = 0) const;

    // Stats
    ChunkManagerStats get_stats() const;
    ChunkState get_chunk_state(const ChunkKey &key) const;
    glm::ivec3 get_current_center() const { return m_currentCenter; }
    uint64_t get_total_generated() const { return m_chunksGenerated.load(std::memory_order_relaxed); }
    uint64_t get_total_unloaded() const { return m_chunksUnloaded.load(std::memory_order_relaxed); }

    // Debug methods
    void unload_all_chunks(flecs::world& ecs);

    // Debugs flags
    bool enqueueCandidates = true;
    bool loadingEnabled = true;
    bool unloadQueueEnabled = true;

private:
    // Stats
    std::atomic<uint64_t> m_chunksGenerated{0};
    std::atomic<uint64_t> m_chunksUnloaded{0};


    std::deque<ChunkKey> m_unloadQueue;
    std::unordered_set<ChunkKey, IChunkKeyHash> m_unloadQueueSet;

    std::unordered_map<ChunkKey, flecs::entity, IChunkKeyHash> m_loadedChunks;
    std::unordered_set<ChunkKey, IChunkKeyHash> m_emptyChunks;
    std::unordered_set<ChunkKey, IChunkKeyHash> m_loadingChunks;

    std::unordered_set<ChunkKey, IChunkKeyHash> m_cancelledChunks;

    static constexpr std::chrono::microseconds DRAIN_TIME_BUDGET{500};
    static constexpr int MAX_UNLOADS_PER_FRAME = 50;
    static constexpr int MAX_GENERATION_RESULTS_PER_FRAME = 100;
    static constexpr size_t GENERATION_BATCH_SIZE = 4;

    void shutdown();

    // ECS Systems
    void update_chunks_system(flecs::entity e, ChunkLoader& loader, const Position& position, WorldGenerator* generator);
    void poll_generation_results_system(flecs::iter& it);
    void process_unload_queue_system(flecs::iter& it);
    void drain_candidate_buffer_system();

    // Actions
    void request_chunks_in_radius(const glm::ivec3& center, const glm::ivec3& oldCenter, int radius, WorldGenerator* generator);
    void cancel_chunk_generation(const ChunkKey& key);
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

    bool is_chunk_processed(const ChunkKey& key) const {
        return m_loadedChunks.contains(key) || m_emptyChunks.contains(key);
    }

    bool is_chunk_in_progress(const ChunkKey& key) const {
        return m_loadingChunks.contains(key) || m_inCandidateHeap.contains(key);
    }

    bool is_chunk_cancelled(const ChunkKey& key) const {
        return m_cancelledChunks.contains(key);
    }

    int get_lod(float distance) const {
        if (distance < 256.0f) return 0;
        if (distance < 512.0f) return 1;
        if (distance < 1024.0f) return 2;
        return 3;
    }

    static float calculate_priority(const ChunkKey& key, const glm::ivec3& center);
    bool is_chunk_still_needed(const ChunkKey& key, const flecs::world &world) const;

    // Generation threads
    std::vector<std::thread> m_generationThreads;
    VOXEL_LOCKABLE(std::mutex, m_generationMutex);
    std::counting_semaphore<> m_generationSemaphore{0};
    std::vector<TaskGeneratingInput> m_generationQueue; // min-heap managed via push_heap/pop_heap/make_heap
    std::atomic<bool> m_stopGeneration{false};

    std::vector<std::unique_ptr<GenerationWorkerResult>> m_workerResults; // one per worker thread

    // Candidate staging heap (main thread only)
    std::vector<ChunkCandidate> m_candidateHeap;
    std::unordered_set<ChunkKey, IChunkKeyHash> m_inCandidateHeap;
    glm::ivec3 m_currentCenter{};
    int m_currentLoadRadius = 0;
    WorldGenerator* m_cachedGenerator = nullptr;

    void generation_worker_loop(size_t id);
};

inline bool operator>(const TaskGeneratingInput& a, const TaskGeneratingInput& b) {
    return a.priority > b.priority;
}