#pragma once
#include "client/debug/IDebugPanel.h"

class VoxelTerrainRenderer;

class VoxelBufferVisualizer : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;
    const std::string name() const override { return "Voxel Buffer Visualizer"; }
    const std::string category() const override { return "Memory"; }



private:
    void draw(VoxelTerrainRenderer* voxelRenderer);
    void draw_memory_map(VoxelTerrainRenderer* voxelRenderer, float width, float height);
    void draw_statistics(VoxelTerrainRenderer* voxelRenderer);
    void draw_fragmentation_info(VoxelTerrainRenderer* voxelRenderer);
};
