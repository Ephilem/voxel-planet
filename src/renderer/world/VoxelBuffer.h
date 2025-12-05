#pragma once

#include "glm/vec3.hpp"
#include "nvrhi/nvrhi.h"
#include "renderer/render_types.h"
#include "renderer/vulkan/VulkanBackend.h"


static constexpr uint64_t TOTAL_BUFFER_SIZE = 64 * 1024 * 1024; // 64 MB
static constexpr uint32_t VERTEX_REGION_SIZE = 4 * 1024; // 4 KB

static constexpr uint32_t VERTEX_SIZE = sizeof(Vertex3d);
static constexpr uint32_t INDEX_SIZE = sizeof(uint32_t);
static constexpr uint32_t INDIRECT_CMD_SIZE = sizeof(VkDrawIndexedIndirectCommand);

// We will assure a ration of 1.5 indices per vertex.
static constexpr float INDEX_TO_VERTEX_RATIO = 1.5f;

static constexpr uint32_t VERTICES_PER_REGION = VERTEX_REGION_SIZE / VERTEX_SIZE; // 4 KB / 12 B = 341 vertices

static constexpr uint32_t INDICES_PER_VERTEX_REGION = static_cast<uint32_t>(VERTICES_PER_REGION * INDEX_TO_VERTEX_RATIO); // 341 * 1.5 = 511 indices
static constexpr uint32_t INDEX_REGION_SIZE = ((INDICES_PER_VERTEX_REGION * INDEX_SIZE + 1023) / 1024) * 1024;
static constexpr uint32_t COMMAND_PER_ALLOCATION = 1; // assuming 1 draw command per chunk (per allocation so)
static constexpr uint32_t INDIRECT_REGION_SIZE = ((COMMAND_PER_ALLOCATION * INDIRECT_CMD_SIZE + 63) / 64) * 64; // align to 64 bytes

static constexpr uint32_t MAX_VERTEX_REGION = (TOTAL_BUFFER_SIZE * 60 / 100) / VERTEX_REGION_SIZE; // 60% of the total buffer for vertexs
static constexpr uint64_t VERTEX_SECTION_SIZE = MAX_VERTEX_REGION * VERTEX_REGION_SIZE;
static constexpr uint32_t MAX_INDEX_REGION = MAX_VERTEX_REGION * VERTEX_REGION_SIZE / INDEX_REGION_SIZE;
static constexpr uint64_t INDEX_SECTION_SIZE = MAX_INDEX_REGION * INDEX_REGION_SIZE;
static constexpr uint64_t INDIRECT_SECTION_SIZE = TOTAL_BUFFER_SIZE - VERTEX_SECTION_SIZE - INDEX_SECTION_SIZE;
static constexpr uint64_t MAX_INDIRECT_REGION = INDIRECT_SECTION_SIZE / INDIRECT_REGION_SIZE;

// Section offsets in the single 64MB buffer
static constexpr uint64_t VERTEX_SECTION_OFFSET = 0;
static constexpr uint64_t INDEX_SECTION_OFFSET = VERTEX_SECTION_SIZE;
static constexpr uint64_t INDIRECT_SECTION_OFFSET = INDEX_SECTION_OFFSET + INDEX_SECTION_SIZE;




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
     * @param vertexCount Number of vertices
     * @param indexCount Number of indices
     * @param mesh Output: VoxelChunkMesh component to fill with allocation data
     * @return True if allocation succeeded, false otherwise
     */
    bool allocate(uint32_t vertexCount, uint32_t indexCount, struct VoxelChunkMesh& mesh);

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

    // Debug accessors for visualization
    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_vertex_regions() const { return m_freeVertexRegions; }
    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_index_regions() const { return m_freeIndexRegions; }
    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_indirect_regions() const { return m_freeIndirectRegions; }

    /**
     * Calculate total used regions for each section
     */
    uint32_t get_used_vertex_regions() const;
    uint32_t get_used_index_regions() const;
    uint32_t get_used_indirect_regions() const;

    /**
     * Get the largest contiguous free block for each section
     */
    uint32_t get_largest_free_vertex_block() const;
    uint32_t get_largest_free_index_block() const;
    uint32_t get_largest_free_indirect_block() const;
};