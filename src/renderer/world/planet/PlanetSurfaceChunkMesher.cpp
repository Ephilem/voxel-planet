#include "PlanetSurfaceChunkMesher.h"

namespace vp {

PlanetSurfaceChunkMesher::PlanetSurfaceChunkMesher(unsigned workerCount) {
    if (workerCount == 0) {
        workerCount = std::thread::hardware_concurrency() / 2;
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
                                       const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk) {}

bool PlanetSurfaceChunkMesher::poll_results(std::vector<MeshingResult>& outResults, uint32_t maxResults = 32) {}

} // namespace vp
