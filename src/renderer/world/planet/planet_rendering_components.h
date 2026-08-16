#pragma once
#include <memory>
#include <vector>

#include "planet_rendering_types.h"
#include "PlanetTileGenerator.h"

namespace vp {

class PlanetTileAtlas;
class PlanetTileRenderer;

/**
 * Singleton handles on the objects owned by PlanetRendererModule.
 */
struct PlanetTileAtlasRef {
    PlanetTileAtlas* atlas = nullptr;
    PlanetTileRenderer* renderer = nullptr;
};

struct PlanetTileDrawListComp {
    std::vector<PlanetTileDrawItem> drawItems;
};

struct PlanetTileStreamComp {
    std::unique_ptr<PlanetTileGenerator> generator;
    std::vector<PlanetTileGenerator::PlanetTileResult> drainScratch;
};
} // namespace vp
