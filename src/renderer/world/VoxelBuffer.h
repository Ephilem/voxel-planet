#pragma once

#include "nvrhi/nvrhi.h"
#include "renderer/render_types.h"
#include "renderer/vulkan/VulkanBackend.h"

struct alignas(16) TerrainOUB {
    glm::mat4 model;
};

static constexpr uint64_t TOTAL_BUFFER_SIZE = 64 * 1024 * 1024; // 64 MB
static constexpr uint64_t FACES_BUFFER_SIZE = TOTAL_BUFFER_SIZE;

static constexpr uint32_t FACES_PER_REGION = 1000;
static constexpr uint64_t FACES_REGION_SIZE = sizeof(TerrainFace3d) * FACES_PER_REGION;
static constexpr uint32_t MAX_FACES_REGIONS = FACES_BUFFER_SIZE / FACES_REGION_SIZE;

// Index buffer pattern for quads: {0,1,2, 2,3,0} repeated
// static constexpr uint32_t INDICES_PER_QUAD = 6;
static constexpr uint32_t VERTICES_PER_QUAD = 6;

class VoxelBuffer {
    VulkanBackend* m_backend;

    nvrhi::BufferHandle m_facesBuffer;

    bool m_indexBufferWritten = false;
    nvrhi::BufferHandle m_globalIndexBuffer;
    uint32_t m_maxQuadsSupported;

    // Buffers pour indirect draw et OUB
    nvrhi::BufferHandle m_oubBuffer;
    nvrhi::BufferHandle m_indirectBuffer;

    // Management
    std::vector<uint32_t> m_freeDrawSlots;
    uint32_t m_nextDrawSlot = 0;
    std::vector<std::pair<uint32_t, uint32_t>> m_freeFaceRegions;
    std::vector<uint32_t> m_freedPendingDrawSlots;

    void init();

    /**
     * Precompute all the indices for the global index buffer.
     */
    // void create_global_index_buffer();

    bool allocate_regions(std::vector<std::pair<uint32_t, uint32_t>>& freeList,
                        uint32_t regionCount, uint32_t& outStart);
    void free_regions(std::vector<std::pair<uint32_t, uint32_t>>& freeList,
                    uint32_t start, uint32_t count);

public:
    VoxelBuffer(VulkanBackend* backend);
    ~VoxelBuffer();

    bool can_allocate(uint32_t faceCount);
    bool allocate(struct VoxelChunkMesh& mesh);
    void write(nvrhi::CommandListHandle cmd, struct VoxelChunkMesh& mesh, const TerrainOUB& oub);
    void cleanup_freed_draw_slots(nvrhi::CommandListHandle cmd);
    void free(struct VoxelChunkMesh& mesh);

    nvrhi::BufferHandle get_faces_buffer() const { return m_facesBuffer; }
    nvrhi::BufferHandle get_index_buffer() const { return m_globalIndexBuffer; }
    nvrhi::BufferHandle get_oub_buffer() const { return m_oubBuffer; }
    nvrhi::BufferHandle get_indirect_buffer() const { return m_indirectBuffer; }

    const std::vector<std::pair<uint32_t, uint32_t>>& get_free_face_regions() const { return m_freeFaceRegions; }
    const std::vector<uint32_t>& get_free_draw_slots() const { return m_freeDrawSlots; }

    uint32_t get_used_face_regions() const;
    uint32_t get_largest_free_face_block() const;
    uint32_t get_draw_count() const { return m_nextDrawSlot - static_cast<uint32_t>(m_freeDrawSlots.size()); }
};