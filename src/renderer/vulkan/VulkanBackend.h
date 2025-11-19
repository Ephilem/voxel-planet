#pragma once
#include <memory>

#include "VkBootstrap.h"
#include "VulkanSwapchain.h"
#include "vulkan_type.inl"
#include "GLFW/glfw3.h"
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

#include "core/resource/ResourceSystem.h"

#define MAX_FRAMES_IN_FLIGHT 4


class VulkanBackend {
public:
    ResourceSystem resourceSystem;

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

    nvrhi::DeviceHandle nvrhiDevice;
    nvrhi::CommandListHandle commandList;
    nvrhi::GraphicsPipelineHandle graphicsPipeline;
    nvrhi::FramebufferHandle framebuffer;
    nvrhi::ShaderHandle vertexShader;
    nvrhi::ShaderHandle fragmentShader;
    nvrhi::TextureHandle depthBuffer;

    VkRenderPass imguiRenderPass;

    /**
     * @param surface Pointer to a VkSurfaceKHR created from a GLFW window
     */
    VulkanBackend(GLFWwindow* window);
    ~VulkanBackend();

    bool begin_frame();
    bool end_frame();

    void handle_resize(uint32_t width, uint32_t height);

    void update_framebuffer_for_current_image();

private:
    void init_device();
    void init_surface(GLFWwindow* window);
    void init_command_pool();
    void init_nvrhi();
    void create_nvrhi_pipeline();
    void create_nvrhi_framebuffer();
    void create_imgui_render_pass();
};
