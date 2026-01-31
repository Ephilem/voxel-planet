#pragma once
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
        std::size_t h1 = std::hash<int>{}(v.x);
        std::size_t h2 = std::hash<int>{}(v.y);
        std::size_t h3 = std::hash<int>{}(v.z);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

struct ChunkCoordinate : glm::ivec3 {
    using glm::ivec3::ivec3;
    ChunkCoordinate(const glm::ivec3& v) : glm::ivec3(v) {}
};

struct ChunkLoader {
    glm::ivec3 lastVisitedChunk = glm::ivec3(INT32_MAX);
    std::unordered_set<glm::ivec3, IVec3Hash> desiredChunks; // Set of chunk coordinates that should be loaded
    int loadRadius = 4;
    int unloadRadius = 6; // > loadRadius to avoid load/unload thrashing at boundaries

    [[nodiscard]] bool has_visited() const {
        return lastVisitedChunk != glm::ivec3(INT32_MAX);
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




