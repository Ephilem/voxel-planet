#pragma once
#include <vector>

#include <glm/glm.hpp>

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

        PlanetTileRenderer(VulkanBackend *backend, ResourceSystem *resourceSystem)
            : m_backend(backend), m_resourceSystem(resourceSystem) {
            init_gpu();
        }

        ~PlanetTileRenderer() = default;

        /// Uploads every PlanetTileDrawList in the world, then draws one batch per planet
        void render_planets(nvrhi::CommandListHandle cmd, Camera3d &camera, flecs::world &ecs);

        [[nodiscard]] uint32_t last_instance_count() const { return m_lastInstanceCount; }

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

        VulkanBackend *m_backend = nullptr;
        ResourceSystem *m_resourceSystem = nullptr;

        // Mesh indices
        nvrhi::BufferHandle m_indexBuffer;
        uint32_t m_indexCount = 0;

        // Instances management
        std::vector<PlanetTileDrawInstance> m_instanceScratch;
        std::vector<PlanetBatch> m_batches;
        nvrhi::BufferHandle m_instanceBuffer;
        uint32_t m_lastInstanceCount = 0;

        // Pipeline
        nvrhi::GraphicsPipelineHandle m_pipeline;
        nvrhi::BindingLayoutHandle m_bindingLayout;
        nvrhi::BindingSetHandle m_bindingSet;
    };
}
