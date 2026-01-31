#include "VoxelChunkMesher.h"

#include "VoxelTextureManager.h"
#include "core/log/Logger.h"
#include "core/world/ChunkManager.h"
#include "renderer/rendering_components.h"


VoxelChunkMesher::~VoxelChunkMesher() {
    shutdown();
}

void VoxelChunkMesher::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_stop = true;
    }
    m_taskCv.notify_all();

    LOG_DEBUG("VoxelChunkMesher", "Shutting down {} worker threads", m_workerThreads.size());
    for (auto& thread : m_workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_workerThreads.clear();
    LOG_INFO("VoxelChunkMesher", "All worker threads shut down");
}

void VoxelChunkMesher::enqueue(TaskMeshingInput &&taskInput) {
    if (is_pending(taskInput.chunkCoord)) {
        // already pending
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_pendingCoords.insert(taskInput.chunkCoord);
        m_taskQueue.push(std::move(taskInput));
    }
    m_taskCv.notify_one();
}

std::vector<TaskMeshingOutput> VoxelChunkMesher::poll_results(size_t maxResults) {
    std::vector<TaskMeshingOutput> results = {};

    std::lock_guard<std::mutex> lock(m_taskMutex);
    while (!m_resultQueue.empty() && results.size() < maxResults) {
        TaskMeshingOutput output = std::move(m_resultQueue.front());
        m_resultQueue.pop();
        m_pendingCoords.erase(output.chunkCoord);
        results.push_back(std::move(output));
    }

    return results;
}

void VoxelChunkMesher::init(flecs::world &ecs) {
    ecs.component<VoxelChunkMeshState>()
        .add(flecs::Exclusive);

    ecs.system<const VoxelChunk, const ChunkCoordinate>("VoxelChunkMesher-EnqueueChunkBuild")
        .kind(flecs::PostUpdate)
        .with<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>()
        .with<VoxelChunkMesh>() // only chunk that have a mesh component ready to receive the data after the meshing
        .each([this](const flecs::entity e, const VoxelChunk &chunk, const ChunkCoordinate &pos) {
            enqueue_meshing_system(e, chunk, pos);
        });

    ecs.system("VoxelChunkMesher-PollMeshingResults")
        .kind(flecs::PostUpdate)
        .run([this](flecs::iter &it) {
            poll_meshing_results_system(it);
        });

    // init workers
    size_t numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);

    for (size_t i = 0; i < numThreads; i++) {
        m_workerThreads.emplace_back([this, i] { worker_loop(i); });
    }
}

void VoxelChunkMesher::Register(flecs::world &ecs) {
    ecs.emplace<VoxelChunkMesher>();
    ecs.get_mut<VoxelChunkMesher>()->init(ecs);
}

void VoxelChunkMesher::enqueue_meshing_system(flecs::entity e, const VoxelChunk &chunk, const ChunkCoordinate &pos) {
    auto* textureManager = e.world().get_mut<VoxelTextureManager>();
    auto* chunkManager = e.world().get_mut<ChunkManager>();

    // Check if we can mesh this chunk optimally
    if (!chunkManager->can_mesh(pos)) return;

    TaskMeshingInput input;
    input.chunkCoord = pos;
    input.voxels = chunk.voxels;

    // Texture slots. Said to prepare some texture in the gpu
    for (const auto& [textureID, voxelID] : chunk.textureIDs) {
        input.textureIDs[voxelID] =
            textureManager->request_texture_slot(textureID);
    }


    std::vector<std::shared_ptr<const std::array<uint8_t, CHUNK_VOLUME>>> neighborVoxels(6, nullptr);
    std::vector<flecs::entity> neighborChunkEntities = chunkManager->get_neighboring_chunks(pos);

    int neighborIndex = 0;
    for (const auto& neighborEntity : neighborChunkEntities) {
        if (neighborEntity != flecs::entity::null()) {
            auto* neighborChunk = neighborEntity.get_mut<VoxelChunk>();
            if (neighborChunk != nullptr) {
                neighborVoxels[neighborIndex] = neighborChunk->voxels;
            }
        }
        neighborIndex++;
    }

    input.neighborVoxels = std::move(neighborVoxels);

    enqueue(std::move(input));
    e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>();
}

void VoxelChunkMesher::poll_meshing_results_system(flecs::iter &it) {
    const auto* chunkManager = it.world().get<ChunkManager>();
    auto results = poll_results(256);

    auto query = it.world().query_builder<VoxelChunkMesh, const ChunkCoordinate>()
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>()
            .build();

    for (auto& result : results) {
        flecs::entity chunk = chunkManager->get_chunk_entity(result.chunkCoord);
        if (chunk == flecs::entity::null() || !chunk.has<VoxelChunkMesh>()) continue;
        auto mesh = chunk.get_mut<VoxelChunkMesh>();
        mesh->faces = std::move(result.faces);
        mesh->faceCount = static_cast<uint32_t>(mesh->faces.size());
        chunk.add<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>();
    }

}

void VoxelChunkMesher::worker_loop(size_t id) {
    while (true) {
        TaskMeshingInput input;
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_taskCv.wait(lock, [this] {
                return m_stop || !m_taskQueue.empty();
            });

            if (m_stop && m_taskQueue.empty()) {
                return;
            }

            input = std::move(m_taskQueue.front());
            m_taskQueue.pop();
            m_pendingCoords.erase(input.chunkCoord);
        }

        TaskMeshingOutput result = build_mesh(input);

        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_resultQueue.push(std::move(result));
        }
    }
}

TaskMeshingOutput VoxelChunkMesher::build_mesh(const TaskMeshingInput &input) {
    TaskMeshingOutput result;
    result.chunkCoord = input.chunkCoord;
    result.success = true;

    auto at = [&input](int x, int y, int z) -> uint8_t {
        if (0 <= x && x < CHUNK_SIZE &&
            0 <= y && y < CHUNK_SIZE &&
            0 <= z && z < CHUNK_SIZE) {
            return (*input.voxels)[x + CHUNK_SIZE * (y + CHUNK_SIZE * z)];
        }

        // Neighbor indices match ChunkManager
        // 0: +X, 1: -X, 2: +Y, 3: -Y, 4: +Z, 5: -Z
        int nx = x, ny = y, nz = z;
        int neighborIndex = -1;

        if (nx < 0) {
            neighborIndex = 1;  // -X
            nx += CHUNK_SIZE;
        } else if (nx >= CHUNK_SIZE) {
            neighborIndex = 0;  // +X
            nx -= CHUNK_SIZE;
        } else if (ny < 0) {
            neighborIndex = 3;  // -Y
            ny += CHUNK_SIZE;
        } else if (ny >= CHUNK_SIZE) {
            neighborIndex = 2;  // +Y
            ny -= CHUNK_SIZE;
        } else if (nz < 0) {
            neighborIndex = 5;  // -Z
            nz += CHUNK_SIZE;
        } else if (nz >= CHUNK_SIZE) {
            neighborIndex = 4;  // +Z
            nz -= CHUNK_SIZE;
        }

        if (neighborIndex >= 0 && neighborIndex < static_cast<int>(input.neighborVoxels.size())) {
            if (const auto& neighborVoxels = input.neighborVoxels[neighborIndex]) {
                return (*neighborVoxels)[nx + CHUNK_SIZE * (ny + CHUNK_SIZE * nz)];
            }
        }

        return 0;
    };

    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int y = 0; y < CHUNK_SIZE; y++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                uint8_t voxel = at(x, y, z);
                if (voxel == 0) continue;  // Air

                for (int faceIdx = 0; faceIdx < 6; faceIdx++) {
                    int nx = x + ((faceIdx == 0) ? -1 : (faceIdx == 1) ? 1 : 0);
                    int ny = y + ((faceIdx == 2) ? -1 : (faceIdx == 3) ? 1 : 0);
                    int nz = z + ((faceIdx == 4) ? -1 : (faceIdx == 5) ? 1 : 0);

                    bool isVisible = at(nx, ny, nz) == 0;
                    if (!isVisible) continue;

                    uint32_t textureSlot = 0;
                    auto it = input.textureIDs.find(voxel);
                    if (it != input.textureIDs.end()) {
                        textureSlot = it->second;
                    }

                    TerrainFace3d face;
                    face.x = static_cast<uint32_t>(x);
                    face.y = static_cast<uint32_t>(y);
                    face.z = static_cast<uint32_t>(z);
                    face.faceIndex = static_cast<uint32_t>(faceIdx);
                    face.textureSlot = static_cast<uint16_t>(textureSlot);

                    result.faces.push_back(face);
                }
            }
        }
    }

    return result;
}
