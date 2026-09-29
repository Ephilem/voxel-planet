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
#define PLANET_VOXEL_SIZE_LOD0 1.0

enum class VoxelID : uint16_t { Air = 0 };
enum class LocalVoxelID : uint8_t { Air = 0 };

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

struct PlanetVoxelCoord {
    CubemapFace face = FACE_UNKNOWN;
    glm::i64vec3 voxel{0};
};

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

    bool valid() const { return face != FACE_UNKNOWN; }

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
    LocalVoxelID localBlockID; // index into the chunk's textureIDs map, which maps to an AssetID for the actual texture
    uint8_t height;            // for terrain blocks. 0-15 is the height of the block, subdivision

    AABB get_block_aabb() const {
        if (localBlockID == LocalVoxelID::Air)
            return AABB::Zero();

        float h = static_cast<float>(height) / 16.0f;
        return AABB{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, h, 1.0f)};
    }
};

/**
 * Maps BlockID (global registry id) -> local id in a chunk (0-255)
 */
struct PlanetSurfaceChunkPalette {
    std::vector<VoxelID> byLocalId;

    PlanetSurfaceChunkPalette() {
        byLocalId.push_back(VoxelID::Air); // local id 0 is always air
    }

    /**
     * Get the global BlockID of the block mapped to the given local id
     * @param local Local id found in the voxel array
     * @return The global BlockID of the block, or BlockID::Air if the local id is out of range
     */
    [[nodiscard]] VoxelID global(LocalVoxelID local) const {
        const auto idx = static_cast<size_t>(local);
        return idx < byLocalId.size() ? byLocalId[idx] : VoxelID::Air;
    }

    /**
     * Intern a global BlockID into this chunk's palette, returning its local id.
     * @param globalId BlockID already resolved from the planet's voxel registry
     * @return The LocalBlockID to store in the voxel array, or LocalBlockID{0xFF} if the palette is full
     */
    LocalVoxelID intern(VoxelID globalId) {
        for (size_t i = 0; i < byLocalId.size(); ++i) {
            if (byLocalId[i] == globalId) {
                return static_cast<LocalVoxelID>(i);
            }
        }
        if (byLocalId.size() >= 256) {
            return static_cast<LocalVoxelID>(0xFF);
        }
        byLocalId.push_back(globalId);
        return static_cast<LocalVoxelID>(byLocalId.size() - 1);
    }
};

using PlanetVoxelArray = std::array<uint16_t, CHUNK_VOLUME>;

constexpr int planet_voxel_index(int x, int y, int z) {
    return x + (y * CHUNK_SIZE) + (z * CHUNK_SIZE * CHUNK_SIZE);
}

constexpr uint16_t planet_voxel_encode(PlanetSurfaceChunkBlockInfo block) {
    return uint16_t(uint8_t(block.localBlockID)) | uint16_t(uint16_t(block.height) << 8);
}

/**
 * Represets a chunk of voxels on the surface of a planet, with a palette mapping local block IDs to global block IDs.
 * Immutable once built, like a snapshot of a chunk. The voxel array is shared_ptr<const> to allow sharing between
 * multiple consumers without copying
 */
struct PlanetSurfaceVoxelChunk {
    std::shared_ptr<const PlanetVoxelArray> voxels; // nullptr = only air
    PlanetSurfaceChunkPalette palette;

    /// Air chunk, without voxel array
    PlanetSurfaceVoxelChunk() = default;

    PlanetSurfaceVoxelChunk(std::shared_ptr<const PlanetVoxelArray> voxels, PlanetSurfaceChunkPalette palette)
        : voxels(std::move(voxels)), palette(std::move(palette)) {}

    PlanetSurfaceChunkBlockInfo at(int x, int y, int z) const {
        return reinterpret_cast<const PlanetSurfaceChunkBlockInfo&>((*voxels)[planet_voxel_index(x, y, z)]);
    }

    PlanetSurfaceChunkBlockInfo at(glm::ivec3 localPos) const { return at(localPos.x, localPos.y, localPos.z); }

    [[nodiscard]] bool is_allocated() const { return voxels != nullptr; }
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

FMT_BEGIN_NAMESPACE

template <> struct formatter<vp::PlanetSurfaceChunkKey> {
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template <typename FormatContext> auto format(const vp::PlanetSurfaceChunkKey& k, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}[{},{},{}]@{}", vp::face_name(k.face), k.x, k.y, k.alt, k.level);
    }
};

FMT_END_NAMESPACE
