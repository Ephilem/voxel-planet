//
// Created by raph on 18/03/2026.
//

#include "VoxelMeshUploadBatcher.h"

void VoxelMeshUploadBatcher::init(VulkanBackend* backend) {
    // create staging buffers
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size  = MAX_STAGING_BUFFER_SIZE;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocationCreateInfo = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
    };

    vmaCreateBuffer(backend->get_vma_allocator(), &bufferInfo, &allocationCreateInfo,
        &m_stagingBuffers[0].buffer, &m_stagingBuffers[0].allocation, &m_stagingBuffers[0].info);
}

void VoxelMeshUploadBatcher::destroy() {
}

bool VoxelMeshUploadBatcher::enqueue(const VoxelChunkMesh &meshData, const TerrainOUB &oub, VoxelBuffer* targetBuffer) {
}

void VoxelMeshUploadBatcher::flush(VkCommandBuffer cmd) {
}
