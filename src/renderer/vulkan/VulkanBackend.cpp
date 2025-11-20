#include "VulkanBackend.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"

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

VulkanBackend::VulkanBackend(GLFWwindow *window) {
    // Create Vulkan instance
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("VoxelPlanet")
        .request_validation_layers(true)
        .set_debug_callback(vk_debug_callback)
        .build();

    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan instance: " + std::string(inst_ret.error().message()));
    }

    instance = inst_ret.value();

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }

    this->surface = surface;

    init_nvrhi();
    init_swapchain();
    init_renderpasses();
}

void VulkanBackend::init_nvrhi() {
    vkb::PhysicalDeviceSelector selector{ instance };
    auto physicalDevice_ret = selector.set_surface(surface)
        .select();

    if (!physicalDevice_ret) {
        throw std::runtime_error("Failed to select physical device: " + std::string(physicalDevice_ret.error().message()));
    }

    vkb::PhysicalDevice physicalDevice = physicalDevice_ret.value();

    vkb::DeviceBuilder device_builder{ physicalDevice };
    auto device_ret = device_builder.build();

    if (!device_ret) {
        throw std::runtime_error("Failed to create logical device: " + std::string(device_ret.error().message()));
    }

    vkDevice = device_ret.value();

    nvrhi::vulkan::DeviceDesc deviceDesc;
    deviceDesc.instance = instance;
    deviceDesc.physicalDevice = physicalDevice;
    deviceDesc.device = vkDevice;
    deviceDesc.graphicsQueue = vkDevice.get_queue(vkb::QueueType::graphics).value();

    this->device = nvrhi::vulkan::createDevice(deviceDesc);
}

void VulkanBackend::init_swapchain() {
    vkb::SwapchainBuilder builder{ vkDevice.physical_device, vkDevice.device, surface };
    auto swapchain_ret = builder
        .set_desired_format({ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(800, 600)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();

    if (!swapchain_ret) {
        throw std::runtime_error("Failed to create swapchain");
    }

    swapchain = swapchain_ret.value();
    auto images = swapchain.get_images();

    // Create framebuffer and depth buffers
    swapchainTextures.clear();
    swapchainTextures.resize(images->size());
    for (size_t i = 0; i < images->size(); ++i) {
        auto textureDesc = nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setWidth(swapchain.extent.width)
            .setHeight(swapchain.extent.height)
            .setFormat(nvrhi::Format::RGBA8_SNORM)
            .setDebugName("Swapchain Texture");

    }
}

void VulkanBackend::init_renderpasses() {

}


VulkanBackend::~VulkanBackend() {
}

bool VulkanBackend::begin_frame() {
}

bool VulkanBackend::end_frame() {
}

void VulkanBackend::handle_resize(uint32_t width, uint32_t height) {
}
