#pragma once
#include <vulkan/vulkan_core.h>

class VulkanBackend;
class VulkanRenderPass;

class VulkanPipeline {
public:
    VkPipeline handle;
    VkPipelineLayout layout;

    VulkanPipeline(VulkanBackend* p_backend, VulkanRenderPass* p_renderPass);
    ~VulkanPipeline();


private:
    VulkanBackend *_backend;
    VulkanRenderPass *_renderPass;
};
