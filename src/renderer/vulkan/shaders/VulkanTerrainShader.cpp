//
// Created by raph on 17/11/25.
//

#include "VulkanTerrainShader.h"

#include "renderer/vulkan/VulkanBackend.h"
#include "shader_helper.h"

VulkanTerrainShader::~VulkanTerrainShader() {
}

void VulkanTerrainShader::use(VkCommandBuffer commandBuffer) {
}

void VulkanTerrainShader::update_global_ubo() {
}

void VulkanTerrainShader::init() {
    auto resourceSystem = m_backend->resourceSystem;

    // Load Shaders
    auto vertShaderResource = resourceSystem->load<ShaderResource>("simple.vert", ResourceType::SHADER);
    auto fragShaderResource = resourceSystem->load<ShaderResource>("simple.frag", ResourceType::SHADER);

    VkShaderModule vertShaderModule;
    create_shader_module(m_backend->device, *vertShaderResource, &vertShaderModule);
    VkShaderModule fragShaderModule;
    create_shader_module(m_backend->device, *fragShaderResource, &fragShaderModule);

    VkPipelineShaderStageCreateInfo vert_shaderStage{};
    vert_shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shaderStage.module = vertShaderModule;
    vert_shaderStage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_shaderStage{};
    frag_shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shaderStage.module = fragShaderModule;
    frag_shaderStage.pName = "main";

    VkPipelineShaderStageCreateInfo t_stages[] = {vert_shaderStage, frag_shaderStage};

    // Init descriptors
    ////// GLOBAL DESCRIPTOR LAYOUT & POOL //////
    //// For data like projection matrix or view matrix
    VkDescriptorSetLayoutBinding globalUboLayoutBinding{};
    globalUboLayoutBinding.binding = 0;
    globalUboLayoutBinding.descriptorCount = 1; // only one : the global UBO
    globalUboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    globalUboLayoutBinding.pImmutableSamplers = 0;
    globalUboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // at what stage its provided

    VkDescriptorSetLayoutCreateInfo decriptorSetLayoutInfo{};
    decriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    decriptorSetLayoutInfo.bindingCount = 1;
    decriptorSetLayoutInfo.pBindings = &globalUboLayoutBinding;
    if (m_backend->disp.createDescriptorSetLayout(
            &decriptorSetLayoutInfo,
            nullptr,
            &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create terrain shader descriptor set layout.");
    }

    VkDescriptorPoolSize descriptorPoolSize{};
    descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorPoolSize.descriptorCount = m_backend->swapchain->get_image_count();

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 1; // Number of different descriptor types
    descriptorPoolInfo.pPoolSizes = &descriptorPoolSize;
    descriptorPoolInfo.maxSets = m_backend->swapchain->get_image_count(); // One descriptor set per frame
    if (m_backend->disp.createDescriptorPool(
            &descriptorPoolInfo,
            nullptr,
            &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create terrain shader descriptor pool.");
    }

    // Init UBO buffers
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_backend->device.physical_device, &memProperties);

    // Check if any memory type supports both device-local and host-visible
    bool supportsDeviceLocalHostVisible = false;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memProperties.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) ==
            (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            supportsDeviceLocalHostVisible = true;
            break;
            }
    }

    // Allocate with VMA
    uint32_t deviceLocalBits = supportsDeviceLocalHostVisible ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : 0;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(globalUbo) * 4; // enough for
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.requiredFlags = deviceLocalBits | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (vmaCreateBuffer(
            m_backend->allocator,
            &bufferInfo,
            &allocInfo,
            &globalUbosBuffer.buffer,
            &globalUbosBuffer.allocation,
            &globalUbosBuffer.info) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create terrain shader global UBO buffer.");
    }

    // Init Pipeline
    VkPipelineViewportStateCreateInfo viewportStateInfo{};
    viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateInfo.viewportCount = 1;
    viewportStateInfo.pViewports = nullptr; // Dynamic
    viewportStateInfo.scissorCount = 1;
    viewportStateInfo.pScissors = nullptr; // Dynamic

    VkPipelineRasterizationStateCreateInfo rasterizerStateInfo{};
    rasterizerStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerStateInfo.depthClampEnable = VK_FALSE;
    rasterizerStateInfo.rasterizerDiscardEnable = VK_FALSE;
    rasterizerStateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizerStateInfo.lineWidth = 1.0f;
    rasterizerStateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizerStateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizerStateInfo.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendState{};
    colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.logicOpEnable = VK_FALSE;
    colorBlendState.logicOp = VK_LOGIC_OP_COPY;
    colorBlendState.attachmentCount = 1;
    colorBlendState.pAttachments = &colorBlendAttachment;
    colorBlendState.blendConstants[0] = 0.0f;
    colorBlendState.blendConstants[1] = 0.0f;
    colorBlendState.blendConstants[2] = 0.0f;
    colorBlendState.blendConstants[3] = 0.0f;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkDynamicState t_dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = t_dynamicStates;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.offset = sizeof(glm::mat4) * 0;
    pushConstant.size = sizeof(glm::mat4) * 2;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = t_stages;
    pipelineInfo.pVertexInputState = nullptr;
    pipelineInfo.pInputAssemblyState = nullptr;







}

void VulkanTerrainShader::cleanup() {
}
