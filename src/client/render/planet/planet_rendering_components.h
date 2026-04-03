#pragma once
#include <memory>
#include <vector>

#include "client/render/planet/planet_rendering_types.h"
#include "client/render/planet/tile/PlanetTileGenerator.h"
#include "client/render/planet/tile/PlanetTileQuadtrees.h"

namespace vp::client {

class PlanetTileAtlas;
class PlanetTileRenderer;
class PlanetSurfaceChunkMesher;
class PlanetSurfaceChunkRenderer;

/**
 * Singleton handles on the objects owned by PlanetRenderModule.
 */
struct PlanetTileAtlasRef {
    PlanetTileAtlas* atlas = nullptr;
    PlanetTileRenderer* renderer = nullptr;
};

struct PlanetSurfaceChunkRenderingRef {
    PlanetSurfaceChunkMesher* mesher = nullptr;
    PlanetSurfaceChunkRenderer* renderer = nullptr;
};

struct PlanetTileLodComp {
    std::unique_ptr<PlanetTileQuadtrees> quadtree;
    PlanetLodParams params;

    bool frustumCulling = true;

    bool debugDrawNodes = false;
    PlanetTileQuadtrees::DebugMode debugMode = PlanetTileQuadtrees::DebugMode::Level;
    int debugSegmentsPerEdge = 6;
};

struct PlanetTileDrawListComp {
    std::vector<PlanetTileDrawItem> drawItems;
};

struct PlanetTileStreamComp {
    std::unique_ptr<PlanetTileGenerator> generator;
    std::vector<PlanetTileGenerator::PlanetTileResult> drainScratch;
};

} // namespace vp::client