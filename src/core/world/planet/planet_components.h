#pragma once

#include <cstddef>
#include <cstdint>

namespace vp {
    struct PlanetComp {
        float radius = 667544.0f;
    };

    struct PlanetTerrainParams {
        float seaLevel = 0.f;
        float continentAmplitude = 1200.f;
        float continentFrequency = 1.5f;
        float mountainAmplitude = 600.f;
        float mountainFrequency = 8.f;
        int continentOctave = 4;
        int maxOctave = 10;
        int32_t seed = 1234;
    };

    
}
