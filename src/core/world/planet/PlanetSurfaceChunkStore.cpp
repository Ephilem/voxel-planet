//
// Created by raph on 11/08/2026.
//

#include "PlanetSurfaceChunkStore.h"

namespace vp {
void PlanetSurfaceChunkStore::store(const PlanetSurfaceChunkKey& key, std::shared_ptr<PlanetSurfaceVoxelChunk> chunk) {
    m_chunks[key] = std::move(chunk);
    m_lastChunk = nullptr;
    m_lastKey = {};
}

bool PlanetSurfaceChunkStore::remove(const PlanetSurfaceChunkKey& key) {
    if (m_chunks.contains(key)) {
        m_chunks.erase(key);
        m_lastChunk = nullptr;
        m_lastKey = {};
        return true;
    }
    return false;
}

const PlanetSurfaceVoxelChunk* PlanetSurfaceChunkStore::find(const PlanetSurfaceChunkKey& key) const {
    // fast find
    if (m_lastKey == key) {
        return m_lastChunk;
    }

    const PlanetSurfaceVoxelChunk* chunk;
    if (auto it = m_chunks.find(key); it != m_chunks.end()) {
        chunk = it->second.get();
    } else {
        chunk = nullptr;
    }

    m_lastChunk = chunk;
    m_lastKey = key;

    return chunk;
}

bool PlanetSurfaceChunkStore::contains(const PlanetSurfaceChunkKey& key) const {
    if (m_lastKey == key) {
        return m_lastChunk != nullptr;
    }

    bool found = m_chunks.contains(key);
    if (found) {
        m_lastKey = key;
        m_lastChunk = m_chunks.at(key).get();
    } else {
        m_lastKey = {};
        m_lastChunk = nullptr;
    }
    return found;
}

std::optional<PlanetSurfaceChunkBlockInfo> PlanetSurfaceChunkStore::voxel_at(CubemapFace face, uint8_t level,
                                                                             glm::ivec3 voxelPos) const {
    PlanetSurfaceChunkKey key(face, level, voxelPos);
    if (const PlanetSurfaceVoxelChunk* chunk = find(key)) {
        glm::ivec3 localPos = glm::ivec3{voxelPos.x - (key.x * CHUNK_SIZE), voxelPos.y - (key.y * CHUNK_SIZE),
                                         voxelPos.z - (key.alt * CHUNK_SIZE)};
        return chunk->at(localPos);
    }
    return std::optional<PlanetSurfaceChunkBlockInfo>{};
}

void PlanetSurfaceChunkStore::clear() {}
} // namespace vp
