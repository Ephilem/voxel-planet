//
// Created by raph on 25/11/25.
//

#include "VoxelBuffer.h"
#include "../rendering_components.h"
#include "core/TracyIntegration.h"
#include "core/log/Logger.h"
#include "renderer/TracyVulkanIntegration.h"

VoxelBuffer::VoxelBuffer(VulkanBackend* backend) {
    this->m_backend = backend;
    init();
}

VoxelBuffer::~VoxelBuffer() {
    m_facesBuffer = nullptr;
    m_globalIndexBuffer = nullptr;
}

void VoxelBuffer::init() {
    m_freeFaceRegions.clear();
    m_freeFaceRegions.emplace_back(0, MAX_FACES_REGIONS);

    m_freeDrawSlots.clear();
    m_nextDrawSlot = 0;

    // Face buffer
    auto facesDesc = nvrhi::BufferDesc()
            .setByteSize(FACES_BUFFER_SIZE)
            .setDebugName("VoxelBuffer Faces Buffer")
            .setIsConstantBuffer(false)
            .setStructStride(sizeof(TerrainFace3d))
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true);
    m_facesBuffer = m_backend->device->createBuffer(facesDesc);

    // create_global_index_buffer();

    // OUB buffer
    auto oubDesc = nvrhi::BufferDesc()
            .setByteSize(8 * 1024 * 1024)
            .setDebugName("VoxelBuffer OUB Buffer")
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setIsConstantBuffer(false)
            .setStructStride(sizeof(TerrainOUB))
            .setKeepInitialState(true);
    m_oubBuffer = m_backend->device->createBuffer(oubDesc);

    // Chunk cull data buffer
    uint32_t maxSlots = 8 * 1024 * 1024 / sizeof(TerrainOUB);
    auto cullDataDesc = nvrhi::BufferDesc()
            .setByteSize(sizeof(VoxelChunkCullData) * maxSlots)
            .setDebugName("VoxelBuffer Chunk Cull Data Buffer")
            .setStructStride(sizeof(VoxelChunkCullData))
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true);
    m_chunkCullDataBuffer = m_backend->device->createBuffer(cullDataDesc);

    // Culled indirect draw buffer
    auto indirectDesc = nvrhi::BufferDesc()
            .setByteSize(sizeof(nvrhi::DrawIndirectArguments) * maxSlots)
            .setDebugName("VoxelBuffer Culled Indirect Buffer")
            .setIsDrawIndirectArgs(true)
            .setCanHaveUAVs(true)
            .setStructStride(sizeof(nvrhi::DrawIndirectArguments))
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true);
    m_culledIndirectBuffer = m_backend->device->createBuffer(indirectDesc);

    // Culled draw count buffer
    auto countDesc = nvrhi::BufferDesc()
            .setByteSize(sizeof(uint32_t))
            .setDebugName("VoxelBuffer Culled Draw Count Buffer")
            .setIsDrawIndirectArgs(true)
            .setCanHaveRawViews(true)
            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
            .setKeepInitialState(true);
    m_culledDrawCountBuffer = m_backend->device->createBuffer(countDesc);
}

bool VoxelBuffer::can_allocate(uint32_t faceCount) {
    uint32_t faceRegionsNeeded = (faceCount + FACES_PER_REGION - 1) / FACES_PER_REGION;

    for (const auto& [start, count] : m_freeFaceRegions) {
        if (count >= faceRegionsNeeded) {
            return true;
        }
    }
    return false;
}

bool VoxelBuffer::allocate_regions(std::vector<std::pair<uint32_t, uint32_t>>& freeList,
                                   uint32_t regionCount, uint32_t& outStart) {
    for (auto it = freeList.begin(); it != freeList.end(); ++it) {
        if (it->second >= regionCount) {
            outStart = it->first;
            if (it->second == regionCount) {
                freeList.erase(it);
            } else {
                it->first += regionCount;
                it->second -= regionCount;
            }
            return true;
        }
    }
    return false;
}

void VoxelBuffer::free_regions(std::vector<std::pair<uint32_t, uint32_t>>& freeList,
                               uint32_t start, uint32_t count) {
    if (count == 0) return;

    uint32_t newStart = start;
    uint32_t newEnd = start + count;

    for (auto it = freeList.begin(); it != freeList.end();) {
        uint32_t blockStart = it->first;
        uint32_t blockEnd = blockStart + it->second;

        if (blockEnd == newStart) {
            newStart = blockStart;
            it = freeList.erase(it);
        } else if (newEnd == blockStart) {
            newEnd = blockEnd;
            it = freeList.erase(it);
        } else {
            ++it;
        }
    }

    freeList.emplace_back(newStart, newEnd - newStart);
}

bool VoxelBuffer::allocate(VoxelChunkMesh& mesh) {
    VOXEL_ZONE_N("VoxelBuffer-Allocate");
    uint32_t faceRegionsNeeded = (mesh.faceCount + FACES_PER_REGION - 1) / FACES_PER_REGION;

    uint32_t faceStart;
    if (!allocate_regions(m_freeFaceRegions, faceRegionsNeeded, faceStart)) {
        return false;
    }

    mesh.faceRegionStart = faceStart;
    mesh.faceRegionCount = faceRegionsNeeded;

    // Allocate draw slot
    uint32_t drawSlot;
    if (!m_freeDrawSlots.empty()) {
        drawSlot = m_freeDrawSlots.back();
        m_freeDrawSlots.pop_back();
        // Prevent cleanup_freed_draw_slots to overwrite with instanceCount=0 in the same frame.
        // auto it = std::find(m_freedPendingDrawSlots.begin(), m_freedPendingDrawSlots.end(), drawSlot);
        // if (it != m_freedPendingDrawSlots.end()) {
            // m_freedPendingDrawSlots.erase(it);
        // }
    } else {
        drawSlot = m_nextDrawSlot++;
    }

    mesh.drawSlotIndex = drawSlot;

    return true;
}

bool VoxelBuffer::reallocate(VoxelChunkMesh& mesh) {
    VOXEL_ZONE_N("VoxelBuffer-Reallocate");
    // Free the old face region regardless of new size
    if (mesh.faceRegionStart != UINT32_MAX) {
        free_regions(m_freeFaceRegions, mesh.faceRegionStart, mesh.faceRegionCount);
        mesh.faceRegionStart = UINT32_MAX;
        mesh.faceRegionCount = 0;
    }

    // Empty mesh: no face region needed. write() will zero the indirect args.
    if (mesh.faceCount == 0) {
        return true;
    }

    uint32_t faceRegionsNeeded = (mesh.faceCount + FACES_PER_REGION - 1) / FACES_PER_REGION;
    uint32_t faceStart;
    if (!allocate_regions(m_freeFaceRegions, faceRegionsNeeded, faceStart)) {
        return false;
    }

    mesh.faceRegionStart = faceStart;
    mesh.faceRegionCount = faceRegionsNeeded;

    return true;
}

void VoxelBuffer::free(VoxelChunkMesh& mesh) {
    VOXEL_ZONE_N("VoxelBuffer-Free");
    if (!mesh.is_allocated()) {
        return;
    }

    free_regions(m_freeFaceRegions, mesh.faceRegionStart, mesh.faceRegionCount);

    m_freeDrawSlots.push_back(mesh.drawSlotIndex);
    // m_freedPendingDrawSlots.insert(mesh.drawSlotIndex);

    mesh.faceRegionStart = UINT32_MAX;
    mesh.faceRegionCount = 0;
    mesh.drawSlotIndex = UINT32_MAX;
}

uint32_t VoxelBuffer::get_used_face_regions() const {
    uint32_t totalFree = 0;
    for (const auto& [start, count] : m_freeFaceRegions) {
        totalFree += count;
    }
    return MAX_FACES_REGIONS - totalFree;
}

uint32_t VoxelBuffer::get_largest_free_face_block() const {
    uint32_t largest = 0;
    for (const auto& [start, count] : m_freeFaceRegions) {
        if (count > largest) {
            largest = count;
        }
    }
    return largest;
}