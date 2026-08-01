#pragma once
#include <algorithm>
#include <array>
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
#include "renderer/world/VoxelBufferDebugger.h"
#include "renderer/world/VoxelMeshUploadBatcher.h"
#include "renderer/world/VoxelTextureManager.h"
#include "renderer/rendering_components.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "core/resource/ResourceSystem.h"
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

        /**
         * Upper bound on the meshes turned into geometry in a single frame. Each one costs an
         * allocation plus a staging copy, so draining the whole backlog at once would stall.
         *
         * It is the drain rate of the whole pipeline, so it has to be read together with
         * PlanetLodTree::Config::maxJobInFlight, which is the depth of the queue feeding it.
         * Their ratio is how many frames a full queue takes to clear, and for as long as it is
         * clearing the tree accepts nothing new: every request is rearmed, the traversal emits it
         * again the next frame, and the request queue reads as permanently saturated. Raising the
         * job budget without raising this makes that worse, not better.
         */
        static constexpr size_t MAX_MESH_RESULTS_PER_FRAME = 64;

        /**
         * Frames a released mesh waits before its draw slot and face regions go back to the
         * allocator.
         *
         * Freeing on the spot is not safe: the command buffers of the frames still in flight hold
         * indirect draws that read those exact regions, and the arena hands them straight back to
         * the next upload, which then writes over geometry the GPU has not finished with. That
         * shows up as terrain flickering in and out around the player, and it gets worse the more
         * merges happen, so it peaks exactly when moving through the terrain.
         */
        static constexpr size_t GEOMETRY_RETIRE_SLOTS = MAX_FRAMES_IN_FLIGHT + 1;

        /// A subdivision open longer than this has stopped waiting on anything real. Generous on
        /// purpose: a saturated queue can legitimately hold one back for a second or two
        static constexpr uint64_t STRANDED_PENDING_FRAMES = 600;

        PlanetSurfaceTerrainRenderer() = default;
        ~PlanetSurfaceTerrainRenderer() override;

        PlanetSurfaceTerrainRenderer(const PlanetSurfaceTerrainRenderer &) = delete;
        PlanetSurfaceTerrainRenderer &operator=(const PlanetSurfaceTerrainRenderer &) = delete;
        PlanetSurfaceTerrainRenderer(PlanetSurfaceTerrainRenderer &&) = delete;
        PlanetSurfaceTerrainRenderer &operator=(PlanetSurfaceTerrainRenderer &&) = delete;

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

        /**
         * Bring up switch: stop the tree at its roots, so every root draws its own 32 voxel mesh
         * and nothing ever subdivides.
         *
         * It works by raising the threshold out of reach rather than by adding a branch in the
         * shader. The value has to clear 1e9, which is what the traversal reports for a node
         * straddling the near plane, and the camera stands inside its own root.
         */
        bool lodFreezeSubdivision = false;

        /// Radius of the root disc, in root nodes. 0 keeps the single root under the player.
        int lodRootRadius = 7;

        /// Outline every live node of the octree, coloured by level.
        bool lodDrawOctree = false;

        /// Outline only the nodes the traversal would actually draw, that is the ones with no
        /// children. Off shows the whole hierarchy, roots included.
        bool lodDrawOctreeLeavesOnly = true;

        /// Deepest level worth outlining. Level 0 nodes are 32 m, so the line count explodes.
        int lodDrawOctreeMinLevel = 2;

        /// Outline the extent of the flat cube face on the y = 0 plane.
        bool lodDrawFaceBounds = true;

        /**
         * Gap between the split and the merge thresholds, applied in the traversal shader.
         *
         * A node keeps its children while its screen size sits in [threshold / this, threshold].
         * At 1.0 a node on the boundary merges and splits on alternate frames, and every cycle
         * costs a full generate and mesh round trip.
         *
         * 2.0 is the one value with a geometric justification rather than a feel: children are
         * half the size of their parent, so it is exactly the width that guarantees a node just
         * merged cannot immediately want to split again.
         *
         * It is not free. Inside that band a node has already been given its own mesh, while its
         * children still hold theirs and none of them are drawn, so the band is the shell where
         * geometry is paid for twice.
         */
        float lodMergeHysteresis = 2.0f;

        /**
         * Margin of the CPU backstop sweep, as a multiple of the split distance.
         *
         * The shader owns the merge decision for anything it can see. This only reclaims what it
         * never judges at all: nodes behind the camera or past the render distance, which are
         * dropped before any decision is reached and would otherwise keep their children for
         * good.
         *
         * It is the knob that decides how much off screen detail stays resident, and the frustum
         * is a small part of the sphere around the player, so this dominates memory far more than
         * the subdivision threshold does. Lower is leaner, down to the floor below.
         */
        float lodCollapseBackstop = 3.0f;

        /**
         * Smallest ratio allowed between the backstop margin and the merge hysteresis.
         *
         * Both are measured in multiples of the same split distance, so they are directly
         * comparable: the shader keeps children out to hysteresis, the backstop destroys them
         * past the margin. Let the margin fall to or below the hysteresis and a band of distance
         * opens where the CPU deletes every frame what the GPU asks for again the next, which
         * regenerates and remeshes whole subtrees with the camera standing still.
         *
         * effective_backstop() enforces it, so the two sliders can never be put in that state.
         */
        static constexpr float BACKSTOP_MIN_RATIO = 1.5f;

        /// Backstop margin actually used, after the floor that keeps it clear of the hysteresis
        float effective_backstop() const {
            return std::max(lodCollapseBackstop, lodMergeHysteresis * BACKSTOP_MIN_RATIO);
        }

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

        /// Node each draw slot was filled for, same indexing as m_meshBySlot. Only the debugger
        /// reads it: the draw path gets the coordinate from the OUB instead.
        std::vector<PlanetNodeCoord> m_coordBySlot;

        /// Meshes waiting out the frames in flight before their allocation is given back, one
        /// bucket per frame of the rotation
        std::array<std::vector<VoxelChunkMesh>, GEOMETRY_RETIRE_SLOTS> m_retiringMeshes;

        VoxelBufferDebugger m_bufferDebugger;

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

        /// Nodes released by the last collapse pass, for the debug panel
        uint32_t m_lodCollapsedLastFrame = 0;

        /// Pixels a one metre object one metre away would cover, straight out of the projection.
        /// Kept from the last update_lod() so the panel can turn the thresholds, which are in
        /// pixels, into the distances in metres they actually mean
        float m_pixelScale = 0.0f;

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
         * @param coord Node it was generated for, kept for the buffer debugger
         * @return The stored mesh, the one to hand to the upload batcher
         */
        VoxelChunkMesh &remember_mesh(VoxelChunkMesh &&mesh, const PlanetNodeCoord &coord);

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
         * Take a draw slot out of service, without freeing it yet.
         *
         * The node it belonged to is already unlinked, so nothing will select it for drawing
         * again, but the frames still in flight were recorded while it was live. The allocation
         * only goes back once those have retired.
         *
         * @param meshId Draw slot to release
         */
        void retire_geometry(uint32_t meshId);

        /**
         * Give the allocator back everything retired far enough in the past to be untouched by
         * any frame the GPU could still be executing. Runs before the frame allocates anything.
         */
        void reclaim_retired_geometry();

        /**
         * Record everything the LOD system needs this frame: readback of what the GPU asked for,
         * upload of what the tree changed, then the traversal and the draw command emission.
         * @param cmd Command list to record into
         * @param camera Camera of the frame, drives the screen space LOD metric
         */
        void update_lod(nvrhi::CommandListHandle cmd, const Camera3d &camera);

        /// ImGui panel over the LOD pipeline: nodes, jobs and geometry arena occupancy
        void debug_ui();

        /// Feed the debug line buffer with the octree outlines and the face extent
        void debug_draw();

        /**
         * Outline one node, then recurse into its children.
         * @param nodeIndex Node to outline
         */
        void debug_draw_node(uint32_t nodeIndex);

        // Superseded by upload_mesh() and by the LOD tree. Kept for reference while the ECS
        // driven chunk path is being retired.
        // void system_upload_chunk_mesh(const Renderer *renderer, VoxelChunkMesh &mesh, const PlanetNodeCoord &coord);
        // void system_initialize_chunk_mesh(flecs::entity e, const VoxelChunk &mesh, const PlanetNodeCoord &coord);
    };
}
