#include "ImGuiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "platform/PlatformState.h"
#include "renderer/Renderer.h"
#include "renderer/vulkan/VulkanBackend.h"

ImGuiManager::~ImGuiManager() {
    LOG_DEBUG("ImGuiManager", "Destroying ImGuiManager...");
    shutdown();
}

void ImGuiManager::init(GLFWwindow* window, VulkanBackend* backend) {
    if (m_initialized) return;
    m_backend = backend;

    VkDescriptorPoolSize poolSize[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSize));
    poolInfo.pPoolSizes = poolSize;

    if (vkCreateDescriptorPool(backend->vkDevice, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ImGui descriptor pool!");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    // Use Dynamic Rendering (Vulkan 1.3+) - no render pass needed!
    ImGui_ImplVulkan_InitInfo initInfo {};
    initInfo.Instance = backend->instance;
    initInfo.PhysicalDevice = backend->vkDevice.physical_device;
    initInfo.Device = backend->vkDevice;
    initInfo.QueueFamily = backend->vkDevice.get_queue_index(vkb::QueueType::graphics).value();
    initInfo.Queue = backend->graphicsQueue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    initInfo.ImageCount = MAX_FRAMES_IN_FLIGHT;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = nullptr;

    // Dynamic rendering setup - no VkRenderPass!
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &backend->swapchainFormat;

    ImGui_ImplVulkan_Init(&initInfo);

    m_initialized = true;
}

void ImGuiManager::shutdown() {
    if (!m_initialized) return;

    if (!m_backend->device->waitForIdle()) {
        throw std::runtime_error("Failed to wait device idle in ImGui shutdown!");
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_backend->vkDevice, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    m_initialized = false;
}

void ImGuiManager::begin_frame() {
    if (!m_initialized) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiManager::render(VkCommandBuffer commandBuffer) {
    if (!m_initialized) return;

    ImGui::Render();

    VkExtent2D extent = m_backend->get_swapchain_extent();

    // Get image view from nvrhi texture
    nvrhi::TextureHandle texture = m_backend->get_current_texture();
    const auto& desc = texture->getDesc();
    VkImageView imageView = texture->getNativeView(
        nvrhi::ObjectTypes::VK_ImageView,
        desc.format,
        nvrhi::TextureSubresourceSet(0, 1, 0, 1),
        desc.dimension
    );

    // Dynamic rendering
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // Keep existing content
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRendering(commandBuffer);
}

void ImGuiManager::Register(flecs::world& ecs) {
    auto *renderer = ecs.get_mut<Renderer>();
    auto *platform = ecs.get<PlatformState>();
    if (renderer && renderer->backend && platform && platform->window) {
        renderer->imguiManager->init(platform->window->window, renderer->backend.get());
    }

    // Start ImGui Frame (at Load so we can use ImGui in other systems)
    ecs.system<Renderer>("BeginImGuiFrameSystem")
        .kind(flecs::OnLoad)
        .each([](flecs::entity e, Renderer& renderer) {
            if (renderer.imguiManager) {
                renderer.imguiManager->begin_frame();
            }
        });


    // Renderer System
    ecs.system<Renderer>("RenderImGuiSystem")
        .kind(flecs::OnStore)
        .each([](flecs::entity e, Renderer& renderer) {
            auto& ctx = renderer.frameContext;
            if (!ctx.frameActive || !ctx.commandList) {
                // need to close the begin frame first
                ImGui::EndFrame();
                return;
            }

            VkCommandBuffer vkCmdBuf = ctx.commandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer);

            if (vkCmdBuf && renderer.imguiManager) {
                renderer.imguiManager->render(vkCmdBuf);
            }
        });
}
