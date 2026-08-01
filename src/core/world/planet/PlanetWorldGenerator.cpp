#include "PlanetWorldGenerator.h"

#include <algorithm>
#include <cmath>

#include "planet_utils.h"
#include "core/TracyIntegration.h"

using namespace vp;

namespace {
    /// Hermite ramp between two edges, so the mountain mask opens without a visible seam
    float smoothstep(float edge0, float edge1, float x) {
        if (edge1 <= edge0) return x >= edge1 ? 1.0f : 0.0f;
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

PlanetWorldGenerator::PlanetWorldGenerator() {
}

std::pair<float, float> PlanetWorldGenerator::terrain_height_bounds(const PlanetGenerationConfig &config) {
    // Both layers are normalised to 0..1 before being scaled, so the extremes are simply the
    // sum of the amplitudes. Widen this the day a layer is allowed to go negative.
    const auto lowest = static_cast<float>(config.baseHeight);
    const float highest = lowest
                          + static_cast<float>(config.heightAmplitude)
                          + static_cast<float>(config.mountainAmplitude);
    return {lowest, highest};
}

bool PlanetWorldGenerator::generate_planet_chunk(VoxelChunk &chunk, PlanetNodeCoord coord, const PlanetGenerationConfig &config) {
    VOXEL_ZONE_N("Generate Planet Chunk");
    check_noise_generator(config);

    // Flat terrain phase: the cube face is a plain grid, so node coordinates map straight onto
    // world axes. This has to stay in step with lod_node_corner() in planet_lod_common.glsl and
    // with the positioning block of planet_surface.vert, or the LOD would pick levels against
    // geometry that is not where it thinks it is.
    const int32_t voxelSize = 1 << coord.level;
    const int64_t nodeBottom = static_cast<int64_t>(coord.alt) * CHUNK_SIZE * voxelSize;
    const int64_t nodeTop = nodeBottom + static_cast<int64_t>(CHUNK_SIZE) * voxelSize;

    const auto [groundMinF, groundMaxF] = terrain_height_bounds(config);
    const auto groundMin = static_cast<int64_t>(groundMinF);
    const auto groundMax = static_cast<int64_t>(groundMaxF);

    // Only the metres of ground right under the surface are solid, so the node is worth
    // generating when it overlaps that shell and nothing else. Coarse levels answer this without
    // touching the noise at all, which is what keeps the empty altitude band cheap.
    //
    // Everything below the shell stays air: the world is a surface, not a volume. That holds as
    // long as nothing looks at the terrain from underneath.
    const int64_t shellBottom = groundMin - config.surfaceDepth;

    if (nodeBottom >= groundMax) return false;
    if (nodeTop <= shellBottom) return false;

    // Past the two tests above there is ground to write, so the voxel array is worth its 64 KB.
    // The altitude band the terrain lives in is a thin slice of a root node, so the large
    // majority of the nodes handed to this function never reach this line
    chunk.allocate();

    chunk.textureIDs = {
        {"voxelplanet:textures/grass"_asset, TEXTURE_GRASS},
        {"voxelplanet:textures/cobblestone"_asset, TEXTURE_STONE}
    };

    FastNoise::Generator *baseLayer = base_noise(coord.level, config);
    FastNoise::Generator *ridgeLayer = ridge_noise(coord.level, config);
    if (baseLayer == nullptr || ridgeLayer == nullptr) return false;

    // The sample grid is uniform, so each layer comes out of a single call. Sample spacing is one
    // voxel of this level, which is exactly how the LOD gets its smoothing: GenUniformGrid2D
    // walks (xStart + i) * frequency, so passing voxelSize / wavelength lands the samples on
    // world metres divided by the wavelength, whatever the level.
    std::array<float, CHUNK_SIZE * CHUNK_SIZE> baseOut{};
    std::array<float, CHUNK_SIZE * CHUNK_SIZE> ridgeOut{};

    baseLayer->GenUniformGrid2D(baseOut.data(),
                                coord.u * CHUNK_SIZE, coord.v * CHUNK_SIZE,
                                CHUNK_SIZE, CHUNK_SIZE,
                                static_cast<float>(voxelSize) / config.terrainWavelength,
                                static_cast<int>(config.seed));

    ridgeLayer->GenUniformGrid2D(ridgeOut.data(),
                                 coord.u * CHUNK_SIZE, coord.v * CHUNK_SIZE,
                                 CHUNK_SIZE, CHUNK_SIZE,
                                 static_cast<float>(voxelSize) / config.mountainWavelength,
                                 static_cast<int>(config.seed + RIDGE_SEED_OFFSET));

    const float invVoxelSize = 1.0f / static_cast<float>(voxelSize);

    // Solid depth expressed in this level's voxels. At least one, or a coarse node would have a
    // surface with nothing underneath it.
    const int fillDepth = std::max(1, static_cast<int>(config.surfaceDepth / voxelSize));

    bool anyVoxel = false;

    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            const int index = lx + lz * CHUNK_SIZE;

            const float baseT = std::clamp(baseOut[index] * 0.5f + 0.5f, 0.0f, 1.0f);
            const float ridgeT = std::clamp(ridgeOut[index] * 0.5f + 0.5f, 0.0f, 1.0f);

            // Mountains only grow where the base layer is already high, ramped in smoothly so
            // the range has foothills rather than a wall at the threshold
            const float mask = smoothstep(config.mountainThreshold, 1.0f, baseT);

            // Raising the ridge value to a power is what makes the flanks abrupt: it flattens
            // everything but the crests, so the terrain drops away fast on either side
            const float crest = std::pow(ridgeT, config.ridgeSharpness);

            const float groundHeight = static_cast<float>(config.baseHeight)
                                       + baseT * static_cast<float>(config.heightAmplitude)
                                       + mask * crest * static_cast<float>(config.mountainAmplitude);

            // Ground height in this node's own voxels, measured from its bottom face
            const float localHeight = (groundHeight - static_cast<float>(nodeBottom)) * invVoxelSize;
            const int topVoxel = static_cast<int>(std::floor(localHeight));

            // Solid range of the column, clipped to the node. Both bounds can fall outside it:
            // below when the ground is under this node, above when it is over it.
            const int lastFull = std::min(topVoxel, CHUNK_SIZE);
            const int firstFull = std::max(0, topVoxel - fillDepth);

            for (int ly = firstFull; ly < lastFull; ++ly) {
                chunk.set(lx, ly, lz, {.localTextureID = TEXTURE_STONE, .height = VOXEL_FULL_HEIGHT});
            }

            if (topVoxel >= 0 && topVoxel < CHUNK_SIZE) {
                // Fractional part of the last voxel, on the 4 bits ChunkBlockInfo reserves for
                // it. This is what carries the terrain shape at coarse levels, where a whole
                // mountain fits inside a single voxel. The floor of 1 matters: rounding it to 0
                // would make a coarse node report itself uniform, and the tree would then refuse
                // to subdivide it, erasing the range entirely at distance.
                const int subHeight = std::clamp(
                    static_cast<int>((localHeight - static_cast<float>(topVoxel)) * 16.0f), 1, 15);

                chunk.set(lx, topVoxel, lz,
                          {.localTextureID = TEXTURE_GRASS, .height = static_cast<uint8_t>(subHeight)});
                anyVoxel = true;
            } else if (lastFull > firstFull) {
                // The surface is above the ceiling, but its underside still crosses this node
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

int PlanetWorldGenerator::octaves_for_level(uint8_t level, float wavelength, const PlanetGenerationConfig &config) {
    const auto voxelSize = static_cast<float>(1 << level);
    const float lacunarity = config.lacunarity > 1.0f ? config.lacunarity : 2.0f;

    int usable = 1;
    float current = wavelength;

    // Keep adding octaves while the next one still spans at least two voxels of this level
    while (usable < config.maxOctave && current / lacunarity >= 2.0f * voxelSize) {
        current /= lacunarity;
        ++usable;
    }

    return usable;
}

FastNoise::Generator *PlanetWorldGenerator::base_noise(uint8_t level, const PlanetGenerationConfig &config) {
    const int octaves = std::clamp(octaves_for_level(level, config.terrainWavelength, config), 1, MAX_OCTAVES);

    if (m_baseByOctaves[octaves] == nullptr) {
        VOXEL_ZONE_N("Create Base Noise");
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        fractal->SetSource(FastNoise::New<FastNoise::Simplex>());
        fractal->SetOctaveCount(octaves);
        fractal->SetLacunarity(config.lacunarity);
        fractal->SetGain(config.gain);

        m_baseByOctaves[octaves] = fractal;
    }

    return m_baseByOctaves[octaves].get();
}

FastNoise::Generator *PlanetWorldGenerator::ridge_noise(uint8_t level, const PlanetGenerationConfig &config) {
    const int octaves = std::clamp(octaves_for_level(level, config.mountainWavelength, config), 1, MAX_OCTAVES);

    if (m_ridgeByOctaves[octaves] == nullptr) {
        VOXEL_ZONE_N("Create Ridge Noise");
        auto fractal = FastNoise::New<FastNoise::FractalRidged>();
        fractal->SetSource(FastNoise::New<FastNoise::Simplex>());
        fractal->SetOctaveCount(octaves);
        fractal->SetLacunarity(config.lacunarity);
        fractal->SetGain(config.gain);

        // Scales each octave by the previous one, so detail piles up on the crests and the
        // valleys stay smooth. Without it a ridged fractal is noisy everywhere and the ranges
        // lose their shape.
        fractal->SetWeightedStrength(config.ridgeWeighting);

        m_ridgeByOctaves[octaves] = fractal;
    }

    return m_ridgeByOctaves[octaves].get();
}

float PlanetWorldGenerator::sample_terrain_height(const glm::dvec3 &dir, const PlanetGenerationConfig &config) {
    float nx = static_cast<float>(dir.x) * config.frequency;
    float ny = static_cast<float>(dir.y) * config.frequency;
    float nz = static_cast<float>(dir.z) * config.frequency;

    float noiseVal = base_noise(0, config)->GenSingle3D(nx, ny, nz, static_cast<int>(config.seed));

    float t = (noiseVal + 1.0f) * 0.5f;
    return config.baseHeight + t * static_cast<float>(config.heightAmplitude);
}

void PlanetWorldGenerator::check_noise_generator(const PlanetGenerationConfig &config) {
    if (config == m_cachedConfig) return;

    // Octave count, lacunarity, gain and weighting are all baked into the fractals, so a config
    // change invalidates every level of both layers at once
    for (auto &node: m_baseByOctaves) node.reset();
    for (auto &node: m_ridgeByOctaves) node.reset();
    m_cachedConfig = config;
}
