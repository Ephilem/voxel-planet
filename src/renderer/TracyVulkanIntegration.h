#pragma once

#ifdef TRACY_ENABLE

#include <vulkan/vulkan.h>

#include <nvrhi/nvrhi.h>
#include <tracy/TracyVulkan.hpp>

// Two levels so __LINE__ is expanded before the paste
#define VOXEL_CONCAT_IMPL(a, b) a##b
#define VOXEL_CONCAT(a, b) VOXEL_CONCAT_IMPL(a, b)

#define VOXEL_VK_NVRHI_ZONE(ctx, cmdList, name)                                                                        \
    VkCommandBuffer VOXEL_CONCAT(_tracy_vk_cmd_, __LINE__) =                                                           \
        static_cast<VkCommandBuffer>((cmdList)->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));                \
    TracyVkZone(ctx, VOXEL_CONCAT(_tracy_vk_cmd_, __LINE__), name)

#else

#define VOXEL_VK_NVRHI_ZONE(ctx, cmdList, name)

#endif
