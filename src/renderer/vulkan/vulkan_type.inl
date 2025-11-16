#pragma once
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>
#include <glm/vec3.hpp>

typedef struct Vertex3d {
    glm::vec3 position;
} VulkanVertex3d;

typedef struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
} VulkanAllocatedBuffer;

typedef struct VulkanRenderFrameData {
    VkFence renderFence;
    VkCommandBuffer commandBuffer;
    VulkanAllocatedBuffer uboBuffer;

    VkSemaphore imageAvailableSem;
    VkSemaphore renderFinishedSem;
} VulkanRenderFrameData;
