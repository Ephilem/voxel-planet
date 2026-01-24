//
// Created by raph on 25/11/25.
//

#include "VoxelBuffer.h"
#include "../rendering_components.h"
#include "core/log/Logger.h"

VoxelBuffer::VoxelBuffer(VulkanBackend* backend) {
    this->m_backend = backend;
    init();
}

VoxelBuffer::~VoxelBuffer() {
    m_meshBuffer = nullptr;
}


void VoxelBuffer::init() {
    m_freeVertexRegions.clear();
    m_freeVertexRegions.emplace_back(0, MAX_VERTEX_REGION);

    m_freeIndexRegions.clear();
    m_freeIndexRegions.emplace_back(0, MAX_INDEX_REGION);

    m_freeDrawSlots.clear();
    m_nextDrawSlot = 0;

    auto meshDesc = nvrhi::BufferDesc()
            .setByteSize(TOTAL_BUFFER_SIZE)
            .setDebugName("VoxelBuffer Mesh Buffer")
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
            .setInitialState(nvrhi::ResourceStates::CopyDest)
            .setKeepInitialState(true);
    m_meshBuffer = m_backend->device->createBuffer(meshDesc);

    auto oubDesc = nvrhi::BufferDesc()
            .setByteSize(8 * 1024 * 1024) // 8 MB
            .setDebugName("VoxelBuffer OUB Buffer")
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setIsConstantBuffer(false) // structured buffer
            .setStructStride(sizeof(TerrainOUB))
            .setKeepInitialState(true);
    m_oubBuffer = m_backend->device->createBuffer(oubDesc);

    auto indirectDesc = nvrhi::BufferDesc()
            .setByteSize(8 * 1024 * 1024 / sizeof(TerrainOUB) * sizeof(nvrhi::DrawIndexedIndirectArguments)) // 2.5 MB
            .setDebugName("VoxelBuffer Indirect Buffer")
            .setInitialState(nvrhi::ResourceStates::IndirectArgument)
            .setIsDrawIndirectArgs(true)
            .setKeepInitialState(true);
    m_indirectBuffer = m_backend->device->createBuffer(indirectDesc);
}

bool VoxelBuffer::can_allocate(uint32_t vertexCount, uint32_t indexCount) {
    uint32_t vertexRegionsNeeded = (vertexCount + VERTICES_PER_REGION - 1) / VERTICES_PER_REGION;
    uint32_t indexRegionsNeeded = (indexCount + (INDEX_REGION_SIZE / INDEX_SIZE) - 1) / (
                                      INDEX_REGION_SIZE / INDEX_SIZE);

    bool hasVertexSpace = false;
    bool hasIndexSpace = false;
    // bool hasIndirectSpace = false;

    for (const auto &[start, count]: m_freeVertexRegions) {
        if (count >= vertexRegionsNeeded) {
            hasVertexSpace = true;
            break;
        }
    }

    for (const auto &[start, count]: m_freeIndexRegions) {
        if (count >= indexRegionsNeeded) {
            hasIndexSpace = true;
            break;
        }
    }

    if (!hasVertexSpace) {
        LOG_DEBUG("VoxelBuffer", "Cannot allocate {} vertex regions (largest free block = {})",
                  vertexRegionsNeeded, get_largest_free_vertex_block());
    }
    if (!hasIndexSpace) {
        LOG_DEBUG("VoxelBuffer", "Cannot allocate {} index regions (largest free block = {})",
                  indexRegionsNeeded, get_largest_free_index_block());
    }

    return hasVertexSpace && hasIndexSpace;
}

bool VoxelBuffer::allocate_regions(std::vector<std::pair<uint32_t, uint32_t> > &freeList,
                                   uint32_t regionCount, uint32_t &outStart) {
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

void VoxelBuffer::free_regions(std::vector<std::pair<uint32_t, uint32_t> > &freeList,
                               uint32_t start, uint32_t count) {
    if (count == 0) return;

    uint32_t newStart = start;
    uint32_t newEnd = start + count;

    // Search for adjacent blocks to merge
    for (auto it = freeList.begin(); it != freeList.end();) {
        uint32_t blockStart = it->first;
        uint32_t blockEnd = blockStart + it->second;

        // If adjacent, merge
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

bool VoxelBuffer::allocate(VoxelChunkMesh &mesh) {
    /////// Allocate Mesh Regions ///////
    uint32_t vertexRegionsNeeded = (mesh.vertexCount + VERTICES_PER_REGION - 1) / VERTICES_PER_REGION;
    uint32_t indexRegionsNeeded = (mesh.indexCount + (INDEX_REGION_SIZE / INDEX_SIZE) - 1) / (
                                      INDEX_REGION_SIZE / INDEX_SIZE);

    uint32_t vertexStart, indexStart, indirectStart;

    if (!allocate_regions(m_freeVertexRegions, vertexRegionsNeeded, vertexStart)) {
        return false;
    }

    if (!allocate_regions(m_freeIndexRegions, indexRegionsNeeded, indexStart)) {
        // Rollback vertex allocation
        free_regions(m_freeVertexRegions, vertexStart, vertexRegionsNeeded);
        return false;
    }

    mesh.vertexRegionStart = vertexStart;
    mesh.vertexRegionCount = vertexRegionsNeeded;
    mesh.indexRegionStart = indexStart;
    mesh.indexRegionCount = indexRegionsNeeded;


    /////// Allocate Indirect Draw Slot ///////
    uint32_t drawSlot;
    // If there is a slot available, use it
    if (!m_freeDrawSlots.empty()) {
        drawSlot = m_freeDrawSlots.back();
        m_freeDrawSlots.pop_back();
    } else {
        drawSlot = m_nextDrawSlot++;
    }

    mesh.drawSlotIndex = drawSlot;

    return true;
}

void VoxelBuffer::write(nvrhi::CommandListHandle cmd, VoxelChunkMesh &mesh, const TerrainOUB &oub) {
    if (!mesh.is_allocated()) {
        LOG_ERROR("VoxelBuffer", "Cannot write unallocated mesh to buffer");
        return;
    }

    cmd->setBufferState(m_meshBuffer, nvrhi::ResourceStates::CopyDest);

    // Write vertices
    uint64_t vertexByteOffset = mesh.vertexRegionStart * VERTEX_REGION_SIZE;
    cmd->writeBuffer(m_meshBuffer, mesh.vertices.data(),
                     sizeof(TerrainVertex3d) * mesh.vertexCount, vertexByteOffset);

    // Write indices
    uint64_t indexByteOffset = mesh.indexRegionStart * INDEX_REGION_SIZE + INDEX_SECTION_OFFSET;
    cmd->writeBuffer(m_meshBuffer, mesh.indices.data(),
                     sizeof(uint32_t) * mesh.indexCount, indexByteOffset);

    cmd->setBufferState(m_meshBuffer, nvrhi::ResourceStates::VertexBuffer | nvrhi::ResourceStates::IndexBuffer);

    // Write OUB
    uint64_t oubByteOffset = mesh.drawSlotIndex * sizeof(TerrainOUB);
    cmd->writeBuffer(m_oubBuffer, &oub, sizeof(TerrainOUB), oubByteOffset);

    // Write Indirect Args
    auto args = nvrhi::DrawIndexedIndirectArguments()
            .setBaseVertexLocation(mesh.vertexRegionStart * VERTICES_PER_REGION)
            .setIndexCount(mesh.indexCount)
            .setStartIndexLocation(mesh.indexRegionStart * (INDEX_REGION_SIZE / INDEX_SIZE))
            .setInstanceCount(1)
            .setStartInstanceLocation(mesh.drawSlotIndex); // Use firstInstance as draw ID for gl_BaseInstance
    uint64_t indirectByteOffset = mesh.drawSlotIndex * sizeof(nvrhi::DrawIndexedIndirectArguments);
    cmd->writeBuffer(m_indirectBuffer, &args, sizeof(nvrhi::DrawIndexedIndirectArguments), indirectByteOffset);

    // float avgVertexToIndexRatio = ((float)mesh.vertexCount / mesh.indexCount);
    // LOG_INFO("VoxelBuffer", "Vertex/Index ratio: {:.2f}", avgVertexToIndexRatio);
}

void VoxelBuffer::cleanup_freed_draw_slots(nvrhi::CommandListHandle cmd) {
    for (uint32_t drawSlot: m_freedPendingDrawSlots) {
        auto args = nvrhi::DrawIndexedIndirectArguments()
                .setBaseVertexLocation(0)
                .setIndexCount(0)
                .setStartIndexLocation(0)
                .setInstanceCount(0)
                .setStartInstanceLocation(0);
        uint64_t indirectByteOffset = drawSlot * sizeof(nvrhi::DrawIndexedIndirectArguments);
        cmd->writeBuffer(m_indirectBuffer, &args, sizeof(nvrhi::DrawIndexedIndirectArguments), indirectByteOffset);
    }
    m_freedPendingDrawSlots.clear();
}

void VoxelBuffer::free(VoxelChunkMesh &mesh) {
    if (!mesh.is_allocated()) {
        return;
    }

    free_regions(m_freeVertexRegions, mesh.vertexRegionStart, mesh.vertexRegionCount);
    free_regions(m_freeIndexRegions, mesh.indexRegionStart, mesh.indexRegionCount);

    m_freeDrawSlots.push_back(mesh.drawSlotIndex);
    m_freedPendingDrawSlots.push_back(mesh.drawSlotIndex); // Mark for cleanup after GPU is done

    mesh.vertexRegionStart = UINT32_MAX;
    mesh.vertexRegionCount = 0;
    mesh.indexRegionStart = UINT32_MAX;
    mesh.indexRegionCount = 0;
}

uint32_t VoxelBuffer::get_used_vertex_regions() const {
    uint32_t totalFree = 0;
    for (const auto &[start, count]: m_freeVertexRegions) {
        totalFree += count;
    }
    return MAX_VERTEX_REGION - totalFree;
}

uint32_t VoxelBuffer::get_used_index_regions() const {
    uint32_t totalFree = 0;
    for (const auto &[start, count]: m_freeIndexRegions) {
        totalFree += count;
    }
    return MAX_INDEX_REGION - totalFree;
}

uint32_t VoxelBuffer::get_largest_free_vertex_block() const {
    uint32_t largest = 0;
    for (const auto &[start, count]: m_freeVertexRegions) {
        if (count > largest) {
            largest = count;
        }
    }
    return largest;
}

uint32_t VoxelBuffer::get_largest_free_index_block() const {
    uint32_t largest = 0;
    for (const auto &[start, count]: m_freeIndexRegions) {
        if (count > largest) {
            largest = count;
        }
    }
    return largest;
}
