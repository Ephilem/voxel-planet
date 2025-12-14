#pragma once
#include <nvrhi/nvrhi.h>

#include "core/resource/asset_id.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "core/resource/ResourceSystem.h"
#include <flecs.h>

#define MAX_VOXEL_TEXTURE_SLOTS 1024

struct Renderer;

struct VoxelTextureSlot {
    bool uploaded = false;
    nvrhi::TextureHandle handle = nullptr;
};

/**
 * Manage the bindless texture array for voxel textures.
 * The meshes generator will request texture slots from this manager when generating chunk meshes.
 */
class VoxelTextureManager {
public:
    VoxelTextureManager(VulkanBackend* backend);
    ~VoxelTextureManager();

    /**
     * Return an texture slot index for the given texture ID.
     * If the texture is not yet uploaded, it will be marked for upload.
     * For now, if all slots are used, this will return 0.
     * @param textureID The texture asset ID
     * @return The texture slot index
     */
    uint16_t request_texture_slot(const AssetID &textureID);

    static void Register(flecs::world& ecs);

    nvrhi::BindingLayoutHandle get_binding_layout() const { return m_bindingLayout; }
    nvrhi::BindingSetHandle get_binding_set() const { return m_bindingSet; }

private:
    void init();

    /**
     * Will upload any pending textures to the GPU texture array
     */
    void upload_pending_textures_system(Renderer& renderer, ResourceSystem* resourceSys);

    std::vector<VoxelTextureSlot> m_slots;
    std::unordered_map<AssetID, uint32_t> m_textures; // Map from texture ID to slot index

    std::vector<AssetID> m_toUploadList; // List of texture data to upload

    // GPU resources
    nvrhi::TextureHandle m_textureArray;
    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingSetHandle m_bindingSet;
    nvrhi::SamplerHandle m_sampler;

    VulkanBackend* m_backend;
};