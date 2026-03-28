#pragma once
#include <cstdlib>
#include <memory>
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>

#include "core/resource/asset_id.h"

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

struct LoadedBy {};

namespace voxel_chunk_state {
    struct Clean {};
    struct Dirty {};
}
struct VoxelChunkState {};

struct VoxelChunk {
    std::shared_ptr<std::array<uint8_t, CHUNK_VOLUME>> voxels;
    std::unordered_map<AssetID, uint8_t> textureIDs;
    uint8_t lod = 0;

    VoxelChunk() : voxels(std::make_shared<std::array<uint8_t, CHUNK_VOLUME>>()) {
        voxels->fill(0);
    }

    void ensure_unique() {
        if (voxels.use_count() > 1) {
            voxels = std::make_shared<std::array<uint8_t, CHUNK_VOLUME>>(*voxels);
        }
    }

    void set(int x, int y, int z, uint8_t value) {
        ensure_unique();
        (*voxels)[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE] = value;
    }

    uint8_t& at(int x, int y, int z) {
        return voxels->at(x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE);
    }

    uint8_t at(int x, int y, int z) const {
        return voxels->at(x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE);
    }
};




