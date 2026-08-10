#pragma once
#include <vector>

#include <glm/glm.hpp>

#include "PlanetTileAtlas.h"
#include "planet_rendering_components.h"
#include "core/resource/ResourceSystem.h"
#include "renderer/IRenderPass.h"

namespace vp {
    /**
     * Draws the quadtree leaves of every planet as grid patches bent onto the sphere
     */
    class PlanetTileRenderer {
    public:
        static constexpr uint32_t GRID_RES = 33;
        static constexpr uint32_t MAX_INSTANCES = 16384;

        /// Tiles copied into the atlas per frame. One tile is 256x256x2 bytes = 128 KB, so
        /// this caps the upload at 4 MB per frame. At 8 a cold 2048 slot atlas needed 256
        /// frames, about 4 seconds, which read as the planet slowly sharpening
        static constexpr uint32_t MAX_TILE_UPLOADS_PER_FRAME = 32;

        PlanetTileRenderer(VulkanBackend *backend, ResourceSystem *resourceSystem, PlanetTileAtlas *atlas)
            : m_backend(backend), m_resourceSystem(resourceSystem), m_atlas(atlas) {
            init_gpu();
        }

        ~PlanetTileRenderer() = default;

        /// Uploads every PlanetTileDrawList in the world, then draws one batch per planet
        void render_planets(nvrhi::CommandListHandle cmd, Camera3d &camera, flecs::world &ecs);

        [[nodiscard]] uint32_t last_instance_count() const { return m_lastInstanceCount; }

        /**
         * Per-frame breakdown of how the draw list resolved against the atlas.
         *
         * exactSlots + fallbackSlots + missingSlots == last_instance_count(). A healthy steady
         * state is nearly all exact; a persistent fallback majority means generation is not
         * keeping up with the tree, and missing means the tile draws flat at height 0
         */
        struct Stats {
            uint32_t exactSlots = 0; // tile sampled its own slice
            uint32_t fallbackSlots = 0; // sampled an ancestor's slice, uvScale < 1
            uint32_t missingSlots = 0; // no slice at any level, height forced to 0
            uint32_t uploadsThisFrame = 0; // drained results handed to the atlas
            uint32_t deepestFallback = 0; // worst ancestor distance walked this frame
            uint32_t planetsDrawn = 0;
            uint32_t droppedPlanets = 0; // skipped, instance budget reached
        };

        [[nodiscard]] const Stats &stats() const { return m_stats; }

    private:
        struct PlanetTilePushConstants {
            glm::mat4 viewProj;
            glm::vec3 camPosPlanet;
            float radius = 0.f;
        };

        struct PlanetBatch {
            uint32_t firstInstance = 0;
            uint32_t instanceCount = 0;
            glm::vec3 camPosPlanet{0.f};
            float radius = 0.f;
        };

        void init_gpu();
        void generate_mesh();
        /// @return Number of levels walked up before a resident slice was found, 0 on an exact hit
        uint32_t resolve_atlas_slot(const PlanetTileKey &key, PlanetTileAtlasKey &outSlot, float &outScale, glm::vec2 &outOffset);

        VulkanBackend *m_backend = nullptr;
        PlanetTileAtlas *m_atlas = nullptr;
        ResourceSystem *m_resourceSystem = nullptr;

        // Mesh indices
        nvrhi::BufferHandle m_indexBuffer;
        uint32_t m_indexCount = 0;

        // Instances management
        std::vector<GpuPlanetTileDrawInstance> m_instanceScratch;
        std::vector<PlanetBatch> m_batches;
        nvrhi::BufferHandle m_instanceBuffer;
        uint32_t m_lastInstanceCount = 0;

        Stats m_stats;

        // Pipeline
        nvrhi::GraphicsPipelineHandle m_pipeline;
        nvrhi::BindingLayoutHandle m_bindingLayout;
        nvrhi::BindingSetHandle m_bindingSet;
    };
}
