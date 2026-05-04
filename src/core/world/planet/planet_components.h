#pragma once
#include "renderer/world/planet/PlanetQuadtree.h"

namespace vp {


    struct PlanetChunkCoord {
        CubeFace face;
        int x;
        int y;
        int altitude;

        bool operator==(PlanetChunkCoord const& other) const {
            return face == other.face && x == other.x && y == other.y && altitude == other.altitude;
        }
    };

    struct PlanetChunkCoordHash {
        std::size_t operator()(PlanetChunkCoord const& c) const {
            uint64_t key =
              (static_cast<uint64_t>(c.face)          << 61) |
              (static_cast<uint64_t>(c.altitude & 0x1FFF) << 48) |
              (static_cast<uint64_t>(c.y        & 0xFFFFFF) << 24) |
              (static_cast<uint64_t>(c.x        & 0xFFFFFF));
            return std::hash<uint64_t>{}(key);
        }
    };

    struct PlanetComp {
        // world unit (meter)
        float radius = 6371.0f;

    };

    struct PlanetGenerationConfig {
        float radius = 6371.0f;

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
