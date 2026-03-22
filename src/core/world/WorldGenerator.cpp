#include "WorldGenerator.h"

#include "core/TracyIntegration.h"
#include "core/resource/asset_id.h"

WorldGenerator::WorldGenerator(const int64_t seed) : m_seed(seed) {
    auto simplex = FastNoise::New<FastNoise::Simplex>();

    auto makeFBm = [](int octaves) {
        auto simplex = FastNoise::New<FastNoise::Simplex>();
        auto fbm = FastNoise::New<FastNoise::FractalFBm>();
        fbm->SetSource(simplex);
        fbm->SetOctaveCount(octaves);
        fbm->SetLacunarity(2.0f);
        fbm->SetGain(0.5f);
        return fbm;
    };

    m_roughNoise   = makeFBm(1);
    m_terrainNoise = makeFBm(5);
}

WorldGenerator::~WorldGenerator() = default;

WorldGenerator::ColumnBounds WorldGenerator::evaluate_column(glm::ivec2 col) {
    VOXEL_ZONE_N("EvaluateColumn");

    // Sample m_roughNoise (1 octave simplex) at column center
    const int worldX = col.x * CHUNK_SIZE + CHUNK_SIZE / 2;
    const int worldZ = col.y * CHUNK_SIZE + CHUNK_SIZE / 2;

    float roughSample;
    m_roughNoise->GenUniformGrid2D(&roughSample, worldX, worldZ, 1, 1, m_frequency, m_seed);

    // Lipschitz bound: max rate of change of octave 1 per world unit.
    // m_roughNoise is 1-octave FBm (= raw simplex), output in [-1, 1].
    // m_terrainNoise is 5-octave FBm, gain=0.5, also normalized to [-1, 1].
    // Octave 1 weight in the 5-octave sum = 1.0 / (1+0.5+0.25+0.125+0.0625) = 1/1.9375 ≈ 0.516
    // L_octave1 = 2π × frequency × 0.516 ≈ 0.0324
    //
    // For a single chunk column, the max XZ distance from center to any corner
    // is halfDiag = CHUNK_SIZE × √2 / 2 ≈ 22.6
    // Spatial margin = L × halfDiag ≈ 0.0324 × 22.6 ≈ 0.73
    //
    // Higher octaves (2-5) can contribute up to ±0.484 of the normalized range.
    // Total margin = spatial + high_freq
    constexpr float L_OCTAVE1 = 0.0324f;
    constexpr float HALF_DIAG = CHUNK_SIZE * 0.7071f;
    constexpr float SPATIAL_MARGIN = L_OCTAVE1 * HALF_DIAG;
    constexpr float HIGH_FREQ_MARGIN = 0.484f;
    constexpr float TOTAL_MARGIN = SPATIAL_MARGIN + HIGH_FREQ_MARGIN;

    const float noiseMax = std::min(roughSample + TOTAL_MARGIN,  1.0f);

    return {
        m_baseHeight + static_cast<int>(noiseMax * m_heightAmplitude) + 1
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
        m_frequency,
        m_seed
    );

    chunk.textureIDs = {
        {"voxelplanet:textures/grass"_asset, 1},
        {"voxelplanet:textures/cobblestone"_asset, 2}
    };

    bool hasContent = false;

    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            float noiseValue = heightmap[x + z * CHUNK_SIZE]; // value in [-1, 1]
            int terrainHeight = m_baseHeight + static_cast<int>(noiseValue * m_heightAmplitude);

            for (int y = 0; y < CHUNK_SIZE; y++) {
                int worldYPos = worldY + y;

                if (worldYPos < terrainHeight) {
                    if (worldYPos == terrainHeight - 1) {
                        chunk.at(x, y, z) = 1;
                    } else if (worldYPos > terrainHeight - 2) {
                        chunk.at(x, y, z) = 1;
                    } else {
                        chunk.at(x, y, z) = 2;
                    }
                    hasContent = true;
                } else {
                    chunk.at(x, y, z) = 0;
                }
            }
        }
    }

    return hasContent;
}
