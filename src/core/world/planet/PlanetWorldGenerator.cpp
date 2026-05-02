#include "PlanetWorldGenerator.h"

#include "planet_utils.h"
#include "core/TracyIntegration.h"

using namespace vp;

PlanetWorldGenerator::PlanetWorldGenerator() {
}

bool PlanetWorldGenerator::generate_planet_chunk(VoxelChunk &chunk, PlanetChunkCoord coord, const PlanetGenerationConfig &config) {
    VOXEL_ZONE_N("Generate Planet Chunk");
    check_noise_generator(config);
    float planetRadius = config.radius;

    chunk.textureIDs = {
        {"voxelplanet:textures/grass"_asset, 1},
        {"voxelplanet:textures/cobblestone"_asset, 2}
    };

    std::array<glm::vec3, CHUNK_SIZE * CHUNK_SIZE> surfaceDirs;
    std::array<float, CHUNK_SIZE * CHUNK_SIZE> noiseInputX, noiseInputY, noiseInputZ;

    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            double u0 = static_cast<double>(coord.x * CHUNK_SIZE + lx) / planetRadius;
            double v0 = static_cast<double>(coord.y * CHUNK_SIZE + ly) / planetRadius;
            glm::dvec3 dir = glm::normalize(face_to_cube_dir(coord.face, u0, v0));
            int idx = lx + ly * CHUNK_SIZE;
            surfaceDirs[idx] = glm::vec3(dir);
            noiseInputX[idx] = static_cast<float>(dir.x) * config.frequency;
            noiseInputY[idx] = static_cast<float>(dir.y) * config.frequency;
            noiseInputZ[idx] = static_cast<float>(dir.z) * config.frequency;
        }
    }

    std::array<float, CHUNK_SIZE * CHUNK_SIZE> noiseOut;
    m_terrainNoise->GenPositionArray3D(
        noiseOut.data(),
        CHUNK_SIZE * CHUNK_SIZE,
        noiseInputX.data(),
        noiseInputY.data(),
        noiseInputZ.data(),
        0.0f, 0.0f, 0.0f,
        static_cast<int>(config.seed));

    std::array<double, CHUNK_SIZE * CHUNK_SIZE> surfaceRadii;
    for (int i = 0; i < CHUNK_SIZE * CHUNK_SIZE; ++i) {
        float t = (noiseOut[i] + 1.0f) * 0.5f;
        float terrainHeight = config.baseHeight + t * static_cast<float>(config.heightAmplitude);
        surfaceRadii[i] = static_cast<double>(planetRadius) + terrainHeight;
    }

    bool anyVoxel = false;
    double baseRadius = static_cast<double>(planetRadius)
                        + static_cast<double>(coord.altitude) * CHUNK_SIZE;

    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        double voxelRadius = baseRadius + static_cast<double>(lz);

        for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                double surfaceRadius = surfaceRadii[lx + ly * CHUNK_SIZE];

                if (voxelRadius > surfaceRadius) continue;

                bool isTop = (voxelRadius + 1.0 > surfaceRadius);
                ChunkBlockInfo block;
                block.localTextureID = isTop ? 1 : 2;

                if (isTop) {
                    double overflow = surfaceRadius - voxelRadius;
                    block.height = static_cast<uint8_t>(glm::clamp(static_cast<int>(overflow * 16.0), 1, 15));
                } else {
                    block.height = 15;
                }

                chunk.set(lx, ly, lz, block);
                anyVoxel = true;
            }
        }
    }

    return anyVoxel;
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
