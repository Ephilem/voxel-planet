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

    void enqueue(const PlanetSurfaceChunkKey& key, const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk,
                 const PlanetSurfaceVoxelChunkNeighbors& neighbors);
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
        PlanetSurfaceVoxelChunkNeighbors neighbors; // +X, -X, +Y, -Y, +Z, -Z
        uint32_t generation = 0;
    };

    std::vector<std::jthread> m_workerThreads;

    moodycamel::BlockingConcurrentQueue<MeshingTask> m_taskQueue;
    moodycamel::ConcurrentQueue<MeshingResult> m_resultQueue;

    // occupancy grid padded by one voxel on each side
    static constexpr int kPadded = CHUNK_SIZE + 2;
    using PaddedOccupancy = std::array<uint8_t, kPadded * kPadded * kPadded>;

    static constexpr int kNeighborOffset[6] = {1, -1, kPadded, -kPadded, kPadded * kPadded, -kPadded * kPadded};

    // a neighbor not loaded yet hides the border faces
    static constexpr uint8_t kMissingNeighbor = 1;

    static constexpr int padded_index(int x, int y, int z) {
        return (x + 1) + ((y + 1) * kPadded) + ((z + 1) * kPadded * kPadded);
    }

    static bool voxel_occludes(PlanetSurfaceChunkVoxelInfo raw);

    /// Octahedral encoding of a unit vector on 16 bits (8 per axis), decoded in planet_surface_chunk.vert
    static uint32_t encode_normal(glm::vec3 n);

    static void fill_occupancy(PaddedOccupancy& occ, const PlanetSurfaceVoxelChunk& chunk,
                               const PlanetSurfaceVoxelChunkNeighbors& neighbors);

    void worker_loop(std::stop_token stopToken);

    void mesh_chunk(const PlanetSurfaceChunkKey& key, const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk,
                    const PlanetSurfaceVoxelChunkNeighbors& neighbors,
                    std::shared_ptr<PlanetSurfaceChunkMesh>& outMesh);
};

} // namespace vp
