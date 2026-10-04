#include "PlanetSurfaceChunkMesher.h"
#include "core/log/Logger.h"
#include "core/TracyIntegration.h"

#include <algorithm>
#include <cmath>

namespace vp {

static_assert(MAX_VOXEL_TEXTURE_SLOTS <= (1 << 12), "PlanetSurfaceChunkVertex::textureSlot is 12 bits");

PlanetSurfaceChunkMesher::PlanetSurfaceChunkMesher(const PlanetVoxelRenderTable* renderTable, unsigned workerCount) {
    m_voxelRenderTable = renderTable;

    if (workerCount == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        const unsigned total = hw > 4 ? hw - 2 : 2;
        workerCount = std::max(1U, total - std::max(1U, total * 6 / 10));
    }

    m_workerThreads.reserve(workerCount);
    for (unsigned i = 0; i < workerCount; ++i) {
        m_workerThreads.emplace_back([this](std::stop_token stop) { worker_loop(std::move(stop)); });
    }
}

PlanetSurfaceChunkMesher::~PlanetSurfaceChunkMesher() {
    for (auto& w : m_workerThreads) {
        w.request_stop();
    }
    for (auto& w : m_workerThreads) {
        w.join();
    }
}

void PlanetSurfaceChunkMesher::enqueue(const PlanetSurfaceChunkKey& key,
                                       const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk,
                                       const PlanetSurfaceVoxelChunkNeighbors& neighbors) {
    // early out for chunk without voxel data
    if (!chunk->is_allocated()) {
        ++m_stats.skippedUnallocated;
        return;
    }

    const uint32_t generation = m_nextGeneration++;
    m_latest[key] = generation; // supersedes any older meshing still in flight
    m_taskQueue.enqueue({.key = key, .chunk = chunk, .neighbors = neighbors, .generation = generation});
}

uint32_t PlanetSurfaceChunkMesher::drain(std::vector<MeshingResult>& outResults, uint32_t maxResults) {
    uint32_t count = 0;
    MeshingResult result;

    while (count < maxResults && m_resultQueue.try_dequeue(result)) {
        // chunk unloaded meanwhile, or a newer remesh was enqueued
        const auto it = m_latest.find(result.key);
        if (it == m_latest.end() || it->second != result.mesh->generation) {
            ++m_stats.discardedStale;
            continue;
        }
        m_latest.erase(it);

        if (result.mesh->vertices.empty()) {
            ++m_stats.emptyMeshes;
        }

        outResults.emplace_back(std::move(result));
        ++count;
    }

    return count;
}

void PlanetSurfaceChunkMesher::worker_loop(std::stop_token stopToken) {
    moodycamel::ConsumerToken token(m_taskQueue);

    MeshingTask task;
    while (!stopToken.stop_requested()) {
        if (!m_taskQueue.wait_dequeue_timed(token, task, std::chrono::milliseconds(50))) {
            continue;
        }

        if (task.chunk == nullptr) {
            LOG_WARN("MesherWorker", "Chunk {} has invalid chunk data (null pointer)", task.key);
            continue;
        }

        std::shared_ptr<PlanetSurfaceChunkMesh> mesh = std::make_shared<PlanetSurfaceChunkMesh>();
        mesh->generation = task.generation;
        MeshingResult result;
        result.key = task.key;
        mesh_chunk(task.key, task.chunk, task.neighbors, mesh);
        result.mesh = mesh;
        m_resultQueue.enqueue(std::move(result));
    }
}

bool PlanetSurfaceChunkMesher::voxel_occludes(PlanetSurfaceChunkVoxelInfo raw) {
    return raw.localBlockID != LocalVoxelID::Air;
}

uint32_t PlanetSurfaceChunkMesher::encode_normal(glm::vec3 n) {
    n /= std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
    glm::vec2 e(n.x, n.y);
    if (n.z < 0.f) {
        // fold the lower hemisphere over the diagonals
        const glm::vec2 s(e.x >= 0.f ? 1.f : -1.f, e.y >= 0.f ? 1.f : -1.f);
        e = (glm::vec2(1.f) - glm::abs(glm::vec2(e.y, e.x))) * s;
    }
    const auto quantize = [](float v) { return uint32_t(std::lround((v * 0.5f + 0.5f) * 255.f)); };
    return quantize(e.x) | (quantize(e.y) << 8);
}

void PlanetSurfaceChunkMesher::fill_occupancy(PaddedOccupancy& occ, const PlanetSurfaceVoxelChunk& chunk,
                                              const PlanetSurfaceVoxelChunkNeighbors& neighbors) {
    occ.fill(0);

    const PlanetVoxelArray& voxels = *chunk.voxels;
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                occ[padded_index(x, y, z)] = voxel_occludes(voxels[planet_voxel_index(x, y, z)]);
            }
        }
    }

    // copy one border slice of a neighbor, (u, v) spans the two axes of the slice
    const auto border = [&occ](const PlanetSurfaceVoxelChunk* neighbor, auto dstOf, auto srcOf) {
        if (neighbor == nullptr) {
            for (int v = 0; v < CHUNK_SIZE; ++v) {
                for (int u = 0; u < CHUNK_SIZE; ++u) {
                    occ[dstOf(u, v)] = kMissingNeighbor;
                }
            }
            return;
        }
        if (!neighbor->is_allocated()) {
            return; // only air, already 0
        }
        const PlanetVoxelArray& nv = *neighbor->voxels;
        for (int v = 0; v < CHUNK_SIZE; ++v) {
            for (int u = 0; u < CHUNK_SIZE; ++u) {
                occ[dstOf(u, v)] = voxel_occludes(nv[srcOf(u, v)]);
            }
        }
    };

    constexpr int kLast = CHUNK_SIZE - 1;
    border(
        neighbors.px.get(), [](int u, int v) { return padded_index(CHUNK_SIZE, u, v); },
        [](int u, int v) { return planet_voxel_index(0, u, v); });
    border(
        neighbors.nx.get(), [](int u, int v) { return padded_index(-1, u, v); },
        [](int u, int v) { return planet_voxel_index(kLast, u, v); });
    border(
        neighbors.py.get(), [](int u, int v) { return padded_index(u, CHUNK_SIZE, v); },
        [](int u, int v) { return planet_voxel_index(u, 0, v); });
    border(
        neighbors.ny.get(), [](int u, int v) { return padded_index(u, -1, v); },
        [](int u, int v) { return planet_voxel_index(u, kLast, v); });
    border(
        neighbors.pz.get(), [](int u, int v) { return padded_index(u, v, CHUNK_SIZE); },
        [](int u, int v) { return planet_voxel_index(u, v, 0); });
    border(
        neighbors.nz.get(), [](int u, int v) { return padded_index(u, v, -1); },
        [](int u, int v) { return planet_voxel_index(u, v, kLast); });
}

void PlanetSurfaceChunkMesher::mesh_chunk(const PlanetSurfaceChunkKey& key,
                                          const std::shared_ptr<PlanetSurfaceVoxelChunk>& chunk,
                                          const PlanetSurfaceVoxelChunkNeighbors& neighbors,
                                          std::shared_ptr<PlanetSurfaceChunkMesh>& outMesh) {
    VOXEL_ZONE_N("mesh_chunk");

    auto& vertices = outMesh->vertices;

    vertices.reserve(CHUNK_SIZE * CHUNK_SIZE * 6 * 6);

    // compose fast lookup table for render info
    std::array<PlanetVoxelRenderInfo, 256> local{};
    const auto& byLocal = chunk->palette.byLocalId;
    for (size_t i = 0; i < byLocal.size() && i < local.size(); ++i) {
        local[i] = m_voxelRenderTable->get_voxel_render_info(byLocal[i]);
    }

    // dual contouring

    // corner i : bit0 = +x, bit1 = +y, bit2 = +z
    static constexpr glm::ivec3 kCellCorners[8] = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
    };

    // pairs of corners
    static constexpr int kCellEdges[12][2] = {
        {0, 1}, {2, 3}, {4, 5}, {6, 7}, // X
        {0, 2}, {1, 3}, {4, 6}, {5, 7}, // Y
        {0, 4}, {1, 5}, {2, 6}, {3, 7}, // Z
    };

    // density samples 0..CHUNK_SIZE+1 along each axis
    constexpr int kDensityDim = CHUNK_SIZE + 2;
    constexpr int kCellDim = CHUNK_SIZE + 1;

    // thread local buffers
    thread_local static std::array<float, kDensityDim * kDensityDim * kDensityDim> densityGrid;
    thread_local static std::array<uint16_t, kDensityDim * kDensityDim * kDensityDim> textureGrid; // texture slot
    thread_local static std::array<int32_t, kCellDim * kCellDim * kCellDim> cellVertex;            // -1 = no vertex
    thread_local static std::vector<PlanetSurfaceChunkVertex> cellVertices;

    const auto densityAt = [](glm::ivec3 p) {
        return densityGrid[p.x + (p.y * kDensityDim) + (p.z * kDensityDim * kDensityDim)];
    };
    const auto cellIndex = [](glm::ivec3 c) { return c.x + (c.y * kCellDim) + (c.z * kCellDim * kCellDim); };

    // 1. fill density table with quantized density values
    constexpr int kLast = CHUNK_SIZE - 1;
    for (int z = 0; z < kDensityDim; ++z) {
        for (int y = 0; y < kDensityDim; ++y) {
            for (int x = 0; x < kDensityDim; ++x) {
                const int ox = x >= CHUNK_SIZE ? 1 : 0;
                const int oy = y >= CHUNK_SIZE ? 1 : 0;
                const int oz = z >= CHUNK_SIZE ? 1 : 0;

                const PlanetSurfaceVoxelChunk* src = nullptr;
                switch (ox | (oy << 1) | (oz << 2)) {
                case 0:
                    src = chunk.get();
                    break;
                case 1:
                    src = neighbors.px.get();
                    break;
                case 2:
                    src = neighbors.py.get();
                    break;
                case 3:
                    src = neighbors.pxpy.get();
                    break;
                case 4:
                    src = neighbors.pz.get();
                    break;
                case 5:
                    src = neighbors.pxpz.get();
                    break;
                case 6:
                    src = neighbors.pypz.get();
                    break;
                case 7:
                    src = neighbors.pxpypz.get();
                    break;
                default:
                    break;
                }

                const int gridIndex = x + (y * kDensityDim) + (z * kDensityDim * kDensityDim);
                if (src == nullptr) {
                    // neighbor not loaded yet: clamp into this chunk, remeshed once it arrives
                    const glm::ivec3 s(std::min(x, kLast), std::min(y, kLast), std::min(z, kLast));
                    densityGrid[gridIndex] = chunk->sample_quantized_density(s);
                    textureGrid[gridIndex] = local[static_cast<uint8_t>(chunk->at(s).localBlockID)].textureSlot;
                    continue;
                }

                const glm::ivec3 s(x - (ox * CHUNK_SIZE), y - (oy * CHUNK_SIZE), z - (oz * CHUNK_SIZE));
                densityGrid[gridIndex] = src->sample_quantized_density(s);
                if (src == chunk.get()) {
                    textureGrid[gridIndex] = local[static_cast<uint8_t>(chunk->at(s).localBlockID)].textureSlot;
                } else if (src->is_allocated()) {
                    // neighbor: resolved through its own palette
                    const VoxelID id = src->palette.global(src->at(s).localBlockID);
                    textureGrid[gridIndex] = m_voxelRenderTable->get_voxel_render_info(id).textureSlot;
                } else {
                    textureGrid[gridIndex] = 0; // only air, never a solid corner
                }
            }
        }
    }

    // 2. one vertex per cell crossed by the surface, placed by the QEF
    cellVertex.fill(-1);
    cellVertices.clear();

    for (int z = 0; z < kCellDim; ++z) {
        for (int y = 0; y < kCellDim; ++y) {
            for (int x = 0; x < kCellDim; ++x) {
                const glm::ivec3 cell(x, y, z);

                float d[8];
                int mask = 0; // bit i set = corner i inside the solid
                for (int i = 0; i < 8; ++i) {
                    d[i] = densityAt(cell + kCellCorners[i]);
                    if (d[i] < 0.f) {
                        mask |= 1 << i;
                    }
                }

                // fully inside or outside: no surface in this cell
                if (mask == 0 || mask == 0xFF) {
                    continue;
                }

                // continuous density inside the cell: trilinear interpolation of the 8 corners, p local in [0,1]^3
                const auto f = [&d](glm::vec3 p) -> float {
                    const float x00 = glm::mix(d[0], d[1], p.x);
                    const float x10 = glm::mix(d[2], d[3], p.x);
                    const float x01 = glm::mix(d[4], d[5], p.x);
                    const float x11 = glm::mix(d[6], d[7], p.x);
                    return glm::mix(glm::mix(x00, x10, p.y), glm::mix(x01, x11, p.y), p.z);
                };

                const auto gradient = [&f](glm::vec3 p) -> glm::vec3 {
                    constexpr float h = 0.001f;
                    return glm::vec3(f(p + glm::vec3(h, 0.f, 0.f)) - f(p - glm::vec3(h, 0.f, 0.f)),
                                     f(p + glm::vec3(0.f, h, 0.f)) - f(p - glm::vec3(0.f, h, 0.f)),
                                     f(p + glm::vec3(0.f, 0.f, h)) - f(p - glm::vec3(0.f, 0.f, h))) /
                           (2.f * h);
                };

                // QEF accumulators
                glm::mat3 ata(0.f);
                glm::vec3 atb(0.f);
                glm::vec3 pointSum(0.f);
                int count = 0;

                for (int e = 0; e < 12; ++e) {
                    const int c0 = kCellEdges[e][0];
                    const int c1 = kCellEdges[e][1];
                    // test if this edge doesn't cross the surface
                    if (((mask >> c0) & 1) == ((mask >> c1) & 1)) {
                        continue;
                    }

                    // intersection point on the edge (local to the cell, in [0,1]^3)
                    const float t = d[c0] / (d[c0] - d[c1]);
                    const glm::vec3 p = glm::mix(glm::vec3(kCellCorners[c0]), glm::vec3(kCellCorners[c1]), t);
                    pointSum += p;
                    ++count;

                    // a null gradient gives no plane, the point only counts for the mass point
                    const glm::vec3 g = gradient(p);
                    const float len = glm::length(g);
                    if (len > 1e-6f) {
                        const glm::vec3 n = g / len;
                        ata += glm::outerProduct(n, n);
                        atb += n * glm::dot(n, p);
                    }
                }

                // solve the QEF around the mass point, bias keeps flat cells solvable
                constexpr float kBias = 0.1f;
                const glm::vec3 massPoint = pointSum / float(count);
                const glm::vec3 offset = glm::inverse(ata + glm::mat3(kBias)) * (atb - (ata * massPoint));
                const glm::vec3 vertexLocal = glm::clamp(massPoint + offset, glm::vec3(0.f), glm::vec3(1.f));
                const glm::vec3 vertex = glm::vec3(cell) + vertexLocal;

                // smooth normal: density gradient at the vertex, points toward the air. Only depends on the cell
                // corners, so a vertex shared by two chunks gets the same normal on both sides
                const glm::vec3 g = gradient(vertexLocal);
                const glm::vec3 normal = glm::dot(g, g) > 1e-12f ? glm::normalize(g) : glm::vec3(0.f, 0.f, 1.f);

                cellVertex[cellIndex(cell)] = static_cast<int32_t>(cellVertices.size());
                cellVertices.push_back(PlanetSurfaceChunkVertex{
                    .x = uint32_t(std::lround(vertex.x * PLANET_SURFACE_VERTEX_SCALE)),
                    .y = uint32_t(std::lround(vertex.y * PLANET_SURFACE_VERTEX_SCALE)),
                    .z = uint32_t(std::lround(vertex.z * PLANET_SURFACE_VERTEX_SCALE)),
                    .textureSlot = 0, // set per quad
                    .face = 0,        // set per quad
                    .normal = encode_normal(normal),
                });
            }
        }
    }

    // 3. one quad per grid edge crossed by the surface, joining the vertices of the 4 cells around it
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        glm::ivec3 da(0);
        glm::ivec3 du(0);
        glm::ivec3 dv(0);
        da[axis] = 1;   
        du[u] = 1;
        dv[v] = 1;

        glm::ivec3 p;
        for (p.z = 0; p.z <= CHUNK_SIZE; ++p.z) {
            for (p.y = 0; p.y <= CHUNK_SIZE; ++p.y) {
                for (p.x = 0; p.x <= CHUNK_SIZE; ++p.x) {
                    if (p[axis] == CHUNK_SIZE || p[u] == 0 || p[v] == 0) {
                        continue;
                    }

                    const bool inside0 = densityAt(p) < 0.f;
                    const bool inside1 = densityAt(p + da) < 0.f;
                    if (inside0 == inside1) {
                        continue;
                    }
                    const glm::ivec3 solidPt = inside0 ? p : p + da;
                    const uint16_t slot =
                        textureGrid[solidPt.x + solidPt.y * kDensityDim + solidPt.z * kDensityDim * kDensityDim];

                    // the 4 cells sharing the edge, counter-clockwise seen from +axis
                    const int32_t q[4] = {
                        cellVertex[cellIndex(p - du - dv)],
                        cellVertex[cellIndex(p - dv)],
                        cellVertex[cellIndex(p)],
                        cellVertex[cellIndex(p - du)],
                    };
                    if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[3] < 0) {
                        continue; // cannot happen: the 4 cells all see this sign change
                    }

                    // face from the dominant axis of the quad geometric normal (crossed diagonals), not from the
                    // edge axis: on gentle slopes X/Y edges give near horizontal quads
                    const auto pos = [&q](int c) {
                        const PlanetSurfaceChunkVertex& vx = cellVertices[q[c]];
                        return glm::vec3(vx.x, vx.y, vx.z);
                    };
                    glm::vec3 n = glm::cross(pos(2) - pos(0), pos(3) - pos(1));
                    if (!inside0) {
                        n = -n;
                    }
                    uint32_t face = (axis * 2) + (inside0 ? 0 : 1); // fallback for a flat degenerate quad
                    if (glm::dot(n, n) > 1e-6f) {
                        const glm::vec3 a = glm::abs(n);
                        const int dom = (a.z >= a.x && a.z >= a.y) ? 2 : (a.x >= a.y ? 0 : 1);
                        face = (dom * 2) + (n[dom] > 0.f ? 0 : 1);
                    }

                    std::array<PlanetSurfaceChunkVertex, 4> quad;
                    for (int c = 0; c < 4; ++c) {
                        quad[c] = cellVertices[q[c]];
                        quad[c].face = face;
                        quad[c].textureSlot = slot;
                    }

                    // solid -> air along +axis: the quad faces +axis, otherwise flip the winding
                    if (inside0) {
                        vertices.insert(vertices.end(), {quad[0], quad[2], quad[1], quad[0], quad[3], quad[2]});
                    } else {
                        vertices.insert(vertices.end(), {quad[0], quad[2], quad[3], quad[0], quad[1], quad[2]});
                    }
                }
            }
        }
    }

    vertices.shrink_to_fit();

    // for (int z = 0; z < CHUNK_SIZE; ++z) {
    //     for (int y = 0; y < CHUNK_SIZE; ++y) {
    //         for (int x = 0; x < CHUNK_SIZE; ++x) {
    //             const PlanetSurfaceChunkBlockInfo block = chunk->at(x, y, z);
    //             if (block.localBlockID == LocalVoxelID::Air) {
    //                 continue;
    //             }

    //             const PlanetVoxelRenderInfo& renderInfo = local[static_cast<uint8_t>(block.localBlockID)];
    //             if (!renderInfo.visible) {
    //                 continue;
    //             }

    //             const int base = padded_index(x, y, z);
    //             for (uint32_t f = 0; f < 6; ++f) {
    //                 if (occ[base + kNeighborOffset[f]] != 0) {
    //                     continue;
    //                 }

    //                 std::array<PlanetSurfaceChunkVertex, 4> quad;
    //                 for (int c = 0; c < 4; ++c) {
    //                     const glm::ivec3 p = glm::ivec3(x, y, z) + kFaceCorners[f][c];
    //                     quad[c] = {
    //                         .x = uint8_t(p.x),
    //                         .y = uint8_t(p.y),
    //                         .z = uint8_t(p.z),
    //                         .face = f,
    //                         .textureSlot = renderInfo.textureSlot,
    //                     };
    //                 }

    //                 vertices.insert(vertices.end(), {quad[0], quad[2], quad[1], quad[0], quad[3], quad[2]});
    //             }
    //         }
    //     }
    // }

    // vertices.shrink_to_fit();
}

} // namespace vp
