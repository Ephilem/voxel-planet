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
    m_facesBuffer = nullptr;
    m_globalIndexBuffer = nullptr;
}

// void VoxelBuffer::create_global_index_buffer() {
//     m_maxQuadsSupported = MAX_FACES_REGIONS * FACES_PER_REGION;
//
//     std::vector<uint32_t> indices;
//     indices.reserve(m_maxQuadsSupported * INDICES_PER_QUAD);
//
//     for (uint32_t quad = 0; quad < m_maxQuadsSupported; ++quad) {
//         uint32_t baseVertex = quad * VERTICES_PER_QUAD;
//         // Triangle 1: 0,1,2
//         indices.push_back(baseVertex + 0);
//         indices.push_back(baseVertex + 1);
//         indices.push_back(baseVertex + 2);
//         // Triangle 2: 0,2,3
//         indices.push_back(baseVertex + 0);
//         indices.push_back(baseVertex + 2);
//         indices.push_back(baseVertex + 3);
//     }
//
//     auto indexDesc = nvrhi::BufferDesc()
//             .setByteSize(indices.size() * sizeof(uint32_t))
//             .setDebugName("VoxelBuffer Global Index Buffer")
//             .setIsIndexBuffer(true)
//             .setInitialState(nvrhi::ResourceStates::CopyDest)
//             .setKeepInitialState(true);
//     m_globalIndexBuffer = m_backend->device->createBuffer(indexDesc);
//
//     auto cmd = m_backend->device->createCommandList();
//     cmd->open();
//     cmd->writeBuffer(m_globalIndexBuffer, indices.data(),
//                      sizeof(uint32_t) * indices.size(), 0);
//     cmd->setBufferState(m_globalIndexBuffer, nvrhi::ResourceStates::IndexBuffer);
//     cmd->close();
//     m_backend->device->executeCommandList(cmd);
//
//     LOG_INFO("VoxelBuffer", "Created global index buffer for {} quads ({:.2f} MB)",
//              m_maxQuadsSupported, (indices.size() * sizeof(uint32_t)) / (1024.0f * 1024.0f));
//
//     m_indexBufferWritten = true;
// }

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
            .setByteSize(8 * 1024 * 1024) // 8 MB
            .setDebugName("VoxelBuffer OUB Buffer")
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setIsConstantBuffer(false)
            .setStructStride(sizeof(TerrainOUB))
            .setKeepInitialState(true);
    m_oubBuffer = m_backend->device->createBuffer(oubDesc);

    // Indirect buffer
    auto indirectDesc = nvrhi::BufferDesc()
            .setByteSize(8 * 1024 * 1024 / sizeof(TerrainOUB) * sizeof(nvrhi::DrawIndexedIndirectArguments))
            .setDebugName("VoxelBuffer Indirect Buffer")
            .setInitialState(nvrhi::ResourceStates::IndirectArgument)
            .setIsDrawIndirectArgs(true)
            .setKeepInitialState(true);
    m_indirectBuffer = m_backend->device->createBuffer(indirectDesc);
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
    } else {
        drawSlot = m_nextDrawSlot++;
    }

    mesh.drawSlotIndex = drawSlot;

    return true;
}

void VoxelBuffer::write(nvrhi::CommandListHandle cmd, VoxelChunkMesh& mesh, const TerrainOUB& oub) {
    if (!mesh.is_allocated()) {
        LOG_ERROR("VoxelBuffer", "Cannot write unallocated mesh to buffer");
        return;
    }

    if (mesh.faceCount == 0) {
        LOG_WARN("VoxelBuffer", "Skipping write for mesh with 0 faces");
        return;
    }

    cmd->setBufferState(m_facesBuffer, nvrhi::ResourceStates::CopyDest);

    // Write faces
    uint64_t faceByteOffset = mesh.faceRegionStart * FACES_REGION_SIZE;
    cmd->writeBuffer(m_facesBuffer, mesh.faces.data(),
                     sizeof(TerrainFace3d) * mesh.faceCount, faceByteOffset);

    cmd->setBufferState(m_facesBuffer, nvrhi::ResourceStates::ShaderResource);

    // Write OUB
    uint64_t oubByteOffset = mesh.drawSlotIndex * sizeof(TerrainOUB);
    cmd->writeBuffer(m_oubBuffer, &oub, sizeof(TerrainOUB), oubByteOffset);

    // Write Indirect Args
    auto args = nvrhi::DrawIndirectArguments()
            // .setIndexCount(mesh.faceCount * INDICES_PER_QUAD)
            // .setStartIndexLocation(0)
            .setVertexCount(mesh.faceCount * VERTICES_PER_QUAD)
            .setStartVertexLocation(mesh.faceRegionStart * FACES_PER_REGION * VERTICES_PER_QUAD)
            .setInstanceCount(1)
            .setStartInstanceLocation(mesh.drawSlotIndex);  // Draw ID for gl_BaseInstance

    uint64_t indirectByteOffset = mesh.drawSlotIndex * sizeof(nvrhi::DrawIndirectArguments);
    cmd->writeBuffer(m_indirectBuffer, &args, sizeof(nvrhi::DrawIndirectArguments), indirectByteOffset);
}

void VoxelBuffer::cleanup_freed_draw_slots(nvrhi::CommandListHandle cmd) {
    for (uint32_t drawSlot : m_freedPendingDrawSlots) {
        auto args = nvrhi::DrawIndirectArguments()
                // .setBaseVertexLocation(0)
                // .setIndexCount(0)
                // .setStartIndexLocation(0)
                .setInstanceCount(0)
                .setStartInstanceLocation(0);
        uint64_t indirectByteOffset = drawSlot * sizeof(nvrhi::DrawIndirectArguments);
        cmd->writeBuffer(m_indirectBuffer, &args, sizeof(nvrhi::DrawIndirectArguments), indirectByteOffset);
    }
    m_freedPendingDrawSlots.clear();
}

void VoxelBuffer::free(VoxelChunkMesh& mesh) {
    if (!mesh.is_allocated()) {
        return;
    }

    free_regions(m_freeFaceRegions, mesh.faceRegionStart, mesh.faceRegionCount);

    m_freeDrawSlots.push_back(mesh.drawSlotIndex);
    m_freedPendingDrawSlots.push_back(mesh.drawSlotIndex);

    mesh.faceRegionStart = UINT32_MAX;
    mesh.faceRegionCount = 0;
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