#pragma once

#include "core/world/planet/planet_components.h"
#include "renderer/world/PlanetVoxelTextureManager.h"

namespace vp {

struct PlanetVoxelRenderInfo {
    PlanetVoxelTextureManager::TextureSlot textureSlot = VOXEL_TEXTURE_FALLBACK_SLOT;
    bool visible = false;
};

class PlanetVoxelRenderTable {
public:
    PlanetVoxelRenderTable() = default;
    ~PlanetVoxelRenderTable() = default;

    /**
     * Build or rebuild the render table from the voxel registry
     *
     * Do not call this while rendering, as it can create race conditions. (Mesher threads may be reading the render
     * table while it is being rebuilt)
     */
    void build(const PlanetVoxelRegistry& registry, const PlanetVoxelTextureManager& textureManager);

    [[nodiscard]] PlanetVoxelRenderInfo get_voxel_render_info(VoxelID voxelId) const;

private:
    std::vector<PlanetVoxelRenderInfo> m_renderInfosByVoxelId; // index by VoxelID
};
} // namespace vp
