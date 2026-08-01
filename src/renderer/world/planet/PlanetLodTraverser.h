#pragma once
#include <glm/glm.hpp>
#include <nvrhi/nvrhi.h>

#include "PlanetLodGpuBuffers.h"
#include "planet_render_types.h"
#include "core/resource/ResourceSystem.h"
#include "renderer/vulkan/VulkanBackend.h"

namespace vp {
    struct alignas(16) PlanetLodUBO {
        /// Camera relative view projection: node positions are offset by cameraWorldPos first
        glm::mat4 viewProj{1.0f};

        glm::vec3 cameraWorldPos{0.0f};
        float planetRadius = 0.0f;

        /// Viewport size in pixels, used to turn projected sizes into a pixel count
        glm::vec2 viewportSize{1.0f};
        /// A node covering more than this many pixels on screen gets subdivided
        float subdivisionThreshold = 128.0f;
        float maxRenderDistance = 100000.0f;
    };

    /**
     * Drives the GPU side of the LOD system: one breadth first walk down the octree per frame
     *
     * The walk runs one dispatch per LOD level, ping ponging between the two scratch queues of
     * PlanetLodGpuBuffers. Only the root level is dispatched with a count the CPU knows; every
     * level below is sized by the GPU itself, through a single threaded pass that reads the node
     * count the previous level produced and writes it into the indirect dispatch arguments
     */
    class PlanetLodTraverser {
    public:
        static constexpr uint32_t TRAVERSAL_GROUP_SIZE = 64;

        /**
         * Load the shaders and build the pipelines and binding sets.
         * @param backend Vulkan backend owning the NVRHI device
         * @param resources Resource system used to load the compiled shaders
         * @param buffers Buffers to traverse, must stay alive for as long as this object
         * @param maxNodes Capacity of one traversal queue, the same value given to
         *                 PlanetLodGpuBuffers::init()
         */
        void init(VulkanBackend *backend, ResourceSystem *resources, PlanetLodGpuBuffers *buffers, uint32_t maxNodes);

        /**
         * Record a full traversal for this frame.
         *
         * Expects reset_counters() and seed_roots() to have already been recorded on the same
         * command list, and snapshot_requests() to follow it.
         *
         * @param cmd Command list to record into
         * @param frameData Camera and LOD policy for this frame
         * @param rootCount Number of roots seeded, from PlanetLodTree::root_indices().size()
         */
        void traverse(nvrhi::CommandListHandle cmd, const PlanetLodUBO &frameData, uint32_t rootCount);

    private:
        /// Push constants of planet_lod_traversal.comp.
        struct TraversalPushConstants {
            uint32_t inCountIndex;
            uint32_t outCountIndex;
            uint32_t maxQueueEntries;
        };

        /// Push constants of planet_lod_prepare.comp.
        struct PreparePushConstants {
            uint32_t readCountIndex;
            uint32_t zeroCountIndex;
            uint32_t argsOffset;
            uint32_t maxQueueEntries;
            uint32_t groupSize;
        };

        void init_uniform_buffer();
        void init_traversal_pipeline();
        void init_prepare_pipeline();

        /**
         * Record one level of the walk
         * @param cmd Command list to record into
         * @param inputQueue Index of the scratch queue holding the nodes to visit, 0 or 1
         * @param level LOD level being visited, selects the indirect arguments slot
         * @param directGroupCount Workgroup count when the CPU knows it, 0 to dispatch indirectly
         */
        void dispatch_traversal(nvrhi::CommandListHandle cmd, int inputQueue, uint32_t level,
                                uint32_t directGroupCount);

        /**
         * Record the single threaded pass that sizes the next level's dispatch
         * @param cmd Command list to record into
         * @param inputQueue Index of the queue the next level will read from, 0 or 1
         * @param level LOD level the arguments are being written for
         */
        void dispatch_prepare(nvrhi::CommandListHandle cmd, int inputQueue, uint32_t level);

        VulkanBackend *m_backend = nullptr;
        ResourceSystem *m_resourceSystem = nullptr;
        PlanetLodGpuBuffers *m_buffers = nullptr;

        uint32_t m_maxNodes = 0;

        nvrhi::BufferHandle m_uboBuffer;

        // Set 0: per frame uniforms, shared by every traversal dispatch
        nvrhi::BindingLayoutHandle m_frameBindingLayout;
        nvrhi::BindingSetHandle m_frameBindingSet;

        // Set 1: the LOD buffers. Two sets map the same two scratch queues to the in and out
        // bindings in either order, so the ping pong needs no branching in the shader
        nvrhi::BindingLayoutHandle m_traversalBindingLayout;
        nvrhi::BindingSetHandle m_traversalBindingSet[2];

        nvrhi::ShaderHandle m_traversalShader;
        nvrhi::ComputePipelineHandle m_traversalPipeline;

        nvrhi::BindingLayoutHandle m_prepareBindingLayout;
        nvrhi::BindingSetHandle m_prepareBindingSet;
        nvrhi::ShaderHandle m_prepareShader;
        nvrhi::ComputePipelineHandle m_preparePipeline;
    };
}
