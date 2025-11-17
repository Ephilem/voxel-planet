#pragma once
#include <memory>

#include "VkBootstrap.h"
#include "VulkanSwapchain.h"
#include "vulkan_type.inl"
#include "core/resource/ResourceSystem.h"
#include "GLFW/glfw3.h"

class VulkanPipeline;
class VulkanRenderPass;

#define MAX_FRAMES_IN_FLIGHT 4


class VulkanBackend {
public:
    ResourceSystem* resourceSystem;

    vkb::Instance instance;

    vkb::Device device;
    vkb::DispatchTable disp;
    VkSurfaceKHR surface;

    VmaAllocator allocator;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkCommandPool commandPool;

    uint32_t currentFrame = 0;
    uint32_t imageIndex = 0;
    std::vector<VulkanRenderFrameData> frames;

    std::unique_ptr<VulkanSwapchain> swapchain;
    std::unique_ptr<VulkanRenderPass> renderPass;
    std::unique_ptr<VulkanPipeline> pipeline;

    /**
     * @param surface Pointer to a VkSurfaceKHR created from a GLFW window
     */
    VulkanBackend(GLFWwindow* window, ResourceSystem* resourceSystem);
    ~VulkanBackend();

    bool begin_frame();
    bool end_frame();

    void handle_resize(uint32_t width, uint32_t height);

private:
    void init_device();
    void init_surface(GLFWwindow* window);
    void init_command_pool();
};
