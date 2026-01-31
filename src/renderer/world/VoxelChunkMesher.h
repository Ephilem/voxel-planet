#pragma once

#include <condition_variable>
#include <flecs.h>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "core/main_components.h"
#include "core/resource/asset_id.h"
#include "core/world/world_components.h"
#include "renderer/rendering_components.h"

struct TaskMeshingInput {
    glm::ivec3 chunkCoord;
    std::shared_ptr<const std::array<uint8_t, CHUNK_VOLUME>> voxels;
    std::unordered_map<AssetID, uint8_t> textureIDs;
};

struct TaskMeshingOutput {
    glm::ivec3 chunkCoord;

    // moved ownership to not copy large data
    std::vector<TerrainFace3d> faces;

    bool success = false;
};

class VoxelChunkMesher {
public:
    VoxelChunkMesher() = default;
    ~VoxelChunkMesher();
    void shutdown();

    void init(flecs::world& ecs);
    void static Register(flecs::world& ecs);

    size_t pending_count() const {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        return m_pendingCoords.size();
    }

    size_t completed_count() const {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        return m_resultQueue.size();
    }

private:
    void enqueue(TaskMeshingInput&& taskInput);

    /**
     * Check if a chunk at the given coordinate is already pending meshing.
     * @param coord Chunk coordinate
     * @return True if the chunk is pending meshing, false otherwise
     */
    bool is_pending(const glm::ivec3& coord) const {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        return m_pendingCoords.count(coord) > 0;
    }

    std::vector<TaskMeshingOutput> poll_results(size_t maxResults = 30);

    void enqueue_meshing_system(flecs::entity e, const VoxelChunk& chunk, const ChunkCoordinate& pos);
    void poll_meshing_results_system(flecs::iter& it);

    // Worker thread function
    void worker_loop(size_t id);
    TaskMeshingOutput build_mesh(const TaskMeshingInput& input);


    std::vector<std::thread> m_workerThreads;

    // task queue input
    mutable std::mutex m_taskMutex;
    std::condition_variable m_taskCv;
    std::queue<TaskMeshingInput> m_taskQueue;
    std::unordered_set<glm::ivec3, IVec3Hash> m_pendingCoords;

    // result queue output
    mutable std::mutex m_resultMutex;
    std::queue<TaskMeshingOutput> m_resultQueue;

    std::atomic<bool> m_stop;
};
