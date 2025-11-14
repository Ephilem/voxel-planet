#pragma once
#include "VkBootstrap.h"
#include "GLFW/glfw3.h"
#include <memory>

#define MAX_FRAMES_IN_FLIGHT 2

class RenderPassManager;

struct VulkanBackend {
    vkb::Instance instance;

    vkb::Device device;
    vkb::DispatchTable dispatch_table;
    VkSurfaceKHR surface;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    vkb::Swapchain swapchain;

    VkCommandPool commandPool;

    uint8_t currentFrame = 0;

    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;

    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> availableSemaphores;
    std::vector<VkSemaphore> finishedSemaphore;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imageInFlight;

    std::unique_ptr<RenderPassManager> renderPassManager;

    /**
     * @param surface Pointer to a VkSurfaceKHR created from a GLFW window
     */
    VulkanBackend(GLFWwindow* window);
    ~VulkanBackend();

    void begin_frame();

    void end_frame();

private:
    void init_device();
    void init_surface(GLFWwindow* window);
    void init_command_pool();
    void create_swapchain();
};
