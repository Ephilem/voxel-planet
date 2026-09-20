#pragma once

#include "core/resource/ResourceSystem.h"
#include "core/world/planet/planet_components.h"
#include "core/world/spatial/spatial_components.h"
#include "PlanetSurfaceChunkBuffer.h"
#include "renderer/world/PlanetVoxelTextureManager.h"
#include <nvrhi/nvrhi.h>

namespace vp {
class PlanetSurfaceChunkRenderer {
public:
    PlanetSurfaceChunkRenderer(VulkanBackend* backend, ResourceSystem* res, PlanetVoxelTextureManager* textures)
        : m_backend(backend), m_resourceSystem(res), m_textures(textures) {
        init_gpu();
    }

    ~PlanetSurfaceChunkRenderer() = default;

    /**
     * We assume that all chunk loaded are from the same planet
     * There can't have chunk from two planet different in the buffer
     */
    void render(nvrhi::CommandListHandle cmd, Camera3d& camera, const GlobalTransform& planetCamTransform,
                const Planet& playerPlanet);

    /**
     * Remove from the buffer a list of chunks
     */
    void unload_chunks(std::span<const PlanetSurfaceChunkKey> chunks);

    /**
     * Uploads or replace the meshes of chunk in the associated buffers of the renderer.
     * An empty will automaticly release the chunk by the buffer
     *
     */
    void load_chunks(std::span<const PlanetSurfaceChunkMeshUpload> meshes);

    void upload_chunk_to_gpu(nvrhi::ICommandList* cmd);

private:
    struct PushConstants {
        glm::mat4 viewProj;
    };

    void init_gpu();

    static PlanetSurfaceChunkAnchor compute_anchor(const PlanetSurfaceChunkKey& anchorKey, double radius,
                                                   const glm::dvec3& cameraRelPlanet);

    VulkanBackend* m_backend;
    ResourceSystem* m_resourceSystem;
    PlanetVoxelTextureManager* m_textures = nullptr;

    std::unique_ptr<PlanetSurfaceChunkBuffer> m_chunkBuffer;

    nvrhi::GraphicsPipelineHandle m_pipeline;
    nvrhi::BindingLayoutHandle m_bindingLayout;
    nvrhi::BindingSetHandle m_bindingSet;

    nvrhi::BufferHandle m_anchorBuffer;
};
} // namespace vp
