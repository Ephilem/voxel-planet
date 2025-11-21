#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

class VulkanBackend;

class ImGuiManager {
public:
    ImGuiManager() = default;
    ~ImGuiManager();

    void init(GLFWwindow* window, VulkanBackend* backend);
    void shutdown();

    void begin_frame();
    void end_frame(VkCommandBuffer commandBuffer);

private:
    bool m_initialized = false;
    VulkanBackend* m_backend = nullptr;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    void create_renderpass();
};