#pragma once
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>
#include <flecs.h>

#include "core/TracyIntegration.h"
#include "core/world/world_components.h"
#include "renderer/render_types.h"

namespace vp {
    class PlanetChunkMesher {
    public:
        struct MesherTaskInput {
            /// Opaque token, handed back untouched in the matching output
            uint64_t jobId = 0;
            std::shared_ptr<const std::array<uint16_t, CHUNK_VOLUME>> voxels;
            std::unordered_map<uint8_t, uint16_t> gpuTextureIds; // convert voxel id to a gpu allocated texture id in the texture array

            float priority = 0.0f;

            inline bool operator>(const MesherTaskInput& b) const {
                return priority > b.priority;
            }
        };

        struct MesherTaskOutput {
            uint64_t jobId = 0;
            std::vector<TerrainFace3d> faces;
            bool success = false;
        };

        PlanetChunkMesher();
        ~PlanetChunkMesher();

        static void Register(flecs::world& ecs);

        /**
         * Depth of the border skirts, in voxels of the chunk's own level.
         *
         * Neighbour chunks are not available to the mesher, so the lateral borders emit no faces
         * at all. Between two chunks of the same level that is nearly free, but between two LOD
         * levels the terrain genuinely steps by up to a coarse voxel, and the gap is a hole
         * straight through the planet. A skirt hanging under the border closes it without needing
         * to know anything about the neighbour, and it is hidden inside the ground everywhere the
         * terrain happens to line up
         */
        static constexpr int SKIRT_VOXELS = 16;

        /**
         * Queue a chunk for meshing
         * @param jobId Caller side token, returned as is in the matching output
         * @param voxels Voxel data to mesh, shared with the generator so nothing is copied
         * @param gpuTextureIds Maps the chunk local texture ids to slots in the texture array
         * @param priority Higher runs first
         * @return False if the task was rejected, in which case the caller still owns the job and
         *         has to close it itself. Dropping it silently strands the node: it keeps its
         *         pending flag for good, and the subdivision waiting on it never completes
         */
        bool enqueue(uint64_t jobId,
                     std::shared_ptr<const std::array<uint16_t, CHUNK_VOLUME>> voxels,
                     std::unordered_map<uint8_t, uint16_t> gpuTextureIds,
                     float priority);

        /**
         * Take the meshes finished since the last call
         * @param maxResults Upper bound on how many results to drain this frame
         * @return Finished meshes, in no particular order
         */
        std::vector<MesherTaskOutput> poll_results(size_t maxResults);

        /// Tasks waiting for a worker, for diagnostics
        size_t queued_task_count();

    private:
        static constexpr size_t BATCH_SIZE = 4;

        struct alignas(64) MesherWorkerResult {
            std::vector<MesherTaskOutput> results;
            VOXEL_LOCKABLE(std::mutex, resultMutex);
            std::atomic<size_t> pendingCount{0};
        };

        void init(flecs::world &ecs);

        // Disabled with their bodies in the .cpp: they belonged to the ECS driven path, where a
        // chunk was an entity carrying a VoxelChunkMeshState relationship
        // void system_enqueue(flecs::iter &it);
        // void system_poll_results(flecs::iter &it);

        std::vector<std::thread> m_workerThreads;
        VOXEL_LOCKABLE(std::mutex, m_taskMutex);
        std::counting_semaphore<> m_taskSemaphore{0};

        /// Max heap on priority, kept by hand rather than in a std::priority_queue: popping a
        /// task out of one needs a const_cast on top(), because the container is only exposed as
        /// const even though the element is about to be removed
        std::vector<MesherTaskInput> m_queue;

        std::vector<std::unique_ptr<MesherWorkerResult>> m_workerResults;
        std::atomic<bool> m_stop{false};

        void worker_loop(size_t id);
        MesherTaskOutput build_mesh(const MesherTaskInput &input);

        /**
         * Hang a skirt under each of the four lateral borders of the chunk.
         *
         * @param voxels Chunk voxels
         * @param gpuTextureIds Local to gpu texture slot mapping
         * @param faces Face list to append to
         */
        static void emit_border_skirts(const std::array<uint16_t, CHUNK_VOLUME> &voxels,
                                       const std::unordered_map<uint8_t, uint16_t> &gpuTextureIds,
                                       std::vector<TerrainFace3d> &faces);


    };
}
