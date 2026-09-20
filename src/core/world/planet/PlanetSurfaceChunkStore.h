#pragma once

#include "core/world/planet/planet_types.h"

#include <memory>
#include <optional>
#include <unordered_map>

namespace vp {
class PlanetSurfaceChunkStore {
public:
    enum class ChunkChangeKind : uint8_t { Added, Removed, Updated };

    PlanetSurfaceChunkStore() = default;

    void store(const PlanetSurfaceChunkKey& key, std::shared_ptr<PlanetSurfaceVoxelChunk> chunk);
    bool remove(const PlanetSurfaceChunkKey& key);

    [[nodiscard]] std::shared_ptr<PlanetSurfaceVoxelChunk> find(const PlanetSurfaceChunkKey& key) const;
    [[nodiscard]] bool contains(const PlanetSurfaceChunkKey& key) const;

    [[nodiscard]] std::optional<PlanetSurfaceChunkBlockInfo> voxel_at(CubemapFace face, uint8_t level,
                                                                      glm::ivec3 voxelPos) const;

    [[nodiscard]] size_t size() const { return m_chunks.size(); }

    [[nodiscard]] const std::unordered_map<PlanetSurfaceChunkKey, std::shared_ptr<PlanetSurfaceVoxelChunk>>&
    chunks() const {
        return m_chunks;
    }

    [[nodiscard]] const std::unordered_map<PlanetSurfaceChunkKey, ChunkChangeKind>& changed_chunks() const {
        return m_changedChunks;
    }

    template <class Pred> size_t erase_if(Pred&& pred) {
        m_lastChunk = nullptr;
        m_lastKey = {};
        return std::erase_if(m_chunks, [&](const auto& kv) {
            auto eval = pred(kv.first);
            if (eval) {
                m_changedChunks[kv.first] = ChunkChangeKind::Removed;
            }
            return eval;
        });
    }

    void clear_changed_chunks() { m_changedChunks.clear(); }

    void clear();

private:
    std::unordered_map<PlanetSurfaceChunkKey, std::shared_ptr<PlanetSurfaceVoxelChunk>> m_chunks;

    std::unordered_map<PlanetSurfaceChunkKey, ChunkChangeKind> m_changedChunks; // map of changed chunk at this frame

    mutable PlanetSurfaceChunkKey m_lastKey;
    mutable std::shared_ptr<PlanetSurfaceVoxelChunk> m_lastChunk;
};
} // namespace vp
