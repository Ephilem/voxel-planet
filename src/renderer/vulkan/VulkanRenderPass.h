#pragma once
#include <vulkan/vulkan_core.h>

class VulkanBackend;

class VulkanRenderPass {
public:
    VkRenderPass handle;
    bool hasPrevPass;
    bool hasNextPass;

    VulkanRenderPass(VulkanBackend* p_backend);
    ~VulkanRenderPass();

    void use(VkCommandBuffer commandBuffer);
    void stopUse(VkCommandBuffer commandBuffer);

private:
    VulkanBackend* _backend;
    void createRenderPass();
};