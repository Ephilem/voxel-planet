#pragma once
#include <cstdlib>
#include <memory>
#include <glm/glm.hpp>
#include <unordered_map>
#include <array>

#include "core/math/aabb.h"
#include "core/resource/asset_id.h"

/////////////////////
/// Chunks related components
#define CHUNK_SIZE 32
#define CHUNK_VOLUME (CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE)

struct IVec3Hash {
      std::size_t operator()(const glm::ivec3& v) const {
          return static_cast<std::size_t>(v.x) * 73856093 ^
                 static_cast<std::size_t>(v.y) * 19349663 ^
                 static_cast<std::size_t>(v.z) * 83492791;
      }
  };

struct ChunkCoordinate : glm::ivec3 {
    using glm::ivec3::ivec3;
    ChunkCoordinate(const glm::ivec3& v) : glm::ivec3(v) {}
};

struct ChunkLoader {
    glm::ivec3 lastVisitedChunk = glm::ivec3(INT32_MAX);
    int loadRadius = 4;
    int unloadRadius = 6; // > loadRadius to avoid load/unload thrashing at boundaries

    [[nodiscard]] bool has_visited() const {
        return lastVisitedChunk != glm::ivec3(INT32_MAX);
    }

    [[nodiscard]] bool is_chunk_desired(const glm::ivec3& chunkPos) const {
        glm::ivec3 rel = chunkPos - lastVisitedChunk;
        return std::abs(rel.x) <= loadRadius &&
               std::abs(rel.y) <= loadRadius &&
               std::abs(rel.z) <= loadRadius;
    }
};

struct ChunkBlockInfo {
    uint8_t localTextureID = 0; // index into the chunk's textureIDs map, which maps to an AssetID for the actual texture
    uint8_t height; // for terrain blocks. 0-15 is the height of the block, subdivision

    AABB get_block_aabb() const {
        if (localTextureID == 0) return AABB::Zero();

        float h = static_cast<float>(height) / 16.0f;
        return AABB{
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f, h, 1.0f)
        };
    }
};

struct LoadedBy {};

namespace voxel_chunk_state {
    struct Clean {};
    struct Dirty {};
}
struct VoxelChunkState {};

struct VoxelChunk {
    // int16 = int8 for textureId (mapped by textureIds) + uint8 for height
    std::shared_ptr<std::array<uint16_t, CHUNK_VOLUME>> voxels;
    std::unordered_map<AssetID, uint8_t> textureIDs;

    /// Tag for the constructor that leaves the voxel array unallocated. The planet generator
    /// rejects the large majority of the nodes it is handed on a pair of altitude comparisons,
    /// and a chunk is 64 KB that would be allocated and zeroed only to be thrown away
    struct Unallocated {};

    VoxelChunk() : voxels(std::make_shared<std::array<uint16_t, CHUNK_VOLUME>>()) {
        voxels->fill(0);
    }

    explicit VoxelChunk(Unallocated) {}

    /// Allocate the voxel array if it is not there yet. Cheap to call on an allocated chunk
    void allocate() {
        if (voxels) return;
        voxels = std::make_shared<std::array<uint16_t, CHUNK_VOLUME>>();
        voxels->fill(0);
    }

    void ensure_unique() {
        if (!voxels) {
            allocate();
            return;
        }
        if (voxels.use_count() > 1) {
            voxels = std::make_shared<std::array<uint16_t, CHUNK_VOLUME>>(*voxels);
        }
    }

    void set(int x, int y, int z, ChunkBlockInfo blockInfo) {
        ensure_unique();

        (*voxels)[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE] = blockInfo.localTextureID | static_cast<uint16_t>(blockInfo.height) << 8;
    }

    ChunkBlockInfo at(int x, int y, int z) const {
        return reinterpret_cast<const ChunkBlockInfo&>((*voxels)[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE]);
    }

    ChunkBlockInfo at(glm::ivec3 localPos) const {
        return at(localPos.x, localPos.y, localPos.z);
    }
};




