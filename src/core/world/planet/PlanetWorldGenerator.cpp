#include "PlanetWorldGenerator.h"

#include "planet_utils.h"
#include "core/TracyIntegration.h"

using namespace vp;

PlanetWorldGenerator::PlanetWorldGenerator() {
}

bool PlanetWorldGenerator::generate_planet_chunk(VoxelChunk &chunk, PlanetChunkCoord coord,
                                                 const PlanetGenerationConfig &config) {
    VOXEL_ZONE_N("Generate Planet Chunk");
    check_noise_generator(config);
    float planetRadius = config.radius;

    if (coord.altitude != 0) return false;

    chunk.textureIDs = {
        {"voxelplanet:textures/grass"_asset, 1},
        {"voxelplanet:textures/cobblestone"_asset, 2}
    };

    bool anyVoxel = false;
    for (int lx = -16; lx < 16; lx++) {
        for (int lz = -16; lz < 16; lz++) {
            int surfaceHeight = (glm::abs(lx) + glm::abs(lz)) / 2;
            // int surfaceHeight = 10;
            for (int ly = 0; ly < CHUNK_SIZE; ly++) {
                ChunkBlockInfo block{
                    .height = 15
                };

                if (ly < surfaceHeight - 1) {
                    block.localTextureID = 2; // cobble
                } else if (ly == surfaceHeight) {
                    block.localTextureID = 1;
                } else {
                    continue;
                }

                chunk.set(lx + 16, ly, lz + 16, block);
                anyVoxel = true;
            }
        }
    }

    return anyVoxel;

    // 3D noise inputs — uses all 3 direction components to avoid stretching on any face
    // std::array<float, CHUNK_SIZE * CHUNK_SIZE> noiseInputX, noiseInputY, noiseInputZ;
    //
    // for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
    //     for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
    //         double u = (coord.x * CHUNK_SIZE + lx) / planetRadius;
    //         double v = (coord.y * CHUNK_SIZE + lz) / planetRadius;
    //         glm::dvec3 dir = glm::normalize(face_to_cube_dir(coord.face, u, v));
    //
    //         double stretch = glm::length(glm::dvec3(u, v, 1.0)); // sqrt(u² + v² + 1)
    //         int idx = lx + lz * CHUNK_SIZE;
    //         noiseInputX[idx] = static_cast<float>(dir.x * config.frequency * stretch);
    //         noiseInputY[idx] = static_cast<float>(dir.y * config.frequency * stretch);
    //         noiseInputZ[idx] = static_cast<float>(dir.z * config.frequency * stretch);
    //     }
    // }
    //
    // std::array<float, CHUNK_SIZE * CHUNK_SIZE> noiseOut;
    // m_terrainNoise->GenPositionArray3D(
    //     noiseOut.data(),
    //     CHUNK_SIZE * CHUNK_SIZE,
    //     noiseInputX.data(),
    //     noiseInputY.data(),
    //     noiseInputZ.data(),
    //     0.0f, 0.0f, 0.0f,
    //     static_cast<int>(config.seed));
    //
    // std::array<float, CHUNK_SIZE * CHUNK_SIZE> surfaceHeights;
    // for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE; ++i) {
    //     surfaceHeights[i] = config.baseHeight + noiseOut[i] * config.heightAmplitude;
    // }
    //
    // bool anyVoxel = false;
    // int baseAltitudeVoxel = coord.altitude * CHUNK_SIZE;
    //
    // for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
    //     for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
    //         float surfaceHeight = surfaceHeights[lx + lz * CHUNK_SIZE];
    //         int surfaceHeightSub = static_cast<int>(surfaceHeight * 16.0f);
    //         int surfaceBlockY = surfaceHeightSub / 16;
    //         int surfaceSubHeight = surfaceHeightSub % 16;
    //
    //         for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
    //             int worldY = baseAltitudeVoxel + ly;
    //             ChunkBlockInfo block{};
    //
    //             if (worldY < surfaceBlockY - 1) {
    //                 block.localTextureID = 2; // cobble
    //                 block.height = 15;
    //             } else if (worldY == surfaceBlockY - 1) {
    //                 block.localTextureID = 1;
    //                 // grass plein
    //                 block.height = 15;
    //             } else if (worldY == surfaceBlockY) {
    //                 if (surfaceSubHeight == 0) continue;
    //                 block.localTextureID = 1; // grass sub-voxel
    //                 block.height = surfaceSubHeight - 1;
    //             } else {
    //                 continue;
    //             }
    //
    //             chunk.set(lx, ly, lz, block);
    //             anyVoxel = true;
    //         }
    //     }
    // }
    //
    //
    // return anyVoxel;
}

FastNoise::SmartNode<FastNoise::FractalFBm> PlanetWorldGenerator::create_noise_generator(
    const PlanetGenerationConfig &config) {
    VOXEL_ZONE_N("Create Noise Generator");
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
