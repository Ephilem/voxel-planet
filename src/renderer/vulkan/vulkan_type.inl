#pragma once
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

typedef struct Vertex3d {
    glm::vec3 position;
} VulkanVertex3d;

typedef struct VulkanAllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
} VulkanAllocatedBuffer;

typedef struct VulkanRenderFrameData {
    VkFence renderFence;
    VkCommandBuffer commandBuffer;
    VulkanAllocatedBuffer uboBuffer;

    VkSemaphore imageAvailableSem;
    VkSemaphore renderFinishedSem;
} VulkanRenderFrameData;

typedef struct VulkanShaderStage {
    VkShaderModuleCreateInfo info;
    VkPipelineShaderStageCreateInfo stageInfo;
    VkShaderModule shaderModule;
} VulkanShaderStage;

typedef struct TerrainGlobalUBO {
    glm::mat4 view;
    glm::mat4 projection;

    glm::mat4 m_0;
    glm::mat4 m_1;
} TerrainGlobalUBO;