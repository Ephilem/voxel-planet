#pragma once

#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/world/planet/planet_types.h"
#include "nvrhi/nvrhi.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "renderer/world/planet/planet_rendering_types.h"

namespace vp {
/**
 * GPU storage of the planet surface chunk meshes.
 */
class PlanetSurfaceChunkBuffer {
public:
    using ChunkBufferSlot = uint32_t;
    static constexpr ChunkBufferSlot INVALID_CHUNK_BUFFER_SLOT = UINT32_MAX;

    static constexpr uint32_t VERTEX_GRANULE = 64;
    static constexpr uint32_t SHRINK_THRESHOLD = 256;

    PlanetSurfaceChunkBuffer(VulkanBackend* backend, uint32_t maxVertices = 1U << 23, uint32_t maxChunks = 4096);

    ~PlanetSurfaceChunkBuffer() = default;

    PlanetSurfaceChunkBuffer(const PlanetSurfaceChunkBuffer&) = delete;
    PlanetSurfaceChunkBuffer& operator=(const PlanetSurfaceChunkBuffer&) = delete;

    /**
     * Stores the mesh of a chunk, or replaces it when the chunk is already resident
     */
    ChunkBufferSlot allocate(std::shared_ptr<PlanetSurfaceChunkMesh> chunkMesh, const PlanetSurfaceChunkKey& chunkKey);

    void release(const PlanetSurfaceChunkKey& chunkKey);

    /**
     * Records the copies of every pending mesh. Call once per frame, in the command list of the draw and before it
     */
    void upload_pending_chunks(nvrhi::ICommandList* commandList);

    [[nodiscard]] nvrhi::BufferHandle vertices_buffer() const { return m_verticesBuffer; }

    [[nodiscard]] nvrhi::BufferHandle instance_buffer() const { return m_chunkInstanceBuffer; }

    [[nodiscard]] nvrhi::BufferHandle draw_buffer() const { return m_drawBuffer; }

    [[nodiscard]] uint32_t draw_count() const { return m_instanceHighWater; }

    struct Stats {
        uint32_t residentChunks = 0;
        uint32_t reservedVertices = 0;
        uint32_t usedVertices = 0;
        uint32_t freeRanges = 0;
        uint32_t largestFreeRange = 0;
        uint32_t uploadsThisFrame = 0;
        uint32_t relocations = 0;
        uint32_t failedAllocations = 0;
    };

    [[nodiscard]] const Stats& stats() const { return m_stats; }

private:
    struct ChunkAllocation {
        uint32_t instance = INVALID_CHUNK_BUFFER_SLOT;
        uint32_t firstVertex = 0;
        uint32_t capacity = 0;
        uint32_t vertexCount = 0;

        std::shared_ptr<PlanetSurfaceChunkMesh> pending; // not null = to upload
    };

    void init_gpu();

    /**
     * Return the first vertex index in the buffer for a range of size "size" vertices. Returns nullopt if no range is
     * available
     */
    std::optional<uint32_t> allocate_range(uint32_t size);

    /**
     * Free a range of vertices in the buffer. Merged with ranges before and after (if there are free)
     */
    void free_range(uint32_t firstVertex, uint32_t vertexCount);

    /**
     * Try to grow a range of vertices in place
     */
    bool try_grow_range(uint32_t firstVertex, uint32_t oldSize, uint32_t newSize);

    void refresh_stats();

    uint32_t m_maxVertices;
    uint32_t m_maxChunks;

    std::unordered_map<PlanetSurfaceChunkKey, ChunkAllocation> m_chunks;
    std::map<uint32_t, uint32_t>
        m_freeRanges; // first vertex mapped to vertices count in the range. sorted for the merges

    std::vector<uint32_t> m_freeInstances;
    uint32_t m_instanceHighWater = 0;

    std::vector<PlanetSurfaceChunkKey> m_dirty;
    std::vector<uint32_t> m_releasedInstances; // instances to clear in the buffer.
    bool m_drawBufferCleared = false;

    Stats m_stats;

    VulkanBackend* m_backend;
    nvrhi::BufferHandle m_verticesBuffer;
    nvrhi::BufferHandle m_chunkInstanceBuffer;
    nvrhi::BufferHandle m_drawBuffer;
};
} // namespace vp
