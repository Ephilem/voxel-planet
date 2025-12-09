//
// Created by raph on 25/11/25.
//

#include "VoxelBuffer.h"
#include "../rendering_components.h"

VoxelBuffer::VoxelBuffer(VulkanBackend *backend) {
    this->m_backend = backend;
    init();
}

VoxelBuffer::~VoxelBuffer() {
    m_buffer = nullptr;
}


void VoxelBuffer::init() {
    m_freeVertexRegions.clear();
    m_freeVertexRegions.emplace_back(0, MAX_VERTEX_REGION);

    m_freeIndexRegions.clear();
    m_freeIndexRegions.emplace_back(0, MAX_INDEX_REGION);

    m_freeIndirectRegions.clear();
    m_freeIndirectRegions.emplace_back(0, MAX_INDIRECT_REGION);

    nvrhi::BufferDesc bufferDesc = {};
    bufferDesc.byteSize = TOTAL_BUFFER_SIZE;
    bufferDesc.debugName = "VoxelBuffer";
    bufferDesc.isVertexBuffer = true;
    bufferDesc.isIndexBuffer = true;
    bufferDesc.isDrawIndirectArgs = true;
    bufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
    bufferDesc.keepInitialState = true;
    m_buffer = m_backend->device->createBuffer(bufferDesc);
}

bool VoxelBuffer::can_allocate(uint32_t vertexCount, uint32_t indexCount) {
    uint32_t vertexRegionsNeeded = (vertexCount + VERTICES_PER_REGION - 1) / VERTICES_PER_REGION;
    uint32_t indexRegionsNeeded = (indexCount + (INDEX_REGION_SIZE / INDEX_SIZE) - 1) / (INDEX_REGION_SIZE / INDEX_SIZE);

    bool hasVertexSpace = false;
    bool hasIndexSpace = false;
    bool hasIndirectSpace = false;

    for (const auto& [start, count] : m_freeVertexRegions) {
        if (count >= vertexRegionsNeeded) {
            hasVertexSpace = true;
            break;
        }
    }

    for (const auto& [start, count] : m_freeIndexRegions) {
        if (count >= indexRegionsNeeded) {
            hasIndexSpace = true;
            break;
        }
    }

    for (const auto& [start, count] : m_freeIndirectRegions) {
        if (count >= 1) {
            hasIndirectSpace = true;
            break;
        }
    }

    return hasVertexSpace && hasIndexSpace && hasIndirectSpace;
}

bool VoxelBuffer::allocate_regions(std::vector<std::pair<uint32_t, uint32_t>>& freeList,
                                  uint32_t regionCount, uint32_t& outStart) {
    // Find first fit
    for (auto it = freeList.begin(); it != freeList.end(); ++it) {
        if (it->second >= regionCount) {
            outStart = it->first;

            // Reduce or remove this free block
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
    freeList.emplace_back(start, count);
}

bool VoxelBuffer::allocate(VoxelChunkMesh& mesh) {
    uint32_t vertexRegionsNeeded = (mesh.vertexCount + VERTICES_PER_REGION - 1) / VERTICES_PER_REGION;
    uint32_t indexRegionsNeeded = (mesh.indexCount + (INDEX_REGION_SIZE / INDEX_SIZE) - 1) / (INDEX_REGION_SIZE / INDEX_SIZE);

    uint32_t vertexStart, indexStart, indirectStart;

    if (!allocate_regions(m_freeVertexRegions, vertexRegionsNeeded, vertexStart)) {
        return false;
    }

    if (!allocate_regions(m_freeIndexRegions, indexRegionsNeeded, indexStart)) {
        // Rollback vertex allocation
        free_regions(m_freeVertexRegions, vertexStart, vertexRegionsNeeded);
        return false;
    }

    if (!allocate_regions(m_freeIndirectRegions, 1, indirectStart)) {
        // Rollback previous allocations
        free_regions(m_freeVertexRegions, vertexStart, vertexRegionsNeeded);
        free_regions(m_freeIndexRegions, indexStart, indexRegionsNeeded);
        return false;
    }

    mesh.vertexRegionStart = vertexStart;
    mesh.vertexRegionCount = vertexRegionsNeeded;
    mesh.indexRegionStart = indexStart;
    mesh.indexRegionCount = indexRegionsNeeded;
    mesh.indirectRegionIndex = indirectStart;

    return true;
}

void VoxelBuffer::write(nvrhi::CommandListHandle cmd, VoxelChunkMesh &mesh) {
    cmd->setBufferState(m_buffer, nvrhi::ResourceStates::CopyDest);

    // Write vertices
    uint64_t vertexByteOffset = mesh.vertexRegionStart * VERTEX_REGION_SIZE;
    cmd->writeBuffer(m_buffer, mesh.vertices.data(),
                     sizeof(Vertex3d) * mesh.vertexCount, vertexByteOffset);

    // Write indices
    uint64_t indexByteOffset = mesh.indexRegionStart * INDEX_REGION_SIZE + INDEX_SECTION_OFFSET;
    cmd->writeBuffer(m_buffer, mesh.indices.data(),
                     sizeof(uint32_t) * mesh.indexCount, indexByteOffset);

    // Write indirect draw command
    // Note: Commands must be packed at sizeof(DrawIndexedIndirectArguments) stride for multi-draw
    uint64_t indirectOffset = mesh.indirectRegionIndex * sizeof(nvrhi::DrawIndexedIndirectArguments) + INDIRECT_SECTION_OFFSET;

    // Calculate baseVertexLocation from actual byte offset to handle region padding correctly
    uint32_t baseVertexLocation = (mesh.vertexRegionStart * VERTEX_REGION_SIZE) / sizeof(Vertex3d);
    uint32_t startIndexLocation = mesh.indexRegionStart * (INDEX_REGION_SIZE / sizeof(uint32_t));

    nvrhi::DrawIndexedIndirectArguments indirectArgs;
    indirectArgs.setIndexCount(mesh.indexCount)
                .setInstanceCount(1)
                .setStartIndexLocation(startIndexLocation)
                .setBaseVertexLocation(baseVertexLocation)
                .setStartInstanceLocation(0);

    cmd->writeBuffer(m_buffer, &indirectArgs, sizeof(indirectArgs), indirectOffset);

    cmd->setBufferState(m_buffer,
                        nvrhi::ResourceStates::VertexBuffer |
                        nvrhi::ResourceStates::IndexBuffer |
                        nvrhi::ResourceStates::IndirectArgument);
}


void VoxelBuffer::free(VoxelChunkMesh& mesh) {
    if (!mesh.is_allocated()) {
        return;
    }

    free_regions(m_freeVertexRegions, mesh.vertexRegionStart, mesh.vertexRegionCount);
    free_regions(m_freeIndexRegions, mesh.indexRegionStart, mesh.indexRegionCount);
    free_regions(m_freeIndirectRegions, mesh.indirectRegionIndex, 1);

    mesh.vertexRegionStart = UINT32_MAX;
    mesh.vertexRegionCount = 0;
    mesh.indexRegionStart = UINT32_MAX;
    mesh.indexRegionCount = 0;
    mesh.indirectRegionIndex = UINT32_MAX;
}

uint32_t VoxelBuffer::get_used_vertex_regions() const {
    uint32_t totalFree = 0;
    for (const auto& [start, count] : m_freeVertexRegions) {
        totalFree += count;
    }
    return MAX_VERTEX_REGION - totalFree;
}

uint32_t VoxelBuffer::get_used_index_regions() const {
    uint32_t totalFree = 0;
    for (const auto& [start, count] : m_freeIndexRegions) {
        totalFree += count;
    }
    return MAX_INDEX_REGION - totalFree;
}

uint32_t VoxelBuffer::get_used_indirect_regions() const {
    uint32_t totalFree = 0;
    for (const auto& [start, count] : m_freeIndirectRegions) {
        totalFree += count;
    }
    return static_cast<uint32_t>(MAX_INDIRECT_REGION - totalFree);
}

uint32_t VoxelBuffer::get_largest_free_vertex_block() const {
    uint32_t largest = 0;
    for (const auto& [start, count] : m_freeVertexRegions) {
        if (count > largest) {
            largest = count;
        }
    }
    return largest;
}

uint32_t VoxelBuffer::get_largest_free_index_block() const {
    uint32_t largest = 0;
    for (const auto& [start, count] : m_freeIndexRegions) {
        if (count > largest) {
            largest = count;
        }
    }
    return largest;
}

uint32_t VoxelBuffer::get_largest_free_indirect_block() const {
    uint32_t largest = 0;
    for (const auto& [start, count] : m_freeIndirectRegions) {
        if (count > largest) {
            largest = count;
        }
    }
    return largest;
}
