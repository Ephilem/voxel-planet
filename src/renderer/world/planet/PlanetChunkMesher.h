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
        PlanetChunkMesher();
        ~PlanetChunkMesher();

        static void Register(flecs::world& ecs);
    private:
        static constexpr size_t BATCH_SIZE = 4;

        struct MesherTaskInput {
            flecs::entity e;
            std::shared_ptr<const std::array<uint16_t, CHUNK_VOLUME>> voxels;
            std::unordered_map<uint8_t, uint16_t> gpuTextureIds; // convert voxel id to a gpu allocated texture id in the texture array

            float priority = 0.0f;
            uint32_t meshGeneration = 0;

            inline bool operator>(const MesherTaskInput& b) const {
                return priority > b.priority;
            }
        };

        struct MesherTaskOutput {
            flecs::entity e;
            std::vector<TerrainFace3d> faces;
            bool success = false;

            uint32_t meshGeneration = 0;
        };

        struct alignas(64) MesherWorkerResult {
            std::vector<MesherTaskOutput> results;
            VOXEL_LOCKABLE(std::mutex, resultMutex);
            std::atomic<size_t> pendingCount{0};
        };

        void init(flecs::world &ecs);

        void system_enqueue(flecs::iter &it);
        void system_poll_results(flecs::iter &it);

        std::vector<MesherTaskOutput> poll_results(size_t maxResults);

        std::vector<std::thread> m_workerThreads;
        VOXEL_LOCKABLE(std::mutex, m_taskMutex);
        std::counting_semaphore<> m_taskSemaphore{0};
        std::priority_queue<MesherTaskInput, std::vector<MesherTaskInput>, std::greater<>> m_queue;
        std::unordered_set<flecs::entity_t> m_pending;
        std::vector<std::unique_ptr<MesherWorkerResult>> m_workerResults;
        std::atomic<bool> m_stop{false};

        void worker_loop(size_t id);
        MesherTaskOutput build_mesh(const MesherTaskInput &input);


    };
}
