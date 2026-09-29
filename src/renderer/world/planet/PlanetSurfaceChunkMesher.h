#pragma once

#include "blockingconcurrentqueue.h"
#include "core/world/planet/planet_types.h"
#include "renderer/world/planet/planet_rendering_types.h"
#include "renderer/world/planet/PlanetVoxelRenderTable.h"
#include <thread>
#include <unordered_map>
#include <vector>

namespace vp {

class PlanetSurfaceChunkMesher {
public:
    using MeshingResult = PlanetSurfaceChunkMeshUpload;

    PlanetSurfaceChunkMesher(const PlanetVoxelRenderTable* renderTable, unsigned workerCount = 0);
    ~PlanetSurfaceChunkMesher();

    PlanetSurfaceChunkMesher(const PlanetSurfaceChunkMesher&) = delete;
    PlanetSurfaceChunkMesher& operator=(const PlanetSurfaceChunkMesher&) = delete;

    void enqueue(const PlanetSurfaceChunkKey& key, const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk);
    uint32_t drain(std::vector<MeshingResult>& outResults, uint32_t maxResults = 32);

    /// Drop any in-flight meshing of this chunk: its result will be discarded by drain()
    void cancel(const PlanetSurfaceChunkKey& key) { m_latest.erase(key); }

    /// Cumulative counters, main thread only
    struct Stats {
        uint32_t skippedUnallocated = 0; // enqueue() early out
        uint32_t discardedStale = 0;     // results dropped by drain() cancelled or superseded by newer generation
        uint32_t emptyMeshes = 0;        // results without vertices
    };

    [[nodiscard]] const Stats& stats() const { return m_stats; }

    [[nodiscard]] size_t in_flight_count() const { return m_latest.size(); }

    [[nodiscard]] size_t queued_results() const { return m_resultQueue.size_approx(); }

private:
    Stats m_stats;

    // main thread only: latest generation enqueued per chunk
    const PlanetVoxelRenderTable* m_voxelRenderTable;
    std::unordered_map<PlanetSurfaceChunkKey, uint32_t> m_latest;
    uint32_t m_nextGeneration = 1;

    struct MeshingTask {
        PlanetSurfaceChunkKey key;
        std::shared_ptr<PlanetSurfaceVoxelChunk> chunk;
        std::array<std::shared_ptr<const PlanetSurfaceVoxelChunk>, 6> neighbors; // +X, -X, +Y, -Y, +Z, -Z
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
