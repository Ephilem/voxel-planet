#pragma once
#include "VulkanBackend.h"
#include "VulkanPipeline.h"

struct VulkanRenderPass {
    VkRenderPass handle;
    std::unique_ptr<VulkanPipeline> pipeline;

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
