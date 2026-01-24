#pragma once

#include <glm/glm.hpp>
#include <FastNoise/FastNoise.h>

#include "world_components.h"

class WorldGenerator {
public:
    WorldGenerator(const int64_t seed = 0);
    ~WorldGenerator();

    /**
     * Generate a voxel chunk content at the given chunk position based in the world generator parameters.
     * If there is no data to generate (only air), return false.
     * @param chunk The chunk to fill
     * @param chunkPosition The position of the chunk in chunk coordinates
     * @return True if the chunk has been filled with data, false if it is empty (all air)
     */
    bool generate_chunk(VoxelChunk& chunk, glm::ivec3 chunkPosition);

private:
    int64_t m_seed;

    FastNoise::SmartNode<FastNoise::FractalFBm> m_terrainNoise; // determine terrain height
};
