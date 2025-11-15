#pragma once
#include <memory>

#include "VkBootstrap.h"
#include "GLFW/glfw3.h"

class VulkanPipeline;
class VulkanRenderPass;

#define MAX_FRAMES_IN_FLIGHT 2


class VulkanBackend {
public:
    vkb::Instance instance;

    vkb::Device device;
    vkb::DispatchTable disp;
    VkSurfaceKHR surface;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    vkb::Swapchain swapchain;

    VkCommandPool commandPool;

    uint32_t currentFrame = 0;

    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    uint32_t swapchainImageIndex = 0;

    std::vector<VkImage> depthImages;
    std::vector<VkDeviceMemory> depthImageMemories;
    std::vector<VkImageView> depthImageViews;

    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> availableSemaphores;
    std::vector<VkSemaphore> finishedSemaphore;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imageInFlight;

    std::unique_ptr<VulkanRenderPass> renderPass;
    std::unique_ptr<VulkanPipeline> pipeline;

    /**
     * @param surface Pointer to a VkSurfaceKHR created from a GLFW window
     */
    VulkanBackend(GLFWwindow* window);
    ~VulkanBackend();

    bool begin_frame();
    void use_default_renderpass(VkCommandBuffer commandBuffer);

    bool end_frame();

private:
    void init_device();
    void init_surface(GLFWwindow* window);
    void init_command_pool();
    void create_swapchain();
    void create_depth_resources();
    void create_framebuffers();
};
