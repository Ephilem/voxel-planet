#pragma once
#include <condition_variable>
#include <deque>
#include <unordered_set>
#include <flecs.h>
#include <queue>

#include "world_components.h"
#include "core/main_components.h"


class WorldGenerator;

struct TaskGeneratingInput {
    glm::ivec3 chunkCoord;
    WorldGenerator* generator;
};

struct TaskGeneratingOutput {
    std::shared_ptr<std::array<uint8_t, (32 * 32 * 32)>> voxels;
    std::unordered_map<AssetID, uint8_t> textureIDs;

    glm::ivec3 chunkCoord;

    bool success = false;
    bool empty = true;
};


/**
 * Class with the responsibility to manage chunk loading, unloading, and overall chunk lifecycle.
 */
class ChunkManager {
public:
    ChunkManager() = default;
    ~ChunkManager();

    void init(flecs::world& ecs);
    void static Register(flecs::world& ecs);

private:
    std::deque<glm::ivec3> m_loadQueue;
    std::deque<glm::ivec3> m_unloadQueue;

    std::unordered_map<glm::ivec3, flecs::entity, IVec3Hash> m_loadedChunks;
    std::unordered_set<glm::ivec3, IVec3Hash> m_emptyChunks;
    std::unordered_set<glm::ivec3, IVec3Hash> m_loadingChunks;

    static constexpr int MAX_CHUNKS_PER_FRAME = 20;
    static constexpr int MAX_UNLOADS_PER_FRAME = 50;

    void shutdown();

    // Ecs systems
    void update_desired_chunk_system(flecs::entity e, ChunkLoader& loader, const Position& position);
    void process_load_queue_system(flecs::iter& it);
    void process_unload_queue_system(flecs::iter& it);

    void poll_generation_results_system(flecs::iter& it);

    // Action methods
    void load_chunks_at_radius(const ChunkCoordinate &center, int radius, WorldGenerator* generator);

    void enqueue_chunk_generation(const glm::ivec3 & chunkPos, WorldGenerator* generator);

    std::vector<TaskGeneratingOutput> poll_generation_results(size_t maxResults = 30);

    // Helper
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

    static bool is_within_sphere(const glm::ivec3& center, const glm::ivec3& point, int radius) {
        glm::ivec3 diff = point - center;
        return diff.x * diff.x + diff.y * diff.y + diff.z * diff.z <= radius * radius;
    }

    static bool is_chunk_still_needed(const glm::ivec3& chunkPos, flecs::iter &it);

    // Generation thread's methods and attributes
    std::vector<std::thread> m_generationThreads;
    std::mutex m_generationMutex;
    std::condition_variable m_generationCv;
    std::queue<TaskGeneratingInput> m_generationQueue;
    std::atomic<bool> m_stopGeneration{false};

    std::mutex m_generationResultsMutex;
    std::queue<TaskGeneratingOutput> m_generationResultsQueue;

    void generation_worker_loop(size_t id);
};