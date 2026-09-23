#pragma once

#include "blockingconcurrentqueue.h"
#include "core/world/planet/planet_types.h"
#include "renderer/world/planet/planet_rendering_types.h"
#include <thread>
#include <unordered_map>
#include <vector>

namespace vp {

class PlanetSurfaceChunkMesher {
public:
    using MeshingResult = PlanetSurfaceChunkMeshUpload;

    PlanetSurfaceChunkMesher(unsigned workerCount = 0);
    ~PlanetSurfaceChunkMesher();

    PlanetSurfaceChunkMesher(const PlanetSurfaceChunkMesher&) = delete;
    PlanetSurfaceChunkMesher& operator=(const PlanetSurfaceChunkMesher&) = delete;

    void enqueue(const PlanetSurfaceChunkKey& key, const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk);
    uint32_t drain(std::vector<MeshingResult>& outResults, uint32_t maxResults = 32);

    /// Drop any in-flight meshing of this chunk: its result will be discarded by drain()
    void cancel(const PlanetSurfaceChunkKey& key) { m_latest.erase(key); }

private:
    // main thread only: latest generation enqueued per chunk. A result with an older generation is stale
    std::unordered_map<PlanetSurfaceChunkKey, uint32_t> m_latest;
    uint32_t m_nextGeneration = 1;

    struct MeshingTask {
        PlanetSurfaceChunkKey key;
        std::shared_ptr<PlanetSurfaceVoxelChunk> chunk;
        uint32_t generation = 0;
    };

    std::vector<std::jthread> m_workerThreads;

    moodycamel::BlockingConcurrentQueue<MeshingTask> m_taskQueue;
    moodycamel::ConcurrentQueue<MeshingResult> m_resultQueue;

    void worker_loop(std::stop_token stopToken);

    void mesh_chunk(const PlanetSurfaceChunkKey& key, const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk,
                    std::shared_ptr<PlanetSurfaceChunkMesh>& outMesh);
};

} // namespace vp
