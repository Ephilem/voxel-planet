#pragma once
#include "IImGuiDebugModule.h"

class VoxelTerrainRenderer;

class VoxelBufferVisualizer : public IImGuiDebugModule {
public:
    void register_ecs(flecs::world &ecs) override;

    const char* get_name() const override { return "Voxel Buffer Visualizer"; }
    const char* get_category() const override { return "Memory"; }
    bool is_visible() const override { return m_visible; }
    void set_visible(bool visible) override { m_visible = visible; }

private:
    void draw(VoxelTerrainRenderer* voxelRenderer);
    void draw_memory_map(VoxelTerrainRenderer* voxelRenderer, float width, float height);
    void draw_statistics(VoxelTerrainRenderer* voxelRenderer);
    void draw_fragmentation_info(VoxelTerrainRenderer* voxelRenderer);

    bool m_visible = false;
};
