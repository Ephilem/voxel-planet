#pragma once
#include "VulkanTerrainShader.h"
#include "core/resource/resource_type.h"

void create_shader_module(VkDevice device, const ShaderResource& shaderResource, VkShaderModule* outShaderModule);
