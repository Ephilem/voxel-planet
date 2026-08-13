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
 *
 * Both live in unique_ptrs the module never publishes, so debug panels on the client side have
 * no way to read atlas residency or slot resolution. These borrow them, non-owning: they stay
 * valid exactly as long as the module does
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
