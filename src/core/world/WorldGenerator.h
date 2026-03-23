#pragma once

#include <glm/glm.hpp>
#include <FastNoise/FastNoise.h>

#include "world_components.h"

class WorldGenerator {
public:
    struct NoiseParams {
        int64_t seed          = 0;
        float   frequency     = 0.01f;
        int     octaves       = 5;
        float   lacunarity    = 2.0f;
        float   gain          = 0.5f;
        int     baseHeight    = 100;
        int     heightAmplitude = 32;
    };

    WorldGenerator(int64_t seed = 0);
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

    // --- Debug / preview API ---

    NoiseParams get_params() const { return m_params; }

    /**
     * Update noise parameters and rebuild the noise graph.
     * Thread-unsafe: do not call while generation workers are running.
     */
    void set_params(const NoiseParams& params);

    /**
     * Sample the terrain heightmap at arbitrary world-space positions.
     * Fills outHeights[z * width + x] with the terrain height (world Y, in voxels)
     * for each (worldX + x, worldZ + z) position.
     * @param worldX  Starting world X coordinate
     * @param worldZ  Starting world Z coordinate
     * @param width   Number of samples along X
     * @param height  Number of samples along Z
     * @param outHeights  Output buffer, must be at least width * height floats
     */
    void sample_heightmap(int worldX, int worldZ, int width, int height, float* outHeights) const;

private:
    NoiseParams m_params;

    FastNoise::SmartNode<FastNoise::FractalFBm> m_terrainNoise;
    FastNoise::SmartNode<FastNoise::FractalFBm> m_roughNoise;

    void rebuild_noise();
};
