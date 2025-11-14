#pragma once
#include "VulkanBackend.h"

struct VulkanRenderPass {
    VkRenderPass handle;
    VkPipelineLayout layout;
    VkPipeline pipeline;

    bool hasPrevPass;
    bool hasNextPass;

    VulkanRenderPass(VulkanBackend* backend);
    ~VulkanRenderPass();

    void use(VkCommandBuffer commandBuffer);
    void stopUse(VkCommandBuffer commandBuffer);

private:
    VulkanBackend* _backend;

    void createRenderPass();
    void createPipeline();
};
