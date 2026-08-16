#pragma once

#include "core/world/planet/planet_types.h"

#include <memory>
#include <optional>
#include <unordered_map>

namespace vp {
class PlanetSurfaceChunkStore {
public:
    PlanetSurfaceChunkStore() = default;

    void store(const PlanetSurfaceChunkKey& key, std::shared_ptr<PlanetSurfaceVoxelChunk> chunk);
    bool remove(const PlanetSurfaceChunkKey& key);

    [[nodiscard]] const PlanetSurfaceVoxelChunk* find(const PlanetSurfaceChunkKey& key) const;
    [[nodiscard]] bool contains(const PlanetSurfaceChunkKey& key) const;

    [[nodiscard]] std::optional<PlanetSurfaceChunkBlockInfo> voxel_at(CubemapFace face, uint8_t level,
                                                                      glm::ivec3 voxelPos) const;

    [[nodiscard]] size_t size() const { return m_chunks.size(); }

    void clear();

private:
    std::unordered_map<PlanetSurfaceChunkKey, std::shared_ptr<PlanetSurfaceVoxelChunk>> m_chunks;

    mutable PlanetSurfaceChunkKey m_lastKey;
    mutable const PlanetSurfaceVoxelChunk* m_lastChunk = nullptr;
};
} // namespace vp
