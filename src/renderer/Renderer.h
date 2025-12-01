#pragma once
#include <memory>

#include "debug/ImGuiDebugModuleManager.h"
#include "debug/ImGuiManager.h"
#include "nvrhi/nvrhi.h"
#include "world/VoxelTerrainRenderer.h"
#include "vulkan/VulkanBackend.h"

struct FrameContext {
    nvrhi::CommandListHandle commandList;
    bool frameActive = false;
};

struct Renderer {
    std::unique_ptr<VulkanBackend> backend;
    std::unique_ptr<ImGuiManager> imguiManager;
    std::unique_ptr<ImGuiDebugModuleManager> debugModuleManager;

    // TODO: set renderer in a more flexible way with like a render graph
    std::unique_ptr<VoxelTerrainRenderer> voxelTerrainRenderer;

    FrameContext frameContext = {};
};

