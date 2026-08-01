#pragma once
#include <memory>

#include <flecs.h>

#include "nvrhi/nvrhi.h"
#include "PlanetChunkMesher.h"
#include "PlanetLodGpuBuffers.h"
#include "PlanetLodJobs.h"
#include "PlanetLodTraverser.h"
#include "PlanetLodTree.h"
#include "core/world/planet/PlanetChunkGenerator.h"
#include "renderer/IRenderPass.h"
#include "renderer/world/VoxelBuffer.h"
#include "renderer/world/VoxelMeshUploadBatcher.h"
#include "renderer/world/VoxelTextureManager.h"
#include "renderer/rendering_components.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "core/resource/ResourceSystem.h"
#include "core/world/world_components.h"
#include "core/world/planet/planet_components.h"

namespace vp {
    struct alignas(16) SurfaceChunkOUB {
        glm::ivec4 coord; // x = u, y = v, z = alt, w = LOD level
    };

    struct alignas(16) SurfaceSurfaceUBO {
        glm::mat4 view;
        glm::mat4 projection;
        float planetRadius;
        float farPlane;
        float _pad0;
        float _pad1;

        // Anchor frame in world (planet) space.
        // Unused during the flat terrain phase, kept for when curvature comes back.
        glm::vec4 anchorX;             // tangent right
        glm::vec4 anchorY;             // up (radial). always at alt = 0
        glm::vec4 anchorZ;             // tangent forward
        glm::vec4 anchorCameraPos;     // anchor position relative to camera (FO), computed CPU-side in double

        // .x = anchorFaceU
        // .y = anchorFaceV
        // .z = face index
        // .w = planet altitude
        glm::vec4 anchorFacePos;

        // Camera position in world space, what the flat mapping offsets vertices by.
        glm::vec4 cameraWorldPos;
    };

    class PlanetSurfaceTerrainRenderer : public IRenderPass {
    public:
        /// Node capacity of the LOD tree, shared by the CPU mirror and every GPU buffer.
        static constexpr uint32_t LOD_MAX_NODES = 1u << 18;

        /// Only one VoxelBuffer is allowed for now. Growing past it needs the draw slot to also
        /// carry which buffer it lives in, which GpuNode::meshFlags is reserved for.
        static constexpr size_t MAX_CHUNK_BUFFERS = 1;

        /// Upper bound on the meshes turned into geometry in a single frame. Each one costs an
        /// allocation plus a staging copy, so draining the whole backlog at once would stall.
        static constexpr size_t MAX_MESH_RESULTS_PER_FRAME = 64;

        PlanetSurfaceTerrainRenderer() = default;
        ~PlanetSurfaceTerrainRenderer() override;

        void render(nvrhi::CommandListHandle cmd, Camera3d &camera, VulkanBackend &backend) override;

        static void Register(flecs::world &ecs);

        PlanetLodTree &lod_tree() { return m_lodTree; }

        /**
         * Camera position in world space, in the same frame the node grid lives in.
         *
         * It drives both the vertex offset and which roots stay loaded, and it cannot be derived
         * from Camera3d: the view matrix is camera relative, so its translation is zero. Feed it
         * from the spatial system every frame. Left at the origin, the roots sit at the corner of
         * the face and half the disc falls on negative coordinates, which update_roots() skips.
         */
        glm::vec3 cameraWorldPos{0.0f};

        /// A node covering more than this many pixels on screen gets subdivided.
        float lodSubdivisionThreshold = 128.0f;

    private:
        VulkanBackend *m_backend = nullptr;
        ResourceSystem *m_resourceSystem = nullptr;
        VoxelTextureManager *m_textureManager = nullptr;

        SurfaceSurfaceUBO m_ubo{};
        nvrhi::BufferHandle m_uboBuffer;

        // Set 0: per-frame (UBO)
        nvrhi::BindingLayoutHandle m_frameBindingLayout;
        nvrhi::BindingSetHandle m_frameBindingSet;

        // Set 1: per-chunk OUB (chunk coords)
        nvrhi::BindingLayoutHandle m_oubBindingLayout;
        std::vector<nvrhi::BindingSetHandle> m_oubBindingSets;

        // Set 2: face buffer
        nvrhi::BindingLayoutHandle m_faceBindingLayout;
        std::vector<nvrhi::BindingSetHandle> m_faceBindingSets;

        nvrhi::ShaderHandle m_vertexShader;
        nvrhi::ShaderHandle m_pixelShader;
        nvrhi::GraphicsPipelineHandle m_pipeline;

        std::vector<VoxelBuffer> m_chunkBuffers;
        VoxelMeshUploadBatcher m_meshUploader;

        /// Uploaded meshes indexed by draw slot. The LOD tree only ever refers to geometry by
        /// draw slot, so this is what lets a release find the face regions to give back. It
        /// takes over from the VoxelChunkMesh that used to live on each chunk entity.
        std::vector<VoxelChunkMesh> m_meshBySlot;

        // --- LOD system ---

        PlanetLodTree m_lodTree;
        std::unique_ptr<PlanetLodGpuBuffers> m_lodBuffers;
        PlanetLodTraverser m_lodTraverser;

        /// Turns the opaque tokens the worker pools carry back into node indices
        PlanetLodJobs m_lodJobs;

        /// Worker pools, owned by the ECS as singletons
        PlanetChunkGenerator *m_generator = nullptr;
        PlanetChunkMesher *m_mesher = nullptr;

        /// Mirror of the planet entity's generation config, refreshed every frame because that
        /// entity does not exist yet when the render module is imported
        PlanetGenerationConfig m_genConfig{};

        /// Turns the render queue into indirect draw commands.
        nvrhi::ShaderHandle m_emitDrawsShader;
        nvrhi::BindingLayoutHandle m_emitDrawsBindingLayout;
        nvrhi::BindingSetHandle m_emitDrawsBindingSet;
        nvrhi::ComputePipelineHandle m_emitDrawsPipeline;

        /// Drives the request readback rotation, incremented once per rendered frame.
        uint64_t m_frameIndex = 0;

        void init(flecs::world &ecs);
        void init_gpu();
        void init_lod();
        void init_emit_draws_pipeline();
        void destroy();

        VoxelBuffer &create_buffer();

        /**
         * Move a mesh into the draw slot table, which is its final home: the upload batcher
         * keeps a raw pointer into the face vector until it is flushed, and releasing the node
         * later needs the face regions back.
         * @param mesh Mesh that has just been allocated, consumed by the call
         * @return The stored mesh, the one to hand to the upload batcher
         */
        VoxelChunkMesh &remember_mesh(VoxelChunkMesh &&mesh);

        /**
         * Give a finished mesh a draw slot and queue its geometry for upload.
         * @param mesh Meshed faces, consumed by the call
         * @param coord Node the geometry belongs to, written into the per chunk OUB
         * @return Draw slot index, or NODE_INVALID_MESH when the arena is full
         */
        uint32_t upload_mesh(VoxelChunkMesh &&mesh, const PlanetNodeCoord &coord);

        /**
         * Advance every job the LOD tree has in flight: generated voxels go to the mesher,
         * finished meshes go to the geometry arena and back into the tree.
         *
         * Runs before the upload batcher is flushed, because update_lod() publishes the new
         * NODE_HAS_MESH flags in the same frame and the faces they point at have to be in the
         * batch about to be copied.
         */
        void poll_jobs();

        /**
         * Record everything the LOD system needs this frame: readback of what the GPU asked for,
         * upload of what the tree changed, then the traversal and the draw command emission.
         * @param cmd Command list to record into
         * @param camera Camera of the frame, drives the screen space LOD metric
         */
        void update_lod(nvrhi::CommandListHandle cmd, const Camera3d &camera);

        /// ImGui panel over the LOD pipeline: nodes, jobs and geometry arena occupancy
        void debug_ui();

        // Superseded by upload_mesh() and by the LOD tree. Kept for reference while the ECS
        // driven chunk path is being retired.
        // void system_upload_chunk_mesh(const Renderer *renderer, VoxelChunkMesh &mesh, const PlanetNodeCoord &coord);
        // void system_initialize_chunk_mesh(flecs::entity e, const VoxelChunk &mesh, const PlanetNodeCoord &coord);
    };
}
