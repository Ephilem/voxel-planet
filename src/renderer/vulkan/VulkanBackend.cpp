#include "VulkanBackend.h"

#include <algorithm>
#include <iostream>
#include <VkBootstrap.h>
#include "GLFW/glfw3.h"
#include <stdexcept>
#include <vulkan/vulkan.hpp>

VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* _) {
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

    VULKAN_HPP_DEFAULT_DISPATCHER.init(glfwGetInstanceProcAddress);

    // Create Vulkan instance
    vkb::InstanceBuilder builder;
    auto inst_ret = builder.set_app_name("VoxelPlanet")
        .request_validation_layers(true)
        .require_api_version(1, 3)
        .set_debug_callback(vk_debug_callback)
        .build();

    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan instance: " + std::string(inst_ret.error().message()));
    }

    instance = inst_ret.value();

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(instance.instance));

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }

    this->surface = surface;

    init_nvrhi();
    create_swapchain();
    init_syncs();

    m_commandLists.resize(MAX_FRAMES_IN_FLIGHT);
}

VulkanBackend::~VulkanBackend() {
    if (device) {
        device->waitForIdle();
    }

    // Drain all frames in flight - wait for all pending NVRHI queries
    while (!m_framesInFlight.empty()) {
        auto query = m_framesInFlight.front();
        m_framesInFlight.pop();
        device->waitEventQuery(query);
    }

    for (auto& query : m_queryPool) {
        query.Reset();
    }
    m_queryPool.clear();

    m_commandLists.clear();

    for (const auto& semaphore : m_presentSemaphores) {
        vkDestroySemaphore(vkDevice.device, semaphore, nullptr);
    }
    m_presentSemaphores.clear();

    for (const auto& semaphore : m_acquireImageSemaphores) {
        vkDestroySemaphore(vkDevice.device, semaphore, nullptr);
    }
    m_acquireImageSemaphores.clear();

    destroy_swapchain();

    device.Reset();

    // Destroy surface
    vkDestroySurfaceKHR(instance, surface, nullptr);

    vkb::destroy_device(vkDevice);
    vkb::destroy_instance(instance);
}

void VulkanBackend::init_nvrhi() {
    vkb::PhysicalDeviceSelector selector{ instance };
    auto physicalDevice_ret = selector
        .set_surface(surface)
        .set_minimum_version(1, 3)
        .select();

    if (!physicalDevice_ret) {
        throw std::runtime_error("Failed to select physical device: " + std::string(physicalDevice_ret.error().message()));
    }

    vkb::PhysicalDevice physicalDevice = physicalDevice_ret.value();

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{};
    timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timelineFeatures.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
    dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    auto device_ret = deviceBuilder
        .add_pNext(&timelineFeatures)
        .add_pNext(&dynamicRenderingFeatures)
        .build();

    if (!device_ret) {
        throw std::runtime_error("Failed to create logical device: " + std::string(device_ret.error().message()));
    }

    vkDevice = device_ret.value();

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(instance.instance), vk::Device(vkDevice.device));

    auto presentQueue_ret = vkDevice.get_queue(vkb::QueueType::present);
    if (!presentQueue_ret) {
        throw std::runtime_error("Failed to get present queue");
    }
    presentQueue = presentQueue_ret.value();

    auto graphicsQueue_ret = vkDevice.get_queue(vkb::QueueType::graphics);
    if (!graphicsQueue_ret) {
        throw std::runtime_error("Failed to get graphics queue");
    }
    graphicsQueue = graphicsQueue_ret.value();

    // get family graphics queue index
    auto graphicsQueueIndex_ret = vkDevice.get_queue_index(vkb::QueueType::graphics);
    if (!graphicsQueueIndex_ret) {
        throw std::runtime_error("Failed to get graphics queue index");
    }

    nvrhi::vulkan::DeviceDesc deviceDesc;
    deviceDesc.instance = instance;
    deviceDesc.physicalDevice = physicalDevice;
    deviceDesc.device = vkDevice;
    deviceDesc.graphicsQueue = graphicsQueue;
    deviceDesc.graphicsQueueIndex = graphicsQueueIndex_ret.value();

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
    swapchainFormat = m_swapchain.image_format;
    auto images = m_swapchain.get_images();

    // Create framebuffer and depth buffers
    m_swapchainTextures.clear();
    m_swapchainTextures.reserve(images->size());
    for (size_t i = 0; i < images->size(); ++i) {
        auto textureDesc = nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture2D)
            .setWidth(m_swapchain.extent.width)
            .setHeight(m_swapchain.extent.height)
            .setFormat(nvrhi::Format::RGBA8_UNORM)
            .setIsRenderTarget(true)
            .setInitialState(nvrhi::ResourceStates::Present)
            .setKeepInitialState(true)
            .setDebugName("Swapchain Texture");

        nvrhi::TextureHandle texture = device->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image,
            nvrhi::Object(images->at(i)),
            textureDesc);

        m_swapchainTextures.push_back(texture);
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
    if (device) {
        device->waitForIdle();
    }

    // Explicitly release all swapchain NVRHI handles
    for (auto& fb : m_swapchainFramebuffers) {
        fb.Reset();
    }
    m_swapchainFramebuffers.clear();

    depthBuffer.Reset();
    m_depthTexture.Reset();

    for (auto& tex : m_swapchainTextures) {
        tex.Reset();
    }
    m_swapchainTextures.clear();

    if (m_swapchain.swapchain != VK_NULL_HANDLE) {
        vkb::destroy_swapchain(m_swapchain);
        m_swapchain.swapchain = VK_NULL_HANDLE;
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

bool VulkanBackend::begin_frame(nvrhi::CommandListHandle &out_currentCommandList) {
    // Wait for the oldest frame to complete BEFORE we start reusing resources
    // This ensures command lists and ImGui buffers from frame N-2 are no longer in use
    while (m_framesInFlight.size() >= MAX_FRAMES_IN_FLIGHT) {
        auto query = m_framesInFlight.front();
        m_framesInFlight.pop();

        device->waitEventQuery(query);

        m_queryPool.push_back(query);
    }

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

    // Aquire a command list
    // Create command list for this frame slot if it doesn't exist yet
    if (!m_commandLists[m_commandListIndex]) {
        m_commandLists[m_commandListIndex] = device->createCommandList();
    }
    out_currentCommandList = m_commandLists[m_commandListIndex];
    m_commandListIndex = (m_commandListIndex + 1) % MAX_FRAMES_IN_FLIGHT;


    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        // Schedule the wait. The actual wait operation will be submitted when the app executes any command list!
        // In the swap chain acquire flow, this line tells NVRHI: "Before executing any commands on the Graphics queue, wait for this semaphore to be signaled."
        device->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
        return true;
    }

    return false;
}

bool VulkanBackend::present() {
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

    // Track this frame in flight
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

nvrhi::FramebufferHandle VulkanBackend::get_swapchain_framebuffer(uint32_t index) const {
    if (index >= m_swapchainFramebuffers.size()) return VK_NULL_HANDLE;
    return m_swapchainFramebuffers[index];
}
