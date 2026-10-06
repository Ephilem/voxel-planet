#pragma once

#include "client/render/planet/voxel/PlanetSurfaceChunkBuffer.h"
#include "client/render/planet/voxel/PlanetVoxelTextureManager.h"
#include "client/render/render_settings.h"
#include "core/resource/ResourceSystem.h"
#include "core/world/planet/planet_components.h"
#include "core/world/spatial/spatial_components.h"
#include "renderer/rendering_components.h"
#include <nvrhi/nvrhi.h>

namespace vp::client
{
    class PlanetSurfaceChunkRenderer
    {
    public:
        PlanetSurfaceChunkRenderer(renderer::VulkanBackend* backend, core::ResourceSystem* res,
                                   PlanetVoxelTextureManager* textures)
            : m_backend(backend), m_resourceSystem(res), m_textures(textures)
        {
            init_gpu();
        }

        ~PlanetSurfaceChunkRenderer() = default;

        /**
     * We assume that all chunk loaded are from the same planet
     * There can't have chunk from two planet different in the buffer
     */
        void render(nvrhi::CommandListHandle cmd, const renderer::RenderView& view,
                    const core::GlobalTransform& planetCamTransform, const core::Planet& playerPlanet,
                    const RenderingPreferences& renderParam);

        /**
     * Remove from the buffer a list of chunks
     */
        void unload_chunks(std::span<const core::PlanetSurfaceChunkKey> chunks);

        /**
     * Uploads or replace the meshes of chunk in the associated buffers of the renderer.
     * An empty will automaticly release the chunk by the buffer
     *
     */
        void load_chunks(std::span<const PlanetSurfaceChunkMeshUpload> meshes);

        void upload_chunk_to_gpu(nvrhi::ICommandList* cmd);

        [[nodiscard]] const PlanetSurfaceChunkBuffer& chunk_buffer() const { return *m_chunkBuffer; }

    private:
        struct PushConstants
        {
            glm::mat4 viewProj;

            // in meter
            float startFade = 0.f;
            float endFade = 0.f;
        };

        void init_gpu();

        static PlanetSurfaceChunkAnchor compute_anchor(const core::PlanetSurfaceChunkKey& anchorKey, double radius,
                                                       const glm::dvec3& cameraRelPlanet);

        renderer::VulkanBackend* m_backend;
        core::ResourceSystem* m_resourceSystem;
        PlanetVoxelTextureManager* m_textures = nullptr;

        std::unique_ptr<PlanetSurfaceChunkBuffer> m_chunkBuffer;

        nvrhi::GraphicsPipelineHandle m_pipeline;
        nvrhi::BindingLayoutHandle m_bindingLayout;
        nvrhi::BindingSetHandle m_bindingSet;

        nvrhi::BufferHandle m_anchorBuffer;
    };
} // namespace vp::client