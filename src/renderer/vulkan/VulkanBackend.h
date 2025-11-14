#pragma once
#include "VkBootstrap.h"
#include "GLFW/glfw3.h"
#include <memory>

#include "VulkanRenderPass.h"

#define MAX_FRAMES_IN_FLIGHT 2

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

    VulkanRenderPass renderPass;

    /**
     * @param surface Pointer to a VkSurfaceKHR created from a GLFW window
     */
    VulkanBackend(GLFWwindow* window);
    ~VulkanBackend();

    void begin_frame();
    void use_default_renderpass(VkCommandBuffer commandBuffer);
    void end_frame();

private:
    void init_device();
    void init_surface(GLFWwindow* window);
    void init_command_pool();
    void create_swapchain();
};
