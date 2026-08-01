#pragma once

#include <cstddef>
#include <cstdint>

namespace vp {
    enum CubeFace {
        PosX = 0,
        NegX = 1,
        PosY = 2,
        NegY = 3,
        PosZ = 4,
        NegZ = 5,
    };

    struct PlanetNodeCoord {
        CubeFace face = PosX;
        uint8_t level = 0;
        int32_t u = 0, v = 0, alt = 0;

        /**
         * Coordinate of one of the 8 children of this node.
         * @param index Child index, bit 0 selects u, bit 1 selects v, bit 2 selects alt
         * @return Coordinate at level - 1
         */
        PlanetNodeCoord child(uint32_t index) const {
            return {
                face,
                static_cast<uint8_t>(level - 1),
                u * 2 + static_cast<int32_t>(index & 1u),
                v * 2 + static_cast<int32_t>((index >> 1) & 1u),
                alt * 2 + static_cast<int32_t>((index >> 2) & 1u)
            };
        }

        /**
         * Coordinate of the parent node. Arithmetic shift keeps negative altitudes correct.
         * @return Coordinate at level + 1
         */
        PlanetNodeCoord parent() const {
            return {face, static_cast<unsigned char>(level + 1), u >> 1, v >> 1, alt >> 1};
        }

        bool operator==(const PlanetNodeCoord &other) const = default;
    };

    struct PlanetNodeCoordHash {
        size_t operator()(const PlanetNodeCoord &c) const noexcept {
            uint64_t k = (static_cast<uint64_t>(c.face) << 61)
                         | (static_cast<uint64_t>(c.level) << 57)
                         | (static_cast<uint64_t>(static_cast<uint32_t>(c.u) & 0xFFFFu) << 41)
                         | (static_cast<uint64_t>(static_cast<uint32_t>(c.v) & 0xFFFFu) << 25)
                         | (static_cast<uint64_t>(static_cast<uint32_t>(c.alt + 256) & 0x1FFu) << 16);
            k ^= k >> 33;
            k *= 0xff51afd7ed558ccdULL;
            k ^= k >> 33;
            return static_cast<size_t>(k);
        }
    };

    struct PlanetComp {
        // world unit (meter)s
        float radius = 667544.0f;
    };

    struct PlanetGenerationConfig {
        float radius = 667544.0f;

        // Simple heightmap
        int64_t seed = 0;
        float frequency = 32.f;
        int maxOctave = 6; // max because lod will reduce octaves number. This value is used for the highest resolution
        float lacunarity = 2.0f;
        float gain = 0.5f;
        int baseHeight = 100;
        int heightAmplitude = 32;

        bool operator==(const PlanetGenerationConfig &other) const {
            return seed == other.seed &&
                   frequency == other.frequency &&
                   maxOctave == other.maxOctave &&
                   lacunarity == other.lacunarity &&
                   gain == other.gain &&
                   baseHeight == other.baseHeight &&
                   heightAmplitude == other.heightAmplitude;
        }
    };


}
