#include "PlanetWorldGenerator.h"

#include <algorithm>
#include <cmath>

#include "planet_utils.h"
#include "core/TracyIntegration.h"

using namespace vp;

PlanetWorldGenerator::PlanetWorldGenerator() {
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

    const auto groundMin = static_cast<int64_t>(config.baseHeight);
    const auto groundMax = static_cast<int64_t>(config.baseHeight + config.heightAmplitude);

    // Nothing to sample when the node sits entirely in the sky. Coarse levels answer this
    // without touching the noise at all, which is what keeps the empty altitude band cheap.
    if (nodeBottom >= groundMax) return false;

    // Fully buried nodes are reported empty too, so the world is a surface shell rather than a
    // solid volume. That is only valid while nothing ever looks at the terrain from below: the
    // day digging shows up, this has to become a solid fill instead.
    if (nodeTop <= groundMin) return false;

    chunk.textureIDs = {
        {"voxelplanet:textures/grass"_asset, TEXTURE_GRASS},
        {"voxelplanet:textures/cobblestone"_asset, TEXTURE_STONE}
    };

    FastNoise::Generator *noise = noise_for_level(coord.level, config);
    if (noise == nullptr) return false;

    // The sample grid is uniform, so the whole chunk comes out of a single call. Sample spacing
    // is one voxel of this level, which is exactly how the LOD gets its smoothing.
    std::array<float, CHUNK_SIZE * CHUNK_SIZE> noiseOut{};
    noise->GenUniformGrid2D(noiseOut.data(),
                            coord.u * CHUNK_SIZE, coord.v * CHUNK_SIZE,
                            CHUNK_SIZE, CHUNK_SIZE,
                            static_cast<float>(voxelSize) / FLAT_TERRAIN_WAVELENGTH,
                            static_cast<int>(config.seed));

    const float invVoxelSize = 1.0f / static_cast<float>(voxelSize);
    bool anyVoxel = false;

    for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
            const float n = noiseOut[lx + lz * CHUNK_SIZE]; // [-1, 1]
            const float groundHeight = static_cast<float>(config.baseHeight)
                                       + (n * 0.5f + 0.5f) * static_cast<float>(config.heightAmplitude);

            // Ground height in this node's own voxels, measured from its bottom face
            const float localHeight = (groundHeight - static_cast<float>(nodeBottom)) * invVoxelSize;
            if (localHeight <= 0.0f) continue;

            const int topVoxel = static_cast<int>(std::floor(localHeight));

            // Fractional part of the last voxel, on the 4 bits ChunkBlockInfo reserves for it.
            // This is what carries the terrain shape at coarse levels, where a whole hill fits
            // inside a single voxel. The floor of 1 matters: rounding it to 0 would make a
            // coarse node report itself uniform, and the tree would then refuse to subdivide it,
            // erasing the terrain entirely at distance.
            const int subHeight = std::clamp(
                static_cast<int>((localHeight - static_cast<float>(topVoxel)) * 16.0f), 1, 15);

            const int fullBlocks = std::clamp(topVoxel, 0, CHUNK_SIZE);
            for (int ly = 0; ly < fullBlocks; ++ly) {
                chunk.set(lx, ly, lz, {.localTextureID = TEXTURE_STONE, .height = VOXEL_FULL_HEIGHT});
            }

            if (topVoxel < CHUNK_SIZE) {
                // The ground lands inside this node, so the top block is the visible surface
                chunk.set(lx, topVoxel, lz,
                          {.localTextureID = TEXTURE_GRASS, .height = static_cast<uint8_t>(subHeight)});
                anyVoxel = true;
            } else if (fullBlocks > 0) {
                // The ground is somewhere above: the column is solid through and through, and
                // the surface belongs to the node overhead
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
    int octaves, const PlanetGenerationConfig &config) {
    VOXEL_ZONE_N("Create Noise Generator");
    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    auto simplex = FastNoise::New<FastNoise::Simplex>();

    fractal->SetSource(simplex);
    fractal->SetOctaveCount(octaves);
    fractal->SetLacunarity(config.lacunarity);
    fractal->SetGain(config.gain);

    return fractal;
}

int PlanetWorldGenerator::octaves_for_level(uint8_t level, const PlanetGenerationConfig &config) {
    const float voxelSize = static_cast<float>(1 << level);
    const float lacunarity = config.lacunarity > 1.0f ? config.lacunarity : 2.0f;

    int usable = 1;
    float wavelength = FLAT_TERRAIN_WAVELENGTH;

    // Keep adding octaves while the next one still spans at least two voxels of this level
    while (usable < config.maxOctave && wavelength / lacunarity >= 2.0f * voxelSize) {
        wavelength /= lacunarity;
        ++usable;
    }

    return usable;
}

FastNoise::Generator *PlanetWorldGenerator::noise_for_level(uint8_t level, const PlanetGenerationConfig &config) {
    const int octaves = std::clamp(octaves_for_level(level, config), 1, MAX_OCTAVES);

    if (m_noiseByOctaves[octaves] == nullptr) {
        m_noiseByOctaves[octaves] = create_noise_generator(octaves, config);
    }

    return m_noiseByOctaves[octaves].get();
}

float PlanetWorldGenerator::sample_terrain_height(const glm::dvec3 &dir, const PlanetGenerationConfig &config) {
    float nx = static_cast<float>(dir.x) * config.frequency;
    float ny = static_cast<float>(dir.y) * config.frequency;
    float nz = static_cast<float>(dir.z) * config.frequency;

    float noiseVal = noise_for_level(0, config)->GenSingle3D(nx, ny, nz, static_cast<int>(config.seed));

    float t = (noiseVal + 1.0f) * 0.5f;
    return config.baseHeight + t * static_cast<float>(config.heightAmplitude);
}

void PlanetWorldGenerator::check_noise_generator(const PlanetGenerationConfig &config) {
    if (config == m_cachedConfig) return;

    // Octave count, lacunarity and gain are all baked into the fractals, so a config change
    // invalidates every level at once
    for (auto &node: m_noiseByOctaves) node.reset();
    m_cachedConfig = config;
}
