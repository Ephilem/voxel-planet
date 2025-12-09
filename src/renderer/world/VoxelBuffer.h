#pragma once

#include "nvrhi/nvrhi.h"
#include "renderer/render_types.h"
#include "renderer/vulkan/VulkanBackend.h"


// ==================== CONFIGURATION OPTIMALE ====================
static constexpr uint64_t TOTAL_BUFFER_SIZE = 64 * 1024 * 1024; // 64 MB

static constexpr uint32_t VERTEX_SIZE = sizeof(Vertex3d);
static constexpr uint32_t INDEX_SIZE = sizeof(uint32_t);
static constexpr uint32_t INDIRECT_CMD_SIZE = sizeof(VkDrawIndexedIndirectCommand);

static constexpr float INDEX_TO_VERTEX_RATIO = 1.5f;

// Tailles des régions
static constexpr uint32_t VERTICES_PER_REGION = 341;
static constexpr uint32_t VERTEX_REGION_SIZE = VERTICES_PER_REGION * VERTEX_SIZE;
static constexpr uint32_t INDICES_PER_VERTEX_REGION = static_cast<uint32_t>(VERTICES_PER_REGION * INDEX_TO_VERTEX_RATIO); // 511
static constexpr uint32_t INDEX_REGION_SIZE = ((INDICES_PER_VERTEX_REGION * INDEX_SIZE + 1023) / 1024) * 1024; // 2048 bytes
static constexpr uint32_t INDIRECT_REGION_SIZE = INDIRECT_CMD_SIZE;

// Sections optimales (63/32/5) - TOUS ALIGNÉS À 64 BYTES
static constexpr uint64_t VERTEX_SECTION_SIZE = 42278528ULL;    // 40.32 MB (63%)
static constexpr uint64_t INDEX_SECTION_SIZE = 21474816ULL;     // 20.48 MB (32%)
static constexpr uint64_t INDIRECT_SECTION_SIZE = 3355520ULL;   // 3.20 MB (5%)

// Offsets des sections
static constexpr uint64_t VERTEX_SECTION_OFFSET = 0;
static constexpr uint64_t INDEX_SECTION_OFFSET = 42278528ULL;
static constexpr uint64_t INDIRECT_SECTION_OFFSET = 63753344ULL;

// Capacités maximales
static constexpr uint32_t MAX_VERTEX_REGION = 10321;    // Vertex section / région
static constexpr uint32_t MAX_INDEX_REGION = 10485;     // Index section / région
static constexpr uint64_t MAX_INDIRECT_REGION = 52430;  // Indirect section / région

// Vérifications de compilation
static_assert(VERTEX_SECTION_SIZE + INDEX_SECTION_SIZE + INDIRECT_SECTION_SIZE == TOTAL_BUFFER_SIZE,
              "Sections must sum to total buffer size");
static_assert(INDEX_SECTION_OFFSET % 4 == 0,
              "Index offset MUST be aligned to 4 bytes for VK_INDEX_TYPE_UINT32");
static_assert(INDEX_SECTION_OFFSET % 64 == 0,
              "Index offset should be aligned to 64 bytes for cache efficiency");
static_assert(INDIRECT_SECTION_OFFSET % 64 == 0,
              "Indirect offset should be aligned to 64 bytes");





// This class represent a buffer that can hold multiple voxel chunks in GPU memory in a limit of TOTAL_BUFFER_SIZE
class VoxelBuffer {
    VulkanBackend* m_backend;

    // Single 64 MB buffer with all three usage flags
    nvrhi::BufferHandle m_buffer;

    // Separate free lists for each section (start region, count)
    std::vector<std::pair<uint32_t, uint32_t>> m_freeVertexRegions;
    std::vector<std::pair<uint32_t, uint32_t>> m_freeIndexRegions;
    std::vector<std::pair<uint32_t, uint32_t>> m_freeIndirectRegions;

    void init();

    // Helper methods for region management
    bool allocate_regions(std::vector<std::pair<uint32_t, uint32_t>>& freeList,
                        uint32_t regionCount, uint32_t& outStart);
    void free_regions(std::vector<std::pair<uint32_t, uint32_t>>& freeList,
                    uint32_t start, uint32_t count);

public:
    VoxelBuffer(VulkanBackend* backend);
    ~VoxelBuffer();

    /**
     * Test if allocation is possible for the given mesh data
     * @param vertexCount The count of vertices to allocate
     * @param indexCount The count of indices to allocate
     * @return True if allocation is possible, false otherwise
     */
    bool can_allocate(uint32_t vertexCount, uint32_t indexCount);

    /**
     * Allocate space in the buffer for a chunk mesh
     * Updates the VoxelChunkMesh component with allocation info
     * @param mesh VoxelChunkMesh component to fill with allocation data
     * @return True if allocation succeeded, false otherwise
     */
    bool allocate(struct VoxelChunkMesh& mesh);

    /**
     * Write data of an already allocated mesh to the buffer.
     * Warning: it will not check if allocate in this buffer or not
     * @param mesh Allocated mesh data to write in the buffer
     */
    void write(nvrhi::CommandListHandle cmd, struct VoxelChunkMesh& mesh);

    /**
     * Free a previously allocated chunk
     * @param mesh The VoxelChunkMesh component with allocation data
     */
    void free(struct VoxelChunkMesh& mesh);

    /**
     * Get the underlying buffer handle
     */
    nvrhi::BufferHandle get_buffer() const { return m_buffer; }

    /**
     * Get byte offset for a vertex region
     */
    uint64_t get_vertex_offset(uint32_t regionStart) const {
        return VERTEX_SECTION_OFFSET + (regionStart * VERTEX_REGION_SIZE);
    }

    /**
     * Get byte offset for an index region
     */
    uint64_t get_index_offset(uint32_t regionStart) const {
        return INDEX_SECTION_OFFSET + (regionStart * INDEX_REGION_SIZE);
    }

    /**
     * Get byte offset for an indirect region
     */
    uint64_t get_indirect_offset(uint32_t regionIndex) const {
        return INDIRECT_SECTION_OFFSET + (regionIndex * INDIRECT_REGION_SIZE);
    }

    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_vertex_regions() const { return m_freeVertexRegions; }
    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_index_regions() const { return m_freeIndexRegions; }
    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_indirect_regions() const { return m_freeIndirectRegions; }

    uint32_t get_used_vertex_regions() const;
    uint32_t get_used_index_regions() const;
    uint32_t get_used_indirect_regions() const;

    uint32_t get_largest_free_vertex_block() const;
    uint32_t get_largest_free_index_block() const;
    uint32_t get_largest_free_indirect_block() const;
};