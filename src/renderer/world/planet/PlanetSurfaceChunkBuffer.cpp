#include "PlanetSurfaceChunkBuffer.h"

#include <algorithm>

#include "core/log/Logger.h"
#include "core/TracyIntegration.h"

namespace vp {

uint32_t round_up_granule(uint32_t vertexCount) {
    constexpr uint32_t g = PlanetSurfaceChunkBuffer::VERTEX_GRANULE;
    return (vertexCount + g - 1) / g * g;
}

PlanetSurfaceChunkBuffer::PlanetSurfaceChunkBuffer(VulkanBackend* backend, uint32_t maxVertices, uint32_t maxChunks)
    : m_maxVertices(maxVertices), m_maxChunks(maxChunks), m_backend(backend) {
    init_gpu();

    m_freeRanges.emplace(0, m_maxVertices);

    // Reversed so the lowest indices come out first, which keeps draw_count() tight
    m_freeInstances.reserve(m_maxChunks);
    for (uint32_t i = m_maxChunks; i > 0; --i) {
        m_freeInstances.push_back(i - 1);
    }
}

void PlanetSurfaceChunkBuffer::init_gpu() {
    const uint64_t verticesBufferSize = uint64_t(m_maxVertices) * sizeof(PlanetSurfaceChunkVertex);
    auto verticesDesc = nvrhi::BufferDesc()
                            .setByteSize(verticesBufferSize)
                            .setStructStride(sizeof(PlanetSurfaceChunkVertex))
                            .setDebugName("PlanetSurfaceChunkBuffer::vertices")
                            .setInitialState(nvrhi::ResourceStates::ShaderResource)
                            .setKeepInitialState(true);
    m_verticesBuffer = m_backend->device->createBuffer(verticesDesc);
    LOG_DEBUG("PlanetSurfaceChunkBuffer", "Created vertices buffer with size {} bytes", verticesBufferSize);

    const uint64_t instanceBufferSize = uint64_t(m_maxChunks) * sizeof(PlanetSurfaceChunkInstance);
    auto instanceDesc = nvrhi::BufferDesc()
                            .setByteSize(instanceBufferSize)
                            .setStructStride(sizeof(PlanetSurfaceChunkInstance))
                            .setDebugName("PlanetSurfaceChunkBuffer::instances")
                            .setInitialState(nvrhi::ResourceStates::ShaderResource)
                            .setKeepInitialState(true);
    m_chunkInstanceBuffer = m_backend->device->createBuffer(instanceDesc);
    LOG_DEBUG("PlanetSurfaceChunkBuffer", "Created instance buffer with size {} bytes", instanceBufferSize);

    const uint64_t drawBufferSize = uint64_t(m_maxChunks) * sizeof(nvrhi::DrawIndirectArguments);
    auto drawDesc = nvrhi::BufferDesc()
                        .setByteSize(drawBufferSize)
                        .setIsDrawIndirectArgs(true)
                        .setDebugName("PlanetSurfaceChunkBuffer::drawArgs")
                        .setInitialState(nvrhi::ResourceStates::IndirectArgument)
                        .setKeepInitialState(true);
    m_drawBuffer = m_backend->device->createBuffer(drawDesc);
    LOG_DEBUG("PlanetSurfaceChunkBuffer", "Created draw buffer with size {} bytes", drawBufferSize);
}

PlanetSurfaceChunkBuffer::ChunkBufferSlot
PlanetSurfaceChunkBuffer::allocate(std::shared_ptr<PlanetSurfaceChunkMesh> chunkMesh,
                                   const PlanetSurfaceChunkKey& chunkKey) {
    if (!chunkKey.valid() || !chunkMesh) {
        return INVALID_CHUNK_BUFFER_SLOT;
    }

    if (chunkMesh->vertices.size() == 0) {
        release(chunkKey);
        return INVALID_CHUNK_BUFFER_SLOT;
    }

    const uint32_t needed = round_up_granule(chunkMesh->vertices.size());

    auto it = m_chunks.find(chunkKey);
    if (it == m_chunks.end()) {
        // New chunk: an instance and a range
        if (m_freeInstances.empty()) {
            ++m_stats.failedAllocations;
            LOG_WARN("PlanetSurfaceChunkBuffer", "No free instance ({} chunks), chunk dropped", m_maxChunks);
            return INVALID_CHUNK_BUFFER_SLOT;
        }

        const auto firstVertex = allocate_range(needed);
        if (!firstVertex) {
            ++m_stats.failedAllocations;
            LOG_WARN("PlanetSurfaceChunkBuffer", "No free range of {} vertices, chunk dropped", needed);
            return INVALID_CHUNK_BUFFER_SLOT;
        }

        ChunkAllocation alloc;
        alloc.instance = m_freeInstances.back();
        m_freeInstances.pop_back();
        alloc.firstVertex = *firstVertex;
        alloc.capacity = needed;

        m_instanceHighWater = std::max(m_instanceHighWater, alloc.instance + 1);
        it = m_chunks.emplace(chunkKey, std::move(alloc)).first;
    } else {
        ChunkAllocation& alloc = it->second;

        if (needed <= alloc.capacity) {
            // Same size or smaller: rewritten in place
            if (alloc.capacity - needed >= SHRINK_THRESHOLD) {
                free_range(alloc.firstVertex + needed, alloc.capacity - needed);
                alloc.capacity = needed;
            }
        } else if (try_grow_range(alloc.firstVertex, alloc.capacity, needed)) {
            // Larger, but the range right after is free
            alloc.capacity = needed;
        } else {
            // Larger and blocked: move. Allocate first so a failure keeps the current mesh
            const auto firstVertex = allocate_range(needed);
            if (!firstVertex) {
                ++m_stats.failedAllocations;
                LOG_WARN("PlanetSurfaceChunkBuffer", "No free range of {} vertices, remesh dropped", needed);
                return INVALID_CHUNK_BUFFER_SLOT;
            }

            free_range(alloc.firstVertex, alloc.capacity);
            alloc.firstVertex = *firstVertex;
            alloc.capacity = needed;
            ++m_stats.relocations;
        }
    }

    ChunkAllocation& alloc = it->second;
    if (!alloc.pending) {
        m_dirty.push_back(chunkKey);
    }
    alloc.pending = std::move(chunkMesh); // the last call wins

    return alloc.instance;
}

void PlanetSurfaceChunkBuffer::release(const PlanetSurfaceChunkKey& chunkKey) {
    const auto it = m_chunks.find(chunkKey);
    if (it == m_chunks.end()) {
        return;
    }

    const ChunkAllocation& alloc = it->second;
    free_range(alloc.firstVertex, alloc.capacity);
    m_freeInstances.push_back(alloc.instance);
    m_releasedInstances.push_back(alloc.instance);

    m_chunks.erase(it);
}

void PlanetSurfaceChunkBuffer::upload_pending_chunks(nvrhi::ICommandList* cmd) {
    VOXEL_ZONE_N("PlanetSurfaceChunkBuffer-Upload");
    m_stats.uploadsThisFrame = 0;

    if (!m_drawBufferCleared) {
        cmd->clearBufferUInt(m_drawBuffer, 0);
        m_drawBufferCleared = true;
    }

    // Clear the draw args of released instances
    nvrhi::DrawIndirectArguments emptyArgs;
    emptyArgs.instanceCount = 0;
    for (const uint32_t instance : m_releasedInstances) {
        cmd->writeBuffer(m_drawBuffer, &emptyArgs, sizeof(emptyArgs),
                         uint64_t(instance) * sizeof(nvrhi::DrawIndirectArguments));
    }
    m_releasedInstances.clear();

    // for each dirty chunk, upload the vertices and instance data, and write the draw args
    for (const PlanetSurfaceChunkKey& key : m_dirty) {
        const auto it = m_chunks.find(key);
        if (it == m_chunks.end() || !it->second.pending) {
            continue; // released meanwhile, or already uploaded through a duplicate entry
        }

        ChunkAllocation& alloc = it->second;
        const auto& vertices = alloc.pending->vertices;

        cmd->writeBuffer(m_verticesBuffer, vertices.data(), vertices.size() * sizeof(PlanetSurfaceChunkVertex),
                         uint64_t(alloc.firstVertex) * sizeof(PlanetSurfaceChunkVertex));

        PlanetSurfaceChunkInstance instance{};
        instance.chunkFacePos = {key.x, key.y, key.alt};
        cmd->writeBuffer(m_chunkInstanceBuffer, &instance, sizeof(instance),
                         uint64_t(alloc.instance) * sizeof(PlanetSurfaceChunkInstance));

        alloc.vertexCount = static_cast<uint32_t>(vertices.size());

        nvrhi::DrawIndirectArguments args;
        args.vertexCount = alloc.vertexCount;
        args.instanceCount = 1;
        args.startVertexLocation = alloc.firstVertex;
        args.startInstanceLocation = alloc.instance; // TODO: the drawIndirectFirstInstance device feature
        cmd->writeBuffer(m_drawBuffer, &args, sizeof(args),
                         uint64_t(alloc.instance) * sizeof(nvrhi::DrawIndirectArguments));

        alloc.pending.reset();
        ++m_stats.uploadsThisFrame;
    }
    m_dirty.clear();

    refresh_stats();
}

std::optional<uint32_t> PlanetSurfaceChunkBuffer::allocate_range(uint32_t size) {
    auto best = m_freeRanges.end();
    for (auto it = m_freeRanges.begin(); it != m_freeRanges.end(); ++it) {
        if (it->second >= size && (best == m_freeRanges.end() || it->second < best->second)) {
            best = it;
        }
    }
    if (best == m_freeRanges.end()) {
        return std::nullopt;
    }

    const uint32_t firstVertex = best->first;
    const uint32_t rest = best->second - size;
    m_freeRanges.erase(best);
    if (rest > 0) {
        m_freeRanges.emplace(firstVertex + size, rest);
    }

    return firstVertex;
}

void PlanetSurfaceChunkBuffer::free_range(uint32_t firstVertex, uint32_t vertexCount) {
    auto next = m_freeRanges.lower_bound(firstVertex);

    // Merge with the free range right after
    if (next != m_freeRanges.end() && firstVertex + vertexCount == next->first) {
        vertexCount += next->second;
        next = m_freeRanges.erase(next);
    }

    // Merge with the free range right before
    if (next != m_freeRanges.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == firstVertex) {
            prev->second += vertexCount;
            return;
        }
    }

    m_freeRanges.emplace_hint(next, firstVertex, vertexCount);
}

bool PlanetSurfaceChunkBuffer::try_grow_range(uint32_t firstVertex, uint32_t oldSize, uint32_t newSize) {
    auto next = m_freeRanges.find(firstVertex + oldSize);
    const uint32_t extra = newSize - oldSize;
    if (next == m_freeRanges.end() || next->second < extra)
        return false;

    const uint32_t rest = next->second - extra;
    m_freeRanges.erase(next);
    if (rest > 0)
        m_freeRanges.emplace(firstVertex + newSize, rest);

    return true;
}

void PlanetSurfaceChunkBuffer::refresh_stats() {
    uint32_t freeVertices = 0;
    uint32_t largest = 0;
    for (const auto& range : m_freeRanges) {
        freeVertices += range.second;
        largest = std::max(largest, range.second);
    }

    uint32_t used = 0;
    for (const auto& entry : m_chunks)
        used += entry.second.vertexCount;

    m_stats.residentChunks = static_cast<uint32_t>(m_chunks.size());
    m_stats.reservedVertices = m_maxVertices - freeVertices;
    m_stats.usedVertices = used;
    m_stats.freeRanges = static_cast<uint32_t>(m_freeRanges.size());
    m_stats.largestFreeRange = largest;
}

} // namespace vp
