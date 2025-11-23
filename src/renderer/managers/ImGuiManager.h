#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <flecs.h>

class VulkanBackend;

class ImGuiManager {
public:
    ImGuiManager() = default;
    ~ImGuiManager();

    void init(GLFWwindow* window, VulkanBackend* backend);
    void shutdown();

    void begin_frame();
    void render(VkCommandBuffer commandBuffer);

    static void Register(flecs::world& world);

private:
    bool m_initialized = false;
    VulkanBackend* m_backend = nullptr;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};