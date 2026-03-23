#pragma once

#include <chrono>

#include "VkBootstrap.h"
#include "vulkan_type.inl"
#include "GLFW/glfw3.h"
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

#ifdef TRACY_ENABLE
#include <tracy/TracyVulkan.hpp>
#endif

#include "core/resource/ResourceSystem.h"
#include "renderer/rendering_components.h"

typedef struct RenderParameters {
    uint32_t width;
    uint32_t height;
} RenderParameters;

class VulkanBackend {
public:
    // ResourceSystem resourceSystem;

#ifdef TRACY_ENABLE
    tracy::VkCtx* tracyVkCtx = nullptr;
#endif

    RenderParameters renderParameters;

    vkb::Instance instance;
    vkb::Device vkDevice;

    nvrhi::vulkan::DeviceHandle device;
    nvrhi::TextureHandle depthBuffer;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkSurfaceKHR surface;
    VkFormat swapchainFormat = VK_FORMAT_R8G8B8A8_UNORM;

    /**
     * @param surface Pointer to a VkSurfaceKHR created from a GLFW window
     */
    VulkanBackend(GLFWwindow* window, RenderParameters renderParameters);
    ~VulkanBackend();

    bool begin_frame(nvrhi::CommandListHandle &out_currentCommandList);
    bool present();
    nvrhi::FramebufferHandle get_current_framebuffer() const {
        return m_swapchainFramebuffers[m_imageIndex];
    }

    uint32_t get_current_image_index() const { return m_imageIndex; }
    uint32_t get_swapchain_image_count() const { return static_cast<uint32_t>(m_swapchainTextures.size()); }

    nvrhi::FramebufferHandle get_swapchain_framebuffer(uint32_t index) const;
    nvrhi::TextureHandle get_current_texture() const { return m_swapchainTextures[m_imageIndex]; }
    VkExtent2D get_swapchain_extent() const { return m_swapchain.extent; }

    VmaAllocator get_vma_allocator() const { return m_vmaAllocator; }

    /**
     * Index of the current frame in flight, mapped to MAX_FRAMES_IN_FLIGHT. This is used for syncing and command list management.
     * Can be used for double/triple buffering logic in the renderer, but should not be used for indexing swapchain images directly, as the swapchain image index can be different from the frame in flight index.
     * @return Index of the current frame in flight, mapped to MAX_FRAMES_IN_FLIGHT
     */
    uint32_t get_frame_in_flight_index() const { return m_commandListIndex; }

    void handle_resize(uint32_t width, uint32_t height);

private:
    // Swapchain
    bool m_swapchainDirty = false;
    vkb::Swapchain m_swapchain;
    std::vector<nvrhi::TextureHandle> m_swapchainTextures;
    std::vector<nvrhi::FramebufferHandle> m_swapchainFramebuffers;
    nvrhi::TextureHandle m_depthTexture;
    std::vector<nvrhi::CommandListHandle> m_commandLists;

    uint32_t m_imageIndex;
    uint32_t m_acquiredSemaphoreIndex = 0;
    uint32_t m_commandListIndex = 0; // mapped to MAX_FRAMES_IN_FLIGHT

    VmaAllocator m_vmaAllocator;

    bool m_windowVisible = true;

    // Resize tracking
    bool m_isResizing = false;
    std::chrono::steady_clock::time_point m_lastResizeTime;

    // Syncs
    std::vector<VkSemaphore> m_acquireImageSemaphores; // for each frame in flight
    std::vector<VkSemaphore> m_presentSemaphores; // for each swapchain image
    nvrhi::EventQueryHandle m_frameSlotQueries[MAX_FRAMES_IN_FLIGHT]; // one query per frame slot, waited in begin_frame before reuse
    std::vector<nvrhi::EventQueryHandle> m_queryPool;

    void init_nvrhi();

    void create_swapchain();
    void destroy_swapchain();
    void recreate_swapchain();

    void init_syncs();
};
