#pragma once
#include <memory>
#include <queue>
#include <chrono>

#include "VkBootstrap.h"
#include "vulkan_type.inl"
#include "GLFW/glfw3.h"
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

#include "core/resource/ResourceSystem.h"

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct RenderParameters {
    uint32_t width;
    uint32_t height;
} RenderParameters;

class VulkanBackend {
public:
    // ResourceSystem resourceSystem;

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

    bool m_windowVisible = true;

    // Resize tracking
    bool m_isResizing = false;
    std::chrono::steady_clock::time_point m_lastResizeTime;

    // Syncs
    std::vector<VkSemaphore> m_acquireImageSemaphores; // for each frame in flight
    std::vector<VkSemaphore> m_presentSemaphores; // for each swapchain image
    std::queue<nvrhi::EventQueryHandle> m_framesInFlight; // to track frames in flight
    std::vector<nvrhi::EventQueryHandle> m_queryPool;

    void init_nvrhi();

    void create_swapchain();
    void destroy_swapchain();
    void recreate_swapchain();

    void init_syncs();
};
