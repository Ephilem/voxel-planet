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
        float continentFrequency = 2.5f;
        float mountainAmplitude = 1200.f;
        float mountainFrequency = 12.f;
        int   continentOctave = 5;
        int maxOctave = 10;
        int32_t seed = 1234;
    };

    
}
