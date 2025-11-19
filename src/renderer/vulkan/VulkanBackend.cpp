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


VulkanBackend::VulkanBackend(GLFWwindow* window, ResourceSystem resourceSystem) {
    this->resourceSystem = resourceSystem;
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

    frames.resize(MAX_FRAMES_IN_FLIGHT);


    init_surface(window);
    init_device();

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = device.physical_device;
    allocatorInfo.device = device.device;
    allocatorInfo.instance = instance.instance;
    vmaCreateAllocator(&allocatorInfo, &allocator);

    init_nvrhi();

    swapchain = std::make_unique<VulkanSwapchain>(this, 1280, 720);

    create_nvrhi_pipeline();
    create_nvrhi_framebuffer();
    create_imgui_render_pass();

    init_command_pool();

    VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo semaphoreInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (disp.createFence(&fenceInfo, nullptr, &frames[i].renderFence) != VK_SUCCESS ||
            disp.createSemaphore(&semaphoreInfo, nullptr, &frames[i].imageAvailableSem) != VK_SUCCESS ||
            disp.createSemaphore(&semaphoreInfo, nullptr, &frames[i].renderFinishedSem) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create sync objects for frame " + std::to_string(i));
        }
    }
}

VulkanBackend::~VulkanBackend() {
    if (device.device != VK_NULL_HANDLE) {
        if (disp.deviceWaitIdle() != VK_SUCCESS) {
            throw std::runtime_error("Failed to wait for device idle on destruction.");
        }
    }

    for (auto& frame : frames) {
        disp.destroyFence(frame.renderFence, nullptr);
        disp.destroySemaphore(frame.imageAvailableSem, nullptr);
        disp.destroySemaphore(frame.renderFinishedSem, nullptr);
    }

    if (commandPool != VK_NULL_HANDLE) {
        disp.destroyCommandPool(commandPool, nullptr);
    }

    if (nvrhiDevice) {
        graphicsPipeline.Reset();
        framebuffer.Reset();
        vertexShader.Reset();
        fragmentShader.Reset();
        depthBuffer.Reset();
        commandList.Reset();
        nvrhiDevice.Reset();
    }

    if (imguiRenderPass != VK_NULL_HANDLE) {
        disp.destroyRenderPass(imguiRenderPass, nullptr);
    }

    swapchain.reset();

    if (allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }

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

    for (auto& frame : frames) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (disp.allocateCommandBuffers(&allocInfo, &frame.commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffer");
        }
    }
}

bool VulkanBackend::begin_frame() {
    auto& frame = frames[currentFrame];
    // 1. Wait for fence to be signaled from previous frame
    if (disp.waitForFences(1, &frame.renderFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return false;
    }

    // 2. Get the next image from the swapchain
    VkResult result = swapchain->acquire_next_image_index(UINT64_MAX, frame.imageAvailableSem, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain->recreate();
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image.");
    }

    // 3. Reset the fence for this frame
    disp.resetFences(1, &frame.renderFence);

    // 5. Reset et commencer l'enregistrement du command buffer
    disp.resetCommandBuffer(frame.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;


    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain->get_width());
    viewport.height = static_cast<float>(swapchain->get_height());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain->get_extent();

    if (disp.beginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer.");
    }

    disp.cmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
    disp.cmdSetScissor(frame.commandBuffer, 0, 1, &scissor);

    return true;
}

bool VulkanBackend::end_frame() {
    auto& frame = frames[currentFrame];
    // 1. Finish recording the command buffer
    if (disp.endCommandBuffer(frame.commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer.");
    }

    // 2. Send the command buffer to the graphics queue
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { frame.imageAvailableSem };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;

    // Signal semaphore when rendering is finished
    VkSemaphore signalSemaphores[] = { frame.renderFinishedSem };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // Send with the fence to signal when rendering is finished
    if (disp.queueSubmit(graphicsQueue, 1, &submitInfo, frame.renderFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer.");
    }

    // 3. Present the swapchain image
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    // Wait for rendering to finish
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = { swapchain->get_handle() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = disp.queuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchain->recreate();
        return false;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image.");
    }

    // 4. Passer au frame suivant
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    return true;
}

void VulkanBackend::handle_resize(uint32_t width, uint32_t height) {
    swapchain->recreate(width, height);
}

void VulkanBackend::init_nvrhi() {
    nvrhi::vulkan::DeviceDesc deviceDesc;
    deviceDesc.instance = instance.instance;
    deviceDesc.physicalDevice = device.physical_device;
    deviceDesc.device = device;
    deviceDesc.graphicsQueue = graphicsQueue;

    nvrhiDevice = nvrhi::vulkan::createDevice(deviceDesc);
    commandList = nvrhiDevice->createCommandList();
}

void VulkanBackend::create_nvrhi_pipeline() {
    // Create vertex shader
    nvrhi::ShaderDesc vertexShaderDesc;
    vertexShaderDesc.shaderType = nvrhi::ShaderType::Vertex;
    vertexShaderDesc.debugName = "simple.vert";
    auto& vert_binary =
    vertexShader = nvrhiDevice->createShader("shaders/simple.vert.spv", vertexShaderDesc);

    // Create fragment shader
    nvrhi::ShaderDesc fragmentShaderDesc;
    fragmentShaderDesc.shaderType = nvrhi::ShaderType::Fragment;
    fragmentShaderDesc.debugName = "simple.frag";
    fragmentShader = nvrhiDevice->createShaderFromFile("shaders/simple.frag.spv", fragmentShaderDesc);

    // Create graphics pipeline
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.VS = vertexShader;
    pipelineDesc.PS = fragmentShader;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
    pipelineDesc.renderState.depthStencilState.depthWriteEnable = true;
    pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
    pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pipelineDesc.renderState.rasterState.fillMode = nvrhi::RasterFillMode::Solid;

    graphicsPipeline = nvrhiDevice->createGraphicsPipeline(pipelineDesc);
}

void VulkanBackend::create_nvrhi_framebuffer() {
    // Create depth buffer
    nvrhi::TextureDesc depthDesc;
    depthDesc.width = swapchain->get_width();
    depthDesc.height = swapchain->get_height();
    depthDesc.format = nvrhi::Format::D24_UNORM_S8_UINT;
    depthDesc.debugName = "DepthBuffer";
    depthDesc.initialState = nvrhi::ResourceStates::DepthWrite;
    depthDesc.keepInitialState = true;
    depthDesc.isRenderTarget = true;

    depthBuffer = nvrhiDevice->createTexture(depthDesc);

    // Create framebuffer using swapchain images
    // Note: This will be updated each frame with the current swapchain image
    nvrhi::FramebufferDesc framebufferDesc;
    framebufferDesc.addColorAttachment(nullptr); // Will be set per frame
    framebufferDesc.setDepthAttachment(depthBuffer);

    framebuffer = nvrhiDevice->createFramebuffer(framebufferDesc);
}

void VulkanBackend::update_framebuffer_for_current_image() {
    // Wrap the current swapchain image as an NVRHI texture
    nvrhi::vulkan::TextureDesc swapchainImageDesc;
    swapchainImageDesc.vkImage = swapchain->get_images()[imageIndex];
    swapchainImageDesc.vkFormat = static_cast<VkFormat>(swapchain->swapchain.image_format);
    swapchainImageDesc.width = swapchain->get_width();
    swapchainImageDesc.height = swapchain->get_height();
    swapchainImageDesc.debugName = "SwapchainImage";

    auto swapchainTexture = nvrhiDevice->createTexture(swapchainImageDesc);

    // Create a new framebuffer with the current swapchain image
    nvrhi::FramebufferDesc framebufferDesc;
    framebufferDesc.addColorAttachment(swapchainTexture);
    framebufferDesc.setDepthAttachment(depthBuffer);

    framebuffer = nvrhiDevice->createFramebuffer(framebufferDesc);
}

void VulkanBackend::create_imgui_render_pass() {
    // Create a minimal render pass for ImGui compatibility
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchain->swapchain.image_format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Load existing content
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (disp.createRenderPass(&renderPassInfo, nullptr, &imguiRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui render pass!");
    }
}

