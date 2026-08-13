#pragma once

#include <cstdint>
#include <vector>

#include <imgui.h>
#include <nvrhi/nvrhi.h>
#include <vulkan/vulkan.h>

class VulkanBackend;

namespace vp {
/**
 * Texture RGB8 for imgui / implot
 *
 * Use NVRHI and VkDescriptorSet created by ImGui Vulkan to obtain an ImTextureId
 */
class DebugImageTexture {
public:
    DebugImageTexture() = default;
    ~DebugImageTexture();

    DebugImageTexture(const DebugImageTexture&) = delete;
    DebugImageTexture& operator=(const DebugImageTexture&) = delete;
    DebugImageTexture(DebugImageTexture&&) noexcept;
    DebugImageTexture& operator=(DebugImageTexture&&) noexcept;

    /**
     * (Re)create texture if needed and upload pixels
     *
     * Upload use a immediate command list followed by a waitForIdle
     * The method IS BLOCKING
     *
     * @param pixels RGB8 pixel data, size must be width * height * 4
     * @return true if upload is successful, false otherwise
     */
    bool upload(VulkanBackend* backend, const uint8_t* pixels, uint32_t width, uint32_t height);
    void release();

    /**
     * Test if the texture is ready to be draw
     * @return true if the texture is ready, false otherwise
     */
    bool valid() const { return m_descriptorSet != VK_NULL_HANDLE; }

    ImTextureID texture_id() const { return reinterpret_cast<ImTextureID>(m_descriptorSet); }

    uint32_t width() const { return m_width; }

    uint32_t height() const { return m_height; }

private:
    nvrhi::TextureHandle m_texture;
    nvrhi::SamplerHandle m_sampler;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    VulkanBackend* m_backend = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};
} // namespace vp