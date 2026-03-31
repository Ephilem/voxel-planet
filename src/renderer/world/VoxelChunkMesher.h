#pragma once

#include <condition_variable>
#include <flecs.h>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "core/math/frustrum.h"
#include "core/resource/asset_id.h"
#include "core/world/world_components.h"
#include "renderer/rendering_components.h"

#include "core/TracyIntegration.h"

struct TaskMeshingInput {
    glm::ivec3 chunkCoord;
    std::shared_ptr<const std::array<uint16_t, CHUNK_VOLUME>> voxels;
    std::array<std::shared_ptr<const std::array<uint16_t, CHUNK_VOLUME>>, 6> neighborVoxels; // size 6
    std::unordered_map<uint8_t, uint16_t> textureIDs;
    float priority = 0.0f;
    uint32_t meshGeneration = 0; // Copied to output so stale results can be discarded
};

inline bool operator>(const TaskMeshingInput& a, const TaskMeshingInput& b) {
    return a.priority > b.priority;
}

struct TaskMeshingOutput {
    glm::ivec3 chunkCoord;

    // moved ownership to not copy large data
    std::vector<TerrainFace3d> faces;

    bool success = false;
    uint32_t meshGeneration = 0; // Must match VoxelChunkMesh::meshGeneration to be applied
};

// alignas(64) to avoid false sharing between worker threads and main thread when pushing/polling results
struct alignas(64) MeshWorkerResult {
    VOXEL_LOCKABLE(std::mutex, m_resultMutex);
    std::vector<TaskMeshingOutput> results;
    std::atomic<int> pendingCount{0};
};

class VoxelChunkMesher {
public:
    VoxelChunkMesher() = default;
    ~VoxelChunkMesher();
    void shutdown();

    void init(flecs::world& ecs);
    void static Register(flecs::world& ecs);

    void update_frustum(const glm::mat4& projectionViewMatrix, const glm::vec3& cameraPos, const glm::vec3& viewDir) {
        m_frustum.update(projectionViewMatrix);
        m_cameraPos = cameraPos;
        m_viewDir = viewDir;
    }

    size_t pending_count() const {
        return m_pendingCoords.size();
    }

    size_t completed_count() const {
        int total = 0;
        for (const auto& wr : m_workerResults) {
            total += wr->pendingCount.load();
        }
        return total;
    }

    static constexpr size_t MESHING_BATCH_SIZE = 4;

private:
    std::vector<TaskMeshingOutput> poll_results(size_t maxResults = 30);

    void poll_meshing_results_system(flecs::iter& it);
    void enqueue_chunks_build_system(flecs::iter& it);
    void resolve_waiting_chunks_system(flecs::iter& it);

    float calculate_task_priority(const glm::ivec3& chunkPos) const;

    // Worker thread function
    void worker_loop(size_t id);
    TaskMeshingOutput build_mesh(const TaskMeshingInput& input);

    std::vector<std::thread> m_workerThreads;

    // task queue input
    mutable VOXEL_LOCKABLE(std::mutex, m_taskMutex);
    std::counting_semaphore<> m_taskSemaphore{0};
    std::priority_queue<
          TaskMeshingInput,
          std::vector<TaskMeshingInput>,
          std::greater<TaskMeshingInput>
    > m_taskQueue;
    std::unordered_set<glm::ivec3, IVec3Hash> m_pendingCoords;

    // result queue output
    std::vector<std::unique_ptr<MeshWorkerResult>> m_workerResults; // one per worker thread
    std::atomic<bool> m_stop;

    Frustrum m_frustum;
    glm::vec3 m_cameraPos;
    glm::vec3 m_viewDir;
};
