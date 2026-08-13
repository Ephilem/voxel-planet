#pragma once
#include "core/world/planet/planet_types.h"
#include <glm/glm.hpp>
#include <vector>

namespace vp {

    struct PlanetTileKey {
        union {
            uint32_t value{};

            struct {
                uint32_t faceBits: 3;
                uint32_t levelBits: 5;
                uint32_t xBits: 12;
                uint32_t yBits: 12;
            };
        };

        PlanetTileKey() : value(0xFFFFFFFFu) {
        }

        PlanetTileKey(CubemapFace face, uint8_t level, uint32_t x, uint32_t y)
            : faceBits(uint32_t(face)), levelBits(level), xBits(x), yBits(y) {
        }

        [[nodiscard]] CubemapFace face() const { return CubemapFace(faceBits); }
        [[nodiscard]] uint8_t level() const { return uint8_t(levelBits); }
        [[nodiscard]] uint32_t x() const { return xBits; }
        [[nodiscard]] uint32_t y() const { return yBits; }

        [[nodiscard]] bool valid() const { return value != 0xFFFFFFFFu; }

        [[nodiscard]] PlanetTileKey parent() const {
            if (levelBits == 0) return {};
            return {face(), uint8_t(levelBits - 1), xBits / uint32_t(2), yBits / uint32_t(2)};
        }

        [[nodiscard]] float parent_offset_x() const { return float(xBits & 1u) * 0.5f; }
        [[nodiscard]] float parent_offset_y() const { return float(yBits & 1u) * 0.5f; }

        bool operator==(const PlanetTileKey &o) const { return value == o.value; }
    };
    static_assert(sizeof(PlanetTileKey) == 4);

    constexpr float PLANET_HEIGHT_SCALE = 16384.f;

    /// Quantizes an absolute altitude into the [0,65535] range encoded by a tile heightmap
    inline uint16_t planet_tile_encode_height(float altitude) {
        const float h01 = glm::clamp(altitude / (2.f * PLANET_HEIGHT_SCALE) + 0.5f, 0.f, 1.f);
        return static_cast<uint16_t>(h01 * 65535.f + 0.5f);
    }

    struct PlanetTileData {
        uint16_t resolution = 256;

        std::vector<uint16_t> heightmap;
    };

    using PlanetTileAtlasKey = uint16_t;
    constexpr PlanetTileAtlasKey INVALID_ATLAS_SLOT = 0xFFFFu;

    struct alignas(16) GpuPlanetTileDrawInstance {
        glm::vec3 originSpacePos;
        float extent;
        glm::vec2 nodeFaceOrigin;

        glm::vec2 uvOffset;

        /// face (3 bits) | level (5 bits)
        uint32_t packed;

        uint32_t atlasSlot;

        float uvScale;

        /// 0 = own resolution, 1 = snapped to the parent
        float morph;
    };
    static_assert(sizeof(GpuPlanetTileDrawInstance) == 48);

    inline uint32_t planet_tile_pack(CubemapFace face, uint8_t level) {
        return uint32_t(face) | (uint32_t(level) << 3);
    }

    /// Node extent in face coordinates, ie [-1,1] split 2^level times
    inline float planet_tile_extent(uint8_t level) {
        return 2.f / float(1u << level);
    }

    /// Bottom-left corner of a tile in its face's [-1,1] coordinates
    inline glm::vec2 planet_tile_face_origin(const PlanetTileKey &key) {
        const float extent = planet_tile_extent(key.level());
        return {-1.f + float(key.x()) * extent, -1.f + float(key.y()) * extent};
    }

    struct PlanetTileDrawItem {
        PlanetTileKey key;
        glm::vec3 originSpacePos;
        float morph;

        /// Camera distance to the tile bounds, meters. Already computed for the LOD test,
        /// carried over so the streamer can prioritize without recomputing it
        float distance = 0.f;
    };
}

// TO use PlanetTileKey as an hash
template<>
struct std::hash<vp::PlanetTileKey> {
    size_t operator()(const vp::PlanetTileKey &k) const noexcept {
        return std::hash<uint32_t>{}(k.value);
    }
};