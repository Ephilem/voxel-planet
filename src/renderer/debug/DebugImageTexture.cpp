#include "DebugImageTexture.h"

#include <imgui_impl_vulkan.h>

#include <utility>

#include "core/log/Logger.h"
#include "renderer/vulkan/VulkanBackend.h"

using namespace vp;

DebugImageTexture::~DebugImageTexture() {
    release();
}

DebugImageTexture::DebugImageTexture(DebugImageTexture &&other) noexcept
    : m_texture(std::move(other.m_texture)),
      m_sampler(std::move(other.m_sampler)),
      m_descriptorSet(other.m_descriptorSet),
      m_backend(other.m_backend),
      m_width(other.m_width),
      m_height(other.m_height) {
    other.m_descriptorSet = VK_NULL_HANDLE;
    other.m_backend = nullptr;
    other.m_width = 0;
    other.m_height = 0;
}

DebugImageTexture &DebugImageTexture::operator=(DebugImageTexture &&other) noexcept {
    if (this != &other) {
        release();

        m_texture = std::move(other.m_texture);
        m_sampler = std::move(other.m_sampler);
        m_descriptorSet = other.m_descriptorSet;
        m_backend = other.m_backend;
        m_width = other.m_width;
        m_height = other.m_height;

        other.m_descriptorSet = VK_NULL_HANDLE;
        other.m_backend = nullptr;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

void DebugImageTexture::release() {
    if (m_descriptorSet != VK_NULL_HANDLE) {
        // test if the imgui abckend still exist
        if (ImGui::GetCurrentContext() != nullptr &&
            ImGui::GetIO().BackendRendererUserData != nullptr) {
            if (m_backend && m_backend->device) {
                m_backend->device->waitForIdle();
            }
            ImGui_ImplVulkan_RemoveTexture(m_descriptorSet);
        }
        m_descriptorSet = VK_NULL_HANDLE;
    }

    m_texture = nullptr;
    m_sampler = nullptr;
    m_backend = nullptr;
    m_width = 0;
    m_height = 0;
}

bool DebugImageTexture::upload(VulkanBackend *backend, const uint8_t *pixels,
                               uint32_t width, uint32_t height) {
    if (!backend || !backend->device || !pixels || width == 0 || height == 0) {
        return false;
    }

    // recreate only if dimension change, if there is no change, we only reupload pixels
    const bool needsRecreate = !m_texture || m_width != width || m_height != height;

    if (needsRecreate) {
        release();
        m_backend = backend;

        auto textureDesc = nvrhi::TextureDesc()
                .setWidth(width)
                .setHeight(height)
                .setFormat(nvrhi::Format::RGBA8_UNORM)
                .setDimension(nvrhi::TextureDimension::Texture2D)
                .setMipLevels(1)
                .setDebugName("DebugImageTexture")
                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                .setKeepInitialState(true);

        m_texture = backend->device->createTexture(textureDesc);
        if (!m_texture) {
            LOG_ERROR("DebugImageTexture", "Failed to create texture {}x{}", width, height);
            return false;
        }

        auto samplerDesc = nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);

        m_sampler = backend->device->createSampler(samplerDesc);
        if (!m_sampler) {
            LOG_ERROR("DebugImageTexture", "Failed to create sampler");
            m_texture = nullptr;
            return false;
        }

        m_width = width;
        m_height = height;
    }

    m_backend = backend;

    nvrhi::CommandListHandle cmd = backend->device->createCommandList();
    if (!cmd) {
        LOG_ERROR("DebugImageTexture", "Failed to create command list");
        return false;
    }

    cmd->open();
    cmd->writeTexture(m_texture, 0, 0, pixels, static_cast<size_t>(width) * 4);
    cmd->close();

    backend->device->executeCommandList(cmd);
    backend->device->waitForIdle();

    if (m_descriptorSet == VK_NULL_HANDLE) {
        const auto &desc = m_texture->getDesc();

        VkImageView imageView = m_texture->getNativeView(
            nvrhi::ObjectTypes::VK_ImageView,
            desc.format,
            nvrhi::TextureSubresourceSet(0, 1, 0, 1),
            desc.dimension);

        VkSampler vkSampler = m_sampler->getNativeObject(nvrhi::ObjectTypes::VK_Sampler);

        if (imageView == VK_NULL_HANDLE || vkSampler == VK_NULL_HANDLE) {
            LOG_ERROR("DebugImageTexture", "Failed to retrieve native Vulkan handles");
            return false;
        }

        m_descriptorSet = ImGui_ImplVulkan_AddTexture(
            vkSampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (m_descriptorSet == VK_NULL_HANDLE) {
            LOG_ERROR("DebugImageTexture", "ImGui_ImplVulkan_AddTexture failed");
            return false;
        }
    }

    return true;
}
