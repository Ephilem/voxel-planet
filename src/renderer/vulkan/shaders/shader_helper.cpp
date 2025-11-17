//
// Created by raph on 17/11/25.
//

#include "shader_helper.h"

void create_shader_module(VkDevice device, const ShaderResource &shaderResource, VkShaderModule* outShaderModule) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shaderResource.get_data_size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderResource.getData());

    if (vkCreateShaderModule(device, &createInfo, nullptr, outShaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module.");
    }
}
