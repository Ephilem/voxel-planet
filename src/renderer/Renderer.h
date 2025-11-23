#pragma once
#include <memory>

#include "nvrhi/nvrhi.h"
#include "vulkan/VulkanBackend.h"

struct FrameContext {
    nvrhi::CommandListHandle commandList;
    bool frameActive = false;
};

struct Renderer {
    std::unique_ptr<struct VulkanBackend> backend;
    std::unique_ptr<struct ImGuiManager> imguiManager;
    FrameContext frameContext = {};
};

