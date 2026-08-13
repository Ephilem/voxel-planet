#pragma once

#include <flecs.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

class VulkanBackend;

class ImGuiManager {
public:
    ImGuiManager() = default;
    ~ImGuiManager();

    void init(GLFWwindow* window, VulkanBackend* backend);
    void shutdown();

    void begin_frame();
    void render(VkCommandBuffer commandBuffer);

private:
    bool m_initialized = false;
    VulkanBackend* m_backend = nullptr;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};