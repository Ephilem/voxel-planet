//
// Created by raph on 11/08/2026.
//

#include "PlanetSurfaceChunkGenerator.h"

#include "core/TracyIntegration.h"
#include "core/world/planet/planet_transform.h"
#include "core/world/planet/planet_types.h"

namespace vp {

PlanetSurfaceChunkGenerator::PlanetSurfaceChunkGenerator(PlanetTerrainParams params, double planetRadius,
                                                         flecs::ref<const PlanetVoxelRegistry> registry,
                                                         unsigned workerCount)
    : m_params(params), m_planetRadius(planetRadius), m_registry(registry) {
    if (workerCount == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        workerCount = hw > 3 ? hw - 2 : 1;
    }

    for (unsigned i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this](std::stop_token stop) { worker_loop(std::move(stop)); });
    }

    LOG_DEBUG("PlanetTileGenerator", "Started {} worker threads", m_workers.size());
}

PlanetSurfaceChunkGenerator::~PlanetSurfaceChunkGenerator() {
    const size_t count = m_workers.size();

    for (auto& worker : m_workers)
        worker.request_stop();
    for (auto& worker : m_workers)
        worker.join();
    m_workers.clear();

    LOG_DEBUG("PlanetTileGenerator", "Stopped {} worker threads", count);
}

void PlanetSurfaceChunkGenerator::request(const PlanetSurfaceChunkKey& key, float priority) {
    if (!key.valid()) {
        return;
    }
    if (m_inFlight.contains(key)) {
        ++m_stats.dedupRejects;
        return;
    }
    if (m_pendingSet.contains(key)) {
        ++m_stats.dedupRejects;
        return;
    }

    m_pending.emplace_back(Pending{.key = key, .priority = priority});
    m_pendingSet.insert(key);
    ++m_stats.requested;
    ++m_stats.requestedThisFrame;
    m_stats.peakPending = std::max(m_stats.peakPending, uint32_t(m_pending.size()));
}

void PlanetSurfaceChunkGenerator::submit_pending(uint32_t maxSubmit) {
    if (m_pending.empty()) {
        return;
    }

    const uint32_t count = std::min<uint32_t>(maxSubmit, static_cast<uint32_t>(m_pending.size()));

    if (count < m_pending.size()) {
        std::partial_sort(m_pending.begin(), m_pending.begin() + count, m_pending.end(),
                          [](const Pending& a, const Pending& b) { return a.priority < b.priority; });
    }

    if (m_keyScratch.size() < count) {
        m_keyScratch.resize(count);
    }

    for (uint32_t i = 0; i < count; ++i) {
        m_inFlight.insert(m_pending[i].key);
        m_keyScratch[i] = m_pending[i].key;
    }
    m_requestQueue.enqueue_bulk(m_keyScratch.data(), count);

    m_stats.submitted += count;
    m_stats.submittedThisFrame += count;
    m_stats.peakInFlight = std::max(m_stats.peakInFlight, uint32_t(m_inFlight.size()));

    m_pending.erase(m_pending.begin(), m_pending.begin() + count);
}

uint32_t PlanetSurfaceChunkGenerator::drain(std::vector<PlanetSurfaceChunkGenerationResult>& out, uint32_t maxDrain) {
    PlanetSurfaceChunkGenerationResult result;
    uint32_t count = 0;

    while (count < maxDrain && m_resultQueue.try_dequeue(result)) {
        m_inFlight.erase(result.key);
        out.emplace_back(std::move(result));
        ++count;
    }

    m_stats.completed += count;
    m_stats.completedThisFrame += count;
    return count;
}

void PlanetSurfaceChunkGenerator::begin_frame() {
    m_stats.requestedThisFrame = 0;
    m_stats.submittedThisFrame = 0;
    m_stats.completedThisFrame = 0;
}

void PlanetSurfaceChunkGenerator::worker_loop(std::stop_token stop) {
    PlanetTerrainSampler sampler = PlanetTerrainSampler(m_params);
    PlanetTerrainSampler::BatchScratch scratch;

    while (!stop.stop_requested()) {
        PlanetSurfaceChunkKey key;
        if (!m_requestQueue.wait_dequeue_timed(key, std::chrono::milliseconds(100))) {
            continue;
        }

        PlanetSurfaceVoxelChunk chunk(PlanetSurfaceVoxelChunk::Unallocated{});
        generate(key, chunk, sampler, scratch);

        m_resultQueue.enqueue(PlanetSurfaceChunkGenerationResult{
            .key = key, .chunk = std::make_shared<PlanetSurfaceVoxelChunk>(std::move(chunk))});
    }
}

void PlanetSurfaceChunkGenerator::generate(const PlanetSurfaceChunkKey& key, PlanetSurfaceVoxelChunk& out,
                                           const PlanetTerrainSampler& sampler,
                                           PlanetTerrainSampler::BatchScratch& scratch) const {
    VOXEL_ZONE_N("PlanetSurfaceChunkGenerator::generate");

    constexpr int COLUMNS = CHUNK_SIZE * CHUNK_SIZE;

    const double voxelSize = planet_chunk_voxel_size(key.level);
    const double voxelSpan = 2.0 / planet_voxels_per_face_side(m_planetRadius, key.level);
    const double chunkSpan = voxelSpan * double(CHUNK_SIZE);
    const double u0 = -1.0 + (double(key.x) * chunkSpan);
    const double v0 = -1.0 + (double(key.y) * chunkSpan);

    const double chunkBottom = double(key.alt) * double(CHUNK_SIZE) * voxelSize;
    const double chunkTop = chunkBottom + (double(CHUNK_SIZE) * voxelSize);

    thread_local std::vector<float> dirX, dirY, dirZ, heights;
    dirX.resize(COLUMNS);
    dirY.resize(COLUMNS);
    dirZ.resize(COLUMNS);
    heights.resize(COLUMNS);

    int idx = 0;
    for (int j = 0; j < CHUNK_SIZE; ++j) {
        for (int i = 0; i < CHUNK_SIZE; ++i, ++idx) {
            // voxel cente
            const double u = u0 + ((double(i) + 0.5) * voxelSpan);
            const double v = v0 + ((double(j) + 0.5) * voxelSpan);
            const glm::dvec3 dir = face_uv_to_direction(key.face, u, v);
            dirX[idx] = float(dir.x);
            dirY[idx] = float(dir.y);
            dirZ[idx] = float(dir.z);
        }
    }

    sampler.sample_height_batch(dirX.data(), dirY.data(), dirZ.data(), COLUMNS, key.level, heights.data(), scratch);

    const auto [minIt, maxIt] = std::minmax_element(heights.begin(), heights.end());
    const float minH = *minIt;
    const float maxH = *maxIt;

    if (chunkBottom >= double(maxH)) {
        return;
    }

    const auto* registry = m_registry.try_get();
    if (registry == nullptr) {
        LOG_ERROR("PlanetSurfaceChunkGenerator", "Voxel registry unavailable during generate(), skipping chunk");
        return;
    }

    out.allocate();
    const LocalBlockID stoneLocal = out.palette.intern(registry->resolve("voxelplanet:cobblestone"_asset));
    const PlanetSurfaceChunkBlockInfo solid{.localBlockID = stoneLocal, .height = 15};

    if (chunkTop <= double(minH)) {
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int j = 0; j < CHUNK_SIZE; ++j) {
                for (int i = 0; i < CHUNK_SIZE; ++i) {
                    out.set(i, j, z, solid);
                }
            }
        }
        return;
    }

    idx = 0;
    for (int j = 0; j < CHUNK_SIZE; ++j) {
        for (int i = 0; i < CHUNK_SIZE; ++i, ++idx) {
            const double colTop = double(heights[idx]);
            const int filled = std::clamp(int(std::floor((colTop - chunkBottom) / voxelSize)), 0, CHUNK_SIZE);

            for (int z = 0; z < filled; ++z)
                out.set(i, j, z, solid);
        }
    }
}

} // namespace vp
