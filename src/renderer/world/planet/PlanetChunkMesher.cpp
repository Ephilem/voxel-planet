#include "PlanetChunkMesher.h"

#include <algorithm>
#include <bit>

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
    m_stop = true;

    // Every worker is parked inside acquire(). Setting the stop flag alone is not enough: nothing
    // would ever wake them, and the join below would hang the process on exit
    m_taskSemaphore.release(static_cast<int>(m_workerThreads.size()));

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

                // The VoxelChunkMeshState counters that used to sit here counted ECS chunk
                // entities, which no longer exist. The equivalent state now lives in the node
                // flags, and is reported by the PlanetLod panel.

                ImGui::End();
            });
}

namespace {
    /// Max heap on priority, so the most urgent chunk is the one popped first
    bool mesher_task_less(const vp::PlanetChunkMesher::MesherTaskInput &a,
                          const vp::PlanetChunkMesher::MesherTaskInput &b) {
        return a.priority < b.priority;
    }

    /// One greedy meshing mask: the rows of a single slice that share a key
    struct MaskEntry {
        uint32_t key = 0;
        std::array<uint32_t, CHUNK_SIZE> rows{};
    };

    /// Masks of one slice. A slice carries a handful of distinct keys, so a linear scan over a
    /// flat list is both faster and allocation free compared to a hash map
    using SliceMasks = std::vector<MaskEntry>;

    /// Rows of a key inside a slice, created empty on first use. The reference is only valid
    /// until the next call on the same slice
    std::array<uint32_t, CHUNK_SIZE> &rows_for(SliceMasks &slice, uint32_t key) {
        for (MaskEntry &entry: slice) {
            if (entry.key == key) return entry.rows;
        }

        slice.push_back(MaskEntry{key, {}});
        return slice.back().rows;
    }
}

bool PlanetChunkMesher::enqueue(uint64_t jobId,
                                std::shared_ptr<const std::array<uint16_t, CHUNK_VOLUME> > voxels,
                                std::unordered_map<uint8_t, uint16_t> gpuTextureIds,
                                float priority) {
    if (!voxels) return false;

    MesherTaskInput input = {
        .jobId = jobId,
        .voxels = std::move(voxels),
        .gpuTextureIds = std::move(gpuTextureIds),
        .priority = priority,
    }; {
        std::lock_guard lock(m_taskMutex);
        m_queue.push_back(std::move(input));
        std::push_heap(m_queue.begin(), m_queue.end(), mesher_task_less);
    }

    m_taskSemaphore.release(1);
    return true;
}

size_t PlanetChunkMesher::queued_task_count() {
    std::lock_guard lock(m_taskMutex);
    return m_queue.size();
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
                std::pop_heap(m_queue.begin(), m_queue.end(), mesher_task_less);
                batch.push_back(std::move(m_queue.back()));
                m_queue.pop_back();
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
    result.jobId = input.jobId;
    result.success = true;

    const auto &voxels = *input.voxels;

    // Neighbour chunks are not available to the mesher yet, so the chunk borders are guessed.
    //
    // Returning air on every side made each chunk emit its full border wall, and the wall of the
    // chunk next door landed on exactly the same plane: two coplanar quads at identical depth,
    // drawn in whatever order the indirect draws happen to run, which reads as violent flicker
    // along every seam. Reporting the neighbours as solid drops both walls instead.
    //
    // The lateral cost is the reverse error: a genuine step in the terrain on a chunk border
    // loses its face and leaves a crack, and across a LOD boundary that step is a whole coarse
    // voxel. emit_border_skirts() below covers exactly that, without needing neighbour data.
    //
    // The floor (face 2) is reported solid for the same reason the sides are: a node sitting
    // fully under the surface is solid to its ceiling, so its own top face and the bottom face of
    // the node above land on the same plane and fight. The ceiling (face 3) stays air, or a
    // column whose ground reaches the top of the node would lose its grass face.
    constexpr uint16_t NEIGHBOR_SOLID = 1u | (15u << 8);

    // Careful with neighborIdx: it is the mirrored face direction, so the query made across face
    // f passes f ^ 1. Index 2 is therefore what face 3 asks with, the look at the node above, and
    // index 3 is what face 2 asks with, the look at the node below.
    auto get_neighbor_voxel = [](int neighborIdx, int lx, int ly, int lz) -> uint16_t {
        // if (const auto &nv = input.neighborVoxels[neighborIdx]) {
        //     return (*nv)[lx + CHUNK_SIZE * (ly + CHUNK_SIZE * lz)];
        // }
        const bool lookingUp = neighborIdx == 2;
        return lookingUp ? 0 : NEIGHBOR_SOLID;
    };

    // Masks keyed by uint32_t: texID (8b) | blkH (8b) | nbH (8b).
    // For top/bottom faces nbH stays 0. Side faces encode the neighbor height
    // so that only faces with the same visible extent can greedy-merge.
    //
    // A slice holds a handful of distinct keys, so a flat list searched linearly beats a hash
    // map by a wide margin here: there are 6 * 32 of these per chunk, and an unordered_map that
    // allocates its buckets on every one of them was the single most expensive thing the mesher
    // did. Kept thread_local so the storage is reused from one chunk to the next.
    thread_local std::array<std::array<SliceMasks, CHUNK_SIZE>, 6> allMasks;

    for (auto &face: allMasks) {
        for (auto &slice: face) slice.clear(); // keeps the capacity
    } {
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
                            rows_for(allMasks[0][x],
                                     static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16))[y] |= (1u << z);
                    }
                    // Face 1: +X, check x+1
                    {
                        uint16_t nb = (x + 1 < CHUNK_SIZE)
                                          ? voxels[(x + 1) + CHUNK_SIZE * (y + CHUNK_SIZE * z)]
                                          : get_neighbor_voxel(0, 0, y, z);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            rows_for(allMasks[1][x],
                                     static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16))[y] |= (1u << z);
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
                            rows_for(allMasks[2][y], static_cast<uint32_t>(voxel))[z] |= (1u << x);
                    }
                    // Face 3: +Y (top/grass face, away from planet), check y+1
                    // Visible when neighbor above is air OR current block is partial
                    {
                        uint16_t nb = (y + 1 < CHUNK_SIZE)
                                          ? voxels[x + CHUNK_SIZE * ((y + 1) + CHUNK_SIZE * z)]
                                          : get_neighbor_voxel(2, x, 0, z);
                        if ((nb & 0xFF) == 0 || blkH < 15)
                            rows_for(allMasks[3][y], static_cast<uint32_t>(voxel))[z] |= (1u << x);
                    }
                    // Face 4: -Z (side face), check z-1
                    {
                        uint16_t nb = (z > 0)
                                          ? voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * (z - 1))]
                                          : get_neighbor_voxel(5, x, y, CHUNK_SIZE - 1);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            rows_for(allMasks[4][z],
                                     static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16))[y] |= (1u << x);
                    }
                    // Face 5: +Z (side face), check z+1
                    {
                        uint16_t nb = (z + 1 < CHUNK_SIZE)
                                          ? voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * (z + 1))]
                                          : get_neighbor_voxel(4, x, y, 0);
                        const uint8_t nbTexID = nb & 0xFF;
                        const uint8_t nbH = static_cast<uint8_t>(nb >> 8);
                        if (nbTexID == 0 || blkH > nbH)
                            rows_for(allMasks[5][z],
                                     static_cast<uint32_t>(voxel) | (static_cast<uint32_t>(nbH) << 16))[y] |= (1u << x);
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

    emit_border_skirts(voxels, input.gpuTextureIds, result.faces);

    return result;
}

void PlanetChunkMesher::emit_border_skirts(const std::array<uint16_t, CHUNK_VOLUME> &voxels,
                                           const std::unordered_map<uint8_t, uint16_t> &gpuTextureIds,
                                           std::vector<TerrainFace3d> &faces) {
    VOXEL_ZONE_N("BorderSkirts");

    // {faceDir, the axis the border runs along}. Face 0 is the x = 0 wall, face 1 the x = 31 one,
    // face 4 the z = 0 wall and face 5 the z = 31 one. The greedy pass above emits none of these
    // at the borders, because it reports the lateral neighbours as solid
    struct Border {
        int faceDir;
        int fixedX; // -1 when the border runs along x
        int fixedZ; // -1 when the border runs along z
    };
    constexpr Border BORDERS[4] = {
        {0, 0, -1},
        {1, CHUNK_SIZE - 1, -1},
        {4, -1, 0},
        {5, -1, CHUNK_SIZE - 1},
    };

    for (const Border &border: BORDERS) {
        for (int along = 0; along < CHUNK_SIZE; ++along) {
            const int x = border.fixedX >= 0 ? border.fixedX : along;
            const int z = border.fixedZ >= 0 ? border.fixedZ : along;

            // Topmost solid voxel of this border column. Everything under it is either solid or
            // out of the node, so the skirt only has to start there
            int topY = -1;
            uint16_t topVoxel = 0;
            for (int y = CHUNK_SIZE - 1; y >= 0; --y) {
                const uint16_t voxel = voxels[x + CHUNK_SIZE * (y + CHUNK_SIZE * z)];
                if ((voxel & 0xFF) == 0) continue;
                topY = y;
                topVoxel = voxel;
                break;
            }

            if (topY < 0) continue; // empty column, nothing to hang a skirt from

            const auto blkH = static_cast<uint8_t>(topVoxel >> 8);

            // Sub voxel altitude of the top of the column, in the same units the face encoding
            // uses: 16 per voxel
            const int topSub = topY * 16 + blkH;
            const int bottomSub = std::max(0, topSub - SKIRT_VOXELS * 16);
            const int extentSub = topSub - bottomSub;
            if (extentSub <= 0) continue;

            uint32_t texSlot = 0;
            if (const auto it = gpuTextureIds.find(static_cast<uint8_t>(topVoxel & 0xFF));
                it != gpuTextureIds.end()) {
                texSlot = it->second;
            }

            TerrainFace3d face{};
            face.x = static_cast<uint32_t>(x);
            face.z = static_cast<uint32_t>(z);
            face.y = static_cast<uint32_t>(bottomSub);
            face.faceIndex = static_cast<uint32_t>(border.faceDir);

            // For every lateral face the vertex shader scales one corner axis by width and the
            // altitude axis by height, so one voxel wide and extentSub tall is what closes the
            // column. Both fields are stored as value - 1
            face.width = 16u - 1u;
            face.height = static_cast<uint32_t>(extentSub - 1);
            face.textureSlot = texSlot;

            faces.push_back(face);
        }
    }
}
