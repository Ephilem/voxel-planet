#pragma once

#include <FastNoise/FastNoise.h>

#include "planet_components.h"
#include "core/world/world_components.h"

namespace vp {

class PlanetWorldGenerator {
public:
    PlanetWorldGenerator();

     /**
      * Generate a voxel chunk content at the given chunk position based in the planet generation config.
      * If there is no data to generate (only air), return false.
      * @param chunk The chunk to fill
      * @param coord The position of the chunk in planet chunk coordinates
      * @param config The planet generation config to use for this chunk generation
      * @return True if the chunk has been filled with data, false if it is empty (all air)
      */
    bool generate_planet_chunk(VoxelChunk &chunk, SurfaceChunkCoord coord, const PlanetGenerationConfig& config);

private:
    PlanetGenerationConfig m_cachedConfig;
    FastNoise::SmartNode<FastNoise::FractalFBm> m_terrainNoise;

    FastNoise::SmartNode<FastNoise::FractalFBm> create_noise_generator(const PlanetGenerationConfig& config);

    /**
     * Check if the noise generator needs to be recreated based on the current planet generation config. If so, recreate it and update the cached config.
     */
    void check_noise_generator(const PlanetGenerationConfig& config);

    float sample_terrain_height(const glm::dvec3 &dir, const PlanetGenerationConfig &config);

};

}
