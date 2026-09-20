//
// Created by raph on 11/08/2026.
//

#include "PlanetSurfaceChunkStore.h"

namespace vp {
void PlanetSurfaceChunkStore::store(const PlanetSurfaceChunkKey& key, std::shared_ptr<PlanetSurfaceVoxelChunk> chunk) {
    // Captured before the insert: it may rehash, which invalidates an iterator kept across it
    const bool existed = m_chunks.contains(key);
    m_chunks[key] = std::move(chunk);
    m_lastChunk = nullptr;
    m_lastKey = {};

    m_changedChunks[key] = existed ? ChunkChangeKind::Updated : ChunkChangeKind::Added;
}

bool PlanetSurfaceChunkStore::remove(const PlanetSurfaceChunkKey& key) {
    if (m_chunks.contains(key)) {
        m_chunks.erase(key);
        m_lastChunk = nullptr;
        m_lastKey = {};
        m_changedChunks[key] = ChunkChangeKind::Removed;
        return true;
    }
    return false;
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

std::optional<PlanetSurfaceChunkBlockInfo> PlanetSurfaceChunkStore::voxel_at(CubemapFace face, uint8_t level,
                                                                             glm::ivec3 voxelPos) const {
    PlanetSurfaceChunkKey key(face, level, voxelPos);
    if (const auto chunk = find(key)) {
        glm::ivec3 localPos = glm::ivec3{voxelPos.x - (key.x * CHUNK_SIZE), voxelPos.y - (key.y * CHUNK_SIZE),
                                         voxelPos.z - (key.alt * CHUNK_SIZE)};
        return chunk->at(localPos);
    }
    return std::optional<PlanetSurfaceChunkBlockInfo>{};
}

void PlanetSurfaceChunkStore::clear() {
    for (const auto& [key, chunk] : m_chunks) {
        m_changedChunks[key] = ChunkChangeKind::Removed;
    }

    m_chunks.clear();
    m_lastChunk = nullptr;
    m_lastKey = {};
}
} // namespace vp
