#include "PlanetWorldGenerator.h"

#include "planet_utils.h"

using namespace vp;

PlanetWorldGenerator::PlanetWorldGenerator() {
}

bool PlanetWorldGenerator::generate_planet_chunk(VoxelChunk &chunk, PlanetChunkCoord coord, const PlanetGenerationConfig &config) {
    check_noise_generator(config);
    float planetRadius = config.radius;

    glm::dvec3 chunkWorldPos = planet_chunk_to_world(coord, static_cast<double>(planetRadius));
    double chunkRadialDist = glm::length(chunkWorldPos);

    bool anyVoxel = false;

    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                // Absolute position of this voxel in planet-relative world space
                // We compute the surface direction from chunk center + local offset projected back
                double u0 = static_cast<double>(coord.x * CHUNK_SIZE + lx) / planetRadius;
                double v0 = static_cast<double>(coord.y * CHUNK_SIZE + ly) / planetRadius;
                glm::dvec3 cubeDir = face_to_cube_dir(coord.face, u0, v0);
                glm::dvec3 surfaceDir = glm::normalize(cubeDir);

                // Radial distance of this voxel from planet center
                double voxelRadius = planetRadius
                                     + static_cast<double>(coord.altitude) * CHUNK_SIZE
                                     + static_cast<double>(lz);

                // Sample terrain height at this surface direction
                float terrainHeight = sample_terrain_height(surfaceDir, config);

                // The terrain surface is at planetRadius + terrainHeight voxels from center
                double surfaceRadius = static_cast<double>(planetRadius) + terrainHeight;

                if (voxelRadius <= surfaceRadius) {
                    // Determine block type: top layer = grass, below = stone
                    bool isTop = (voxelRadius + 1.0 > surfaceRadius);
                    uint8_t textureLocalID = isTop ? 1 : 2;

                    ChunkBlockInfo block;
                    block.localTextureID = textureLocalID;

                    // Partial-height top voxel for smooth terrain
                    if (isTop) {
                        double overflow = surfaceRadius - voxelRadius; // 0..1
                        block.height = static_cast<uint8_t>(
                            glm::clamp(static_cast<int>(overflow * 16.0), 1, 15));
                    } else {
                        block.height = 15; // full voxel
                    }

                    chunk.set(lx, ly, lz, block);
                    anyVoxel = true;
                }
            }
        }
    }

    return anyVoxel;
}

FastNoise::SmartNode<FastNoise::FractalFBm> PlanetWorldGenerator::create_noise_generator(
    const PlanetGenerationConfig &config) {
    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    auto simplex = FastNoise::New<FastNoise::Simplex>();

    fractal->SetSource(simplex);
    fractal->SetOctaveCount(config.maxOctave);
    fractal->SetLacunarity(config.lacunarity);
    fractal->SetGain(config.gain);

    return fractal;
}

float PlanetWorldGenerator::sample_terrain_height(const glm::dvec3 &dir, const PlanetGenerationConfig &config) {
    float nx = static_cast<float>(dir.x) * config.frequency;
    float ny = static_cast<float>(dir.y) * config.frequency;
    float nz = static_cast<float>(dir.z) * config.frequency;

    float noiseVal = m_terrainNoise->GenSingle3D(nx, ny, nz, static_cast<int>(config.seed));

    float t = (noiseVal + 1.0f) * 0.5f;
    return config.baseHeight + t * static_cast<float>(config.heightAmplitude);
}

void PlanetWorldGenerator::check_noise_generator(const PlanetGenerationConfig &config) {
    if (m_terrainNoise != nullptr && config == m_cachedConfig) return;
    m_terrainNoise = create_noise_generator(config);
    m_cachedConfig = config;
}
