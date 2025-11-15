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

    renderPass = std::make_unique<VulkanRenderPass>(this);
    pipeline = std::make_unique<VulkanPipeline>(this, renderPass.get());

    create_depth_resources();
    create_framebuffers();

    // fences
    availableSemaphores.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    finishedSemaphore.resize(swapchain.image_count);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
    imageInFlight.resize(swapchain.image_count);

    VkSemaphoreCreateInfo semaphoreInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < swapchain.image_count; i++) {
        if (disp.createSemaphore(&semaphoreInfo, nullptr, &finishedSemaphore[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create semaphore!");
        }
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (disp.createSemaphore(&semaphoreInfo, nullptr, &availableSemaphores[i]) != VK_SUCCESS ||
            disp.createFence(&fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for a frame.");
        }
    }
}

VulkanBackend::~VulkanBackend() {
    if (device.device != VK_NULL_HANDLE) {
        if (disp.deviceWaitIdle() != VK_SUCCESS) {
            throw std::runtime_error("Failed to wait for device idle on destruction.");
        }
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (availableSemaphores.size() > i && availableSemaphores[i] != VK_NULL_HANDLE) {
            disp.destroySemaphore(availableSemaphores[i], nullptr);
        }
        if (finishedSemaphore.size() > i && finishedSemaphore[i] != VK_NULL_HANDLE) {
            disp.destroySemaphore(finishedSemaphore[i], nullptr);
        }
        if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE) {
            disp.destroyFence(inFlightFences[i], nullptr);
        }
    }

    for (auto framebuffer : framebuffers) {
        disp.destroyFramebuffer(framebuffer, nullptr);
    }

    for (size_t i = 0; i < depthImages.size(); i++) {
        disp.destroyImageView(depthImageViews[i], nullptr);
        disp.destroyImage(depthImages[i], nullptr);
        disp.freeMemory(depthImageMemories[i], nullptr);
    }

    if (commandPool != VK_NULL_HANDLE) {
        disp.destroyCommandPool(commandPool, nullptr);
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
    disp = device.make_table();

    auto graphicsQueueRet = device.get_queue(vkb::QueueType::graphics);
    if (!graphicsQueueRet) {
        throw std::runtime_error("Failed to get graphics queue.");
    }
    graphicsQueue = graphicsQueueRet.value();

    auto presentQueueRet = device.get_queue(vkb::QueueType::present);
    if (!presentQueueRet) {
        throw std::runtime_error("Failed to get present queue.");
    }
    presentQueue = presentQueueRet.value();
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

    if (disp.createCommandPool(&poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool.");
    }

    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    if (disp.allocateCommandBuffers(&allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers.");
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

bool VulkanBackend::begin_frame() {
    // 1. Attendre que le frame actuel soit disponible
    disp.waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    // 2. Acquérir la prochaine image du swapchain
    VkResult result = disp.acquireNextImageKHR(
        swapchain.swapchain,
        UINT64_MAX,
        availableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &swapchainImageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        create_swapchain();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image.");
    }

    // 3. Attendre si cette image est encore utilisée par un frame précédent
    if (imageInFlight[swapchainImageIndex] != VK_NULL_HANDLE) {
        disp.waitForFences(1, &imageInFlight[swapchainImageIndex], VK_TRUE, UINT64_MAX);
    }
    // Marquer cette image comme utilisée par le frame actuel
    imageInFlight[swapchainImageIndex] = inFlightFences[currentFrame];

    // 4. Reset la fence maintenant qu'on va commencer à travailler
    disp.resetFences(1, &inFlightFences[currentFrame]);

    // 5. Reset et commencer l'enregistrement du command buffer
    disp.resetCommandBuffer(commandBuffers[currentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;


    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapchain.extent.width;
    viewport.height = (float)swapchain.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain.extent;

    if (disp.beginCommandBuffer(commandBuffers[currentFrame], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer.");
    }

    disp.cmdSetViewport(commandBuffers[currentFrame], 0, 1, &viewport);
    disp.cmdSetScissor(commandBuffers[currentFrame], 0, 1, &scissor);

    return true;
}

bool VulkanBackend::end_frame() {
    // 1. Terminer l'enregistrement du command buffer
    if (disp.endCommandBuffer(commandBuffers[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer.");
    }

    // 2. Soumettre le command buffer à la queue graphique
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // Attendre que l'image soit disponible avant de dessiner dessus
    VkSemaphore waitSemaphores[] = { availableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

    // Signaler quand le rendu est terminé
    VkSemaphore signalSemaphores[] = { finishedSemaphore[swapchainImageIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // Soumettre avec la fence qui sera signalée quand tout est fini
    if (disp.queueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer.");
    }

    // 3. Présenter l'image à l'écran
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    // Attendre que le rendu soit fini avant de présenter
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = { swapchain.swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult result = disp.queuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        create_swapchain();
        return false;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image.");
    }

    // 4. Passer au frame suivant
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    return true;
}

void VulkanBackend::create_framebuffers() {
    swapchainImages = swapchain.get_images().value();
    swapchainImageViews = swapchain.get_image_views().value();

    framebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        VkImageView attachments[] = {
            swapchainImageViews[i],
            depthImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass->handle;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchain.extent.width;
        framebufferInfo.height = swapchain.extent.height;
        framebufferInfo.layers = 1;

        if (disp.createFramebuffer(&framebufferInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}

void VulkanBackend::create_depth_resources() {
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    depthImages.resize(swapchain.image_count);
    depthImageMemories.resize(swapchain.image_count);
    depthImageViews.resize(swapchain.image_count);

    for (size_t i = 0; i < swapchain.image_count; i++) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = swapchain.extent.width;
        imageInfo.extent.height = swapchain.extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = depthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (disp.createImage(&imageInfo, nullptr, &depthImages[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image!");
        }

        VkMemoryRequirements memRequirements;
        disp.getImageMemoryRequirements(depthImages[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = 0; // Need to find proper memory type

        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(device.physical_device, &memProperties);

        for (uint32_t j = 0; j < memProperties.memoryTypeCount; j++) {
            if ((memRequirements.memoryTypeBits & (1 << j)) &&
                (memProperties.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                allocInfo.memoryTypeIndex = j;
                break;
            }
        }

        if (disp.allocateMemory(&allocInfo, nullptr, &depthImageMemories[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate depth image memory!");
        }

        disp.bindImageMemory(depthImages[i], depthImageMemories[i], 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depthImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (disp.createImageView(&viewInfo, nullptr, &depthImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image view!");
        }
    }
}
