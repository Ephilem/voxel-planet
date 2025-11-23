#pragma once
#include <memory>

#include "debug/LogConsole.h"
#include "managers/ImGuiManager.h"
#include "nvrhi/nvrhi.h"
#include "vulkan/VulkanBackend.h"

struct FrameContext {
    nvrhi::CommandListHandle commandList;
    bool frameActive = false;
};

struct Renderer {
    std::unique_ptr<VulkanBackend> backend;
    std::unique_ptr<ImGuiManager> imguiManager;
    std::unique_ptr<LogConsole> logConsole;
    FrameContext frameContext = {};
};

