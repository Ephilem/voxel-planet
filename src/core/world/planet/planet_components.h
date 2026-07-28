#pragma once

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
        CubeFace face;
        unsigned char level = 0; // 0 the smallest
        int32_t u = 0, v = 0, alt = 0;
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
