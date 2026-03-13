#include "VoxelChunkMesher.h"

#include <bit>
#include <imgui.h>

#include "core/TracyIntegration.h"

#include "VoxelTextureManager.h"
#include "core/log/Logger.h"
#include "core/world/ChunkManager.h"
#include "renderer/rendering_components.h"


VoxelChunkMesher::~VoxelChunkMesher() {
    shutdown();
}

void VoxelChunkMesher::shutdown() { {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_stop = true;
    }
    m_taskCv.notify_all();

    LOG_DEBUG("VoxelChunkMesher", "Shutting down {} worker threads", m_workerThreads.size());
    for (auto &thread: m_workerThreads) {
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
    } {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_pendingCoords.insert(taskInput.chunkCoord);
        m_taskQueue.push(std::move(taskInput));
    }
    m_taskCv.notify_one();
}

std::vector<TaskMeshingOutput> VoxelChunkMesher::poll_results(size_t maxResults) {
    std::vector<TaskMeshingOutput> results; {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        while (!m_resultQueue.empty() && results.size() < maxResults) {
            results.push_back(std::move(m_resultQueue.front()));
            m_resultQueue.pop();
        }
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

    ecs.system("VoxelChunkMesher-DebugInfo")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                ImGui::Begin("Voxel Chunk Mesher");
                ImGui::Text("Pending Tasks: %zu", pending_count());
                ImGui::Text("Completed Results: %zu", completed_count());
                ImGui::End();
            });

    ecs.system<Camera3d, const Position, const Orientation>("VoxelChunkMesher-UpdateFrustum")
            .kind(flecs::PreUpdate)
            .each([this](flecs::entity e, Camera3d &camera, const Position &pos, const Orientation &orient) {
                glm::mat4 viewProjection = camera.projectionMatrix * camera.viewMatrix;
                glm::vec3 viewDir = orient.forward();
                glm::vec3 cameraPos = {pos.x, pos.y, pos.z};

                this->update_frustum(viewProjection, cameraPos, viewDir);
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
    VOXEL_ZONE_N("EnqueueMeshing");
    auto* textureManager = e.world().get_mut<VoxelTextureManager>();
    auto* chunkManager = e.world().get_mut<ChunkManager>();

    // Check if we can mesh this chunk optimally
    if (!chunkManager->can_mesh(pos)) return;

    TaskMeshingInput input;
    input.chunkCoord = pos;
    input.voxels = chunk.voxels;

    input.priority = calculate_task_priority(pos);

    // Bump generation so any in-flight task result for this chunk is discarded on arrival
    auto* mesh = e.get_mut<VoxelChunkMesh>();
    mesh->meshGeneration++;
    input.meshGeneration = mesh->meshGeneration;

    // Texture slots. Said to prepare some texture in the gpu
    for (const auto &[textureID, voxelID]: chunk.textureIDs) {
        input.textureIDs[voxelID] =
                textureManager->request_texture_slot(textureID);
    }


    std::array<std::shared_ptr<const std::array<uint8_t, CHUNK_VOLUME>>, 6> neighborVoxels = {};
    std::array<flecs::entity, 6> neighborChunkEntities = chunkManager->get_neighboring_chunks(pos);

    int neighborIndex = 0;
    int presentNeighbors = 0;
    for (const auto &neighborEntity: neighborChunkEntities) {
        if (neighborEntity != flecs::entity::null()) {
            auto* neighborChunk = neighborEntity.get_mut<VoxelChunk>();
            if (neighborChunk != nullptr) {
                neighborVoxels[neighborIndex] = neighborChunk->voxels;
                presentNeighbors++;
            } else {
                LOG_WARN("VoxelChunkMesher", "Chunk ({},{},{}) neighbor[{}] entity exists but VoxelChunk is null",
                         pos.x, pos.y, pos.z, neighborIndex);
            }
        }
        neighborIndex++;
    }

    input.neighborVoxels = neighborVoxels;

    // if (mesh->meshGeneration > 1) {
    //     LOG_DEBUG("VoxelChunkMesher", "[REMESH] ({},{},{}) gen={} neighbors={}/6 prevFaces={}",
    //               pos.x, pos.y, pos.z, mesh->meshGeneration, presentNeighbors, mesh->faceCount);
    // }

    enqueue(std::move(input));
    e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>();
}

void VoxelChunkMesher::poll_meshing_results_system(flecs::iter &it) {
    VOXEL_ZONE_N("PollMeshingResults");
    const auto* chunkManager = it.world().get<ChunkManager>();
    auto results = poll_results(999);

    for (auto &result: results) {
        flecs::entity chunk = chunkManager->get_chunk_entity(result.chunkCoord);
        if (chunk == flecs::entity::null() || !chunk.has<VoxelChunkMesh>()) continue;

        auto mesh = chunk.get_mut<VoxelChunkMesh>();

        // Discard stale results: generation mismatch means a newer task was dispatched
        // (e.g., a neighbor arrived and re-marked this chunk Dirty while the old task was running).
        if (result.meshGeneration != mesh->meshGeneration) {
            // The chunk was re-enqueued after this task started; discard and let the newer task win.
            continue;
        }

        // If the state is no longer Meshing (e.g., chunk was unloaded), discard.
        if (!chunk.has<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>()) {
            chunk.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();
            continue;
        }

        uint32_t prevFaceCount = mesh->faceCount;
        mesh->faces = std::move(result.faces);
        mesh->faceCount = static_cast<uint32_t>(mesh->faces.size());

        // Stage-3 probe: did the face count actually change on a remesh?
        if (result.meshGeneration > 1) {
            LOG_DEBUG("VoxelChunkMesher", "[REMESH RESULT] ({},{},{}) gen={} faces: {} -> {}{}",
                      result.chunkCoord.x, result.chunkCoord.y, result.chunkCoord.z,
                      result.meshGeneration, prevFaceCount, mesh->faceCount,
                      (prevFaceCount == mesh->faceCount) ? "  <-- NO CHANGE (neighbor data had no effect?)" : "");
        }

        chunk.add<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>();
    }
}

float VoxelChunkMesher::calculate_task_priority(const glm::ivec3 &chunkPos) const {
    glm::vec3 chunkCenter = glm::vec3(chunkPos) * static_cast<float>(CHUNK_SIZE)
                            + glm::vec3(CHUNK_SIZE * 0.5f);

    float distance = glm::length(chunkCenter - m_cameraPos);

    AABB chunkAABB = AABB::from_chunk(chunkPos, CHUNK_SIZE);
    bool inFrustum = m_frustum.intersects(chunkAABB);

    glm::vec3 toChunk = glm::normalize(chunkCenter - m_cameraPos);
    float dotProduct = glm::dot(toChunk, m_viewDir);

    float priority = distance;

    if (!inFrustum) {
        priority += 1000.0f;
    } else {
        priority -= dotProduct * 50.0f;
    }

    return priority;
}


void VoxelChunkMesher::worker_loop(size_t id) {
#ifdef TRACY_ENABLE
    char threadName[32];
    snprintf(threadName, sizeof(threadName), "MeshWorker %zu", id);
    tracy::SetThreadName(threadName);
#endif

    constexpr size_t BATCH_SIZE = 32;
    std::vector<TaskMeshingInput> batch;
    std::vector<TaskMeshingOutput> results;
    batch.reserve(BATCH_SIZE);
    results.reserve(BATCH_SIZE);

    while (true) {
        batch.clear(); {
            VOXEL_ZONE_N("WaitForTasks");
            std::unique_lock<std::mutex> lock(m_taskMutex);
            m_taskCv.wait(lock, [this] {
                return m_stop || !m_taskQueue.empty();
            });

            if (m_stop && m_taskQueue.empty()) {
                return;
            }

            while (!m_taskQueue.empty() && batch.size() < BATCH_SIZE) {
                batch.push_back(std::move(m_taskQueue.top()));
                m_taskQueue.pop();
                m_pendingCoords.erase(batch.back().chunkCoord);
            }
        } {
            VOXEL_ZONE_N("ProcessBatch");
            results.clear();
            for (auto &input: batch) {
                results.push_back(build_mesh(input));
            }
        } {
            VOXEL_ZONE_N("PushResults");
            std::lock_guard<std::mutex> lock(m_resultMutex);
            for (auto &result: results) {
                m_resultQueue.push(std::move(result));
            }
        }
    }
}

TaskMeshingOutput VoxelChunkMesher::build_mesh(const TaskMeshingInput &input) {
    VOXEL_ZONE_N("BuildMesh");

    TaskMeshingOutput result;
    result.chunkCoord = input.chunkCoord;
    result.success = true;
    result.meshGeneration = input.meshGeneration;

    const auto &voxels = *input.voxels;

    // Helper to get neighbor voxel at boundary
    auto get_neighbor_voxel = [&input](int neighborIdx, int lx, int ly, int lz) -> uint8_t {
        if (const auto &nv = input.neighborVoxels[neighborIdx]) {
            return (*nv)[lx + CHUNK_SIZE * (ly + CHUNK_SIZE * lz)];
        }
        return 0;
    };

    // Pre-allocated masks for all 6 faces, all slices, per voxel type
    using SliceMasks = std::unordered_map<uint8_t, std::array<uint32_t, CHUNK_SIZE> >;
    std::array<std::array<SliceMasks, CHUNK_SIZE>, 6> allMasks; {
        VOXEL_ZONE_N("BuildAllMasks");
        for (int z = 0; z < CHUNK_SIZE; z++) {
            for (int y = 0; y < CHUNK_SIZE; y++) {
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    const uint8_t voxel = voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * z)];
                    if (voxel == 0) continue;

                    // Face 0: normal toward -X, check x-1 neighbor
                    {
                        uint8_t neighbor = (x > 0)
                                               ? voxels[(x - 1) + CHUNK_SIZE * (y + CHUNK_SIZE * z)]
                                               : get_neighbor_voxel(1, CHUNK_SIZE - 1, y, z);
                        if (neighbor == 0) {
                            allMasks[0][x][voxel][y] |= (1u << z);
                        }
                    }

                    // Face 1: normal toward +X, check x+1 neighbor
                    {
                        uint8_t neighbor = (x + 1 < CHUNK_SIZE)
                                               ? voxels[(x + 1) + CHUNK_SIZE * (y + CHUNK_SIZE * z)]
                                               : get_neighbor_voxel(0, 0, y, z);
                        if (neighbor == 0) {
                            allMasks[1][x][voxel][y] |= (1u << z);
                        }
                    }

                    // Face 2: normal toward -Y, check y-1 neighbor
                    {
                        uint8_t neighbor = (y > 0)
                                               ? voxels[x + CHUNK_SIZE * ((y - 1) + CHUNK_SIZE * z)]
                                               : get_neighbor_voxel(3, x, CHUNK_SIZE - 1, z);
                        if (neighbor == 0) {
                            allMasks[2][y][voxel][z] |= (1u << x);
                        }
                    }

                    // Face 3: normal toward +Y, check y+1 neighbor
                    {
                        uint8_t neighbor = (y + 1 < CHUNK_SIZE)
                                               ? voxels[x + CHUNK_SIZE * ((y + 1) + CHUNK_SIZE * z)]
                                               : get_neighbor_voxel(2, x, 0, z);
                        if (neighbor == 0) {
                            allMasks[3][y][voxel][z] |= (1u << x);
                        }
                    }

                    // Face 4: normal toward -Z, check z-1 neighbor
                    {
                        uint8_t neighbor = (z > 0)
                                               ? voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * (z - 1))]
                                               : get_neighbor_voxel(5, x, y, CHUNK_SIZE - 1);
                        if (neighbor == 0) {
                            allMasks[4][z][voxel][y] |= (1u << x);
                        }
                    }

                    // Face 5: normal toward +Z, check z+1 neighbor
                    {
                        uint8_t neighbor = (z + 1 < CHUNK_SIZE)
                                               ? voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * (z + 1))]
                                               : get_neighbor_voxel(4, x, y, 0);
                        if (neighbor == 0) {
                            allMasks[5][z][voxel][y] |= (1u << x);
                        }
                    }
                }
            }
        }
    }

    // Face axes for coordinate reconstruction: {slice_axis, u_axis, v_axis}
    constexpr int FACE_AXES[6][3] = {
        {0, 2, 1}, {0, 2, 1}, {1, 0, 2}, {1, 0, 2}, {2, 0, 1}, {2, 0, 1}
    };

    // Greedy meshing on pre-computed masks
    {
        VOXEL_ZONE_N("GreedyMerge");
        for (int faceDir = 0; faceDir < 6; faceDir++) {
            const int sAxis = FACE_AXES[faceDir][0];
            const int uAxis = FACE_AXES[faceDir][1];
            const int vAxis = FACE_AXES[faceDir][2];

            for (int slice = 0; slice < CHUNK_SIZE; slice++) {
                for (auto &[voxel, mask]: allMasks[faceDir][slice]) {
                    uint32_t texSlot = 0;
                    if (auto it = input.textureIDs.find(voxel); it != input.textureIDs.end())
                        texSlot = it->second;

                    for (int v = 0; v < CHUNK_SIZE; v++) {
                        while (mask[v]) {
                            int u = std::countr_zero(mask[v]);
                            uint32_t shifted = mask[v] >> u;
                            uint32_t inverted = ~shifted;
                            int w = inverted ? std::countr_zero(inverted) : (32 - u);
                            uint32_t runMask = static_cast<uint32_t>(((1ull << w) - 1ull) << u);

                            int h = 1;
                            for (int nv = v + 1; nv < CHUNK_SIZE && (mask[nv] & runMask) == runMask; nv++) {
                                mask[nv] &= ~runMask;
                                h++;
                            }
                            mask[v] &= ~runMask;

                            int p[3];
                            p[sAxis] = slice;
                            p[uAxis] = u;
                            p[vAxis] = v;
                            TerrainFace3d face{};
                            face.x = p[0];
                            face.y = p[1];
                            face.z = p[2];
                            face.faceIndex = faceDir;
                            // Swap width/height for faces where UV orientation differs
                            if (faceDir == 1 || faceDir == 3 || faceDir == 4) {
                                face.width = h - 1;
                                face.height = w - 1;
                            } else {
                                face.width = w - 1;
                                face.height = h - 1;
                            }
                            face.textureSlot = texSlot;
                            result.faces.push_back(face);
                        }
                    }
                }
            }
        }
    }

    return result;
}
