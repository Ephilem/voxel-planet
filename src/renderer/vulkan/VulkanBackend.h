#pragma once
#include <memory>
#include <queue>

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
    nvrhi::CommandListHandle commandList;
    nvrhi::TextureHandle depthBuffer;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkSurfaceKHR surface;

    /**
     * @param surface Pointer to a VkSurfaceKHR created from a GLFW window
     */
    VulkanBackend(GLFWwindow* window, RenderParameters renderParameters);
    ~VulkanBackend();

    bool begin_frame();
    bool end_frame_and_present();

    void handle_resize(uint32_t width, uint32_t height);

private:
    // Swapchain
    vkb::Swapchain m_swapchain;
    std::vector<nvrhi::TextureHandle> m_swapchainTextures;
    std::vector<nvrhi::FramebufferHandle> m_swapchainFramebuffers;
    nvrhi::TextureHandle m_depthTexture;

    uint32_t m_imageIndex;
    uint32_t m_acquiredSemaphoreIndex = 0;

    bool m_windowVisible = true;

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
