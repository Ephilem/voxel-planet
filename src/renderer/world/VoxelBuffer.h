#pragma once

#include "nvrhi/nvrhi.h"
#include "renderer/render_types.h"
#include "renderer/vulkan/VulkanBackend.h"


struct alignas(16) TerrainOUB {
    glm::mat4 model; // Model matrice
};

// Represent a draw to do
struct ChunkDrawData {
    nvrhi::DrawIndexedIndirectArguments drawCmd;
    TerrainOUB oubData;
};

static constexpr uint64_t TOTAL_BUFFER_SIZE = 64 * 1024 * 1024; // 64 MB

static constexpr uint32_t VERTEX_SIZE = sizeof(TerrainVertex3d);
static constexpr uint32_t INDEX_SIZE = sizeof(uint32_t);

static constexpr float INDEX_TO_VERTEX_RATIO = 5.0f;

static constexpr uint32_t VERTICES_PER_REGION = 300;
static constexpr uint32_t VERTEX_REGION_SIZE = VERTICES_PER_REGION * VERTEX_SIZE;
static constexpr uint32_t INDICES_PER_VERTEX_REGION = static_cast<uint32_t>(VERTICES_PER_REGION * INDEX_TO_VERTEX_RATIO);
static constexpr uint32_t INDEX_REGION_SIZE = ((INDICES_PER_VERTEX_REGION * INDEX_SIZE + 1023) / 1024) * 1024;

static constexpr float VERTEX_TO_INDEX_BYTE_RATIO = 0.67f;

static constexpr float VERTEX_FRACTION = VERTEX_TO_INDEX_BYTE_RATIO / (VERTEX_TO_INDEX_BYTE_RATIO + 1.0f);
static constexpr float INDEX_FRACTION = 1.0f - VERTEX_FRACTION;

static constexpr uint64_t VERTEX_SECTION_SIZE = ((static_cast<uint64_t>(TOTAL_BUFFER_SIZE * VERTEX_FRACTION) + 63) / 64) * 64;
static constexpr uint64_t INDEX_SECTION_SIZE = TOTAL_BUFFER_SIZE - VERTEX_SECTION_SIZE;

static constexpr uint64_t VERTEX_SECTION_OFFSET = 0;
static constexpr uint64_t INDEX_SECTION_OFFSET = VERTEX_SECTION_SIZE;

static constexpr uint32_t MAX_VERTEX_REGION = VERTEX_SECTION_SIZE / VERTEX_REGION_SIZE;
static constexpr uint32_t MAX_INDEX_REGION = INDEX_SECTION_SIZE / INDEX_REGION_SIZE;

static_assert(VERTEX_SECTION_SIZE + INDEX_SECTION_SIZE == TOTAL_BUFFER_SIZE,
              "Sections must sum to total buffer size");
static_assert(INDEX_SECTION_OFFSET % 4 == 0,
              "Index offset MUST be aligned to 4 bytes for VK_INDEX_TYPE_UINT32");
static_assert(INDEX_SECTION_OFFSET % 64 == 0,
              "Index offset should be aligned to 64 bytes for cache efficiency");
static_assert(VERTEX_SECTION_SIZE % 64 == 0,
              "Vertex section should be aligned to 64 bytes");
static_assert(INDEX_SECTION_SIZE % 64 == 0,
              "Index section should be aligned to 64 bytes");





// This class represent a buffer that can hold multiple voxel chunks in GPU memory in a limit of TOTAL_BUFFER_SIZE
class VoxelBuffer {
    VulkanBackend* m_backend;

    // Single 64 MB buffer with all three usage flags
    nvrhi::BufferHandle m_meshBuffer;

    // Small buffer for indirect draw commands and OUB data
    nvrhi::BufferHandle m_oubBuffer;
    nvrhi::BufferHandle m_indirectBuffer;
    // Management of those buffers
    std::vector<uint32_t> m_freeDrawSlots;
    uint32_t m_nextDrawSlot = 0;

    // Separate free lists for each section (start region, count)
    std::vector<std::pair<uint32_t, uint32_t>> m_freeVertexRegions;
    std::vector<std::pair<uint32_t, uint32_t>> m_freeIndexRegions;

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
     * @param oub
     */
    void write(nvrhi::CommandListHandle cmd, struct VoxelChunkMesh& mesh, const TerrainOUB &oub);

    /**
     * Free a previously allocated chunk
     * @param mesh The VoxelChunkMesh component with allocation data
     */
    void free(struct VoxelChunkMesh& mesh);

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

    nvrhi::BufferHandle get_mesh_buffer() const { return m_meshBuffer; }
    nvrhi::BufferHandle get_oub_buffer() const { return m_oubBuffer; }
    nvrhi::BufferHandle get_indirect_buffer() const { return m_indirectBuffer; }

    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_vertex_regions() const { return m_freeVertexRegions; }
    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_index_regions() const { return m_freeIndexRegions; }

    const std::vector <uint32_t>& get_free_draw_slots() const { return m_freeDrawSlots; }

    uint32_t get_used_vertex_regions() const;
    uint32_t get_used_index_regions() const;

    uint32_t get_largest_free_vertex_block() const;
    uint32_t get_largest_free_index_block() const;

    /**
     * Return the number of registered draw commands in this buffer
     * @return Number of draw commands
     */
    uint32_t get_draw_count() const { return m_nextDrawSlot - static_cast<uint32_t>(m_freeDrawSlots.size()); }
};