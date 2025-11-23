#pragma once
#include <memory>

#include "nvrhi/nvrhi.h"

struct FrameContext {
    nvrhi::CommandListHandle commandList = nullptr;
    bool frameActive = false;
};

struct Renderer {
    // Order matters for destruction! backend must be destroyed LAST
    // because other members hold NVRHI handles that need the device alive
    std::unique_ptr<struct VulkanBackend> backend;
    std::unique_ptr<struct ImGuiManager> imguiManager;
    FrameContext frameContext = {};
};

