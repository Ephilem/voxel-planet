#pragma once
#include <memory>

#include "VkBootstrap.h"
#include "VulkanSwapchain.h"
#include "vulkan_type.inl"
#include "GLFW/glfw3.h"
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

#include "core/resource/ResourceSystem.h"

#define MAX_FRAMES_IN_FLIGHT 2


class VulkanBackend {
public:
    // ResourceSystem resourceSystem;
    vkb::Instance instance;
    vkb::Device vkDevice;

    nvrhi::DeviceHandle device;
    nvrhi::CommandListHandle commandList;
    nvrhi::FramebufferHandle framebuffer;
    nvrhi::TextureHandle depthBuffer;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkSurfaceKHR surface;

    // Swapchain
    vkb::Swapchain swapchain;
    std::vector<nvrhi::TextureHandle> swapchainTextures;
    std::vector<nvrhi::FramebufferHandle> swapchainFramebuffers;
    nvrhi::TextureHandle depthTexture;

    uint32_t imageIndex;

    // Syncs (per frame in the swapchain)
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;


    /**
     * @param surface Pointer to a VkSurfaceKHR created from a GLFW window
     */
    VulkanBackend(GLFWwindow* window);
    ~VulkanBackend();

    bool begin_frame();
    bool end_frame();

    void handle_resize(uint32_t width, uint32_t height);

private:
    void init_nvrhi();
    void init_swapchain();
    void init_renderpasses();
    void init_syncs();
};
