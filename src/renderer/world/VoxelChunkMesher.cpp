#include "VoxelChunkMesher.h"

#include <bit>
#include <imgui.h>

#include "VoxelTextureManager.h"
#include "core/log/Logger.h"
#include "core/world/ChunkManager.h"
#include "platform/inputs/input_state.h"
#include "renderer/rendering_components.h"


VoxelChunkMesher::~VoxelChunkMesher() {
    shutdown();
}

void VoxelChunkMesher::shutdown() { {
        std::lock_guard lock(m_taskMutex);
        m_stop = true;
    }

    LOG_DEBUG("VoxelChunkMesher", "Shutting down {} worker threads", m_workerThreads.size());
    m_taskSemaphore.release(m_workerThreads.size());

    for (auto &thread: m_workerThreads) {
        if (thread.joinable()) thread.join();
    }
    m_workerThreads.clear();
    LOG_INFO("VoxelChunkMesher", "All worker threads shut down");
}


std::vector<TaskMeshingOutput> VoxelChunkMesher::poll_results(size_t maxResults) {
    std::vector<TaskMeshingOutput> results;
    for (auto &wr: m_workerResults) {
        VOXEL_ZONE_N("Poll Result From a Worker");
        if (results.size() >= maxResults) break;
        if (wr->pendingCount.load(std::memory_order_relaxed) == 0) continue;

        std::lock_guard lock(wr->m_resultMutex);
        int count = static_cast<int>(wr->results.size());
        for (auto &r: wr->results) {
            results.push_back(std::move(r));
        }
        wr->results.clear();
        wr->pendingCount.fetch_sub(count, std::memory_order_relaxed);
    }
    return results;
}


void VoxelChunkMesher::init(flecs::world &ecs) {
    ecs.component<VoxelChunkMeshState>()
            .add(flecs::Exclusive);

    ecs.system<const ChunkCoordinate>("VoxelChunkMesher-ResolveWaiting")
            .kind(flecs::PostUpdate)
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::WaitingForNeighbors>()
            .run([this](flecs::iter &it) {
                resolve_waiting_chunks_system(it);
            });

    ecs.system<const VoxelChunk, const ChunkCoordinate, VoxelChunkMesh>("VoxelChunkMesher-EnqueueChunksBuild")
            .kind(flecs::PostUpdate)
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>()
            .run([this](flecs::iter &it) {
                // static bool canMesh = false;
                // auto* inputAction = it.world().get<InputActionState>();
                // if (inputAction->is_action_pressed(ActionInputType::Debug2))
                //     canMesh = !canMesh;
                //
                // if (canMesh)
                //     enqueue_chunks_build_system(it);
                // else
                //     it.fini();
                enqueue_chunks_build_system(it);
            });

    ecs.system("VoxelChunkMesher-PollMeshingResults")
            .kind(flecs::PostUpdate)
            .run([this](flecs::iter &it) {
                VOXEL_ZONE_N("Mesher-PollResults");
                poll_meshing_results_system(it);
            });

    // ecs.system("VoxelChunkMesher-DebugInfo")
    //         .kind(flecs::OnStore)
    //         .run([this](flecs::iter &it) {
    //             VOXEL_ZONE_N("Mesher-Debug");
    //             ImGui::Begin("Voxel Chunk Mesher");
    //             ImGui::Text("Pending Tasks: %zu", pending_count());
    //             ImGui::Text("Completed Results: %zu", completed_count());
    //             ImGui::End();
    //         });

    ecs.system<Camera3d, const Transform>("VoxelChunkMesher-UpdateFrustum")
            .kind(flecs::PreUpdate)
            .each([this](flecs::entity e, Camera3d &camera, const Transform &transform) {
                VOXEL_ZONE_N("Mesher-Frustum");
                glm::mat4 viewProjection = camera.projectionMatrix * camera.viewMatrix;
                glm::vec3 viewDir = transform.forward();
                glm::vec3 cameraPos = transform.pos;

                this->update_frustum(viewProjection, cameraPos, viewDir);
            });

    // init workers
    size_t numThreads = std::max(1u, std::thread::hardware_concurrency()/2);
    // size_t numThreads = 2;

    m_workerResults.reserve(numThreads);
    for (size_t i = 0; i < numThreads; i++) {
        m_workerResults.push_back(std::make_unique<MeshWorkerResult>());
    }

    for (size_t i = 0; i < numThreads; i++) {
        m_workerThreads.emplace_back([this, i] { worker_loop(i); });
    }
}

void VoxelChunkMesher::Register(flecs::world &ecs) {
    ecs.emplace<VoxelChunkMesher>();
    ecs.get_mut<VoxelChunkMesher>()->init(ecs);
}

void VoxelChunkMesher::resolve_waiting_chunks_system(flecs::iter &it) {
    VOXEL_ZONE_N("Mesher-ResolveWaiting");

    const auto* chunkManager = it.world().get<ChunkManager>();
    if (!chunkManager) return;

    while (it.next()) {
        auto positions = it.field<const ChunkCoordinate>(0);
        for (auto i: it) {
            if (chunkManager->can_mesh(positions[i])) {
                it.entity(i).add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();
            }
        }
    }
}

void VoxelChunkMesher::enqueue_chunks_build_system(flecs::iter &it) {
    VOXEL_ZONE_N("Mesher-EnqueueChunksBuild");

    struct PendingMesh {
        flecs::entity e;
        VoxelChunkMesh* mesh;
        TaskMeshingInput input;
    };
    std::vector<PendingMesh> pending;

    while (it.next()) {
        VOXEL_ZONE_N("IT: Prepare Enqueue Chunk");
        auto chunks = it.field<const VoxelChunk>(0);
        auto positions = it.field<const ChunkCoordinate>(1);
        auto meshes = it.field<VoxelChunkMesh>(2);

        auto* chunkManager = it.world().get_mut<ChunkManager>();
        auto* textureManager = it.world().get_mut<VoxelTextureManager>();

        for (auto i: it) {
            VOXEL_ZONE_N("Prepare Enqueue Chunk");
            const ChunkCoordinate &pos = positions[i];
            // if (!chunkManager->can_mesh(pos)) continue;

            TaskMeshingInput input;
            input.chunkCoord = pos;
            input.voxels = chunks[i].voxels;
            input.priority = calculate_task_priority(pos);

            for (const auto &[textureID, voxelID]: chunks[i].textureIDs) {
                input.textureIDs[voxelID] = textureManager->request_texture_slot(textureID);
            } {
                VOXEL_ZONE_N("Manage Neighboring Chunks");
                auto neighborEntities = chunkManager->get_neighboring_chunks(pos);
                for (int n = 0; n < 6; n++) {
                    if (neighborEntities[n] != flecs::entity::null()) {
                        const auto* neighborChunk = neighborEntities[n].get<VoxelChunk>();
                        if (neighborChunk) input.neighborVoxels[n] = neighborChunk->voxels;
                    }
                }
            }

            pending.push_back({it.entity(i), &meshes[i], std::move(input)});
        }
    }

    if (pending.empty()) return;

    size_t enqueued = 0; {
        VOXEL_ZONE_N("Enqueue Batch");
        std::lock_guard lock(m_taskMutex);
        for (auto &p: pending) {
            VOXEL_ZONE_N("Enqueue Chunk");
            if (m_pendingCoords.count(p.input.chunkCoord)) continue;

            p.mesh->meshGeneration++;
            p.input.meshGeneration = p.mesh->meshGeneration;

            m_pendingCoords.insert(p.input.chunkCoord);
            m_taskQueue.push(std::move(p.input));
            p.e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>();
            enqueued++;
        }
    }

    if (enqueued > 0) {
        VOXEL_ZONE_N("Release Semaphore")
        m_taskSemaphore.release(std::min((enqueued + MESHING_BATCH_SIZE - 1) / MESHING_BATCH_SIZE, static_cast<size_t>(3)));
    }
}

void VoxelChunkMesher::poll_meshing_results_system(flecs::iter &it) {
    const auto* chunkManager = it.world().get<ChunkManager>();
    auto results = poll_results(999);

    for (auto &result: results) {
        VOXEL_ZONE_N("Handle Mesh Result")
        m_pendingCoords.erase(result.chunkCoord);
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

        mesh->faces = std::move(result.faces);
        mesh->faceCount = static_cast<uint32_t>(mesh->faces.size());

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

    constexpr size_t BATCH_SIZE = MESHING_BATCH_SIZE;
    std::vector<TaskMeshingInput> batch;
    std::vector<TaskMeshingOutput> results;
    batch.reserve(BATCH_SIZE);
    results.reserve(BATCH_SIZE);

    while (true) {
        batch.clear();

        m_taskSemaphore.acquire();
        if (m_stop.load(std::memory_order_relaxed) && [&] {
            std::lock_guard lock(m_taskMutex);
            return m_taskQueue.empty();
        }())
            return;

        {
            VOXEL_ZONE_N("PollingJobs");
            std::lock_guard lock(m_taskMutex);
            if (!m_taskQueue.empty()) {
                while (batch.size() < BATCH_SIZE && !m_taskQueue.empty()) {
                    batch.push_back(std::move(const_cast<TaskMeshingInput&>(m_taskQueue.top())));
                    m_taskQueue.pop();
                }
                if (!m_taskQueue.empty()) {
                    m_taskSemaphore.release(1);
                }
            }
        }

        if (batch.empty()) continue; {
            VOXEL_ZONE_N("ProcessBatch");
            results.clear();
            for (auto &input: batch) {
                results.push_back(build_mesh(input));
            }
        } {
            VOXEL_ZONE_N("PushResults");
            auto &workerResult = *m_workerResults[id];
            std::lock_guard lock(workerResult.m_resultMutex);
            for (auto &result: results) {
                workerResult.results.push_back(std::move(result));
                workerResult.pendingCount.fetch_add(1, std::memory_order_relaxed);
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

    // Returns 0 (air) when the neighbor chunk is absent
    auto get_neighbor_voxel = [&input](int neighborIdx, int lx, int ly, int lz) -> uint16_t {
        if (const auto &nv = input.neighborVoxels[neighborIdx]) {
            return (*nv)[lx + CHUNK_SIZE * (ly + CHUNK_SIZE * lz)];
        }
        return 0;
    };

    // Masks keyed by uint32_t: texID (8b) | blkH (8b) | nbH (8b).
    // For top/bottom faces nbH stays 0. Side faces encode the neighbor height
    // so that only faces with the same visible extent can greedy-merge.
    using SliceMasks = std::unordered_map<uint32_t, std::array<uint32_t, CHUNK_SIZE>>;
    std::array<std::array<SliceMasks, CHUNK_SIZE>, 6> allMasks;
    {
        VOXEL_ZONE_N("BuildAllMasks");
        for (int z = 0; z < CHUNK_SIZE; z++) {
            for (int y = 0; y < CHUNK_SIZE; y++) {
                for (int x = 0; x < CHUNK_SIZE; x++) {
                    const uint16_t voxel = voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * z)];
                    if ((voxel & 0xFF) == 0) continue; // air: textureID == 0

                    const uint8_t blkH = static_cast<uint8_t>(voxel >> 8);

                    // Face 0: -X, check x-1
                    {
                        uint16_t nb = (x > 0)
                            ? voxels[(x - 1) + CHUNK_SIZE * (y + CHUNK_SIZE * z)]
                            : get_neighbor_voxel(1, CHUNK_SIZE - 1, y, z);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            allMasks[0][x][static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16)][y] |= (1u << z);
                    }
                    // Face 1: +X, check x+1
                    {
                        uint16_t nb = (x + 1 < CHUNK_SIZE)
                            ? voxels[(x + 1) + CHUNK_SIZE * (y + CHUNK_SIZE * z)]
                            : get_neighbor_voxel(0, 0, y, z);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            allMasks[1][x][static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16)][y] |= (1u << z);
                    }
                    // Face 2: -Y, check y-1
                    // Also visible when the block below is partial (gap above it)
                    {
                        uint16_t nb = (y > 0)
                            ? voxels[x + CHUNK_SIZE * ((y - 1) + CHUNK_SIZE * z)]
                            : get_neighbor_voxel(3, x, CHUNK_SIZE - 1, z);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH     = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || nbH < 15)
                            allMasks[2][y][static_cast<uint32_t>(voxel)][z] |= (1u << x);
                    }
                    // Face 3: +Y, check y+1
                    // Also visible when the current block is partial (gap within cell)
                    {
                        uint16_t nb = (y + 1 < CHUNK_SIZE)
                            ? voxels[x + CHUNK_SIZE * ((y + 1) + CHUNK_SIZE * z)]
                            : get_neighbor_voxel(2, x, 0, z);
                        if ((nb & 0xFF) == 0 || blkH < 15)
                            allMasks[3][y][static_cast<uint32_t>(voxel)][z] |= (1u << x);
                    }
                    // Face 4: -Z, check z-1
                    {
                        uint16_t nb = (z > 0)
                            ? voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * (z - 1))]
                            : get_neighbor_voxel(5, x, y, CHUNK_SIZE - 1);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            allMasks[4][z][static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16)][y] |= (1u << x);
                    }
                    // Face 5: +Z, check z+1
                    {
                        uint16_t nb = (z + 1 < CHUNK_SIZE)
                            ? voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * (z + 1))]
                            : get_neighbor_voxel(4, x, y, 0);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            allMasks[5][z][static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16)][y] |= (1u << x);
                    }
                }
            }
        }
    }

    // {slice_axis, u_axis, v_axis}
    constexpr int FACE_AXES[6][3] = {
        {0, 2, 1}, {0, 2, 1}, {1, 0, 2}, {1, 0, 2}, {2, 0, 1}, {2, 0, 1}
    };

    {
        VOXEL_ZONE_N("GreedyMerge");
        for (int faceDir = 0; faceDir < 6; faceDir++) {
            const int sAxis = FACE_AXES[faceDir][0];
            const int uAxis = FACE_AXES[faceDir][1];
            const int vAxis = FACE_AXES[faceDir][2];

            // For side faces the v-axis is Y. Partial blocks must NOT merge
            // vertically: there are gaps between their cell tops.
            const bool isSideFace = (faceDir == 0 || faceDir == 1 || faceDir == 4 || faceDir == 5);

            for (int slice = 0; slice < CHUNK_SIZE; slice++) {
                for (auto &[key, mask]: allMasks[faceDir][slice]) {
                    const uint8_t texID  = key & 0xFF;
                    const uint8_t blkH   = (key >> 8) & 0xFF;
                    const uint8_t nbH    = (key >> 16) & 0xFF;
                    const bool isPartial = isSideFace ? (blkH < 15 || nbH > 0) : (blkH < 15);

                    uint32_t texSlot = 0;
                    if (auto it = input.textureIDs.find(texID); it != input.textureIDs.end())
                        texSlot = it->second;

                    for (int v = 0; v < CHUNK_SIZE; v++) {
                        while (mask[v]) {
                            const int u        = std::countr_zero(mask[v]);
                            const uint32_t shifted  = mask[v] >> u;
                            const uint32_t inverted = ~shifted;
                            const int w        = inverted ? std::countr_zero(inverted) : (32 - u);
                            const uint32_t runMask = static_cast<uint32_t>(((1ull << w) - 1ull) << u);

                            int h = 1;
                            if (!(isSideFace && isPartial)) {
                                for (int nv = v + 1; nv < CHUNK_SIZE && (mask[nv] & runMask) == runMask; nv++) {
                                    mask[nv] &= ~runMask;
                                    h++;
                                }
                            }
                            mask[v] &= ~runMask;

                            int p[3];
                            p[sAxis] = slice;
                            p[uAxis] = u;
                            p[vAxis] = v;

                            TerrainFace3d face{};
                            face.x = p[0];
                            face.z = p[2];
                            // y is stored in sub-voxel units (value / 16.0 = voxel position).
                            // +Y face encodes the exact top position; others encode the base voxel.
                            face.y = (faceDir == 3)
                                ? static_cast<uint32_t>(p[1] * 16 + blkH)
                                : (isSideFace ? static_cast<uint32_t>(p[1] * 16 + nbH) : static_cast<uint32_t>(p[1] * 16));
                            face.faceIndex = faceDir;

                            // Width/height in sub-voxel units (stored as value - 1).
                            // The Y-direction of a side face uses partial-block height when applicable.
                            const int hSubvoxel = (isSideFace && isPartial) ? static_cast<int>(blkH - nbH) : (h * 16 - 1);
                            const int wSubvoxel = w * 16 - 1;

                            // Swap width/height for faces where the greedy h/w map differently
                            if (faceDir == 1 || faceDir == 3 || faceDir == 4) {
                                face.width  = hSubvoxel;
                                face.height = wSubvoxel;
                            } else {
                                face.width  = wSubvoxel;
                                face.height = hSubvoxel;
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
