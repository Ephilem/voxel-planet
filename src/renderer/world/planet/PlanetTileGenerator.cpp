//
// Created by raph on 08/08/2026.
//

#include "PlanetTileGenerator.h"

#include <algorithm>

#include "core/log/Logger.h"
#include "core/TracyIntegration.h"
#include "core/world/planet/planet_transform.h"

namespace vp {
PlanetTileGenerator::PlanetTileGenerator(PlanetTerrainParams params, uint16_t resolution, double planetRadius,
                                         unsigned workerCount)
    : m_params(std::move(params)), m_resolution(resolution), m_planetRadius(planetRadius) {
    if (workerCount == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        workerCount = hw > 3 ? hw - 2 : 1;
    }

    for (unsigned i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this](std::stop_token stop) { worker_loop(std::move(stop)); });
    }

    LOG_DEBUG("PlanetTileGenerator", "Started {} worker threads", m_workers.size());
}

PlanetTileGenerator::~PlanetTileGenerator() {
    const size_t count = m_workers.size();

    for (auto& worker : m_workers)
        worker.request_stop();
    for (auto& worker : m_workers)
        worker.join();
    m_workers.clear();

    LOG_DEBUG("PlanetTileGenerator", "Stopped {} worker threads", count);
}

void PlanetTileGenerator::begin_frame() {
    m_stats.requestedThisFrame = 0;
    m_stats.submittedThisFrame = 0;
    m_stats.completedThisFrame = 0;

    // Dropping what was not submitted is deliberate: the draw list re-requests every
    // tile it still misses, so a leftover request is either re-issued this frame or
    // no longer wanted. Keeping it would just queue work for tiles the camera left
    m_pending.clear();
    m_pendingSet.clear();
}

void PlanetTileGenerator::request(const PlanetTileKey& key, float priority) {
    if (!key.valid())
        return;
    if (m_inFlight.contains(key)) {
        ++m_stats.dedupRejects;
        return;
    }

    // The pending set mirrors m_pending so the dedup stays O(1). A linear scan was fine
    // at a handful of requests per frame, but the draw list re-requests every missing
    // tile every frame, so it grew quadratic once the atlas started out cold
    if (!m_pendingSet.insert(key).second) {
        ++m_stats.dedupRejects;
        return;
    }

    m_pending.emplace_back(PendingTile{key, priority});
    ++m_stats.requested;
    ++m_stats.requestedThisFrame;
    m_stats.peakPending = std::max(m_stats.peakPending, uint32_t(m_pending.size()));
}

void PlanetTileGenerator::submit_pending(uint32_t maxSubmit) {
    if (m_pending.empty())
        return;

    const uint32_t count = std::min<uint32_t>(maxSubmit, static_cast<uint32_t>(m_pending.size()));

    // Only a prefix is submitted, so the order decides what gets generated first. The
    // draw list is built face by face, which bears no relation to what the player is
    // looking at, hence the sort: nearest first, so the tile under the camera stops
    // waiting behind a horizon tile that nobody can tell apart from its ancestor
    if (count < m_pending.size()) {
        std::partial_sort(m_pending.begin(), m_pending.begin() + count, m_pending.end(),
                          [](const PendingTile& a, const PendingTile& b) { return a.priority < b.priority; });
    }

    if (m_keyScratch.size() < count)
        m_keyScratch.resize(count);

    for (uint32_t i = 0; i < count; ++i) {
        m_inFlight.insert(m_pending[i].key);
        m_keyScratch[i] = m_pending[i].key;
    }
    m_requestQueue.enqueue_bulk(m_keyScratch.data(), count);

    m_stats.submitted += count;
    m_stats.submittedThisFrame += count;
    m_stats.peakInFlight = std::max(m_stats.peakInFlight, uint32_t(m_inFlight.size()));

    // The rest is dropped by begin_frame: an unsubmitted request is re-issued next
    // frame if the tile is still missing
    m_pending.erase(m_pending.begin(), m_pending.begin() + count);
}

uint32_t PlanetTileGenerator::drain(std::vector<PlanetTileResult>& out, uint32_t maxDrain) {
    PlanetTileResult result;
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

void PlanetTileGenerator::worker_loop(std::stop_token stop) {
    PlanetTerrainSampler sampler(m_params);
    PlanetTerrainSampler::BatchScratch scratch;

    moodycamel::ConsumerToken token(m_requestQueue);

    PlanetTileKey key;
    while (!stop.stop_requested()) {
        if (!m_requestQueue.wait_dequeue_timed(token, key, std::chrono::milliseconds(50)))
            continue;

        PlanetTileResult result;
        result.key = key;
        generate(key, result.data, sampler, scratch);
        m_resultQueue.enqueue(std::move(result));
    }
}

void PlanetTileGenerator::generate(const PlanetTileKey& key, PlanetTileData& out, const PlanetTerrainSampler& sampler,
                                   PlanetTerrainSampler::BatchScratch& scratch) const {
    VOXEL_ZONE_N("PlanetTileGenerator::generate");

    const int res = m_resolution;
    const int count = res * res;

    out.resolution = m_resolution;

    const size_t padded = static_cast<size_t>(count) + PlanetTerrainSampler::SIMD_PADDING;
    std::vector<float> dirX(padded, 0.f), dirY(padded, 0.f), dirZ(padded, 1.f);
    std::vector<float> heights(padded, 0.f);

    const double extent = 2.0 / static_cast<double>(1u << key.level());
    const double u0 = -1.0 + static_cast<double>(key.x()) * extent;
    const double v0 = -1.0 + static_cast<double>(key.y()) * extent;

    const double step = extent / static_cast<double>(res - 1);

    for (int y = 0; y < res; ++y) {
        for (int x = 0; x < res; ++x) {
            const glm::dvec3 dir = face_uv_to_direction(key.face(), u0 + static_cast<double>(x) * step,
                                                        v0 + static_cast<double>(y) * step);

            const int i = y * res + x;
            dirX[i] = static_cast<float>(dir.x);
            dirY[i] = static_cast<float>(dir.y);
            dirZ[i] = static_cast<float>(dir.z);
        }
    }

    sampler.sample_height_batch(dirX.data(), dirY.data(), dirZ.data(), count, key.level(), heights.data(), scratch);

    out.heightmap.resize(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        out.heightmap[i] = planet_tile_encode_height(heights[i]);
    }
}
} // namespace vp
