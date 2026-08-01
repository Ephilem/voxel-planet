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
     * @param oub Per chunk data, layout owned by the renderer that fills it
     * @param aabbMin World space lower corner of the mesh, stored in the chunk cull data
     * @param aabbMax World space upper corner of the mesh, stored in the chunk cull data
     * @param targetBuffer
     * @return true if the mesh was successfully queued for upload, false if there was not enough space in the staging buffer and the caller should try again in the next frame.
     */
    bool enqueue(const VoxelChunkMesh& meshData, const TerrainOUB& oub,
                 const glm::vec3& aabbMin, const glm::vec3& aabbMax,
                 VoxelBuffer* targetBuffer);

    /**
     * Will write to the draw slot index a null draw (instance count = 0)
     * Will not clean the face regions, it will just be ignored and overwritten by the next upload
     * @param drawSlotIndex index of the draw slot to free. This should be the same index that was used in the original enqueue() call for this mesh.
     * @param targetBuffer target buffer to write the null draw to. This should be the same buffer that was used in the original enqueue() call for this mesh.
     * @return true if the free command was successfully queued, false if there was not enough space in the staging buffer and the caller should try again in the next frame
     */
    bool enqueue_free(uint32_t drawSlotIndex, VoxelBuffer* targetBuffer);

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

        // World space bounds of the mesh. They used to be rebuilt here by pushing the 8 corners
        // of a CHUNK_SIZE cube through oub.model, which only ever worked for the renderer that
        // happened to store a model matrix there. The LOD path stores a packed node coordinate in
        // the same bytes, so that produced a singular matrix and garbage bounds, and it ignored
        // the LOD level entirely. The caller knows its own layout, so it passes them in.
        glm::vec4 aabbMin;
        glm::vec4 aabbMax;

        uint32_t drawSlotIndex;

        VoxelBuffer* targetBuffer; // target buffer to copy the data to
    };

    VulkanBackend* m_backend = nullptr;

    uint64_t m_currentTotalSize = 0;

    StagingBuffer m_stagingBuffers[STAGING_NUMBER];
    std::vector<UploadTask> m_uploadTasks;
};