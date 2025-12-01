#pragma once
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

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