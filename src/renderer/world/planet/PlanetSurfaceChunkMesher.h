#pragma once

#include "blockingconcurrentqueue.h"
#include "core/world/planet/planet_types.h"
#include "renderer/world/planet/planet_rendering_types.h"
#include <thread>
#include <vector>

namespace vp {

class PlanetSurfaceChunkMesher {
public:
    struct MeshingResult {
        PlanetSurfaceChunkKey key;
        std::shared_ptr<PlanetSurfaceChunkMesh> mesh;
    };

    PlanetSurfaceChunkMesher(unsigned workerCount = 0);
    ~PlanetSurfaceChunkMesher();

    PlanetSurfaceChunkMesher(const PlanetSurfaceChunkMesher&) = delete;
    PlanetSurfaceChunkMesher& operator=(const PlanetSurfaceChunkMesher&) = delete;

    void enqueue(const PlanetSurfaceChunkKey& key, const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk);
    uint32_t drain(std::vector<MeshingResult>& outResults, uint32_t maxResults = 32);

private:
    struct MeshingTask {
        PlanetSurfaceChunkKey key;
        std::shared_ptr<PlanetSurfaceVoxelChunk> chunk;
    };

    std::vector<std::jthread> m_workerThreads;

    moodycamel::BlockingConcurrentQueue<MeshingTask> m_taskQueue;
    moodycamel::ConcurrentQueue<MeshingResult> m_resultQueue;

    void worker_loop(std::stop_token stopToken);

    void mesh_chunk(const PlanetSurfaceChunkKey& key, const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk,
                    std::shared_ptr<PlanetSurfaceChunkMesh>& outMesh);
};

} // namespace vp
