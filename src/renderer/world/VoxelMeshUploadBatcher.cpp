//
// Created by raph on 18/03/2026.
//

#include "VoxelMeshUploadBatcher.h"

#include "core/TracyIntegration.h"
#include "core/log/Logger.h"
#include "core/world/world_components.h"
#include "planet/PlanetSurfaceTerrainRenderer.h"
#include "renderer/TracyVulkanIntegration.h"

void VoxelMeshUploadBatcher::init(VulkanBackend* backend) {
    m_backend = backend;

    // create staging buffers
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = MAX_STAGING_BUFFER_SIZE;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocationCreateInfo = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
    };

    for (int i = 0; i < STAGING_NUMBER; i++) {
        vmaCreateBuffer(backend->get_vma_allocator(), &bufferInfo, &allocationCreateInfo,
                        &m_stagingBuffers[i].buffer, &m_stagingBuffers[i].allocation, &m_stagingBuffers[i].info);
    }
}

void VoxelMeshUploadBatcher::destroy() {
    for (int i = 0; i < STAGING_NUMBER; i++) {
        vmaDestroyBuffer(m_backend->get_vma_allocator(), m_stagingBuffers[i].buffer, m_stagingBuffers[i].allocation);
    }
}

bool VoxelMeshUploadBatcher::enqueue(const VoxelChunkMesh &meshData, const TerrainOUB &oub, VoxelBuffer* targetBuffer) {
    VOXEL_ZONE_N("VoxelMeshUploadBatcher-Enqueue");
    // Size test
    size_t totalSizeNeeded = meshData.faces.size() * sizeof(TerrainFace3d) + sizeof(TerrainOUB) + sizeof(VoxelChunkCullData);
    if (m_currentTotalSize + totalSizeNeeded > MAX_STAGING_BUFFER_SIZE) {
        VOXEL_MESSAGE("Staging buffer full");
        LOG_ERROR("VoxelMeshUploadBatcher", "Mesh data is too large to fit in the staging buffer ({} bytes needed, max is {})",
                  totalSizeNeeded, MAX_STAGING_BUFFER_SIZE);
        return false;
    }

    m_uploadTasks.emplace_back(UploadTask{
        .faceData = meshData.faces.data(),
        .faceDataSize = meshData.faces.size() * sizeof(TerrainFace3d),
        .faceDataOffset = meshData.faceRegionStart * FACES_REGION_SIZE,

        .oub = oub,
        .drawSlotIndex = meshData.drawSlotIndex,

        .targetBuffer = targetBuffer
    });
    m_currentTotalSize += totalSizeNeeded;

    // LOG_TRACE("VoxelMeshUploadBatcher", "Prepare to write in drawSlotIndex = {}", meshData.drawSlotIndex);

    return true;
}

bool VoxelMeshUploadBatcher::enqueue_free(uint32_t drawSlotIndex, VoxelBuffer* targetBuffer) {
    size_t totalSizeNeeded = sizeof(TerrainOUB) + sizeof(VoxelChunkCullData);
    if (m_currentTotalSize + totalSizeNeeded > MAX_STAGING_BUFFER_SIZE) {
        VOXEL_MESSAGE("Staging buffer full");
        LOG_ERROR("VoxelMeshUploadBatcher", "Free command is too large to fit in the staging buffer ({} bytes needed, max is {})",
                  totalSizeNeeded, MAX_STAGING_BUFFER_SIZE);
        return false;
    }

    m_uploadTasks.emplace_back(UploadTask{
        .faceData = nullptr,
        .faceDataSize = 0,
        .faceDataOffset = 0,

        .oub = {},
        .drawSlotIndex = drawSlotIndex,

        .targetBuffer = targetBuffer
    });
    m_currentTotalSize += totalSizeNeeded;

    return true;
}

void VoxelMeshUploadBatcher::flush(VkCommandBuffer cmd) {
    VOXEL_ZONE_N("Flush VoxelMeshUploadBatcher");
    if (m_uploadTasks.empty()) return;
    int currentSlot = m_backend->get_frame_in_flight_index();

    StagingBuffer &staging = m_stagingBuffers[currentSlot];
    uint8_t* mapped = static_cast<uint8_t *>(staging.info.pMappedData);
    VkDeviceSize stagingOffset = 0;

    // Collect VkBufferCopy regions for each destination buffer type.
    // Grouped by VkBuffer dst since there could be multiple VoxelBuffers.
    struct PerDst {
        VkBuffer dst;
        std::vector<VkBufferCopy> regions;
    };
    std::vector<PerDst> facesCopies, oubCopies, cullDataCopies;

    auto get_regions = [](std::vector<PerDst> &list, VkBuffer dst) -> std::vector<VkBufferCopy> & {
        for (auto &e: list) if (e.dst == dst) return e.regions;
        list.push_back({dst, {}});
        return list.back().regions;
    };

    // Writing data to staging buffer and preparing copy regions for each upload task
    for (const auto &task: m_uploadTasks) {
        VOXEL_ZONE_N("Prepare task for upload");
        VkBuffer vkFaces = (VkBuffer) task.targetBuffer->get_faces_buffer()->getNativeObject(
            nvrhi::ObjectTypes::VK_Buffer);
        VkBuffer vkOub = (VkBuffer) task.targetBuffer->get_oub_buffer()->getNativeObject(nvrhi::ObjectTypes::VK_Buffer);
        VkBuffer vkCullData = (VkBuffer) task.targetBuffer->get_chunk_cull_data_buffer()->getNativeObject(
            nvrhi::ObjectTypes::VK_Buffer);

        // --- Faces ---
        if (task.faceDataSize > 0) {
            memcpy(mapped + stagingOffset, task.faceData, task.faceDataSize);
            get_regions(facesCopies, vkFaces).push_back({
                .srcOffset = stagingOffset,
                .dstOffset = task.faceDataOffset,
                .size = task.faceDataSize,
            });
            stagingOffset += task.faceDataSize;
        }

        // --- OUB ---
        memcpy(mapped + stagingOffset, &task.oub, sizeof(vp::SurfaceChunkOUB));
        get_regions(oubCopies, vkOub).push_back({
            .srcOffset = stagingOffset,
            .dstOffset = task.drawSlotIndex * sizeof(vp::SurfaceChunkOUB),
            .size = sizeof(vp::SurfaceChunkOUB),
        });
        stagingOffset += sizeof(vp::SurfaceChunkOUB);

        // --- Chunk cull data and indirectCmd ---
        // Cull information
        VoxelChunkCullData cullData = {};

        glm::vec3 corners[8] = {
            {0, 0, 0}, {CHUNK_SIZE, 0, 0}, {0, CHUNK_SIZE, 0},
            {CHUNK_SIZE, CHUNK_SIZE, 0}, {0, 0, CHUNK_SIZE},{CHUNK_SIZE, 0, CHUNK_SIZE},
            {0, CHUNK_SIZE, CHUNK_SIZE},{CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE},
        };
        glm::vec3 wMin(FLT_MAX), wMax(-FLT_MAX);
        for (auto& c : corners) {
            glm::vec3 w = glm::vec3(task.oub.model * glm::vec4(c, 1.0f));
            wMin = glm::min(wMin, w);
            wMax = glm::max(wMax, w);
        }
        cullData.aabbMin = glm::vec4(wMin, 0.0f);
        cullData.aabbMax = glm::vec4(wMax, 0.0f);

        uint32_t faceCount = task.faceDataSize / sizeof(TerrainFace3d);
        uint32_t faceRegionStart = task.faceDataOffset / FACES_REGION_SIZE;
        cullData.drawArgs = {
            .vertexCount = faceCount * VERTICES_PER_QUAD,
            .instanceCount = faceCount > 0 ? 1u : 0u,
            .firstVertex = faceRegionStart * FACES_PER_REGION * VERTICES_PER_QUAD,
            .firstInstance = task.drawSlotIndex,
        };
        memcpy(mapped + stagingOffset, &cullData, sizeof(VoxelChunkCullData));
        get_regions(cullDataCopies, vkCullData).push_back({
            .srcOffset = stagingOffset,
            .dstOffset = task.drawSlotIndex * sizeof(VoxelChunkCullData),
            .size = sizeof(VoxelChunkCullData),
        });
        stagingOffset += sizeof(VoxelChunkCullData);
    }

    // Barrier : lecture state to TRANSFER_DST
    // std::vector<VkBufferMemoryBarrier2> beforeBarriers;
    // for (auto& e : facesCopies) beforeBarriers.push_back({
    //     .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    //     .srcStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    //     .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
    //     .dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //     .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //     .buffer = e.dst, .offset = 0, .size = VK_WHOLE_SIZE,
    // });
    // for (auto& e : oubCopies) beforeBarriers.push_back({
    //     .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    //     .srcStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    //     .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
    //     .dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //     .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //     .buffer = e.dst, .offset = 0, .size = VK_WHOLE_SIZE,
    // });
    // for (auto& e : indirectCopies) beforeBarriers.push_back({
    //     .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    //     .srcStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    //     .srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
    //     .dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //     .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //     .buffer = e.dst, .offset = 0, .size = VK_WHOLE_SIZE,
    // });
    // VkDependencyInfo depBefore = {
    //     .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    //     .bufferMemoryBarrierCount = (uint32_t)beforeBarriers.size(),
    //     .pBufferMemoryBarriers    = beforeBarriers.data(),
    // };
    // vkCmdPipelineBarrier2(cmd, &depBefore);

    // Copies command
    {
        VOXEL_VK_ZONE(m_backend->tracyVkCtx, cmd, "GPU Upload Chunk Meshes");
        for (auto &e: facesCopies)
            vkCmdCopyBuffer(cmd, staging.buffer, e.dst, (uint32_t) e.regions.size(), e.regions.data());
        for (auto &e: oubCopies)
            vkCmdCopyBuffer(cmd, staging.buffer, e.dst, (uint32_t) e.regions.size(), e.regions.data());
        for (auto &e: cullDataCopies)
            vkCmdCopyBuffer(cmd, staging.buffer, e.dst, (uint32_t) e.regions.size(), e.regions.data());
    }

    // Barrier : TRANSFER_DST → lecture/indirect read
    // std::vector<VkBufferMemoryBarrier2> afterBarriers;
    // for (auto& e : facesCopies) afterBarriers.push_back({
    //     .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    //     .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //     .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //     .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    //     .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
    //     .buffer = e.dst, .offset = 0, .size = VK_WHOLE_SIZE,
    // });
    // for (auto& e : oubCopies) afterBarriers.push_back({
    //     .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    //     .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //     .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //     .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    //     .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
    //     .buffer = e.dst, .offset = 0, .size = VK_WHOLE_SIZE,
    // });
    // for (auto& e : indirectCopies) afterBarriers.push_back({
    //     .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    //     .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    //     .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    //     .dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    //     .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
    //     .buffer = e.dst, .offset = 0, .size = VK_WHOLE_SIZE,
    // });
    // VkDependencyInfo depAfter = {
    //     .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    //     .bufferMemoryBarrierCount = (uint32_t)afterBarriers.size(),
    //     .pBufferMemoryBarriers    = afterBarriers.data(),
    // };
    // vkCmdPipelineBarrier2(cmd, &depAfter);

    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                        | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                        | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                         | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
    };
    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep); // block until the end of transfer writes, so that the new data is visible to subsequent draw calls in the same frame

    m_uploadTasks.clear();
    m_currentTotalSize = 0;
}
