#pragma once

#include <array>

#include <FastNoise/FastNoise.h>

#include "planet_components.h"
#include "core/world/world_components.h"

namespace vp {

class PlanetWorldGenerator {
public:
    /**
     * Wavelength of the coarsest terrain octave, in world meters.
     * Flat terrain phase only: the spherical path samples a normalized direction instead, and
     * uses PlanetGenerationConfig::frequency for the same role.
     */
    static constexpr float FLAT_TERRAIN_WAVELENGTH = 512.0f;

    PlanetWorldGenerator();

     /**
      * Generate a voxel chunk content at the given node coordinate, at that node's LOD level.
      * If there is no data to generate (only air), return false.
      *
      * A node at level L holds CHUNK_SIZE voxels of (1 << L) meters each, so a coarse node
      * covers more ground with the same voxel budget. The ground height is kept in sub voxel
      * units, which is what stops a level 8 node (256 m per voxel) from flattening a 100 m
      * terrain down to nothing.
      *
      * @param chunk The chunk to fill
      * @param coord The position of the chunk in planet node coordinates, level included
      * @param config The planet generation config to use for this chunk generation
      * @return True if the chunk has been filled with data, false if it is empty (all air)
      */
    bool generate_planet_chunk(VoxelChunk &chunk, PlanetNodeCoord coord, const PlanetGenerationConfig& config);

private:
    /// GpuNode::level is 4 bits, and no fractal is worth more octaves than that anyway
    static constexpr int MAX_OCTAVES = 16;

    /// Chunk local texture ids, the keys of VoxelChunk::textureIDs
    static constexpr uint8_t TEXTURE_GRASS = 1;
    static constexpr uint8_t TEXTURE_STONE = 2;

    /// ChunkBlockInfo::height of a block with no gap above it
    static constexpr uint8_t VOXEL_FULL_HEIGHT = 15;

    PlanetGenerationConfig m_cachedConfig;

    /// One fractal per octave count, built on demand. Index 0 stays empty
    std::array<FastNoise::SmartNode<FastNoise::FractalFBm>, MAX_OCTAVES + 1> m_noiseByOctaves;

    FastNoise::SmartNode<FastNoise::FractalFBm> create_noise_generator(int octaves, const PlanetGenerationConfig& config);

    /**
     * Number of octaves a node at this level can actually represent.
     *
     * An octave whose wavelength falls below two voxels cannot be resolved and would alias into
     * a different terrain rather than a smoothed one, which is what makes LOD transitions pop.
     * Coarse nodes therefore drop the fine octaves.
     *
     * @param level LOD level of the node
     * @param config Generation config, for the octave ceiling and the lacunarity
     * @return Octave count, at least 1
     */
    static int octaves_for_level(uint8_t level, const PlanetGenerationConfig& config);

    /**
     * Fractal to sample a node of this level with, created on first use.
     * @param level LOD level of the node
     * @param config Generation config
     * @return Borrowed generator, owned by the cache
     */
    FastNoise::Generator* noise_for_level(uint8_t level, const PlanetGenerationConfig& config);

    /**
     * Drop the whole fractal cache when the generation config changes underneath it.
     */
    void check_noise_generator(const PlanetGenerationConfig& config);

    float sample_terrain_height(const glm::dvec3 &dir, const PlanetGenerationConfig &config);

};

}
