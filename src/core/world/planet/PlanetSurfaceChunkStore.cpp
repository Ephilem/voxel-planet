//
// Created by raph on 11/08/2026.
//

#include "PlanetSurfaceChunkStore.h"

namespace vp::core {
void PlanetSurfaceChunkStore::store(const PlanetSurfaceChunkKey& key, std::shared_ptr<PlanetSurfaceVoxelChunk> chunk) {
    // Captured before the insert: it may rehash, which invalidates an iterator kept across it
    const bool existed = m_chunks.contains(key);
    m_chunks[key] = std::move(chunk);
    m_lastChunk = nullptr;
    m_lastKey = {};

    m_changedChunks[key] = existed ? ChunkChangeKind::Updated : ChunkChangeKind::Added;
}

std::shared_ptr<PlanetSurfaceVoxelChunk> PlanetSurfaceChunkStore::find(const PlanetSurfaceChunkKey& key) const {
    // fast find
    if (m_lastKey == key) {
        return m_lastChunk;
    }

    const auto it = m_chunks.find(key);
    m_lastChunk = it == m_chunks.end() ? nullptr : it->second;
    m_lastKey = key;

    return m_lastChunk;
}

bool PlanetSurfaceChunkStore::contains(const PlanetSurfaceChunkKey& key) const {
    if (m_lastKey == key) {
        return m_lastChunk != nullptr;
    }

    bool found = m_chunks.contains(key);
    if (found) {
        m_lastKey = key;
        m_lastChunk = m_chunks.at(key);
    } else {
        m_lastKey = {};
        m_lastChunk = nullptr;
    }
    return found;
}

PlanetSurfaceVoxelChunkNeighbors PlanetSurfaceChunkStore::neighbors_of(const PlanetSurfaceChunkKey& key) const {
    const auto offset = [&](int dx, int dy, int dalt) {
        PlanetSurfaceChunkKey k = key;
        k.x += dx;
        k.y += dy;
        k.alt += dalt;
        return find(k);
    };

    PlanetSurfaceVoxelChunkNeighbors neighbors;
    neighbors.px = offset(+1, 0, 0);
    neighbors.nx = offset(-1, 0, 0);
    neighbors.py = offset(0, +1, 0);
    neighbors.ny = offset(0, -1, 0);
    neighbors.pz = offset(0, 0, +1);
    neighbors.nz = offset(0, 0, -1);
    neighbors.pxpy = offset(+1, +1, 0);
    neighbors.pxpz = offset(+1, 0, +1);
    neighbors.pypz = offset(0, +1, +1);
    neighbors.pxpypz = offset(+1, +1, +1);

    return neighbors;
}
} // namespace vp::core
