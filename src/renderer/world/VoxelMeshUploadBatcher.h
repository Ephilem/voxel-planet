#pragma once

#include <vulkan/vulkan_core.h>

#include "VoxelBuffer.h"
#include "renderer/rendering_components.h"
#include "renderer/vulkan/VulkanBackend.h"

/// Help to batches multiple mesh uploads together to reduce the number of GPU calls and increase performance.
/// This is especially useful when multiple chunks are meshed in the same frame, which can lead to multiple uploads in the same frame.
class VoxelMeshUploadBatcher {
public:
    static constexpr int STAGING_NUMBER = MAX_FRAMES_IN_FLIGHT;
    static constexpr VkDeviceSize MAX_STAGING_BUFFER_SIZE = 16 * 1024 * 1024;

     VoxelMeshUploadBatcher() = default;
     ~VoxelMeshUploadBatcher() = default;

    /**
     * Initialize the batcher with the Vulkan backend. This will create the staging buffers and other necessary resources.
     * @param backend vulkan backend
     */
    void init(VulkanBackend* backend);
    void destroy();

    /**
     * Will try to queue a mesh to upload to the GPU.
     * If there is not space left in the staging buffer, this will return false and the caller should try again in the next frame.
     * @param meshData Mesh data to upload
     * @param oub
     * @param targetBuffer
     * @return true if the mesh was successfully queued for upload, false if there was not enough space in the staging buffer and the caller should try again in the next frame.
     */
    bool enqueue(const VoxelChunkMesh& meshData, const TerrainOUB& oub, VoxelBuffer* targetBuffer);

    /**
     * Flush and execute all queued uploads. This should be called once per frame after all mesh uploads have been enqueued.
     * @param cmd Command buffer to record the copy commands into. This should be the same command buffer that will be submitted for rendering after this, so the uploads are guaranteed to be executed before rendering.
     */
    void flush(VkCommandBuffer cmd);

private:
    struct StagingBuffer {
        VkBuffer buffer;
        VmaAllocation allocation;
        VmaAllocationInfo info;
    };

    struct UploadTask {
        const void* faceData;
        VkDeviceSize faceDataSize;
        VkDeviceSize faceDataOffset; // offset in the target buffer

        TerrainOUB oub;

        uint32_t drawSlotIndex;

        VoxelBuffer* targetBuffer; // target buffer to copy the data to
    };

    VulkanBackend* m_backend = nullptr;

    StagingBuffer m_stagingBuffers[STAGING_NUMBER];
    std::vector<UploadTask> m_uploadTasks;
};