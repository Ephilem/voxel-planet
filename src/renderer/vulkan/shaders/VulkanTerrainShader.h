#pragma once
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "vk_mem_alloc.h"
#include "renderer/vulkan/VulkanPipeline.h"
#include "renderer/vulkan/vulkan_type.inl"

class VulkanTerrainShader {
public:
    std::vector<VulkanShaderStage> stages;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSet; // for each frame

    // Global UBO source of truth
    TerrainGlobalUBO globalUbo;
    // UBO buffers (for each frame)
    VulkanAllocatedBuffer globalUbosBuffer;
    std::vector<bool> globalDescriptorUpdated; // for each frame

    VulkanTerrainShader(VulkanBackend* backend) : m_backend(backend) {}
    ~VulkanTerrainShader();

    void use(VkCommandBuffer commandBuffer);

    /**
     * Update the global UBO of the current frame used (imageIndex of the swapchain)
     */
    void update_global_ubo();

private:
    VulkanBackend* m_backend;

    void init();
    void cleanup();
};
