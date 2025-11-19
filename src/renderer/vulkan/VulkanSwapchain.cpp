//
// Created by raph on 15/11/2025.
//

#include "VulkanSwapchain.h"

#include "VulkanBackend.h"

VulkanSwapchain::VulkanSwapchain(VulkanBackend* p_backend, uint32_t width, uint32_t height) {
    _backend = p_backend;
    create(width, height);
}

VulkanSwapchain::~VulkanSwapchain() {
    cleanup(true);
}

void VulkanSwapchain::recreate(uint32_t width, uint32_t height) {
    if (_backend->disp.deviceWaitIdle() != VK_SUCCESS) {
        throw std::runtime_error("Failed to wait device idle!");
    }

    cleanup(false);
    create(width, height);
    // create_framebuffers(); // Not needed anymore - using NVRHI framebuffers
}

void VulkanSwapchain::recreate() {
    recreate(swapchain.extent.width, swapchain.extent.height);
}

VkResult VulkanSwapchain::acquire_next_image_index(uint64_t timeout, VkSemaphore sem, uint32_t* image_index) {
    uint32_t index = 0;
    VkResult result = _backend->disp.acquireNextImageKHR(
        swapchain.swapchain,
        timeout,
        sem,
        VK_NULL_HANDLE,
        &index
    );
    *image_index = index;
    return result;
}

void VulkanSwapchain::create(uint32_t width, uint32_t height) {

    vkb::SwapchainBuilder swapchainBuilder{_backend->device};
    auto swapchainRet = swapchainBuilder
                                        .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                                        // .set_old_swapchain(swapchain)
                                        .set_desired_extent(width, height)
                                        .build();
    if (!swapchainRet) {
        throw std::runtime_error("Failed to create swapchain: " + swapchainRet.error().message());
    }

    swapchain = swapchainRet.value();

    images = swapchain.get_images().value();
    imageViews = swapchain.get_image_views().value();
    framebuffers.resize(swapchain.image_count);

    ensure_depth_resources();
}

void VulkanSwapchain::cleanup(bool noRecreation) {

    for (auto& framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            _backend->disp.destroyFramebuffer(framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
    }

    // Destroy depth resources
    if (depthImageView != VK_NULL_HANDLE) {
        _backend->disp.destroyImageView(depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(_backend->allocator, depthImage, depthImageMemory);
        depthImage = VK_NULL_HANDLE;
        depthImageMemory = VK_NULL_HANDLE;
    }

    swapchain.destroy_image_views(imageViews);

    // TODO : Redo the swapchain because it REALLY dont know why ONLY on the final destruction, it segfault without explaination...
    if (!noRecreation) vkb::destroy_swapchain(swapchain);
}

void VulkanSwapchain::ensure_depth_resources() {
    constexpr VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent.width = swapchain.extent.width;
    depthImageInfo.extent.height = swapchain.extent.height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = depthFormat;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    vmaCreateImage(
        _backend->allocator,
        &depthImageInfo,
        &allocInfo,
        &depthImage,
        &depthImageMemory,
        nullptr);

    VkImageViewCreateInfo depthImageViewInfo{};
    depthImageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthImageViewInfo.image = depthImage;
    depthImageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthImageViewInfo.format = depthFormat;
    depthImageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthImageViewInfo.subresourceRange.baseMipLevel = 0;
    depthImageViewInfo.subresourceRange.levelCount = 1;
    depthImageViewInfo.subresourceRange.baseArrayLayer = 0;
    depthImageViewInfo.subresourceRange.layerCount = 1;

    if (_backend->disp.createImageView(&depthImageViewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view!");
    }
}

void VulkanSwapchain::create_framebuffers() {
    // This method is no longer needed - NVRHI handles framebuffer creation
    // The old Vulkan framebuffers are replaced by NVRHI framebuffers
}