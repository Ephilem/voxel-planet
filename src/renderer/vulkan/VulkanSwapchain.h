#pragma once
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

#include "vulkan_type.inl"

class VulkanBackend;

class VulkanSwapchain {
public:
    vkb::Swapchain swapchain;

    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;

    // Depth Image
    VkImage depthImage;
    VmaAllocation depthImageMemory;
    VkImageView depthImageView;

    VulkanSwapchain(VulkanBackend* p_backend, uint32_t width, uint32_t height);
    ~VulkanSwapchain();

    void recreate(uint32_t width, uint32_t height);
    void recreate();

    VkResult acquire_next_image_index(uint64_t timeout, VkSemaphore sem, uint32_t* image_index);

    VkSwapchainKHR get_handle() const { return swapchain.swapchain; }
    uint32_t get_width() const { return swapchain.extent.width; }
    uint32_t get_height() const { return swapchain.extent.height; }
    VkExtent2D get_extent() const { return swapchain.extent; }
    VkFramebuffer get_framebuffer(uint32_t index) const { return framebuffers[index]; }
    const std::vector<VkImage>& get_images() const { return images; }

    void create_framebuffers();
    void cleanup(bool noRecreation);
private:
    VulkanBackend* _backend;

    void create(uint32_t width, uint32_t height);

    void ensure_depth_resources();
};
