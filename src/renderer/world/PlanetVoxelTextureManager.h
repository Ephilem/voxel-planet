#pragma once
#include <nvrhi/nvrhi.h>

#include "core/resource/asset_id.h"
#include "core/resource/ResourceSystem.h"
#include "renderer/vulkan/VulkanBackend.h"
#include <flecs.h>

struct Renderer;

#define MAX_VOXEL_TEXTURE_SLOTS 1024
#define VOXEL_TEXTURE_SIZE 32

#define VOXEL_TEXTURE_FALLBACK_SLOT 0

namespace vp {

/**
 * Manage the bindless texture array for voxel textures.
 * Fast lookup of texture slots by AssetID, and handles uploading textures to the GPU.
 *
 * Its recommanded to load all at first. But it's possible to load textures on demand, but will be costly (mipmap
 * generation)
 */
class PlanetVoxelTextureManager {
public:
    using TextureSlot = uint16_t;

    PlanetVoxelTextureManager(VulkanBackend* backend, ResourceSystem* resourceSystem);
    ~PlanetVoxelTextureManager();

    /**
     * Allocate, and mark to upload the textures to the GPU. The textures will be uploaded on the next call to
     * upload_pending()
     *
     * If the texture is already registered, the texture will be marked to be reuploaded
     */
    void register_textures(std::span<const AssetID> textures);

    [[nodiscard]] TextureSlot slot_of(const AssetID textureID);

    void upload_pending(nvrhi::ICommandList* cmd);

    nvrhi::BindingLayoutHandle get_binding_layout() const { return m_bindingLayout; }

    nvrhi::BindingSetHandle get_binding_set() const { return m_bindingSet; }

private:
    struct MipmapGenerator {
        nvrhi::ShaderHandle computeShader;
        nvrhi::ComputePipelineHandle pipeline;
        nvrhi::BindingLayoutHandle bindingLayout;
        nvrhi::SamplerHandle linearSampler;
        bool initialized = false;
    } m_mipmapGenerator;

    struct MipmapGeneratorPushConstants {
        uint32_t srcMipLevel;
        uint32_t arraySlice;
        uint32_t dstMipWidth;
        uint32_t dstMipHeight;
    } m_mipmapPushConstants;

    void init_gpu();
    void init_mipmap_generator();

    static std::array<uint32_t, VOXEL_TEXTURE_SIZE * VOXEL_TEXTURE_SIZE> generate_checkerboard_texture();

    // Uploads the checkerboard into VOXEL_TEXTURE_FALLBACK_SLOT. Called once from init_gpu()
    void upload_fallback_texture(nvrhi::ICommandList* cmd);

    void generate_mipmaps(nvrhi::ICommandList* cmd, TextureSlot textureSlot);

    std::vector<AssetID> m_slots;
    std::unordered_map<AssetID, TextureSlot> m_slotByTexture; // Map from texture ID to slot index

    uint32_t m_nextFreeSlot = VOXEL_TEXTURE_FALLBACK_SLOT + 1; // slot 0 is reserved for the fallback

    std::vector<AssetID> m_toUploadList; // list of texture data to upload

    // GPU resources
    nvrhi::TextureHandle m_textureArray;
    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingSetHandle m_bindingSet;
    nvrhi::SamplerHandle m_sampler;

    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;
};
} // namespace vp
