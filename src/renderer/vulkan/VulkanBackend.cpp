#include "VulkanBackend.h"

#include <algorithm>
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

VulkanBackend::VulkanBackend(GLFWwindow *window, RenderParameters renderParameters) {
    this->renderParameters = renderParameters;

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
    create_swapchain();
    init_syncs();
}

VulkanBackend::~VulkanBackend() {
    // Wait for device to be idle before destroying resources
    if (device) {
        device->waitForIdle();
    }

    // Destroy sync objects
    for (const auto& semaphore : m_presentSemaphores) {
        vkDestroySemaphore(vkDevice.device, semaphore, nullptr);
    }
    for (const auto& semaphore : m_acquireImageSemaphores) {
        vkDestroySemaphore(vkDevice.device, semaphore, nullptr);
    }

    // Destroy swapchain
    destroy_swapchain();

    // Destroy NVRHI device
    if (device) {
        device->Release();
        device = nullptr;
    }

    // Destroy surface
    vkDestroySurfaceKHR(instance, surface, nullptr);

    // Destroy Vulkan device and instance
    destroy_device(vkDevice);
    vkDestroyInstance(instance, nullptr);
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

void VulkanBackend::create_swapchain() {
    vkb::SwapchainBuilder builder{ vkDevice.physical_device, vkDevice.device, surface };
    auto swapchain_ret = builder
        .set_desired_format({ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(renderParameters.width, renderParameters.height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();

    if (!swapchain_ret) {
        throw std::runtime_error("Failed to create swapchain");
    }

    m_swapchain = swapchain_ret.value();
    auto images = m_swapchain.get_images();

    // Create framebuffer and depth buffers
    m_swapchainTextures.clear();
    m_swapchainTextures.reserve(images->size());
    for (size_t i = 0; i < images->size(); ++i) {
        auto textureDesc = nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setWidth(m_swapchain.extent.width)
            .setHeight(m_swapchain.extent.height)
            .setFormat(nvrhi::Format::RGBA8_SNORM)
            .setIsRenderTarget(true)
            .setInitialState(nvrhi::ResourceStates::Present)
            .setKeepInitialState(true)
            .setDebugName("Swapchain Texture");

        nvrhi::TextureHandle texture = device->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image,
            nvrhi::Object(images->at(i)),
            textureDesc);

        m_swapchainTextures[i] = texture;
    }

    auto depthDesc = nvrhi::TextureDesc()
        .setDimension(nvrhi::TextureDimension::Texture2D)
        .setFormat(nvrhi::Format::D32)
        .setWidth(m_swapchain.extent.width)
        .setHeight(m_swapchain.extent.height)
        .setIsRenderTarget(true)
        .setInitialState(nvrhi::ResourceStates::DepthWrite)
        .setKeepInitialState(true)
        .setFormat(nvrhi::Format::D24S8)
        .setDebugName("Depth Texture");

    depthBuffer = device->createTexture(depthDesc);

    // Create framebuffers
    m_swapchainFramebuffers.clear();
    m_swapchainFramebuffers.reserve(images->size());
    for (const auto& colorTexture : m_swapchainTextures) {
        auto fbDesc = nvrhi::FramebufferDesc()
            .addColorAttachment(colorTexture)
            .setDepthAttachment(depthBuffer);

        m_swapchainFramebuffers.push_back(device->createFramebuffer(fbDesc));
    }
}

void VulkanBackend::destroy_swapchain() {
    if (vkDevice) {
        vkDeviceWaitIdle(vkDevice);
    }

    // destroy framebuffers
    for (auto& fb : m_swapchainFramebuffers) {
        fb->Release();
    }

    // destroy depth buffer
    if (depthBuffer) {
        depthBuffer->Release();
        depthBuffer = nullptr;
    }

    // destroy images
    for (auto& tex : m_swapchainTextures) {
        tex->Release();
    }
    m_swapchainTextures.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(vkDevice, m_swapchain, nullptr);
    }
}

void VulkanBackend::recreate_swapchain() {
    destroy_swapchain();
    create_swapchain();
}


void VulkanBackend::init_syncs() {
    // Create a present semaphore for each swapchain image
    size_t const numPresentSemaphore = m_swapchain.image_count;
    m_presentSemaphores.reserve(numPresentSemaphore);
    for (uint32_t i = 0; i < numPresentSemaphore; ++i) {
        VkSemaphoreCreateInfo createInfoSem{};
        createInfoSem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore semaphore;
        if (vkCreateSemaphore(vkDevice.device, &createInfoSem, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create present semaphore");
        }
        m_presentSemaphores.push_back(semaphore);
    }

    // Create semaphore for acquiring images, one per frame in flight or swapchain image count, whichever is larger
    size_t const numAcquiredSemaphore = std::max(m_swapchain.image_count, static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT));
    m_acquireImageSemaphores.reserve(numAcquiredSemaphore);
    for (uint32_t i = 0; i < numAcquiredSemaphore; ++i) {
        VkSemaphoreCreateInfo createInfoSem{};
        createInfoSem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore semaphore;
        if (vkCreateSemaphore(vkDevice.device, &createInfoSem, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create acquire image semaphore");
        }
        m_acquireImageSemaphores.push_back(semaphore);
    }
}

bool VulkanBackend::begin_frame() {
    const auto& semaphore = m_acquireImageSemaphores[m_acquiredSemaphoreIndex];

    VkResult result;
    int const maxAttempts = 3;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        result = vkAcquireNextImageKHR(vkDevice,
            m_swapchain,
            UINT64_MAX,
            semaphore,
            VK_NULL_HANDLE,
            &m_imageIndex);

        if ((result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) && attempt < maxAttempts) {
            VkSurfaceCapabilitiesKHR surfaceCaps;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkDevice.physical_device, surface, &surfaceCaps);

            renderParameters.width = surfaceCaps.currentExtent.width;
            renderParameters.height = surfaceCaps.currentExtent.height;

            recreate_swapchain();
        }
        else
            break;
    }

    m_acquiredSemaphoreIndex = (m_acquiredSemaphoreIndex + 1) % m_acquireImageSemaphores.size();

    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        // Schedule the wait. The actual wait operation will be submitted when the app executes any command list!
        // In the swap chain acquire flow, this line tells NVRHI: "Before executing any commands on the Graphics queue, wait for this semaphore to be signaled."
        device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
        return true;
    }

    return false;
}

bool VulkanBackend::end_frame_and_present() {
    const auto& semaphore = m_presentSemaphores[m_imageIndex];

    // This will signal the semaphore when all prior commands on the Graphics queue have completed
    device->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);

    device->executeCommandLists(nullptr, 0);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain.swapchain;
    presentInfo.pImageIndices = &m_imageIndex;
    VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (!(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)) {
        return false;
    }

    while (m_framesInFlight.size() >= MAX_FRAMES_IN_FLIGHT) {
        auto query = m_framesInFlight.front();
        m_framesInFlight.pop();

        device->waitEventQuery(query);

        m_queryPool.push_back(query);
    }

    nvrhi::EventQueryHandle query;
    if (!m_queryPool.empty()) {
        query = m_queryPool.back();
        m_queryPool.pop_back();
    }
    else {
        query = device->createEventQuery();
    }

    device->resetEventQuery(query);
    device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
    m_framesInFlight.push(query);
    return true;
}

void VulkanBackend::handle_resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        m_windowVisible = false;
        return;
    }

    m_windowVisible = true;

    if (renderParameters.width != width || renderParameters.height != height) {
        renderParameters.width = width;
        renderParameters.height = height;

        recreate_swapchain();
    }
}
