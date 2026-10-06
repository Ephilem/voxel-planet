#include "PlanetVoxelRenderTable.h"

namespace vp::client
{
    void PlanetVoxelRenderTable::build(const core::PlanetVoxelRegistry& registry,
                                       const PlanetVoxelTextureManager& textureManager)
    {
        m_renderInfosByVoxelId.clear();
        for (const auto& def : registry.get_all())
        {
            m_renderInfosByVoxelId.push_back(PlanetVoxelRenderInfo{
                .textureSlot = textureManager.slot_of(def.texture),
                .visible = def.opaque,
            });
            LOG_TRACE("PlanetVoxelRenderTable", "Registered voxel {} with texture slot {} ", def.name,
                      m_renderInfosByVoxelId.back().textureSlot);
        }

        LOG_DEBUG("PlanetVoxelRenderTable", "Built render table with {} entries", m_renderInfosByVoxelId.size());
    }

    [[nodiscard]] PlanetVoxelRenderInfo PlanetVoxelRenderTable::get_voxel_render_info(core::VoxelID voxelId) const
    {
        if (static_cast<uint16_t>(voxelId) >= m_renderInfosByVoxelId.size())
        {
            return PlanetVoxelRenderInfo{};
        }

        return m_renderInfosByVoxelId[static_cast<uint16_t>(voxelId)];
    }
} // namespace vp::client
