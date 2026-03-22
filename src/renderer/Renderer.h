#pragma once
#include <memory>
#include <vector>

#include "IRenderPass.h"
#include "debug/ImGuiManager.h"
#include "nvrhi/nvrhi.h"
#include "vulkan/VulkanBackend.h"

struct FrameContext {
    nvrhi::CommandListHandle commandList;
    bool frameActive = false;
};

struct Renderer {
    std::unique_ptr<VulkanBackend> backend;

    std::unique_ptr<ImGuiManager> imguiManager;

    std::vector<std::unique_ptr<IRenderPass>> renderPasses;

    FrameContext frameContext = {};
};

