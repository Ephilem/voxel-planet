#include "WorldGenerator.h"

#include "planet/planet_utils.h"
#include "core/TracyIntegration.h"
#include "core/resource/asset_id.h"

WorldGenerator::WorldGenerator(const int64_t seed) {
    m_params.seed = seed;
    rebuild_noise();
}

WorldGenerator::~WorldGenerator() = default;

void WorldGenerator::rebuild_noise() {
    auto makeFBm = [&](int octaves) {
        auto simplex = FastNoise::New<FastNoise::Simplex>();
        auto fbm = FastNoise::New<FastNoise::FractalFBm>();
        fbm->SetSource(simplex);
        fbm->SetOctaveCount(octaves);
        fbm->SetLacunarity(m_params.lacunarity);
        fbm->SetGain(m_params.gain);
        return fbm;
    };

    m_roughNoise   = makeFBm(1);
    m_terrainNoise = makeFBm(m_params.octaves);
}

void WorldGenerator::set_params(const NoiseParams& params) {
    m_params = params;
    rebuild_noise();
}

void WorldGenerator::sample_heightmap(int worldX, int worldZ, int width, int height, float* outHeights) const {
    std::vector<float> noise(width * height);
    m_terrainNoise->GenUniformGrid2D(
        noise.data(),
        worldX, worldZ,
        width, height,
        m_params.frequency,
        m_params.seed
    );

    for (int i = 0; i < width * height; i++) {
        outHeights[i] = static_cast<float>(m_params.baseHeight) + noise[i] * static_cast<float>(m_params.heightAmplitude);
    }
}

WorldGenerator::ColumnBounds WorldGenerator::evaluate_column(glm::ivec2 col) {
    VOXEL_ZONE_N("EvaluateColumn");

    // Sample m_roughNoise (1 octave simplex) at column center
    const int worldX = col.x * CHUNK_SIZE + CHUNK_SIZE / 2;
    const int worldZ = col.y * CHUNK_SIZE + CHUNK_SIZE / 2;

    float roughSample;
    m_roughNoise->GenUniformGrid2D(&roughSample, worldX, worldZ, 1, 1, m_params.frequency, m_params.seed);

    // Lipschitz bound: max FBm value anywhere in the chunk, given the center sample.
    //
    // W = sum of octave weights = (1 - gain^n) / (1 - gain)  [geometric series]
    // L_simplex_2D ≈ 2π × frequency  (Lipschitz constant of 2D simplex per world unit)
    // L_octave1_in_fbm = L_simplex_2D / W  (octave 1 weight in normalized FBm)
    // spatial_margin   = L_octave1_in_fbm × halfDiag  (max variation of octave 1 across chunk)
    // high_freq_margin = 1 - 1/W  (max contribution of octaves 2..n)
    // total_margin     = spatial_margin + high_freq_margin
    //
    // Note: K_SIMPLEX_2D ≈ 2π is a theoretical upper bound. FastNoise2 does not
    // expose its exact Lipschitz constant, so this bound is conservative (never too tight).
    const float W = (m_params.gain == 1.0f)
        ? static_cast<float>(m_params.octaves)
        : (1.0f - std::pow(m_params.gain, m_params.octaves)) / (1.0f - m_params.gain);
    constexpr float K_SIMPLEX_2D = 6.2832f; // 2π
    constexpr float HALF_DIAG = CHUNK_SIZE * 0.7071f;
    const float L_octave1_in_fbm = (K_SIMPLEX_2D * m_params.frequency) / W;
    const float SPATIAL_MARGIN   = L_octave1_in_fbm * HALF_DIAG;
    const float HIGH_FREQ_MARGIN = 1.0f - (1.0f / W);
    const float TOTAL_MARGIN     = SPATIAL_MARGIN + HIGH_FREQ_MARGIN;

    const float noiseMax = std::min(roughSample + TOTAL_MARGIN,  1.0f);

    return {
        m_params.baseHeight + static_cast<int>(noiseMax * m_params.heightAmplitude) + 1
    };
}

bool WorldGenerator::generate_chunk(VoxelChunk &chunk, glm::ivec3 chunkPosition) {
    VOXEL_ZONE_N("Generate Chunk");

    int worldX = chunkPosition.x * CHUNK_SIZE;
    int worldY = chunkPosition.y * CHUNK_SIZE;
    int worldZ = chunkPosition.z * CHUNK_SIZE;

    std::vector<float> heightmap(CHUNK_SIZE * CHUNK_SIZE);
    m_terrainNoise->GenUniformGrid2D(
        heightmap.data(),
        worldX, worldZ,
        CHUNK_SIZE, CHUNK_SIZE,
        m_params.frequency,
        m_params.seed
    );

    chunk.textureIDs = {
        {"voxelplanet:textures/grass"_asset, 1},
        {"voxelplanet:textures/cobblestone"_asset, 2}
    };

    bool hasContent = false;

    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            float noiseValue = heightmap[x + z * CHUNK_SIZE]; // [-1, 1]

            // Ex: baseHeight=20, amplitude=10 -> range [10*16, 30*16]
            float terrainHeightF = m_params.baseHeight + noiseValue * m_params.heightAmplitude;
            int terrainHeightSubvoxel = static_cast<int>(terrainHeightF * 16.0f);

            int surfaceBlockY = terrainHeightSubvoxel / 16;
            int surfaceSubHeight = terrainHeightSubvoxel % 16;

            for (int y = 0; y < CHUNK_SIZE; y++) {
                int worldYPos = worldY + y;
                ChunkBlockInfo info{};

                if (worldYPos < surfaceBlockY - 1) {
                    // Below the second grass layer -> solid
                    info.localTextureID = 2; // cobblestone
                    info.height = 15;
                    hasContent = true;
                } else if (worldYPos == surfaceBlockY - 1) {
                    // Second layer of grass (full block)
                    info.localTextureID = 1; // grass
                    info.height = 15;
                    hasContent = true;
                } else if (worldYPos == surfaceBlockY) {
                    // Surface block
                    if (surfaceSubHeight == 0) {
                        info.localTextureID = 0; // air
                        info.height = 0;
                    } else {
                        info.localTextureID = 1; // grass
                        info.height = surfaceSubHeight - 1; // 0-15
                        hasContent = true;
                    }
                } else {
                    // Above the surface -> empty
                    info.localTextureID = 0;
                    info.height = 0;
                }

                chunk.set(x, y, z, info);
            }
        }
    }


    return hasContent;
}
