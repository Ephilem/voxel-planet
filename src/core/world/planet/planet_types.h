#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "core/math/aabb.h"
#include "core/resource/asset_id.h"
#include "utils/maths/maths.h"

namespace vp {
#define CHUNK_SIZE 32
#define CHUNK_VOLUME (CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE)

enum class BlockID : uint16_t { Air = 0 };
enum class LocalBlockId : uint8_t { Air = 0 };

enum CubemapFace : uint8_t {
    FACE_POS_X = 0,
    FACE_NEG_X = 1,
    FACE_POS_Y = 2,
    FACE_NEG_Y = 3,
    FACE_POS_Z = 4,
    FACE_NEG_Z = 5,

    FACE_UNKNOWN = 0xFF
};

inline const char* face_name(CubemapFace face) {
    switch (face) {
    case FACE_POS_X:
        return "+X";
    case FACE_NEG_X:
        return "-X";
    case FACE_POS_Y:
        return "+Y";
    case FACE_NEG_Y:
        return "-Y";
    case FACE_POS_Z:
        return "+Z";
    case FACE_NEG_Z:
        return "-Z";
    default:
        return "??";
    }
}

struct PlanetSurfaceChunkKey {
    int32_t x = 0;
    int32_t y = 0;
    int32_t alt = 0;   // signé : sous le niveau de la mer
    uint8_t level = 0; // 0 = le plus fin
    CubemapFace face = FACE_UNKNOWN;

    inline PlanetSurfaceChunkKey() = default;

    inline PlanetSurfaceChunkKey(CubemapFace face, uint8_t level, const glm::ivec3& voxel) {
        this->face = face;
        this->level = level;
        this->x = floor_div(voxel.x, CHUNK_SIZE);
        this->y = floor_div(voxel.y, CHUNK_SIZE);
        this->alt = floor_div(voxel.z, CHUNK_SIZE);
    }

    bool operator==(const PlanetSurfaceChunkKey& o) const {
        return x == o.x && y == o.y && alt == o.alt && level == o.level && face == o.face;
    }

    bool operator!=(const PlanetSurfaceChunkKey& o) const { return !(*this == o); }
};

static_assert(sizeof(PlanetSurfaceChunkKey) == 16);

struct BlockDefinition {
    AssetID id = AssetID::Invalid;
    AssetID texture{};
    std::string_view name;

    bool opaque = true;

    static BlockDefinition Uniform(AssetID blockId, AssetID tex) {
        BlockDefinition d;
        d.id = blockId;
        d.texture = tex;
        return d;
    }

    static BlockDefinition Uniform(NamedAssetID blockId, AssetID tex) {
        BlockDefinition d = Uniform(blockId.id, tex);
        d.name = blockId.namespaceString;
        return d;
    }
};

struct PlanetSurfaceChunkBlockInfo {
    uint8_t localTextureID =
        0;          // index into the chunk's textureIDs map, which maps to an AssetID for the actual texture
    uint8_t height; // for terrain blocks. 0-15 is the height of the block, subdivision

    AABB get_block_aabb() const {
        if (localTextureID == 0)
            return AABB::Zero();

        float h = static_cast<float>(height) / 16.0f;
        return AABB{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, h, 1.0f)};
    }
};

/**
 * Maps AssetID -> local id in a chunk
 */
struct PlanetSurfaceChunkPalette {
    std::vector<AssetID> byLocalId;

    PlanetSurfaceChunkPalette() {
        byLocalId.push_back("voxelplanet:air"_asset); // local id 0 is always air
    }

    /**
     * Get the asset id of the block mapped to the given local id
     * @param local Local id found in the voxel array
     * @return The global AssetID of the block, or AssetID::Invalid if the local id is out of range
     */
    [[nodiscard]] AssetID global(uint8_t local) const {
        return local < byLocalId.size() ? byLocalId[local] : AssetID::Invalid;
    }

    uint8_t intern(AssetID globalId) {
        for (size_t i = 0; i < byLocalId.size(); ++i)
            if (byLocalId[i] == globalId)
                return static_cast<uint8_t>(i);
        if (byLocalId.size() >= 256)
            return 0xFF;
        byLocalId.push_back(globalId);
        return static_cast<uint8_t>(byLocalId.size() - 1);
    }
};

struct PlanetSurfaceVoxelChunk {
    std::shared_ptr<std::array<uint16_t, CHUNK_VOLUME>> voxels;
    PlanetSurfaceChunkPalette palette;

    /// Tag for the constructor that leaves the voxel array unallocated. The planet generator
    /// rejects the large majority of the nodes it is handed on a pair of altitude comparisons,
    /// and a chunk is 64 KB that would be allocated and zeroed only to be thrown away
    struct Unallocated {};

    PlanetSurfaceVoxelChunk() : voxels(std::make_shared<std::array<uint16_t, CHUNK_VOLUME>>()) { voxels->fill(0); }

    explicit PlanetSurfaceVoxelChunk(Unallocated) {}

    /// Allocate the voxel array if it is not there yet. Cheap to call on an allocated chunk
    void allocate() {
        if (voxels)
            return;
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

    void set(int x, int y, int z, PlanetSurfaceChunkBlockInfo blockInfo) {
        ensure_unique();

        (*voxels)[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE] =
            blockInfo.localTextureID | static_cast<uint16_t>(blockInfo.height) << 8;
    }

    PlanetSurfaceChunkBlockInfo at(int x, int y, int z) const {
        return reinterpret_cast<const PlanetSurfaceChunkBlockInfo&>(
            (*voxels)[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE]);
    }

    PlanetSurfaceChunkBlockInfo at(glm::ivec3 localPos) const { return at(localPos.x, localPos.y, localPos.z); }
};

} // namespace vp

template <> struct std::hash<vp::PlanetSurfaceChunkKey> {
    size_t operator()(const vp::PlanetSurfaceChunkKey& k) const noexcept {
        size_t h = 1469598103934665603ull;
        auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        mix(static_cast<uint32_t>(k.x));
        mix(static_cast<uint32_t>(k.y));
        mix(static_cast<uint32_t>(k.alt));
        mix(static_cast<uint32_t>(k.level) | (static_cast<uint32_t>(k.face) << 8));
        return h;
    }
};
