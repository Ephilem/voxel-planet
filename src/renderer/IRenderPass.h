#pragma once

#include <flecs.h>
#include <nvrhi/nvrhi.h>

struct Camera3d;
class VulkanBackend;

class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual void render(nvrhi::CommandListHandle cmd, Camera3d& camera, VulkanBackend& backend) = 0;
    virtual void on_resize(uint32_t newWidth, uint32_t newHeight) {}
};
