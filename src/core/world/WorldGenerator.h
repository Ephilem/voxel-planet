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

    struct ColumnBounds {
        int yMax; // highest world Y that may contain terrain (exclusive)
    };

    /**
     * Evaluate a single X/Z column to get the Y range that may contain terrain.
     * Uses 1 low-frequency noise sample + Lipschitz bound to conservatively
     * bracket the terrain height across the chunk column.
     * @param columnChunkPos The X/Z position of the column in chunk coordinates
     * @return Conservative [yMin, yMax) world-Y bounds for terrain in this column
     */
    ColumnBounds evaluate_column(glm::ivec2 columnChunkPos);


private:
    int64_t m_seed;

    float m_frequency = 0.01f;
    uint16_t m_baseHeight = 100;
    uint16_t m_heightAmplitude = 32;

    FastNoise::SmartNode<FastNoise::FractalFBm> m_terrainNoise; // determine terrain height
    FastNoise::SmartNode<FastNoise::FractalFBm> m_roughNoise;
};
