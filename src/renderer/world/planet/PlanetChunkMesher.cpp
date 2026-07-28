#include "PlanetChunkMesher.h"

#include <imgui.h>

#include "core/log/Logger.h"
#include "core/world/planet/planet_components.h"
#include "renderer/rendering_components.h"
#include "renderer/world/VoxelTextureManager.h"

using namespace vp;


PlanetChunkMesher::PlanetChunkMesher() {
    // init threads
    size_t threadCount = std::thread::hardware_concurrency() / 2;
    m_workerResults.reserve(threadCount);
    for (size_t i = 0; i < threadCount; i++) {
        m_workerResults.push_back(std::make_unique<MesherWorkerResult>());
        m_workerThreads.emplace_back(&PlanetChunkMesher::worker_loop, this, i);
    }
    LOG_TRACE("PlanetChunkGenerator", "Started {} generation worker threads", threadCount);
}

PlanetChunkMesher::~PlanetChunkMesher() {
    // join
    m_stop = true;
    for (auto &thread: m_workerThreads) {
        if (thread.joinable()) thread.join();
    }
    LOG_TRACE("PlanetChunkMesher", "All worker threads stopped");
}

void PlanetChunkMesher::Register(flecs::world &ecs) {
    ecs.emplace<PlanetChunkMesher>();
    PlanetChunkMesher *mesher = ecs.get_mut<PlanetChunkMesher>();
    mesher->init(ecs);
}

void PlanetChunkMesher::init(flecs::world &ecs) {
    ecs.system<const VoxelChunk, VoxelChunkMesh>("PlanetChunkMesher-Enqueue")
            .kind(flecs::PostUpdate)
            // .with<const PlanetChunkCoord>()
            .with<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>()
            .run([this](flecs::iter &it) {
                system_enqueue(it);
            });

    ecs.system("PlanetChunkMesher-PollResults")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                system_poll_results(it);
            });

    ecs.system("PlanetChunkMesher-ImguiDebugStat")
            .kind(flecs::PostUpdate)
            .run([this](flecs::iter &it) {
                ImGui::Begin("PlanetChunkMesher Debug");
                // task queue statistics
                {
                    std::lock_guard lock(m_taskMutex);
                    ImGui::Text("Pending tasks: %zu", m_queue.size());
                }
                // worker results statistics
                size_t total = 0;
                for (size_t i = 0; i < m_workerResults.size(); i++) {
                    auto &wr = m_workerResults[i];
                    std::lock_guard lock(wr->resultMutex);
                    total += wr->results.size();
                }
                ImGui::Text("Pending results: %zu", total);
                ImGui::Separator();
                // query count entities with certain flags :
                auto queryMeshing = it.world().query_builder<const PlanetNodeCoord>()
                        .with<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>().build();
                auto queryReady = it.world().query_builder<const PlanetNodeCoord>()
                        .with<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>().build();
                auto queryDirty = it.world().query_builder<const PlanetNodeCoord>()
                        .with<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>().build();
                auto queryClean = it.world().query_builder<const PlanetNodeCoord>()
                        .with<VoxelChunkMeshState, voxel_chunk_mesh_state::Clean>().build();

                ImGui::Text("Clean: %ul", queryClean.count());
                ImGui::Text("Meshing: %ul", queryMeshing.count());
                ImGui::Text("Ready for upload: %ul", queryReady.count());
                ImGui::Text("Dirty: %ul", queryDirty.count());

                ImGui::End();
            });
}

void PlanetChunkMesher::system_enqueue(flecs::iter &it) {
    auto *textureManager = it.world().get_mut<VoxelTextureManager>();

    std::vector<MesherTaskInput> toEnqueue;

    while (it.next()) {
        // fields of the table
        auto datas = it.field<const VoxelChunk>(0);
        auto meshes = it.field<VoxelChunkMesh>(1);

        for (auto i: it) {
            flecs::entity e = it.entity(i);
            VoxelChunkMesh &mesh = meshes[i];
            const VoxelChunk &data = datas[i];

            MesherTaskInput input = {
                .e = e,
                .voxels = data.voxels,
                .gpuTextureIds = {},
                .priority = 0.0f, // TODO compute priority based on distance to player
                .meshGeneration = ++mesh.meshGeneration
            };

            if (textureManager)
                for (const auto &[texId, voxelId]: data.textureIDs)
                    input.gpuTextureIds[voxelId] = textureManager->request_texture_slot(texId);
            else
                LOG_ERROR("PlanetChunkMesher", "Missing VoxelTextureManager, cannot request texture slots for meshing");

            toEnqueue.push_back(std::move(input));
            e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>();
        }
    }

    if (!toEnqueue.empty()) {
        size_t enqueued = 0; {
            std::lock_guard lock(m_taskMutex);
            for (auto &input: toEnqueue) {
                if (m_pending.contains(input.e)) continue;
                m_pending.insert(input.e);
                m_queue.push(std::move(input));
                enqueued++;
            }
        }
        if (enqueued > 0)
            m_taskSemaphore.release(std::min((enqueued + BATCH_SIZE - 1) / BATCH_SIZE, size_t(3)));
    }
}

void PlanetChunkMesher::system_poll_results(flecs::iter &it) {
    auto results = poll_results(999);

    for (auto &result: results) {
        flecs::entity e = result.e;
        if (!e.is_valid() || !e.is_alive() || !e.has<VoxelChunkMesh>()) continue;

        auto *mesh = e.get_mut<VoxelChunkMesh>();
        // mesh was updated again after this task was enqueued, skip
        if (mesh->meshGeneration != result.meshGeneration) continue;
        // weird state to be, to be sure, we rerequest a meshing
        if (!e.has<VoxelChunkMeshState, voxel_chunk_mesh_state::Meshing>()) {
            e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::Dirty>();
            continue;
        }

        mesh->faceCount = result.faces.size();
        mesh->faces = std::move(result.faces);
        e.add<VoxelChunkMeshState, voxel_chunk_mesh_state::ReadyForUpload>();
    }
}

std::vector<PlanetChunkMesher::MesherTaskOutput> PlanetChunkMesher::poll_results(size_t maxResults) {
    std::vector<MesherTaskOutput> out;
    for (auto &wr: m_workerResults) {
        if (out.size() >= maxResults) break;
        if (wr->pendingCount.load(std::memory_order_relaxed) == 0) continue;

        std::lock_guard lock(wr->resultMutex);

        int n = static_cast<int>(wr->results.size());
        for (auto &r: wr->results) out.push_back(std::move(r));

        wr->results.clear();
        wr->pendingCount.fetch_sub(n, std::memory_order_relaxed);
    }
    return out;
}

void PlanetChunkMesher::worker_loop(size_t id) {
    std::vector<MesherTaskInput> batch;
    std::vector<MesherTaskOutput> results;
    batch.reserve(BATCH_SIZE);
    results.reserve(BATCH_SIZE);

    while (true) {
        batch.clear();

        m_taskSemaphore.acquire();
        if (m_stop.load(std::memory_order_relaxed) && [&] {
            std::lock_guard lock(m_taskMutex);
            return m_queue.empty();
        }())
            return; {
            std::lock_guard lock(m_taskMutex);
            while (batch.size() < BATCH_SIZE && !m_queue.empty()) {
                batch.push_back(std::move(const_cast<MesherTaskInput &>(m_queue.top())));
                m_queue.pop();
            }
            if (!m_queue.empty()) {
                m_taskSemaphore.release(1);
            }
        }

        if (batch.empty()) continue; {
            results.clear();
            for (auto &input: batch) {
                results.push_back(build_mesh(input));
            }
        } {
            auto &workerResult = *m_workerResults[id];
            std::lock_guard lock(workerResult.resultMutex);
            for (auto &result: results) {
                workerResult.results.push_back(std::move(result));
                workerResult.pendingCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}

PlanetChunkMesher::MesherTaskOutput PlanetChunkMesher::build_mesh(const MesherTaskInput &input) {
    VOXEL_ZONE_N("BuildMesh");

    // MesherTaskOutput result;
    // result.e = input.e;
    // result.success = true;
    // result.meshGeneration = input.meshGeneration;
    //
    // const auto &voxels = *input.voxels;
    //
    // // directions: -X, +X, -Y, +Y, -Z, +Z
    // constexpr int DIRS[6][3] = {
    //     {-1, 0, 0}, {1, 0, 0},
    //     {0, -1, 0}, {0, 1, 0},
    //     {0, 0, -1}, {0, 0, 1}
    // };
    //
    // for (int x = 0; x < CHUNK_SIZE; ++x) {
    //     for (int y = 0; y < CHUNK_SIZE; ++y) {
    //         for (int z = 0; z < CHUNK_SIZE; ++z) {
    //             uint16_t voxel = voxels[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE];
    //             uint8_t texID = voxel & 0xFF;
    //             if (texID == 0) continue;
    //
    //             uint8_t blkH = static_cast<uint8_t>(voxel >> 8);
    //
    //             uint32_t texSlot = 0;
    //             if (auto it = input.gpuTextureIds.find(texID); it != input.gpuTextureIds.end())
    //                 texSlot = it->second;
    //
    //             for (int faceDir = 0; faceDir < 6; faceDir++) {
    //                 int nx = x + DIRS[faceDir][0];
    //                 int ny = y + DIRS[faceDir][1];
    //                 int nz = z + DIRS[faceDir][2];
    //
    //                 // neighbor hors chunk = air
    //                 uint8_t nbTexID = 0;
    //                 if (nx >= 0 && nx < CHUNK_SIZE &&
    //                     ny >= 0 && ny < CHUNK_SIZE &&
    //                     nz >= 0 && nz < CHUNK_SIZE) {
    //                     uint16_t nb = voxels[nx + ny * CHUNK_SIZE + nz * CHUNK_SIZE * CHUNK_SIZE];
    //                     nbTexID = nb & 0xFF;
    //                 }
    //
    //                 if (nbTexID != 0) continue;
    //                 // face cachée
    //
    //                 TerrainFace3d face{};
    //                 face.x = static_cast<uint32_t>(x);
    //                 face.y = static_cast<uint32_t>(y * 16);
    //                 face.z = static_cast<uint32_t>(z);
    //                 // sub-voxel, blocs pleins
    //                 face.faceIndex = static_cast<uint32_t>(faceDir);
    //                 face.width = 15; // 1 voxel = (15+1)/16 = 1.0
    //                 face.height = 15;
    //                 face.textureSlot = texSlot;
    //                 result.faces.push_back(face);
    //             }
    //         }
    //     }
    // }
    //
    // return result;


    MesherTaskOutput result;
    result.e = input.e;
    result.success = true;
    result.meshGeneration = input.meshGeneration;

    const auto &voxels = *input.voxels;

    // Returns 0 (air) when the neighbor chunk is absent
    auto get_neighbor_voxel = [&input](int neighborIdx, int lx, int ly, int lz) -> uint16_t {
        // if (const auto &nv = input.neighborVoxels[neighborIdx]) {
        //     return (*nv)[lx + CHUNK_SIZE * (ly + CHUNK_SIZE * lz)];
        // }
        return 0;
    };

    // Masks keyed by uint32_t: texID (8b) | blkH (8b) | nbH (8b).
    // For top/bottom faces nbH stays 0. Side faces encode the neighbor height
    // so that only faces with the same visible extent can greedy-merge.
    using SliceMasks = std::unordered_map<uint32_t, std::array<uint32_t, CHUNK_SIZE> >;
    std::array<std::array<SliceMasks, CHUNK_SIZE>, 6> allMasks; {
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
                            allMasks[0][x][static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16)][y] |= (
                                1u << z);
                    }
                    // Face 1: +X, check x+1
                    {
                        uint16_t nb = (x + 1 < CHUNK_SIZE)
                                          ? voxels[(x + 1) + CHUNK_SIZE * (y + CHUNK_SIZE * z)]
                                          : get_neighbor_voxel(0, 0, y, z);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            allMasks[1][x][static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16)][y] |= (
                                1u << z);
                    }
                    // Face 2: -Y (bottom, toward planet center), check y-1
                    // Visible when neighbor below is air OR has a gap above it (nbH < 15)
                    {
                        uint16_t nb = (y > 0)
                                          ? voxels[x + CHUNK_SIZE * ((y - 1) + CHUNK_SIZE * z)]
                                          : get_neighbor_voxel(3, x, CHUNK_SIZE - 1, z);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || nbH < 15)
                            allMasks[2][y][static_cast<uint32_t>(voxel)][z] |= (1u << x);
                    }
                    // Face 3: +Y (top/grass face, away from planet), check y+1
                    // Visible when neighbor above is air OR current block is partial
                    {
                        uint16_t nb = (y + 1 < CHUNK_SIZE)
                                          ? voxels[x + CHUNK_SIZE * ((y + 1) + CHUNK_SIZE * z)]
                                          : get_neighbor_voxel(2, x, 0, z);
                        if ((nb & 0xFF) == 0 || blkH < 15)
                            allMasks[3][y][static_cast<uint32_t>(voxel)][z] |= (1u << x);
                    }
                    // Face 4: -Z (side face), check z-1
                    {
                        uint16_t nb = (z > 0)
                                          ? voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * (z - 1))]
                                          : get_neighbor_voxel(5, x, y, CHUNK_SIZE - 1);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            allMasks[4][z][static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16)][y] |= (1u << x);
                    }
                    // Face 5: +Z (side face), check z+1
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

    // Y is the altitude axis (radial, away from planet center).
    // Faces 2/3 (±Y) are bottom/top (toward/away from planet).
    // Faces 0/1/4/5 (±X, ±Z) are side faces.
    // {slice_axis, u_axis, v_axis} — u = bitmask bit position, v = mask row index
    constexpr int FACE_AXES[6][3] = {
        {0, 2, 1}, {0, 2, 1}, // ±X: slice=X, u=Z(bit), v=Y(alt,row)
        {1, 0, 2}, {1, 0, 2}, // ±Y: slice=Y, u=X(bit), v=Z(row)  [top/bottom]
        {2, 0, 1}, {2, 0, 1}, // ±Z: slice=Z, u=X(bit), v=Y(alt,row)
    }; {
        VOXEL_ZONE_N("GreedyMerge");
        for (int faceDir = 0; faceDir < 6; faceDir++) {
            const int sAxis = FACE_AXES[faceDir][0];
            const int uAxis = FACE_AXES[faceDir][1];
            const int vAxis = FACE_AXES[faceDir][2];

            // Side faces have Y as their v-axis. Partial blocks must NOT merge
            // along Y (altitude): there are gaps between their tops.
            const bool isSideFace = (faceDir != 2 && faceDir != 3);

            for (int slice = 0; slice < CHUNK_SIZE; slice++) {
                for (auto &[key, mask]: allMasks[faceDir][slice]) {
                    const uint8_t texID = key & 0xFF;
                    const uint8_t blkH = (key >> 8) & 0xFF;
                    const uint8_t nbH = (key >> 16) & 0xFF;
                    const bool isPartial = isSideFace ? (blkH < 15 || nbH > 0) : (blkH < 15);

                    uint32_t texSlot = 0;
                    if (auto it = input.gpuTextureIds.find(texID); it != input.gpuTextureIds.end())
                        texSlot = it->second;

                    for (int v = 0; v < CHUNK_SIZE; v++) {
                        while (mask[v]) {
                            const int u = std::countr_zero(mask[v]);
                            const uint32_t shifted = mask[v] >> u;
                            const uint32_t inverted = ~shifted;
                            const int w = inverted ? std::countr_zero(inverted) : (32 - u);
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
                            // y is stored in sub-voxel units (value / 16.0 = voxel altitude).
                            // +Y face (faceDir==3) encodes exact top position; side faces encode base + nbH offset.
                            face.y = (faceDir == 3)
                                         ? static_cast<uint32_t>(p[1] * 16 + blkH)
                                         : (isSideFace
                                                ? static_cast<uint32_t>(p[1] * 16 + nbH)
                                                : static_cast<uint32_t>(p[1] * 16));
                            face.faceIndex = faceDir;

                            // Width/height in sub-voxel units (stored as value - 1).
                            // The Y-direction of a side face uses partial-block height when applicable.
                            const int hSubvoxel = (isSideFace && isPartial)
                                                      ? static_cast<int>(blkH - nbH)
                                                      : (h * 16 - 1);
                            const int wSubvoxel = w * 16 - 1;

                            face.width = wSubvoxel;
                            face.height = hSubvoxel;

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
