#pragma once
#include <memory>

#include "nvrhi/nvrhi.h"
#include "vulkan/VulkanBackend.h"

namespace vp::renderer {
struct FrameContext {
    nvrhi::CommandListHandle commandList;
    bool frameActive = false;
};

struct Renderer {
    std::unique_ptr<VulkanBackend> backend;

    FrameContext frameContext = {};
};
}