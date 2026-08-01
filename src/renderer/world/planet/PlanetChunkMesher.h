#pragma once
#include <queue>
#include <thread>
#include <unordered_set>
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
         * Queue a chunk for meshing
         * @param jobId Caller side token, returned as is in the matching output
         * @param voxels Voxel data to mesh, shared with the generator so nothing is copied
         * @param gpuTextureIds Maps the chunk local texture ids to slots in the texture array
         * @param priority Higher runs first
         */
        void enqueue(uint64_t jobId,
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
        std::priority_queue<MesherTaskInput, std::vector<MesherTaskInput>, std::greater<>> m_queue;
        // Deduplication by entity, unused now that every job carries a fresh token
        std::unordered_set<flecs::entity_t> m_pending;
        std::vector<std::unique_ptr<MesherWorkerResult>> m_workerResults;
        std::atomic<bool> m_stop{false};

        void worker_loop(size_t id);
        MesherTaskOutput build_mesh(const MesherTaskInput &input);


    };
}
