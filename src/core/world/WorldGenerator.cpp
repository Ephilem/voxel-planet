#include "WorldGenerator.h"

#include "core/resource/asset_id.h"

WorldGenerator::WorldGenerator(const int64_t seed) : m_seed(seed) {
    auto simplex = FastNoise::New<FastNoise::Simplex>();

    m_terrainNoise = FastNoise::New<FastNoise::FractalFBm>();
    m_terrainNoise->SetSource(simplex);
    m_terrainNoise->SetOctaveCount(5);
    m_terrainNoise->SetLacunarity(2.0f);
    m_terrainNoise->SetGain(0.5f);
}

WorldGenerator::~WorldGenerator() = default;

bool WorldGenerator::generate_chunk(VoxelChunk &chunk, glm::ivec3 chunkPosition) {
    constexpr float frequency = 0.01f;
    constexpr int baseHeight = 100;
    constexpr int heightAmplitude = 32;

    int worldX = chunkPosition.x * CHUNK_SIZE;
    int worldY = chunkPosition.y * CHUNK_SIZE;
    int worldZ = chunkPosition.z * CHUNK_SIZE;

    std::vector<float> heightmap(CHUNK_SIZE * CHUNK_SIZE);
    m_terrainNoise->GenUniformGrid2D(
        heightmap.data(),
        worldX, worldZ,
        CHUNK_SIZE, CHUNK_SIZE,
        frequency,
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
            int terrainHeight = baseHeight + static_cast<int>(noiseValue * heightAmplitude);

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
