#pragma once

#ifdef TRACY_ENABLE
#include <nvrhi/nvrhi.h>
#include <tracy/TracyVulkan.hpp>

#define VOXEL_VK_NVRHI_ZONE(ctx, cmdList, name)                                                                        \
    VkCommandBuffer _tracy_vk_cmd_##__LINE__ =                                                                         \
        static_cast<VkCommandBuffer>((cmdList)->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));                \
    TracyVkZone(ctx, _tracy_vk_cmd_##__LINE__, name)

#define VOXEL_VK_ZONE(ctx, cmdList, name)                                                                              \
    VkCommandBuffer _tracy_vk_cmd_##__LINE__ = cmdList;                                                                \
    TracyVkZone(ctx, _tracy_vk_cmd_##__LINE__, name)

#else

#define VOXEL_VK_NVRHI_ZONE(ctx, cmdList, name)
#define VOXEL_VK_ZONE(ctx, cmdList, name)

#endif
