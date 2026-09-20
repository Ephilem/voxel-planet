#include "PlanetSurfaceChunkMesher.h"
#include "core/log/Logger.h"

#include <algorithm>

namespace vp {

PlanetSurfaceChunkMesher::PlanetSurfaceChunkMesher(unsigned workerCount) {
    if (workerCount == 0) {
        workerCount = std::max(1U, std::thread::hardware_concurrency() / 2);
    }

    m_workerThreads.reserve(workerCount);
    for (unsigned i = 0; i < workerCount; ++i) {
        m_workerThreads.emplace_back([this](std::stop_token stop) { worker_loop(std::move(stop)); });
    }
}

PlanetSurfaceChunkMesher::~PlanetSurfaceChunkMesher() {
    for (auto& w : m_workerThreads) {
        w.request_stop();
    }
    for (auto& w : m_workerThreads) {
        w.join();
    }
}

void PlanetSurfaceChunkMesher::enqueue(const PlanetSurfaceChunkKey& key,
                                       const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk, uint32_t generation) {
    // early out for chunk without voxel data
    if (!chunk->is_allocated()) {
        return;
    }

    m_taskQueue.enqueue({.key = key, .chunk = chunk, .generation = generation});
}

uint32_t PlanetSurfaceChunkMesher::drain(std::vector<MeshingResult>& outResults, uint32_t maxResults) {
    uint32_t count = 0;
    MeshingResult result;

    while (count < maxResults && m_resultQueue.try_dequeue(result)) {
        outResults.emplace_back(std::move(result));
        ++count;
    }

    return count;
}

void PlanetSurfaceChunkMesher::worker_loop(std::stop_token stopToken) {
    moodycamel::ConsumerToken token(m_taskQueue);

    MeshingTask task;
    while (!stopToken.stop_requested()) {
        if (!m_taskQueue.wait_dequeue_timed(token, task, std::chrono::milliseconds(50))) {
            continue;
        }

        if (task.chunk == nullptr) {
            LOG_WARN("MesherWorker", "Chunk {} has invalid chunk data (null pointer)", task.key);
            continue;
        }

        std::shared_ptr<PlanetSurfaceChunkMesh> mesh;
        mesh->generation = task.generation;
        MeshingResult result;
        result.key = task.key;
        mesh_chunk(task.key, task.chunk, mesh);
        result.mesh = mesh;
        m_resultQueue.enqueue(std::move(result));
    }
}

void PlanetSurfaceChunkMesher::mesh_chunk(const PlanetSurfaceChunkKey& key,
                                          const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk,
                                          std::shared_ptr<PlanetSurfaceChunkMesh>& outMesh) {
    constexpr glm::ivec3 kFaceNormals[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };

    constexpr glm::ivec3 kFaceCorners[6][4] = {
        {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, // +X
        {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, // -X
        {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, // +Y
        {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, // -Y
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, // +Z
        {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}, // -Z
    };

    outMesh = std::make_shared<PlanetSurfaceChunkMesh>();

    auto& vertices = outMesh->vertices;

    vertices.reserve(CHUNK_SIZE * CHUNK_SIZE * 6 * 6);

    const auto solid = [&chunk](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) {
            return false;
        }
        return chunk->at(x, y, z).localBlockID != LocalBlockID::Air;
    };

    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                const PlanetSurfaceChunkBlockInfo block = chunk->at(x, y, z);
                if (block.localBlockID == LocalBlockID::Air) {
                    continue;
                }

                // TODO resove slot (for now defualt checkboard)
                const uint32_t slot = 0;

                for (uint32_t f = 0; f < 6; ++f) {
                    const glm::ivec3 n = kFaceNormals[f];
                    if (solid(x + n.x, y + n.y, z + n.z)) {
                        continue;
                    }

                    std::array<PlanetSurfaceChunkVertex, 4> quad;
                    for (int c = 0; c < 4; ++c) {
                        const glm::ivec3 p = glm::ivec3(x, y, z) + kFaceCorners[f][c];
                        quad[c] = {
                            .x = uint8_t(p.x),
                            .y = uint8_t(p.y),
                            .z = uint8_t(p.z),
                            .face = f,
                            .textureSlot = slot,
                        };
                    }

                    vertices.insert(vertices.end(), {quad[0], quad[1], quad[2], quad[0], quad[2], quad[3]});
                }
            }
        }
    }

    vertices.shrink_to_fit();
}

} // namespace vp
