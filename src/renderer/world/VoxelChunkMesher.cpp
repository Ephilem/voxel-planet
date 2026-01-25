#include "VoxelChunkMesher.h"

#include "VoxelTextureManager.h"
#include "core/log/Logger.h"
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
    results.reserve(maxResults);

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

    TaskMeshingInput input;
    input.chunkCoord = pos;
    input.voxels = chunk.voxels;

    // Texture slots. Said to prepare some texture in the gpu
    for (const auto& [textureID, voxelID] : chunk.textureIDs) {
        input.textureIDs[voxelID] =
            textureManager->request_texture_slot(textureID);
    }

    enqueue(std::move(input));
    e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>();
}

void VoxelChunkMesher::poll_meshing_results_system(flecs::iter &it) {
    auto results = poll_results(16);

    auto query = it.world().query_builder<VoxelChunkMesh, const ChunkCoordinate>()
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>()
            .build();

    for (auto& result : results) {
        query.each([&result](flecs::entity e, VoxelChunkMesh &mesh, const ChunkCoordinate &coord) {
            if (coord == result.chunkCoord) {
                mesh.vertices = std::move(result.vertices);
                mesh.indices = std::move(result.indices);
                mesh.vertexCount = mesh.vertices.size();
                mesh.indexCount = mesh.indices.size();
                e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>();
            }
        });
    }

}

void VoxelChunkMesher::worker_loop(size_t id) {
    LOG_DEBUG("VoxelChunkMesher", "Worker thread {} started", id);

    while (true) {
        TaskMeshingInput input;
        {
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_taskCv.wait(lock, [this] {
                return m_stop || !m_taskQueue.empty();
            });

            if (m_stop && m_taskQueue.empty()) {
                LOG_DEBUG("VoxelChunkMesher", "Worker thread {} stopping", id);
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

    // Lambda to get voxel at (x, y, z) with bounds checking
    auto at = [&input](int x, int y, int z) -> uint8_t {
        if (x < 0 || x >= CHUNK_SIZE ||
            y < 0 || y >= CHUNK_SIZE ||
            z < 0 || z >= CHUNK_SIZE) {
            return 0;
        }
        return (*input.voxels)[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE];
    };

    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int y = 0; y < CHUNK_SIZE; y++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                uint8_t voxel = at(x, y, z);
                if (voxel == 0) continue;  // Air

                for (int face = 0; face < 6; face++) {
                    int nx = x + ((face == 0) ? -1 : (face == 1) ? 1 : 0);
                    int ny = y + ((face == 2) ? -1 : (face == 3) ? 1 : 0);
                    int nz = z + ((face == 4) ? -1 : (face == 5) ? 1 : 0);

                    bool isVisible = (at(nx, ny, nz) == 0);
                    if (!isVisible) continue;

                    static const float verts[6][4][3] = {
                        // Face 0: -X
                        {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}},
                        // Face 1: +X
                        {{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}},
                        // Face 2: -Y
                        {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}},
                        // Face 3: +Y
                        {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}},
                        // Face 4: -Z
                        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}},
                        // Face 5: +Z
                        {{0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}}
                    };

                    static const uint8_t faceUVs[4][2] = {
                        {0, 0}, {0, 1}, {1, 1}, {1, 0}
                    };

                    uint32_t textureSlot = 0;
                    auto it = input.textureIDs.find(voxel);
                    if (it != input.textureIDs.end()) {
                        textureSlot = it->second;
                    }

                    uint32_t uvOffset = (x * 73856093) ^ (y * 19349663) ^ (z * 83492791);
                    uvOffset = uvOffset % 4;

                    uint32_t baseIdx = result.vertices.size();
                    float fx = static_cast<float>(x);
                    float fy = static_cast<float>(y);
                    float fz = static_cast<float>(z);

                    for (int i = 0; i < 4; i++) {
                        TerrainVertex3d vertex;

                        vertex.x = static_cast<uint32_t>(
                            std::round((fx + verts[face][i][0]) / CHUNK_SIZE * 1023.0f));
                        vertex.y = static_cast<uint32_t>(
                            std::round((fy + verts[face][i][1]) / CHUNK_SIZE * 1023.0f));
                        vertex.z = static_cast<uint32_t>(
                            std::round((fz + verts[face][i][2]) / CHUNK_SIZE * 1023.0f));

                        int uvIdx = (uvOffset + i) % 4;
                        vertex.u = faceUVs[uvIdx][0];
                        vertex.v = faceUVs[uvIdx][1];

                        vertex.textureSlot = textureSlot;
                        vertex.faceIndex = face;

                        result.vertices.push_back(vertex);
                    }

                    result.indices.push_back(baseIdx + 0);
                    result.indices.push_back(baseIdx + 1);
                    result.indices.push_back(baseIdx + 2);

                    result.indices.push_back(baseIdx + 0);
                    result.indices.push_back(baseIdx + 2);
                    result.indices.push_back(baseIdx + 3);
                }
            }
        }
    }

    return result;
}
