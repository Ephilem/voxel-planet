#pragma once

#include <cstdint>
#include <unordered_map>

#include "core/log/Logger.h"
#include "core/resource/asset_id.h"
#include "planet_types.h"

namespace vp {

struct PlanetSurfaceChunkGenerator;

struct Planet {
    float radius = 667544.0f;
};

struct PlanetTerrainParams {
    float seaLevel = 0.f;
    float continentAmplitude = 1200.f;
    float continentFrequency = 2.5f;
    float mountainAmplitude = 1200.f;
    float mountainFrequency = 12.f;
    int continentOctave = 5;
    int maxOctave = 10;
    int32_t seed = 1234;
};

struct PlanetChunkLoader {
    uint8_t loadingDistance = 4;
    uint8_t unloadingDistance = 5;
    uint8_t altitudeDistance = 2;

    PlanetSurfaceChunkKey lastCenter;
};

struct PlanetVoxelRegistry {
public:
    BlockID register_block(BlockDefinition def) {
        if (auto it = byAsset.find(def.id); it != byAsset.end())
            return it->second;
        const auto newId = static_cast<BlockID>(blocks.size());
        byAsset.emplace(def.id, newId);
        blocks.push_back(std::move(def));
        LOG_TRACE("PlanetVoxelRegistry", "Registered block {} with ID {}", blocks.back().name,
                  static_cast<uint16_t>(newId));
        return newId;
    }

    [[nodiscard]] BlockID resolve(AssetID id) const {
        if (auto it = byAsset.find(id); it != byAsset.end())
            return it->second;
        return BlockID::Air;
    }

    [[nodiscard]] const BlockDefinition& block(BlockID id) const {
        const auto i = static_cast<size_t>(id);
        return i < blocks.size() ? blocks[i] : blocks[0];
    }

    [[nodiscard]] std::span<const BlockDefinition> get_all() const { return blocks; }

    [[nodiscard]] AssetID asset(BlockID id) const { return block(id).id; }

    [[nodiscard]] bool is_opaque(BlockID id) const { return block(id).opaque; }

    [[nodiscard]] size_t size() const { return blocks.size(); }

private:
    std::vector<BlockDefinition> blocks;
    std::unordered_map<AssetID, BlockID> byAsset;
};

struct PlanetSurfaceChunkGeneratorComp {
    std::unique_ptr<PlanetSurfaceChunkGenerator> generator;
};

} // namespace vp
