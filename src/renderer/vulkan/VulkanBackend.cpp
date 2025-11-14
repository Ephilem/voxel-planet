#include "VulkanBackend.h"

#include <VkBootstrap.h>
#include "GLFW/glfw3.h"
#include <stdexcept>

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data) {
    switch (message_severity) {
        default:
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            printf("%s\n", callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            printf("%s\n", callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            printf("%s\n", callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            printf("%s\n", callback_data->pMessage);
            break;
    }
    return VK_FALSE;
}


VulkanBackend::VulkanBackend(GLFWwindow* window) {
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("VoxelPlanet")
                          .request_validation_layers(true)
                          // .set_debug_callback(vk_debug_callback)
                          .use_default_debug_messenger()
                          .require_api_version(1, 3, 0)
                          .build();

    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan instance: " + inst_ret.error().message());
    }

    instance = inst_ret.value();

    init_surface(window);
    init_device();
    create_swapchain();
    init_command_pool();

    renderPass = VulkanRenderPass(this);
}

VulkanBackend::~VulkanBackend() {
    if (device.device != VK_NULL_HANDLE) {
        dispatch_table.deviceWaitIdle();
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (availableSemaphores.size() > i && availableSemaphores[i] != VK_NULL_HANDLE) {
            dispatch_table.destroySemaphore(availableSemaphores[i], nullptr);
        }
        if (finishedSemaphore.size() > i && finishedSemaphore[i] != VK_NULL_HANDLE) {
            dispatch_table.destroySemaphore(finishedSemaphore[i], nullptr);
        }
        if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE) {
            dispatch_table.destroyFence(inFlightFences[i], nullptr);
        }
    }

    if (commanPool != VK_NULL_HANDLE) {
        dispatch_table.destroyCommandPool(commanPool, nullptr);
    }

    vkb::destroy_swapchain(swapchain);
    vkb::destroy_device(device);
    vkb::destroy_surface(instance, surface);
    vkb::destroy_instance(instance);
}


void VulkanBackend::init_device() {
    vkb::PhysicalDeviceSelector selector{instance};
    auto phys_ret = selector.set_minimum_version(1, 3)
                           .set_surface(surface)
                           .select();
    if (!phys_ret) {
        throw std::runtime_error("Failed to select physical device: " + phys_ret.error().message());
    }

    vkb::PhysicalDevice physicalDevice = phys_ret.value();
    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    auto dev_ret = deviceBuilder.build();
    if (!dev_ret) {
        throw std::runtime_error("Failed to create logical device: " + dev_ret.error().message());
    }

    device = dev_ret.value();
    dispatch_table = device.make_table();
}

void VulkanBackend::init_surface(GLFWwindow* window) {
    if (glfwCreateWindowSurface(instance.instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface.");
    }
}

void VulkanBackend::init_command_pool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = device.get_queue_index(vkb::QueueType::graphics).value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (dispatch_table.createCommandPool(&poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool.");
    }
}


void VulkanBackend::create_swapchain() {
    vkb::SwapchainBuilder swapchainBuilder{device};
    auto swapchain_ret = swapchainBuilder
                                        .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
                                        .build();

    if (!swapchain_ret) {
        throw std::runtime_error("Failed to create swapchain: " + swapchain_ret.error().message());
    }

    vkb::destroy_swapchain(swapchain);
    swapchain = swapchain_ret.value();
}

void VulkanBackend::begin_frame() {
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    dispatch_table.waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
}

void VulkanBackend::end_frame() {

}
